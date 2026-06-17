// Copyright Nexus Team. All Rights Reserved.

#include "Handlers/NexusPhysicsHandler.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsAssetUtils.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusPhysicsHandler::Handle(
    const FString& CommandType,
    const TSharedPtr<FJsonObject>& Params)
{
    FString SubCommand = CommandType.RightChop(GetNamespace().Len() + 1);

    if (SubCommand == TEXT("set_collision_profile"))      return HandleSetCollisionProfile(Params);
    if (SubCommand == TEXT("enable_physics_simulation"))   return HandleEnablePhysicsSimulation(Params);
    if (SubCommand == TEXT("set_physics_properties"))      return HandleSetPhysicsProperties(Params);
    if (SubCommand == TEXT("set_collision_response"))      return HandleSetCollisionResponse(Params);
    if (SubCommand == TEXT("add_physics_constraint"))      return HandleAddPhysicsConstraint(Params);
    if (SubCommand == TEXT("create_physics_asset"))        return HandleCreatePhysicsAsset(Params);
    if (SubCommand == TEXT("apply_force"))                 return HandleApplyForce(Params);
    if (SubCommand == TEXT("get_physics_info"))            return HandleGetPhysicsInfo(Params);

    return MakeError(TEXT("NOT_IMPLEMENTED"),
        FString::Printf(TEXT("Command '%s' not yet implemented"), *CommandType));
}

// ─────────────────────────────────────────────────────────────────────────────
// Actor / component resolution helpers
// ─────────────────────────────────────────────────────────────────────────────

AActor* FNexusPhysicsHandler::FindActorByPath(const FString& Path)
{
    if (Path.IsEmpty()) return nullptr;
    UObject* Obj = StaticFindObject(AActor::StaticClass(), nullptr, *Path);
    return Cast<AActor>(Obj);
}

AActor* FNexusPhysicsHandler::FindActorByLabel(const FString& Label)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) return nullptr;

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetActorLabel() == Label)
        {
            return *It;
        }
    }
    return nullptr;
}

AActor* FNexusPhysicsHandler::ResolveActor(
    const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
    FString ActorPath = GetStringParam(Params, TEXT("actor_path"));
    if (!ActorPath.IsEmpty())
    {
        AActor* Actor = FindActorByPath(ActorPath);
        if (!Actor)
        {
            // Fallback: try as label
            Actor = FindActorByLabel(ActorPath);
        }
        if (!Actor)
        {
            OutError = FString::Printf(TEXT("No actor found for '%s'"), *ActorPath);
        }
        return Actor;
    }

    FString ActorLabel = GetStringParam(Params, TEXT("actor_label"));
    if (!ActorLabel.IsEmpty())
    {
        AActor* Actor = FindActorByLabel(ActorLabel);
        if (!Actor)
        {
            OutError = FString::Printf(TEXT("No actor with label '%s'"), *ActorLabel);
        }
        return Actor;
    }

    OutError = TEXT("Must provide 'actor_path' or 'actor_label'");
    return nullptr;
}

UPrimitiveComponent* FNexusPhysicsHandler::GetPrimitiveComponent(
    AActor* Actor, const FString& ComponentName, FString& OutError)
{
    if (!Actor)
    {
        OutError = TEXT("Actor is null");
        return nullptr;
    }

    // If a specific component name is given, find it
    if (!ComponentName.IsEmpty())
    {
        TArray<UActorComponent*> Components;
        Actor->GetComponents(Components);
        for (UActorComponent* Comp : Components)
        {
            if (Comp && Comp->GetName() == ComponentName)
            {
                UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Comp);
                if (PrimComp)
                {
                    return PrimComp;
                }
                OutError = FString::Printf(
                    TEXT("Component '%s' exists but is not a PrimitiveComponent"), *ComponentName);
                return nullptr;
            }
        }
        OutError = FString::Printf(
            TEXT("Component '%s' not found on actor '%s'"), *ComponentName, *Actor->GetName());
        return nullptr;
    }

    // Default: find first PrimitiveComponent (root preferred)
    UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
    if (RootPrim)
    {
        return RootPrim;
    }

    // Search all components
    UPrimitiveComponent* FirstPrim = Actor->FindComponentByClass<UPrimitiveComponent>();
    if (!FirstPrim)
    {
        OutError = FString::Printf(
            TEXT("No PrimitiveComponent found on actor '%s'"), *Actor->GetName());
    }
    return FirstPrim;
}

