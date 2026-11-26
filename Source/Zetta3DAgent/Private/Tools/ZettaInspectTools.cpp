// Copyright Zetta 3D. See LICENSE.md.
// Reading the level: level_outline, inspect_actor, find_actors, get_selection.

#include "AssetRegistry/AssetData.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/MovementComponent.h"
#include "IContentBrowserSingleton.h"
#include "ZettaLevelOutline.h"
#include "ZettaProperties.h"
#include "ZettaTargets.h"
#include "ZettaToolCommon.h"

namespace ZettaInspectImpl
{
	FString ShortClass(const UObject* O)
	{
		FString N = O->GetClass()->GetName();
		N.RemoveFromEnd(TEXT("_C"));
		N.RemoveFromEnd(TEXT("Component"));
		return N;
	}

	FString AssetName(const UObject* Asset) { return Asset ? Asset->GetName() : TEXT("no mesh"); }

	struct FOutlineFolder
	{
		FString Name;
		TMap<FString, TSharedPtr<FOutlineFolder>> Children;
		TArray<AActor*> Actors;
	};

	struct FWriter
	{
		const ZettaOutline::FOptions& Options;
		TArray<FString> Lines;
		TMap<AActor*, TArray<AActor*>> Attached;

		explicit FWriter(const ZettaOutline::FOptions& InOptions) : Options(InOptions) {}

		void Actor(AActor* A, int32 Level)
		{
			if (Level > Options.Depth) return;
			FString Line = FString::ChrN(Level * 2, TEXT(' ')) + ZettaTargets::Label(A);
			if (A->GetActorLabel() != A->GetName()) Line += TEXT(" #") + A->GetName();
			Line += TEXT(" ") + ZettaOutline::Brief(A, Options.bComponents);
			if (A->IsTemporarilyHiddenInEditor() || A->IsHidden()) Line += TEXT(" (hidden)");
			const TArray<AActor*>* Children = Attached.Find(A);
			if (Children && Level == Options.Depth) Line += FString::Printf(TEXT(" (+%d attached)"), Children->Num());
			Lines.Add(Line);
			if (Children)
			{
				for (AActor* Child : *Children) Actor(Child, Level + 1);
			}
		}

		void Folder(const FOutlineFolder& F, int32 Level)
		{
			if (Level > Options.Depth) return;
			if (!F.Name.IsEmpty())
			{
				int32 Count = F.Actors.Num();
				for (const auto& Pair : F.Children) Count += Pair.Value->Actors.Num();
				Lines.Add(FString::ChrN(Level * 2, TEXT(' ')) + F.Name + TEXT("/") + (Level == Options.Depth ? FString::Printf(TEXT(" (%d inside)"), Count) : FString()));
			}
			const int32 Inner = F.Name.IsEmpty() ? Level : Level + 1;
			TArray<FString> Names;
			F.Children.GetKeys(Names);
			Names.Sort();
			for (const FString& N : Names) Folder(*F.Children[N], Inner);
			for (AActor* A : F.Actors) Actor(A, Inner);
		}
	};

	bool ByLabel(const AActor& A, const AActor& B) { return A.GetActorLabel() < B.GetActorLabel(); }
}

namespace ZettaOutline
{
	FString Brief(const AActor* Actor, bool bComponents)
	{
		FString Class = Actor->GetClass()->GetName();
		Class.RemoveFromEnd(TEXT("_C"));
		TArray<FString> Parts = { Class };
		if (bComponents)
		{
			TInlineComponentArray<UActorComponent*> Components(Actor);
			int32 Shown = 0, Extra = 0;
			for (const UActorComponent* C : Components)
			{
				FString Part;
				if (const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(C))
				{
					Part = FString::Printf(TEXT("%s ×%d"), *ZettaInspectImpl::AssetName(ISM->GetStaticMesh()), ISM->GetInstanceCount());
				}
				else if (const UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(C))
				{
					Part = ZettaInspectImpl::AssetName(SM->GetStaticMesh());
				}
				else if (const USkeletalMeshComponent* SK = Cast<USkeletalMeshComponent>(C))
				{
					Part = ZettaInspectImpl::AssetName(SK->GetSkeletalMeshAsset());
				}
				else if (C->IsA<ULightComponent>() || C->IsA<UCameraComponent>() || C->IsA<UShapeComponent>() || C->IsA<UMovementComponent>())
				{
					Part = ZettaInspectImpl::ShortClass(C);
					// "PointLight [PointLight]" says nothing new.
					if (Class.StartsWith(Part)) Part.Reset();
				}
				if (Part.IsEmpty()) continue;
				if (Shown < 4)
				{
					Parts.Add(Part);
					++Shown;
				}
				else
				{
					++Extra;
				}
			}
			if (Extra > 0) Parts.Add(FString::Printf(TEXT("+%d"), Extra));
		}
		return TEXT("[") + FString::Join(Parts, TEXT(" · ")) + TEXT("]");
	}

