// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaAgentRuntime.h"

#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "Zetta3DEditorModule.h"
#include "ZettaAgentCatalog.h"
#include "ZettaAgentChangeLog.h"
#include "ZettaAuth.h"
#include "ZettaCatalog.h"
#include "ZettaJson.h"
#include "ZettaSettings.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace
{
	constexpr int32 RuntimeMaxContent = 100000;
	constexpr int32 RuntimeMaxEditorJson = 60000;

	FString RuntimeTruncate(const FString& Text)
	{
		if (Text.Len() <= RuntimeMaxContent) return Text;
		return Text.Left(RuntimeMaxContent - 80) + FString::Printf(TEXT("\n… [truncated: %d more characters]"), Text.Len() - RuntimeMaxContent + 80);
	}

	FString RuntimeShorten(const FString& Text, int32 Max)
	{
		return Text.Len() <= Max ? Text : Text.Left(Max - 1) + TEXT("…");
	}

	/** Plain-language labels for the server's own tools. */
	FString RuntimeServerToolLabel(const FString& Name)
	{
		if (Name == TEXT("remember")) return LOCTEXT("AgentUI.SrvRemember", "Remembering that").ToString();
		if (Name == TEXT("forget")) return LOCTEXT("AgentUI.SrvForget", "Forgetting that").ToString();
		if (Name == TEXT("update_project_brief")) return LOCTEXT("AgentUI.SrvBrief", "Updating the project brief").ToString();
		if (Name == TEXT("generate_image")) return LOCTEXT("AgentUI.SrvImage", "Drawing a picture").ToString();
		if (Name == TEXT("ask_options")) return LOCTEXT("AgentUI.SrvOptions", "Asking you to choose").ToString();
		return Name;
	}

	/** What a client tool step says while it waits or runs, before its own result names what it did. */
	FString RuntimeClientToolLabel(const FString& Name)
	{
		static const TMap<FString, FText> Labels = {
			{ TEXT("unreal_project_info"), LOCTEXT("AgentUI.TlProject", "Reading the project") },
			{ TEXT("level_outline"), LOCTEXT("AgentUI.TlOutline", "Reading the level") },
			{ TEXT("inspect_actor"), LOCTEXT("AgentUI.TlInspectActor", "Inspecting an actor") },
			{ TEXT("find_actors"), LOCTEXT("AgentUI.TlFindActors", "Finding actors") },
			{ TEXT("find_assets"), LOCTEXT("AgentUI.TlFindAssets", "Finding assets") },
			{ TEXT("inspect_asset"), LOCTEXT("AgentUI.TlInspectAsset", "Inspecting an asset") },
			{ TEXT("read_file"), LOCTEXT("AgentUI.TlReadFile", "Reading a file") },
			{ TEXT("search_files"), LOCTEXT("AgentUI.TlSearch", "Searching the project's files") },
			{ TEXT("output_log"), LOCTEXT("AgentUI.TlLog", "Reading the Output Log") },
			{ TEXT("capture_view"), LOCTEXT("AgentUI.TlCapture", "Looking at the level") },
			{ TEXT("get_selection"), LOCTEXT("AgentUI.TlSelection", "Checking what is selected") },
			{ TEXT("api_lookup"), LOCTEXT("AgentUI.TlApi", "Looking up the engine's API") },
			{ TEXT("write_file"), LOCTEXT("AgentUI.TlWrite", "Writing a file") },
			{ TEXT("edit_file"), LOCTEXT("AgentUI.TlEdit", "Editing a file") },
			{ TEXT("compile"), LOCTEXT("AgentUI.TlCompile", "Compiling the C++") },
			{ TEXT("spawn_actor"), LOCTEXT("AgentUI.TlSpawn", "Adding an actor") },
			{ TEXT("modify_actor"), LOCTEXT("AgentUI.TlModify", "Changing an actor") },
			{ TEXT("add_component"), LOCTEXT("AgentUI.TlAddComponent", "Adding a component") },
			{ TEXT("set_properties"), LOCTEXT("AgentUI.TlSetProps", "Setting properties") },
			{ TEXT("create_blueprint"), LOCTEXT("AgentUI.TlCreateBp", "Making a Blueprint") },
			{ TEXT("edit_blueprint"), LOCTEXT("AgentUI.TlEditBp", "Editing a Blueprint") },
			{ TEXT("create_material"), LOCTEXT("AgentUI.TlMaterial", "Making a material") },
			{ TEXT("assign_material"), LOCTEXT("AgentUI.TlAssign", "Assigning a material") },
			{ TEXT("level_operation"), LOCTEXT("AgentUI.TlLevelOp", "Working with levels") },
			{ TEXT("asset_operation"), LOCTEXT("AgentUI.TlAssetOp", "Organizing assets") },
			{ TEXT("import_file"), LOCTEXT("AgentUI.TlImport", "Importing a file") },
			{ TEXT("save_image_asset"), LOCTEXT("AgentUI.TlSaveImage", "Saving a picture as a texture") },
			{ TEXT("play_in_editor"), LOCTEXT("AgentUI.TlPie", "Play In Editor") },
			{ TEXT("console_command"), LOCTEXT("AgentUI.TlConsole", "Running a console command") },
			{ TEXT("viewport"), LOCTEXT("AgentUI.TlViewport", "Moving the viewport") },
			{ TEXT("undo"), LOCTEXT("AgentUI.TlUndo", "Undoing its last step") },
			{ TEXT("delete_actors"), LOCTEXT("AgentUI.TlDeleteActors", "Deleting actors") },
			{ TEXT("remove_component"), LOCTEXT("AgentUI.TlRemoveComponent", "Removing a component") },
			{ TEXT("delete_asset"), LOCTEXT("AgentUI.TlDeleteAsset", "Deleting an asset") },
			{ TEXT("run_python"), LOCTEXT("AgentUI.TlPython", "Running Python") },
			{ TEXT("generate_3d_model"), LOCTEXT("AgentUI.TlGenerate", "Setting up a 3D model") },
			{ TEXT("process_model"), LOCTEXT("AgentUI.TlProcess", "Setting up a model tool") },
			{ TEXT("import_generation"), LOCTEXT("AgentUI.TlImportGen", "Importing a model") },
			{ TEXT("list_generations"), LOCTEXT("AgentUI.TlListGen", "Looking through your models") },
			{ TEXT("list_images"), LOCTEXT("AgentUI.TlListImages", "Looking through your pictures") },
			{ TEXT("edit_image"), LOCTEXT("AgentUI.TlEditImage", "Editing a picture") },
			{ TEXT("compose_images"), LOCTEXT("AgentUI.TlCompose", "Composing pictures") },
			{ TEXT("generate_image_with_references"), LOCTEXT("AgentUI.TlDrawRefs", "Drawing a picture") },
			{ TEXT("list_pipelines"), LOCTEXT("AgentUI.TlListPipelines", "Looking through your pipelines") },
			{ TEXT("get_pipeline"), LOCTEXT("AgentUI.TlGetPipeline", "Reading a pipeline") },
			{ TEXT("build_pipeline"), LOCTEXT("AgentUI.TlBuildPipeline", "Building a pipeline") },
			{ TEXT("edit_pipeline"), LOCTEXT("AgentUI.TlEditPipeline", "Editing a pipeline") },
			{ TEXT("run_pipeline"), LOCTEXT("AgentUI.TlRunPipeline", "Running a pipeline") },
			{ TEXT("pipeline_run_status"), LOCTEXT("AgentUI.TlRunStatus", "Checking a pipeline run") },
			{ TEXT("manage_pipeline_run"), LOCTEXT("AgentUI.TlManageRun", "Managing a pipeline run") },
			{ TEXT("import_pipeline_results"), LOCTEXT("AgentUI.TlImportRun", "Importing what a pipeline made") },
			{ TEXT("ask_user"), LOCTEXT("AgentUI.TlAsk", "Asking you") },
		};
		if (const FText* Label = Labels.Find(Name)) return Label->ToString();
		FString Spaced = Name.Replace(TEXT("_"), TEXT(" "));
		if (Spaced.Len() > 0) Spaced[0] = FChar::ToUpper(Spaced[0]);
		return Spaced.IsEmpty() ? LOCTEXT("AgentUI.TlWorking", "Working").ToString() : Spaced;
	}

	FString RuntimeStepLabel(const FZettaClientCall& Call)
	{
		if (Call.Name == TEXT("ask_user")) return ZettaAgentAsk::Describe(Call.Input);
		return RuntimeClientToolLabel(Call.Name);
	}

	/** Takes the block between Open and Close out of Text, handing each one's body to Each. */
	FString RuntimeStripBlocks(const FString& Text, const FString& Open, const FString& Close, TFunctionRef<void(const FString& Head, const FString& Body)> Each)
	{
		FString Out = Text;
		int32 Start = Out.Find(Open);
		while (Start != INDEX_NONE)
		{
			const int32 HeadEnd = Out.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
			const int32 End = Out.Find(Close, ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
			if (HeadEnd == INDEX_NONE || End == INDEX_NONE) break;
			Each(Out.Mid(Start, HeadEnd - Start + 1), Out.Mid(HeadEnd + 1, End - HeadEnd - 1));
			Out = Out.Left(Start) + Out.Mid(End + Close.Len());
			Start = Out.Find(Open);
		}
		return Out;
	}

	/** A user message as the bubble shows it: the typed words, and a summary of tags and inlined files. */
	void RuntimeForDisplay(const FString& Message, FString& OutText, FString& OutExtras)
	{
		TArray<FString> Extras;
		FString Text = RuntimeStripBlocks(Message, TEXT("<mentions>"), TEXT("</mentions>"), [&Extras](const FString&, const FString& Body)
		{
			TArray<FString> Lines;
			Body.ParseIntoArrayLines(Lines);
			for (const FString& Line : Lines)
			{
				// - actor "Lighting/Sun" (…) → @Sun
				int32 Q1 = INDEX_NONE;
				if (!Line.FindChar(TEXT('"'), Q1)) continue;
				const int32 Q2 = Line.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Q1 + 1);
				if (Q2 == INDEX_NONE) continue;
				FString Path = Line.Mid(Q1 + 1, Q2 - Q1 - 1);
				int32 Slash = INDEX_NONE;
				if (Path.FindLastChar(TEXT('/'), Slash)) Path = Path.Mid(Slash + 1);
				Extras.Add(TEXT("@") + Path);
			}
		});
		Text = RuntimeStripBlocks(Text, TEXT("<attached_file"), TEXT("</attached_file>"), [&Extras](const FString& Head, const FString&)
		{
			int32 Q1 = INDEX_NONE;
			if (!Head.FindChar(TEXT('"'), Q1)) return;
			const int32 Q2 = Head.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Q1 + 1);
			if (Q2 != INDEX_NONE) Extras.Add(Head.Mid(Q1 + 1, Q2 - Q1 - 1));
		});
		const int32 Note = Text.Find(TEXT("(Attached: "), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (Note != INDEX_NONE && Text.TrimEnd().EndsWith(TEXT(")"))) Text = Text.Left(Note);
		OutText = Text.TrimStartAndEnd();
		OutExtras = FString::Join(Extras, TEXT(" · "));
	}

	TSharedRef<FJsonValue> RuntimeFilesJson(const TArray<FZettaAgentFile>& Files, bool bNamed)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (int32 i = 0; i < Files.Num() && i < 4; ++i)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("data"), Files[i].Data);
			O->SetStringField(TEXT("mediaType"), Files[i].MediaType);
			if (bNamed && !Files[i].Name.IsEmpty()) O->SetStringField(TEXT("name"), Files[i].Name.Left(200));
			Out.Add(MakeShared<FJsonValueObject>(O));
		}
		return MakeShared<FJsonValueArray>(Out);
	}
}

