// Copyright Zetta 3D. See LICENSE.md.
// Changing the level: spawn_actor, modify_actor, add_component, remove_component, delete_actors, set_properties,
// level_operation, play_in_editor, console_command, viewport, undo.

#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Engine/Blueprint.h"
#include "Engine/Level.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "LevelEditorSubsystem.h"
#include "LevelEditorViewport.h"
#include "Misc/PackageName.h"
#include "Misc/StringOutputDevice.h"
#include "Serialization/JsonSerializer.h"
#include "ScopedTransaction.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "ZettaAgentTools.h"
#include "ZettaAssetUtil.h"
#include "ZettaImporter.h"
#include "ZettaProperties.h"
#include "ZettaTargets.h"
#include "ZettaToolCommon.h"

namespace ZettaLevelImpl
{
	using ZettaTool::Fail;
	using ZettaTool::Ok;

	FString JoinLines(const TArray<FString>& Lines, const FString& Indent = TEXT("  "))
	{
		FString Out;
		for (const FString& L : Lines) Out += TEXT("\n") + Indent + L;
		return Out;
	}

	bool RefuseWhilePlaying(FString& OutError)
	{
		if (!ZettaTool::IsPlaying()) return false;
		OutError = TEXT("Play In Editor is running: level edits made now would be lost when it stops. Stop it first (play_in_editor stop).");
		return true;
	}

	bool ParseMobility(const FString& In, EComponentMobility::Type& Out)
	{
		const FString M = In.TrimStartAndEnd().ToLower();
		if (M == TEXT("static")) Out = EComponentMobility::Static;
		else if (M == TEXT("stationary")) Out = EComponentMobility::Stationary;
		else if (M == TEXT("movable") || M == TEXT("moveable") || M == TEXT("dynamic")) Out = EComponentMobility::Movable;
		else return false;
		return true;
	}

	// ─── spawn_actor ─────────────────────────────────────────────────────────

