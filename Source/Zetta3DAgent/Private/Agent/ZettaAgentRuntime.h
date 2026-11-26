// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "ZettaAgentAsk.h"
#include "ZettaAgentTools.h"
#include "ZettaHttp.h"
#include "ZettaJobs.h"


/** One line of the conversation as the page draws it. */
struct FZettaChatItem
{
	enum class EKind : uint8
	{
		User, Assistant, Event, Step, Memory, Question, Ask, Approval, Image, Error, Notice, Changes,
	};

	EKind Kind = EKind::Notice;
	FString Text;
	/** User: tags and attachments; step: progress or the error; error: its code; question: the chosen option. */
	FString Detail;
	FString ToolUseId;
	FString ToolName;
	/** Step: running | done | error | waiting | declined | stopped. Ask: answered | skipped | stopped | superseded. */
	FString Status;
	bool bClient = false;
	bool bResolved = false;

	/** Question: the option labels. */
	TArray<FString> Options;

	/** Image. */
	FString ImageUrl;
	int64 ImageId = 0;

	/** Approval: what the card shows, from the tool's preview. */
	FString ApprovalKind;
	FString ConfirmLabel;
	FString Code;
	FString PreviewError;
	TArray<FString> Reasons;
	TOptional<int32> Credits;

	/** Ask: the card's questions, and what the user has filled in so far (or answered). */
	TSharedPtr<FZettaAskRequest> Ask;
	TArray<FZettaAskAnswer> Answers;

	/** Changes: the turn whose changes the card lists. */
	FString Turn;

	/** What a redraw compares: a row is rebuilt only when this changes. */
	uint32 Signature() const;
};

using FZettaChatItemPtr = TSharedPtr<FZettaChatItem>;

/** A client tool call streamed by the server (`client_tool`). */
struct FZettaClientCall
{
	FString ToolUseId;
	FString Name;
	TSharedPtr<FJsonObject> Input;
	/** read | write | destructive | generate */
	FString Effect = TEXT("read");
	bool bConfirm = false;
	TArray<FString> Reasons;

	static FZettaClientCall From(const TSharedPtr<FJsonObject>& O);
};

/** A picture or PDF going with a message. */
struct FZettaAgentFile
{
	FString Data;
	FString MediaType;
	FString Name;
};

/**
 * Drives the Zetta Agent's conversation: sends a turn, streams the reply, runs the client tools the server hands
 * back — with the user's approval where it is needed — and resumes the turn with the results. Also brings
 * finished 3D jobs and pipeline runs the agent started back into the conversation as events.
 *
 * Everything runs on the game thread; tools answer through callbacks, now or later.
 */
class FZettaAgentRuntime
{
public:
	enum class EPhase : uint8 { Idle, Streaming, Sending, Tools };

	static FZettaAgentRuntime& Get();

	void Initialize();
	void Shutdown();

	// ─── What the page reads ───────────────────────────────────────────────

	const TArray<FZettaChatItemPtr>& Items() const { return ChatItems; }
	/** The reply still being written (its text grows with each streamed word), until the turn moves on. */
	bool IsStreaming(const FZettaChatItemPtr& Item) const { return Item.IsValid() && Item == OpenText; }
	EPhase Phase() const { return CurrentPhase; }
	bool IsBusy() const { return CurrentPhase != EPhase::Idle; }
	/** Busy, and a card is waiting on the user (an approval or a question). */
	bool IsWaitingForUser() const;
	int64 SessionId() const { return Session; }
	int32 TokensThisSession() const { return Tokens; }
	/** "Capable · Low thinking" — what the server said this turn runs on; empty until it says. */
	FText RouteText() const;
	/** Who picked the model and the thinking, and whether the plan lowered them. */
	FText RouteWhy() const;
	/** A library picture's link the server gave this session. */
	FString ImageUrl(int64 ImageId) const;

	/** The transcript or the phase changed. */
	FSimpleMulticastDelegate OnChanged;
	/** Something needs the user: the page shows itself. */
	FSimpleMulticastDelegate OnNeedsUser;

	// ─── What the page does ────────────────────────────────────────────────