// ─── Items ────────────────────────────────────────────────────────────────────

uint32 FZettaChatItem::Signature() const
{
	uint32 Hash = GetTypeHash((uint8)Kind);
	Hash = HashCombine(Hash, GetTypeHash(Text));
	Hash = HashCombine(Hash, GetTypeHash(Detail));
	Hash = HashCombine(Hash, GetTypeHash(Status));
	Hash = HashCombine(Hash, GetTypeHash(bResolved));
	Hash = HashCombine(Hash, GetTypeHash(Options.Num()));
	Hash = HashCombine(Hash, GetTypeHash(ImageUrl));
	return Hash;
}

FZettaClientCall FZettaClientCall::From(const TSharedPtr<FJsonObject>& O)
{
	FZettaClientCall Call;
	Call.ToolUseId = ZettaJson::Str(O, TEXT("toolUseId"));
	Call.Name = ZettaJson::Str(O, TEXT("name"));
	Call.Input = ZettaJson::Obj(O, TEXT("input"));
	if (!Call.Input.IsValid()) Call.Input = MakeShared<FJsonObject>();
	Call.Effect = ZettaJson::Str(O, TEXT("effect"), TEXT("read"));
	const TSharedPtr<FJsonObject> Review = ZettaJson::Obj(O, TEXT("review"));
	Call.bConfirm = ZettaJson::Bool(Review, TEXT("confirm"));
	Call.Reasons = ZettaJson::Strings(Review, TEXT("reasons"));
	return Call;
}

// ─── Runtime ──────────────────────────────────────────────────────────────────

FZettaAgentRuntime& FZettaAgentRuntime::Get()
{
	static FZettaAgentRuntime Instance;
	return Instance;
}

FString FZettaAgentRuntime::Api(const FString& Path)
{
	return TEXT("/unrealAgent") + Path;
}

void FZettaAgentRuntime::Initialize()
{
	JobsChangedHandle = FZettaJobs::Get().OnChanged.AddRaw(this, &FZettaAgentRuntime::OnJobsChanged);
	JobFinishedHandle = FZettaJobs::Get().OnFinished.AddRaw(this, &FZettaAgentRuntime::OnJobFinished);
}

void FZettaAgentRuntime::Shutdown()
{
	FZettaJobs::Get().OnChanged.Remove(JobsChangedHandle);
	FZettaJobs::Get().OnFinished.Remove(JobFinishedHandle);
	++StreamSerial;
	if (CurrentStream.IsValid()) CurrentStream->Cancel();
	CurrentStream.Reset();
	Approvals.Empty();
	Questions.Empty();
}