	FZettaToolResult SpawnActor(const TSharedPtr<FJsonObject>& In)
	{
		FString Error;
		if (RefuseWhilePlaying(Error)) return Fail(Error);
		UWorld* World = ZettaTool::EditorWorld();
		if (!World || !GEditor) return Fail(TEXT("No level is open."));

		UObject* Mesh = nullptr;
		if (ZettaTool::Has(In, TEXT("mesh")))
		{
			Mesh = ZettaTargets::LoadAsset(ZettaTool::Str(In, TEXT("mesh")), Error);
			if (!Mesh) return Fail(Error);
			if (!Mesh->IsA<UStaticMesh>() && !Mesh->IsA<USkeletalMesh>()) return Fail(FString::Printf(TEXT("'mesh' must be a static or skeletal mesh; %s is a %s."), *ZettaTool::Str(In, TEXT("mesh")), *Mesh->GetClass()->GetName()));
		}
		UClass* Class = nullptr;
		if (ZettaTool::Has(In, TEXT("class")))
		{
			Class = ZettaTargets::FindClass(ZettaTool::Str(In, TEXT("class")), AActor::StaticClass(), Error);
			if (!Class) return Fail(Error);
		}
		else if (Mesh)
		{
			Class = Mesh->IsA<USkeletalMesh>() ? ASkeletalMeshActor::StaticClass() : AStaticMeshActor::StaticClass();
		}
		else
		{
			return Fail(TEXT("Give a 'class' (\"PointLight\", \"/Game/Blueprints/BP_Enemy\") or a 'mesh' asset to spawn."));
		}
		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NotPlaceable))
		{
			return Fail(FString::Printf(TEXT("%s can't be placed in a level (it is abstract or not placeable). Pick a concrete subclass."), *Class->GetName()));
		}

		TOptional<FVector> Location, Rotation, Scale;
		if (!ZettaTool::OptVector(In, TEXT("location"), Location, Error) || !ZettaTool::OptVector(In, TEXT("rotation"), Rotation, Error) || !ZettaTool::OptVector(In, TEXT("scale"), Scale, Error)) return Fail(Error);
		AActor* Parent = nullptr;
		if (ZettaTool::Has(In, TEXT("parent")))
		{
			Parent = ZettaTargets::FindActor(ZettaTool::Str(In, TEXT("parent")), Error);
			if (!Parent) return Fail(Error);
		}

		const FVector At = Location.IsSet() ? Location.GetValue() : ZettaImporter::DropPoint();
		const FRotator Rot = Rotation.IsSet() ? FRotator(Rotation->X, Rotation->Y, Rotation->Z) : FRotator::ZeroRotator;
		const FTransform Transform(Rot, At, Scale.IsSet() ? Scale.GetValue() : FVector::OneVector);

		FString Label = ZettaTool::Str(In, TEXT("label")).TrimStartAndEnd();
		if (Label.IsEmpty() && Mesh) Label = Mesh->GetName();
		const FString Title = ZettaTool::TransactionTitle(FString::Printf(TEXT("Spawn %s"), Label.IsEmpty() ? *Class->GetName() : *Label));
		TUniquePtr<FScopedTransaction> Transaction = MakeUnique<FScopedTransaction>(FText::FromString(Title));
		AActor* Actor = GEditor->AddActor(World->GetCurrentLevel(), Class, Transform, true, RF_Transactional, false);
		if (!Actor)
		{
			Transaction->Cancel();
			return Fail(FString::Printf(TEXT("Unreal could not spawn a %s here."), *Class->GetName()));
		}
		if (Mesh)
		{
			if (UStaticMesh* SM = Cast<UStaticMesh>(Mesh))
			{
				if (UStaticMeshComponent* C = Actor->FindComponentByClass<UStaticMeshComponent>())
				{
					C->Modify();
					C->SetStaticMesh(SM);
				}
			}
			else if (USkeletalMesh* SK = Cast<USkeletalMesh>(Mesh))
			{
				if (USkeletalMeshComponent* C = Actor->FindComponentByClass<USkeletalMeshComponent>())
				{
					C->Modify();
					C->SetSkeletalMeshAsset(SK);
				}
			}
		}
		if (!Label.IsEmpty()) Actor->SetActorLabel(Label);
		if (ZettaTool::Has(In, TEXT("folder"))) Actor->SetFolderPath(FName(*ZettaTool::Str(In, TEXT("folder")).TrimStartAndEnd().TrimChar(TEXT('/'))));
		if (Parent) Actor->AttachToActor(Parent, FAttachmentTransformRules::KeepWorldTransform);

		TArray<FString> Lines, Errors;
		ZettaProps::WriteAll(Actor, ZettaTool::Obj(In, TEXT("properties")), Lines, Errors);
		Actor->PostEditChange();
		Transaction.Reset();
		ZettaAgentChanges::LevelTransaction(Title);

		FString Text = FString::Printf(TEXT("Spawned %s at %s."), *ZettaTargets::Describe(Actor), *ZettaTool::Vec(Actor->GetActorLocation()));
		if (Parent) Text += FString::Printf(TEXT(" Attached to %s."), *ZettaTargets::Label(Parent));
		FVector Origin, Extent;
		Actor->GetActorBounds(true, Origin, Extent);
		if (!Extent.IsNearlyZero()) Text += FString::Printf(TEXT(" Size %s cm."), *ZettaTool::Vec(Extent * 2));
		if (!Location.IsSet()) Text += TEXT(" (Placed on the ground in front of the viewport.)");
		if (Lines.Num() > 0) Text += TEXT("\nSet:") + JoinLines(Lines);
		if (Errors.Num() > 0) Text += TEXT("\nNot set:") + JoinLines(Errors);
		Text += FString::Printf(TEXT("\nTarget it as \"%s\"."), *ZettaTargets::TargetOf(Actor));
		return Ok(Text, FString::Printf(TEXT("Spawned %s"), *ZettaTargets::Label(Actor)));
	}

	// ─── modify_actor ────────────────────────────────────────────────────────

	FZettaToolResult ModifyActor(const TSharedPtr<FJsonObject>& In)
	{
		FString Error;
		if (RefuseWhilePlaying(Error)) return Fail(Error);
		AActor* Actor = ZettaTargets::FindActor(ZettaTool::Str(In, TEXT("target")), Error);
		if (!Actor) return Fail(Error);

		TOptional<FVector> Location, Rotation, Scale;
		if (!ZettaTool::OptVector(In, TEXT("location"), Location, Error) || !ZettaTool::OptVector(In, TEXT("rotation"), Rotation, Error) || !ZettaTool::OptVector(In, TEXT("scale"), Scale, Error)) return Fail(Error);
		const bool bRelative = ZettaTool::BoolOr(In, TEXT("relative"), false);

		const bool bDetach = In->HasField(TEXT("parent")) && !ZettaTool::Has(In, TEXT("parent"));
		AActor* Parent = nullptr;
		if (ZettaTool::Has(In, TEXT("parent")))
		{
			Parent = ZettaTargets::FindActor(ZettaTool::Str(In, TEXT("parent")), Error);
			if (!Parent) return Fail(Error);
			if (Parent == Actor || Parent->IsAttachedTo(Actor)) return Fail(TEXT("An actor can't be attached to itself or to one of its own children."));
		}
		EComponentMobility::Type Mobility = EComponentMobility::Static;
		const bool bMobility = ZettaTool::Has(In, TEXT("mobility"));
		if (bMobility && !ParseMobility(ZettaTool::Str(In, TEXT("mobility")), Mobility)) return Fail(TEXT("'mobility' must be Static, Stationary or Movable."));
		if (bMobility && !Actor->GetRootComponent()) return Fail(TEXT("This actor has no root component, so it has no mobility."));

		const FString Title = ZettaTool::TransactionTitle(FString::Printf(TEXT("Change %s"), *ZettaTargets::Label(Actor)));
		TUniquePtr<FScopedTransaction> Transaction = MakeUnique<FScopedTransaction>(FText::FromString(Title));
		Actor->Modify();
		if (USceneComponent* Root = Actor->GetRootComponent()) Root->Modify();
		TArray<FString> Done;

		if (ZettaTool::Has(In, TEXT("label")))
		{
			Actor->SetActorLabel(ZettaTool::Str(In, TEXT("label")).TrimStartAndEnd());
			Done.Add(FString::Printf(TEXT("label \"%s\""), *Actor->GetActorLabel()));
		}
		if (bDetach && Actor->GetAttachParentActor())
		{
			Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			Done.Add(TEXT("detached"));
		}
		if (Parent)
		{
			Actor->AttachToActor(Parent, FAttachmentTransformRules::KeepWorldTransform);
			Done.Add(TEXT("attached to ") + ZettaTargets::Label(Parent));
		}
		if (Actor->GetRootComponent())
		{
			if (bMobility)
			{
				Actor->GetRootComponent()->SetMobility(Mobility);
				Done.Add(TEXT("mobility ") + ZettaTool::Str(In, TEXT("mobility")));
			}
			if (Location.IsSet())
			{
				Actor->SetActorLocation(bRelative ? Actor->GetActorLocation() + Location.GetValue() : Location.GetValue(), false, nullptr, ETeleportType::TeleportPhysics);
				Done.Add(TEXT("location ") + ZettaTool::Vec(Actor->GetActorLocation()));
			}
			if (Rotation.IsSet())
			{
				const FRotator R(Rotation->X, Rotation->Y, Rotation->Z);
				Actor->SetActorRotation(bRelative ? (Actor->GetActorRotation() + R) : R, ETeleportType::TeleportPhysics);
				Done.Add(TEXT("rotation ") + ZettaTool::Rot(Actor->GetActorRotation()));
			}
			if (Scale.IsSet())
			{
				Actor->SetActorScale3D(bRelative ? Actor->GetActorScale3D() * Scale.GetValue() : Scale.GetValue());
				Done.Add(TEXT("scale ") + ZettaTool::Vec(Actor->GetActorScale3D()));
			}
			if (Location.IsSet() || Rotation.IsSet() || Scale.IsSet()) Actor->PostEditMove(true);
		}
		else if (Location.IsSet() || Rotation.IsSet() || Scale.IsSet())
		{
			Done.Add(TEXT("(no transform: this actor has no root component)"));
		}
		if (ZettaTool::Has(In, TEXT("folder")) || (In->HasField(TEXT("folder")) && !ZettaTool::Has(In, TEXT("folder"))))
		{
			const FString Folder = ZettaTool::Str(In, TEXT("folder")).TrimStartAndEnd().TrimChar(TEXT('/'));
			Actor->SetFolderPath(Folder.IsEmpty() ? NAME_None : FName(*Folder));
			Done.Add(Folder.IsEmpty() ? TEXT("moved out of its folder") : TEXT("folder ") + Folder);
		}
		if (const TOptional<bool> Hidden = ZettaTool::Bool(In, TEXT("hidden")); Hidden.IsSet())
		{
			Actor->SetActorHiddenInGame(Hidden.GetValue());
			Actor->SetIsTemporarilyHiddenInEditor(Hidden.GetValue());
			Done.Add(Hidden.GetValue() ? TEXT("hidden (in game and in the editor)") : TEXT("shown"));
		}
		if (In->HasField(TEXT("tags")))
		{
			Actor->Tags.Reset();
			for (const FString& T : ZettaTool::Strings(In, TEXT("tags"))) Actor->Tags.AddUnique(FName(*T));
			TArray<FString> Names;
			for (const FName& T : Actor->Tags) Names.Add(T.ToString());
			Done.Add(Names.Num() > 0 ? TEXT("tags ") + FString::Join(Names, TEXT(", ")) : TEXT("tags cleared"));
		}
		if (Done.Num() == 0)
		{
			Transaction->Cancel();
			return Fail(TEXT("Nothing to change: give label, location, rotation, scale, parent, folder, hidden, mobility or tags."));
		}
		Actor->PostEditChange();
		Transaction.Reset();
		ZettaAgentChanges::LevelTransaction(Title);
		return Ok(FString::Printf(TEXT("%s: %s."), *ZettaTargets::Describe(Actor), *FString::Join(Done, TEXT(", "))), FString::Printf(TEXT("Changed %s"), *ZettaTargets::Label(Actor)));
	}

	// ─── add_component / remove_component ────────────────────────────────────

	FZettaToolResult AddComponent(const TSharedPtr<FJsonObject>& In)
	{
		FString Error;
		if (RefuseWhilePlaying(Error)) return Fail(Error);
		AActor* Actor = ZettaTargets::FindActor(ZettaTool::Str(In, TEXT("target")), Error);
		if (!Actor) return Fail(Error);
		FString TypeName = ZettaTool::Str(In, TEXT("type")).TrimStartAndEnd();
		if (TypeName.IsEmpty()) return Fail(TEXT("'type' is required (\"PointLightComponent\", \"BoxComponent\", \"/Script/MyGame.HealthComponent\")."));
		UClass* Class = ZettaTargets::FindClass(TypeName, UActorComponent::StaticClass(), Error);
		if (!Class && !TypeName.EndsWith(TEXT("Component")) && !TypeName.StartsWith(TEXT("/")))
		{
			FString Second;
			Class = ZettaTargets::FindClass(TypeName + TEXT("Component"), UActorComponent::StaticClass(), Second);
		}
		if (!Class) return Fail(Error);
		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)) return Fail(FString::Printf(TEXT("%s is abstract; pick a concrete component class."), *Class->GetName()));

		USceneComponent* AttachTo = nullptr;
		if (ZettaTool::Has(In, TEXT("attachTo")))
		{
			AttachTo = Cast<USceneComponent>(ZettaTargets::FindComponent(Actor, ZettaTool::Str(In, TEXT("attachTo"))));
			if (!AttachTo) return Fail(FString::Printf(TEXT("%s has no scene component \"%s\" to attach to."), *ZettaTargets::Describe(Actor), *ZettaTool::Str(In, TEXT("attachTo"))));
		}
		FString Name = ZettaTool::Str(In, TEXT("name")).TrimStartAndEnd();
		if (!Name.IsEmpty() && ZettaTargets::FindComponent(Actor, Name) && ZettaTargets::FindComponent(Actor, Name)->GetName().Equals(Name, ESearchCase::IgnoreCase))
		{
			return Fail(FString::Printf(TEXT("%s already has a component named \"%s\". Pick another name."), *ZettaTargets::Label(Actor), *Name));
		}
		const FName ObjectName = MakeUniqueObjectName(Actor, Class, Name.IsEmpty() ? FName(*Class->GetName().Replace(TEXT("Component"), TEXT(""))) : FName(*Name));

		const FString Title = ZettaTool::TransactionTitle(FString::Printf(TEXT("Add %s to %s"), *Class->GetName(), *ZettaTargets::Label(Actor)));
		TUniquePtr<FScopedTransaction> Transaction = MakeUnique<FScopedTransaction>(FText::FromString(Title));
		Actor->Modify();
		UActorComponent* Component = NewObject<UActorComponent>(Actor, Class, ObjectName, RF_Transactional);
		FString Where;
		if (USceneComponent* Scene = Cast<USceneComponent>(Component))
		{
			USceneComponent* Root = Actor->GetRootComponent();
			if (!Root)
			{
				Actor->SetRootComponent(Scene);
				Where = TEXT(" as the root");
			}
			else
			{
				USceneComponent* ParentComponent = AttachTo ? AttachTo : Root;
				ParentComponent->Modify();
				Scene->SetupAttachment(ParentComponent);
				Where = TEXT(" under ") + ParentComponent->GetName();
			}
		}
		Actor->AddInstanceComponent(Component);
		Component->OnComponentCreated();
		Component->RegisterComponent();

		TArray<FString> Lines, Errors;
		ZettaProps::WriteAll(Component, ZettaTool::Obj(In, TEXT("properties")), Lines, Errors);
		Actor->RerunConstructionScripts();
		Transaction.Reset();
		ZettaAgentChanges::LevelTransaction(Title);

		FString Text = FString::Printf(TEXT("Added %s (%s) to %s%s. Target it as \"%s/%s\"."), *Component->GetName(), *Class->GetName(), *ZettaTargets::Describe(Actor), *Where,
			*ZettaTargets::TargetOf(Actor), *Component->GetName());
		if (Lines.Num() > 0) Text += TEXT("\nSet:") + JoinLines(Lines);
		if (Errors.Num() > 0) Text += TEXT("\nNot set:") + JoinLines(Errors);
		return Ok(Text, FString::Printf(TEXT("Added %s"), *Component->GetName()));
	}

	FZettaToolResult RemoveComponent(const TSharedPtr<FJsonObject>& In)
	{
		FString Error;
		if (RefuseWhilePlaying(Error)) return Fail(Error);
		AActor* Actor = nullptr;
		UActorComponent* Component = nullptr;
		if (!ZettaTargets::FindActorComponent(ZettaTool::Str(In, TEXT("target")), Actor, Component, Error)) return Fail(Error);
		if (Component->CreationMethod != EComponentCreationMethod::Instance)
		{
			const bool bFromBlueprint = Component->CreationMethod == EComponentCreationMethod::SimpleConstructionScript;
			return Fail(FString::Printf(TEXT("%s comes from the actor's class (%s), so it can't be removed from this one actor.%s"), *Component->GetName(),
				bFromBlueprint ? TEXT("its Blueprint") : TEXT("C++"),
				bFromBlueprint ? TEXT(" Remove it from the Blueprint with edit_blueprint (remove_component), or turn it off with set_properties.") : TEXT(" Turn it off with set_properties instead.")));
		}
		USceneComponent* Scene = Cast<USceneComponent>(Component);
		if (Scene && Scene == Actor->GetRootComponent() && Scene->GetNumChildrenComponents() > 0)
		{
			return Fail(FString::Printf(TEXT("%s is the root of %s and has components attached; make another component the root first."), *Component->GetName(), *ZettaTargets::Label(Actor)));
		}
		const FString Name = Component->GetName();
		const FString Title = ZettaTool::TransactionTitle(FString::Printf(TEXT("Remove %s from %s"), *Name, *ZettaTargets::Label(Actor)));
		TUniquePtr<FScopedTransaction> Transaction = MakeUnique<FScopedTransaction>(FText::FromString(Title));
		Actor->Modify();
		Component->Modify();
		Actor->RemoveInstanceComponent(Component);
		Component->DestroyComponent(true);
		Transaction.Reset();
		ZettaAgentChanges::LevelTransaction(Title);
		return Ok(FString::Printf(TEXT("Removed %s from %s."), *Name, *ZettaTargets::Describe(Actor)), FString::Printf(TEXT("Removed %s"), *Name));
	}

	// ─── delete_actors ───────────────────────────────────────────────────────

	bool CollectActors(const TSharedPtr<FJsonObject>& In, const FString& Key, TArray<AActor*>& Out, FString& OutError)
	{
		const TArray<FString> Targets = ZettaTool::Strings(In, Key);
		if (Targets.Num() == 0)
		{
			OutError = FString::Printf(TEXT("'%s' is required: a list of actor targets."), *Key);
			return false;
		}
		TArray<FString> Missing;
		for (const FString& T : Targets)
		{
			FString Error;
			if (AActor* A = ZettaTargets::FindActor(T, Error)) Out.AddUnique(A);
			else Missing.Add(Error);
		}
		if (Missing.Num() > 0)
		{
			OutError = FString::Join(Missing, TEXT(" "));
			return false;
		}
		return true;
	}

	FZettaToolResult DeleteActors(const TSharedPtr<FJsonObject>& In)
	{
		FString Error;
		if (RefuseWhilePlaying(Error)) return Fail(Error);
		TArray<AActor*> Actors;
		if (!CollectActors(In, TEXT("targets"), Actors, Error)) return Fail(Error);
		if (Actors.Num() > 100) return Fail(TEXT("At most 100 actors per call."));
		TArray<FString> Names;
		for (const AActor* A : Actors) Names.Add(ZettaTargets::Label(A));
		const FString Title = ZettaTool::TransactionTitle(Actors.Num() == 1 ? FString::Printf(TEXT("Delete %s"), *Names[0]) : FString::Printf(TEXT("Delete %d actors"), Actors.Num()));
		TUniquePtr<FScopedTransaction> Transaction = MakeUnique<FScopedTransaction>(FText::FromString(Title));
		UEditorActorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
		if (!Subsystem || !Subsystem->DestroyActors(Actors))
		{
			Transaction->Cancel();
			return Fail(TEXT("Unreal could not delete the actors (are they locked or in a level that is not loaded?)."));
		}
		Transaction.Reset();
		ZettaAgentChanges::LevelTransaction(Title);
		return Ok(FString::Printf(TEXT("Deleted %s: %s. Edit › Undo brings them back."), *ZettaTool::Plural(Names.Num(), TEXT("actor"), TEXT("actors")), *ZettaTool::JoinQuoted(Names, 30)),
			FString::Printf(TEXT("Deleted %s"), *ZettaTool::Plural(Names.Num(), TEXT("actor"), TEXT("actors"))));
	}

	FZettaToolPreview DescribeDelete(const TSharedPtr<FJsonObject>& In)
	{
		TArray<AActor*> Actors;
		FString Error;
		if (!CollectActors(In, TEXT("targets"), Actors, Error)) return ZettaTool::PreviewError(TEXT("Delete actors"), Error);
		TArray<FString> Names;
		for (const AActor* A : Actors) Names.Add(ZettaTargets::Describe(A));
		return ZettaTool::Preview(Actors.Num() == 1 ? FString::Printf(TEXT("Delete %s"), *ZettaTargets::Label(Actors[0])) : FString::Printf(TEXT("Delete %d actors"), Actors.Num()),
			FString::Join(Names, TEXT("\n")).Left(3000));
	}

	// ─── set_properties ──────────────────────────────────────────────────────

	FZettaToolResult SetProperties(const TSharedPtr<FJsonObject>& In)
	{
		FString Error;
		const TSharedPtr<FJsonObject> Properties = ZettaTool::Obj(In, TEXT("properties"));
		if (!Properties.IsValid() || Properties->Values.Num() == 0) return Fail(TEXT("'properties' is required: { \"<path>\": value }."));
		if (Properties->Values.Num() > 40) return Fail(TEXT("At most 40 properties per call."));
		ZettaTargets::FObjectTarget Target;
		if (!ZettaTargets::ResolveObject(ZettaTool::Str(In, TEXT("target")), true, Target, Error)) return Fail(Error);
		if (!Target.bAsset && RefuseWhilePlaying(Error)) return Fail(Error);

		const FString AssetPath = Target.bAsset ? ZettaTool::PackagePath((Target.Blueprint ? (UObject*)Target.Blueprint : Target.Object)->GetPathName()) : FString();
		if (Target.bAsset) ZettaAgentChanges::WillChangeAsset(AssetPath, TEXT("changed (properties set)"));

		const FString Title = ZettaTool::TransactionTitle(FString::Printf(TEXT("Set %s on %s"), *ZettaTool::Plural(Properties->Values.Num(), TEXT("property"), TEXT("properties")), *Target.Label));
		TArray<FString> Lines, Errors;
		{
			TUniquePtr<FScopedTransaction> Transaction = MakeUnique<FScopedTransaction>(FText::FromString(Title));
			ZettaProps::WriteAll(Target.Object, Properties, Lines, Errors);
			if (Lines.Num() == 0) Transaction->Cancel();
		}
		if (Lines.Num() == 0) return Fail(FString::Printf(TEXT("Nothing was set on %s. %s"), *Target.Label, *FString::Join(Errors, TEXT(" "))));

		FString Text = FString::Printf(TEXT("%s:%s"), *Target.Label, *JoinLines(Lines));
		if (Errors.Num() > 0) Text += TEXT("\nNot set:") + JoinLines(Errors);
		if (Target.Blueprint)
		{
			bool bCompiled = false;
			Text += TEXT("\n") + ZettaAssets::CompileAndSave(Target.Blueprint, bCompiled);
		}
		else if (Target.bAsset)
		{
			FString SaveError;
			Text += ZettaAssets::Save(Target.Object, SaveError) ? TEXT("\nSaved.") : TEXT("\nNot saved: ") + SaveError;
		}
		else
		{
			ZettaAgentChanges::LevelTransaction(Title);
		}
		FZettaToolResult R = Ok(Text, FString::Printf(TEXT("Set %s on %s"), *ZettaTool::Plural(Lines.Num(), TEXT("property"), TEXT("properties")), *Target.Label));
		if (Errors.Num() > 0 && Lines.Num() == 0) R.bIsError = true;
		return R;
	}

	// ─── level_operation ─────────────────────────────────────────────────────

	bool IsUntitled(UWorld* World)
	{
		return !World || FPackageName::IsTempPackage(World->GetOutermost()->GetName()) || World->GetOutermost()->GetName().StartsWith(TEXT("/Temp/"));
	}

	/** Unreal's own "Save Content" prompt for unsaved levels; false when the user cancelled it. */
	bool AskToSaveDirtyLevels()
	{
		return FEditorFileUtils::SaveDirtyPackages(true, true, false);
	}

	FZettaToolResult LevelOperation(const TSharedPtr<FJsonObject>& In)
	{
		FString Error;
		const FString Action = ZettaTool::Str(In, TEXT("action")).TrimStartAndEnd().ToLower();
		if (ZettaTool::IsPlaying()) return Fail(TEXT("Play In Editor is running. Stop it first (play_in_editor stop)."));
		ULevelEditorSubsystem* Levels = GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
		UWorld* World = ZettaTool::EditorWorld();
		if (!Levels || !World) return Fail(TEXT("The level editor is not available."));
		FString Package;
		const bool bHasPath = ZettaTool::Has(In, TEXT("path"));
		if (bHasPath && !ZettaTool::WritableContentPath(ZettaTool::Str(In, TEXT("path")), Package, Error) && Action != TEXT("open")) return Fail(Error);
		if (Action == TEXT("open")) Package = ZettaTool::PackagePath(ZettaTool::Str(In, TEXT("path")));

		if (Action == TEXT("save"))
		{
			if (IsUntitled(World)) return Fail(TEXT("This level has never been saved. Use action save_as with a path (\"/Game/Maps/MyLevel\")."));
			if (!Levels->SaveCurrentLevel()) return Fail(TEXT("Unreal could not save the level (is the file read-only or checked in?)."));
			return Ok(FString::Printf(TEXT("Saved %s."), *World->GetOutermost()->GetName()), TEXT("Saved the level"));
		}
		if (Action == TEXT("save_all"))
		{
			if (!UEditorLoadingAndSavingUtils::SaveDirtyPackages(true, true)) return Fail(TEXT("Some packages could not be saved (read-only or checked in?). The Output Log names them."));
			return Ok(TEXT("Saved every unsaved level and asset."), TEXT("Saved all"));
		}
		if (Action == TEXT("save_as"))
		{
			if (!bHasPath) return Fail(TEXT("save_as needs a 'path' (\"/Game/Maps/MyLevel\")."));
			if (ZettaTargets::AssetExists(Package)) return Fail(FString::Printf(TEXT("%s already exists. Pick another path."), *Package));
			if (!UEditorLoadingAndSavingUtils::SaveMap(World, Package)) return Fail(FString::Printf(TEXT("Unreal could not save the level as %s."), *Package));
			ZettaAgentChanges::CreatedAsset(Package);
			return Ok(FString::Printf(TEXT("Saved the level as %s; it is now the open level."), *Package), TEXT("Saved the level as…"));
		}
		if (Action == TEXT("open"))
		{
			if (!bHasPath) return Fail(TEXT("open needs a 'path' to a level (\"/Game/Maps/Main\")."));
			UObject* Asset = ZettaTargets::LoadAsset(Package, Error);
			if (!Asset) return Fail(Error);
			if (!Asset->IsA<UWorld>()) return Fail(FString::Printf(TEXT("%s is a %s, not a level."), *Package, *Asset->GetClass()->GetName()));
			if (!AskToSaveDirtyLevels()) return Fail(TEXT("The user cancelled saving the open level, so the other level was not opened."));
			if (!Levels->LoadLevel(Package)) return Fail(FString::Printf(TEXT("Unreal could not open %s."), *Package));
			return Ok(FString::Printf(TEXT("Opened %s."), *Package), TEXT("Opened a level"));
		}
		if (Action == TEXT("new"))
		{
			const FString Template = ZettaTool::Str(In, TEXT("template"), TEXT("basic")).ToLower();
			if (Template != TEXT("basic") && Template != TEXT("empty")) return Fail(TEXT("'template' must be empty or basic."));
			if (bHasPath && ZettaTargets::AssetExists(Package)) return Fail(FString::Printf(TEXT("%s already exists. Open it, or pick another path."), *Package));
			if (!AskToSaveDirtyLevels()) return Fail(TEXT("The user cancelled saving the open level, so no new level was made."));
			const FString TemplateMap = TEXT("/Engine/Maps/Templates/Template_Default");
			bool bOk;
			if (bHasPath)
			{
				bOk = Template == TEXT("basic") ? Levels->NewLevelFromTemplate(Package, TemplateMap) : Levels->NewLevel(Package);
				if (bOk) ZettaAgentChanges::CreatedAsset(Package);
			}
			else
			{
				bOk = (Template == TEXT("basic") ? UEditorLoadingAndSavingUtils::NewMapFromTemplate(TemplateMap, false) : UEditorLoadingAndSavingUtils::NewBlankMap(false)) != nullptr;
			}
			if (!bOk) return Fail(TEXT("Unreal could not make the new level."));
			return Ok(FString::Printf(TEXT("Made a new %s level%s and opened it.%s"), Template == TEXT("basic") ? TEXT("basic (sky, light, floor)") : TEXT("empty"),
				bHasPath ? *FString::Printf(TEXT(" at %s"), *Package) : TEXT(""), bHasPath ? TEXT("") : TEXT(" It is unsaved: use save_as with a path to keep it.")), TEXT("New level"));
		}
		return Fail(TEXT("'action' must be new, open, save, save_as or save_all."));
	}

	FZettaToolPreview DescribeLevelOperation(const TSharedPtr<FJsonObject>& In)
	{
		const FString Action = ZettaTool::Str(In, TEXT("action")).ToLower();
		const FString Path = ZettaTool::Str(In, TEXT("path"));
		UWorld* World = ZettaTool::EditorWorld();
		const bool bDirty = World && World->GetOutermost()->IsDirty();
		FString Body;
		FString Title = TEXT("Level");
		if (Action == TEXT("open")) Title = FString::Printf(TEXT("Open %s"), *Path);
		else if (Action == TEXT("new")) Title = TEXT("New level");
		else if (Action == TEXT("save")) Title = TEXT("Save the level");
		else if (Action == TEXT("save_as")) Title = FString::Printf(TEXT("Save the level as %s"), *Path);
		else if (Action == TEXT("save_all")) Title = TEXT("Save all");
		if ((Action == TEXT("open") || Action == TEXT("new")) && bDirty)
		{
			Body = FString::Printf(TEXT("%s has unsaved changes. Unreal will ask whether to save them first."), World ? *World->GetMapName() : TEXT("The open level"));
		}
		return ZettaTool::Preview(Title, Body);
	}

	// ─── play_in_editor ──────────────────────────────────────────────────────

	FZettaToolResult PlayInEditor(const TSharedPtr<FJsonObject>& In)
	{
		const FString Action = ZettaTool::Str(In, TEXT("action")).TrimStartAndEnd().ToLower();
		ULevelEditorSubsystem* Levels = GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
		if (!Levels) return Fail(TEXT("The level editor is not available."));
		const bool bPlaying = ZettaTool::IsPlaying();
		if (Action == TEXT("start") || Action == TEXT("play") || Action == TEXT("simulate"))
		{
			if (bPlaying) return Ok(TEXT("Play In Editor is already running. Use capture_view with view \"game\" to see it."));
			if (Action == TEXT("simulate")) Levels->EditorPlaySimulate();
			else Levels->EditorRequestBeginPlay();
			return Ok(FString::Printf(TEXT("%s starts on the next frame. Use capture_view (view \"game\") to see it and output_log for errors; stop it with play_in_editor stop."),
				Action == TEXT("simulate") ? TEXT("Simulate") : TEXT("Play In Editor")), TEXT("Started playing"));
		}
		if (!bPlaying) return Ok(TEXT("Play In Editor is not running."));
		if (Action == TEXT("stop"))
		{
			Levels->EditorRequestEndPlay();
			return Ok(TEXT("Play In Editor stops on the next frame."), TEXT("Stopped playing"));
		}
		if (Action == TEXT("pause") || Action == TEXT("resume"))
		{
			GEditor->SetPIEWorldsPaused(Action == TEXT("pause"));
			return Ok(Action == TEXT("pause") ? TEXT("Paused.") : TEXT("Resumed."));
		}
		return Fail(TEXT("'action' must be start, simulate, stop, pause or resume."));
	}

	// ─── console_command ─────────────────────────────────────────────────────

	/** Why a console command is refused, or empty. */
	FString RefuseCommand(const FString& Command)
	{
		const FString C = Command.TrimStartAndEnd().ToLower();
		FString Verb = C;
		int32 Space;
		if (C.FindChar(TEXT(' '), Space)) Verb = C.Left(Space);
		const FString Rest = C.RightChop(Verb.Len()).TrimStartAndEnd();
		static const TCHAR* Quit[] = { TEXT("quit"), TEXT("exit"), TEXT("quit_editor"), TEXT("crash"), TEXT("gpf"), TEXT("forcequit"), TEXT("shutdownafterserver") };
		for (const TCHAR* Q : Quit)
		{
			if (Verb == Q) return TEXT("it would close or crash the editor");
		}
		if (Verb == TEXT("debug") && (Rest.StartsWith(TEXT("crash")) || Rest.StartsWith(TEXT("assert")) || Rest.StartsWith(TEXT("ensure")) || Rest.StartsWith(TEXT("gpf")) || Rest.StartsWith(TEXT("hang")) || Rest.StartsWith(TEXT("eatmem"))))
		{
			return TEXT("it would crash or hang the editor");
		}
		if (Verb == TEXT("py") || Verb == TEXT("python")) return TEXT("Python runs through run_python, which shows the code first");
		if (Verb == TEXT("savesettings") || Verb == TEXT("saveconfig") || (Verb == TEXT("scalability") && Rest.Contains(TEXT("save"))) || Verb == TEXT("config") || Verb == TEXT("ini"))
		{
			return TEXT("it would change engine settings permanently");
		}
		if (Verb == TEXT("open") || Verb == TEXT("travel") || Verb == TEXT("servertravel") || Verb == TEXT("exec") || Verb == TEXT("loadpackage") || Verb == TEXT("map"))
		{
			if (Rest.Contains(TEXT(":")) || (Rest.StartsWith(TEXT("/")) && !Rest.StartsWith(TEXT("/game/"))) || Rest.Contains(TEXT("..")) || Rest.Contains(TEXT("\\")))
			{
				return TEXT("it would open a file outside the project");
			}
			if (Verb == TEXT("exec")) return TEXT("it runs a file of commands the agent has not shown; run them one by one instead");
		}
		return FString();
	}

	FZettaToolResult ConsoleCommand(const TSharedPtr<FJsonObject>& In)
	{
		const FString Command = ZettaTool::Str(In, TEXT("command")).TrimStartAndEnd();
		if (Command.IsEmpty()) return Fail(TEXT("'command' is required."));
		const FString Refusal = RefuseCommand(Command);
		if (!Refusal.IsEmpty()) return Fail(FString::Printf(TEXT("Refused: %s."), *Refusal));
		UWorld* World = ZettaTool::IsPlaying() && ZettaTool::PlayWorld() ? ZettaTool::PlayWorld() : ZettaTool::EditorWorld();
		FStringOutputDevice Output;
		Output.SetAutoEmitLineTerminator(true);
		const bool bHandled = GEditor && GEditor->Exec(World, *Command, Output);
		FString Text = Output.TrimStartAndEnd();
		if (!bHandled && Text.IsEmpty()) return Fail(FString::Printf(TEXT("Unreal did not recognise \"%s\" as a console command."), *Command));
		return Ok(FString::Printf(TEXT("Ran \"%s\"%s.%s"), *Command, ZettaTool::IsPlaying() ? TEXT(" in the Play In Editor world") : TEXT(""),
			Text.IsEmpty() ? TEXT(" (no output)") : *(TEXT("\n") + ZettaTool::Clip(Text, 20000))), FString::Printf(TEXT("Ran %s"), *ZettaTool::Short(Command, 40)));
	}

	// ─── viewport ────────────────────────────────────────────────────────────

	FLevelEditorViewportClient* ActiveViewport()
	{
		if (GCurrentLevelEditingViewportClient) return GCurrentLevelEditingViewportClient;
		if (GEditor)
		{
			for (FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
			{
				if (Client && Client->IsPerspective()) return Client;
			}
		}
		return nullptr;
	}

	FZettaToolResult Viewport(const TSharedPtr<FJsonObject>& In)
	{
		const FString Action = ZettaTool::Str(In, TEXT("action")).TrimStartAndEnd().ToLower();
		FLevelEditorViewportClient* Client = ActiveViewport();
		if (!Client) return Fail(TEXT("No level viewport is open."));
		if (Action == TEXT("focus"))
		{
			TArray<AActor*> Actors;
			FString Error;
			if (!CollectActors(In, TEXT("targets"), Actors, Error)) return Fail(Error);
			GEditor->MoveViewportCamerasToActor(Actors, true);
			return Ok(FString::Printf(TEXT("The viewport now frames %s."), *ZettaTool::Plural(Actors.Num(), TEXT("actor"), TEXT("actors"))));
		}
		if (Action == TEXT("view"))
		{
			const FString View = ZettaTool::Str(In, TEXT("view"), TEXT("perspective")).ToLower();
			ELevelViewportType Type = LVT_Perspective;
			if (View == TEXT("top")) Type = LVT_OrthoTop;
			else if (View == TEXT("front")) Type = LVT_OrthoFront;
			else if (View == TEXT("side") || View == TEXT("right")) Type = LVT_OrthoRight;
			else if (View == TEXT("left")) Type = LVT_OrthoLeft;
			else if (View != TEXT("perspective")) return Fail(TEXT("'view' must be perspective, top, front or side."));
			Client->SetViewportType(Type);
			Client->Invalidate();
			return Ok(FString::Printf(TEXT("The viewport shows the %s view."), *View));
		}
		if (Action == TEXT("game_view"))
		{
			const bool bNow = !Client->IsInGameView();
			Client->SetGameView(bNow);
			Client->Invalidate();
			return Ok(bNow ? TEXT("Game View is on: editor icons and gizmos are hidden.") : TEXT("Game View is off."));
		}
		return Fail(TEXT("'action' must be focus, view or game_view."));
	}

	// ─── undo ────────────────────────────────────────────────────────────────

	FZettaToolResult Undo(const TSharedPtr<FJsonObject>& In)
	{
		if (!GEditor || !GEditor->Trans) return Fail(TEXT("The undo history is not available."));
		const int32 Steps = ZettaTool::IntIn(In, TEXT("steps"), 1, 1, 20);
		TArray<FString> Undone;
		FString Stopped;
		for (int32 i = 0; i < Steps; ++i)
		{
			if (!GEditor->Trans->CanUndo())
			{
				Stopped = TEXT("there is nothing more to undo");
				break;
			}
			const FString Title = GEditor->Trans->GetUndoContext(false).Title.ToString();
			if (!Title.StartsWith(ZettaTool::TransactionPrefix))
			{
				Stopped = FString::Printf(TEXT("the newest step is the user's own (\"%s\"), which the agent leaves alone"), *Title);
				break;
			}
			if (!GEditor->UndoTransaction()) break;
			Undone.Add(Title.RightChop(FCString::Strlen(ZettaTool::TransactionPrefix)));
		}
		if (Undone.Num() == 0) return Fail(FString::Printf(TEXT("Nothing undone: %s."), Stopped.IsEmpty() ? TEXT("the undo failed") : *Stopped));
		FString Text = FString::Printf(TEXT("Undid %s: %s."), *ZettaTool::Plural(Undone.Num(), TEXT("step"), TEXT("steps")), *ZettaTool::JoinQuoted(Undone, 20));
		if (!Stopped.IsEmpty()) Text += FString::Printf(TEXT(" Stopped because %s."), *Stopped);
		Text += TEXT(" File writes and saved assets are not part of this history.");
		return Ok(Text, FString::Printf(TEXT("Undid %s"), *ZettaTool::Plural(Undone.Num(), TEXT("step"), TEXT("steps"))));
	}
}

