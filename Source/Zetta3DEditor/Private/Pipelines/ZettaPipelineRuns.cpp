// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaPipelineRuns.h"

#include "GenericPlatform/GenericPlatformHttp.h"
#include "ZettaCatalog.h"
#include "ZettaHttp.h"
#include "ZettaPipelineJson.h"

using namespace ZettaPJson;

static FDateTime PlDate(const FObj& O, const FString& Key)
{
	FDateTime Out;
	const TOptional<FString> T = JText(O, Key);
	if (T.IsSet()) FDateTime::ParseIso8601(*T.GetValue(), Out);
	return Out;
}

static TOptional<FDateTime> PlOptDate(const FObj& O, const FString& Key)
{
	if (!JIsString(JGet(O, Key))) return {};
	return PlDate(O, Key);
}

static int64 PlLong(const FObj& O, const FString& Key)
{
	const TOptional<double> N = JNumber(O, Key);
	return N.IsSet() ? (int64)N.GetValue() : 0;
}

static TOptional<int64> PlOptLong(const FObj& O, const FString& Key)
{
	const TOptional<double> N = JNumber(O, Key);
	if (!N.IsSet()) return {};
	return (int64)N.GetValue();
}

static TArray<double> PlBox(const FVal& V)
{
	TArray<double> Out;
	for (const FVal& Item : JArrayOf(V)) if (JIsNumber(Item)) Out.Add(Item->AsNumber());
	return Out;
}

// ─── Models ─────────────────────────────────────────────────────────────────────────────────────────────────────────

FZettaPipeline FZettaPipeline::From(const TSharedPtr<FJsonObject>& O)
{
	FZettaPipeline P;
	P.Id = PlLong(O, TEXT("id"));
	P.Title = JTextOr(O, TEXT("title"));
	P.Graph = FZettaPipelineGraph::FromJson(JObject(O, TEXT("graph")));
	P.CreatedAt = PlDate(O, TEXT("createdAt"));
	P.UpdatedAt = PlDate(O, TEXT("updatedAt"));
	P.Problems = FZettaPipelineProblem::ListFrom(JArrayOf(JGet(O, TEXT("problems"))));
	return P;
}

FZettaPipelineDraft FZettaPipelineDraft::From(const TSharedPtr<FJsonObject>& O)
{
	FZettaPipelineDraft D;
	D.Title = JTextOr(O, TEXT("title"));
	D.Summary = JTextOr(O, TEXT("summary"));
	D.Graph = FZettaPipelineGraph::FromJson(JObject(O, TEXT("graph")));
	return D;
}

FZettaPipelineSummary FZettaPipelineSummary::From(const TSharedPtr<FJsonObject>& O)
{
	FZettaPipelineSummary S;
	S.Id = PlLong(O, TEXT("id"));
	S.Title = JTextOr(O, TEXT("title"));
	S.Steps = (int32)PlLong(O, TEXT("steps"));
	S.CreatedAt = PlDate(O, TEXT("createdAt"));
	S.UpdatedAt = PlDate(O, TEXT("updatedAt"));
	if (const FObj Last = JObject(O, TEXT("lastRun")))
	{
		S.LastRunId = PlLong(Last, TEXT("id"));
		S.LastRunStatus = JTextOr(Last, TEXT("status"));
		S.LastRunAt = PlDate(Last, TEXT("createdAt"));
	}
	if (const FObj Counts = JObject(O, TEXT("runCounts")))
	{
		for (const TPair<FString, FVal>& Pair : Counts->Values) if (JIsNumber(Pair.Value)) S.RunCounts.Add(Pair.Key, (int32)Pair.Value->AsNumber());
	}
	return S;
}

bool FZettaRunSummary::IsLive() const { return ZettaRunStatus::IsLive(Status); }

