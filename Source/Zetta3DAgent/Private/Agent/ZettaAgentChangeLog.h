// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"

/** One line of the changes card: a file, an asset, or the agent's steps in the level. */
struct FZettaChangeRow
{
	/** "file", "asset" or "level". */
	FString Kind;
	/** "created", "edited", "deleted" or "moved" (files and assets); "level" for the level steps. */
	FString Change;
	/** What the line shows: a project-relative file path, a content path, or the level's name. */
	FString Path;
	/** Files: the file on disk. Assets: the package file on disk. */
	FString DiskPath;
	/** The copy taken before the agent first touched it this message (empty when it did not exist). */
	FString Backup;
	/** Moved assets: where to. */
	FString MovedTo;
	/** Level: the agent's transactions, oldest first. */
	TArray<FGuid> Transactions;
	TArray<FString> TransactionTitles;
	int32 StepsUndone = 0;

	bool bKept = false;
	bool bUndone = false;

	/** Nothing left to decide on this line (a kept one can still be undone). */
	bool IsSettled() const { return bKept || bUndone; }
};

/**
 * What the agent changed in each message — files it wrote, assets it created, changed, moved or deleted, and its
 * transactions in the level — and how to take each back. Copies of what it changes are taken just before it first
 * touches them, under Saved/Zetta3D/Agent/Backups/<turn>/: never in the project's content or version control.
 *
 * The level's steps are the named transactions its tools make ("Zetta Agent: …"). The editor can only undo from
 * the top of its history, so each step remembers its transaction's id, and undoing checks that it is still the
 * newest — never undoing something the user did after it.
 */
class FZettaAgentChangeLog
{
public:
	static FZettaAgentChangeLog& Get();

	/** A new message: what its tools change from here on is one change set. Returns its id. */
	FString BeginTurn();
	FString CurrentTurn() const { return Turn; }
	/** True while the agent's tools run: only then is what is changed the agent's. */
	bool bRecording = false;

	bool Has(const FString& InTurn) const;
	TArray<FZettaChangeRow>* Rows(const FString& InTurn);

	/** Takes back one line. Returns a sentence for the user when it could not. */
	FString UndoRow(const FString& InTurn, int32 Index);
	/** Takes back the message: the level steps, then the files and assets. Kept lines stay unless bIncludeKept. */
	FString UndoAll(const FString& InTurn, bool bIncludeKept);
	void KeepRow(const FString& InTurn, int32 Index);
	void KeepAll(const FString& InTurn);

	/** Something was recorded, kept or undone: the cards redraw. */
	FSimpleMulticastDelegate OnChanged;

	// What the tools report (see ZettaAgentChanges in ZettaAgentTools.h).
	void WillWriteFile(const FString& Path);
	void WillChangeAsset(const FString& ContentPath, const FString& What);
	void CreatedAsset(const FString& ContentPath);
	void LevelTransaction(const FString& Title);

	/** Lets go of copies from older sessions (more than a week old). */
	void Prune();

private:
	FString BackupFolder() const;
	FString TakeCopy(const FString& DiskPath);
	FZettaChangeRow* Find(const FString& Kind, const FString& Path);
	FString UndoLevel(FZettaChangeRow& Row);
	FString UndoFile(FZettaChangeRow& Row);
	FString UndoAsset(FZettaChangeRow& Row);

	FString Turn;
	TMap<FString, TArray<FZettaChangeRow>> Sets;
	int32 Copies = 0;
};
