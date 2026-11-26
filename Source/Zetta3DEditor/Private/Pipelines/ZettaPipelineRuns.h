// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "ZettaApi.h"
#include "ZettaModels.h"
#include "ZettaPipelineRecipe.h"

// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// Pipelines and runs as the server returns them, their status vocabularies, and every /pipelines and /pipeline-runs
// route, typed. Callbacks run on the game thread.
// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

/** A pipeline, as GET /pipelines/:id returns it. */
struct FZettaPipeline
{
	int64 Id = 0;
	FString Title;
	FZettaPipelineGraph Graph;
	FDateTime CreatedAt, UpdatedAt;
	/** The server's own problems (note it also asks for a seed picture, which the editor does not). */
	TArray<FZettaPipelineProblem> Problems;
	static FZettaPipeline From(const TSharedPtr<FJsonObject>& O);
};

/** A recipe from a description: a title, one line saying what was made or changed, and the graph (settings only). */
struct FZettaPipelineDraft
{
	FString Title;
	FString Summary;
	FZettaPipelineGraph Graph;
	static FZettaPipelineDraft From(const TSharedPtr<FJsonObject>& O);
};

/** A pipeline as GET /pipelines lists it. */
struct FZettaPipelineSummary
{
	int64 Id = 0;
	FString Title;
	int32 Steps = 0;
	FDateTime CreatedAt, UpdatedAt;
	TOptional<int64> LastRunId;
	FString LastRunStatus;
	FDateTime LastRunAt;
	/** How many of its runs are in each state. */
	TMap<FString, int32> RunCounts;
	int32 Count(const FString& Status) const { return RunCounts.FindRef(Status); }
	static FZettaPipelineSummary From(const TSharedPtr<FJsonObject>& O);
};

/** A run as GET /pipelines/:id/runs lists it (POST /batch returns a lighter one). */
struct FZettaRunSummary
{
	int64 Id = 0;
	FString Mode, Status;
	bool bPaused = false;
	int32 EstimatedCredits = 0, SpentCredits = 0;
	FDateTime CreatedAt;
	TOptional<FDateTime> FinishedAt;
	bool bHasProgress = false;
	int32 Done = 0, Total = 0, Waiting = 0, Failed = 0;
	/** A draft's steps still to fill in before it can start. */
	int32 InputsNeeded = 0;
	bool bHasSeed = false;
	FString SeedType, SeedName, SeedThumbnailUrl;
	bool IsLive() const;
	static FZettaRunSummary From(const TSharedPtr<FJsonObject>& O);
};

/** A picture the server signed for display (the URL expires). */
struct FZettaSignedPicture
{
	FString Url;
	TOptional<int64> Gen2dPostId;
	static FZettaSignedPicture From(const TSharedPtr<FJsonObject>& O);
};

struct FZettaIdentifiedPart
{
	FString Key, Label, Kind, Level;
	/** x, y, width, height in 0–1 of the source picture; empty when unknown. */
	TArray<double> Box;
};

/** Isolate Parts waiting for the user (step status review): what it found, what it proposes, the picture it read. */
struct FZettaStepReview
{
	FString Subject;
	bool bHumanoid = false;
	TArray<FZettaIdentifiedPart> Parts;
	TArray<FString> ProposedKeys;
	FZettaSignedPicture Source;
};

struct FZettaIsolatedPart
{
	FString Key, Label, Kind;
	TArray<double> Box;
	FZettaSignedPicture Picture;
};

/** What a step produced — a union on Type: picture, views, isolated, mesh, splat or file. Raw always holds the JSON. */
struct FZettaStepOutput
{
	FString Type;
	TSharedPtr<FJsonObject> Raw;
	FZettaSignedPicture Picture;
	TArray<TPair<FString, FZettaSignedPicture>> Views;
	FZettaSignedPicture Character;
	TArray<FZettaIsolatedPart> Parts;
	int64 Gen3dPostId = 0;
	FString Format;