	FString Build(const FOptions& Options, int32& OutTotal)
	{
		using namespace ZettaInspectImpl;
		UWorld* World = ZettaTool::EditorWorld();
		if (!World)
		{
			OutTotal = 0;
			return TEXT("No level is open.");
		}
		FWriter Writer(Options);
		TArray<AActor*> Actors = ZettaTargets::LevelActors();
		Actors.Sort(&ByLabel);
		TSet<AActor*> Listed(Actors);
		for (AActor* A : Actors)
		{
			AActor* Parent = A->GetAttachParentActor();
			if (Parent && Listed.Contains(Parent)) Writer.Attached.FindOrAdd(Parent).Add(A);
		}

		if (Options.Root)
		{
			Writer.Actor(Options.Root, 0);
		}
		else
		{
			FOutlineFolder Top;
			for (AActor* A : Actors)
			{
				AActor* Parent = A->GetAttachParentActor();
				if (Parent && Listed.Contains(Parent)) continue;
				FOutlineFolder* Into = &Top;
				const FName Path = A->GetFolderPath();
				if (!Path.IsNone())
				{
					TArray<FString> Parts;
					Path.ToString().ParseIntoArray(Parts, TEXT("/"));
					for (const FString& Part : Parts)
					{
						TSharedPtr<FOutlineFolder>& Child = Into->Children.FindOrAdd(Part);
						if (!Child.IsValid())
						{
							Child = MakeShared<FOutlineFolder>();
							Child->Name = Part;
						}
						Into = Child.Get();
					}
				}
				Into->Actors.Add(A);
			}
			Writer.Folder(Top, 1);
		}

		OutTotal = Writer.Lines.Num();
		FString Header = Options.Root
			? FString::Printf(TEXT("%s (in %s)"), *ZettaTargets::Describe(Options.Root), *World->GetMapName())
			: FString::Printf(TEXT("%s (level, %d actors)"), *World->GetMapName(), Actors.Num());
		FString Out = Header;
		const int32 Start = FMath::Clamp(Options.Offset, 0, Writer.Lines.Num());
		const int32 End = FMath::Min(Writer.Lines.Num(), Start + Options.Limit);
		int32 Written = Start;
		for (; Written < End; ++Written)
		{
			const FString& Line = Writer.Lines[Written];
			if (Out.Len() + Line.Len() + 80 > Options.MaxChars) break;
			Out += TEXT("\n") + Line;
		}
		if (Written < Writer.Lines.Num())
		{
			Out += FString::Printf(TEXT("\n… %d more line(s); continue with offset %d."), Writer.Lines.Num() - Written, Written);
		}
		return Out;
	}
}

namespace ZettaInspectImpl
{
	FZettaToolResult LevelOutline(const TSharedPtr<FJsonObject>& In)
	{
		ZettaOutline::FOptions Options;
		Options.Depth = ZettaTool::IntIn(In, TEXT("depth"), 3, 1, 8);
		Options.bComponents = ZettaTool::BoolOr(In, TEXT("components"), true);
		Options.Offset = ZettaTool::IntIn(In, TEXT("offset"), 0, 0, 1000000);
		Options.Limit = ZettaTool::IntIn(In, TEXT("limit"), 200, 1, 500);
		if (ZettaTool::Has(In, TEXT("root")))
		{
			FString Error;
			Options.Root = ZettaTargets::FindActor(ZettaTool::Str(In, TEXT("root")), Error);
			if (!Options.Root) return ZettaTool::Fail(Error);
		}
		int32 Total = 0;
		FString Text = ZettaOutline::Build(Options, Total);
		if (ZettaTool::IsPlaying()) Text += TEXT("\n(Play In Editor is running; this is the editor level, not the game world.)");
		return ZettaTool::Ok(Text);
	}

