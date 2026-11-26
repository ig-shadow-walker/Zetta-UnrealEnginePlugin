// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "ZettaModels.h"

struct FZettaResponse;

/**
 * The library's logic that isn't drawing: pipeline-run groups (the web's run-groups.ts), what can be forked,
 * which imported assets a model has in this project, imports under way, and deletes a few at a time.
 */
namespace ZettaLibrary
{
	// ─── Pipeline run groups ────────────────────────────────────────────────────

	/** What a run is called — the name its models share (the seed's) — and its detail line ("3 models · run #12"). */
	void RunName(const FZettaRunGroup& Group, FString& OutName, FString& OutDetail);
	/** How many models the run made (the server's count, or the members sent). */
	int32 RunCount(const FZettaRunGroup& Group);
	/** A run's models in the order it made them: the pipeline's step order where known, made-at otherwise, id breaking a tie. */
	TArray<FZettaGenerationPtr> InRunOrder(const TArray<FZettaGenerationPtr>& Members);
	/** A model's step tag: "Step 2 · Retopology · seat". */
	FString StepText(const FZettaGeneration& Member, int32 Index);
	/** The page's rows with each run's models in place of its entry — what select-all and bulk actions look through. */
	TArray<FZettaGenerationPtr> Flatten(const TArray<FZettaGenerationPtr>& Entries);
	bool IsRunEntry(const FZettaGenerationPtr& Row);

	// ─── What a row can do ──────────────────────────────────────────────────────

	/** Fork puts a generation's settings back on Generate or Smart 3D: only the three ways in, on those engines. */
	bool CanFork(const FZettaGeneration& Row);
	/** The engine's name for a badge — only for engines Zetta names; never a raw id. */
	FString ModelBadge(const FString& Model);
	/** "Nov 12, 2026". */
	FText DateText(const FDateTime& When);
	void OpenOnSite(const FZettaGeneration& Row);

	// ─── In the project ─────────────────────────────────────────────────────────

	/** Every asset an import of this model made that still exists in the project (main mesh first). */
	TArray<FAssetData> ImportedAssets(int64 GenerationId);
	/** Just its meshes — what gets placed or dragged into the level. */
	TArray<FAssetData> ImportedMeshes(int64 GenerationId);
	/** Selects the model in the Content Browser (its assets, or its folder when they are gone). */
	void ShowInContentBrowser(int64 GenerationId);
	/** A short-lived note in the editor's corner. */
	void Toast(const FText& Message, bool bError = false);

	// ─── Imports under way ──────────────────────────────────────────────────────

	struct FImporting
	{
		float Progress = 0.f;
		FString Status;
	};

	/** Imports in flight, by generation id. Lives outside any widget so a redraw (or a closed window) keeps them. */
	TMap<int64, FImporting>& Importing();
	bool IsImporting(int64 GenerationId);

	// ─── Deletes ────────────────────────────────────────────────────────────────

	/** The delete confirmation's words for a batch (the web's bulk-delete dialog). */
	void BulkDeleteText(int32 Count, FText& OutTitle, FText& OutMessage);

	/**
	 * Deletes generations a few at a time — quick, but not enough to trip the rate limit. Progress reports
	 * (done, total) after each one; Done gets the ids deleted and the ids that could not be.
	 */
	void RunDeletes(const TArray<int64>& Ids, TFunction<void(int32, int32)> Progress,
		TFunction<void(const TArray<int64>& /*Deleted*/, const TArray<int64>& /*Failed*/, const FString& /*LastError*/)> Done);
}
