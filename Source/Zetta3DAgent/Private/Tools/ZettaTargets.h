// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UActorComponent;
class UBlueprint;
class UClass;
class UObject;
class USCS_Node;

/**
 * How the agent names things (the protocol's "Targets and values"): actors by label, "#ObjectName" or
 * "Folder/Label"; components as "Actor/Component"; assets by content path; Blueprint component templates as
 * "/Game/BP_Player:Mesh". A miss is a one-line error that lists the nearest names, so the model can retry.
 */
namespace ZettaTargets
{
	/** Actors the Outliner lists in the editor level (and its loaded sublevels). */
	TArray<AActor*> LevelActors();

	FString Label(const AActor* Actor);
	/** "Lighting/Sun" for an actor in an Outliner folder, else the label. */
	FString FolderAndLabel(const AActor* Actor);
	/** How the model should target this actor: its label when unique, else "#Name". */
	FString TargetOf(const AActor* Actor);
	/** "Chair (#StaticMeshActor_12, StaticMeshActor)". */
	FString Describe(const AActor* Actor);
	FString DescribeComponent(const UActorComponent* Component);

	AActor* FindActor(const FString& Target, FString& OutError);
	/** A component of an actor by name (or, failing that, by class name when only one matches). */
	UActorComponent* FindComponent(AActor* Actor, const FString& Name);
	/** "Actor/Component". */
	bool FindActorComponent(const FString& Target, AActor*& OutActor, UActorComponent*& OutComponent, FString& OutError);

	/** Loads an asset by content path; when Expected is set the asset must be one. */
	UObject* LoadAsset(const FString& Path, FString& OutError, const UClass* Expected = nullptr);
	bool AssetExists(const FString& Path);

	/**
	 * A class by short name ("PointLight", "StaticMeshComponent"), script path ("/Script/Engine.SpotLight") or
	 * Blueprint ("/Game/Blueprints/BP_Enemy"). Base limits what fits.
	 */
	UClass* FindClass(const FString& Name, const UClass* Base, FString& OutError);

	/** A Blueprint's component by name: its own SCS node, one inherited from a parent Blueprint, or a native one. */
	struct FBlueprintComponent
	{
		UObject* Template = nullptr;
		USCS_Node* Node = nullptr;
		bool bInherited = false;
		bool bNative = false;
	};
	/** With bForWrite, an inherited template gets the Blueprint's own override to edit. */
	bool FindBlueprintComponent(UBlueprint* Blueprint, const FString& Name, bool bForWrite, FBlueprintComponent& Out, FString& OutError);
	/** Every component name a Blueprint has (own, inherited and native), for messages. */
	TArray<FString> BlueprintComponentNames(UBlueprint* Blueprint);

	/** What set_properties (and friends) write to. */
	struct FObjectTarget
	{
		UObject* Object = nullptr;
		AActor* Actor = nullptr;
		UBlueprint* Blueprint = nullptr;
		/** The object is an asset (or a Blueprint's template / class defaults): it is saved after a write. */
		bool bAsset = false;
		FString Label;
	};
	bool ResolveObject(const FString& Target, bool bForWrite, FObjectTarget& Out, FString& OutError);

	/** Up to Max candidates closest to Query, quoted, for "Did you mean …?". */
	FString NearMatches(const FString& Query, const TArray<FString>& Candidates, int32 Max = 5);
}