	void NewChat();
	/** Sends a message (the composer has already added tags and inlined files to it). */
	void Send(const FString& Message, const TArray<FZettaAgentFile>& Images = {}, const TArray<FZettaAgentFile>& Documents = {});
	/** Answers a question card with the chosen option. */
	void Answer(const FZettaChatItemPtr& Question, const FString& Option);
	/** OK on an ask_user card, or Skip when bSkip. */
	void SubmitAnswers(const FZettaChatItemPtr& Card, bool bSkip);
	/** Allow or Decline on an approval card. */
	void Resolve(const FZettaChatItemPtr& Approval, bool bAllowed);
	/**
	 * Stops whatever the agent is doing, tells the server to end the turn, and holds everything — tool results,
	 * queued job events — until the user sends a new message. bByUser false is a quiet cancel (switching chats).
	 */
	void Stop(bool bByUser = true);
	/** Loads a stored conversation, and carries it on if the server is waiting on the editor. */
	void OpenSession(int64 Id);
	/** A line in the conversation that is not the agent's: the page telling the user something. */
	void Notice(const FString& Text);
	/** An error line; a provider_* code lets the page offer Open settings. */
	void AddError(const FString& Message, const FString& Code = FString());

	/** The ask_user tool's way in: shows the card for the call being run and answers Done on OK or Skip. */
	void AskUser(const FZettaAskRequest& Request, FZettaToolDone Done);

	/** A pipeline run a tool started for the agent; its end comes back as `pipeline_run_finished`. */
	void FollowPipelineRun(int64 RunId);
	/** The pipelines service says a run finished or needs the user (status done | failed | cancelled | awaiting). */
	void PipelineRunFinished(int64 RunId, const FString& Status, const TArray<FString>& ImportedPaths);

	/** "/unrealAgent" + Path. */
	static FString Api(const FString& Path);

private:
	void Notify() { OnChanged.Broadcast(); }
	TSharedRef<FJsonObject> Base();
	void Stream(const TSharedRef<FJsonObject>& Body);
	void OnEvent(const TSharedPtr<FJsonObject>& Event, int32 Serial);
	void OnStreamDone(const FZettaResponse& Response, int32 Serial);
	void AppendText(const FString& Text, bool bStart);
	void FinishOpenText() { OpenText.Reset(); }
	FZettaChatItemPtr Step(const FString& ToolUseId) const;
	FZettaChatItemPtr AddItem(FZettaChatItem::EKind Kind, const FString& Text = FString(), const FString& Detail = FString());
	void ShowChanges();

	// Client tools
	void RunPendingTools();
	void RunNext(int32 Batch);
	void Execute(int32 Batch, const FZettaClientCall& Call, const FZettaChatItemPtr& StepItem);
	void Record(int32 Batch, const FZettaClientCall& Call, const FZettaChatItemPtr& StepItem, const FZettaToolResult& Result);
	FString EffectOf(const FZettaClientCall& Call) const;
	bool NeedsApproval(const FZettaClientCall& Call) const;
	void CancelPending(const FString& Status);
	void SendResults();
	void ClearPending();

	// App events
	void OnJobsChanged();
	void OnJobFinished(const FZettaJobPtr& Job);
	void QueueEvent(const TSharedRef<FJsonObject>& Event);
	void DrainQueuedEvents();

	TArray<FZettaChatItemPtr> ChatItems;
	EPhase CurrentPhase = EPhase::Idle;
	int64 Session = 0;
	int32 Tokens = 0;
	bool bHalted = false;

	TSharedPtr<FZettaStream, ESPMode::ThreadSafe> CurrentStream;
	/** Bumped for every stream: events from one that was cancelled are ignored. */
	int32 StreamSerial = 0;
	TArray<FString> Awaiting;
	TArray<FZettaClientCall> StreamCalls;
	FZettaChatItemPtr OpenText;

	/** The paused calls and the results gathered for them so far. */
	TArray<FZettaClientCall> PendingCalls;
	TArray<TSharedPtr<FJsonObject>> PendingResults;
	/** Bumped when a batch starts or is cancelled: a call that finishes for a batch that is over sends nothing. */
	int32 BatchSerial = 0;
	FString RunningToolUseId;

	TMap<FZettaChatItem*, TFunction<void(bool)>> Approvals;
	TMap<FZettaChatItem*, FZettaToolDone> Questions;

	/** App events (a job finished) that arrived while a turn was running. */
	TArray<TSharedRef<FJsonObject>> QueuedEvents;
	/** Which session started each agent job and pipeline run: only that conversation hears about it. */
	TMap<int64, int64> JobSessions;
	TMap<int64, int64> RunSessions;

	TMap<int64, FString> ImageUrls;

	FString RouteLabel, RouteThinking, RouteReason, RouteSource, RouteThinkingAsked;
	/** The changes record the tools write into: one per message. */
	FString ChangeTurn;

	FDelegateHandle JobsChangedHandle, JobFinishedHandle;
};
