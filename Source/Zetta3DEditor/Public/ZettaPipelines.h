// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "ZettaImporter.h"

/**
 * 3D PIPELINES, AS A SERVICE — what the Pipelines page does, for the agent (and anything else in the editor).
 *
 * A pipeline is a saved recipe of Studio tools that starts from one seed (a picture, a library model, or pictures of
 * each side). The service builds and edits recipes with the same builders the page uses, so whatever it saves is
 * priced and runnable as it stands; runs it on seeds; follows runs while the editor is open and imports what they
 * make. Every call answers on the game thread, exactly once.
 *
 * Every result carries Text: what the agent tells the model (or, when bOk is false, a one-line error it can act on).
 * Step kinds: remove_background, ai_edit, draw_views, isolate_parts, generate_3d, generate_smart_3d, retopology,
 * segmentation, texture, smart_uv, rigging (seed_picture / seed_model come from Build's StartsFrom). Splats and
 * Export are web-only and never offered here.
 *
 * Calls that spend credits (Run; Manage's run_waiting, run_step, retry_step and set_mode to auto) and Manage's cancel
 * come with a Describe… call that checks everything first and returns the approval card's content.
 */
namespace ZettaPipelines
{
	struct FResult
	{
		bool bOk = false;
		/** For the model: the answer, or a one-line error. */
		FString Text;
	};

	// ─── Reading ─────────────────────────────────────────────────────────────────────────────────────────────────────

	struct FPipelineInfo
	{
		int64 Id = 0;
		FString Title;
		int32 Steps = 0;
		TOptional<int64> LastRunId;
		FString LastRunStatus;
		FDateTime LastRunAt;
		FDateTime UpdatedAt;
		/** How many of its runs are in each state (running, awaiting, draft, paused, done, failed, cancelled). */
		TMap<FString, int32> RunCounts;
	};

	struct FListResult : FResult
	{
		/** Newest first. */
		TArray<FPipelineInfo> Pipelines;
	};

	/** list_pipelines. */
	ZETTA3DEDITOR_API void List(TFunction<void(const FListResult&)> Done);

	struct FNodeInfo
	{
		FString Id;
		FString Kind;
		/** The user's name for it, or the kind's label. */
		FString Name;
		/** What it is set to, in the Studio's words. */
		FString Summary;
		/** Its settings in a few words, agent-shaped ("model=n25, quality=high-res, …"). */
		FString Settings;
		/** Where its input comes from: "<nodeId>.<output>", or empty (a seed, or not connected). */
		FString From;
		/** One run of it, in credits; unset while prices are unknown. */
		TOptional<int32> Price;
		/** Runs once per part (after Isolate parts › each). */
		bool bPerPart = false;
		/** Its input is given on each run (run inputs). */
		bool bPerRunInput = false;
		/** Web-only, or newer than this plugin: kept, never changed here. */
		bool bReadOnly = false;
	};

	struct FProblem
	{
		/** Empty: the pipeline as a whole. */
		FString NodeId;
		FString Message;
	};

	struct FRunInfo
	{
		int64 Id = 0;
		/** draft | running | awaiting | paused | done | failed | cancelled */
		FString Status;
		/** manual | auto */
		FString Mode;
		bool bPaused = false;
		int32 StepsDone = 0, StepsTotal = 0, StepsWaiting = 0, StepsFailed = 0;
		int32 InputsNeeded = 0;
		int32 SpentCredits = 0, EstimatedCredits = 0;
		FString SeedType, SeedName;
		FDateTime CreatedAt;
	};

	struct FPipelineDetails : FResult
	{
		int64 Id = 0;
		FString Title;
		/** picture | model | views | empty (no seed) */
		FString StartsFrom;
		TArray<FNodeInfo> Nodes;
		/** What stops it running; empty = runnable. */
		TArray<FProblem> Problems;
		TArray<FString> Warnings;
		/** The most a run can cost; unset while prices are unknown. */
		TOptional<int32> EstimateCredits;
		/** Steps run once per part: the estimate assumes Max parts. */
		bool bEstimatePerPart = false;
		/** The node ids that take a per-run input (FRunRequest::Inputs). */
		TArray<FString> PerRunInputs;
		/** The newest runs (up to 5) and how many it has in all. */
		TArray<FRunInfo> RecentRuns;
		int32 RunCount = 0;
	};