	FString ComponentHeader(const UActorComponent* C, const AActor* Owner)
	{
		FString Line = FString::Printf(TEXT("- %s (%s"), *C->GetName(), *C->GetClass()->GetName());
		if (Owner->GetRootComponent() == C) Line += TEXT(", root");
		switch (C->CreationMethod)
		{
		case EComponentCreationMethod::Native: Line += TEXT(", from C++"); break;
		case EComponentCreationMethod::SimpleConstructionScript: Line += TEXT(", from the Blueprint"); break;
		case EComponentCreationMethod::Instance: Line += TEXT(", added in the level"); break;
		default: Line += TEXT(", made by the construction script"); break;
		}
		Line += TEXT(")");
		if (const USceneComponent* S = Cast<USceneComponent>(C))
		{
			if (S->GetAttachParent() && Owner->GetRootComponent() != S) Line += FString::Printf(TEXT(" attached to %s"), *S->GetAttachParent()->GetName());
			if (!S->GetRelativeLocation().IsNearlyZero() || !S->GetRelativeRotation().IsNearlyZero())
			{
				Line += FString::Printf(TEXT(" at %s rot %s"), *ZettaTool::Vec(S->GetRelativeLocation()), *ZettaTool::Rot(S->GetRelativeRotation()));
			}
			if (!S->GetRelativeScale3D().Equals(FVector::OneVector)) Line += FString::Printf(TEXT(" scale %s"), *ZettaTool::Vec(S->GetRelativeScale3D()));
		}
		return Line;
	}

	FZettaToolResult InspectActor(const TSharedPtr<FJsonObject>& In)
	{
		FString Error;
		AActor* Actor = ZettaTargets::FindActor(ZettaTool::Str(In, TEXT("target")), Error);
		if (!Actor) return ZettaTool::Fail(Error);
		const bool bAll = ZettaTool::BoolOr(In, TEXT("all"), false);
		const TArray<FString> Only = ZettaTool::Strings(In, TEXT("components"));

		TArray<FString> Out;
		Out.Add(FString::Printf(TEXT("%s — %s"), *ZettaTargets::Describe(Actor), *Actor->GetClass()->GetPathName()));
		TArray<FString> Facts;
		if (!Actor->GetFolderPath().IsNone()) Facts.Add(TEXT("folder ") + Actor->GetFolderPath().ToString());
		if (const AActor* Parent = Actor->GetAttachParentActor()) Facts.Add(TEXT("attached to ") + ZettaTargets::Describe(Parent));
		if (const USceneComponent* Root = Actor->GetRootComponent()) Facts.Add(TEXT("mobility ") + StaticEnum<EComponentMobility::Type>()->GetNameStringByValue(Root->Mobility));
		if (Actor->Tags.Num() > 0)
		{
			TArray<FString> Tags;
			for (const FName& T : Actor->Tags) Tags.Add(T.ToString());
			Facts.Add(TEXT("tags ") + FString::Join(Tags, TEXT(", ")));
		}
		if (Actor->IsHidden()) Facts.Add(TEXT("hidden in game"));
		if (Actor->IsTemporarilyHiddenInEditor()) Facts.Add(TEXT("hidden in the editor"));
		if (Facts.Num() > 0) Out.Add(FString::Join(Facts, TEXT(" · ")));
		if (Actor->GetRootComponent())
		{
			Out.Add(FString::Printf(TEXT("Location %s · Rotation %s · Scale %s (world, cm and degrees)"), *ZettaTool::Vec(Actor->GetActorLocation()),
				*ZettaTool::Rot(Actor->GetActorRotation()), *ZettaTool::Vec(Actor->GetActorScale3D())));
			FVector Origin, Extent;
			Actor->GetActorBounds(true, Origin, Extent);
			if (!Extent.IsNearlyZero()) Out.Add(FString::Printf(TEXT("Bounds: size %s, centre %s"), *ZettaTool::Vec(Extent * 2), *ZettaTool::Vec(Origin)));
		}
		TArray<AActor*> Children;
		Actor->GetAttachedActors(Children);
		if (Children.Num() > 0)
		{
			TArray<FString> Names;
			for (const AActor* C : Children) Names.Add(ZettaTargets::Label(C));
			Out.Add(TEXT("Attached: ") + ZettaTool::JoinQuoted(Names, 20));
		}

		if (Only.Num() == 0)
		{
			const TArray<FString> Lines = ZettaProps::Lines(Actor, bAll, 120);
			Out.Add(bAll ? TEXT("Actor properties:") : (Lines.Num() > 0 ? TEXT("Actor properties changed from the class defaults:") : TEXT("Actor properties: all at the class defaults.")));
			for (const FString& L : Lines) Out.Add(TEXT("  ") + L);
		}

		TInlineComponentArray<UActorComponent*> Components(Actor);
		Out.Add(FString::Printf(TEXT("Components (%d):"), Components.Num()));
		TArray<FString> Missing = Only;
		for (UActorComponent* C : Components)
		{
			if (!C) continue;
			if (Only.Num() > 0)
			{
				const bool bWanted = Only.ContainsByPredicate([C](const FString& N) { return C->GetName().Equals(N, ESearchCase::IgnoreCase) || C->GetClass()->GetName().Equals(N, ESearchCase::IgnoreCase); });
				if (!bWanted) continue;
				Missing.RemoveAll([C](const FString& N) { return C->GetName().Equals(N, ESearchCase::IgnoreCase) || C->GetClass()->GetName().Equals(N, ESearchCase::IgnoreCase); });
			}
			Out.Add(ComponentHeader(C, Actor));
			for (const FString& L : ZettaProps::Lines(C, bAll, bAll ? 200 : 60)) Out.Add(TEXT("    ") + L);
		}
		if (Missing.Num() > 0) Out.Add(FString::Printf(TEXT("Not found on this actor: %s."), *ZettaTool::JoinQuoted(Missing)));
		if (!bAll) Out.Add(TEXT("(Only properties changed from the defaults are listed; all: true lists every editable one.)"));
		return ZettaTool::Ok(FString::Join(Out, TEXT("\n")));
	}

