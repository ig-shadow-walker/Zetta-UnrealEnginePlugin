// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "ZettaApi.h"
#include "ZettaImporter.h"
#include "ZettaPipelineRuns.h"

/**
 * What one run starts from — the seed node's request for that run: an uploaded picture {uploadKey, name}, a library
 * picture {gen2dPostId}, a library model {gen3dPostId}, or pictures of each side {views} — plus a name and a small
 * JPEG preview for the UI.
 */
struct FZettaPipelineSeed
{
	/** Longest edge a seed picture is uploaded at (the web's downscaleForUpload). */
	static constexpr int32 MaxEdge = 1536;
	static constexpr int32 ThumbnailSize = 160;

	TSharedPtr<FJsonObject> Request;
	FString Name;
	/** picture | model | views */
	FString Type = TEXT("picture");
	/** A ≤160px JPEG of the picture, when one could be made. */
	TArray<uint8> Thumbnail;
	/** A URL the UI may load instead (a library row's thumbnail). */
	FString ThumbnailUrl;

	FString ThumbnailDataUrl() const;
	TOptional<int64> ImagePostId() const;
	TOptional<int64> ModelPostId() const;

	/** A picture uploaded as the Studio uploads a reference: scaled to MaxEdge (JPEG 90 only when it was larger). */
	static void FromUpload(const TArray<uint8>& Bytes, const FString& ContentType, const FString& Name,
		TFunction<void(const FZettaResponse&, const FZettaPipelineSeed&)> Done, FZettaProgress Progress = nullptr);
	static FZettaPipelineSeed FromImagePost(int64 Gen2dPostId, const FString& Name = FString(), const FString& ThumbnailUrl = FString());
	static FZettaPipelineSeed FromModel(int64 Gen3dPostId, const FString& Name = FString(), const FString& ThumbnailUrl = FString());
	static FZettaPipelineSeed FromModel(const FZettaGeneration& Generation);
	/** A Multi-view seed's run, keyed by side — named and previewed by its front. */
	static FZettaPipelineSeed FromViews(const TSharedPtr<FJsonObject>& Set);

	/** Downloads a (signed) picture and makes its preview; empty when it cannot. */
	static void ThumbnailFromUrl(const FString& Url, TFunction<void(const TArray<uint8>&)> Done);
	static TArray<uint8> DownscaleForUpload(const TArray<uint8>& Bytes, const FString& ContentType, FString& OutType);
	static TArray<uint8> ThumbnailOf(const TArray<uint8>& Bytes);
	static FString DataUrl(const TArray<uint8>& Jpeg);
	/** The bytes of a data: URL, or empty. */
	static TArray<uint8> FromDataUrl(const FString& Url);
};

/** Pictures and models in the project, as seeds. */
namespace ZettaPipelineAssets
{
	/** A texture asset's picture (its source file when there is one, else its pixels as PNG). Empty + error otherwise. */
	bool PictureOfTexture(const FString& ObjectPath, TArray<uint8>& OutBytes, FString& OutContentType, FString& OutName, FString& OutError);
	/** Whether a content path or file is something a model seed can come from. */
	bool IsModelSource(const FString& Path);
	/**
	 * A model seed from the project: a model imported from Zetta 3D (its library row), or a .glb file on disk
	 * (uploaded to the library, free). Calls back with the library row or an error.
	 */
	void ModelFromAsset(const FString& Path, TFunction<void(const FZettaGenerationPtr&, const FString& /*Error*/)> Done,
		TFunction<void(const FString&)> Status = nullptr);
	/** Content Browser selection, as object paths. */
	TArray<FString> SelectedAssets();
}

/** How the service and the Pipelines page talk without depending on each other. */
namespace ZettaPipelineHooks
{
	struct FOpenRequest
	{
		int64 PipelineId = 0;
		TOptional<int64> RunId;
	};

	/** Asks the page to show a pipeline; the request waits until a page takes it (the page may not exist yet). */
	void RequestOpen(int64 PipelineId, TOptional<int64> RunId);
	TOptional<FOpenRequest> TakePending();
	FSimpleMulticastDelegate& OnOpenRequested();

	/** The agent saved a pipeline: a page showing it reads it again rather than keep (and autosave) its own copy. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSaved, int64);
	FOnSaved& OnSavedElsewhere();
}

/** The account's 3D price list, gates and image engines, read once and shared by the editor and the agent service. */
class FZettaPipelinePlan
{
public:
	static FZettaPipelinePlan& Get();

	const FZettaPriceList* Prices() const { return PriceList.IsSet() ? &PriceList.GetValue() : nullptr; }
	const FZettaEntitlements3D* Gates() const { return Gates3D.IsSet() ? &Gates3D.GetValue() : nullptr; }
	const FZettaImageModels* ImageModels() const { return Images.IsSet() ? &Images.GetValue() : nullptr; }