FZettaRunSummary FZettaRunSummary::From(const TSharedPtr<FJsonObject>& O)
{
	FZettaRunSummary R;
	R.Id = PlLong(O, TEXT("id"));
	R.Mode = JTextOr(O, TEXT("mode"));
	R.Status = JTextOr(O, TEXT("status"));
	R.bPaused = JBoolOr(O, TEXT("paused"), false);
	R.EstimatedCredits = (int32)PlLong(O, TEXT("estimatedCredits"));
	R.SpentCredits = (int32)PlLong(O, TEXT("spentCredits"));
	R.CreatedAt = PlDate(O, TEXT("createdAt"));
	R.FinishedAt = PlOptDate(O, TEXT("finishedAt"));
	if (const FObj P = JObject(O, TEXT("progress")))
	{
		R.bHasProgress = true;
		R.Done = (int32)PlLong(P, TEXT("done"));
		R.Total = (int32)PlLong(P, TEXT("total"));
		R.Waiting = (int32)PlLong(P, TEXT("waiting"));
		R.Failed = (int32)PlLong(P, TEXT("failed"));
	}
	R.InputsNeeded = (int32)PlLong(O, TEXT("inputsNeeded"));
	if (const FObj Seed = JObject(O, TEXT("seed")))
	{
		R.bHasSeed = true;
		R.SeedType = JTextOr(Seed, TEXT("type"));
		R.SeedName = JTextOr(Seed, TEXT("name"));
		R.SeedThumbnailUrl = JTextOr(Seed, TEXT("thumbnailUrl"));
	}
	return R;
}

FZettaSignedPicture FZettaSignedPicture::From(const TSharedPtr<FJsonObject>& O)
{
	FZettaSignedPicture P;
	P.Url = JTextOr(O, TEXT("url"));
	P.Gen2dPostId = PlOptLong(O, TEXT("gen2dPostId"));
	return P;
}

TOptional<int64> FZettaStepOutput::Gen3dPost() const
{
	if (IsMesh() || IsSplat() || IsFile()) return Gen3dPostId;
	return {};
}

TOptional<FZettaStepOutput> FZettaStepOutput::From(const TSharedPtr<FJsonObject>& O)
{
	if (!O.IsValid()) return {};
	FZettaStepOutput Out;
	Out.Type = JTextOr(O, TEXT("type"));
	Out.Raw = JClone(O);
	if (Out.IsPicture()) Out.Picture = FZettaSignedPicture::From(JObject(O, TEXT("picture")));
	else if (Out.IsViews())
	{
		for (const FVal& V : JArrayOf(JGet(O, TEXT("views"))))
		{
			const FObj View = JObjectOf(V);
			Out.Views.Add({ JTextOr(View, TEXT("viewType")), FZettaSignedPicture::From(JObject(View, TEXT("picture"))) });
		}
	}
	else if (Out.IsIsolated())
	{
		Out.Character = FZettaSignedPicture::From(JObject(O, TEXT("character")));
		for (const FVal& V : JArrayOf(JGet(O, TEXT("parts"))))
		{
			const FObj P = JObjectOf(V);
			if (!P.IsValid()) continue;
			Out.Parts.Add({ JTextOr(P, TEXT("key")), JTextOr(P, TEXT("label")), JTextOr(P, TEXT("kind")), PlBox(JGet(P, TEXT("box"))), FZettaSignedPicture::From(JObject(P, TEXT("picture"))) });
		}
	}
	else if (Out.IsMesh() || Out.IsSplat() || Out.IsFile())
	{
		Out.Gen3dPostId = PlLong(O, TEXT("gen3dPostId"));
		Out.Format = JTextOr(O, TEXT("format"));
	}
	return Out;
}

FZettaNodeRunState FZettaNodeRunState::Of(const TArray<const FZettaPipelineRunStep*>& Steps)
{
	FZettaNodeRunState S;
	if (Steps.Num() == 0) return S;
	auto HasStatus = [&Steps](const TCHAR* Status)
	{
		for (const FZettaPipelineRunStep* Step : Steps) if (Step->Status == Status) return true;
		return false;
	};
	using namespace ZettaStepStatus;
	S.Status = HasStatus(Running) || HasStatus(Submitted) || HasStatus(Ready) ? Running
		: HasStatus(Review) ? Review
		: HasStatus(Waiting) ? Waiting
		: HasStatus(Failed) ? Failed
		: HasStatus(Cancelled) ? Cancelled
		: Success;
	S.Total = Steps.Num();
	for (const FZettaPipelineRunStep* Step : Steps)
	{
		if (Step->Status == Success) ++S.Done;
		S.Credits += Step->Credits;
	}
	return S;
}