	FZettaToolResult FindActors(const TSharedPtr<FJsonObject>& In)
	{
		const FString Name = ZettaTool::Str(In, TEXT("name")).TrimStartAndEnd();
		const FString Class = ZettaTool::Str(In, TEXT("class")).TrimStartAndEnd();
		const FString Tag = ZettaTool::Str(In, TEXT("tag")).TrimStartAndEnd();
		const FString Folder = ZettaTool::Str(In, TEXT("folder")).TrimStartAndEnd();
		const FString Component = ZettaTool::Str(In, TEXT("component")).TrimStartAndEnd();
		const int32 Limit = ZettaTool::IntIn(In, TEXT("limit"), 50, 1, 200);

		// A class name matches the actor's class or any parent ("Light" finds every light).
		auto ClassMatches = [](const UClass* C, const FString& Wanted)
		{
			for (; C; C = C->GetSuperClass())
			{
				FString N = C->GetName();
				N.RemoveFromEnd(TEXT("_C"));
				if (N.Equals(Wanted, ESearchCase::IgnoreCase) || C->GetPathName().Equals(Wanted, ESearchCase::IgnoreCase) || (Wanted.StartsWith(TEXT("/")) && C->GetPathName().StartsWith(ZettaTool::PackagePath(Wanted)))) return true;
			}
			return false;
		};

		TArray<FString> Lines;
		int32 Matches = 0;
		TArray<AActor*> Actors = ZettaTargets::LevelActors();
		Actors.Sort(&ByLabel);
		for (AActor* A : Actors)
		{
			if (!Name.IsEmpty() && !A->GetActorLabel().Contains(Name) && !A->GetName().Contains(Name)) continue;
			if (!Class.IsEmpty() && !ClassMatches(A->GetClass(), Class) && !A->GetClass()->GetName().Contains(Class)) continue;
			if (!Tag.IsEmpty() && !A->Tags.ContainsByPredicate([&](const FName& T) { return T.ToString().Equals(Tag, ESearchCase::IgnoreCase); })) continue;
			if (!Folder.IsEmpty() && !A->GetFolderPath().ToString().StartsWith(Folder.TrimChar(TEXT('/')), ESearchCase::IgnoreCase)) continue;
			if (!Component.IsEmpty())
			{
				TInlineComponentArray<UActorComponent*> Components(A);
				const bool bHas = Components.ContainsByPredicate([&](const UActorComponent* C)
				{
					return C && (ClassMatches(C->GetClass(), Component) || ClassMatches(C->GetClass(), Component + TEXT("Component")) || C->GetName().Equals(Component, ESearchCase::IgnoreCase));
				});
				if (!bHas) continue;
			}
			++Matches;
			if (Lines.Num() >= Limit) continue;
			FString Line = FString::Printf(TEXT("%s #%s [%s]"), *ZettaTargets::Label(A), *A->GetName(), *A->GetClass()->GetName());
			if (A->GetRootComponent()) Line += TEXT(" at ") + ZettaTool::Vec(A->GetActorLocation());
			if (!A->GetFolderPath().IsNone()) Line += TEXT(" in ") + A->GetFolderPath().ToString() + TEXT("/");
			Lines.Add(Line);
		}
		if (Matches == 0) return ZettaTool::Ok(TEXT("No actors match. level_outline shows everything in the level."));
		FString Text = FString::Printf(TEXT("%d actor(s) match%s:\n%s"), Matches, Matches > Lines.Num() ? *FString::Printf(TEXT(" (first %d)"), Lines.Num()) : TEXT(""), *FString::Join(Lines, TEXT("\n")));
		return ZettaTool::Ok(Text);
	}

