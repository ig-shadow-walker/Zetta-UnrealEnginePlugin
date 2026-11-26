// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** A picture a tool hands back to the model (capture_view). Base64, no data: prefix. */
struct ZETTA3DAGENT_API FZettaToolImage
{
	FString Data;
	FString MediaType = TEXT("image/jpeg");
};

/** What a client tool answers: plain text for the model (≤100,000 characters), or a one-line error it can act on. */
struct ZETTA3DAGENT_API FZettaToolResult
{
	FString Content;
	bool bIsError = false;
	TArray<FZettaToolImage> Images;
	/** Shown in the chat instead of the raw content, when set (e.g. "Spawned 3 actors"). */
	FString Summary;
	/** A library picture the chat should draw (picture tools). */
	TOptional<int64> ImageId;
	FString ImageUrl;

	static FZettaToolResult Ok(const FString& Text) { FZettaToolResult R; R.Content = Text; return R; }
	static FZettaToolResult Error(const FString& Text) { FZettaToolResult R; R.Content = Text; R.bIsError = true; return R; }
};

using FZettaToolDone = TFunction<void(const FZettaToolResult&)>;

/** What the approval card shows for a call before it runs. */
struct ZETTA3DAGENT_API FZettaToolPreview
{
	/** "Generate a 3D model", "Run Python", "Delete 3 actors". */
	FString Title;
	/** A few lines on what will happen. */
	FString Body;
	/** Credits it will spend, when it spends any. */
	TOptional<int32> Credits;
	/** Code to show in full (run_python). */
	FString Code;
	/** Set when the call can't run as asked (the card shows it and offers only Skip). */
	FString Error;
};

/**
 * A client tool the agent can call — one per name in the protocol. Run is called on the game thread and must
 * call Done exactly once, now or later (for tools that wait on a download, a compile or a job).
 */
class ZETTA3DAGENT_API IZettaAgentTool
{
public:
	virtual ~IZettaAgentTool() = default;
	virtual FName GetName() const = 0;
	virtual void Run(const TSharedPtr<FJsonObject>& Input, FZettaToolDone Done) = 0;
	/** The approval card's content. The default names the tool and lists its input. */
	virtual void Describe(const TSharedPtr<FJsonObject>& Input, TFunction<void(const FZettaToolPreview&)> Done);
};

/** Every client tool the plugin has, by name. */
class ZETTA3DAGENT_API FZettaAgentTools
{
public:
	static FZettaAgentTools& Get();

	void Register(const TSharedRef<IZettaAgentTool>& Tool);
	/** A tool that answers at once. */
	void Register(FName Name, TFunction<FZettaToolResult(const TSharedPtr<FJsonObject>&)> Run,
		TFunction<FZettaToolPreview(const TSharedPtr<FJsonObject>&)> Describe = nullptr);
	/** A tool that answers later. */
	void RegisterAsync(FName Name, TFunction<void(const TSharedPtr<FJsonObject>&, FZettaToolDone)> Run,
		TFunction<void(const TSharedPtr<FJsonObject>&, TFunction<void(const FZettaToolPreview&)>)> Describe = nullptr);

	bool Has(FName Name) const;
	TSharedPtr<IZettaAgentTool> Find(FName Name) const;
	/** Runs a tool; an unknown name answers "this version of the plugin does not have the tool". Never throws. */
	void Run(FName Name, const TSharedPtr<FJsonObject>& Input, FZettaToolDone Done);
	void Describe(FName Name, const TSharedPtr<FJsonObject>& Input, TFunction<void(const FZettaToolPreview&)> Done);
	TArray<FName> Names() const;

private:
	TMap<FName, TSharedRef<IZettaAgentTool>> Tools;
};

/** Registers every built-in Unreal tool (Private/Tools). Called once when the module starts. */
void ZettaRegisterAgentTools(FZettaAgentTools& Tools);

/** The editor snapshot sent with every chat request (the protocol's `editor`), ≤60,000 characters. */
namespace ZettaAgentSnapshot
{
	ZETTA3DAGENT_API TSharedRef<FJsonObject> Build();
}

/**
 * What the agent changed during one message, for the "changes" card under the reply (Undo / Keep per line).
 * Tools call these as they work; the runtime opens and closes a message's record.
 */
namespace ZettaAgentChanges
{
	/** Before a file on disk is written (absolute or project-relative): keeps its old content to restore. */
	ZETTA3DAGENT_API void WillWriteFile(const FString& Path);
	/** Before an asset is created, saved over, moved or deleted (content path). */
	ZETTA3DAGENT_API void WillChangeAsset(const FString& ContentPath, const FString& What);
	/** An asset the agent created (Undo deletes it). */
	ZETTA3DAGENT_API void CreatedAsset(const FString& ContentPath);
	/** An editor transaction the agent made in the level (Undo steps it back while it is the newest). */
	ZETTA3DAGENT_API void LevelTransaction(const FString& Title);
}

/**
 * What a tool, or another part of the plugin, can ask of the agent's conversation.
 *
 * ZettaAgentChanges::WillChangeAsset's `What` starts with a verb the changes card reads: "saved" / "changed"
 * (Undo restores the package from before the message), "deleted" (Undo puts it back), or "moved to /Game/…" /
 * "renamed to /Game/…" (Undo moves it back from there).
 */
namespace ZettaAgentSession
{
	/** The chat session the running call belongs to (0 before the server has named one). */
	ZETTA3DAGENT_API int64 CurrentSessionId();
	/** The link of a picture the server made this session (generate_image), by its library id; empty when unknown. */
	ZETTA3DAGENT_API FString ImageUrl(int64 ImageId);
	/**
	 * The file behind "attached:<name>" — a picture the user attached to a message (a file from disk or a picture of
	 * the viewport), kept under Saved/Zetta3D/Attached for a week. Empty when it is not one, or is gone.
	 */
	ZETTA3DAGENT_API FString AttachedFile(const FString& Key);
	/** A pipeline run a tool started for the agent: when it ends, the conversation hears of it (pipeline_run_finished). */
	ZETTA3DAGENT_API void FollowPipelineRun(int64 RunId);
	/**
	 * The pipelines service reports a run that finished or stopped for the user: Status is "done", "failed",
	 * "cancelled" or "awaiting"; ImportedPaths are the content paths of what was imported. Runs the agent did not
	 * start (FollowPipelineRun) are ignored.
	 */
	ZETTA3DAGENT_API void PipelineRunFinished(int64 RunId, const FString& Status, const TArray<FString>& ImportedPaths);
}