	/** Reads what is missing (or everything with bForce). Safe to call often. */
	void EnsureLoaded(bool bForce = false);
	/** Calls back once prices and gates are read (or could not be). */
	void WhenLoaded(TFunction<void()> Done);

	FSimpleMulticastDelegate OnChanged;

private:
	void Finish();
	TOptional<FZettaPriceList> PriceList;
	TOptional<FZettaEntitlements3D> Gates3D;
	TOptional<FZettaImageModels> Images;
	int32 Pending = 0;
	double LastTry = -1000;
	TArray<TFunction<void()>> Waiting;
	FDelegateHandle AuthHandle;
};

/**
 * KEEPS AN EYE ON RUNS while the editor is open (the web's polling, plus the Studio's own job follower): every 3 s
 * while a followed run is running, paused or waiting for the user, and the runs list of a followed pipeline while any
 * of its runs is. Optionally imports each finished mesh step (never a splat). Events arrive on the game thread.
 */
class FZettaRunFollower
{
public:
	static constexpr double PollSeconds = 3.0;
	/** A draft (not started) changes only when someone presses Start. */
	static constexpr double DraftPollSeconds = 15.0;

	struct FFollowed
	{
		int64 RunId = 0;
		int64 PipelineId = 0;
		/** The agent session that started it (empty = the user), so the agent can say when it is done. */
		FString AgentSessionId;
		EZettaRunImport Import = EZettaRunImport::None;
		/** Where imports go; empty = the project's models folder. */
		FString Folder;
		TSet<int64> ImportedSteps;
		TArray<FString> ImportedPaths;
		FString Status;
		double LastPolledAt = -1000;
		bool bInFlight = false;
		int32 ImportsRunning = 0;
		bool bFinished = false;
	};

	static FZettaRunFollower& Get();

	/** Follows a run until it is over. Following one already followed updates its options (imports so far are kept). */
	void Follow(int64 RunId, int64 PipelineId = 0, const FString& AgentSessionId = FString(), EZettaRunImport Import = EZettaRunImport::None, const FString& Folder = FString());
	void Unfollow(int64 RunId);
	/** Polls a pipeline's runs list while any of its runs is live. */
	void FollowRuns(int64 PipelineId);
	void UnfollowRuns(int64 PipelineId);
	bool IsFollowing(int64 RunId) const;
	TSharedPtr<FFollowed> Find(int64 RunId) const;
	FZettaPipelineRunPtr LatestRun(int64 RunId) const { return Latest.FindRef(RunId); }

	/** Reads a run now, outside the 3 s rhythm (right after an action). */
	void Refresh(int64 RunId, ZettaPipelinesApi::FRunDone Done = nullptr);
	/** Hands a run the caller has just read (an action's answer) to the follower, as if polled. */
	void Report(const FZettaPipelineRunPtr& Run);

	/**
	 * Imports the given (or all final) mesh steps of a run now. Splats and unfinished steps are skipped. Calls back with
	 * the imported asset paths and the failures ("nodeId: reason").
	 */
	static void Import(const FZettaPipelineRunPtr& Run, const TArray<FString>& NodeIds, const FZettaImportOptions& Options,
		TFunction<void(const TArray<FString>& /*Paths*/, const TArray<FString>& /*Failures*/)> Done);

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnRun, const FZettaPipelineRunPtr&);
	/** A followed run was read again. */
	FOnRun OnRunUpdated;
	/** A followed run now waits for the user (a step waiting for its Run, or parts to choose). */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRunWithEntry, const FZettaPipelineRunPtr&, const FFollowed&);
	FOnRunWithEntry OnRunNeedsUser;
	/** A followed run is over — done, stopped or cancelled — with what was imported for it. It is forgotten right after. */
	FOnRunWithEntry OnRunFinished;
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRuns, int64 /*PipelineId*/, const TArray<FZettaRunSummary>&);
	FOnRuns OnRunsUpdated;

	void Shutdown();

private:
	bool Tick(float DeltaTime);
	void Poll(const TSharedPtr<FFollowed>& Entry);
	void PollList(int64 PipelineId);
	void Handle(const TSharedPtr<FFollowed>& Entry, const FZettaPipelineRunPtr& Run);
	void ImportNext(const TSharedPtr<FFollowed>& Entry, const FZettaPipelineRunPtr& Run, TArray<int64> Queue, bool bNowAwaiting);
	void AfterImports(const TSharedPtr<FFollowed>& Entry, const FZettaPipelineRunPtr& Run, bool bNowAwaiting);
	void EnsureTicking();

	TArray<TSharedPtr<FFollowed>> Runs;
	TArray<int64> Pipelines;
	TSet<int64> ListsInFlight;
	double LastListPoll = -1000;
	TMap<int64, FZettaPipelineRunPtr> Latest;
	FTSTicker::FDelegateHandle TickHandle;
};