FString FZettaNodeRunState::StatusLabel(const FString& Status)
{
	using namespace ZettaStepStatus;
	if (Status == TEXT("idle")) return TEXT("Not run");
	if (Status == Waiting) return TEXT("Waiting for you");
	if (Status == Ready) return TEXT("Starting");
	if (Status == Running || Status == Submitted) return TEXT("Running");
	if (Status == Review) return TEXT("Choose parts");
	if (Status == Success) return TEXT("Done");
	if (Status == Failed) return TEXT("Failed");
	if (Status == Cancelled) return TEXT("Cancelled");
	return Status;
}

bool FZettaPipelineRun::IsLive() const { return ZettaRunStatus::IsLive(Status); }

TArray<const FZettaPipelineRunStep*> FZettaPipelineRun::StepsOf(const FString& NodeId) const
{
	TArray<const FZettaPipelineRunStep*> Out;
	for (const FZettaPipelineRunStep& S : Steps) if (S.NodeId == NodeId) Out.Add(&S);
	return Out;
}

const FZettaPipelineRunStep* FZettaPipelineRun::Step(int64 StepId) const
{
	for (const FZettaPipelineRunStep& S : Steps) if (S.Id == StepId) return &S;
	return nullptr;
}

FZettaNodeRunState FZettaPipelineRun::NodeState(const FString& NodeId) const { return FZettaNodeRunState::Of(StepsOf(NodeId)); }

bool FZettaPipelineRun::IsNodeEditable(const FString& NodeId) const
{
	const FZettaPipelineNode* Node = Graph.Node(NodeId);
	if (!Node || ZettaNodeCatalog::IsSeed(Node->Kind) || Status == ZettaRunStatus::Cancelled) return false;
	for (const FZettaPipelineRunStep* S : StepsOf(NodeId)) if (S->Status != ZettaStepStatus::Waiting) return false;
	return true;
}

FString FZettaPipelineRun::PartLabel(const FString& PartKey) const
{
	if (PartKey.IsEmpty()) return FString();
	int32 Slash = INDEX_NONE;
	PartKey.FindLastChar(TEXT('/'), Slash);
	const FString Key = PartKey.Mid(Slash + 1);
	for (const FZettaPipelineRunStep& S : Steps)
	{
		if (!S.Output.IsSet() || !S.Output->IsIsolated()) continue;
		for (const FZettaIsolatedPart& Part : S.Output->Parts) if (Part.Key == Key) return Part.Label;
	}
	return Key;
}

TArray<const FZettaPipelineRunStep*> FZettaPipelineRun::ImportableSteps(EZettaRunImport ImportMode) const
{
	TArray<const FZettaPipelineRunStep*> Out;
	if (ImportMode == EZettaRunImport::None) return Out;
	for (const FZettaPipelineRunStep& S : Steps)
	{
		if (S.Status != ZettaStepStatus::Success || !S.Output.IsSet() || !S.Output->IsMesh()) continue;
		const FZettaPipelineNode* Node = Graph.Node(S.NodeId);
		if (!Node || ZettaNodeCatalog::IsSeed(Node->Kind)) continue;
		if (!S.Generation.IsValid() || !S.Generation->IsSuccess() || S.Generation->IsSplat()) continue;
		if (ImportMode == EZettaRunImport::AllMeshSteps)
		{
			Out.Add(&S);
			continue;
		}
		bool bTakenFurther = false;
		for (const FZettaPipelineEdge& E : Graph.Edges)
		{
			if (E.Source != Node->Id) continue;
			const FZettaPipelineNode* Target = Graph.Node(E.Target);
			const FZettaInputPort* Port = Target ? ZettaNodeCatalog::InputPort(Target->Kind, E.TargetHandle) : nullptr;
			if (Port && Port->AcceptsType(ZettaPortTypes::Mesh)) bTakenFurther = true;
		}
		if (!bTakenFurther) Out.Add(&S);
	}
	return Out;
}

int32 FZettaPipelineRun::WaitingCount() const
{
	int32 N = 0;
	for (const FZettaPipelineRunStep& S : Steps) if (S.Status == ZettaStepStatus::Waiting) ++N;
	return N;
}