bool FZettaAgentRuntime::IsWaitingForUser() const
{
	if (CurrentPhase != EPhase::Tools) return false;
	for (const FZettaChatItemPtr& Item : ChatItems)
	{
		if ((Item->Kind == FZettaChatItem::EKind::Approval || Item->Kind == FZettaChatItem::EKind::Ask) && !Item->bResolved) return true;
	}
	return false;
}

FText FZettaAgentRuntime::RouteText() const
{
	if (RouteLabel.IsEmpty()) return FText();
	if (RouteThinking.IsEmpty() || RouteThinking == TEXT("auto")) return FText::FromString(RouteLabel);
	return FText::Format(LOCTEXT("AgentUI.RouteFmt", "{0} · {1}"), FText::FromString(RouteLabel), FZettaAgentCatalog::ThinkingLabel(RouteThinking));
}

FText FZettaAgentRuntime::RouteWhy() const
{
	FString Reason = RouteReason;
	const bool bCapped = Reason.Contains(TEXT("capped by the plan"));
	Reason.ReplaceInline(TEXT(" (capped by the plan)"), TEXT(""));
	const bool bAutoModel = RouteSource != TEXT("manual");
	const bool bAutoThinking = RouteThinkingAsked.IsEmpty() || RouteThinkingAsked == TEXT("auto");
	const FString Why = bAutoModel && !Reason.IsEmpty() ? TEXT(" for ") + Reason : FString();
	FString Text = bAutoModel && bAutoThinking ? FString::Printf(TEXT("Auto picked the model and the thinking%s."), *Why)
		: bAutoModel ? FString::Printf(TEXT("Auto picked the model%s; you picked the thinking."), *Why)
		: bAutoThinking ? FString(TEXT("You picked the model; Auto picked the thinking."))
		: FString(TEXT("You picked the model and the thinking."));
	if (bCapped) Text += TEXT(" Lowered to what your plan includes.");
	return FText::FromString(Text);
}

FString FZettaAgentRuntime::ImageUrl(int64 Id) const
{
	const FString* Url = ImageUrls.Find(Id);
	return Url ? *Url : FString();
}

FZettaChatItemPtr FZettaAgentRuntime::AddItem(FZettaChatItem::EKind Kind, const FString& Text, const FString& Detail)
{
	FZettaChatItemPtr Item = MakeShared<FZettaChatItem>();
	Item->Kind = Kind;
	Item->Text = Text;
	Item->Detail = Detail;
	ChatItems.Add(Item);
	return Item;
}

FZettaChatItemPtr FZettaAgentRuntime::Step(const FString& ToolUseId) const
{
	if (ToolUseId.IsEmpty()) return nullptr;
	for (int32 i = ChatItems.Num() - 1; i >= 0; --i)
	{
		if (ChatItems[i]->Kind == FZettaChatItem::EKind::Step && ChatItems[i]->ToolUseId == ToolUseId) return ChatItems[i];
	}
	return nullptr;
}

void FZettaAgentRuntime::Notice(const FString& Text)
{
	AddItem(FZettaChatItem::EKind::Notice, Text);
	Notify();
}

void FZettaAgentRuntime::AddError(const FString& Message, const FString& Code)
{
	FinishOpenText();
	AddItem(FZettaChatItem::EKind::Error, Message, Code);
	Notify();
}

// ─── Public API ───────────────────────────────────────────────────────────────

void FZettaAgentRuntime::NewChat()
{
	Stop(false);
	ChatItems.Reset();
	Session = 0;
	Tokens = 0;
	bHalted = false;
	QueuedEvents.Reset();
	ImageUrls.Reset();
	RouteLabel.Reset();
	RouteThinking.Reset();
	RouteReason.Reset();
	RouteSource.Reset();
	ChangeTurn.Reset();
	Notify();
}

void FZettaAgentRuntime::Send(const FString& InMessage, const TArray<FZettaAgentFile>& Images, const TArray<FZettaAgentFile>& Documents)
{
	const FString Message = InMessage.TrimStartAndEnd().Left(16000);
	if (Message.IsEmpty() || !FZettaAuth::Get().IsSignedIn()) return;

	// Typing while the agent waits on tools or on the user: the server closes the pending calls itself.
	if (CurrentPhase == EPhase::Tools) CancelPending(TEXT("superseded"));
	if (CurrentPhase == EPhase::Streaming || CurrentPhase == EPhase::Sending) return;
	bHalted = false;

	// The bubble shows what the user typed; the blocks the agent reads (tags, inlined files) become a summary line.
	FString Shown, Extras;
	RuntimeForDisplay(Message, Shown, Extras);
	TArray<FString> Summary;
	if (!Extras.IsEmpty()) Summary.Add(Extras);
	if (Images.Num() > 0) Summary.Add(FString::Printf(TEXT("%d image%s attached"), Images.Num(), Images.Num() > 1 ? TEXT("s") : TEXT("")));
	if (Documents.Num() > 0) Summary.Add(FString::Printf(TEXT("%d PDF%s attached"), Documents.Num(), Documents.Num() > 1 ? TEXT("s") : TEXT("")));
	AddItem(FZettaChatItem::EKind::User, Shown, FString::Join(Summary, TEXT(" · ")));

	ClearPending();
	ChangeTurn = FZettaAgentChangeLog::Get().BeginTurn();
	TSharedRef<FJsonObject> Body = Base();
	Body->SetStringField(TEXT("message"), Message);
	if (Images.Num() > 0) Body->SetField(TEXT("images"), RuntimeFilesJson(Images, false));
	if (Documents.Num() > 0) Body->SetField(TEXT("documents"), RuntimeFilesJson(Documents, true));
	Stream(Body);
}

void FZettaAgentRuntime::Answer(const FZettaChatItemPtr& Question, const FString& Option)
{
	if (!Question.IsValid() || Question->bResolved) return;
	Question->bResolved = true;
	Question->Detail = Option;
	Send(Option);
}

void FZettaAgentRuntime::SubmitAnswers(const FZettaChatItemPtr& Card, bool bSkip)
{
	if (!Card.IsValid() || Card->bResolved) return;
	Card->bResolved = true;
	Card->Status = bSkip ? TEXT("skipped") : TEXT("answered");
	FZettaToolDone Done;
	if (FZettaToolDone* Found = Questions.Find(Card.Get()))
	{
		Done = MoveTemp(*Found);
		Questions.Remove(Card.Get());
	}
	Notify();
	if (Done)
	{
		Done(FZettaToolResult::Ok(bSkip || !Card->Ask.IsValid() ? ZettaAgentAsk::Skipped() : ZettaAgentAsk::Format(*Card->Ask, Card->Answers)));
	}
}

void FZettaAgentRuntime::Resolve(const FZettaChatItemPtr& Approval, bool bAllowed)
{
	if (!Approval.IsValid() || Approval->bResolved) return;
	Approval->bResolved = true;
	Approval->Status = bAllowed ? TEXT("done") : TEXT("declined");
	TFunction<void(bool)> Then;
	if (TFunction<void(bool)>* Found = Approvals.Find(Approval.Get()))
	{
		Then = MoveTemp(*Found);
		Approvals.Remove(Approval.Get());
	}
	Notify();
	if (Then) Then(bAllowed);
}