// ─────────────────────────────────────────────────────────────────────────────
// physics.set_collision_profile
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusPhysicsHandler::HandleSetCollisionProfile(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    AActor* Actor = ResolveActor(Params, Error);
    if (!Actor) return MakeError(TEXT("NOT_FOUND"), Error);

    FString ComponentName = GetStringParam(Params, TEXT("component_name"));
    UPrimitiveComponent* Comp = GetPrimitiveComponent(Actor, ComponentName, Error);
    if (!Comp) return MakeError(TEXT("NO_COMPONENT"), Error);

    FString CollisionProfile = GetStringParam(Params, TEXT("collision_profile"), TEXT("BlockAll"));

    Comp->SetCollisionProfileName(FName(*CollisionProfile));
    Actor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetStringField(TEXT("collision_profile"), CollisionProfile);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// physics.enable_physics_simulation
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusPhysicsHandler::HandleEnablePhysicsSimulation(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    AActor* Actor = ResolveActor(Params, Error);
    if (!Actor) return MakeError(TEXT("NOT_FOUND"), Error);

    FString ComponentName = GetStringParam(Params, TEXT("component_name"));
    UPrimitiveComponent* Comp = GetPrimitiveComponent(Actor, ComponentName, Error);
    if (!Comp) return MakeError(TEXT("NO_COMPONENT"), Error);

    bool bEnable = GetBoolParam(Params, TEXT("enable"), true);
    bool bGravityEnabled = GetBoolParam(Params, TEXT("gravity_enabled"), true);

    Comp->SetSimulatePhysics(bEnable);
    Comp->SetEnableGravity(bGravityEnabled);
    Actor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetBoolField(TEXT("simulating"), bEnable);
    Data->SetBoolField(TEXT("gravity"), bGravityEnabled);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// physics.set_physics_properties
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusPhysicsHandler::HandleSetPhysicsProperties(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    AActor* Actor = ResolveActor(Params, Error);
    if (!Actor) return MakeError(TEXT("NOT_FOUND"), Error);

    FString ComponentName = GetStringParam(Params, TEXT("component_name"));
    UPrimitiveComponent* Comp = GetPrimitiveComponent(Actor, ComponentName, Error);
    if (!Comp) return MakeError(TEXT("NO_COMPONENT"), Error);

    FBodyInstance* BI = Comp->GetBodyInstance();
    if (!BI)
    {
        return MakeError(TEXT("NO_BODY_INSTANCE"),
            TEXT("Component has no physics body instance"));
    }

    // Apply only parameters that are explicitly provided
    double Value;
    bool BoolValue;

    if (Params->TryGetNumberField(TEXT("mass"), Value))
    {
        BI->SetMassOverride(Value);
    }

    if (Params->TryGetNumberField(TEXT("linear_damping"), Value))
    {
        Comp->SetLinearDamping(Value);
    }

    if (Params->TryGetNumberField(TEXT("angular_damping"), Value))
    {
        Comp->SetAngularDamping(Value);
    }

    if (Params->TryGetBoolField(TEXT("enable_ccd"), BoolValue))
    {
        BI->SetUseCCD(BoolValue);
    }

    if (Params->TryGetNumberField(TEXT("friction"), Value))
    {
        // Friction is set through the physical material
        if (UPhysicalMaterial* PhysMat = BI->GetSimplePhysicalMaterial())
        {
            PhysMat->Friction = Value;
            PhysMat->MarkPackageDirty();
        }
    }

    if (Params->TryGetNumberField(TEXT("restitution"), Value))
    {
        if (UPhysicalMaterial* PhysMat = BI->GetSimplePhysicalMaterial())
        {
            PhysMat->Restitution = Value;
            PhysMat->MarkPackageDirty();
        }
    }

    if (Params->TryGetNumberField(TEXT("max_angular_velocity"), Value))
    {
        BI->SetMaxAngularVelocityInRadians(FMath::DegreesToRadians(Value), false, true);
    }

    if (Params->TryGetNumberField(TEXT("max_depenetration_velocity"), Value))
    {
        BI->SetMaxDepenetrationVelocity(Value);
    }

    if (Params->TryGetNumberField(TEXT("sleep_threshold"), Value))
    {
        BI->SleepFamily = ESleepFamily::Custom;
        BI->CustomSleepThresholdMultiplier = Value;
    }

    Actor->MarkPackageDirty();

    // Build response with current state
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetNumberField(TEXT("mass"), BI->GetMassOverride());
    Data->SetNumberField(TEXT("linear_damping"), Comp->GetLinearDamping());
    Data->SetNumberField(TEXT("angular_damping"), Comp->GetAngularDamping());
    Data->SetBoolField(TEXT("ccd_enabled"), BI->bUseCCD);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// physics.set_collision_response
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusPhysicsHandler::HandleSetCollisionResponse(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    AActor* Actor = ResolveActor(Params, Error);
    if (!Actor) return MakeError(TEXT("NOT_FOUND"), Error);

    FString ChannelStr = GetStringParam(Params, TEXT("channel"));
    if (ChannelStr.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("channel is required"));
    }

    FString ResponseStr = GetStringParam(Params, TEXT("response"), TEXT("Block"));
    FString ComponentName = GetStringParam(Params, TEXT("component_name"));

    UPrimitiveComponent* Comp = GetPrimitiveComponent(Actor, ComponentName, Error);
    if (!Comp) return MakeError(TEXT("NO_COMPONENT"), Error);

    // Parse collision channel
    ECollisionChannel Channel = ECC_WorldStatic;
    if (ChannelStr == TEXT("WorldStatic"))        Channel = ECC_WorldStatic;
    else if (ChannelStr == TEXT("WorldDynamic"))   Channel = ECC_WorldDynamic;
    else if (ChannelStr == TEXT("Pawn"))            Channel = ECC_Pawn;
    else if (ChannelStr == TEXT("Visibility"))      Channel = ECC_Visibility;
    else if (ChannelStr == TEXT("Camera"))          Channel = ECC_Camera;
    else if (ChannelStr == TEXT("PhysicsBody"))     Channel = ECC_PhysicsBody;
    else if (ChannelStr == TEXT("Vehicle"))         Channel = ECC_Vehicle;
    else if (ChannelStr == TEXT("Destructible"))    Channel = ECC_Destructible;
    else
    {
        return MakeError(TEXT("INVALID_CHANNEL"),
            FString::Printf(TEXT("Unknown collision channel '%s'. Use: WorldStatic, WorldDynamic, Pawn, Visibility, Camera, PhysicsBody, Vehicle, Destructible"), *ChannelStr));
    }

    // Parse response type
    ECollisionResponse Response = ECR_Block;
    if (ResponseStr == TEXT("Ignore"))       Response = ECR_Ignore;
    else if (ResponseStr == TEXT("Overlap")) Response = ECR_Overlap;
    else if (ResponseStr == TEXT("Block"))   Response = ECR_Block;
    else
    {
        return MakeError(TEXT("INVALID_RESPONSE"),
            FString::Printf(TEXT("Unknown response type '%s'. Use: Ignore, Overlap, Block"), *ResponseStr));
    }

    Comp->SetCollisionResponseToChannel(Channel, Response);
    Actor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetStringField(TEXT("channel"), ChannelStr);
    Data->SetStringField(TEXT("response"), ResponseStr);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// physics.add_physics_constraint
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusPhysicsHandler::HandleAddPhysicsConstraint(
    const TSharedPtr<FJsonObject>& Params)
{
    FString ActorPath1 = GetStringParam(Params, TEXT("actor_path_1"));
    FString ActorPath2 = GetStringParam(Params, TEXT("actor_path_2"));
    if (ActorPath1.IsEmpty() || ActorPath2.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"),
            TEXT("actor_path_1 and actor_path_2 are required"));
    }

    FString Error;

    // Resolve actor 1
    AActor* Actor1 = FindActorByPath(ActorPath1);
    if (!Actor1) Actor1 = FindActorByLabel(ActorPath1);
    if (!Actor1)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Actor 1 not found: '%s'"), *ActorPath1));
    }

    // Resolve actor 2
    AActor* Actor2 = FindActorByPath(ActorPath2);
    if (!Actor2) Actor2 = FindActorByLabel(ActorPath2);
    if (!Actor2)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Actor 2 not found: '%s'"), *ActorPath2));
    }

    FString CompName1 = GetStringParam(Params, TEXT("component_name_1"));
    FString CompName2 = GetStringParam(Params, TEXT("component_name_2"));

    UPrimitiveComponent* Comp1 = GetPrimitiveComponent(Actor1, CompName1, Error);
    if (!Comp1) return MakeError(TEXT("NO_COMPONENT"), Error);

    UPrimitiveComponent* Comp2 = GetPrimitiveComponent(Actor2, CompName2, Error);
    if (!Comp2) return MakeError(TEXT("NO_COMPONENT"), Error);

    FString ConstraintType = GetStringParam(Params, TEXT("constraint_type"), TEXT("Fixed"));
    bool bDisableCollision = GetBoolParam(Params, TEXT("disable_collision"), true);

    // Create constraint component on actor 1
    UPhysicsConstraintComponent* Constraint = NewObject<UPhysicsConstraintComponent>(
        Actor1, NAME_None, RF_Transactional);
    Constraint->SetWorldLocation(Actor1->GetActorLocation());
    Constraint->AttachToComponent(
        Actor1->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
    Actor1->AddInstanceComponent(Constraint);
    Constraint->RegisterComponent();

    // Connect the two components
    Constraint->SetConstrainedComponents(Comp1, NAME_None, Comp2, NAME_None);
    Constraint->SetDisableCollision(bDisableCollision);

    // Configure constraint type
    if (ConstraintType == TEXT("Fixed"))
    {
        Constraint->SetLinearXLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
        Constraint->SetLinearYLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
        Constraint->SetLinearZLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
        Constraint->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Locked, 0.0f);
        Constraint->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Locked, 0.0f);
        Constraint->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Locked, 0.0f);
    }
    else if (ConstraintType == TEXT("Free"))
    {
        Constraint->SetLinearXLimit(ELinearConstraintMotion::LCM_Free, 0.0f);
        Constraint->SetLinearYLimit(ELinearConstraintMotion::LCM_Free, 0.0f);
        Constraint->SetLinearZLimit(ELinearConstraintMotion::LCM_Free, 0.0f);
        Constraint->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Free, 0.0f);
        Constraint->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Free, 0.0f);
        Constraint->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Free, 0.0f);
    }
    else if (ConstraintType == TEXT("Hinge"))
    {
        Constraint->SetLinearXLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
        Constraint->SetLinearYLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
        Constraint->SetLinearZLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
        Constraint->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Locked, 0.0f);
        Constraint->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Locked, 0.0f);
        Constraint->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Free, 0.0f);
    }
    else if (ConstraintType == TEXT("Prismatic"))
    {
        Constraint->SetLinearXLimit(ELinearConstraintMotion::LCM_Free, 0.0f);
        Constraint->SetLinearYLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
        Constraint->SetLinearZLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
        Constraint->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Locked, 0.0f);
        Constraint->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Locked, 0.0f);
        Constraint->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Locked, 0.0f);
    }
    else if (ConstraintType == TEXT("BallSocket"))
    {
        Constraint->SetLinearXLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
        Constraint->SetLinearYLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
        Constraint->SetLinearZLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
        Constraint->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Free, 0.0f);
        Constraint->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Free, 0.0f);
        Constraint->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Free, 0.0f);
    }

    // Apply optional limits
    double LimitValue;
    if (Params->TryGetNumberField(TEXT("swing_limit_1"), LimitValue))
    {
        Constraint->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Limited, LimitValue);
    }
    if (Params->TryGetNumberField(TEXT("swing_limit_2"), LimitValue))
    {
        Constraint->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Limited, LimitValue);
    }
    if (Params->TryGetNumberField(TEXT("twist_limit"), LimitValue))
    {
        Constraint->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Limited, LimitValue);
    }
    if (Params->TryGetNumberField(TEXT("linear_limit"), LimitValue))
    {
        Constraint->SetLinearXLimit(ELinearConstraintMotion::LCM_Limited, LimitValue);
        Constraint->SetLinearYLimit(ELinearConstraintMotion::LCM_Limited, LimitValue);
        Constraint->SetLinearZLimit(ELinearConstraintMotion::LCM_Limited, LimitValue);
    }

    // Apply break thresholds
    double BreakForce;
    if (Params->TryGetNumberField(TEXT("break_force"), BreakForce))
    {
        Constraint->SetLinearBreakable(true, BreakForce);
    }

    double BreakTorque;
    if (Params->TryGetNumberField(TEXT("break_torque"), BreakTorque))
    {
        Constraint->SetAngularBreakable(true, BreakTorque);
    }

    Actor1->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("constraint_type"), ConstraintType);
    Data->SetStringField(TEXT("actor_1"), Actor1->GetPathName());
    Data->SetStringField(TEXT("actor_2"), Actor2->GetPathName());
    Data->SetStringField(TEXT("constraint_name"), Constraint->GetName());
    Data->SetBoolField(TEXT("disable_collision"), bDisableCollision);
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// physics.create_physics_asset
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusPhysicsHandler::HandleCreatePhysicsAsset(
    const TSharedPtr<FJsonObject>& Params)
{
    FString SkelMeshPath = GetStringParam(Params, TEXT("skeletal_mesh_path"));
    if (SkelMeshPath.IsEmpty())
    {
        return MakeError(TEXT("MISSING_PARAM"), TEXT("skeletal_mesh_path is required"));
    }

    USkeletalMesh* SkelMesh = LoadObject<USkeletalMesh>(nullptr, *SkelMeshPath);
    if (!SkelMesh)
    {
        return MakeError(TEXT("NOT_FOUND"),
            FString::Printf(TEXT("Skeletal Mesh not found at '%s'"), *SkelMeshPath));
    }

    FString AssetName = GetStringParam(Params, TEXT("asset_name"));
    if (AssetName.IsEmpty())
    {
        AssetName = SkelMesh->GetName() + TEXT("_PhysicsAsset");
    }

    FString DestFolder = GetStringParam(Params, TEXT("destination_folder"));
    if (DestFolder.IsEmpty())
    {
        DestFolder = FPackageName::GetLongPackagePath(SkelMesh->GetOutermost()->GetName());
    }

    FString BodyTypeStr = GetStringParam(Params, TEXT("body_type"), TEXT("Capsule"));
    double MinBoneSize = GetNumberParam(Params, TEXT("min_bone_size"), 5.0);

    // Physics asset creation is complex and depends on specific UE factory APIs.
    // Validate inputs and return info for codegen to handle full creation.
    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("skeletal_mesh_path"), SkelMesh->GetPathName());
    Data->SetStringField(TEXT("asset_name"), AssetName);
    Data->SetStringField(TEXT("destination_folder"), DestFolder);
    Data->SetStringField(TEXT("body_type"), BodyTypeStr);
    Data->SetNumberField(TEXT("min_bone_size"), MinBoneSize);

    USkeleton* Skel = SkelMesh->GetSkeleton();
    Data->SetStringField(TEXT("skeleton_path"), Skel ? Skel->GetPathName() : TEXT(""));
    Data->SetNumberField(TEXT("bone_count"),
        Skel ? Skel->GetReferenceSkeleton().GetNum() : 0);

    // Check if a physics asset already exists
    UPhysicsAsset* ExistingPA = SkelMesh->GetPhysicsAsset();
    Data->SetBoolField(TEXT("has_existing_physics_asset"), ExistingPA != nullptr);
    if (ExistingPA)
    {
        Data->SetStringField(TEXT("existing_physics_asset_path"), ExistingPA->GetPathName());
    }

    Data->SetStringField(TEXT("note"),
        TEXT("Physics asset parameters validated. Use codegen for full PhysicsAsset creation via FPhysAssetCreateParams."));
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// physics.apply_force
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusPhysicsHandler::HandleApplyForce(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    AActor* Actor = ResolveActor(Params, Error);
    if (!Actor) return MakeError(TEXT("NOT_FOUND"), Error);

    FString ComponentName = GetStringParam(Params, TEXT("component_name"));
    UPrimitiveComponent* Comp = GetPrimitiveComponent(Actor, ComponentName, Error);
    if (!Comp) return MakeError(TEXT("NO_COMPONENT"), Error);

    if (!Comp->IsSimulatingPhysics())
    {
        return MakeError(TEXT("NOT_SIMULATING"),
            TEXT("Component is not simulating physics. Call enable_physics_simulation first."));
    }

    // Parse force vector — Python sends as array [x, y, z]
    FVector Force = FVector::ZeroVector;
    const TArray<TSharedPtr<FJsonValue>>* ForceArray;
    if (Params->TryGetArrayField(TEXT("force"), ForceArray) && ForceArray->Num() >= 3)
    {
        Force.X = (*ForceArray)[0]->AsNumber();
        Force.Y = (*ForceArray)[1]->AsNumber();
        Force.Z = (*ForceArray)[2]->AsNumber();
    }
    else
    {
        // Fallback: individual params
        Force.X = GetNumberParam(Params, TEXT("force_x"), 0.0);
        Force.Y = GetNumberParam(Params, TEXT("force_y"), 0.0);
        Force.Z = GetNumberParam(Params, TEXT("force_z"), 0.0);
    }

    bool bIsImpulse = GetBoolParam(Params, TEXT("is_impulse"), false);
    bool bVelChange = GetBoolParam(Params, TEXT("vel_change"), false);
    FString BoneName = GetStringParam(Params, TEXT("bone_name"));
    FName BoneFName = BoneName.IsEmpty() ? NAME_None : FName(*BoneName);

    // Check for force_point (application point)
    FVector ForcePoint = FVector::ZeroVector;
    bool bHasForcePoint = false;
    const TArray<TSharedPtr<FJsonValue>>* ForcePointArray;
    if (Params->TryGetArrayField(TEXT("force_point"), ForcePointArray) && ForcePointArray->Num() >= 3)
    {
        ForcePoint.X = (*ForcePointArray)[0]->AsNumber();
        ForcePoint.Y = (*ForcePointArray)[1]->AsNumber();
        ForcePoint.Z = (*ForcePointArray)[2]->AsNumber();
        bHasForcePoint = true;
    }

    if (bIsImpulse)
    {
        if (bHasForcePoint)
        {
            Comp->AddImpulseAtLocation(Force, ForcePoint, BoneFName);
        }
        else
        {
            Comp->AddImpulse(Force, BoneFName, bVelChange);
        }
    }
    else
    {
        if (bHasForcePoint)
        {
            Comp->AddForceAtLocation(Force, ForcePoint, BoneFName);
        }
        else
        {
            Comp->AddForce(Force, BoneFName, bVelChange);
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetObjectField(TEXT("force"), VectorToJson(Force));
    Data->SetBoolField(TEXT("is_impulse"), bIsImpulse);
    Data->SetBoolField(TEXT("vel_change"), bVelChange);
    if (!BoneName.IsEmpty())
    {
        Data->SetStringField(TEXT("bone_name"), BoneName);
    }
    if (bHasForcePoint)
    {
        Data->SetObjectField(TEXT("force_point"), VectorToJson(ForcePoint));
    }
    return MakeSuccess(Data);
}

// ─────────────────────────────────────────────────────────────────────────────
// physics.get_physics_info
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FNexusPhysicsHandler::HandleGetPhysicsInfo(
    const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    AActor* Actor = ResolveActor(Params, Error);
    if (!Actor) return MakeError(TEXT("NOT_FOUND"), Error);

    FString ComponentName = GetStringParam(Params, TEXT("component_name"));
    UPrimitiveComponent* Comp = GetPrimitiveComponent(Actor, ComponentName, Error);
    if (!Comp) return MakeError(TEXT("NO_COMPONENT"), Error);

    bool bIncludeConstraints = GetBoolParam(Params, TEXT("include_constraints"), true);
    bool bIncludeCollision = GetBoolParam(Params, TEXT("include_collision"), true);

    FBodyInstance* BI = Comp->GetBodyInstance();

    TSharedPtr<FJsonObject> Data = MakeShareable(new FJsonObject());
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetStringField(TEXT("component_name"), Comp->GetName());
    Data->SetBoolField(TEXT("simulating"), Comp->IsSimulatingPhysics());
    Data->SetBoolField(TEXT("gravity_enabled"), Comp->IsGravityEnabled());

    if (BI)
    {
        Data->SetNumberField(TEXT("mass"), BI->GetMassOverride());
        Data->SetNumberField(TEXT("linear_damping"), Comp->GetLinearDamping());
        Data->SetNumberField(TEXT("angular_damping"), Comp->GetAngularDamping());
        Data->SetBoolField(TEXT("ccd_enabled"), BI->bUseCCD);
    }

    // Collision profile info
    if (bIncludeCollision)
    {
        Data->SetStringField(TEXT("collision_profile"),
            Comp->GetCollisionProfileName().ToString());
        Data->SetBoolField(TEXT("collision_enabled"),
            Comp->GetCollisionEnabled() != ECollisionEnabled::NoCollision);

        FString CollisionEnabledStr;
        switch (Comp->GetCollisionEnabled())
        {
        case ECollisionEnabled::NoCollision:          CollisionEnabledStr = TEXT("NoCollision"); break;
        case ECollisionEnabled::QueryOnly:            CollisionEnabledStr = TEXT("QueryOnly"); break;
        case ECollisionEnabled::PhysicsOnly:          CollisionEnabledStr = TEXT("PhysicsOnly"); break;
        case ECollisionEnabled::QueryAndPhysics:      CollisionEnabledStr = TEXT("QueryAndPhysics"); break;
        case ECollisionEnabled::ProbeOnly:            CollisionEnabledStr = TEXT("ProbeOnly"); break;
        case ECollisionEnabled::QueryAndProbe:        CollisionEnabledStr = TEXT("QueryAndProbe"); break;
        default:                                       CollisionEnabledStr = TEXT("Unknown"); break;
        }
        Data->SetStringField(TEXT("collision_enabled_type"), CollisionEnabledStr);
    }

    // Constraint info
    if (bIncludeConstraints)
    {
        TArray<TSharedPtr<FJsonValue>> ConstraintArray;

        TArray<UActorComponent*> Components;
        Actor->GetComponents(Components);
        for (UActorComponent* ActorComp : Components)
        {
            UPhysicsConstraintComponent* PhysConstraint =
                Cast<UPhysicsConstraintComponent>(ActorComp);
            if (!PhysConstraint) continue;

            TSharedPtr<FJsonObject> ConstraintObj = MakeShareable(new FJsonObject());
            ConstraintObj->SetStringField(TEXT("name"), PhysConstraint->GetName());

            // Determine constraint type from limits
            FString TypeStr = TEXT("Custom");
            bool bLinearLocked =
                PhysConstraint->ConstraintInstance.GetLinearXMotion() == ELinearConstraintMotion::LCM_Locked &&
                PhysConstraint->ConstraintInstance.GetLinearYMotion() == ELinearConstraintMotion::LCM_Locked &&
                PhysConstraint->ConstraintInstance.GetLinearZMotion() == ELinearConstraintMotion::LCM_Locked;
            bool bAngularLocked =
                PhysConstraint->ConstraintInstance.GetAngularSwing1Motion() == EAngularConstraintMotion::ACM_Locked &&
                PhysConstraint->ConstraintInstance.GetAngularSwing2Motion() == EAngularConstraintMotion::ACM_Locked &&
                PhysConstraint->ConstraintInstance.GetAngularTwistMotion() == EAngularConstraintMotion::ACM_Locked;

            if (bLinearLocked && bAngularLocked)
            {
                TypeStr = TEXT("Fixed");
            }
            else if (bLinearLocked && !bAngularLocked)
            {
                TypeStr = TEXT("BallSocket");
            }

            ConstraintObj->SetStringField(TEXT("type"), TypeStr);
            ConstraintObj->SetBoolField(TEXT("is_broken"),
                PhysConstraint->IsBroken());

            ConstraintArray.Add(MakeShareable(new FJsonValueObject(ConstraintObj)));
        }

        Data->SetArrayField(TEXT("constraints"), ConstraintArray);
        Data->SetNumberField(TEXT("constraint_count"), ConstraintArray.Num());
    }

    return MakeSuccess(Data);
}