	bool IsPicture() const { return Type == TEXT("picture"); }
	bool IsViews() const { return Type == TEXT("views"); }
	bool IsIsolated() const { return Type == TEXT("isolated"); }
	bool IsMesh() const { return Type == TEXT("mesh"); }
	bool IsSplat() const { return Type == TEXT("splat"); }
	bool IsFile() const { return Type == TEXT("file"); }
	/** The 3D post the output names (mesh, splat, file). */
	TOptional<int64> Gen3dPost() const;
	static TOptional<FZettaStepOutput> From(const TSharedPtr<FJsonObject>& O);
};

/** One step of a run — one per node, or one per part downstream of Isolate Parts' "each". */
struct FZettaPipelineRunStep
{
	int64 Id = 0;
	FString NodeId;
	/** Empty normally; the part's key (nested as parent/child) downstream of "each". */
	FString PartKey;
	FString Status;
	int32 Attempt = 0;
	int32 Credits = 0;
	FString ErrorMessage;
	TOptional<int64> Gen3dPostId;
	TOptional<FZettaStepOutput> Output;
	TOptional<FZettaStepReview> Review;
	/** The 3D job this step made or reads, when there is one. */
	FZettaGenerationPtr Generation;
	FDateTime CreatedAt, UpdatedAt;
};

/** Which finished mesh steps of a run are imported into the project. */
enum class EZettaRunImport : uint8
{
	None,
	/** The mesh steps nothing in the run's graph takes further — the run's results. */
	FinalSteps,
	/** Every mesh step (a generation, then its retopology, its texture, …). */
	AllMeshSteps,
};

/** What one node is doing in a run, from all of its steps. */
struct FZettaNodeRunState
{
	/** idle (no steps yet), or one of the step statuses (running covers ready/submitted). */
	FString Status = TEXT("idle");
	int32 Total = 0, Done = 0, Credits = 0;
	FString Label() const { return StatusLabel(Status); }
	static FZettaNodeRunState Of(const TArray<const FZettaPipelineRunStep*>& Steps);
	static FString StatusLabel(const FString& Status);
};

/** One run of a pipeline: the graph applied to one seed, on its own copy of the graph. */
struct FZettaPipelineRun
{
	int64 Id = 0;
	int64 PipelineId = 0;
	FString Mode, Status;
	bool bPaused = false;
	/** The seed's request plus its type (picture | model | views). */
	TSharedPtr<FJsonObject> Seed;
	int32 EstimatedCredits = 0, SpentCredits = 0;
	/** The run's own copy of the graph — per-run settings live here, never on the pipeline. */
	FZettaPipelineGraph Graph;
	FDateTime CreatedAt, UpdatedAt;
	TOptional<FDateTime> FinishedAt;
	TArray<FZettaPipelineRunStep> Steps;

	bool IsLive() const;
	TArray<const FZettaPipelineRunStep*> StepsOf(const FString& NodeId) const;
	const FZettaPipelineRunStep* Step(int64 StepId) const;
	FZettaNodeRunState NodeState(const FString& NodeId) const;
	/** A node's settings may still be changed for this run: not a seed, not cancelled, every step still waiting. */
	bool IsNodeEditable(const FString& NodeId) const;
	/** "knight/sword" → the label the isolation gave "sword". */
	FString PartLabel(const FString& PartKey) const;
	/** The finished, importable mesh steps (never a seed, a splat or an unfinished generation). */
	TArray<const FZettaPipelineRunStep*> ImportableSteps(EZettaRunImport ImportMode) const;
	int32 WaitingCount() const;

	static TSharedRef<FZettaPipelineRun> From(const TSharedPtr<FJsonObject>& O);
};

using FZettaPipelineRunPtr = TSharedPtr<FZettaPipelineRun>;

/** The server's estimate of a graph (POST /pipelines/estimate). */
struct FZettaServerEstimate
{
	int32 Credits = 0;
	bool bPerPart = false;
	TArray<FZettaPipelineProblem> Problems;
	static FZettaServerEstimate From(const TSharedPtr<FJsonObject>& O);
};

enum class EZettaRunTone : uint8 { Running, Attention, Done, Failed, Idle };

namespace ZettaRunStatus
{
	extern const TCHAR* Draft;
	extern const TCHAR* Running;
	extern const TCHAR* Awaiting;
	extern const TCHAR* Paused;
	extern const TCHAR* Failed;
	extern const TCHAR* Done;
	extern const TCHAR* Cancelled;
	extern const TCHAR* ModeAuto;
	extern const TCHAR* ModeManual;

