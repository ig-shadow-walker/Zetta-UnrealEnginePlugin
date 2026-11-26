// Copyright Zetta 3D. See LICENSE.md.
// What the agent changed in each message, and how to take it back (the changes card under the reply).

#include "Agent/ZettaAgentChangeLog.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Editor/Transactor.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"
#include "ZettaAgentTools.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace
{
	FString ChangesProjectRoot()
	{
		FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		FPaths::NormalizeDirectoryName(Root);
		return Root + TEXT("/");
	}

	/** An absolute path for a project-relative or absolute one. */
	FString ChangesFullPath(const FString& Path)
	{
		FString Full = FPaths::IsRelative(Path) ? FPaths::ConvertRelativePathToFull(ChangesProjectRoot(), Path) : FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeFilename(Full);
		FPaths::CollapseRelativeDirectories(Full);
		return Full;
	}

	/** "Source/Game/Ship.cpp" for a file in the project; the full path for anything else. */
	FString ChangesShown(const FString& Full)
	{
		const FString Root = ChangesProjectRoot();
		return Full.StartsWith(Root) ? Full.Mid(Root.Len()) : Full;
	}

	FString ChangesPackageOf(const FString& ContentPath)
	{
		FString Path = ContentPath.TrimStartAndEnd();
		// "/Script/Engine.StaticMesh'/Game/X.X'" → "/Game/X.X"
		int32 Quote = INDEX_NONE;
		if (Path.FindChar(TEXT('\''), Quote))
		{
			Path = Path.Mid(Quote + 1);
			Path.RemoveFromEnd(TEXT("'"));
		}
		return FPackageName::ObjectPathToPackageName(Path);
	}

	/** The package file of a content path on disk, when it exists. */
	FString ChangesPackageFile(const FString& Package)
	{
		FString Filename;
		if (!FPackageName::DoesPackageExist(Package, &Filename)) return FString();
		return FPaths::ConvertRelativePathToFull(Filename);
	}

	IAssetRegistry& ChangesRegistry()
	{
		return FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	}

	UObject* ChangesMainAsset(const FString& Package)
	{
		TArray<FAssetData> Assets;
		ChangesRegistry().GetAssetsByPackageName(FName(*Package), Assets, true);
		return Assets.Num() > 0 ? Assets[0].GetAsset() : nullptr;
	}
}

FZettaAgentChangeLog& FZettaAgentChangeLog::Get()
{
	static FZettaAgentChangeLog Instance;
	return Instance;
}

FString FZettaAgentChangeLog::BackupFolder() const
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Zetta3D/Agent/Backups"));
}

