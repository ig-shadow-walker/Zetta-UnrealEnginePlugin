// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "ZettaImporter.h"
#include "ZettaModels.h"

/** A 3D job the plugin started (or was asked to follow) and is watching until it finishes. */
struct ZETTA3DEDITOR_API FZettaJob
{
	FZettaGenerationPtr Generation;
	TOptional<int32> QueuePosition;
	/** Import into the project when it succeeds. */
	bool bAutoImport = true;
	FZettaImportOptions ImportOptions;
	/** Who started it: "studio", "library", "agent", "pipeline". */
	FString Source;
	bool bImporting = false;
	float ImportProgress = 0.f;
	FString ImportStatus;
	TOptional<FZettaImportResult> Import;
	FString Error;
	double StartedAt = 0;
	bool bPollInFlight = false;

	int64 Id() const { return Generation.IsValid() ? Generation->Id : 0; }
	bool IsFinished() const { return Generation.IsValid() && Generation->IsTerminal() && !bImporting; }
};

using FZettaJobPtr = TSharedPtr<FZettaJob>;

/**
 * Follows running 3D jobs: polls every 2.5s, imports what succeeds (when asked), and tells listeners —
 * the Studio's jobs strip, the library, the agent — when anything changes or finishes.
 */
class ZETTA3DEDITOR_API FZettaJobs
{
public:
	static FZettaJobs& Get();

	FZettaJobPtr Track(const FZettaGenerationPtr& Generation, bool bAutoImport, const FZettaImportOptions& Options, const FString& Source);
	FZettaJobPtr Find(int64 GenerationId) const;
	const TArray<FZettaJobPtr>& GetJobs() const { return Jobs; }
	void Dismiss(int64 GenerationId);
	void DismissFinished();
	/** Imports a finished job's model by hand (e.g. after an auto-import failed). */
	void ImportNow(int64 GenerationId, const FZettaImportOptions& Options);
	void Cancel(int64 GenerationId);
	int32 RunningCount() const;

	void Shutdown();

	DECLARE_MULTICAST_DELEGATE(FOnJobsChanged);
	FOnJobsChanged OnChanged;
	/** A job reached the end: success (imported or not), failure, cancellation. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnJobFinished, const FZettaJobPtr&);
	FOnJobFinished OnFinished;

private:
	bool Tick(float DeltaTime);
	void Poll(const FZettaJobPtr& Job);
	void Completed(const FZettaJobPtr& Job);
	void Notify(const FZettaJobPtr& Job);

	TArray<FZettaJobPtr> Jobs;
	FTSTicker::FDelegateHandle TickHandle;
	double LastPoll = 0;
};