TSharedRef<FZettaPipelineRun> FZettaPipelineRun::From(const TSharedPtr<FJsonObject>& O)
{
	TSharedRef<FZettaPipelineRun> R = MakeShared<FZettaPipelineRun>();
	R->Id = PlLong(O, TEXT("id"));
	R->PipelineId = PlLong(O, TEXT("pipelineId"));
	R->Mode = JTextOr(O, TEXT("mode"));
	R->Status = JTextOr(O, TEXT("status"));
	R->bPaused = JBoolOr(O, TEXT("paused"), false);
	if (const FObj Seed = JObject(O, TEXT("seed"))) R->Seed = JClone(Seed);
	R->EstimatedCredits = (int32)PlLong(O, TEXT("estimatedCredits"));
	R->SpentCredits = (int32)PlLong(O, TEXT("spentCredits"));
	R->Graph = FZettaPipelineGraph::FromJson(JObject(O, TEXT("graph")));
	R->CreatedAt = PlDate(O, TEXT("createdAt"));
	R->UpdatedAt = PlDate(O, TEXT("updatedAt"));
	R->FinishedAt = PlOptDate(O, TEXT("finishedAt"));
	for (const FVal& V : JArrayOf(JGet(O, TEXT("steps"))))
	{
		const FObj S = JObjectOf(V);
		if (!S.IsValid()) continue;
		FZettaPipelineRunStep Step;
		Step.Id = PlLong(S, TEXT("id"));
		Step.NodeId = JTextOr(S, TEXT("nodeId"));
		Step.PartKey = JTextOr(S, TEXT("partKey"));
		Step.Status = JTextOr(S, TEXT("status"));
		Step.Attempt = (int32)PlLong(S, TEXT("attempt"));
		Step.Credits = (int32)PlLong(S, TEXT("credits"));
		Step.ErrorMessage = JTextOr(S, TEXT("errorMessage"));
		Step.Gen3dPostId = PlOptLong(S, TEXT("gen3dPostId"));
		Step.Output = FZettaStepOutput::From(JObject(S, TEXT("output")));
		if (const FObj Review = JObject(S, TEXT("review")))
		{
			FZettaStepReview Rv;
			const FObj Id = JObject(Review, TEXT("identification"));
			Rv.Subject = JTextOr(Id, TEXT("subject"));
			Rv.bHumanoid = JBoolOr(Id, TEXT("humanoid"), false);
			for (const FVal& P : JArrayOf(JGet(Id, TEXT("parts"))))
			{
				const FObj Part = JObjectOf(P);
				if (!Part.IsValid()) continue;
				Rv.Parts.Add({ JTextOr(Part, TEXT("key")), JTextOr(Part, TEXT("label")), JTextOr(Part, TEXT("kind")), JTextOr(Part, TEXT("level")), PlBox(JGet(Part, TEXT("box"))) });
			}
			Rv.ProposedKeys = JStringsOf(JGet(Review, TEXT("proposedKeys")));
			Rv.Source = FZettaSignedPicture::From(JObject(Review, TEXT("source")));
			Step.Review = Rv;
		}
		if (const FObj Generation = JObject(S, TEXT("generation"))) Step.Generation = FZettaGeneration::From(Generation);
		Step.CreatedAt = PlDate(S, TEXT("createdAt"));
		Step.UpdatedAt = PlDate(S, TEXT("updatedAt"));
		R->Steps.Add(MoveTemp(Step));
	}
	return R;
}

FZettaServerEstimate FZettaServerEstimate::From(const TSharedPtr<FJsonObject>& O)
{
	FZettaServerEstimate E;
	E.Credits = (int32)PlLong(O, TEXT("credits"));
	E.bPerPart = JBoolOr(O, TEXT("perPart"), false);
	E.Problems = FZettaPipelineProblem::ListFrom(JArrayOf(JGet(O, TEXT("problems"))));
	return E;
}

// ─── Statuses ───────────────────────────────────────────────────────────────────────────────────────────────────────

namespace ZettaRunStatus
{
	const TCHAR* Draft = TEXT("draft");
	const TCHAR* Running = TEXT("running");
	const TCHAR* Awaiting = TEXT("awaiting");
	const TCHAR* Paused = TEXT("paused");
	const TCHAR* Failed = TEXT("failed");
	const TCHAR* Done = TEXT("done");
	const TCHAR* Cancelled = TEXT("cancelled");
	const TCHAR* ModeAuto = TEXT("auto");
	const TCHAR* ModeManual = TEXT("manual");