FString FZettaAgentChangeLog::BeginTurn()
{
	static int32 Counter = 0;
	Turn = FString::Printf(TEXT("%s-%d"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")), ++Counter);
	return Turn;
}

bool FZettaAgentChangeLog::Has(const FString& InTurn) const
{
	const TArray<FZettaChangeRow>* Set = Sets.Find(InTurn);
	return Set && Set->Num() > 0;
}

TArray<FZettaChangeRow>* FZettaAgentChangeLog::Rows(const FString& InTurn)
{
	return Sets.Find(InTurn);
}

FZettaChangeRow* FZettaAgentChangeLog::Find(const FString& Kind, const FString& Path)
{
	TArray<FZettaChangeRow>* Set = Sets.Find(Turn);
	if (!Set) return nullptr;
	return Set->FindByPredicate([&](const FZettaChangeRow& Row) { return Row.Kind == Kind && Row.Path == Path; });
}

FString FZettaAgentChangeLog::TakeCopy(const FString& DiskPath)
{
	const FString Target = BackupFolder() / Turn / FString::Printf(TEXT("%03d-%s"), ++Copies, *FPaths::GetCleanFilename(DiskPath));
	return IFileManager::Get().Copy(*Target, *DiskPath, true, true) == COPY_OK ? Target : FString();
}

// ─── Recording ────────────────────────────────────────────────────────────────

void FZettaAgentChangeLog::WillWriteFile(const FString& Path)
{
	if (!bRecording || Turn.IsEmpty() || Path.IsEmpty()) return;
	const FString Full = ChangesFullPath(Path);
	const FString Display = ChangesShown(Full);
	if (Find(TEXT("file"), Display)) return; // the first copy of the message is the one to go back to
	FZettaChangeRow Row;
	Row.Kind = TEXT("file");
	Row.Path = Display;
	Row.DiskPath = Full;
	const bool bExists = IFileManager::Get().FileExists(*Full);
	Row.Change = bExists ? TEXT("edited") : TEXT("created");
	if (bExists) Row.Backup = TakeCopy(Full);
	Sets.FindOrAdd(Turn).Add(MoveTemp(Row));
	OnChanged.Broadcast();
}

void FZettaAgentChangeLog::WillChangeAsset(const FString& ContentPath, const FString& What)
{
	if (!bRecording || Turn.IsEmpty() || ContentPath.IsEmpty()) return;
	const FString Package = ChangesPackageOf(ContentPath);
	const FString Verb = What.TrimStartAndEnd().ToLower();
	const bool bMove = Verb.StartsWith(TEXT("mov")) || Verb.StartsWith(TEXT("renam"));
	const bool bDelete = Verb.StartsWith(TEXT("delet")) || Verb.StartsWith(TEXT("remov"));

	if (FZettaChangeRow* Existing = Find(TEXT("asset"), Package))
	{
		// Created this message and now deleted again: nothing is left to undo but the delete itself.
		if (bDelete && Existing->Change == TEXT("created")) Existing->Change = TEXT("deleted");
		return;
	}

	FZettaChangeRow Row;
	Row.Kind = TEXT("asset");
	Row.Path = Package;
	Row.DiskPath = ChangesPackageFile(Package);
	const bool bExists = !Row.DiskPath.IsEmpty();
	if (bMove)
	{
		Row.Change = TEXT("moved");
		// "moved to /Game/Props/SM_Chair" — where it went, so Undo can bring it back.
		int32 Slash = Verb.Find(TEXT(" /"));
		if (Slash != INDEX_NONE) Row.MovedTo = ChangesPackageOf(What.TrimStartAndEnd().Mid(Slash + 1));
	}
	else if (bDelete)
	{
		Row.Change = TEXT("deleted");
	}
	else
	{
		Row.Change = bExists ? TEXT("edited") : TEXT("created");
	}
	if (bExists) Row.Backup = TakeCopy(Row.DiskPath);
	Sets.FindOrAdd(Turn).Add(MoveTemp(Row));
	OnChanged.Broadcast();
}

void FZettaAgentChangeLog::CreatedAsset(const FString& ContentPath)
{
	if (!bRecording || Turn.IsEmpty() || ContentPath.IsEmpty()) return;
	const FString Package = ChangesPackageOf(ContentPath);
	if (FZettaChangeRow* Existing = Find(TEXT("asset"), Package))
	{
		// Deleted, then made again in the same message: it is an edit of what was there.
		if (Existing->Change == TEXT("deleted")) Existing->Change = Existing->Backup.IsEmpty() ? TEXT("created") : TEXT("edited");
		return;
	}
	FZettaChangeRow Row;
	Row.Kind = TEXT("asset");
	Row.Path = Package;
	Row.Change = TEXT("created");
	Sets.FindOrAdd(Turn).Add(MoveTemp(Row));
	OnChanged.Broadcast();
}

void FZettaAgentChangeLog::LevelTransaction(const FString& Title)
{
	if (!bRecording || Turn.IsEmpty() || !GEditor || !GEditor->Trans) return;
	const FTransactionContext Context = GEditor->Trans->GetUndoContext(false);
	if (!Context.TransactionId.IsValid()) return;

	FZettaChangeRow* Row = Find(TEXT("level"), TEXT("level"));
	if (!Row)
	{
		FZettaChangeRow New;
		New.Kind = TEXT("level");
		New.Change = TEXT("level");
		New.Path = TEXT("level");
		Row = &Sets.FindOrAdd(Turn).Add_GetRef(MoveTemp(New));
	}
	if (Row->Transactions.Contains(Context.TransactionId)) return;
	Row->Transactions.Add(Context.TransactionId);
	Row->TransactionTitles.Add(Title.IsEmpty() ? Context.Title.ToString() : Title);
	Row->bUndone = false;
	OnChanged.Broadcast();
}

// ─── Keep and undo ────────────────────────────────────────────────────────────

void FZettaAgentChangeLog::KeepRow(const FString& InTurn, int32 Index)
{
	TArray<FZettaChangeRow>* Set = Sets.Find(InTurn);
	if (!Set || !Set->IsValidIndex(Index)) return;
	(*Set)[Index].bKept = true;
	OnChanged.Broadcast();
}

void FZettaAgentChangeLog::KeepAll(const FString& InTurn)
{
	if (TArray<FZettaChangeRow>* Set = Sets.Find(InTurn))
	{
		for (FZettaChangeRow& Row : *Set)
		{
			if (!Row.IsSettled()) Row.bKept = true;
		}
	}
	OnChanged.Broadcast();
}

FString FZettaAgentChangeLog::UndoRow(const FString& InTurn, int32 Index)
{
	TArray<FZettaChangeRow>* Set = Sets.Find(InTurn);
	if (!Set || !Set->IsValidIndex(Index)) return FString();
	FZettaChangeRow& Row = (*Set)[Index];
	if (Row.bUndone) return FString();
	const FString Problem = Row.Kind == TEXT("level") ? UndoLevel(Row) : Row.Kind == TEXT("file") ? UndoFile(Row) : UndoAsset(Row);
	OnChanged.Broadcast();
	return Problem;
}

FString FZettaAgentChangeLog::UndoAll(const FString& InTurn, bool bIncludeKept)
{
	TArray<FZettaChangeRow>* Set = Sets.Find(InTurn);
	if (!Set) return FString();
	FString First;
	// The level first: its steps are on the editor's history, which only unwinds from the top.
	for (FZettaChangeRow& Row : *Set)
	{
		if (Row.Kind != TEXT("level") || Row.bUndone || (Row.bKept && !bIncludeKept)) continue;
		const FString Problem = UndoLevel(Row);
		if (First.IsEmpty()) First = Problem;
	}
	for (int32 i = Set->Num() - 1; i >= 0; --i)
	{
		FZettaChangeRow& Row = (*Set)[i];
		if (Row.Kind == TEXT("level") || Row.bUndone || (Row.bKept && !bIncludeKept)) continue;
		const FString Problem = Row.Kind == TEXT("file") ? UndoFile(Row) : UndoAsset(Row);
		if (First.IsEmpty()) First = Problem;
	}
	OnChanged.Broadcast();
	return First;
}

FString FZettaAgentChangeLog::UndoLevel(FZettaChangeRow& Row)
{
	if (!GEditor || !GEditor->Trans) return LOCTEXT("AgentUI.NoUndo", "The editor's undo history is not available.").ToString();
	if (GEditor->IsTransactionActive()) return LOCTEXT("AgentUI.TransactionActive", "Finish what you are doing in the editor first, then try again.").ToString();
	UTransactor* Trans = GEditor->Trans;

	// Steps already undone (by the agent's own undo tool, or Edit › Undo) are skipped, not undone twice.
	auto IsUndone = [Trans](const FGuid& Id)
	{
		const int32 Length = Trans->GetQueueLength();
		for (int32 i = Length - Trans->GetUndoCount(); i < Length; ++i)
		{
			const FTransaction* T = Trans->GetTransaction(i);
			if (T && T->GetContext().TransactionId == Id) return true;
		}
		return false;
	};

	for (int32 i = Row.Transactions.Num() - 1; i >= 0; --i)
	{
		const FGuid& Id = Row.Transactions[i];
		if (IsUndone(Id)) continue;
		if (Trans->GetUndoContext(false).TransactionId != Id)
		{
			return LOCTEXT("AgentUI.LevelChangedSince",
				"The level has changed since the agent's steps. Undo your own changes first with Edit › Undo, then try again.").ToString();
		}
		GEditor->UndoTransaction();
		++Row.StepsUndone;
	}
	Row.bUndone = true;
	return FString();
}

FString FZettaAgentChangeLog::UndoFile(FZettaChangeRow& Row)
{
	IFileManager& Files = IFileManager::Get();
	if (Row.Change == TEXT("created"))
	{
		// Moved aside, not deleted: the agent's file stays with the message's copies in case it is wanted after all.
		if (Files.FileExists(*Row.DiskPath))
		{
			const FString Aside = BackupFolder() / Turn / TEXT("undone") / FPaths::GetCleanFilename(Row.DiskPath);
			if (!Files.Move(*Aside, *Row.DiskPath, true, true))
			{
				return FText::Format(LOCTEXT("AgentUI.FileNotRemoved", "Could not remove {0}. Is it open somewhere?"), FText::FromString(Row.Path)).ToString();
			}
		}
		Row.bUndone = true;
		return FString();
	}
	if (Row.Backup.IsEmpty() || !Files.FileExists(*Row.Backup))
	{
		return FText::Format(LOCTEXT("AgentUI.NoCopy", "The copy of {0} from before this message is gone."), FText::FromString(Row.Path)).ToString();
	}
	if (Files.Copy(*Row.DiskPath, *Row.Backup, true, true) != COPY_OK)
	{
		return FText::Format(LOCTEXT("AgentUI.FileNotRestored", "Could not put {0} back. Is it open somewhere?"), FText::FromString(Row.Path)).ToString();
	}
	Row.bUndone = true;
	return FString();
}

FString FZettaAgentChangeLog::UndoAsset(FZettaChangeRow& Row)
{
	if (Row.Change == TEXT("created"))
	{
		TArray<FAssetData> Assets;
		ChangesRegistry().GetAssetsByPackageName(FName(*Row.Path), Assets, true);
		TArray<UObject*> Objects;
		for (const FAssetData& Asset : Assets)
		{
			if (UObject* Object = Asset.GetAsset()) Objects.Add(Object);
		}
		if (Objects.Num() > 0 && ObjectTools::ForceDeleteObjects(Objects, false) == 0)
		{
			return FText::Format(LOCTEXT("AgentUI.AssetNotDeleted", "Could not delete {0}. Close any editor it is open in and try again."), FText::FromString(Row.Path)).ToString();
		}
		Row.bUndone = true;
		return FString();
	}

	if (Row.Change == TEXT("moved"))
	{
		UObject* Moved = Row.MovedTo.IsEmpty() ? nullptr : ChangesMainAsset(Row.MovedTo);
		if (!Moved)
		{
			return FText::Format(LOCTEXT("AgentUI.MovedNotFound", "Could not find where {0} was moved to. Move it back in the Content Browser."), FText::FromString(Row.Path)).ToString();
		}
		IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		TArray<FAssetRenameData> Rename;
		Rename.Emplace(Moved, FPackageName::GetLongPackagePath(Row.Path), FPackageName::GetShortName(Row.Path));
		if (!Tools.RenameAssets(Rename))
		{
			return FText::Format(LOCTEXT("AgentUI.NotMovedBack", "Could not move {0} back."), FText::FromString(Row.Path)).ToString();
		}
		Row.bUndone = true;
		return FString();
	}

	// Edited or deleted: the package file from before the message goes back, and the editor reloads it.
	IFileManager& Files = IFileManager::Get();
	if (Row.Backup.IsEmpty() || Row.DiskPath.IsEmpty() || !Files.FileExists(*Row.Backup))
	{
		return FText::Format(LOCTEXT("AgentUI.NoAssetCopy", "The copy of {0} from before this message is gone."), FText::FromString(Row.Path)).ToString();
	}
	UPackage* Loaded = FindPackage(nullptr, *Row.Path);
	// The open level is not reloaded under the user: its file goes back, and opening it again shows it.
	const bool bOpenLevel = Loaded && GEditor && GEditor->GetEditorWorldContext().World() &&
		GEditor->GetEditorWorldContext().World()->GetOutermost() == Loaded;
	if (Loaded) ResetLoaders(Loaded);
	if (Files.Copy(*Row.DiskPath, *Row.Backup, true, true) != COPY_OK)
	{
		return FText::Format(LOCTEXT("AgentUI.AssetNotRestored", "Could not put {0} back. Close any editor it is open in and try again."), FText::FromString(Row.Path)).ToString();
	}
	Row.bUndone = true;
	if (bOpenLevel)
	{
		return LOCTEXT("AgentUI.LevelFileRestored", "The level's file is back as it was before this message. Open the level again (File › Open Level) to see it.").ToString();
	}
	if (Loaded)
	{
		UPackageTools::ReloadPackages({ Loaded });
	}
	else
	{
		ChangesRegistry().ScanFilesSynchronous({ Row.DiskPath }, true);
	}
	return FString();
}

void FZettaAgentChangeLog::Prune()
{
	const FString Folder = BackupFolder();
	TArray<FString> Old;
	const FDateTime Cutoff = FDateTime::UtcNow() - FTimespan::FromDays(7);
	IFileManager::Get().IterateDirectory(*Folder, [&Old, &Cutoff](const TCHAR* Path, bool bDirectory)
	{
		if (bDirectory && IFileManager::Get().GetTimeStamp(Path) < Cutoff) Old.Add(Path);
		return true;
	});
	for (const FString& Dir : Old) IFileManager::Get().DeleteDirectory(*Dir, false, true);
}

// ─── What the tools call ──────────────────────────────────────────────────────

namespace ZettaAgentChanges
{
	void WillWriteFile(const FString& Path) { FZettaAgentChangeLog::Get().WillWriteFile(Path); }
	void WillChangeAsset(const FString& ContentPath, const FString& What) { FZettaAgentChangeLog::Get().WillChangeAsset(ContentPath, What); }
	void CreatedAsset(const FString& ContentPath) { FZettaAgentChangeLog::Get().CreatedAsset(ContentPath); }
	void LevelTransaction(const FString& Title) { FZettaAgentChangeLog::Get().LevelTransaction(Title); }
}

#undef LOCTEXT_NAMESPACE