void FZettaAgentRuntime::Stop(bool bByUser)
{
	const bool bWasBusy = IsBusy();
	const int64 StoppedSession = Session;

	// Settle the state before anything that was waiting runs again: a batch released from an approval card must
	// find the turn halted and idle, not carry on and send a "declined" result.
	if (bWasBusy && bByUser)
	{
		bHalted = true;
		QueuedEvents.Reset();
	}
	// The server ends the model's turn and closes any calls it is waiting on, so the next message starts clean.
	if (bWasBusy && StoppedSession > 0)
	{
		FZettaHttp::Post(Api(FString::Printf(TEXT("/sessions/%lld/stop"), StoppedSession)), nullptr, [](const FZettaResponse&)
		{
			// An older server without the route, or nothing running: the next message closes pending calls anyway.
		});
	}
	++StreamSerial;
	if (CurrentStream.IsValid()) CurrentStream->Cancel();
	CurrentStream.Reset();
	CancelPending(bByUser ? TEXT("stopped") : TEXT("declined"));
	CurrentPhase = EPhase::Idle;
	ClearPending();
	FZettaAgentChangeLog::Get().bRecording = false;

	for (const FZettaChatItemPtr& Item : ChatItems)
	{
		if (Item->Kind == FZettaChatItem::EKind::Step && (Item->Status == TEXT("running") || Item->Status == TEXT("waiting"))) Item->Status = TEXT("stopped");
	}
	if (bWasBusy && bByUser)
	{
		int32 LastUser = INDEX_NONE;
		for (int32 i = ChatItems.Num() - 1; i >= 0; --i)
		{
			if (ChatItems[i]->Kind == FZettaChatItem::EKind::User) { LastUser = i; break; }
		}
		bool bReplied = false;
		for (int32 i = LastUser + 1; i < ChatItems.Num(); ++i)
		{
			if (ChatItems[i]->Kind == FZettaChatItem::EKind::Assistant && !ChatItems[i]->Text.TrimStartAndEnd().IsEmpty()) bReplied = true;
		}
		AddItem(FZettaChatItem::EKind::Notice, bReplied ? LOCTEXT("AgentUI.Stopped", "Stopped.").ToString() : LOCTEXT("AgentUI.StoppedEarly", "Stopped before it replied.").ToString());
	}
	if (bByUser) ShowChanges();
	FinishOpenText();
	Notify();
}

void FZettaAgentRuntime::OpenSession(int64 Id)
{
	Stop(false);
	FZettaHttp::Get(Api(FString::Printf(TEXT("/sessions/%lld"), Id)), [this, Id](const FZettaResponse& R)
	{
		if (!R.bOk)
		{
			AddError(ZettaCatalog::Explain(R));
			return;
		}
		NewChat();
		Session = Id;
		const TSharedPtr<FJsonObject> Body = R.Object();
		for (const TSharedPtr<FJsonObject>& Entry : ZettaJson::Objects(Body, TEXT("transcript")))
		{
			const FString Role = ZettaJson::Str(Entry, TEXT("role"));
			if (Role == TEXT("tool"))
			{
				FZettaChatItemPtr StepItem = AddItem(FZettaChatItem::EKind::Step);
				StepItem->ToolName = ZettaJson::Str(Entry, TEXT("name"));
				StepItem->ToolUseId = ZettaJson::Str(Entry, TEXT("toolUseId"));
				StepItem->bClient = ZettaJson::Str(Entry, TEXT("side")) == TEXT("client");
				StepItem->Status = ZettaJson::Bool(Entry, TEXT("isError")) ? TEXT("error") : TEXT("done");
				const FString Summary = ZettaJson::Str(Entry, TEXT("summary"));
				StepItem->Text = !Summary.IsEmpty() ? Summary : StepItem->bClient ? RuntimeClientToolLabel(StepItem->ToolName) : RuntimeServerToolLabel(StepItem->ToolName);
				continue;
			}
			const FString Text = ZettaJson::Str(Entry, TEXT("text"));
			if (Role == TEXT("user"))
			{
				FString Shown, Extras;
				RuntimeForDisplay(Text, Shown, Extras);
				AddItem(FZettaChatItem::EKind::User, Shown, Extras);
			}
			else
			{
				AddItem(Role == TEXT("assistant") ? FZettaChatItem::EKind::Assistant : FZettaChatItem::EKind::Event, Text);
			}
		}
		TArray<FZettaClientCall> Waiting;
		for (const TSharedPtr<FJsonObject>& Call : ZettaJson::Objects(Body, TEXT("awaiting"))) Waiting.Add(FZettaClientCall::From(Call));
		if (Waiting.Num() > 0)
		{
			AddItem(FZettaChatItem::EKind::Notice, LOCTEXT("AgentUI.WasWaiting", "This conversation was waiting on the editor. Carrying on…").ToString());
			ChangeTurn = FZettaAgentChangeLog::Get().BeginTurn();
			PendingCalls = MoveTemp(Waiting);
			PendingResults.Reset();
			RunPendingTools();
			return;
		}
		Notify();
	});
}

// ─── Streaming ────────────────────────────────────────────────────────────────

TSharedRef<FJsonObject> FZettaAgentRuntime::Base()
{
	const UZettaUserSettings* Prefs = UZettaUserSettings::Get();
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("projectId"), UZettaProjectSettings::Get()->EnsureProjectId());
	Body->SetStringField(TEXT("projectName"), FString(FApp::GetProjectName()).Left(120));
	Body->SetStringField(TEXT("tier"), Prefs->AgentTier.IsEmpty() ? TEXT("auto") : Prefs->AgentTier);
	Body->SetBoolField(TEXT("adaptive"), true);
	Body->SetStringField(TEXT("mode"), Prefs->AgentMode == TEXT("ask") ? TEXT("ask") : TEXT("agent"));
	Body->SetStringField(TEXT("approval"), Prefs->AgentApproval == TEXT("auto") ? TEXT("auto") : TEXT("confirm_destructive"));
	// Model and thinking are each Auto on their own: "auto" here lets the server decide the thinking.
	Body->SetStringField(TEXT("thinking"), Prefs->AgentThinking.IsEmpty() ? TEXT("auto") : Prefs->AgentThinking);

	// On the user's own key: the provider and model picked under the message box (the key itself goes in a header).
	FZettaAgentCatalog& Catalog = FZettaAgentCatalog::Get();
	if (Catalog.OwnKeys())
	{
		const FZettaAgentProvider* Provider = Catalog.ActiveProvider();
		const FZettaAgentModel* Model = Catalog.ActiveModel();
		if (Provider && Model)
		{
			Body->SetStringField(TEXT("provider"), Provider->Key);
			Body->SetStringField(TEXT("model"), Model->Key);
			Body->SetStringField(TEXT("thinking"), FZettaAgentCatalog::ThinkingFor(Model, Prefs->AgentThinking));
		}
	}
	if (Session > 0) Body->SetNumberField(TEXT("sessionId"), (double)Session);

	const TSharedRef<FJsonObject> Snapshot = ZettaAgentSnapshot::Build();
	if (ZettaJson::Write(Snapshot).Len() <= RuntimeMaxEditorJson)
	{
		Body->SetObjectField(TEXT("editor"), Snapshot);
	}
	else
	{
		// Too much to send whole: the outline is the part that grows with the level.
		TSharedRef<FJsonObject> Trimmed = ZettaJson::Clone(Snapshot);
		Trimmed->RemoveField(TEXT("outline"));
		if (ZettaJson::Write(Trimmed).Len() <= RuntimeMaxEditorJson) Body->SetObjectField(TEXT("editor"), Trimmed);
	}
	return Body;
}