	bool IsLive(const FString& Status) { return Status == Running || Status == Paused || Status == Awaiting; }
	bool IsDeletable(const FString& Status) { return Status == Done || Status == Failed || Status == Cancelled || Status == Draft; }

	FString Label(const FString& Status)
	{
		if (Status == Draft) return TEXT("Not started");
		if (Status == Running) return TEXT("Running");
		if (Status == Awaiting) return TEXT("Needs you");
		if (Status == Paused) return TEXT("Paused");
		if (Status == Failed) return TEXT("Stopped");
		if (Status == Done) return TEXT("Done");
		if (Status == Cancelled) return TEXT("Cancelled");
		return Status;
	}

	EZettaRunTone Tone(const FString& Status)
	{
		if (Status == Draft || Status == Awaiting) return EZettaRunTone::Attention;
		if (Status == Running) return EZettaRunTone::Running;
		if (Status == Done) return EZettaRunTone::Done;
		if (Status == Failed) return EZettaRunTone::Failed;
		return EZettaRunTone::Idle;
	}

	FString ModeLabel(const FString& Mode) { return Mode == ModeAuto ? TEXT("Auto") : TEXT("Manual"); }

	FString ModeHint(const FString& Mode)
	{
		return Mode == ModeAuto ? TEXT("Every step submits itself, to the end.") : TEXT("Waits for you before each step — you run it from Runs.");
	}
}

namespace ZettaStepStatus
{
	const TCHAR* Waiting = TEXT("waiting");
	const TCHAR* Ready = TEXT("ready");
	const TCHAR* Running = TEXT("running");
	const TCHAR* Submitted = TEXT("submitted");
	const TCHAR* Review = TEXT("review");
	const TCHAR* Success = TEXT("success");
	const TCHAR* Failed = TEXT("failed");
	const TCHAR* Cancelled = TEXT("cancelled");
}

// ─── Routes ─────────────────────────────────────────────────────────────────────────────────────────────────────────

namespace ZettaPipelinesApi
{
	TArray<FZettaPipelineProblem> ProblemsOf(const FZettaResponse& Response)
	{
		return FZettaPipelineProblem::ListFrom(JArrayOf(ZettaPJson::JGet(Response.Object(), TEXT("problems"))));
	}

	FString Explain(const FZettaResponse& Response)
	{
		FString Text = ZettaCatalog::Explain(Response);
		for (const FZettaPipelineProblem& P : ProblemsOf(Response)) Text += TEXT(" ") + P.ToString();
		return Text;
	}

	static TSharedRef<FJsonObject> GraphBody(const FZettaPipelineGraph& Graph) { return Graph.ToJson(); }

	static void Pipeline(const FZettaResponse& R, const TZettaDone<FZettaPipeline>& Done)
	{
		if (Done) Done(R, R.bOk ? FZettaPipeline::From(R.Object()) : FZettaPipeline());
	}

	static void Run(const FZettaResponse& R, const FRunDone& Done)
	{
		FZettaPipelineRunPtr Run;
		if (R.bOk && R.Object().IsValid()) Run = FZettaPipelineRun::From(R.Object());
		if (Done) Done(R, Run);
	}

	static void RunAction(const FString& Path, const TSharedPtr<FJsonObject>& Body, FRunDone Done)
	{
		FZettaHttp::Post(Path, Body.IsValid() ? Body : MakeShared<FJsonObject>(), [Done](const FZettaResponse& R) { Run(R, Done); });
	}

	void List(TZettaDone<TArray<FZettaPipelineSummary>> Done)
	{
		FZettaHttp::Get(TEXT("/pipelines"), [Done](const FZettaResponse& R)
		{
			TArray<FZettaPipelineSummary> List;
			if (R.bOk) for (const FVal& V : JArrayOf(R.Json)) if (JIsObject(V)) List.Add(FZettaPipelineSummary::From(V->AsObject()));
			if (Done) Done(R, List);
		});
	}

	void Get(int64 Id, TZettaDone<FZettaPipeline> Done)
	{
		FZettaHttp::Get(FString::Printf(TEXT("/pipelines/%lld"), Id), [Done](const FZettaResponse& R) { Pipeline(R, Done); });
	}