	/** get_pipeline: its steps, problems, estimate and recent runs. */
	ZETTA3DEDITOR_API void Get(int64 PipelineId, TFunction<void(const FPipelineDetails&)> Done);

	// ─── Building and editing ────────────────────────────────────────────────────────────────────────────────────────

	struct FBuildStep
	{
		FString Kind;
		/** The index of an earlier step this one follows; unset = the seed. */
		TOptional<int32> After;
		/** After isolate_parts: character (default) | each | part:<key>. */
		FString Output;
		/**
		 * The web editor's shapes; omitted fields take the Studio's defaults. Generate steps take pbr and faceCount
		 * (a number) as well as the web's pbrMaterials / polygonCount.
		 */
		TSharedPtr<FJsonObject> Settings;
	};

	struct FBuildRequest
	{
		/** ≤200 characters. */
		FString Title;
		/** picture | model */
		FString StartsFrom = TEXT("picture");
		/** 1–24 steps. */
		TArray<FBuildStep> Steps;
		/** Open the new pipeline on the Pipelines page. */
		bool bOpen = true;
	};

	struct FChange
	{
		/** add | set | remove */
		FString Op;
		/** add: the step kind. */
		FString Kind;
		/** add: the node id it follows (empty = the seed). */
		FString After;
		/** add after isolate_parts: character | each | part:<key>. */
		FString Output;
		/** set / remove: the node. */
		FString NodeId;
		/** add / set: what to set (merged over what the step has). */
		TSharedPtr<FJsonObject> Settings;
	};

	struct FEditRequest
	{
		int64 PipelineId = 0;
		/** A new title (≤200), when set. */
		TOptional<FString> Title;
		/** 0–24 changes (none with a Title renames only). */
		TArray<FChange> Changes;
	};

	struct FSaveResult : FResult
	{
		int64 PipelineId = 0;
		/** Build: each step's node id, in step order (empty = skipped). Edit: the ids of the steps added. */
		TArray<FString> NodeIds;
		/** "steps[2]: reason" / "changes[1]: reason". */
		TArray<FString> Skipped;
		/** Build: opened on the Pipelines page. */
		bool bOpened = false;
		/** The pipeline as saved: problems, estimate, steps. */
		FPipelineDetails Pipeline;
	};

	/** build_pipeline: a new pipeline, saved — priced and runnable as it stands. */
	ZETTA3DEDITOR_API void Build(const FBuildRequest& Request, TFunction<void(const FSaveResult&)> Done);
	/** edit_pipeline: changes applied as the editor applies them, then saved. A page showing it reloads. */
	ZETTA3DEDITOR_API void Edit(const FEditRequest& Request, TFunction<void(const FSaveResult&)> Done);

	// ─── Running ─────────────────────────────────────────────────────────────────────────────────────────────────────

	/** One seed: exactly one of the three. */
	struct FSeed
	{
		/** A library picture (generate_image's imageId) — a picture seed. */
		TOptional<int64> ImageId;
		/** A finished 3D library model — a model seed. */
		TOptional<int64> GenerationId;
		/**
		 * A picture or model in the project: a texture asset (/Game/…) or a .png/.jpg/.jpeg file (a picture); a model
		 * imported from Zetta 3D (/Game/… inside its folder) or a .glb file (a model, uploaded to the library for free).
		 */
		FString AssetPath;
	};

	/** A per-run input: AI texturing's description or reference picture, or an askAtRun AI edit's instruction. */
	struct FRunInput
	{
		FString NodeId;
		/** ≤2000 characters. */
		FString Prompt;
		/** A library picture — AI texturing's reference. */
		TOptional<int64> ImageId;
	};

	struct FRunRequest
	{
		int64 PipelineId = 0;
		/** 1–20 seeds, each its own run. */
		TArray<FSeed> Seeds;
		/** manual (each paid step waits for a Run) | auto (every step submits itself). */
		FString Mode = TEXT("manual");
		TArray<FRunInput> Inputs;
		/** Who started it — handed back in FRunEvent. Empty = the user. */
		FString AgentSessionId;
		/** Import each run's final models into the project as they finish. */
		bool bImportResults = true;
	};