void FZettaAgentRuntime::Stream(const TSharedRef<FJsonObject>& Body)
{
	FZettaAgentCatalog& Catalog = FZettaAgentCatalog::Get();
	const FString Provider = ZettaJson::Str(Body, TEXT("provider"));
	// Own keys chosen: never fall back to the Zetta account without saying so.
	if (Catalog.OwnKeys() && Provider.IsEmpty())
	{
		CurrentPhase = EPhase::Idle;
		AddError(LOCTEXT("AgentUI.NoKeySaved", "The agent is set to run on your own API keys, and none is saved. Add one in the agent's settings, or switch back to your Zetta account there.").ToString(),
			TEXT("provider_key_missing"));
		return;
	}
	TMap<FString, FString> Headers;
	if (!Provider.IsEmpty())
	{
		const FString Key = Catalog.GetKey(Provider);
		if (Key.IsEmpty())
		{
			CurrentPhase = EPhase::Idle;
			const FZettaAgentProvider* Info = Catalog.Provider(Provider);
			AddError(FText::Format(LOCTEXT("AgentUI.NoKeyFor", "No API key is saved for {0}. Add it in the agent's settings."), FText::FromString(Info ? Info->Label : Provider)).ToString(),
				TEXT("provider_key_missing"));
			return;
		}
		Headers.Add(TEXT("X-Zetta-Provider-Key"), Key);
	}

	const bool bResume = Body->HasField(TEXT("toolResults"));
	CurrentPhase = bResume ? EPhase::Sending : EPhase::Streaming;
	// A new message is routed afresh: the last turn's model is not shown while this one's is decided.
	if (!bResume)
	{
		RouteLabel.Reset();
		RouteThinking.Reset();
		RouteReason.Reset();
		RouteSource.Reset();
		RouteThinkingAsked = ZettaJson::Str(Body, TEXT("thinking"));
	}
	Awaiting.Reset();
	StreamCalls.Reset();
	const int32 Serial = ++StreamSerial;
	Notify();
	CurrentStream = FZettaHttp::Stream(Api(TEXT("/chat")), Body,
		[this, Serial](const TSharedPtr<FJsonObject>& Event) { OnEvent(Event, Serial); },
		[this, Serial](const FZettaResponse& R) { OnStreamDone(R, Serial); },
		Headers);
}

void FZettaAgentRuntime::OnEvent(const TSharedPtr<FJsonObject>& Event, int32 Serial)
{
	if (Serial != StreamSerial || !Event.IsValid()) return;
	const FString Type = ZettaJson::Str(Event, TEXT("type"));

	if (Type == TEXT("session"))
	{
		Session = ZettaJson::Int(Event, TEXT("sessionId"), Session);
	}
	else if (Type == TEXT("route"))
	{
		// The model and thinking the server chose. Shown beside the working indicator, not as a line in the chat.
		const TSharedPtr<FJsonObject> Route = ZettaJson::Obj(Event, TEXT("route"));
		RouteLabel = ZettaJson::Str(Route, TEXT("label"));
		RouteThinking = ZettaJson::Str(Route, TEXT("thinking"));
		RouteReason = ZettaJson::Str(Route, TEXT("reason"));
		RouteSource = ZettaJson::Str(Route, TEXT("source"));
	}
	else if (Type == TEXT("text"))
	{
		AppendText(ZettaJson::Str(Event, TEXT("text")), ZettaJson::Bool(Event, TEXT("start")));
	}
	else if (Type == TEXT("tool"))
	{
		FinishOpenText();
		const bool bClient = ZettaJson::Str(Event, TEXT("side")) == TEXT("client");
		FZettaChatItemPtr Item = AddItem(FZettaChatItem::EKind::Step);
		Item->ToolName = ZettaJson::Str(Event, TEXT("name"));
		Item->ToolUseId = ZettaJson::Str(Event, TEXT("toolUseId"));
		Item->bClient = bClient;
		Item->Status = bClient ? TEXT("waiting") : TEXT("running");
		Item->Text = bClient ? RuntimeClientToolLabel(Item->ToolName) : RuntimeServerToolLabel(Item->ToolName);
	}
	else if (Type == TEXT("tool_result"))
	{
		const bool bError = ZettaJson::Bool(Event, TEXT("isError"));
		if (FZettaChatItemPtr Item = Step(ZettaJson::Str(Event, TEXT("toolUseId"))))
		{
			Item->Status = bError ? TEXT("error") : TEXT("done");
			if (bError) Item->Detail = RuntimeShorten(ZettaJson::Str(Event, TEXT("content")), 240);
		}
		const FString Url = ZettaJson::Str(Event, TEXT("imageUrl"));
		if (!Url.IsEmpty())
		{
			const int64 Id = ZettaJson::Int(Event, TEXT("imageId"));
			if (Id > 0) ImageUrls.Add(Id, Url);
			FZettaChatItemPtr Image = AddItem(FZettaChatItem::EKind::Image, Id > 0 ? FString::Printf(TEXT("Image #%lld"), Id) : TEXT("Image"));
			Image->ImageUrl = Url;
			Image->ImageId = Id;
		}
	}
	else if (Type == TEXT("client_tool"))
	{
		const FZettaClientCall Call = FZettaClientCall::From(Event);
		StreamCalls.Add(Call);
		FZettaChatItemPtr Item = Step(Call.ToolUseId);
		if (!Item.IsValid())
		{
			Item = AddItem(FZettaChatItem::EKind::Step);
			Item->ToolName = Call.Name;
			Item->ToolUseId = Call.ToolUseId;
			Item->bClient = true;
		}
		Item->Status = TEXT("waiting");
		Item->Text = RuntimeStepLabel(Call);
	}
	else if (Type == TEXT("memory"))
	{
		const TSharedPtr<FJsonObject> Memory = ZettaJson::Obj(Event, TEXT("memory"));
		const bool bForgot = ZettaJson::Str(Memory, TEXT("action")) == TEXT("forgotten");
		const bool bGlobal = ZettaJson::Str(Memory, TEXT("scope")) == TEXT("global");
		AddItem(FZettaChatItem::EKind::Memory, ZettaJson::Str(Memory, TEXT("text")),
			bForgot ? LOCTEXT("AgentUI.Forgot", "Forgot").ToString()
			: bGlobal ? LOCTEXT("AgentUI.RememberedAll", "Remembered (all projects)").ToString()
			: LOCTEXT("AgentUI.RememberedProject", "Remembered (this project)").ToString());
	}
	else if (Type == TEXT("brief"))
	{
		AddItem(FZettaChatItem::EKind::Event, LOCTEXT("AgentUI.BriefUpdated", "Updated the project brief.").ToString());
	}
	else if (Type == TEXT("question"))
	{
		const TSharedPtr<FJsonObject> Question = ZettaJson::Obj(Event, TEXT("question"));
		FZettaChatItemPtr Item = AddItem(FZettaChatItem::EKind::Question, ZettaJson::Str(Question, TEXT("question")));
		for (const TSharedPtr<FJsonObject>& Option : ZettaJson::Objects(Question, TEXT("options")))
		{
			const FString Label = ZettaJson::Str(Option, TEXT("label"));
			if (!Label.IsEmpty()) Item->Options.Add(Label);
		}
		OnNeedsUser.Broadcast();
	}
	else if (Type == TEXT("done"))
	{
		Tokens += (int32)ZettaJson::Int(Event, TEXT("tokens"));
		if (ZettaJson::Bool(Event, TEXT("stopped"))) bHalted = true;
		Awaiting.Append(ZettaJson::Strings(Event, TEXT("awaiting")));
	}
	else if (Type == TEXT("error"))
	{
		AddError(ZettaJson::Str(Event, TEXT("message"), LOCTEXT("AgentUI.CouldNotFinish", "The agent could not finish.").ToString()), ZettaJson::Str(Event, TEXT("code")));
	}
	Notify();
}

