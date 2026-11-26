// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaTargets.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectIterator.h"
#include "ZettaToolCommon.h"

namespace ZettaTargetsImpl
{
	int32 Distance(const FString& A, const FString& B)
	{
		const FString X = A.ToLower(), Y = B.ToLower();
		TArray<int32> Prev, Cur;
		Prev.SetNumUninitialized(Y.Len() + 1);
		Cur.SetNumUninitialized(Y.Len() + 1);
		for (int32 j = 0; j <= Y.Len(); ++j) Prev[j] = j;
		for (int32 i = 1; i <= X.Len(); ++i)
		{
			Cur[0] = i;
			for (int32 j = 1; j <= Y.Len(); ++j)
			{
				const int32 Cost = X[i - 1] == Y[j - 1] ? 0 : 1;
				Cur[j] = FMath::Min3(Prev[j] + 1, Cur[j - 1] + 1, Prev[j - 1] + Cost);
			}
			Swap(Prev, Cur);
		}
		return Prev[Y.Len()];
	}

	/** The world the targets live in: the Play In Editor world while a session runs and the editor level otherwise. */
	UWorld* TargetWorld()
	{
		return ZettaTool::EditorWorld();
	}
}

namespace ZettaTargets
{
	TArray<AActor*> LevelActors()
	{
		TArray<AActor*> Out;
		UWorld* World = ZettaTargetsImpl::TargetWorld();
		if (!World) return Out;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!IsValid(Actor) || Actor->IsTemplate() || !Actor->IsListedInSceneOutliner()) continue;
			Out.Add(Actor);
		}
		return Out;
	}

	FString Label(const AActor* Actor) { return Actor ? Actor->GetActorLabel() : FString(); }

	FString FolderAndLabel(const AActor* Actor)
	{
		if (!Actor) return FString();
		const FName Folder = Actor->GetFolderPath();
		return Folder.IsNone() ? Label(Actor) : Folder.ToString() + TEXT("/") + Label(Actor);
	}

	FString TargetOf(const AActor* Actor)
	{
		if (!Actor) return FString();
		const FString L = Label(Actor);
		int32 Same = 0;
		for (const AActor* Other : LevelActors())
		{
			if (Other->GetActorLabel() == L && ++Same > 1) break;
		}
		return Same <= 1 ? L : TEXT("#") + Actor->GetName();
	}

	FString Describe(const AActor* Actor)
	{
		if (!Actor) return TEXT("(none)");
		return FString::Printf(TEXT("%s (#%s, %s)"), *Label(Actor), *Actor->GetName(), *Actor->GetClass()->GetName());
	}

	FString DescribeComponent(const UActorComponent* Component)
	{
		if (!Component) return TEXT("(none)");
		return FString::Printf(TEXT("%s/%s (%s)"), *Label(Component->GetOwner()), *Component->GetName(), *Component->GetClass()->GetName());
	}

	AActor* FindActor(const FString& Target, FString& OutError)
	{
		const FString T = Target.TrimStartAndEnd();
		if (T.IsEmpty())
		{
			OutError = TEXT("The target is empty. Name an actor by its label, \"#ObjectName\" or \"Folder/Label\".");
			return nullptr;
		}
		const TArray<AActor*> Actors = LevelActors();
		if (T.StartsWith(TEXT("#")))
		{
			const FString Name = T.RightChop(1);
			for (AActor* A : Actors)
			{
				if (A->GetName().Equals(Name, ESearchCase::IgnoreCase)) return A;
			}
		}
		else
		{
			for (AActor* A : Actors)
			{
				if (A->GetActorLabel() == T) return A;
			}
			for (AActor* A : Actors)
			{
				if (FolderAndLabel(A) == T) return A;
			}
			for (AActor* A : Actors)
			{
				if (A->GetActorLabel().Equals(T, ESearchCase::IgnoreCase) || FolderAndLabel(A).Equals(T, ESearchCase::IgnoreCase)) return A;
			}
			// The object name without '#' still works.
			for (AActor* A : Actors)
			{
				if (A->GetName().Equals(T, ESearchCase::IgnoreCase)) return A;
			}
		}
		TArray<FString> Names;
		for (const AActor* A : Actors) Names.Add(FolderAndLabel(A));
		const FString Near = NearMatches(T.StartsWith(TEXT("#")) ? T.RightChop(1) : T, Names);
		OutError = FString::Printf(TEXT("No actor \"%s\" in the open level.%s Use find_actors or level_outline to look it up."), *T,
			Near.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" Did you mean %s?"), *Near));
		return nullptr;
	}

	UActorComponent* FindComponent(AActor* Actor, const FString& Name)
	{
		if (!Actor) return nullptr;
		const FString N = Name.TrimStartAndEnd();
		TInlineComponentArray<UActorComponent*> Components(Actor);
		for (UActorComponent* C : Components)
		{
			if (C && C->GetName().Equals(N, ESearchCase::IgnoreCase)) return C;
		}
		// The name in the editor's Details tree (an SCS variable name or a native one) usually is the object name;
		// failing that a class name picks the component when exactly one has it.
		UActorComponent* ByClass = nullptr;
		int32 Count = 0;
		for (UActorComponent* C : Components)
		{
			if (!C) continue;
			const FString ClassName = C->GetClass()->GetName();
			if (ClassName.Equals(N, ESearchCase::IgnoreCase) || (ClassName + TEXT("Component")).Equals(N, ESearchCase::IgnoreCase))
			{
				ByClass = C;
				++Count;
			}
		}
		return Count == 1 ? ByClass : nullptr;
	}

	bool FindActorComponent(const FString& Target, AActor*& OutActor, UActorComponent*& OutComponent, FString& OutError)
	{
		OutActor = nullptr;
		OutComponent = nullptr;
		const FString T = Target.TrimStartAndEnd();
		int32 Slash;
		if (!T.FindLastChar(TEXT('/'), Slash) || Slash == 0 || Slash == T.Len() - 1)
		{
			OutError = FString::Printf(TEXT("\"%s\" is not a component target. Write it as \"Actor/Component\"."), *T);
			return false;
		}
		FString ActorError;
		AActor* Actor = FindActor(T.Left(Slash), ActorError);
		if (!Actor)
		{
			OutError = ActorError;
			return false;
		}
		const FString Name = T.RightChop(Slash + 1);
		UActorComponent* Component = FindComponent(Actor, Name);
		if (!Component)
		{
			TArray<FString> Names;
			TInlineComponentArray<UActorComponent*> Components(Actor);
			for (const UActorComponent* C : Components)
			{
				if (C) Names.Add(C->GetName());
			}
			OutError = FString::Printf(TEXT("%s has no component \"%s\". Its components: %s."), *Describe(Actor), *Name, *ZettaTool::JoinQuoted(Names, 30));
			return false;
		}
		OutActor = Actor;
		OutComponent = Component;
		return true;
	}

	bool AssetExists(const FString& Path)
	{
		const FString Package = ZettaTool::PackagePath(Path);
		if (Package.IsEmpty() || !Package.StartsWith(TEXT("/"))) return false;
		IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
		TArray<FAssetData> Found;
		Registry.GetAssetsByPackageName(FName(*Package), Found, true);
		return Found.Num() > 0 || FindObject<UPackage>(nullptr, *Package) != nullptr;
	}

	UObject* LoadAsset(const FString& Path, FString& OutError, const UClass* Expected)
	{
		const FString Package = ZettaTool::PackagePath(Path);
		if (Package.IsEmpty() || !Package.StartsWith(TEXT("/")))
		{
			OutError = FString::Printf(TEXT("\"%s\" is not a content path. Assets are written \"/Game/Folder/Name\"; use find_assets to look one up."), *Path);
			return nullptr;
		}
		IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
		TArray<FAssetData> Found;
		Registry.GetAssetsByPackageName(FName(*Package), Found, true);
		UObject* Asset = nullptr;
		if (Found.Num() > 0)
		{
			// The package's main asset: the one named like the package, else the first.
			const FAssetData* Main = Found.FindByPredicate([&](const FAssetData& D) { return D.AssetName.ToString() == FPackageName::GetShortName(Package); });
			Asset = (Main ? *Main : Found[0]).GetAsset();
		}
		if (!Asset) Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *ZettaTool::ObjectPath(Package), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Asset)
		{
			// Nearby names in the same folder.
			TArray<FAssetData> Siblings;
			Registry.GetAssetsByPath(FName(*FPackageName::GetLongPackagePath(Package)), Siblings, false);
			TArray<FString> Names;
			for (const FAssetData& D : Siblings) Names.Add(D.PackageName.ToString());
			const FString Near = NearMatches(Package, Names);
			OutError = FString::Printf(TEXT("No asset at \"%s\".%s Use find_assets to look it up."), *Package,
				Near.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" Did you mean %s?"), *Near));
			return nullptr;
		}
		if (Expected && !Asset->IsA(Expected))
		{
			OutError = FString::Printf(TEXT("\"%s\" is a %s, not a %s."), *Package, *Asset->GetClass()->GetName(), *Expected->GetName());
			return nullptr;
		}
		return Asset;
	}

	UClass* FindClass(const FString& InName, const UClass* Base, FString& OutError)
	{
		FString Name = InName.TrimStartAndEnd();
		if (Name.IsEmpty())
		{
			OutError = TEXT("The class is empty.");
			return nullptr;
		}
		UClass* Found = nullptr;
		if (Name.StartsWith(TEXT("/Script/")))
		{
			Found = FindObject<UClass>(nullptr, *Name);
			if (!Found) Found = LoadObject<UClass>(nullptr, *Name, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		else if (Name.StartsWith(TEXT("/")))
		{
			// A Blueprint ("/Game/BP_Enemy"), or its generated class ("/Game/BP_Enemy.BP_Enemy_C").
			if (Name.EndsWith(TEXT("_C")))
			{
				Found = LoadObject<UClass>(nullptr, *Name, nullptr, LOAD_NoWarn | LOAD_Quiet);
			}
			if (!Found)
			{
				FString AssetError;
				UObject* Asset = LoadAsset(Name, AssetError);
				if (UBlueprint* BP = Cast<UBlueprint>(Asset))
				{
					Found = BP->GeneratedClass;
					if (!Found)
					{
						OutError = FString::Printf(TEXT("The Blueprint \"%s\" has no compiled class. Compile it (edit_blueprint) and try again."), *Name);
						return nullptr;
					}
				}
				else if (UClass* AsClass = Cast<UClass>(Asset))
				{
					Found = AsClass;
				}
				else
				{
					OutError = Asset ? FString::Printf(TEXT("\"%s\" is a %s, not a Blueprint or class."), *Name, *Asset->GetClass()->GetName()) : AssetError;
					return nullptr;
				}
			}
		}
		else
		{
			// A short name: "PointLight", "UPointLightComponent", "APawn".
			TArray<FString> Tries = { Name };
			if (Name.Len() > 1 && (Name[0] == TEXT('A') || Name[0] == TEXT('U')) && FChar::IsUpper(Name[1])) Tries.Add(Name.RightChop(1));
			for (const FString& Try : Tries)
			{
				Found = FindFirstObject<UClass>(*Try, EFindFirstObjectOptions::NativeFirst);
				if (Found) break;
			}
			if (!Found)
			{
				// Case-insensitive pass, then nearest names under Base.
				TArray<FString> Names;
				for (TObjectIterator<UClass> It; It; ++It)
				{
					UClass* C = *It;
					if (C->HasAnyClassFlags(CLASS_NewerVersionExists | CLASS_Deprecated) || C->GetName().StartsWith(TEXT("SKEL_")) || C->GetName().StartsWith(TEXT("REINST_"))) continue;
					if (Base && !C->IsChildOf(Base)) continue;
					for (const FString& Try : Tries)
					{
						if (C->GetName().Equals(Try, ESearchCase::IgnoreCase)) Found = C;
					}
					if (Found) break;
					Names.Add(C->GetName());
				}
				if (!Found)
				{
					const FString Near = NearMatches(Name, Names);
					OutError = FString::Printf(TEXT("No class \"%s\".%s Use api_lookup to find classes, or a full path (\"/Script/Engine.PointLight\", \"/Game/Blueprints/BP_Enemy\")."), *Name,
						Near.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" Did you mean %s?"), *Near));
					return nullptr;
				}
			}
		}
		if (!Found)
		{
			OutError = FString::Printf(TEXT("No class \"%s\"."), *Name);
			return nullptr;
		}
		if (Base && !Found->IsChildOf(Base))
		{
			OutError = FString::Printf(TEXT("%s is not a %s."), *Found->GetName(), *Base->GetName());
			return nullptr;
		}
		return Found;
	}

	TArray<FString> BlueprintComponentNames(UBlueprint* Blueprint)
	{
		TArray<FString> Names;
		if (!Blueprint) return Names;
		for (UClass* C = Blueprint->GeneratedClass; C; C = C->GetSuperClass())
		{
			if (UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(C))
			{
				if (BPGC->SimpleConstructionScript)
				{
					for (USCS_Node* Node : BPGC->SimpleConstructionScript->GetAllNodes())
					{
						if (Node) Names.AddUnique(Node->GetVariableName().ToString());
					}
				}
			}
		}
		if (Blueprint->SimpleConstructionScript)
		{
			for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
			{
				if (Node) Names.AddUnique(Node->GetVariableName().ToString());
			}
		}
		if (Blueprint->GeneratedClass)
		{
			if (AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()))
			{
				TInlineComponentArray<UActorComponent*> Native(CDO);
				for (UActorComponent* C : Native)
				{
					if (C) Names.AddUnique(C->GetName());
				}
			}
		}
		return Names;
	}

	bool FindBlueprintComponent(UBlueprint* Blueprint, const FString& InName, bool bForWrite, FBlueprintComponent& Out, FString& OutError)
	{
		Out = FBlueprintComponent();
		const FName Name(*InName.TrimStartAndEnd());
		if (!Blueprint)
		{
			OutError = TEXT("No Blueprint.");
			return false;
		}
		// Its own SCS node.
		if (Blueprint->SimpleConstructionScript)
		{
			if (USCS_Node* Node = Blueprint->SimpleConstructionScript->FindSCSNode(Name))
			{
				Out.Node = Node;
				Out.Template = Node->ComponentTemplate;
				return Out.Template != nullptr;
			}
		}
		// A node a parent Blueprint added: edited through this Blueprint's override of it.
		for (UClass* C = Blueprint->ParentClass; C; C = C->GetSuperClass())
		{
			UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(C);
			if (!BPGC || !BPGC->SimpleConstructionScript) continue;
			if (USCS_Node* Node = BPGC->SimpleConstructionScript->FindSCSNode(Name))
			{
				Out.Node = Node;
				Out.bInherited = true;
				const FComponentKey Key(Node);
				UInheritableComponentHandler* Handler = Blueprint->GetInheritableComponentHandler(bForWrite);
				UActorComponent* Override = Handler ? Handler->GetOverridenComponentTemplate(Key) : nullptr;
				if (!Override && bForWrite && Handler) Override = Handler->CreateOverridenComponentTemplate(Key);
				Out.Template = Override ? Override : Node->ComponentTemplate.Get();
				return Out.Template != nullptr;
			}
		}
		// A native component of the parent C++ class, edited on the Blueprint's class defaults.
		if (Blueprint->GeneratedClass)
		{
			if (AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()))
			{
				TInlineComponentArray<UActorComponent*> Native(CDO);
				for (UActorComponent* Comp : Native)
				{
					if (Comp && Comp->GetFName() == Name)
					{
						Out.Template = Comp;
						Out.bNative = true;
						return true;
					}
				}
			}
		}
		OutError = FString::Printf(TEXT("%s has no component \"%s\". Its components: %s."), *Blueprint->GetPathName(), *Name.ToString(),
			*ZettaTool::JoinQuoted(BlueprintComponentNames(Blueprint), 30));
		return false;
	}

	bool ResolveObject(const FString& InTarget, bool bForWrite, FObjectTarget& Out, FString& OutError)
	{
		Out = FObjectTarget();
		const FString Target = InTarget.TrimStartAndEnd();
		if (Target.StartsWith(TEXT("/")))
		{
			// "/Game/BP_Player:Mesh" — a Blueprint's component template.
			FString AssetPart = Target, ComponentPart;
			int32 Colon;
			if (Target.FindLastChar(TEXT(':'), Colon))
			{
				AssetPart = Target.Left(Colon);
				ComponentPart = Target.RightChop(Colon + 1).TrimStartAndEnd();
			}
			UObject* Asset = LoadAsset(AssetPart, OutError);
			if (!Asset) return false;
			Out.bAsset = true;
			if (UBlueprint* BP = Cast<UBlueprint>(Asset))
			{
				Out.Blueprint = BP;
				if (!ComponentPart.IsEmpty())
				{
					FBlueprintComponent Component;
					if (!FindBlueprintComponent(BP, ComponentPart, bForWrite, Component, OutError)) return false;
					Out.Object = Component.Template;
					Out.Label = FString::Printf(TEXT("%s:%s"), *ZettaTool::PackagePath(BP->GetPathName()), *ComponentPart);
					return true;
				}
				if (!BP->GeneratedClass)
				{
					OutError = FString::Printf(TEXT("The Blueprint %s has no compiled class yet. Compile it with edit_blueprint first."), *AssetPart);
					return false;
				}
				Out.Object = BP->GeneratedClass->GetDefaultObject();
				Out.Label = FString::Printf(TEXT("%s (class defaults)"), *ZettaTool::PackagePath(BP->GetPathName()));
				return true;
			}
			if (!ComponentPart.IsEmpty())
			{
				OutError = FString::Printf(TEXT("\"%s\" is a %s, not a Blueprint, so it has no component \"%s\"."), *AssetPart, *Asset->GetClass()->GetName(), *ComponentPart);
				return false;
			}
			Out.Object = Asset;
			Out.Label = ZettaTool::PackagePath(Asset->GetPathName());
			return true;
		}

		FString ActorError;
		if (AActor* Actor = FindActor(Target, ActorError))
		{
			Out.Object = Actor;
			Out.Actor = Actor;
			Out.Label = Label(Actor);
			return true;
		}
		if (Target.Contains(TEXT("/")))
		{
			AActor* Actor = nullptr;
			UActorComponent* Component = nullptr;
			FString ComponentError;
			if (FindActorComponent(Target, Actor, Component, ComponentError))
			{
				Out.Object = Component;
				Out.Actor = Actor;
				Out.Label = FString::Printf(TEXT("%s/%s"), *Label(Actor), *Component->GetName());
				return true;
			}
			// Report the component miss when the actor part was found: it is the closer reading.
			int32 Slash;
			Target.FindLastChar(TEXT('/'), Slash);
			FString Ignored;
			OutError = FindActor(Target.Left(Slash), Ignored) ? ComponentError : ActorError;
			return false;
		}
		OutError = ActorError;
		return false;
	}

	FString NearMatches(const FString& Query, const TArray<FString>& Candidates, int32 Max)
	{
		if (Query.IsEmpty() || Candidates.Num() == 0) return FString();
		struct FScored
		{
			FString Name;
			int32 Score;
		};
		TArray<FScored> Scored;
		const FString Q = Query.ToLower();
		for (const FString& C : Candidates)
		{
			const FString L = C.ToLower();
			int32 Score = ZettaTargetsImpl::Distance(Q, L);
			if (L.Contains(Q) || Q.Contains(L)) Score = FMath::Min(Score, 1 + FMath::Abs(L.Len() - Q.Len()) / 4);
			// Only names that are plausibly what was meant.
			if (Score <= FMath::Max(2, Q.Len() / 2)) Scored.Add({ C, Score });
		}
		Scored.Sort([](const FScored& A, const FScored& B) { return A.Score < B.Score; });
		TArray<FString> Names;
		for (const FScored& S : Scored)
		{
			if (!Names.Contains(S.Name)) Names.Add(S.Name);
			if (Names.Num() >= Max) break;
		}
		TArray<FString> QuotedNames;
		for (const FString& N : Names) QuotedNames.Add(ZettaTool::Quoted(N));
		return FString::Join(QuotedNames, TEXT(", "));
	}
}