	/** Still moving — kept polling while it is. */
	bool IsLive(const FString& Status);
	/** A run that is over, or a draft never started, may be deleted. */
	bool IsDeletable(const FString& Status);
	/** The Runs view's words. */
	FString Label(const FString& Status);
	EZettaRunTone Tone(const FString& Status);
	FString ModeLabel(const FString& Mode);
	FString ModeHint(const FString& Mode);
}

namespace ZettaStepStatus
{
	extern const TCHAR* Waiting;
	extern const TCHAR* Ready;
	extern const TCHAR* Running;
	extern const TCHAR* Submitted;
	extern const TCHAR* Review;
	extern const TCHAR* Success;
	extern const TCHAR* Failed;
	extern const TCHAR* Cancelled;
}

/** Every 3D Pipelines route, typed. Graphs are always sent whole, with every request rebuilt. */
namespace ZettaPipelinesApi
{
	/** The steps a refusal names (PIPELINE_NOT_RUNNABLE, RUN_NEEDS_INPUT). */
	TArray<FZettaPipelineProblem> ProblemsOf(const FZettaResponse& Response);
	/** ZettaCatalog::Explain, with the steps a refusal names. */
	FString Explain(const FZettaResponse& Response);

	using FRunDone = TZettaDone<FZettaPipelineRunPtr>;

	void List(TZettaDone<TArray<FZettaPipelineSummary>> Done);
	void Get(int64 Id, TZettaDone<FZettaPipeline> Done);
	void Create(const FString& Title, const FZettaPipelineGraph* Graph, TZettaDone<FZettaPipeline> Done);
	void Update(int64 Id, const TOptional<FString>& Title, const FZettaPipelineGraph* Graph, TZettaDone<FZettaPipeline> Done);
	/** Every request rebuilt from its settings, then PATCHed whole (the editor's autosave). */
	void Save(int64 Id, const FZettaPipelineGraph& Graph, const TOptional<FString>& Title, const FZettaEntitlements3D* Gates, TZettaDone<FZettaPipeline> Done);
	void Duplicate(int64 Id, TZettaDone<FZettaPipeline> Done);
	void Delete(int64 Id, FZettaCallback Done);
	void Estimate(const FZettaPipelineGraph& Graph, TZettaDone<FZettaServerEstimate> Done);
	/** A recipe from a description; with Current, the recipe on the canvas is changed as asked. Nothing is saved. */
	void Draft(const FString& Description, const FZettaPipelineGraph* Current, const FZettaEntitlements3D* Gates, TZettaDone<FZettaPipelineDraft> Done);

	void Runs(int64 PipelineId, TZettaDone<TArray<FZettaRunSummary>> Done);
	/** One run per seed (1–20). With bDraft, the runs are made, not started: give their inputs, then Start. */
	void Batch(int64 PipelineId, const FString& Mode, const TArray<TSharedPtr<FJsonObject>>& Seeds, const FZettaPipelineGraph* Graph, bool bDraft,
		TZettaDone<TArray<FZettaRunSummary>> Done);
	void DeleteRun(int64 RunId, FZettaCallback Done);
	void GetRun(int64 RunId, FRunDone Done);
	void Start(int64 RunId, FRunDone Done);
	void Pause(int64 RunId, FRunDone Done);
	void Resume(int64 RunId, FRunDone Done);
	void Cancel(int64 RunId, FRunDone Done);
	void SetMode(int64 RunId, const FString& Mode, FRunDone Done);
	void RunWaiting(int64 RunId, FRunDone Done);
	void PatchRunNode(int64 RunId, const FString& NodeId, const TSharedPtr<FJsonObject>& Request, const TSharedPtr<FJsonObject>& Settings, FRunDone Done);
	void RunStep(int64 RunId, int64 StepId, FRunDone Done);
	void RetryStep(int64 RunId, int64 StepId, const TSharedPtr<FJsonObject>& Request, const TSharedPtr<FJsonObject>& Settings, FRunDone Done);
	void ChooseParts(int64 RunId, int64 StepId, const TArray<FString>& Keys, const TArray<FString>& ExtraParts, FRunDone Done);
}
