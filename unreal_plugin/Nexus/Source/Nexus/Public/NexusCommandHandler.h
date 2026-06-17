// Copyright Nexus Team. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Interface for command handler subsystems.
 * Each handler owns a namespace (e.g., "actor", "asset") and supports
 * a set of commands within that namespace.
 *
 * Pattern from kvick-games/UnrealMCP IMCPCommandHandler.
 */
class INexusCommandHandler
{
public:
    virtual ~INexusCommandHandler() = default;

    /** Namespace prefix for this handler's commands, e.g., "actor". */
    virtual FString GetNamespace() const = 0;

    /** List of command names (without namespace prefix). */
    virtual TArray<FString> GetSupportedCommands() const = 0;

    /**
     * Handle a command and return a JSON response.
     * @param CommandType Full command type string, e.g., "actor.spawn"
     * @param Params Command parameters as JSON object
     * @return Response JSON with "success", "data", and optionally "error" fields
     */
    virtual TSharedPtr<FJsonObject> Handle(
        const FString& CommandType,
        const TSharedPtr<FJsonObject>& Params
    ) = 0;

protected:
    /** Helper: Create a success response wrapping data. */
    static TSharedPtr<FJsonObject> MakeSuccess(const TSharedPtr<FJsonObject>& Data)
    {
        TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());
        Response->SetBoolField(TEXT("success"), true);
        Response->SetObjectField(TEXT("data"), Data);
        return Response;
    }

    /** Helper: Create an error response. */
    static TSharedPtr<FJsonObject> MakeError(const FString& Code, const FString& Message)
    {
        TSharedPtr<FJsonObject> ErrorObj = MakeShareable(new FJsonObject());
        ErrorObj->SetStringField(TEXT("code"), Code);
        ErrorObj->SetStringField(TEXT("message"), Message);

        TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject());
        Response->SetBoolField(TEXT("success"), false);
        Response->SetObjectField(TEXT("error"), ErrorObj);
        return Response;
    }

    /** Helper: Get a string param with default. */
    static FString GetStringParam(
        const TSharedPtr<FJsonObject>& Params,
        const FString& Key,
        const FString& Default = TEXT(""))
    {
        FString Value;
        if (Params.IsValid() && Params->TryGetStringField(Key, Value))
        {
            return Value;
        }
        return Default;
    }

    /** Helper: Get a float param with default. */
    static double GetNumberParam(
        const TSharedPtr<FJsonObject>& Params,
        const FString& Key,
        double Default = 0.0)
    {
        double Value;
        if (Params.IsValid() && Params->TryGetNumberField(Key, Value))
        {
            return Value;
        }
        return Default;
    }

    /** Helper: Get a bool param with default. */
    static bool GetBoolParam(
        const TSharedPtr<FJsonObject>& Params,
        const FString& Key,
        bool Default = false)
    {
        bool Value;
        if (Params.IsValid() && Params->TryGetBoolField(Key, Value))
        {
            return Value;
        }
        return Default;
    }

    /** Helper: Get FVector from JSON array or object. */
    static FVector GetVectorParam(
        const TSharedPtr<FJsonObject>& Params,
        const FString& Key,
        const FVector& Default = FVector::ZeroVector)
    {
        if (!Params.IsValid()) return Default;

        const TSharedPtr<FJsonObject>* VecObj;
        if (Params->TryGetObjectField(Key, VecObj))
        {
            return FVector(
                (*VecObj)->GetNumberField(TEXT("x")),
                (*VecObj)->GetNumberField(TEXT("y")),
                (*VecObj)->GetNumberField(TEXT("z"))
            );
        }

        const TArray<TSharedPtr<FJsonValue>>* VecArray;
        if (Params->TryGetArrayField(Key, VecArray) && VecArray->Num() >= 3)
        {
            return FVector(
                (*VecArray)[0]->AsNumber(),
                (*VecArray)[1]->AsNumber(),
                (*VecArray)[2]->AsNumber()
            );
        }

        return Default;
    }

    /** Helper: Convert FVector to JSON object. */
    static TSharedPtr<FJsonObject> VectorToJson(const FVector& Vec)
    {
        TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
        Obj->SetNumberField(TEXT("x"), Vec.X);
        Obj->SetNumberField(TEXT("y"), Vec.Y);
        Obj->SetNumberField(TEXT("z"), Vec.Z);
        return Obj;
    }

    /** Helper: Convert FRotator to JSON object. */
    static TSharedPtr<FJsonObject> RotatorToJson(const FRotator& Rot)
    {
        TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
        Obj->SetNumberField(TEXT("pitch"), Rot.Pitch);
        Obj->SetNumberField(TEXT("yaw"), Rot.Yaw);
        Obj->SetNumberField(TEXT("roll"), Rot.Roll);
        return Obj;
    }
};