void FZettaAgentRuntime::OnStreamDone(const FZettaResponse& R, int32 Serial)
{
	if (Serial != StreamSerial) return;
	CurrentStream.Reset();
	FinishOpenText();
	if (R.bCancelled) return;
	if (!R.bOk)
	{
		// A 404 on the chat route itself means the server this editor talks to has no Unreal agent yet (it is switched on
		// per server), not that something was missing — say so rather than "Not found."
		const FString Message = R.IsUnauthorized() ? LOCTEXT("AgentUI.SignInAgain", "Sign in to Zetta 3D to carry on.").ToString()
			: R.Status == 404 && R.Code.IsEmpty() ? LOCTEXT("AgentUI.AgentOff", "The Zetta Agent for Unreal isn't available on this server yet. The 3D Studio, My Generations and Pipelines work as usual.").ToString()
			: ZettaCatalog::Explain(R);
		AddError(Message, R.Code);
	}
	if (CurrentPhase == EPhase::Streaming || CurrentPhase == EPhase::Sending) CurrentPhase = EPhase::Idle;
	Notify();

	if (Awaiting.Num() > 0 && !bHalted)
	{
		// Keep the server's order; a call we somehow did not see still gets answered (as not run).
		PendingCalls.Reset();
		for (const FString& Id : Awaiting)
		{
			const FZettaClientCall* Seen = StreamCalls.FindByPredicate([&Id](const FZettaClientCall& C) { return C.ToolUseId == Id; });
			if (Seen)
			{
				PendingCalls.Add(*Seen);
			}
			else
			{
				FZettaClientCall Unknown;
				Unknown.ToolUseId = Id;
				Unknown.Name = TEXT("unknown");
				Unknown.Input = MakeShared<FJsonObject>();
				PendingCalls.Add(Unknown);
			}
		}
		PendingResults.Reset();
		RunPendingTools();
		return;
	}
	ShowChanges();
	DrainQueuedEvents();
}

void FZettaAgentRuntime::AppendText(const FString& Text, bool bStart)
{
	if (!OpenText.IsValid() || !ChatItems.Contains(OpenText))
	{
		OpenText = AddItem(FZettaChatItem::EKind::Assistant);
	}
	else if (bStart && !OpenText->Text.IsEmpty())
	{
		OpenText->Text += TEXT("\n\n");
	}
	OpenText->Text += Text;
}

void FZettaAgentRuntime::ShowChanges()
{
	if (ChangeTurn.IsEmpty() || !FZettaAgentChangeLog::Get().Has(ChangeTurn)) return;
	// Once per message, and moved to the end if the turn carried on after an earlier card.
	const int32 Existing = ChatItems.IndexOfByPredicate([this](const FZettaChatItemPtr& I)
	{
		return I->Kind == FZettaChatItem::EKind::Changes && I->Turn == ChangeTurn;
	});
	if (Existing != INDEX_NONE)
	{
		if (Existing == ChatItems.Num() - 1) return;
		ChatItems.RemoveAt(Existing);
	}
	FZettaChatItemPtr Card = AddItem(FZettaChatItem::EKind::Changes);
	Card->Turn = ChangeTurn;
	Notify();
}

// ─── Client tools ─────────────────────────────────────────────────────────────

void FZettaAgentRuntime::RunPendingTools()
{
	CurrentPhase = EPhase::Tools;
	const int32 Batch = ++BatchSerial;
	Notify();
	RunNext(Batch);
}

FString FZettaAgentRuntime::EffectOf(const FZettaClientCall& Call) const
{
	// These pipeline actions spend credits, or throw a run away: they are asked like a generate or a delete.
	if (Call.Name == TEXT("manage_pipeline_run"))
	{
		const FString Action = ZettaJson::Str(Call.Input, TEXT("action"));
		if (Action == TEXT("run_waiting") || Action == TEXT("run_step") || Action == TEXT("retry_step")) return TEXT("generate");
		if (Action == TEXT("cancel")) return TEXT("destructive");
	}
	return Call.Effect;
}

bool FZettaAgentRuntime::NeedsApproval(const FZettaClientCall& Call) const
{
	// Full autonomy: nothing waits on the user — not even credits or a call the pre-flight check flagged. The
	// chat notes each one it approved for itself (as the Unity plugin does; the agent's prompt says the same).
	if (UZettaUserSettings::Get()->AgentApproval == TEXT("auto")) return false;
	// Ask first: spending credits and deleting (run_python included) show a card, and so does anything the
	// server's pre-flight check flagged; other reads and writes run.
	const FString Effect = EffectOf(Call);
	// A manage_pipeline_run that spends credits or cancels is a write by effect, but asks like generate / destructive.
	if (Call.Name == TEXT("manage_pipeline_run"))
	{
		const FString Action = ZettaJson::Str(Call.Input, TEXT("action"));
		if (Action == TEXT("run_waiting") || Action == TEXT("run_step") || Action == TEXT("retry_step") || Action == TEXT("cancel")) return true;
	}
	return Effect == TEXT("generate") || Effect == TEXT("destructive") || Call.bConfirm;
}

