// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaAgentMentions.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ContentBrowserModule.h"
#include "DragAndDrop/ActorDragDropOp.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "DragAndDrop/CompositeDragDropOp.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "IContentBrowserSingleton.h"
#include "Input/DragAndDrop.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateIconFinder.h"

namespace
{
	/** 0 best (name starts with it) … 2 (path contains it), −1 no match. An empty query matches everything. */
	int32 MentionRank(const FString& Name, const FString& Path, const FString& Query)
	{
		if (Query.IsEmpty()) return 1;
		if (Name.StartsWith(Query, ESearchCase::IgnoreCase)) return 0;
		if (Name.Contains(Query, ESearchCase::IgnoreCase)) return 1;
		return Path.Contains(Query, ESearchCase::IgnoreCase) ? 2 : -1;
	}

	/** "/Script/CoreUObject.Class'/Script/Engine.Character'" → "Character". */
	FString MentionShortClassName(const FString& Value)
	{
		FString Clean = Value;
		Clean.TrimQuotesInline();
		Clean.RemoveFromEnd(TEXT("'"));
		int32 Dot = INDEX_NONE;
		if (Clean.FindLastChar(TEXT('.'), Dot)) Clean = Clean.Mid(Dot + 1);
		Clean.RemoveFromEnd(TEXT("_C"));
		return Clean;
	}

	FString MentionLevelOf(const AActor* Actor)
	{
		const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
		return World ? World->GetOutermost()->GetName() : FString();
	}
}

// ─── FZettaMention ────────────────────────────────────────────────────────────

FString FZettaMention::Name() const
{
	FString Out = Path;
	int32 Slash = INDEX_NONE;
	if (Out.FindLastChar(TEXT('/'), Slash) && Slash < Out.Len() - 1) Out = Out.Mid(Slash + 1);
	return Out;
}

FString FZettaMention::Describe() const
{
	if (IsActor())
	{
		FString Where = Class;
		if (!Level.IsEmpty()) Where += TEXT(", level ") + Level;
		return FString::Printf(TEXT("actor \"%s\" (%s) — target it as \"%s\" or \"#%s\""), *Path, *Where, *Path, *ObjectName);
	}
	if (IsAsset())
	{
		FString What = Class.IsEmpty() ? TEXT("Asset") : Class;
		if (!ParentClass.IsEmpty()) What += TEXT(", parent ") + ParentClass;
		FString Line = FString::Printf(TEXT("asset \"%s\" (%s)"), *Path, *What);
		if (!Note.IsEmpty()) Line += TEXT(" — ") + Note;
		return Line;
	}
	FString Line = FString::Printf(TEXT("file \"%s\""), *Path);
	if (!Note.IsEmpty()) Line += TEXT(" — ") + Note;
	return Line;
}

const FSlateBrush* FZettaMention::Icon() const
{
	if (IsActor())
	{
		if (const AActor* Actor = FindActor()) return FSlateIconFinder::FindIconBrushForClass(Actor->GetClass());
		return FAppStyle::GetBrush("ClassIcon.Actor");
	}
	if (IsAsset())
	{
		if (Class == TEXT("Folder")) return FAppStyle::GetBrush("ContentBrowser.AssetTreeFolderClosed");
		const UClass* Found = FindFirstObject<UClass>(*Class, EFindFirstObjectOptions::NativeFirst);
		if (Found) return FSlateIconFinder::FindIconBrushForClass(Found);
		return FAppStyle::GetBrush("ClassIcon.Object");
	}
	return FAppStyle::GetBrush("Icons.Document");
}

AActor* FZettaMention::FindActor() const
{
	if (!IsActor() || ObjectName.IsEmpty()) return nullptr;
	UWorld* World = ZettaMentions::EditorWorld();
	if (!World) return nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetName() == ObjectName) return *It;
	}
	return nullptr;
}

TOptional<FZettaMention> FZettaMention::FromActor(const AActor* Actor)
{
	if (!IsValid(Actor) || !Actor->IsListedInSceneOutliner() || Actor->IsA<AWorldSettings>()) return {};
	FZettaMention M;
	M.Kind = TEXT("actor");
	const FName Folder = Actor->GetFolderPath();
	const FString Label = Actor->GetActorLabel();
	M.Path = Folder.IsNone() ? Label : Folder.ToString() + TEXT("/") + Label;
	M.ObjectName = Actor->GetName();
	M.Class = Actor->GetClass()->GetName();
	M.Class.RemoveFromEnd(TEXT("_C"));
	M.Level = MentionLevelOf(Actor);
	return M;
}

TOptional<FZettaMention> FZettaMention::FromAsset(const FAssetData& Asset)
{
	if (!Asset.IsValid()) return {};
	FZettaMention M;
	M.Kind = TEXT("asset");
	M.Path = Asset.PackageName.ToString();
	M.Class = Asset.AssetClassPath.GetAssetName().ToString();
	FString Parent;
	if (Asset.GetTagValue(FName(TEXT("ParentClass")), Parent) && !Parent.IsEmpty()) M.ParentClass = MentionShortClassName(Parent);
	return M;
}

FZettaMention FZettaMention::FromFolder(const FString& ContentPath)
{
	FZettaMention M;
	M.Kind = TEXT("asset");
	M.Path = ContentPath;
	M.Path.RemoveFromStart(TEXT("/All"));
	M.Class = TEXT("Folder");
	return M;
}