	/** What an approval card shows before a call that spends credits (or cancels a run). */
	struct FApproval
	{
		/** The call needs the user's yes. False: run it straight away. */
		bool bNeeded = false;
		/** generate | destructive */
		FString Kind;
		FString Title;
		FString Body;
		FString ConfirmLabel;
		TOptional<int32> Credits;
		/** Set when the call cannot run as asked: show it (and only offer Skip). */
		FString Error;
	};

	/** The run_pipeline card: checks the pipeline, its per-run inputs and every seed; nothing is uploaded yet. */
	ZETTA3DEDITOR_API void DescribeRun(const FRunRequest& Request, TFunction<void(const FApproval&)> Done);

	struct FRunResult : FResult
	{
		TArray<int64> RunIds;
		/** Runs created but not started (left as drafts), with why. */
		TArray<FString> NotStarted;
	};

	/** run_pipeline (after approval): uploads the seeds, makes the runs, gives their inputs, starts and follows them. */
	ZETTA3DEDITOR_API void Run(const FRunRequest& Request, TFunction<void(const FRunResult&)> Done);

	struct FStepInfo
	{
		/** The step id manage actions take. */
		int64 Id = 0;
		FString NodeId;
		/** "s1 Generate 3D [part sword]". */
		FString Name;
		/** Downstream of Isolate parts › each: the part's key, and its label. */
		FString PartKey, PartLabel;
		/** waiting | ready | running | submitted | review | success | failed | cancelled */
		FString Status;
		int32 Attempt = 0;
		/** Spent so far. */
		int32 Credits = 0;
		/** What it will cost when run (waiting steps); unset while unknown. */
		TOptional<int32> Price;
		FString Error;
		/** The library model it made or reads. */
		TOptional<int64> GenerationId;
		FString GenerationTitle, GenerationStatus;
		/** picture | views | isolated | mesh | splat | file | empty */
		FString Output;
		/** Picture outputs as (label, signed URL — it expires). */
		TArray<TPair<FString, FString>> Pictures;
	};

	/** An Isolate parts step waiting for the parts to be chosen (Manage choose_parts). */
	struct FPartReview
	{
		int64 StepId = 0;
		FString Subject;
		bool bHumanoid = false;
		/** (key, label) of what it found. */
		TArray<TPair<FString, FString>> Parts;
		TArray<FString> ProposedKeys;
	};

	struct FRunStatus : FResult
	{
		int64 RunId = 0;
		int64 PipelineId = 0;
		FString Status;
		FString Mode;
		bool bPaused = false;
		bool bLive = false;
		int32 SpentCredits = 0, EstimatedCredits = 0;
		TArray<FStepInfo> Steps;
		TArray<FPartReview> Reviews;
		/** Final model steps ready to import (ImportResults). */
		TArray<FString> ImportableNodeIds;
	};

	/** pipeline_run_status. */
	ZETTA3DEDITOR_API void RunStatus(int64 RunId, TFunction<void(const FRunStatus&)> Done);

	struct FManageRequest
	{
		int64 RunId = 0;
		/** pause | resume | cancel | set_mode | run_waiting | run_step | retry_step | choose_parts */
		FString Action;
		/** set_mode: manual | auto. */
		FString Mode;
		/** run_step / retry_step / choose_parts. */
		TOptional<int64> StepId;
		/** choose_parts: the review's part keys to isolate, and parts to add by name (≤16 each, ≤80 characters). */
		TArray<FString> Keys;
		TArray<FString> ExtraParts;
		FString AgentSessionId;
	};

	/** The card for a manage action that spends credits or cancels (bNeeded false for the others). */
	ZETTA3DEDITOR_API void DescribeManage(const FManageRequest& Request, TFunction<void(const FApproval&)> Done);
	/** manage_pipeline_run (after approval where one is needed). Answers with the run's new status; a live run is followed. */
	ZETTA3DEDITOR_API void Manage(const FManageRequest& Request, TFunction<void(const FRunStatus&)> Done);