void FZettaAgentRuntime::RunNext(int32 Batch)
{
	if (Batch != BatchSerial || CurrentPhase != EPhase::Tools) return;
	const int32 Index = PendingResults.Num();
	if (Index >= PendingCalls.Num())
	{
		if (!bHalted) SendResults();
		return;
	}

	const FZettaClientCall Call = PendingCalls[Index];
	FZettaChatItemPtr StepItem = Step(Call.ToolUseId);
	if (!StepItem.IsValid())
	{
		StepItem = AddItem(FZettaChatItem::EKind::Step, RuntimeStepLabel(Call));
		StepItem->ToolName = Call.Name;
		StepItem->ToolUseId = Call.ToolUseId;
		StepItem->bClient = true;
	}

	// Ask mode changes nothing, whatever the server sends.
	if (UZettaUserSettings::Get()->AgentMode == TEXT("ask") && EffectOf(Call) != TEXT("read"))
	{
		Record(Batch, Call, StepItem, FZettaToolResult::Error(TEXT("Ask mode is on: the user asked for answers only, so nothing in the project may change. Explain what you would do instead.")));
		return;
	}

	if (!NeedsApproval(Call))
	{
		if (EffectOf(Call) == TEXT("destructive") || EffectOf(Call) == TEXT("generate") || Call.bConfirm)
		{
			// Full autonomy: approved for the user, and a line says so.
			AddItem(FZettaChatItem::EKind::Event, FText::Format(LOCTEXT("AgentUI.AutoApproved", "Approved automatically · {0}"), FText::FromString(StepItem->Text)).ToString());
		}
		Execute(Batch, Call, StepItem);
		return;
	}

	StepItem->Status = TEXT("waiting");
	Notify();
	FZettaAgentTools::Get().Describe(FName(*Call.Name), Call.Input, [this, Batch, Call, StepItem](const FZettaToolPreview& Preview)
	{
		if (Batch != BatchSerial || CurrentPhase != EPhase::Tools) return;
		const FString Effect = EffectOf(Call);
		FZettaChatItemPtr Card = AddItem(FZettaChatItem::EKind::Approval, Preview.Title.IsEmpty() ? StepItem->Text : Preview.Title, Preview.Body);
		Card->ToolUseId = Call.ToolUseId;
		Card->ToolName = Call.Name;
		Card->ApprovalKind = Effect;
		Card->Credits = Preview.Credits;
		Card->Code = Preview.Code;
		Card->PreviewError = Preview.Error;
		Card->Reasons = Call.Reasons;
		Card->ConfirmLabel = Call.Name == TEXT("run_python") || Call.Name == TEXT("run_pipeline") ? LOCTEXT("AgentUI.Run", "Run").ToString()
			: Effect == TEXT("generate") ? LOCTEXT("AgentUI.Generate", "Generate").ToString()
			: Call.Name.StartsWith(TEXT("delete")) || Call.Name.StartsWith(TEXT("remove")) ? LOCTEXT("AgentUI.Delete", "Delete").ToString()
			: LOCTEXT("AgentUI.Allow", "Allow").ToString();
		const FString PreviewError = Preview.Error;
		Approvals.Add(Card.Get(), [this, Batch, Call, StepItem, PreviewError](bool bAllowed)
		{
			if (Batch != BatchSerial || CurrentPhase != EPhase::Tools) return;
			if (!bAllowed || !PreviewError.IsEmpty())
			{
				StepItem->Status = TEXT("declined");
				Record(Batch, Call, StepItem, FZettaToolResult::Error(PreviewError.IsEmpty()
					? TEXT("The user declined this action. Ask what they would like instead, or continue without it.")
					: TEXT("Not run: ") + PreviewError));
				return;
			}
			Execute(Batch, Call, StepItem);
		});
		Notify();
		OnNeedsUser.Broadcast();
	});
}

void FZettaAgentRuntime::Execute(int32 Batch, const FZettaClientCall& Call, const FZettaChatItemPtr& StepItem)
{
	StepItem->Status = Call.Name == TEXT("ask_user") ? TEXT("waiting") : TEXT("running");
	RunningToolUseId = Call.ToolUseId;
	Notify();
	// What the tool writes, creates or changes in the level is recorded, so the message can be undone.
	FZettaAgentChangeLog::Get().bRecording = true;
	TSharedRef<bool> bAnswered = MakeShared<bool>(false);
	FZettaAgentTools::Get().Run(FName(*Call.Name), Call.Input, [this, Batch, Call, StepItem, bAnswered](const FZettaToolResult& Result)
	{
		// A tool must answer once; a second answer is ignored rather than sent twice.
		if (*bAnswered) return;
		*bAnswered = true;
		FZettaAgentChangeLog::Get().bRecording = false;
		Record(Batch, Call, StepItem, Result);
	});
}

void FZettaAgentRuntime::Record(int32 Batch, const FZettaClientCall& Call, const FZettaChatItemPtr& StepItem, const FZettaToolResult& Result)
{
	// Stopped, or a new message took over, while the tool waited (on the user, a download…).
	if (Batch != BatchSerial || CurrentPhase != EPhase::Tools) return;
	RunningToolUseId.Reset();

	if (StepItem->Status != TEXT("declined")) StepItem->Status = Result.bIsError ? TEXT("error") : TEXT("done");
	if (Result.bIsError) StepItem->Detail = RuntimeShorten(Result.Content, 240);
	else if (!Result.Summary.IsEmpty()) StepItem->Text = RuntimeShorten(Result.Summary, 200);

	// A picture the tool made (edit_image, compose_images…): drawn like a generate_image result.
	if (Result.ImageId.IsSet() || !Result.ImageUrl.IsEmpty())
	{
		const int64 Id = Result.ImageId.Get(0);
		FString Url = Result.ImageUrl;
		if (Url.IsEmpty()) Url = ImageUrl(Id);
		if (Id > 0 && !Url.IsEmpty()) ImageUrls.Add(Id, Url);
		FZettaChatItemPtr Image = AddItem(FZettaChatItem::EKind::Image, Id > 0 ? FString::Printf(TEXT("Image #%lld"), Id) : TEXT("Image"));
		Image->ImageId = Id;
		Image->ImageUrl = Url;
		if (Url.IsEmpty() && Id > 0)
		{
			// Signed again by the server: links expire.
			FZettaHttp::Get(Api(FString::Printf(TEXT("/images/%lld"), Id)), [this, Image, Id](const FZettaResponse& R)
			{
				const FString Fresh = ZettaJson::Str(R.Object(), TEXT("url"));
				if (Fresh.IsEmpty()) return;
				ImageUrls.Add(Id, Fresh);
				Image->ImageUrl = Fresh;
				Notify();
			});
		}
	}

	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("toolUseId"), Call.ToolUseId);
	Json->SetStringField(TEXT("content"), RuntimeTruncate(Result.Content.IsEmpty() ? (Result.bIsError ? TEXT("Failed.") : TEXT("Done.")) : Result.Content));
	Json->SetBoolField(TEXT("isError"), Result.bIsError);
	if (Result.Images.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Images;
		for (int32 i = 0; i < Result.Images.Num() && i < 4; ++i)
		{
			TSharedRef<FJsonObject> Image = MakeShared<FJsonObject>();
			Image->SetStringField(TEXT("data"), Result.Images[i].Data);
			Image->SetStringField(TEXT("mediaType"), Result.Images[i].MediaType);
			Images.Add(MakeShared<FJsonValueObject>(Image));
		}
		Json->SetArrayField(TEXT("images"), Images);
	}
	PendingResults.Add(Json);
	Notify();
	RunNext(Batch);
}

void FZettaAgentRuntime::AskUser(const FZettaAskRequest& Request, FZettaToolDone Done)
{
	if (CurrentPhase != EPhase::Tools)
	{
		Done(FZettaToolResult::Error(TEXT("The editor cannot show questions right now. Ask in your reply instead.")));
		return;
	}
	FZettaChatItemPtr Card = AddItem(FZettaChatItem::EKind::Ask, Request.Title);
	Card->ToolUseId = RunningToolUseId;
	Card->Ask = MakeShared<FZettaAskRequest>(Request);
	for (const FZettaAskQuestion& Q : Request.Questions)
	{
		FZettaAskAnswer Answer;
		Answer.Id = Q.Id;
		Card->Answers.Add(Answer);
	}
	Questions.Add(Card.Get(), MoveTemp(Done));
	Notify();
	OnNeedsUser.Broadcast();
}

