// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusUIHandler.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "WidgetBlueprint.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "Components/Widget.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "WidgetBlueprintFactory.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusUIHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("create_widget_blueprint"))       return HandleCreateWidgetBlueprint(Params);
    if (SubCommand == TEXT("create_widget_animation"))        return HandleCreateWidgetAnimation(Params);
    if (SubCommand == TEXT("add_widget_to_viewport"))         return HandleAddWidgetToViewport(Params);
    if (SubCommand == TEXT("remove_widget_from_viewport"))    return HandleRemoveWidgetFromViewport(Params);
    if (SubCommand == TEXT("set_widget_property"))            return HandleSetWidgetProperty(Params);
    if (SubCommand == TEXT("get_widget_info"))                return HandleGetWidgetInfo(Params);
    if (SubCommand == TEXT("list_widget_bindings"))           return HandleListWidgetBindings(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// umg.create_widget_blueprint
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusUIHandler::HandleCreateWidgetBlueprint(
    const TSharedPtr<FJsonObject>& Params)
{
    FString WidgetName = GetStringParam(Params, TEXT("widget_name"));
    if (WidgetName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("widget_name is required"));
    }

    FString DestFolder = GetStringParam(Params, TEXT("destination_folder"), TEXT("/Game/UI"));
    FString ParentClassName = GetStringParam(Params, TEXT("parent_class"), TEXT("UserWidget"));
    FString Template = GetStringParam(Params, TEXT("template"));
    int32 Width = static_cast<int32>(GetNumberParam(Params, TEXT("width"), 1920.0));
    int32 Height = static_cast<int32>(GetNumberParam(Params, TEXT("height"), 1080.0));

    // Resolve parent class — default to UUserWidget
    UClass* ParentClass = UUserWidget::StaticClass();
    if (!ParentClassName.IsEmpty() && ParentClassName != TEXT("UserWidget"))
    {
        UClass* FoundClass = FindObject<UClass>(nullptr, *ParentClassName);
        if (!FoundClass)
        {
            // Try with full path prefix
            FString FullPath = FString::Printf(TEXT("/Script/UMG.%s"), *ParentClassName);
            FoundClass = FindObject<UClass>(nullptr, *FullPath);
        }
        if (FoundClass && FoundClass->IsChildOf(UUserWidget::StaticClass()))
        {
            ParentClass = FoundClass;
        }
    }

    // Create Widget Blueprint via factory
    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(
        "AssetTools").Get();

    UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
    Factory->ParentClass = ParentClass;

    UObject* NewAsset = AssetTools.CreateAsset(
        WidgetName, DestFolder, UWidgetBlueprint::StaticClass(), Factory);
    if (!NewAsset)
    {
        return MakeError(TEXT("CREATION_FAILED"),
            FString::Printf(TEXT("Failed to create Widget Blueprint '%s' in '%s'"),
                *WidgetName, *DestFolder));
    }

    UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(NewAsset);
    if (!WidgetBP)
    {
        return MakeError(TEXT("CREATION_FAILED"),
            TEXT("Created asset is not a Widget Blueprint"));
    }

    // Set design-time canvas size on the CDO of the generated class
#if WITH_EDITORONLY_DATA
    if (UWidgetBlueprintGeneratedClass* GenClass = Cast<UWidgetBlueprintGeneratedClass>(WidgetBP->GeneratedClass))
    {
        if (UUserWidget* CDO = Cast<UUserWidget>(GenClass->GetDefaultObject()))
        {
            CDO->DesignSizeMode = EDesignPreviewSizeMode::Custom;
            CDO->DesignTimeSize = FVector2D(Width, Height);
        }
    }
#endif

    // Apply template if specified — add a root CanvasPanel
    if (!Template.IsEmpty() && WidgetBP->WidgetTree)
    {
        UCanvasPanel* RootCanvas = WidgetBP->WidgetTree->ConstructWidget<UCanvasPanel>(
            UCanvasPanel::StaticClass(), FName(TEXT("RootCanvas")));
        if (RootCanvas)
        {
            WidgetBP->WidgetTree->RootWidget = RootCanvas;
        }
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
    NewAsset->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("asset_path"), NewAsset->GetPathName());
    Data->SetStringField(TEXT("widget_name"), WidgetName);
    Data->SetStringField(TEXT("parent_class"), ParentClass->GetName());
    Data->SetStringField(TEXT("template"), Template);
    Data->SetNumberField(TEXT("width"), Width);
    Data->SetNumberField(TEXT("height"), Height);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// umg.create_widget_animation
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusUIHandler::HandleCreateWidgetAnimation(
    const TSharedPtr<FJsonObject>& Params)
{
    FString WidgetPath = GetStringParam(Params, TEXT("widget_path"));
    if (WidgetPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("widget_path is required"));
    }

    FString AnimName = GetStringParam(Params, TEXT("animation_name"));
    if (AnimName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("animation_name is required"));
    }

    FString TargetWidgetName = GetStringParam(Params, TEXT("target_widget_name"));
    if (TargetWidgetName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("target_widget_name is required"));
    }

    FString PropertyName = GetStringParam(Params, TEXT("property_name"), TEXT("RenderOpacity"));
    double StartValue = GetNumberParam(Params, TEXT("start_value"), 0.0);
    double EndValue = GetNumberParam(Params, TEXT("end_value"), 1.0);
    double Duration = GetNumberParam(Params, TEXT("duration"), 0.5);
    FString Easing = GetStringParam(Params, TEXT("easing"), TEXT("EaseInOut"));
    bool bLoop = GetBoolParam(Params, TEXT("loop"), false);

    // Load the Widget Blueprint
    UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetPath);
    if (!WidgetBP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Widget Blueprint not found at '%s'"), *WidgetPath));
    }

    // Check that the target widget exists in the hierarchy
    UWidget* TargetWidget = nullptr;
    if (WidgetBP->WidgetTree)
    {
        TargetWidget = WidgetBP->WidgetTree->FindWidget(FName(*TargetWidgetName));
    }
    if (!TargetWidget)
    {
        return MakeError(TEXT("WIDGET_NOT_FOUND"),
            FString::Printf(TEXT("Child widget '%s' not found in Widget Blueprint"), *TargetWidgetName));
    }

    // Create the widget animation
    // In UE5, widget animations are UWidgetAnimation objects stored on the WidgetBlueprint
    UWidgetAnimation* NewAnim = NewObject<UWidgetAnimation>(
        WidgetBP, FName(*AnimName), RF_Transactional);
    if (!NewAnim)
    {
        return MakeError(TEXT("CREATION_FAILED"),
            FString::Printf(TEXT("Failed to create animation '%s'"), *AnimName));
    }

    // Configure the movie scene for the animation
    UMovieScene* MovieScene = NewAnim->GetMovieScene();
    if (MovieScene)
    {
        // Set playback range based on duration
        FFrameRate TickResolution = MovieScene->GetTickResolution();
        FFrameNumber StartFrame = FFrameNumber(0);
        FFrameNumber EndFrame = (Duration * TickResolution).FloorToFrame();
        MovieScene->SetPlaybackRange(
            TRange<FFrameNumber>(StartFrame, EndFrame));

        // Set display name
        MovieScene->SetDisplayRate(FFrameRate(30, 1));
    }

    // Add animation to the blueprint's animations list
    WidgetBP->Animations.Add(NewAnim);

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
    WidgetBP->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("widget_path"), WidgetPath);
    Data->SetStringField(TEXT("animation_name"), AnimName);
    Data->SetStringField(TEXT("target_widget_name"), TargetWidgetName);
    Data->SetStringField(TEXT("property_name"), PropertyName);
    Data->SetNumberField(TEXT("start_value"), StartValue);
    Data->SetNumberField(TEXT("end_value"), EndValue);
    Data->SetNumberField(TEXT("duration"), Duration);
    Data->SetStringField(TEXT("easing"), Easing);
    Data->SetBoolField(TEXT("loop"), bLoop);
    Data->SetNumberField(TEXT("animation_count"), WidgetBP->Animations.Num());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// umg.add_widget_to_viewport
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusUIHandler::HandleAddWidgetToViewport(
    const TSharedPtr<FJsonObject>& Params)
{
    FString WidgetPath = GetStringParam(Params, TEXT("widget_path"));
    if (WidgetPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("widget_path is required"));
    }

    int32 ZOrder = static_cast<int32>(GetNumberParam(Params, TEXT("z_order"), 0.0));
    int32 PlayerIndex = static_cast<int32>(GetNumberParam(Params, TEXT("player_index"), 0.0));

    // Ensure we're in PIE or have a game viewport
    UWorld* World = GEditor ? GEditor->GetPIEWorldContext()
        ? GEditor->GetPIEWorldContext()->World() : nullptr : nullptr;
    if (!World)
    {
        return MakeError(TEXT("NO_PIE"),
            TEXT("Play-In-Editor must be active to add widgets to viewport. Start PIE first."));
    }

    // Load the Widget Blueprint class
    UClass* WidgetClass = LoadClass<UUserWidget>(nullptr, *WidgetPath);
    if (!WidgetClass)
    {
        // Try loading the Blueprint and getting its generated class
        UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetPath);
        if (WidgetBP)
        {
            WidgetClass = WidgetBP->GeneratedClass;
        }
    }
    if (!WidgetClass)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Widget class not found at '%s'"), *WidgetPath));
    }

    // Get the player controller
    APlayerController* PC = nullptr;
    if (PlayerIndex == 0)
    {
        PC = World->GetFirstPlayerController();
    }
    else
    {
        int32 Idx = 0;
        for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
        {
            if (Idx == PlayerIndex)
            {
                PC = It->Get();
                break;
            }
            Idx++;
        }
    }
    if (!PC)
    {
        return MakeError(TEXT("NO_PLAYER"),
            FString::Printf(TEXT("No player controller found for player_index %d"), PlayerIndex));
    }

    // Create and add the widget
    UUserWidget* WidgetInstance = CreateWidget<UUserWidget>(PC, WidgetClass);
    if (!WidgetInstance)
    {
        return MakeError(TEXT("CREATION_FAILED"),
            TEXT("Failed to create widget instance"));
    }

    WidgetInstance->AddToViewport(ZOrder);

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("widget_path"), WidgetPath);
    Data->SetStringField(TEXT("widget_class"), WidgetClass->GetName());
    Data->SetStringField(TEXT("instance_id"),
        FString::Printf(TEXT("%llu"), (uint64)WidgetInstance));
    Data->SetNumberField(TEXT("z_order"), ZOrder);
    Data->SetNumberField(TEXT("player_index"), PlayerIndex);
    Data->SetBoolField(TEXT("is_in_viewport"), WidgetInstance->IsInViewport());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// umg.remove_widget_from_viewport
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusUIHandler::HandleRemoveWidgetFromViewport(
    const TSharedPtr<FJsonObject>& Params)
{
    FString WidgetPath = GetStringParam(Params, TEXT("widget_path"));
    FString InstanceId = GetStringParam(Params, TEXT("widget_instance_id"));
    bool bRemoveAll = GetBoolParam(Params, TEXT("remove_all"), false);
    int32 PlayerIndex = static_cast<int32>(GetNumberParam(Params, TEXT("player_index"), 0.0));

    // Ensure we're in PIE
    UWorld* World = GEditor ? GEditor->GetPIEWorldContext()
        ? GEditor->GetPIEWorldContext()->World() : nullptr : nullptr;
    if (!World)
    {
        return MakeError(TEXT("NO_PIE"),
            TEXT("Play-In-Editor must be active to manage viewport widgets."));
    }

    // Get the player controller
    APlayerController* PC = nullptr;
    if (PlayerIndex == 0)
    {
        PC = World->GetFirstPlayerController();
    }
    else
    {
        int32 Idx = 0;
        for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
        {
            if (Idx == PlayerIndex)
            {
                PC = It->Get();
                break;
            }
            Idx++;
        }
    }
    if (!PC)
    {
        return MakeError(TEXT("NO_PLAYER"),
            FString::Printf(TEXT("No player controller found for player_index %d"), PlayerIndex));
    }

    int32 RemovedCount = 0;

    if (bRemoveAll)
    {
        // Remove all widgets — iterate all UUserWidget objects in the world
        for (TObjectIterator<UUserWidget> It; It; ++It)
        {
            UUserWidget* Widget = *It;
            if (Widget && Widget->IsInViewport() && Widget->GetWorld() == World)
            {
                Widget->RemoveFromParent();
                RemovedCount++;
            }
        }
    }
    else if (!InstanceId.IsEmpty())
    {
        // Remove by instance pointer ID
        uint64 PtrVal = FCString::Strtoui64(*InstanceId, nullptr, 10);
        UUserWidget* Widget = reinterpret_cast<UUserWidget*>(PtrVal);
        if (Widget && IsValid(Widget) && Widget->IsInViewport())
        {
            Widget->RemoveFromParent();
            RemovedCount = 1;
        }
        else
        {
            return MakeError(TEXT("NOT_FOUND"),
                FString::Printf(TEXT("No valid widget instance found for id '%s'"), *InstanceId));
        }
    }
    else if (!WidgetPath.IsEmpty())
    {
        // Remove all instances of the specified widget class
        UClass* WidgetClass = LoadClass<UUserWidget>(nullptr, *WidgetPath);
        if (!WidgetClass)
        {
            UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetPath);
            if (WidgetBP)
            {
                WidgetClass = WidgetBP->GeneratedClass;
            }
        }
        if (!WidgetClass)
        {
            return MakeError(TEXT("NOT_FOUND"),
                FString::Printf(TEXT("Widget class not found at '%s'"), *WidgetPath));
        }

        for (TObjectIterator<UUserWidget> It; It; ++It)
        {
            UUserWidget* Widget = *It;
            if (Widget && Widget->IsInViewport() && Widget->GetWorld() == World
                && Widget->IsA(WidgetClass))
            {
                Widget->RemoveFromParent();
                RemovedCount++;
            }
        }
    }
    else
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("Provide widget_path, widget_instance_id, or set remove_all=true"));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetNumberField(TEXT("removed_count"), RemovedCount);
    Data->SetBoolField(TEXT("remove_all"), bRemoveAll);
    Data->SetNumberField(TEXT("player_index"), PlayerIndex);
    if (!WidgetPath.IsEmpty())
    {
        Data->SetStringField(TEXT("widget_path"), WidgetPath);
    }
    if (!InstanceId.IsEmpty())
    {
        Data->SetStringField(TEXT("widget_instance_id"), InstanceId);
    }
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// umg.set_widget_property
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusUIHandler::HandleSetWidgetProperty(
    const TSharedPtr<FJsonObject>& Params)
{
    FString WidgetPath = GetStringParam(Params, TEXT("widget_path"));
    if (WidgetPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("widget_path is required"));
    }

    FString WidgetName = GetStringParam(Params, TEXT("widget_name"));
    if (WidgetName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("widget_name is required"));
    }

    FString PropertyName = GetStringParam(Params, TEXT("property_name"));
    if (PropertyName.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("property_name is required"));
    }

    FString Value = GetStringParam(Params, TEXT("value"));
    FString ValueType = GetStringParam(Params, TEXT("value_type"), TEXT("String"));
    FString InstanceId = GetStringParam(Params, TEXT("instance_id"));

    // For design-time editing, modify the widget blueprint asset
    UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetPath);
    if (!WidgetBP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Widget Blueprint not found at '%s'"), *WidgetPath));
    }

    // Find the target child widget in the widget tree
    UWidget* TargetWidget = nullptr;
    if (WidgetBP->WidgetTree)
    {
        TargetWidget = WidgetBP->WidgetTree->FindWidget(FName(*WidgetName));
    }
    if (!TargetWidget)
    {
        return MakeError(TEXT("WIDGET_NOT_FOUND"),
            FString::Printf(TEXT("Child widget '%s' not found in Widget Blueprint"), *WidgetName));
    }

    bool bPropertySet = false;

    // Handle known property types directly for common widgets
    if (PropertyName == TEXT("Text"))
    {
        UTextBlock* TextBlock = Cast<UTextBlock>(TargetWidget);
        if (TextBlock)
        {
            TextBlock->SetText(FText::FromString(Value));
            bPropertySet = true;
        }
    }
    else if (PropertyName == TEXT("Visibility"))
    {
        ESlateVisibility NewVis = ESlateVisibility::Visible;
        if (Value == TEXT("Collapsed")) NewVis = ESlateVisibility::Collapsed;
        else if (Value == TEXT("Hidden")) NewVis = ESlateVisibility::Hidden;
        else if (Value == TEXT("HitTestInvisible")) NewVis = ESlateVisibility::HitTestInvisible;
        else if (Value == TEXT("SelfHitTestInvisible")) NewVis = ESlateVisibility::SelfHitTestInvisible;
        TargetWidget->SetVisibility(NewVis);
        bPropertySet = true;
    }
    else if (PropertyName == TEXT("RenderOpacity"))
    {
        float Opacity = FCString::Atof(*Value);
        TargetWidget->SetRenderOpacity(Opacity);
        bPropertySet = true;
    }
    else if (PropertyName == TEXT("IsEnabled"))
    {
        bool bEnabled = Value.ToBool();
        TargetWidget->SetIsEnabled(bEnabled);
        bPropertySet = true;
    }
    else if (PropertyName == TEXT("ToolTipText"))
    {
        TargetWidget->SetToolTipText(FText::FromString(Value));
        bPropertySet = true;
    }
    else if (PropertyName == TEXT("Percent"))
    {
        UProgressBar* ProgressBar = Cast<UProgressBar>(TargetWidget);
        if (ProgressBar)
        {
            float Percent = FCString::Atof(*Value);
            ProgressBar->SetPercent(Percent);
            bPropertySet = true;
        }
    }
    else if (PropertyName == TEXT("ColorAndOpacity"))
    {
        // Parse "r,g,b,a" format
        TArray<FString> Parts;
        Value.ParseIntoArray(Parts, TEXT(","), true);
        FLinearColor Color = FLinearColor::White;
        if (Parts.Num() >= 3)
        {
            Color.R = FCString::Atof(*Parts[0].TrimStartAndEnd());
            Color.G = FCString::Atof(*Parts[1].TrimStartAndEnd());
            Color.B = FCString::Atof(*Parts[2].TrimStartAndEnd());
            if (Parts.Num() >= 4)
            {
                Color.A = FCString::Atof(*Parts[3].TrimStartAndEnd());
            }
        }
        // SetColorAndOpacity is only available on specific widget types
        if (UTextBlock* TextBlock = Cast<UTextBlock>(TargetWidget))
        {
            TextBlock->SetColorAndOpacity(FSlateColor(Color));
            bPropertySet = true;
        }
        else if (UImage* Image = Cast<UImage>(TargetWidget))
        {
            Image->SetColorAndOpacity(Color);
            bPropertySet = true;
        }
        else if (UButton* Button = Cast<UButton>(TargetWidget))
        {
            Button->SetColorAndOpacity(Color);
            bPropertySet = true;
        }
    }

    // Fallback: use property reflection for unknown properties
    if (!bPropertySet)
    {
        FProperty* Prop = TargetWidget->GetClass()->FindPropertyByName(FName(*PropertyName));
        if (Prop)
        {
            void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(TargetWidget);
            if (ValueType == TEXT("Float"))
            {
                if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
                {
                    FloatProp->SetPropertyValue(ValuePtr, FCString::Atof(*Value));
                    bPropertySet = true;
                }
                else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
                {
                    DoubleProp->SetPropertyValue(ValuePtr, FCString::Atod(*Value));
                    bPropertySet = true;
                }
            }
            else if (ValueType == TEXT("Int"))
            {
                if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
                {
                    IntProp->SetPropertyValue(ValuePtr, FCString::Atoi(*Value));
                    bPropertySet = true;
                }
            }
            else if (ValueType == TEXT("Bool"))
            {
                if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
                {
                    BoolProp->SetPropertyValue(ValuePtr, Value.ToBool());
                    bPropertySet = true;
                }
            }
            else if (ValueType == TEXT("String"))
            {
                if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
                {
                    StrProp->SetPropertyValue(ValuePtr, Value);
                    bPropertySet = true;
                }
                else if (FTextProperty* TextProp = CastField<FTextProperty>(Prop))
                {
                    TextProp->SetPropertyValue(ValuePtr, FText::FromString(Value));
                    bPropertySet = true;
                }
            }
        }
    }

    if (!bPropertySet)
    {
        return MakeError(TEXT("PROPERTY_NOT_SET"),
            FString::Printf(TEXT("Could not set property '%s' on widget '%s' (type: %s)"),
                *PropertyName, *WidgetName, *TargetWidget->GetClass()->GetName()));
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
    WidgetBP->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("widget_path"), WidgetPath);
    Data->SetStringField(TEXT("widget_name"), WidgetName);
    Data->SetStringField(TEXT("property_name"), PropertyName);
    Data->SetStringField(TEXT("value"), Value);
    Data->SetStringField(TEXT("value_type"), ValueType);
    Data->SetStringField(TEXT("widget_type"), TargetWidget->GetClass()->GetName());
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// umg.get_widget_info
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusUIHandler::HandleGetWidgetInfo(
    const TSharedPtr<FJsonObject>& Params)
{
    FString WidgetPath = GetStringParam(Params, TEXT("widget_path"));
    FString InstanceId = GetStringParam(Params, TEXT("instance_id"));
    bool bIncludeChildren = GetBoolParam(Params, TEXT("include_children"), true);
    bool bIncludeAnimations = GetBoolParam(Params, TEXT("include_animations"), true);
    int32 PlayerIndex = static_cast<int32>(GetNumberParam(Params, TEXT("player_index"), 0.0));

    if (WidgetPath.IsEmpty() && InstanceId.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("Either widget_path or instance_id is required"));
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());

    // Case 1: Inspect a live widget instance
    if (!InstanceId.IsEmpty())
    {
        uint64 PtrVal = FCString::Strtoui64(*InstanceId, nullptr, 10);
        UUserWidget* Widget = reinterpret_cast<UUserWidget*>(PtrVal);
        if (!Widget || !IsValid(Widget))
        {
            return MakeError(TEXT("NOT_FOUND"),
                FString::Printf(TEXT("No valid widget instance found for id '%s'"), *InstanceId));
        }

        Data->SetStringField(TEXT("instance_id"), InstanceId);
        Data->SetStringField(TEXT("widget_class"), Widget->GetClass()->GetName());
        Data->SetBoolField(TEXT("is_in_viewport"), Widget->IsInViewport());
        Data->SetBoolField(TEXT("is_visible"),
            Widget->GetVisibility() == ESlateVisibility::Visible);
        Data->SetNumberField(TEXT("render_opacity"), Widget->GetRenderOpacity());
        Data->SetStringField(TEXT("source"), TEXT("instance"));

        if (bIncludeChildren && Widget->WidgetTree)
        {
            TArray<TSharedPtr<FJsonValue>> ChildArr;
            Widget->WidgetTree->ForEachWidget([&](UWidget* Child)
            {
                TSharedPtr<FJsonObject> ChildObj = MakeShareable(new FJsonObject());
                ChildObj->SetStringField(TEXT("name"), Child->GetName());
                ChildObj->SetStringField(TEXT("type"), Child->GetClass()->GetName());
                ChildObj->SetStringField(TEXT("visibility"),
                    StaticEnum<ESlateVisibility>()->GetNameStringByValue(
                        static_cast<int64>(Child->GetVisibility())));
                ChildObj->SetBoolField(TEXT("is_enabled"), Child->GetIsEnabled());
                ChildArr.Add(MakeShareable(new FJsonValueObject(ChildObj)));
            });
            Data->SetArrayField(TEXT("children"), ChildArr);
            Data->SetNumberField(TEXT("child_count"), ChildArr.Num());
        }

        return MakeSuccess(Data);
    }

    // Case 2: Inspect a Widget Blueprint asset
    UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetPath);
    if (!WidgetBP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Widget Blueprint not found at '%s'"), *WidgetPath));
    }

    Data->SetStringField(TEXT("asset_path"), WidgetBP->GetPathName());
    Data->SetStringField(TEXT("widget_name"), WidgetBP->GetName());
    Data->SetStringField(TEXT("parent_class"),
        WidgetBP->ParentClass ? WidgetBP->ParentClass->GetName() : TEXT("Unknown"));
    Data->SetStringField(TEXT("source"), TEXT("asset"));

    // Include child widgets hierarchy
    if (bIncludeChildren && WidgetBP->WidgetTree)
    {
        TArray<TSharedPtr<FJsonValue>> ChildArr;
        WidgetBP->WidgetTree->ForEachWidget([&](UWidget* Child)
        {
            TSharedPtr<FJsonObject> ChildObj = MakeShareable(new FJsonObject());
            ChildObj->SetStringField(TEXT("name"), Child->GetName());
            ChildObj->SetStringField(TEXT("type"), Child->GetClass()->GetName());
            ChildObj->SetBoolField(TEXT("is_variable"), Child->bIsVariable);
            ChildArr.Add(MakeShareable(new FJsonValueObject(ChildObj)));
        });
        Data->SetArrayField(TEXT("children"), ChildArr);
        Data->SetNumberField(TEXT("child_count"), ChildArr.Num());
    }

    // Include animations
    if (bIncludeAnimations)
    {
        TArray<TSharedPtr<FJsonValue>> AnimArr;
        for (UWidgetAnimation* Anim : WidgetBP->Animations)
        {
            if (!Anim) continue;

            TSharedPtr<FJsonObject> AnimObj = MakeShareable(new FJsonObject());
            AnimObj->SetStringField(TEXT("name"), Anim->GetName());

            // Get animation duration from movie scene
            UMovieScene* MovieScene = Anim->GetMovieScene();
            if (MovieScene)
            {
                FFrameRate TickRes = MovieScene->GetTickResolution();
                TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
                double DurationSec = 0.0;
                if (PlaybackRange.HasLowerBound() && PlaybackRange.HasUpperBound())
                {
                    FFrameNumber Start = PlaybackRange.GetLowerBoundValue();
                    FFrameNumber End = PlaybackRange.GetUpperBoundValue();
                    DurationSec = (End - Start).Value / static_cast<double>(TickRes.Numerator);
                }
                AnimObj->SetNumberField(TEXT("duration"), DurationSec);
                AnimObj->SetNumberField(TEXT("track_count"),
                    MovieScene->GetTracks().Num());
            }
            AnimArr.Add(MakeShareable(new FJsonValueObject(AnimObj)));
        }
        Data->SetArrayField(TEXT("animations"), AnimArr);
        Data->SetNumberField(TEXT("animation_count"), AnimArr.Num());
    }

    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// umg.list_widget_bindings
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusUIHandler::HandleListWidgetBindings(
    const TSharedPtr<FJsonObject>& Params)
{
    FString WidgetPath = GetStringParam(Params, TEXT("widget_path"));
    if (WidgetPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("widget_path is required"));
    }

    UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetPath);
    if (!WidgetBP)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Widget Blueprint not found at '%s'"), *WidgetPath));
    }

    TArray<TSharedPtr<FJsonValue>> BindingArr;

    // Iterate the widget tree to find property bindings
    if (WidgetBP->WidgetTree)
    {
        WidgetBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
        {
            if (!Widget) return;

            // Check for delegate bindings in the blueprint's bindings array
            for (const FDelegateEditorBinding& Binding : WidgetBP->Bindings)
            {
                if (Binding.ObjectName == Widget->GetName())
                {
                    TSharedPtr<FJsonObject> BindObj = MakeShareable(new FJsonObject());
                    BindObj->SetStringField(TEXT("widget_name"), Widget->GetName());
                    BindObj->SetStringField(TEXT("widget_type"),
                        Widget->GetClass()->GetName());
                    BindObj->SetStringField(TEXT("property"),
                        Binding.PropertyName.ToString());
                    BindObj->SetStringField(TEXT("function_name"),
                        Binding.FunctionName.ToString());
                    BindObj->SetStringField(TEXT("source_path"),
                        Binding.SourcePath.GetDisplayText().ToString());
                    BindingArr.Add(MakeShareable(new FJsonValueObject(BindObj)));
                }
            }
        });
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("widget_path"), WidgetPath);
    Data->SetStringField(TEXT("widget_name"), WidgetBP->GetName());
    Data->SetArrayField(TEXT("bindings"), BindingArr);
    Data->SetNumberField(TEXT("binding_count"), BindingArr.Num());
    return MakeSuccess(Data);
}