void ZettaToolAreas::RegisterLevelTools(FZettaAgentTools& Tools)
{
	using namespace ZettaLevelImpl;
	using ZettaTool::Preview;
	Tools.Register(TEXT("spawn_actor"), &SpawnActor, [](const TSharedPtr<FJsonObject>& In)
	{
		const FString What = ZettaTool::Str(In, TEXT("label"), ZettaTool::Str(In, TEXT("class"), FPaths::GetBaseFilename(ZettaTool::Str(In, TEXT("mesh"), TEXT("an actor")))));
		return Preview(FString::Printf(TEXT("Spawn %s"), *What), ZettaTool::Has(In, TEXT("location")) ? FString::Printf(TEXT("At %s"), *ZettaTool::Str(In, TEXT("location"))) : FString());
	});
	Tools.Register(TEXT("modify_actor"), &ModifyActor, [](const TSharedPtr<FJsonObject>& In) { return Preview(FString::Printf(TEXT("Change %s"), *ZettaTool::Str(In, TEXT("target")))); });
	Tools.Register(TEXT("add_component"), &AddComponent, [](const TSharedPtr<FJsonObject>& In)
	{
		return Preview(FString::Printf(TEXT("Add %s to %s"), *ZettaTool::Str(In, TEXT("type")), *ZettaTool::Str(In, TEXT("target"))));
	});
	Tools.Register(TEXT("remove_component"), &RemoveComponent, [](const TSharedPtr<FJsonObject>& In) { return Preview(FString::Printf(TEXT("Remove %s"), *ZettaTool::Str(In, TEXT("target")))); });
	Tools.Register(TEXT("delete_actors"), &DeleteActors, &DescribeDelete);
	Tools.Register(TEXT("set_properties"), &SetProperties, [](const TSharedPtr<FJsonObject>& In)
	{
		const TSharedPtr<FJsonObject> P = ZettaTool::Obj(In, TEXT("properties"));
		TArray<FString> Lines;
		if (P.IsValid())
		{
			for (const auto& Pair : P->Values)
			{
				FString Value;
				if (Pair.Value.IsValid())
				{
					const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Value);
					FJsonSerializer::Serialize(Pair.Value, TEXT(""), W);
				}
				Lines.Add(FString::Printf(TEXT("%s = %s"), *FString(Pair.Key), *ZettaTool::Short(Value, 120)));
			}
		}
		return Preview(FString::Printf(TEXT("Set %s on %s"), *ZettaTool::Plural(Lines.Num(), TEXT("property"), TEXT("properties")), *ZettaTool::Str(In, TEXT("target"))), FString::Join(Lines, TEXT("\n")));
	});
	Tools.Register(TEXT("level_operation"), &LevelOperation, &DescribeLevelOperation);
	Tools.Register(TEXT("play_in_editor"), &PlayInEditor, [](const TSharedPtr<FJsonObject>& In)
	{
		const FString A = ZettaTool::Str(In, TEXT("action")).ToLower();
		return Preview(A == TEXT("stop") ? TEXT("Stop playing") : A == TEXT("pause") ? TEXT("Pause the game") : A == TEXT("resume") ? TEXT("Resume the game") : A == TEXT("simulate") ? TEXT("Simulate") : TEXT("Play in the editor"));
	});
	Tools.Register(TEXT("console_command"), &ConsoleCommand, [](const TSharedPtr<FJsonObject>& In)
	{
		FZettaToolPreview P = Preview(TEXT("Run a console command"), ZettaTool::Str(In, TEXT("command")));
		const FString Refusal = RefuseCommand(ZettaTool::Str(In, TEXT("command")));
		if (!Refusal.IsEmpty()) P.Error = FString::Printf(TEXT("Refused: %s."), *Refusal);
		return P;
	});
	Tools.Register(TEXT("viewport"), &Viewport, [](const TSharedPtr<FJsonObject>& In) { return Preview(TEXT("Change the viewport")); });
	Tools.Register(TEXT("undo"), &Undo, [](const TSharedPtr<FJsonObject>& In)
	{
		return Preview(FString::Printf(TEXT("Undo %s"), *ZettaTool::Plural(ZettaTool::IntIn(In, TEXT("steps"), 1, 1, 20), TEXT("step"), TEXT("steps"))), TEXT("Steps back the agent's own changes (never the user's)."));
	});
}