FZettaMention FZettaMention::FromFile(const FString& ProjectRelativePath, const FString& Note)
{
	FZettaMention M;
	M.Kind = TEXT("file");
	M.Path = ProjectRelativePath;
	M.Note = Note;
	return M;
}

// ─── ZettaMentions ────────────────────────────────────────────────────────────

namespace ZettaMentions
{
	FString Block(const TArray<FZettaMention>& Mentions)
	{
		if (Mentions.Num() == 0) return FString();
		FString Out = TEXT("\n\n<mentions>");
		for (const FZettaMention& M : Mentions) Out += TEXT("\n- ") + M.Describe();
		return Out + TEXT("\n</mentions>");
	}

	UWorld* EditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	TArray<FZettaMention> SearchLevel(const FString& InQuery, int32 Limit)
	{
		const FString Query = InQuery.TrimStartAndEnd();
		TArray<TPair<int32, FZettaMention>> Found;
		if (UWorld* World = EditorWorld())
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				TOptional<FZettaMention> M = FZettaMention::FromActor(*It);
				if (!M.IsSet()) continue;
				int32 R = MentionRank(It->GetActorLabel(), M->Path, Query);
				if (R < 0 && MentionRank(M->Class, M->Class, Query) >= 0) R = 3;
				if (R >= 0) Found.Emplace(R, M.GetValue());
				if (Found.Num() > 5000) break;
			}
		}
		Found.StableSort([](const TPair<int32, FZettaMention>& A, const TPair<int32, FZettaMention>& B) { return A.Key < B.Key; });
		TArray<FZettaMention> Out;
		for (int32 i = 0; i < Found.Num() && Out.Num() < Limit; ++i) Out.Add(Found[i].Value);
		return Out;
	}

	TArray<FZettaMention> SearchProject(const FString& InQuery, int32 Limit)
	{
		const FString Query = InQuery.TrimStartAndEnd();
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Assets;
		Registry.GetAssetsByPath(FName(TEXT("/Game")), Assets, true);

		TArray<TPair<int32, const FAssetData*>> Found;
		for (const FAssetData& Asset : Assets)
		{
			const int32 R = MentionRank(Asset.AssetName.ToString(), Asset.PackageName.ToString(), Query);
			if (R >= 0) Found.Emplace(R, &Asset);
		}
		Found.StableSort([](const TPair<int32, const FAssetData*>& A, const TPair<int32, const FAssetData*>& B)
		{
			if (A.Key != B.Key) return A.Key < B.Key;
			return A.Value->PackageName.LexicalLess(B.Value->PackageName);
		});
		TArray<FZettaMention> Out;
		for (int32 i = 0; i < Found.Num() && Out.Num() < Limit; ++i)
		{
			if (TOptional<FZettaMention> M = FZettaMention::FromAsset(*Found[i].Value)) Out.Add(M.GetValue());
		}
		return Out;
	}

	TArray<FZettaMention> SelectedActors()
	{
		TArray<FZettaMention> Out;
		if (!GEditor) return Out;
		for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
		{
			if (TOptional<FZettaMention> M = FZettaMention::FromActor(Cast<AActor>(*It))) Out.Add(M.GetValue());
		}
		return Out;
	}

	TArray<FZettaMention> SelectedAssets()
	{
		TArray<FZettaMention> Out;
		FContentBrowserModule& Browser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		TArray<FAssetData> Assets;
		Browser.Get().GetSelectedAssets(Assets);
		for (const FAssetData& Asset : Assets)
		{
			if (TOptional<FZettaMention> M = FZettaMention::FromAsset(Asset)) Out.Add(M.GetValue());
		}
		return Out;
	}

	TArray<FZettaMention> FromSelection()
	{
		TArray<FZettaMention> Out = SelectedActors();
		Out.Append(SelectedAssets());
		return Out;
	}

	static TSharedPtr<FActorDragDropOp> ActorOp(const FDragDropEvent& Event)
	{
		if (TSharedPtr<FActorDragDropOp> Direct = Event.GetOperationAs<FActorDragDropOp>()) return Direct;
		// The Outliner wraps its actors in a composite operation.
		if (TSharedPtr<FCompositeDragDropOp> Composite = Event.GetOperationAs<FCompositeDragDropOp>()) return Composite->GetSubOp<FActorDragDropOp>();
		return nullptr;
	}

	bool DragHasObjects(const FDragDropEvent& Event)
	{
		return ActorOp(Event).IsValid() || Event.GetOperationAs<FAssetDragDropOp>().IsValid();
	}

	TArray<FZettaMention> FromDrag(const FDragDropEvent& Event)
	{
		TArray<FZettaMention> Out;
		if (const TSharedPtr<FActorDragDropOp> Actors = ActorOp(Event))
		{
			for (const TWeakObjectPtr<AActor>& Actor : Actors->Actors)
			{
				if (TOptional<FZettaMention> M = FZettaMention::FromActor(Actor.Get())) Out.Add(M.GetValue());
			}
		}
		if (const TSharedPtr<FAssetDragDropOp> Assets = Event.GetOperationAs<FAssetDragDropOp>())
		{
			for (const FAssetData& Asset : Assets->GetAssets())
			{
				if (TOptional<FZettaMention> M = FZettaMention::FromAsset(Asset)) Out.Add(M.GetValue());
			}
			for (const FString& Folder : Assets->GetAssetPaths()) Out.Add(FZettaMention::FromFolder(Folder));
		}
		return Out;
	}
}