	FZettaToolResult GetSelection(const TSharedPtr<FJsonObject>& In)
	{
		TArray<FString> Out;
		if (GEditor)
		{
			TArray<FString> Actors;
			for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
			{
				if (const AActor* A = Cast<AActor>(*It)) Actors.Add(FString::Printf(TEXT("- %s #%s [%s]"), *ZettaTargets::Label(A), *A->GetName(), *A->GetClass()->GetName()));
			}
			Out.Add(Actors.Num() > 0 ? FString::Printf(TEXT("Selected actors (%d):\n%s"), Actors.Num(), *FString::Join(Actors, TEXT("\n"))) : TEXT("No actors selected."));

			TArray<FString> Components;
			for (FSelectionIterator It(*GEditor->GetSelectedComponents()); It; ++It)
			{
				if (const UActorComponent* C = Cast<UActorComponent>(*It)) Components.Add(TEXT("- ") + ZettaTargets::DescribeComponent(C));
			}
			if (Components.Num() > 0) Out.Add(FString::Printf(TEXT("Selected components (%d):\n%s"), Components.Num(), *FString::Join(Components, TEXT("\n"))));
		}

		IContentBrowserSingleton& Browser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();
		TArray<FAssetData> Assets;
		Browser.GetSelectedAssets(Assets);
		if (Assets.Num() > 0)
		{
			TArray<FString> Lines;
			for (const FAssetData& A : Assets) Lines.Add(FString::Printf(TEXT("- %s (%s)"), *A.PackageName.ToString(), *A.AssetClassPath.GetAssetName().ToString()));
			Out.Add(FString::Printf(TEXT("Content Browser selection (%d):\n%s"), Assets.Num(), *FString::Join(Lines, TEXT("\n"))));
		}
		else
		{
			Out.Add(TEXT("No assets selected in the Content Browser."));
		}
		TArray<FString> Folders;
		Browser.GetSelectedFolders(Folders);
		if (Folders.Num() > 0) Out.Add(TEXT("Selected folders: ") + FString::Join(Folders, TEXT(", ")));
		const FString Current = Browser.GetCurrentPath().GetInternalPathString();
		if (!Current.IsEmpty()) Out.Add(TEXT("Content Browser folder: ") + Current);
		return ZettaTool::Ok(FString::Join(Out, TEXT("\n")));
	}

	FZettaToolPreview Named(const FString& Title) { return ZettaTool::Preview(Title); }
}

void ZettaToolAreas::RegisterInspectTools(FZettaAgentTools& Tools)
{
	using namespace ZettaInspectImpl;
	Tools.Register(TEXT("level_outline"), &LevelOutline, [](const TSharedPtr<FJsonObject>&) { return Named(TEXT("Read the level outline")); });
	Tools.Register(TEXT("inspect_actor"), &InspectActor, [](const TSharedPtr<FJsonObject>& In) { return Named(FString::Printf(TEXT("Inspect %s"), *ZettaTool::Str(In, TEXT("target")))); });
	Tools.Register(TEXT("find_actors"), &FindActors, [](const TSharedPtr<FJsonObject>&) { return Named(TEXT("Find actors")); });
	Tools.Register(TEXT("get_selection"), &GetSelection, [](const TSharedPtr<FJsonObject>&) { return Named(TEXT("Read the selection")); });
}