	void Create(const FString& Title, const FZettaPipelineGraph* Graph, TZettaDone<FZettaPipeline> Done)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		if (!Title.TrimStartAndEnd().IsEmpty()) Body->SetStringField(TEXT("title"), Title);
		if (Graph) Body->SetObjectField(TEXT("graph"), GraphBody(*Graph));
		FZettaHttp::Post(TEXT("/pipelines"), Body, [Done](const FZettaResponse& R) { Pipeline(R, Done); });
	}

	void Update(int64 Id, const TOptional<FString>& Title, const FZettaPipelineGraph* Graph, TZettaDone<FZettaPipeline> Done)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		if (Title.IsSet()) Body->SetStringField(TEXT("title"), Title.GetValue());
		if (Graph) Body->SetObjectField(TEXT("graph"), GraphBody(*Graph));
		FZettaHttp::Patch(FString::Printf(TEXT("/pipelines/%lld"), Id), Body, [Done](const FZettaResponse& R) { Pipeline(R, Done); });
	}

	void Save(int64 Id, const FZettaPipelineGraph& Graph, const TOptional<FString>& Title, const FZettaEntitlements3D* Gates, TZettaDone<FZettaPipeline> Done)
	{
		const FZettaPipelineGraph Built = ZettaRequestBuilder::WithRequests(Graph, Gates);
		Update(Id, Title, &Built, MoveTemp(Done));
	}

	void Duplicate(int64 Id, TZettaDone<FZettaPipeline> Done)
	{
		FZettaHttp::Post(FString::Printf(TEXT("/pipelines/%lld/duplicate"), Id), MakeShared<FJsonObject>(), [Done](const FZettaResponse& R) { Pipeline(R, Done); });
	}

	void Delete(int64 Id, FZettaCallback Done)
	{
		FZettaHttp::Delete(FString::Printf(TEXT("/pipelines/%lld"), Id), MoveTemp(Done));
	}

	void Estimate(const FZettaPipelineGraph& Graph, TZettaDone<FZettaServerEstimate> Done)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetObjectField(TEXT("graph"), GraphBody(Graph));
		FZettaHttp::Post(TEXT("/pipelines/estimate"), Body, [Done](const FZettaResponse& R)
		{
			if (Done) Done(R, R.bOk ? FZettaServerEstimate::From(R.Object()) : FZettaServerEstimate());
		});
	}

	void Draft(const FString& Description, const FZettaPipelineGraph* Current, const FZettaEntitlements3D* Gates, TZettaDone<FZettaPipelineDraft> Done)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("description"), Description);
		if (Current) Body->SetObjectField(TEXT("graph"), GraphBody(ZettaRequestBuilder::WithRequests(*Current, Gates)));
		// A model call: up to a minute or two.
		FZettaHttp::Post(TEXT("/pipelines/draft"), Body, [Done](const FZettaResponse& R)
		{
			if (Done) Done(R, R.bOk ? FZettaPipelineDraft::From(R.Object()) : FZettaPipelineDraft());
		}, {}, 240.f);
	}

	void Runs(int64 PipelineId, TZettaDone<TArray<FZettaRunSummary>> Done)
	{
		FZettaHttp::Get(FString::Printf(TEXT("/pipelines/%lld/runs"), PipelineId), [Done](const FZettaResponse& R)
		{
			TArray<FZettaRunSummary> List;
			if (R.bOk) for (const FVal& V : JArrayOf(R.Json)) if (JIsObject(V)) List.Add(FZettaRunSummary::From(V->AsObject()));
			if (Done) Done(R, List);
		});
	}

	void Batch(int64 PipelineId, const FString& Mode, const TArray<TSharedPtr<FJsonObject>>& Seeds, const FZettaPipelineGraph* Graph, bool bDraft,
		TZettaDone<TArray<FZettaRunSummary>> Done)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("mode"), Mode.IsEmpty() ? FString(ZettaRunStatus::ModeManual) : Mode);
		TArray<FVal> SeedValues;
		for (const TSharedPtr<FJsonObject>& Seed : Seeds) SeedValues.Add(MakeShared<FJsonValueObject>(JClone(Seed)));
		Body->SetArrayField(TEXT("seeds"), SeedValues);
		Body->SetBoolField(TEXT("draft"), bDraft);
		if (Graph) Body->SetObjectField(TEXT("graph"), GraphBody(*Graph));
		FZettaHttp::Post(FString::Printf(TEXT("/pipelines/%lld/batch"), PipelineId), Body, [Done](const FZettaResponse& R)
		{
			TArray<FZettaRunSummary> List;
			if (R.bOk) for (const FVal& V : JArrayOf(ZettaPJson::JGet(R.Object(), TEXT("runs")))) if (JIsObject(V)) List.Add(FZettaRunSummary::From(V->AsObject()));
			if (Done) Done(R, List);
		});
	}

	void DeleteRun(int64 RunId, FZettaCallback Done) { FZettaHttp::Delete(FString::Printf(TEXT("/pipeline-runs/%lld"), RunId), MoveTemp(Done)); }

	void GetRun(int64 RunId, FRunDone Done)
	{
		FZettaHttp::Get(FString::Printf(TEXT("/pipeline-runs/%lld"), RunId), [Done](const FZettaResponse& R) { Run(R, Done); });
	}

	void Start(int64 RunId, FRunDone Done) { RunAction(FString::Printf(TEXT("/pipeline-runs/%lld/start"), RunId), nullptr, MoveTemp(Done)); }
	void Pause(int64 RunId, FRunDone Done) { RunAction(FString::Printf(TEXT("/pipeline-runs/%lld/pause"), RunId), nullptr, MoveTemp(Done)); }
	void Resume(int64 RunId, FRunDone Done) { RunAction(FString::Printf(TEXT("/pipeline-runs/%lld/resume"), RunId), nullptr, MoveTemp(Done)); }
	void Cancel(int64 RunId, FRunDone Done) { RunAction(FString::Printf(TEXT("/pipeline-runs/%lld/cancel"), RunId), nullptr, MoveTemp(Done)); }
	void RunWaiting(int64 RunId, FRunDone Done) { RunAction(FString::Printf(TEXT("/pipeline-runs/%lld/run-waiting"), RunId), nullptr, MoveTemp(Done)); }
	void RunStep(int64 RunId, int64 StepId, FRunDone Done) { RunAction(FString::Printf(TEXT("/pipeline-runs/%lld/steps/%lld/run"), RunId, StepId), nullptr, MoveTemp(Done)); }

	void SetMode(int64 RunId, const FString& Mode, FRunDone Done)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("mode"), Mode);
		RunAction(FString::Printf(TEXT("/pipeline-runs/%lld/mode"), RunId), Body, MoveTemp(Done));
	}

	static TSharedRef<FJsonObject> RequestAndSettings(const TSharedPtr<FJsonObject>& Request, const TSharedPtr<FJsonObject>& Settings)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		if (Request.IsValid()) Body->SetObjectField(TEXT("request"), JClone(Request));
		if (Settings.IsValid()) Body->SetObjectField(TEXT("settings"), JClone(Settings));
		return Body;
	}

	void PatchRunNode(int64 RunId, const FString& NodeId, const TSharedPtr<FJsonObject>& Request, const TSharedPtr<FJsonObject>& Settings, FRunDone Done)
	{
		const FString Path = FString::Printf(TEXT("/pipeline-runs/%lld/nodes/%s"), RunId, *FGenericPlatformHttp::UrlEncode(NodeId));
		FZettaHttp::Patch(Path, RequestAndSettings(Request, Settings), [Done](const FZettaResponse& R) { Run(R, Done); });
	}

	void RetryStep(int64 RunId, int64 StepId, const TSharedPtr<FJsonObject>& Request, const TSharedPtr<FJsonObject>& Settings, FRunDone Done)
	{
		RunAction(FString::Printf(TEXT("/pipeline-runs/%lld/steps/%lld/retry"), RunId, StepId), RequestAndSettings(Request, Settings), MoveTemp(Done));
	}

	void ChooseParts(int64 RunId, int64 StepId, const TArray<FString>& Keys, const TArray<FString>& ExtraParts, FRunDone Done)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetField(TEXT("keys"), JStrings(Keys));
		Body->SetField(TEXT("extraParts"), JStrings(ExtraParts));
		RunAction(FString::Printf(TEXT("/pipeline-runs/%lld/steps/%lld/parts"), RunId, StepId), Body, MoveTemp(Done));
	}
}