	struct FImportRequest
	{
		int64 RunId = 0;
		/** The steps whose models to import (≤50); empty = the run's final models. */
		TArray<FString> NodeIds;
		/** Place them in the level (Place.Location / Place.Parent as given). */
		bool bPlace = false;
		FZettaImportOptions Place;
	};

	struct FImportResult : FResult
	{
		TArray<FString> AssetPaths;
		TArray<FString> Failures;
		/** Steps with nothing to import, and why. */
		TArray<FString> Skipped;
	};

	/** import_pipeline_results: mesh steps only (never a splat or an unfinished step). */
	ZETTA3DEDITOR_API void ImportResults(const FImportRequest& Request, TFunction<void(const FImportResult&)> Done);

	/** Opens the Zetta 3D window's Pipelines page on a pipeline — on its Runs, with a run open, when RunId is set. */
	ZETTA3DEDITOR_API void OpenInWindow(int64 PipelineId, TOptional<int64> RunId = {});

	/** Stops following runs (the module calls it as it shuts down). */
	ZETTA3DEDITOR_API void Shutdown();

	// ─── Followed runs ───────────────────────────────────────────────────────────────────────────────────────────────

	struct FRunEvent
	{
		int64 RunId = 0;
		int64 PipelineId = 0;
		/** done | failed | cancelled — or awaiting, when it waits for the user (a step to run, parts to choose). */
		FString Status;
		/** Models imported for it. */
		TArray<FString> ImportedPaths;
		/** Who started it (FRunRequest::AgentSessionId); empty = the user. */
		FString AgentSessionId;
		/** The run's status, as pipeline_run_status reads it. */
		FString Text;
	};

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnRunEvent, const FRunEvent&);
	/** A followed run finished, or now needs the user (pipeline_run_finished). Raised on the game thread. */
	ZETTA3DEDITOR_API FOnRunEvent& OnRunFinished();

	// ─── Tool input ──────────────────────────────────────────────────────────────────────────────────────────────────
	// The protocol's inputs, validated with one-line errors the model can act on. Each returns false and sets OutError
	// when the input is not usable.

	/** A pipeline or run id ("#12" and "12" accepted). */
	ZETTA3DEDITOR_API bool ParseId(const TSharedPtr<FJsonObject>& Input, const FString& Key, int64& OutId, FString& OutError);
	/** build_pipeline { title, startsFrom, steps: [{ kind, after?, output?, settings? }], open? }. */
	ZETTA3DEDITOR_API bool ParseBuild(const TSharedPtr<FJsonObject>& Input, FBuildRequest& Out, FString& OutError);
	/** edit_pipeline { pipelineId, title?, changes }. */
	ZETTA3DEDITOR_API bool ParseEdit(const TSharedPtr<FJsonObject>& Input, FEditRequest& Out, FString& OutError);
	/** run_pipeline { pipelineId, seeds, mode?, inputs? }. The seeds' type is checked against the pipeline in DescribeRun. */
	ZETTA3DEDITOR_API bool ParseRun(const TSharedPtr<FJsonObject>& Input, FRunRequest& Out, FString& OutError);
	/** manage_pipeline_run { runId, action, mode?, stepId?, keys?, extraParts? }. */
	ZETTA3DEDITOR_API bool ParseManage(const TSharedPtr<FJsonObject>& Input, FManageRequest& Out, FString& OutError);
	/** import_pipeline_results { runId, nodeIds? } — the agent resolves place { parent?, position? } itself. */
	ZETTA3DEDITOR_API bool ParseImport(const TSharedPtr<FJsonObject>& Input, FImportRequest& Out, FString& OutError);

	/** The chat label of a pipeline tool call ("Running pipeline #12 on 3 seeds"), or empty for another tool. */
	ZETTA3DEDITOR_API FString DescribeCall(const FString& ToolName, const TSharedPtr<FJsonObject>& Input);
	/** The approval card detail of a pipeline write, or empty. */
	ZETTA3DEDITOR_API FString SummarizeCall(const FString& ToolName, const TSharedPtr<FJsonObject>& Input);
	/** The step kinds the agent may use, in the palette's order. */
	ZETTA3DEDITOR_API TArray<FString> StepKinds();
}