void FZettaAgentRuntime::CancelPending(const FString& Status)
{
	// The phase leaves Tools FIRST, so a batch waiting on a card sees it has been superseded and sends nothing on.
	if (CurrentPhase == EPhase::Tools) CurrentPhase = EPhase::Idle;
	++BatchSerial;
	FZettaAgentChangeLog::Get().bRecording = false;

	TMap<FZettaChatItem*, FZettaToolDone> Asked = MoveTemp(Questions);
	Questions.Reset();
	for (const FZettaChatItemPtr& Item : ChatItems)
	{
		if (FZettaToolDone* Done = Asked.Find(Item.Get()))
		{
			Item->bResolved = true;
			Item->Status = Status == TEXT("stopped") ? TEXT("stopped") : TEXT("superseded");
			// The batch is over, so this answer goes nowhere; it lets the tool finish.
			(*Done)(FZettaToolResult::Error(TEXT("Closed.")));
		}
		if (Approvals.Contains(Item.Get()))
		{
			Item->bResolved = true;
			Item->Status = Status == TEXT("superseded") ? TEXT("declined") : Status;
		}
	}
	Approvals.Reset();
}

void FZettaAgentRuntime::ClearPending()
{
	PendingCalls.Reset();
	PendingResults.Reset();
}

void FZettaAgentRuntime::SendResults()
{
	if (PendingCalls.Num() == 0) return;
	TSharedRef<FJsonObject> Body = Base();
	TArray<TSharedPtr<FJsonValue>> Results;
	for (const TSharedPtr<FJsonObject>& Result : PendingResults) Results.Add(MakeShared<FJsonValueObject>(Result));
	Body->SetArrayField(TEXT("toolResults"), Results);
	ClearPending();
	Stream(Body);
}

// ─── App events ───────────────────────────────────────────────────────────────

void FZettaAgentRuntime::OnJobsChanged()
{
	// A job the agent started belongs to the conversation that was open when it appeared.
	for (const FZettaJobPtr& Job : FZettaJobs::Get().GetJobs())
	{
		if (Job.IsValid() && Job->Source == TEXT("agent") && Job->Id() > 0 && !JobSessions.Contains(Job->Id())) JobSessions.Add(Job->Id(), Session);
	}
}

void FZettaAgentRuntime::OnJobFinished(const FZettaJobPtr& Job)
{
	if (!Job.IsValid() || Job->Source != TEXT("agent") || !Job->Generation.IsValid()) return;
	const int64* Owner = JobSessions.Find(Job->Id());
	const int64 OwnerSession = Owner ? *Owner : Session;
	if (Session == 0 || OwnerSession != Session) return;

	const FString Status = Job->Generation->Status;
	TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
	Event->SetStringField(TEXT("kind"), TEXT("job_finished"));
	Event->SetNumberField(TEXT("generationId"), (double)Job->Id());
	Event->SetStringField(TEXT("status"), Status);
	FString Imported;
	if (Job->Import.IsSet() && !Job->Import->MainAssetPath().IsEmpty())
	{
		Imported = FPackageName::ObjectPathToPackageName(Job->Import->MainAssetPath());
		Event->SetStringField(TEXT("importedPath"), Imported);
	}
	const FString Title = Job->Generation->DisplayTitle();
	FString Line;
	if (Status == TEXT("success"))
	{
		Line = FString::Printf(TEXT("“%s” is ready"), *Title) + (Imported.IsEmpty() ? FString() : TEXT(" — imported to ") + Imported);
	}
	else
	{
		Line = FString::Printf(TEXT("“%s” %s"), *Title, *Status) + (Job->Error.IsEmpty() ? FString() : TEXT(": ") + Job->Error);
	}
	AddItem(FZettaChatItem::EKind::Event, Line);
	// After Stop the agent does not pick the job up on its own; the line above still tells the user.
	if (!bHalted) QueueEvent(Event);
	Notify();
	DrainQueuedEvents();
}

void FZettaAgentRuntime::FollowPipelineRun(int64 RunId)
{
	if (RunId > 0) RunSessions.Add(RunId, Session);
}

void FZettaAgentRuntime::PipelineRunFinished(int64 RunId, const FString& Status, const TArray<FString>& ImportedPaths)
{
	// Only runs the agent started, and only in the conversation that started them.
	const int64* Owner = RunSessions.Find(RunId);
	if (!Owner || Session == 0 || *Owner != Session) return;
	if (Status != TEXT("done") && Status != TEXT("failed") && Status != TEXT("cancelled") && Status != TEXT("awaiting")) return;

	TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
	Event->SetStringField(TEXT("kind"), TEXT("pipeline_run_finished"));
	Event->SetNumberField(TEXT("runId"), (double)RunId);
	Event->SetStringField(TEXT("status"), Status);
	TArray<FString> Paths;
	for (const FString& Path : ImportedPaths)
	{
		if (!Path.IsEmpty() && Paths.Num() < 50) Paths.Add(FPackageName::ObjectPathToPackageName(Path));
	}
	if (Paths.Num() > 0) Event->SetField(TEXT("importedPaths"), ZettaJson::StringArray(Paths));

	const FString Line = Status == TEXT("done")
		? FString::Printf(TEXT("Pipeline run #%lld finished"), RunId) + (Paths.Num() > 0 ? FString::Printf(TEXT(" — %d model(s) imported"), Paths.Num()) : FString())
		: Status == TEXT("awaiting") ? FString::Printf(TEXT("Pipeline run #%lld is waiting for you"), RunId)
		: FString::Printf(TEXT("Pipeline run #%lld %s"), RunId, *Status);
	AddItem(FZettaChatItem::EKind::Event, Line);
	if (!bHalted) QueueEvent(Event);
	Notify();
	DrainQueuedEvents();
}

void FZettaAgentRuntime::QueueEvent(const TSharedRef<FJsonObject>& Event)
{
	QueuedEvents.Add(Event);
}

void FZettaAgentRuntime::DrainQueuedEvents()
{
	if (IsBusy() || bHalted || QueuedEvents.Num() == 0 || !FZettaAuth::Get().IsSignedIn()) return;
	const TSharedRef<FJsonObject> Event = QueuedEvents[0];
	QueuedEvents.RemoveAt(0);
	// What the agent does about the event is recorded with the last message's changes, or a set of its own.
	if (ChangeTurn.IsEmpty() || FZettaAgentChangeLog::Get().CurrentTurn() != ChangeTurn) ChangeTurn = FZettaAgentChangeLog::Get().BeginTurn();
	TSharedRef<FJsonObject> Body = Base();
	Body->SetObjectField(TEXT("event"), Event);
	Stream(Body);
}

// ─── What the tools can ask of the conversation ───────────────────────────────

namespace ZettaAgentSession
{
	int64 CurrentSessionId() { return FZettaAgentRuntime::Get().SessionId(); }
	FString ImageUrl(int64 ImageId) { return FZettaAgentRuntime::Get().ImageUrl(ImageId); }
	void FollowPipelineRun(int64 RunId) { FZettaAgentRuntime::Get().FollowPipelineRun(RunId); }
	void PipelineRunFinished(int64 RunId, const FString& Status, const TArray<FString>& ImportedPaths)
	{
		FZettaAgentRuntime::Get().PipelineRunFinished(RunId, Status, ImportedPaths);
	}
}

#undef LOCTEXT_NAMESPACE
