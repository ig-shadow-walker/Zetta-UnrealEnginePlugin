// Copyright Zetta 3D. See LICENSE.md.

// The pipeline service the agent's tools stand on (list, get, build, edit, run, status, manage, import), built on the
// same recipe code as the Pipelines page — so what the agent saves is priced and runnable as it stands.

#include "ZettaPipelines.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Texture.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ZettaAuth.h"
#include "ZettaCatalog.h"
#include "ZettaPipelineJson.h"
#include "ZettaPipelineServices.h"
#include "ZettaPipelineText.h"
#include "ZettaSettings.h"

using namespace ZettaPJson;

namespace ZettaPipelines
{
	using namespace ZettaPipelineText;

	static constexpr int32 MaxSeeds = 20;
	static constexpr int32 MaxSteps = 24;
	static constexpr int32 MaxChanges = 24;
	static constexpr int32 MaxTitle = 200;
	static constexpr int32 MaxPrompt = 2000;
	static constexpr int32 MaxPartList = 16;
	static constexpr int32 MaxPartLength = 80;
	static constexpr int32 MaxImportNodes = 50;

	static const FZettaPriceList* Prices() { return FZettaPipelinePlan::Get().Prices(); }
	static const FZettaEntitlements3D* Gates() { return FZettaPipelinePlan::Get().Gates(); }
	static FString ModelsFolder() { return UZettaProjectSettings::Get()->GetModelsFolder(); }

	template <typename T>
	static T Fail(const FString& Text)
	{
		T Out;
		Out.bOk = false;
		Out.Text = Text;
		return Out;
	}

	static bool SignedIn(FString& OutError)
	{
		if (FZettaAuth::Get().IsSignedIn()) return true;
		OutError = TEXT("Not signed in to Zetta 3D — ask the user to sign in from the Zetta 3D window.");
		return false;
	}

	/** An API error the model can act on: 404 named, refusals with the steps they are about. */
	static FString ErrorText(const FZettaResponse& R, const FString& NotFound = FString())
	{
		if (R.Status == 404 && !NotFound.IsEmpty()) return NotFound;
		return ZettaPipelinesApi::Explain(R);
	}

	/** The pipeline's graph as the editor loads it (Studio defaults filled in, no per-run inputs). */
	static FZettaPipelineGraph Recipe(const FZettaPipeline& Pipeline) { return ZettaRequestBuilder::Normalize(Pipeline.Graph).Graph; }

	static FString StartsFromOf(const FZettaPipelineGraph& Graph)
	{
		const FZettaPipelineNode* Seed = Graph.Seed();
		if (!Seed) return FString();
		return Seed->Kind == ZettaNodeKinds::SeedModel ? TEXT("model") : Seed->Kind == ZettaNodeKinds::SeedViews ? TEXT("views") : TEXT("picture");
	}

	static FString StartsFromWords(const FZettaPipelineGraph& Graph)
	{
		const FString S = StartsFromOf(Graph);
		return S == TEXT("model") ? TEXT("a model") : S == TEXT("views") ? TEXT("pictures of each side") : S.IsEmpty() ? TEXT("(no seed)") : TEXT("a picture");
	}

	static FRunInfo RunInfoOf(const FZettaRunSummary& R)
	{
		FRunInfo I;
		I.Id = R.Id;
		I.Status = R.Status;
		I.Mode = R.Mode;
		I.bPaused = R.bPaused;
		I.StepsDone = R.Done;
		I.StepsTotal = R.Total;
		I.StepsWaiting = R.Waiting;
		I.StepsFailed = R.Failed;
		I.InputsNeeded = R.InputsNeeded;
		I.SpentCredits = R.SpentCredits;
		I.EstimatedCredits = R.EstimatedCredits;
		I.SeedType = R.SeedType;
		I.SeedName = R.SeedName;
		I.CreatedAt = R.CreatedAt;
		return I;
	}

	/** The structured half of a pipeline's details (the text is built by each caller). */
	static void FillDetails(FPipelineDetails& D, int64 Id, const FString& Title, const FZettaPipelineGraph& Graph)
	{
		D.Id = Id;
		D.Title = Title;
		D.StartsFrom = StartsFromOf(Graph);
		const TSet<FString> PerPart = ZettaGraphRules::FanOutNodes(Graph);
		for (const FZettaPipelineNode& N : Graph.Nodes)
		{
			FNodeInfo Info;
			Info.Id = N.Id;
			Info.Kind = N.Kind;
			Info.Name = N.DisplayName();
			Info.Summary = ZettaNodeSettings::Summary(N);
			Info.Settings = SettingsBrief(N);
			if (const FZettaPipelineEdge* E = ZettaGraphRules::InputEdge(Graph, N.Id)) Info.From = E->Source + TEXT(".") + E->SourceHandle;
			if (!ZettaNodeCatalog::IsSeed(N.Kind) && ZettaNodeCatalog::IsKnown(N.Kind))
				Info.Price = ZettaRequestBuilder::PriceFor(N, Prices(), { ZettaGraphRules::IncomingType(Graph, N.Id), Gates() });
			Info.bPerPart = PerPart.Contains(N.Id);
			Info.bPerRunInput = ZettaNodeSettings::SetOnEachRun(N);
			Info.bReadOnly = ZettaNodeCatalog::NotAvailableHere(N.Kind);
			if (Info.bPerRunInput) D.PerRunInputs.Add(N.Id);
			D.Nodes.Add(MoveTemp(Info));
		}
		for (const FZettaPipelineProblem& P : ZettaGraphRules::Problems(Graph, Gates())) D.Problems.Add({ P.NodeId, P.Message });
		for (const FZettaPipelineNode& N : Graph.Nodes)
			for (const FString& W : ZettaGraphRules::Warnings(Graph, N.Id)) D.Warnings.Add(FString::Printf(TEXT("[%s] %s"), *N.Id, *W));
		const FZettaGraphEstimate Estimate = ZettaGraphRules::Estimate(Graph, Prices(), Gates());
		D.EstimateCredits = Estimate.Credits;
		D.bEstimatePerPart = Estimate.bPerPart;
	}

	// ─── list / get ──────────────────────────────────────────────────────────────────────────────────────────────────

	void List(TFunction<void(const FListResult&)> Done)
	{
		FString Error;
		if (!SignedIn(Error)) return Done(Fail<FListResult>(Error));
		ZettaPipelinesApi::List([Done](const FZettaResponse& R, const TArray<FZettaPipelineSummary>& List)
		{
			if (!R.bOk) return Done(Fail<FListResult>(ErrorText(R)));
			FListResult Out;
			Out.bOk = true;
			for (const FZettaPipelineSummary& P : List)
			{
				FPipelineInfo Info;
				Info.Id = P.Id;
				Info.Title = P.Title;
				Info.Steps = P.Steps;
				Info.LastRunId = P.LastRunId;
				Info.LastRunStatus = P.LastRunStatus;
				Info.LastRunAt = P.LastRunAt;
				Info.UpdatedAt = P.UpdatedAt;
				Info.RunCounts = P.RunCounts;
				Out.Pipelines.Add(MoveTemp(Info));
			}
			if (List.Num() == 0) Out.Text = TEXT("The user has no 3D pipelines yet. build_pipeline makes one.");
			else
			{
				Out.Text = Plural(List.Num(), TEXT("pipeline")) + TEXT(", newest first:\n");
				for (const FZettaPipelineSummary& P : List) Out.Text += PipelineLine(P) + TEXT("\n");
				Out.Text += TEXT("get_pipeline shows a pipeline's steps; run_pipeline runs it on seeds.");
			}
			Done(Out);
		});
	}

	void Get(int64 PipelineId, TFunction<void(const FPipelineDetails&)> Done)
	{
		FString Error;
		if (!SignedIn(Error)) return Done(Fail<FPipelineDetails>(Error));
		ZettaPipelinesApi::Get(PipelineId, [Done, PipelineId](const FZettaResponse& R, const FZettaPipeline& Pipeline)
		{
			if (!R.bOk) return Done(Fail<FPipelineDetails>(ErrorText(R, FString::Printf(TEXT("Pipeline #%lld was not found. Use list_pipelines for the ids."), PipelineId))));
			FZettaPipelinePlan::Get().WhenLoaded([Done, Pipeline]()
			{
				ZettaPipelinesApi::Runs(Pipeline.Id, [Done, Pipeline](const FZettaResponse& RunsR, const TArray<FZettaRunSummary>& Runs)
				{
					const FZettaPipelineGraph Graph = Recipe(Pipeline);
					FPipelineDetails D;
					D.bOk = true;
					FillDetails(D, Pipeline.Id, Pipeline.Title, Graph);
					int32 StepCount = 0;
					for (const FZettaPipelineNode& N : Graph.Nodes) if (!ZettaNodeCatalog::IsSeed(N.Kind)) ++StepCount;
					FString Text = FString::Printf(TEXT("Pipeline #%lld “%s” — starts from %s, %s, edited %s.\n"), Pipeline.Id, *Pipeline.Title,
						*StartsFromWords(Graph), *Plural(StepCount, TEXT("step")), *Ago(Pipeline.UpdatedAt, FDateTime::UtcNow()));
					Text += GraphReport(Graph, Prices(), Gates()) + TEXT("\n");
					if (D.PerRunInputs.Num() > 0) Text += TEXT("Per-run inputs (run_pipeline inputs): ") + FString::Join(D.PerRunInputs, TEXT(", ")) + TEXT(".\n");
					if (!RunsR.bOk) Text += TEXT("Recent runs: could not be read.");
					else if (Runs.Num() == 0) Text += TEXT("Recent runs: none.");
					else
					{
						Text += FString::Printf(TEXT("Recent runs (%d of %d):\n"), FMath::Min(5, Runs.Num()), Runs.Num());
						for (int32 I = 0; I < FMath::Min(5, Runs.Num()); ++I)
						{
							Text += TEXT("- ") + RunLine(Runs[I]) + TEXT("\n");
							D.RecentRuns.Add(RunInfoOf(Runs[I]));
						}
					}
					D.RunCount = Runs.Num();
					D.Text = Text.TrimEnd();
					Done(D);
				});
			});
		});
	}

	// ─── build / edit ────────────────────────────────────────────────────────────────────────────────────────────────

	void Build(const FBuildRequest& Request, TFunction<void(const FSaveResult&)> Done)
	{
		FString Error;
		if (!SignedIn(Error)) return Done(Fail<FSaveResult>(Error));
		const FString Title = Request.Title.TrimStartAndEnd();
		if (Title.IsEmpty()) return Done(Fail<FSaveResult>(TEXT("'title' is required (≤200 characters).")));
		if (Title.Len() > MaxTitle) return Done(Fail<FSaveResult>(FString::Printf(TEXT("'title' is longer than %d characters."), MaxTitle)));
		const FString StartsFrom = Request.StartsFrom.TrimStartAndEnd().ToLower();
		if (StartsFrom != TEXT("picture") && StartsFrom != TEXT("model")) return Done(Fail<FSaveResult>(TEXT("'startsFrom' must be picture or model.")));
		if (Request.Steps.Num() == 0) return Done(Fail<FSaveResult>(TEXT("'steps' is required: 1–24 of { kind, after?, output?, settings? }.")));

		FZettaPipelinePlan::Get().WhenLoaded([Request, Title, StartsFrom, Done]()
		{
			TArray<FZettaBuildStep> Steps;
			for (const FBuildStep& S : Request.Steps) Steps.Add({ S.Kind, S.After, S.Output, S.Settings });
			const ZettaGraphEditing::FBuilt Built = ZettaGraphEditing::Build(Title, StartsFrom, Steps, Gates());
			bool bAny = false;
			for (const FString& Id : Built.StepNodeIds) bAny |= !Id.IsEmpty();
			if (!bAny)
			{
				TArray<FString> Why;
				for (const TPair<int32, FString>& S : Built.Skipped) Why.Add(FString::Printf(TEXT("steps[%d]: %s"), S.Key, *S.Value));
				return Done(Fail<FSaveResult>(TEXT("None of the steps could be added: ") + FString::Join(Why, TEXT("; "))));
			}
			ZettaPipelinesApi::Create(Built.Title, &Built.Graph, [Request, Built, StartsFrom, Steps, Done](const FZettaResponse& R, const FZettaPipeline& Pipeline)
			{
				if (!R.bOk) return Done(Fail<FSaveResult>(ErrorText(R)));
				const FZettaPipelineGraph Graph = Pipeline.Graph.Nodes.Num() > 0 ? Recipe(Pipeline) : Built.Graph;
				ZettaPipelineHooks::OnSavedElsewhere().Broadcast(Pipeline.Id);
				FSaveResult Out;
				Out.bOk = true;
				Out.PipelineId = Pipeline.Id;
				Out.NodeIds = Built.StepNodeIds;
				if (Request.bOpen)
				{
					ZettaPipelineHooks::RequestOpen(Pipeline.Id, {});
					Out.bOpened = true;
				}
				FString Text = FString::Printf(TEXT("Created pipeline #%lld “%s” (starts from a %s)."), Pipeline.Id, *Pipeline.Title, *StartsFrom);
				Text += Out.bOpened ? TEXT(" Opened on the Pipelines page of the Zetta 3D window.\n") : TEXT("\n");
				TArray<FString> Ids;
				for (int32 I = 0; I < Built.StepNodeIds.Num(); ++I)
					Ids.Add(FString::Printf(TEXT("[%d] %s → %s"), I, *Steps[I].Kind, Built.StepNodeIds[I].IsEmpty() ? TEXT("skipped") : *Built.StepNodeIds[I]));
				Text += TEXT("Node ids by step: ") + FString::Join(Ids, TEXT(", ")) + TEXT("\n");
				for (const TPair<int32, FString>& S : Built.Skipped)
				{
					Text += FString::Printf(TEXT("Skipped steps[%d]: %s\n"), S.Key, *S.Value);
					Out.Skipped.Add(FString::Printf(TEXT("steps[%d]: %s"), S.Key, *S.Value));
				}
				for (int32 I = 0; I < Steps.Num() && I < Built.StepNodeIds.Num(); ++I)
				{
					const TOptional<int32>& A = Steps[I].After;
					if (A.IsSet() && Built.StepNodeIds.IsValidIndex(A.GetValue()) && Built.StepNodeIds[A.GetValue()].IsEmpty() && !Built.StepNodeIds[I].IsEmpty())
						Text += FString::Printf(TEXT("steps[%d] follows the seed because steps[%d] was skipped.\n"), I, A.GetValue());
				}
				Text += GraphReport(Graph, Prices(), Gates());
				Out.Text = Text;
				FillDetails(Out.Pipeline, Pipeline.Id, Pipeline.Title, Graph);
				Out.Pipeline.bOk = true;
				Done(Out);
			});
		});
	}

	void Edit(const FEditRequest& Request, TFunction<void(const FSaveResult&)> Done)
	{
		FString Error;
		if (!SignedIn(Error)) return Done(Fail<FSaveResult>(Error));
		TOptional<FString> Title;
		if (Request.Title.IsSet())
		{
			Title = Request.Title->TrimStartAndEnd();
			if (Title->IsEmpty()) return Done(Fail<FSaveResult>(TEXT("'title' may not be empty.")));
			if (Title->Len() > MaxTitle) return Done(Fail<FSaveResult>(FString::Printf(TEXT("'title' is longer than %d characters."), MaxTitle)));
		}
		if (!Title.IsSet() && Request.Changes.Num() == 0)
			return Done(Fail<FSaveResult>(TEXT("'changes' is required: 1–24 of { op: \"add\", kind, after, output?, settings? } | { op: \"set\", nodeId, settings } | { op: \"remove\", nodeId }.")));
		const int64 Id = Request.PipelineId;
		ZettaPipelinesApi::Get(Id, [Request, Title, Id, Done](const FZettaResponse& R, const FZettaPipeline& Pipeline)
		{
			if (!R.bOk) return Done(Fail<FSaveResult>(ErrorText(R, FString::Printf(TEXT("Pipeline #%lld was not found. Use list_pipelines for the ids."), Id))));
			const FZettaPipelineGraph Graph = Recipe(Pipeline);
			// A set on a step this plugin cannot change is refused before anything is applied.
			for (int32 I = 0; I < Request.Changes.Num(); ++I)
			{
				const FChange& C = Request.Changes[I];
				const FZettaPipelineNode* Node = C.Op == TEXT("set") ? Graph.Node(C.NodeId) : nullptr;
				if (Node && ZettaNodeCatalog::NotAvailableHere(Node->Kind))
					return Done(Fail<FSaveResult>(FString::Printf(TEXT("changes[%d]: %s (%s) is not available in Unreal — change it on zetta3d.ai."), I, *C.NodeId, *Node->DisplayName())));
			}
			FZettaPipelinePlan::Get().WhenLoaded([Request, Title, Id, Pipeline, Graph, Done]()
			{
				TArray<FZettaPipelineChange> Changes;
				for (const FChange& C : Request.Changes) Changes.Add({ C.Op, C.Kind, C.After, C.Output, C.NodeId, C.Settings });
				const ZettaGraphEditing::FAgentChanges Applied = ZettaGraphEditing::ApplyAgentChanges(Graph, Changes, Gates());
				const bool bChanged = Changes.Num() > Applied.Skipped.Num();
				if (!bChanged && !Title.IsSet())
				{
					TArray<FString> Why;
					for (const TPair<int32, FString>& S : Applied.Skipped) Why.Add(FString::Printf(TEXT("changes[%d]: %s"), S.Key, *S.Value));
					return Done(Fail<FSaveResult>(TEXT("Nothing was changed: ") + FString::Join(Why, TEXT("; "))));
				}
				ZettaPipelinesApi::Save(Id, Applied.Graph, Title, Gates(), [Applied, Changes, Title, Id, Pipeline, Done](const FZettaResponse& R, const FZettaPipeline& Saved)
				{
					if (!R.bOk) return Done(Fail<FSaveResult>(ErrorText(R, FString::Printf(TEXT("Pipeline #%lld was not found."), Id))));
					ZettaPipelineHooks::OnSavedElsewhere().Broadcast(Id);
					const FZettaPipelineGraph Result = Saved.Graph.Nodes.Num() > 0 ? Recipe(Saved) : Applied.Graph;
					FSaveResult Out;
					Out.bOk = true;
					Out.PipelineId = Id;
					Out.NodeIds = Applied.Added;
					const FString ShownTitle = !Saved.Title.IsEmpty() ? Saved.Title : Title.IsSet() ? Title.GetValue() : Pipeline.Title;
					FString Text = FString::Printf(TEXT("Saved pipeline #%lld “%s”."), Id, *ShownTitle);
					Text += Title.IsSet() ? TEXT(" Renamed.\n") : TEXT("\n");
					if (Applied.Added.Num() > 0)
					{
						TArray<int32> AddIndexes;
						for (int32 I = 0; I < Changes.Num(); ++I)
						{
							bool bSkipped = false;
							for (const TPair<int32, FString>& S : Applied.Skipped) bSkipped |= S.Key == I;
							if (Changes[I].Op == TEXT("add") && !bSkipped) AddIndexes.Add(I);
						}
						TArray<FString> Lines;
						for (int32 N = 0; N < Applied.Added.Num(); ++N)
						{
							Lines.Add(N < AddIndexes.Num()
								? FString::Printf(TEXT("changes[%d] %s → %s"), AddIndexes[N], *Changes[AddIndexes[N]].Kind, *Applied.Added[N])
								: Applied.Added[N]);
						}
						Text += TEXT("Added: ") + FString::Join(Lines, TEXT(", ")) + TEXT("\n");
					}
					for (const TPair<int32, FString>& S : Applied.Skipped)
					{
						Text += FString::Printf(TEXT("Skipped changes[%d]: %s\n"), S.Key, *S.Value);
						Out.Skipped.Add(FString::Printf(TEXT("changes[%d]: %s"), S.Key, *S.Value));
					}
					Text += GraphReport(Result, Prices(), Gates());
					Out.Text = Text;
					FillDetails(Out.Pipeline, Id, ShownTitle, Result);
					Out.Pipeline.bOk = true;
					Done(Out);
				});
			});
		});
	}

	// ─── run ─────────────────────────────────────────────────────────────────────────────────────────────────────────

	static bool IsPictureFile(const FString& Path)
	{
		const FString Ext = FPaths::GetExtension(Path).ToLower();
		return Ext == TEXT("png") || Ext == TEXT("jpg") || Ext == TEXT("jpeg");
	}

	static bool IsModelFile(const FString& Path) { return FPaths::GetExtension(Path).ToLower() == TEXT("glb"); }

	static FString ObjectPathOf(const FString& ContentPath)
	{
		if (ContentPath.Contains(TEXT("."))) return ContentPath;
		return ContentPath + TEXT(".") + FPaths::GetBaseFilename(ContentPath);
	}

	static FString AbsoluteFile(const FString& Path)
	{
		return FPaths::IsRelative(Path) ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path) : Path;
	}

	/** What a seed's asset path is: "picture", "model", or empty with an error. */
	static FString KindOfAsset(const FString& Path, FString& OutError)
	{
		if (Path.StartsWith(TEXT("/")) && !FPaths::FileExists(Path))
		{
			if (ZettaPipelineAssets::IsModelSource(Path)) return TEXT("model");
			FAssetData Asset;
			if (IAssetRegistry* Registry = IAssetRegistry::Get())
				Asset = Registry->GetAssetByObjectPath(FSoftObjectPath(ObjectPathOf(Path)));
			if (!Asset.IsValid())
			{
				OutError = FString::Printf(TEXT("%s does not exist in the project."), *Path);
				return FString();
			}
			if (Asset.IsInstanceOf(UTexture::StaticClass())) return TEXT("picture");
			OutError = FString::Printf(TEXT("%s is neither a picture (a texture) nor a model imported from Zetta 3D."), *Path);
			return FString();
		}
		const FString File = AbsoluteFile(Path);
		if (!FPaths::FileExists(File))
		{
			OutError = FString::Printf(TEXT("%s does not exist."), *Path);
			return FString();
		}
		if (IsPictureFile(File)) return TEXT("picture");
		if (IsModelFile(File)) return TEXT("model");
		OutError = FString::Printf(TEXT("%s must be a .png, .jpg or .jpeg picture or a .glb model."), *Path);
		return FString();
	}

	static FString SeedLabel(const FSeed& S)
	{
		if (S.ImageId.IsSet()) return FString::Printf(TEXT("picture #%lld"), S.ImageId.GetValue());
		if (S.GenerationId.IsSet()) return FString::Printf(TEXT("model #%lld"), S.GenerationId.GetValue());
		return S.AssetPath;
	}

	/** The settings change a per-run input makes to its step (what RunNodeChange takes). */
	static bool InputPatch(const FZettaPipelineNode* Node, const FRunInput& Input, TSharedPtr<FJsonObject>& OutPatch, FString& OutError)
	{
		if (!Node)
		{
			OutError = FString::Printf(TEXT("inputs[\"%s\"]: the pipeline has no step %s — see get_pipeline for node ids."), *Input.NodeId, *Input.NodeId);
			return false;
		}
		const FString Name = FString::Printf(TEXT("%s (%s)"), *Input.NodeId, *Node->DisplayName());
		if (!ZettaNodeSettings::SetOnEachRun(*Node))
		{
			OutError = Node->Kind == ZettaNodeKinds::AiEdit
				? FString::Printf(TEXT("inputs[\"%s\"]: %s is not set on each run — change its prompt with edit_pipeline (or set askAtRun: true)."), *Input.NodeId, *Name)
				: FString::Printf(TEXT("inputs[\"%s\"]: %s takes no per-run input (only AI texturing and AI edit with askAtRun do)."), *Input.NodeId, *Name);
			return false;
		}
		TSharedRef<FJsonObject> Patch = MakeShared<FJsonObject>();
		if (Node->Kind == ZettaNodeKinds::AiEdit)
		{
			if (Input.ImageId.IsSet())
			{
				OutError = FString::Printf(TEXT("inputs[\"%s\"]: %s takes a prompt (the instruction), not an imageId."), *Input.NodeId, *Name);
				return false;
			}
			Patch->SetStringField(TEXT("prompt"), Input.Prompt);
		}
		else if (Input.ImageId.IsSet() && !Input.Prompt.IsEmpty())
		{
			OutError = FString::Printf(TEXT("inputs[\"%s\"]: give %s a prompt or an imageId, not both — a reference picture replaces the description."), *Input.NodeId, *Name);
			return false;
		}
		else if (Input.ImageId.IsSet())
		{
			Patch->SetStringField(TEXT("prompt"), TEXT(""));
			TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
			Reference->SetNumberField(TEXT("gen2dPostId"), (double)Input.ImageId.GetValue());
			Reference->SetStringField(TEXT("name"), FString::Printf(TEXT("Picture #%lld"), Input.ImageId.GetValue()));
			Patch->SetObjectField(TEXT("reference"), Reference);
		}
		else
		{
			Patch->SetStringField(TEXT("prompt"), Input.Prompt);
			Patch->SetField(TEXT("reference"), JNull());
		}
		OutPatch = Patch;
		return true;
	}

	/** A run's graph with the per-run inputs applied, the way the editor sends them. */
	static bool WithInputs(const FZettaPipelineGraph& RunGraph, const TArray<FRunInput>& Inputs, FZettaPipelineGraph& Out, FString& OutError)
	{
		Out = RunGraph;
		for (const FRunInput& Input : Inputs)
		{
			TSharedPtr<FJsonObject> Patch, Settings, Request;
			if (!InputPatch(Out.Node(Input.NodeId), Input, Patch, OutError)) return false;
			if (!ZettaRequestBuilder::RunNodeChange(Out, Input.NodeId, Patch, Gates(), Settings, Request, OutError)) return false;
			FZettaPipelineNode* Node = Out.Node(Input.NodeId);
			Node->Settings = Settings;
			Node->Request = Request;
		}
		return true;
	}

	/** What a run of this recipe would still need after the inputs — one line per step (empty = every step has its input). */
	static bool MissingInputs(const FZettaPipelineGraph& Recipe, const TArray<FRunInput>& Inputs, TArray<FString>& OutMissing, FString& OutError)
	{
		const FZettaPipelineGraph RunGraph = ZettaRequestBuilder::WithRequests(ZettaRequestBuilder::ForNewRun(Recipe), Gates());
		FZettaPipelineGraph Applied;
		if (!WithInputs(RunGraph, Inputs, Applied, OutError)) return false;
		for (const FZettaPipelineProblem& P : ZettaGraphRules::RunInputProblems(Applied))
		{
			const FZettaPipelineNode* Node = Applied.Node(P.NodeId);
			FString What;
			if (Node && Node->Kind == ZettaNodeKinds::Texture)
				What = FString::Printf(TEXT("inputs[\"%s\"].prompt (a texture description) or inputs[\"%s\"].imageId (a reference picture)"), *P.NodeId, *P.NodeId);
			else if (Node && Node->Kind == ZettaNodeKinds::TextureMultiview)
				What = TEXT("its pictures of each side — those are given on the Pipelines page (the user opens the run there), or wire a Multi-view seed or Draw views into its views input");
			else What = FString::Printf(TEXT("inputs[\"%s\"].prompt (how to change the picture)"), *P.NodeId);
			OutMissing.Add(FString::Printf(TEXT("%s (%s): give %s"), *P.NodeId, Node ? *Node->DisplayName() : TEXT("step"), *What));
		}
		return true;
	}

	/** Everything run_pipeline checks before the card: the pipeline, its problems, its inputs, every seed. */
	struct FPreparedRun
	{
		FString Error;
		FZettaPipeline Pipeline;
		FZettaPipelineGraph Recipe;
		FZettaGraphEstimate Estimate;
		TMap<int32, FZettaGenerationPtr> Models;
		FString Mode;
	};

	static void PrepareRun(const FRunRequest& Request, TFunction<void(const TSharedRef<FPreparedRun>&)> Done)
	{
		TSharedRef<FPreparedRun> P = MakeShared<FPreparedRun>();
		auto Stop = [P, Done](const FString& Error)
		{
			P->Error = Error;
			Done(P);
		};
		FString Error;
		if (!SignedIn(Error)) return Stop(Error);
		P->Mode = Request.Mode.TrimStartAndEnd().ToLower();
		if (P->Mode.IsEmpty()) P->Mode = ZettaRunStatus::ModeManual;
		if (P->Mode != ZettaRunStatus::ModeManual && P->Mode != ZettaRunStatus::ModeAuto) return Stop(TEXT("'mode' must be manual or auto."));
		if (Request.Seeds.Num() == 0) return Stop(TEXT("'seeds' is required: 1–20 of { imageId } | { generationId } | { assetPath }."));
		if (Request.Seeds.Num() > MaxSeeds)
			return Stop(FString::Printf(TEXT("At most %d seeds per call (%d given) — split them over several run_pipeline calls."), MaxSeeds, Request.Seeds.Num()));

		const int64 Id = Request.PipelineId;
		ZettaPipelinesApi::Get(Id, [Request, P, Stop, Done, Id](const FZettaResponse& R, const FZettaPipeline& Pipeline)
		{
			if (!R.bOk) return Stop(ErrorText(R, FString::Printf(TEXT("Pipeline #%lld was not found. Use list_pipelines for the ids."), Id)));
			P->Pipeline = Pipeline;
			P->Recipe = Recipe(Pipeline);
			const FZettaPipelineNode* SeedNode = P->Recipe.Seed();
			if (!SeedNode) return Stop(FString::Printf(TEXT("Pipeline #%lld has no seed step — rebuild it with build_pipeline."), Id));
			// Pictures of each side are given per run on the Pipelines page — one set per run, front required.
			if (SeedNode->Kind == ZettaNodeKinds::SeedViews)
				return Stop(TEXT("This pipeline starts from pictures of each side (a Multi-view seed). Those are given per run on the Pipelines page — ask the user to press New run there and add each set; run_pipeline cannot give them."));
			const FString Wants = SeedNode->Kind == ZettaNodeKinds::SeedModel ? TEXT("model") : TEXT("picture");

			// The seeds' types, before anything else.
			TArray<FString> Kinds;
			for (int32 I = 0; I < Request.Seeds.Num(); ++I)
			{
				const FSeed& S = Request.Seeds[I];
				FString Kind = S.GenerationId.IsSet() ? TEXT("model") : S.ImageId.IsSet() ? TEXT("picture") : FString();
				if (Kind.IsEmpty())
				{
					FString AssetError;
					Kind = KindOfAsset(S.AssetPath, AssetError);
					if (Kind.IsEmpty()) return Stop(FString::Printf(TEXT("seeds[%d]: %s"), I, *AssetError));
				}
				if (Kind != Wants)
				{
					return Stop(Wants == TEXT("picture")
						? FString::Printf(TEXT("seeds[%d] (%s) is a model, but this pipeline starts from a picture — give an imageId or a picture assetPath."), I, *SeedLabel(S))
						: FString::Printf(TEXT("seeds[%d] (%s) is a picture, but this pipeline starts from a model — give a generationId or a model assetPath."), I, *SeedLabel(S)));
				}
				Kinds.Add(Kind);
			}

			FZettaPipelinePlan::Get().WhenLoaded([Request, P, Stop, Done, Id]()
			{
				const TArray<FZettaPipelineProblem> Problems = ZettaGraphRules::Problems(P->Recipe, Gates());
				if (Problems.Num() > 0)
				{
					TArray<FString> Lines;
					for (const FZettaPipelineProblem& Problem : Problems) Lines.Add(Problem.ToString());
					return Stop(FString::Printf(TEXT("Pipeline #%lld cannot run yet — fix it with edit_pipeline: "), Id) + FString::Join(Lines, TEXT(" ")));
				}
				TArray<FString> Missing;
				FString InputError;
				if (!MissingInputs(P->Recipe, Request.Inputs, Missing, InputError)) return Stop(InputError);
				if (Missing.Num() > 0) return Stop(TEXT("Every run needs these per-run inputs: ") + FString::Join(Missing, TEXT("; ")) + TEXT("."));
				P->Estimate = ZettaGraphRules::Estimate(P->Recipe, Prices(), Gates());

				// Library models are read now: only finished meshes can seed a run.
				TArray<int32> ModelIndexes;
				for (int32 I = 0; I < Request.Seeds.Num(); ++I) if (Request.Seeds[I].GenerationId.IsSet()) ModelIndexes.Add(I);
				TSharedRef<int32> Left = MakeShared<int32>(ModelIndexes.Num());
				TSharedRef<FString> FirstError = MakeShared<FString>();
				if (ModelIndexes.Num() == 0) return Done(P);
				for (const int32 Index : ModelIndexes)
				{
					const int64 GenId = Request.Seeds[Index].GenerationId.GetValue();
					ZettaApi::GetGeneration(GenId, [P, Done, Left, FirstError, Index, GenId](const FZettaResponse& R, const FZettaGenerationPtr& Generation, TOptional<int32>)
					{
						if (FirstError->IsEmpty())
						{
							if (!R.bOk || !Generation.IsValid())
								*FirstError = FString::Printf(TEXT("seeds[%d]: %s"), Index, *ErrorText(R, FString::Printf(TEXT("#%lld is not in the user's library (list_generations has the ids)."), GenId)));
							else if (Generation->IsSplat())
								*FirstError = FString::Printf(TEXT("seeds[%d]: #%lld is a Gaussian splat — pipelines in Unreal start from a mesh."), Index, GenId);
							else if (!Generation->IsSuccess())
								*FirstError = FString::Printf(TEXT("seeds[%d]: #%lld is %s — only finished models can seed a run."), Index, GenId, *Generation->StatusLabel().ToLower());
							else P->Models.Add(Index, Generation);
						}
						if (--*Left > 0) return;
						P->Error = *FirstError;
						Done(P);
					});
				}
			});
		});
	}

	static FString ApprovalDetail(const FRunRequest& Request, const FPreparedRun& P)
	{
		FString Out;
		const FZettaPipelineGraph& Recipe = P.Recipe;
		Out += FString::Printf(TEXT("Pipeline #%lld “%s” — starts from %s\n"), P.Pipeline.Id, *P.Pipeline.Title, *StartsFromWords(Recipe));
		TArray<FString> Order;
		if (TOptional<TArray<FString>> Topo = ZettaGraphRules::TopologicalOrder(Recipe)) Order = Topo.GetValue();
		else for (const FZettaPipelineNode& N : Recipe.Nodes) Order.Add(N.Id);
		const TSet<FString> PerPart = ZettaGraphRules::FanOutNodes(Recipe);
		Out += TEXT("Steps:\n");
		for (const FString& NodeId : Order)
		{
			const FZettaPipelineNode* Node = Recipe.Node(NodeId);
			if (!Node || ZettaNodeCatalog::IsSeed(Node->Kind)) continue;
			const TOptional<int32> Price = ZettaRequestBuilder::PriceFor(*Node, Prices(), { ZettaGraphRules::IncomingType(Recipe, NodeId), Gates() });
			const FString Summary = ZettaNodeSettings::Summary(*Node);
			Out += FString::Printf(TEXT("• %s%s (%s)\n"), *Node->DisplayName(), Summary.IsEmpty() ? TEXT("") : *(TEXT(" — ") + Summary), *ZettaRequestBuilder::PriceLabel(Price, PerPart.Contains(NodeId)));
		}
		TArray<FString> Shown;
		for (int32 I = 0; I < FMath::Min(6, Request.Seeds.Num()); ++I) Shown.Add(SeedLabel(Request.Seeds[I]));
		Out += FString::Printf(TEXT("Seeds (%d): %s%s\n"), Request.Seeds.Num(), *FString::Join(Shown, TEXT(", ")),
			Request.Seeds.Num() > Shown.Num() ? *FString::Printf(TEXT(", +%d more"), Request.Seeds.Num() - Shown.Num()) : TEXT(""));
		for (const FRunInput& Input : Request.Inputs)
		{
			const FZettaPipelineNode* Node = Recipe.Node(Input.NodeId);
			const FString Given = Input.ImageId.IsSet() ? FString::Printf(TEXT("reference picture #%lld"), Input.ImageId.GetValue()) : FString::Printf(TEXT("“%s”"), *Short(Input.Prompt, 120));
			Out += FString::Printf(TEXT("Per-run input — %s: %s\n"), Node ? *Node->DisplayName() : *Input.NodeId, *Given);
		}
		Out += P.Mode == ZettaRunStatus::ModeAuto
			? TEXT("Mode: Auto — every step submits itself, to the end.\n")
			: TEXT("Mode: Manual — waits for you before each step (credits are spent only when you run a step).\n");
		if (!P.Estimate.Credits.IsSet()) Out += TEXT("Cost: unknown (prices not loaded).");
		else
		{
			const int32 Each = P.Estimate.Credits.GetValue();
			const FString Up = P.Estimate.bPerPart || P.Mode != ZettaRunStatus::ModeAuto ? TEXT("up to ") : TEXT("");
			Out += Request.Seeds.Num() == 1
				? FString::Printf(TEXT("Cost: %s%d credits."), *Up, Each)
				: FString::Printf(TEXT("Cost: %s%d credits a run × %d = %s%d credits."), *Up, Each, Request.Seeds.Num(), *Up, Each * Request.Seeds.Num());
		}
		if (Request.bImportResults) Out += TEXT("\nResults import to the models folder when each run finishes.");
		return Out;
	}

	void DescribeRun(const FRunRequest& Request, TFunction<void(const FApproval&)> Done)
	{
		PrepareRun(Request, [Request, Done](const TSharedRef<FPreparedRun>& P)
		{
			FApproval A;
			A.bNeeded = true;
			A.Kind = TEXT("generate");
			if (!P->Error.IsEmpty())
			{
				A.Error = P->Error;
				A.Title = TEXT("Run pipeline");
				return Done(A);
			}
			A.Title = FString::Printf(TEXT("Run pipeline: %s on %s"), *P->Pipeline.Title, *Plural(Request.Seeds.Num(), TEXT("seed")));
			A.Body = ApprovalDetail(Request, *P);
			if (P->Estimate.Credits.IsSet()) A.Credits = P->Estimate.Credits.GetValue() * FMath::Max(1, Request.Seeds.Num());
			A.ConfirmLabel = Request.Seeds.Num() == 1 ? FString(TEXT("Run")) : FString::Printf(TEXT("Run %d"), Request.Seeds.Num());
			Done(A);
		});
	}

	/** Runs Body(i, Next) for i = 0..Count-1, one after another, then Finished. */
	struct FSequence : TSharedFromThis<FSequence>
	{
		int32 Index = 0;
		int32 Count = 0;
		TFunction<void(int32, TFunction<void()>)> Body;
		TFunction<void()> Finished;

		static void Run(int32 InCount, TFunction<void(int32, TFunction<void()>)> InBody, TFunction<void()> InFinished)
		{
			TSharedRef<FSequence> S = MakeShared<FSequence>();
			S->Count = InCount;
			S->Body = MoveTemp(InBody);
			S->Finished = MoveTemp(InFinished);
			S->Next();
		}

		void Next()
		{
			if (Index >= Count)
			{
				TFunction<void()> Then = MoveTemp(Finished);
				Body = nullptr;
				if (Then) Then();
				return;
			}
			const int32 I = Index++;
			TSharedRef<FSequence> Self = AsShared();
			Body(I, [Self]() { Self->Next(); });
		}
	};

	void Run(const FRunRequest& Request, TFunction<void(const FRunResult&)> Done)
	{
		PrepareRun(Request, [Request, Done](const TSharedRef<FPreparedRun>& P)
		{
			if (!P->Error.IsEmpty()) return Done(Fail<FRunResult>(P->Error));
			struct FState
			{
				TArray<TSharedPtr<FJsonObject>> Seeds;
				FString Error;
			};
			TSharedRef<FState> State = MakeShared<FState>();
			State->Seeds.SetNum(Request.Seeds.Num());

			// The seeds, uploaded now (nothing was uploaded before the user agreed).
			FSequence::Run(Request.Seeds.Num(), [Request, P, State](int32 I, TFunction<void()> Next)
			{
				if (!State->Error.IsEmpty()) return Next();
				const FSeed& S = Request.Seeds[I];
				auto Use = [State, I, Next](const FZettaPipelineSeed& Seed)
				{
					State->Seeds[I] = Seed.Request;
					Next();
				};
				auto Stop = [State, I, Next, S](const FString& Error)
				{
					State->Error = FString::Printf(TEXT("seeds[%d] (%s): %s Nothing was run."), I, *SeedLabel(S), *Error);
					Next();
				};
				if (S.ImageId.IsSet()) return Use(FZettaPipelineSeed::FromImagePost(S.ImageId.GetValue()));
				if (S.GenerationId.IsSet()) return Use(FZettaPipelineSeed::FromModel(*P->Models.FindRef(I)));
				FString KindError;
				const FString Kind = KindOfAsset(S.AssetPath, KindError);
				if (Kind == TEXT("model"))
				{
					ZettaPipelineAssets::ModelFromAsset(S.AssetPath, [Use, Stop](const FZettaGenerationPtr& Row, const FString& Error)
					{
						if (!Row.IsValid()) return Stop(Error);
						Use(FZettaPipelineSeed::FromModel(*Row));
					});
					return;
				}
				TArray<uint8> Bytes;
				FString Type, Name, Error;
				if (S.AssetPath.StartsWith(TEXT("/")) && !FPaths::FileExists(S.AssetPath))
				{
					if (!ZettaPipelineAssets::PictureOfTexture(ObjectPathOf(S.AssetPath), Bytes, Type, Name, Error)) return Stop(Error);
				}
				else
				{
					const FString File = AbsoluteFile(S.AssetPath);
					if (!FFileHelper::LoadFileToArray(Bytes, *File)) return Stop(TEXT("Could not read the file."));
					Type = FPaths::GetExtension(File).ToLower() == TEXT("png") ? TEXT("image/png") : TEXT("image/jpeg");
					Name = FPaths::GetCleanFilename(File);
				}
				FZettaPipelineSeed::FromUpload(Bytes, Type, Name, [Use, Stop](const FZettaResponse& R, const FZettaPipelineSeed& Seed)
				{
					if (!R.bOk) return Stop(ZettaCatalog::Explain(R));
					Use(Seed);
				});
			}, [Request, P, State, Done]()
			{
				if (!State->Error.IsEmpty()) return Done(Fail<FRunResult>(State->Error));
				// Drafts first, so each gets its inputs before it starts (the web's New run).
				const FZettaPipelineGraph RunGraph = ZettaRequestBuilder::WithRequests(ZettaRequestBuilder::ForNewRun(P->Recipe), Gates());
				struct FPatch
				{
					FString NodeId;
					TSharedPtr<FJsonObject> Settings, Request;
				};
				TArray<FPatch> Patches;
				for (const FRunInput& Input : Request.Inputs)
				{
					TSharedPtr<FJsonObject> Patch;
					FPatch Out;
					Out.NodeId = Input.NodeId;
					FString Error;
					if (!InputPatch(RunGraph.Node(Input.NodeId), Input, Patch, Error)
						|| !ZettaRequestBuilder::RunNodeChange(RunGraph, Input.NodeId, Patch, Gates(), Out.Settings, Out.Request, Error))
					{
						return Done(Fail<FRunResult>(Error));
					}
					Patches.Add(MoveTemp(Out));
				}
				const int64 PipelineId = P->Pipeline.Id;
				ZettaPipelinesApi::Batch(PipelineId, P->Mode, State->Seeds, &RunGraph, true,
					[Request, P, Patches, PipelineId, Done](const FZettaResponse& R, const TArray<FZettaRunSummary>& Created)
				{
					if (!R.bOk) return Done(Fail<FRunResult>(ErrorText(R, FString::Printf(TEXT("Pipeline #%lld was not found."), PipelineId))));
					TSharedRef<FRunResult> Result = MakeShared<FRunResult>();
					FSequence::Run(Created.Num(), [Request, Patches, Created, PipelineId, Result](int32 I, TFunction<void()> Next)
					{
						const int64 RunId = Created[I].Id;
						auto NotStarted = [Result, RunId, Next](const FString& Why)
						{
							Result->NotStarted.Add(FString::Printf(TEXT("#%lld: %s"), RunId, *Why));
							Next();
						};
						// Each run's inputs, then Start.
						// A patch that did not take shows up as Start's refusal (RUN_NEEDS_INPUT), naming the step.
						FSequence::Run(Patches.Num(), [Patches, RunId](int32 PatchIndex, TFunction<void()> NextPatch)
						{
							const FPatch& Patch = Patches[PatchIndex];
							ZettaPipelinesApi::PatchRunNode(RunId, Patch.NodeId, Patch.Request, Patch.Settings, [NextPatch](const FZettaResponse&, const FZettaPipelineRunPtr&) { NextPatch(); });
						}, [Request, RunId, PipelineId, NotStarted, Result, Next]()
						{
							ZettaPipelinesApi::Start(RunId, [Request, RunId, PipelineId, NotStarted, Result, Next](const FZettaResponse& SR, const FZettaPipelineRunPtr& Live)
							{
								if (!SR.bOk) return NotStarted(ErrorText(SR));
								Result->RunIds.Add(RunId);
								FZettaRunFollower::Get().Follow(RunId, PipelineId, Request.AgentSessionId,
									Request.bImportResults ? EZettaRunImport::FinalSteps : EZettaRunImport::None, ModelsFolder());
								if (Live.IsValid()) FZettaRunFollower::Get().Report(Live);
								Next();
							});
						});
					}, [Request, P, Created, Result, Done]()
					{
						FRunResult Out = *Result;
						if (Out.RunIds.Num() == 0 && Out.NotStarted.Num() > 0)
							return Done(Fail<FRunResult>(TEXT("The runs were created as drafts but none started: ") + FString::Join(Out.NotStarted, TEXT("; "))));
						Out.bOk = true;
						TArray<FString> Ids;
						for (const int64 Id : Out.RunIds) Ids.Add(FString::Printf(TEXT("#%lld"), Id));
						FString Text = FString::Printf(TEXT("Started %s of pipeline #%lld “%s”: %s"), *Plural(Out.RunIds.Num(), TEXT("run")), P->Pipeline.Id, *P->Pipeline.Title, *FString::Join(Ids, TEXT(", ")));
						FString Each = EstimateText(P->Estimate);
						Each.RemoveFromEnd(TEXT("."));
						Text += FString::Printf(TEXT(" — %s mode, %s each"), *ZettaRunStatus::ModeLabel(P->Mode), *Each.ToLower());
						if (P->Estimate.Credits.IsSet()) Text += FString::Printf(TEXT(" (up to %d credits in all)"), P->Estimate.Credits.GetValue() * FMath::Max(1, Request.Seeds.Num()));
						Text += TEXT(".\n");
						if (Out.NotStarted.Num() > 0) Text += TEXT("Not started (left as drafts): ") + FString::Join(Out.NotStarted, TEXT("; ")) + TEXT("\n");
						Text += P->Mode == ZettaRunStatus::ModeManual
							? TEXT("Manual: each paid step waits for the user — pipeline_run_status lists them; manage_pipeline_run run_step / run_waiting runs them (with a card). ")
							: TEXT("Auto: every step submits itself. ");
						if (Request.bImportResults) Text += FString::Printf(TEXT("Finished models import to %s/. "), *ModelsFolder());
						Text += TEXT("A pipeline_run_finished event arrives when a run finishes or needs the user.");
						Out.Text = Text;
						Done(Out);
					});
				});
			});
		});
	}

	// ─── status / manage ─────────────────────────────────────────────────────────────────────────────────────────────

	static FRunStatus StatusOf(const FZettaPipelineRun& Run)
	{
		FRunStatus S;
		S.bOk = true;
		S.RunId = Run.Id;
		S.PipelineId = Run.PipelineId;
		S.Status = Run.Status;
		S.Mode = Run.Mode;
		S.bPaused = Run.bPaused;
		S.bLive = Run.IsLive();
		S.SpentCredits = Run.SpentCredits;
		S.EstimatedCredits = Run.EstimatedCredits;
		for (const FZettaPipelineRunStep& Step : Run.Steps)
		{
			FStepInfo I;
			I.Id = Step.Id;
			I.NodeId = Step.NodeId;
			const FZettaPipelineNode* Node = Run.Graph.Node(Step.NodeId);
			I.Name = Step.NodeId + TEXT(" ") + (Node ? Node->DisplayName() : Step.NodeId);
			I.PartKey = Step.PartKey;
			I.PartLabel = Run.PartLabel(Step.PartKey);
			if (!I.PartKey.IsEmpty()) I.Name += FString::Printf(TEXT(" [part %s]"), *I.PartLabel);
			I.Status = Step.Status;
			I.Attempt = Step.Attempt;
			I.Credits = Step.Credits;
			if (Step.Status == ZettaStepStatus::Waiting) I.Price = StepPrice(Run, Step, Prices(), Gates());
			I.Error = Step.ErrorMessage;
			I.GenerationId = Step.Gen3dPostId;
			if (!I.GenerationId.IsSet() && Step.Output.IsSet()) I.GenerationId = Step.Output->Gen3dPost();
			if (Step.Generation.IsValid())
			{
				I.GenerationTitle = Step.Generation->DisplayTitle();
				I.GenerationStatus = Step.Generation->Status;
			}
			if (Step.Output.IsSet())
			{
				const FZettaStepOutput& O = Step.Output.GetValue();
				I.Output = O.Type;
				if (O.IsPicture()) I.Pictures.Add({ TEXT("picture"), O.Picture.Url });
				for (const TPair<FString, FZettaSignedPicture>& V : O.Views) I.Pictures.Add({ V.Key, V.Value.Url });
				if (O.IsIsolated())
				{
					I.Pictures.Add({ TEXT("character"), O.Character.Url });
					for (const FZettaIsolatedPart& Part : O.Parts) I.Pictures.Add({ Part.Label, Part.Picture.Url });
				}
			}
			if (Step.Status == ZettaStepStatus::Review && Step.Review.IsSet())
			{
				FPartReview R;
				R.StepId = Step.Id;
				R.Subject = Step.Review->Subject;
				R.bHumanoid = Step.Review->bHumanoid;
				for (const FZettaIdentifiedPart& Part : Step.Review->Parts) R.Parts.Add({ Part.Key, Part.Label });
				R.ProposedKeys = Step.Review->ProposedKeys;
				S.Reviews.Add(MoveTemp(R));
			}
			S.Steps.Add(MoveTemp(I));
		}
		for (const FZettaPipelineRunStep* Step : Run.ImportableSteps(EZettaRunImport::FinalSteps)) S.ImportableNodeIds.AddUnique(Step->NodeId);
		S.Text = RunStatusText(Run, Prices(), Gates());
		return S;
	}

	void RunStatus(int64 RunId, TFunction<void(const FRunStatus&)> Done)
	{
		FString Error;
		if (!SignedIn(Error)) return Done(Fail<FRunStatus>(Error));
		ZettaPipelinesApi::GetRun(RunId, [RunId, Done](const FZettaResponse& R, const FZettaPipelineRunPtr& Run)
		{
			if (!R.bOk || !Run.IsValid())
				return Done(Fail<FRunStatus>(ErrorText(R, FString::Printf(TEXT("Run #%lld was not found. pipeline_run_status takes the run ids run_pipeline returned."), RunId))));
			FZettaPipelinePlan::Get().WhenLoaded([Run, Done]() { Done(StatusOf(*Run)); });
		});
	}

	static const TArray<FString>& ManageActions()
	{
		static const TArray<FString> List = { TEXT("pause"), TEXT("resume"), TEXT("cancel"), TEXT("set_mode"), TEXT("run_waiting"), TEXT("run_step"), TEXT("retry_step"), TEXT("choose_parts") };
		return List;
	}

	/** Checks a manage request against the run; on success, also says whether it needs a card and what it shows. */
	static FApproval CheckManage(const FManageRequest& Request, const FZettaPipelineRun& Run)
	{
		FApproval A;
		const FString& Action = Request.Action;
		auto Lines = [&Run](const TArray<const FZettaPipelineRunStep*>& Steps)
		{
			TArray<FString> Out;
			for (const FZettaPipelineRunStep* S : Steps)
			{
				const TOptional<int32> Price = StepPrice(Run, *S, Prices(), Gates());
				Out.Add(FString::Printf(TEXT("• %s — %s"), *StepName(Run, *S), !Price.IsSet() ? TEXT("price unknown") : Price.GetValue() == 0 ? TEXT("free") : *FString::Printf(TEXT("%d credits"), Price.GetValue())));
			}
			return FString::Join(Out, TEXT("\n"));
		};
		const FZettaPipelineRunStep* Step = Request.StepId.IsSet() ? Run.Step(Request.StepId.GetValue()) : nullptr;
		if (Request.StepId.IsSet() && !Step)
		{
			A.Error = FString::Printf(TEXT("Run #%lld has no step %lld. pipeline_run_status lists its steps."), Run.Id, Request.StepId.GetValue());
			return A;
		}
		TArray<const FZettaPipelineRunStep*> Waiting;
		for (const FZettaPipelineRunStep& S : Run.Steps) if (S.Status == ZettaStepStatus::Waiting) Waiting.Add(&S);
		const FString StepTitle = Step ? (Run.Graph.Node(Step->NodeId) ? Run.Graph.Node(Step->NodeId)->DisplayName() : Step->NodeId) : FString();
		A.Kind = TEXT("generate");
		if (Action == TEXT("cancel"))
		{
			if (!Run.IsLive() && Run.Status != ZettaRunStatus::Draft)
			{
				A.Error = FString::Printf(TEXT("Run #%lld is already over (%s)."), Run.Id, *Run.Status);
				return A;
			}
			A.bNeeded = true;
			A.Kind = TEXT("destructive");
			A.Title = FString::Printf(TEXT("Cancel run #%lld"), Run.Id);
			A.Body = FString::Printf(TEXT("Stops run #%lld of pipeline #%lld (%s, %d credits spent). Steps not started are cancelled; a queued 3D job is cancelled and refunded. Finished models stay in the library."),
				Run.Id, Run.PipelineId, *ZettaRunStatus::Label(Run.Status), Run.SpentCredits);
			A.ConfirmLabel = TEXT("Cancel run");
		}
		else if (Action == TEXT("set_mode"))
		{
			// Auto releases every waiting step now and from now on — that spends credits, so it is a card too.
			if (Request.Mode == ZettaRunStatus::ModeAuto && Run.Mode != ZettaRunStatus::ModeAuto && (Run.IsLive() || Run.Status == ZettaRunStatus::Draft))
			{
				const int32 Remaining = FMath::Max(0, Run.EstimatedCredits - Run.SpentCredits);
				A.bNeeded = true;
				A.Title = FString::Printf(TEXT("Switch run #%lld to Auto"), Run.Id);
				A.Body = TEXT("Auto submits every step by itself, to the end — ") + (Waiting.Num() > 0 ? TEXT("starting now with:\n") + Lines(Waiting) + TEXT("\n") : FString())
					+ FString::Printf(TEXT("Up to %d more credits (the run's estimate less what it has spent)."), Remaining);
				A.Credits = Remaining;
				A.ConfirmLabel = TEXT("Switch to Auto");
			}
		}
		else if (Action == TEXT("run_waiting"))
		{
			if (Waiting.Num() == 0)
			{
				A.Error = FString::Printf(TEXT("Run #%lld has no steps waiting for a Run (%s)."), Run.Id, *ZettaRunStatus::Label(Run.Status));
				return A;
			}
			A.bNeeded = true;
			A.Title = FString::Printf(TEXT("Run %s of run #%lld"), *Plural(Waiting.Num(), TEXT("waiting step")), Run.Id);
			A.Body = Lines(Waiting);
			A.Credits = StepsPrice(Run, Waiting, Prices(), Gates());
			A.ConfirmLabel = TEXT("Run");
		}
		else if (Action == TEXT("run_step"))
		{
			if (Step->Status != ZettaStepStatus::Waiting)
			{
				A.Error = FString::Printf(TEXT("%s is %s, not waiting — only a waiting step can be run%s."), *StepName(Run, *Step), *Step->Status,
					Step->Status == ZettaStepStatus::Failed ? TEXT(" (use retry_step)") : Step->Status == ZettaStepStatus::Review ? TEXT(" (use choose_parts)") : TEXT(""));
				return A;
			}
			A.bNeeded = true;
			A.Title = FString::Printf(TEXT("Run step: %s (run #%lld)"), *StepTitle, Run.Id);
			A.Body = Lines({ Step });
			A.Credits = StepPrice(Run, *Step, Prices(), Gates());
			A.ConfirmLabel = TEXT("Run");
		}
		else if (Action == TEXT("retry_step"))
		{
			if (Step->Status != ZettaStepStatus::Failed)
			{
				A.Error = FString::Printf(TEXT("%s is %s — only a failed step can be retried."), *StepName(Run, *Step), *Step->Status);
				return A;
			}
			A.bNeeded = true;
			A.Title = FString::Printf(TEXT("Retry step: %s (run #%lld)"), *StepTitle, Run.Id);
			A.Body = Lines({ Step }) + (Step->ErrorMessage.IsEmpty() ? FString() : TEXT("\nLast error: ") + Short(Step->ErrorMessage, 200));
			A.Credits = StepPrice(Run, *Step, Prices(), Gates());
			A.ConfirmLabel = TEXT("Retry");
		}
		else if (Action == TEXT("choose_parts"))
		{
			if (Step->Status != ZettaStepStatus::Review)
			{
				A.Error = FString::Printf(TEXT("%s is %s — choose_parts is for an Isolate parts step waiting for review."), *StepName(Run, *Step), *Step->Status);
				return A;
			}
			TArray<FString> Known, Unknown;
			if (Step->Review.IsSet()) for (const FZettaIdentifiedPart& P : Step->Review->Parts) if (!P.Key.IsEmpty()) Known.Add(P.Key);
			for (const FString& K : Request.Keys) if (Known.Num() > 0 && !Known.Contains(K)) Unknown.Add(K);
			if (Unknown.Num() > 0)
			{
				A.Error = FString::Printf(TEXT("Unknown part key(s) %s — the review found: %s. Name new parts in extraParts."),
					*FString::Join(Unknown, TEXT(", ")), *FString::Join(Known, TEXT(", ")));
				return A;
			}
		}
		return A;
	}

	/** The manage input's own rules (no run needed). */
	static FString CheckManageInput(const FManageRequest& Request)
	{
		if (!ManageActions().Contains(Request.Action)) return TEXT("'action' must be one of ") + FString::Join(ManageActions(), TEXT(", ")) + TEXT(".");
		if (!Request.StepId.IsSet() && (Request.Action == TEXT("run_step") || Request.Action == TEXT("retry_step") || Request.Action == TEXT("choose_parts")))
			return FString::Printf(TEXT("'%s' needs a stepId (the numeric step id pipeline_run_status lists)."), *Request.Action);
		if (Request.Action == TEXT("set_mode") && Request.Mode != ZettaRunStatus::ModeManual && Request.Mode != ZettaRunStatus::ModeAuto)
			return TEXT("set_mode needs 'mode': manual or auto.");
		if (Request.Action == TEXT("choose_parts") && Request.Keys.Num() == 0 && Request.ExtraParts.Num() == 0)
			return TEXT("choose_parts needs keys (from the review's parts) and/or extraParts.");
		return FString();
	}

	static void ReadRunForManage(const FManageRequest& Request, TFunction<void(const FZettaPipelineRunPtr&, const FString&)> Done)
	{
		FString Error;
		if (!SignedIn(Error)) return Done(nullptr, Error);
		Error = CheckManageInput(Request);
		if (!Error.IsEmpty()) return Done(nullptr, Error);
		const int64 RunId = Request.RunId;
		ZettaPipelinesApi::GetRun(RunId, [RunId, Done](const FZettaResponse& R, const FZettaPipelineRunPtr& Run)
		{
			if (!R.bOk || !Run.IsValid())
				return Done(nullptr, ErrorText(R, FString::Printf(TEXT("Run #%lld was not found. pipeline_run_status takes the run ids run_pipeline returned."), RunId)));
			FZettaPipelinePlan::Get().WhenLoaded([Run, Done]() { Done(Run, FString()); });
		});
	}

	void DescribeManage(const FManageRequest& Request, TFunction<void(const FApproval&)> Done)
	{
		ReadRunForManage(Request, [Request, Done](const FZettaPipelineRunPtr& Run, const FString& Error)
		{
			if (!Run.IsValid())
			{
				FApproval A;
				A.bNeeded = true;
				A.Title = FString::Printf(TEXT("Run #%lld"), Request.RunId);
				A.Error = Error;
				return Done(A);
			}
			Done(CheckManage(Request, *Run));
		});
	}

	void Manage(const FManageRequest& Request, TFunction<void(const FRunStatus&)> Done)
	{
		ReadRunForManage(Request, [Request, Done](const FZettaPipelineRunPtr& Run, const FString& Error)
		{
			if (!Run.IsValid()) return Done(Fail<FRunStatus>(Error));
			const FApproval Check = CheckManage(Request, *Run);
			if (!Check.Error.IsEmpty()) return Done(Fail<FRunStatus>(Check.Error));
			const int64 RunId = Run->Id;
			const int64 StepId = Request.StepId.Get(0);
			ZettaPipelinesApi::FRunDone After = [Request, RunId, Done](const FZettaResponse& R, const FZettaPipelineRunPtr& Answer)
			{
				if (!R.bOk) return Done(Fail<FRunStatus>(ErrorText(R)));
				auto Finish = [Request, Done](const FZettaPipelineRunPtr& Now)
				{
					// A live run is followed for this session (its import choice kept when it was already followed).
					if (Now->IsLive())
					{
						const TSharedPtr<FZettaRunFollower::FFollowed> Existing = FZettaRunFollower::Get().Find(Now->Id);
						FZettaRunFollower::Get().Follow(Now->Id, Now->PipelineId, Request.AgentSessionId,
							Existing.IsValid() ? Existing->Import : EZettaRunImport::FinalSteps, Existing.IsValid() ? Existing->Folder : ModelsFolder());
					}
					FZettaRunFollower::Get().Report(Now);
					FRunStatus S = StatusOf(*Now);
					const FString& A = Request.Action;
					const FString Verb = A == TEXT("pause") ? FString(TEXT("Paused"))
						: A == TEXT("resume") ? FString(TEXT("Resumed"))
						: A == TEXT("cancel") ? FString(TEXT("Cancelled"))
						: A == TEXT("set_mode") ? FString::Printf(TEXT("Switched to %s mode:"), *ZettaRunStatus::ModeLabel(Request.Mode))
						: A == TEXT("run_waiting") ? FString(TEXT("Released the waiting steps of"))
						: A == TEXT("run_step") ? FString::Printf(TEXT("Started step %lld of"), Request.StepId.Get(0))
						: A == TEXT("retry_step") ? FString::Printf(TEXT("Retrying step %lld of"), Request.StepId.Get(0))
						: FString::Printf(TEXT("Chose the parts for step %lld of"), Request.StepId.Get(0));
					const FString Follow = Now->IsLive() ? TEXT(" Followed — a pipeline_run_finished event arrives when it finishes or needs the user.") : TEXT("");
					S.Text = FString::Printf(TEXT("%s run #%lld.%s\n"), *Verb, Now->Id, *Follow) + S.Text;
					Done(S);
				};
				if (Answer.IsValid()) return Finish(Answer);
				ZettaPipelinesApi::GetRun(RunId, [Done, Finish](const FZettaResponse& R2, const FZettaPipelineRunPtr& Again)
				{
					if (!R2.bOk || !Again.IsValid()) return Done(Fail<FRunStatus>(ErrorText(R2)));
					Finish(Again);
				});
			};
			const FString& A = Request.Action;
			if (A == TEXT("pause")) ZettaPipelinesApi::Pause(RunId, After);
			else if (A == TEXT("resume")) ZettaPipelinesApi::Resume(RunId, After);
			else if (A == TEXT("cancel")) ZettaPipelinesApi::Cancel(RunId, After);
			else if (A == TEXT("set_mode")) ZettaPipelinesApi::SetMode(RunId, Request.Mode, After);
			else if (A == TEXT("run_waiting")) ZettaPipelinesApi::RunWaiting(RunId, After);
			else if (A == TEXT("run_step")) ZettaPipelinesApi::RunStep(RunId, StepId, After);
			else if (A == TEXT("retry_step")) ZettaPipelinesApi::RetryStep(RunId, StepId, nullptr, nullptr, After);
			else ZettaPipelinesApi::ChooseParts(RunId, StepId, Request.Keys, Request.ExtraParts, After);
		});
	}

	// ─── import ──────────────────────────────────────────────────────────────────────────────────────────────────────

	void ImportResults(const FImportRequest& Request, TFunction<void(const FImportResult&)> Done)
	{
		FString Error;
		if (!SignedIn(Error)) return Done(Fail<FImportResult>(Error));
		if (Request.NodeIds.Num() > MaxImportNodes) return Done(Fail<FImportResult>(FString::Printf(TEXT("At most %d nodeIds per call."), MaxImportNodes)));
		const int64 RunId = Request.RunId;
		ZettaPipelinesApi::GetRun(RunId, [Request, RunId, Done](const FZettaResponse& R, const FZettaPipelineRunPtr& Run)
		{
			if (!R.bOk || !Run.IsValid())
				return Done(Fail<FImportResult>(ErrorText(R, FString::Printf(TEXT("Run #%lld was not found. pipeline_run_status takes the run ids run_pipeline returned."), RunId))));
			TArray<FString> Unknown;
			for (const FString& N : Request.NodeIds) if (!Run->Graph.Node(N)) Unknown.Add(N);
			if (Unknown.Num() > 0)
			{
				TArray<FString> All;
				for (const FZettaPipelineNode& N : Run->Graph.Nodes) All.Add(N.Id);
				return Done(Fail<FImportResult>(FString::Printf(TEXT("Run #%lld has no node(s) %s. Its nodes: %s."), RunId, *FString::Join(Unknown, TEXT(", ")), *FString::Join(All, TEXT(", ")))));
			}
			const bool bAll = Request.NodeIds.Num() == 0;
			TArray<const FZettaPipelineRunStep*> Importable;
			for (const FZettaPipelineRunStep* S : Run->ImportableSteps(bAll ? EZettaRunImport::FinalSteps : EZettaRunImport::AllMeshSteps))
				if (bAll || Request.NodeIds.Contains(S->NodeId)) Importable.Add(S);
			TArray<FString> Notes;
			for (const FZettaPipelineRunStep& S : Run->Steps)
			{
				if ((!bAll && !Request.NodeIds.Contains(S.NodeId)) || Importable.Contains(&S)) continue;
				const FString Name = StepName(*Run, S);
				const FZettaPipelineNode* Node = Run->Graph.Node(S.NodeId);
				if (S.Output.IsSet() && S.Output->IsSplat()) Notes.Add(Name + TEXT(": a Gaussian splat — not supported in Unreal (open it on zetta3d.ai)"));
				else if (S.Output.IsSet() && S.Output->IsFile()) Notes.Add(Name + TEXT(": an export file — open it on zetta3d.ai"));
				else if (S.Output.IsSet() && S.Output->IsMesh())
				{
					// With no nodes named, an intermediate model is left out on purpose: the final ones are imported.
					if (!bAll) Notes.Add(FString::Printf(TEXT("%s: its model is not ready (%s)"), *Name, S.Generation.IsValid() ? *S.Generation->Status : *S.Status));
				}
				else if (!S.Output.IsSet() && !bAll && Node && ZettaNodeCatalog::IsThreeD(Node->Kind)) Notes.Add(FString::Printf(TEXT("%s: %s — not finished yet"), *Name, *S.Status));
				else if (!bAll && Node && !ZettaNodeCatalog::IsSeed(Node->Kind)) Notes.Add(Name + TEXT(": a picture step — nothing to import (pipeline_run_status lists its picture)"));
			}
			if (Importable.Num() == 0)
			{
				FString Why = Notes.Num() > 0 ? TEXT(": ") + FString::Join(Notes, TEXT("; ")) : Run->IsLive() ? TEXT(" — no model step has finished yet.") : TEXT(" — it made no models.");
				return Done(Fail<FImportResult>(FString::Printf(TEXT("Nothing to import from run #%lld (%s)"), RunId, *ZettaRunStatus::Label(Run->Status)) + Why));
			}
			TArray<FString> NodeIds;
			for (const FZettaPipelineRunStep* S : Importable) NodeIds.AddUnique(S->NodeId);
			FZettaImportOptions Options = Request.Place;
			Options.bPlace = Request.bPlace;
			if (!Request.bPlace) Options.bSelect = false;
			FZettaRunFollower::Import(Run, NodeIds, Options, [Request, RunId, Notes, Done](const TArray<FString>& Paths, const TArray<FString>& Failures)
			{
				if (Paths.Num() == 0 && Failures.Num() > 0) return Done(Fail<FImportResult>(TEXT("The import failed: ") + FString::Join(Failures, TEXT("; "))));
				FImportResult Out;
				Out.bOk = true;
				Out.AssetPaths = Paths;
				Out.Failures = Failures;
				Out.Skipped = Notes;
				FString Text = FString::Printf(TEXT("Imported from run #%lld: %s."), RunId, *FString::Join(Paths, TEXT(", ")));
				Text += Request.bPlace ? TEXT(" Placed in the level.") : TEXT(" Not placed in the level.");
				if (Failures.Num() > 0) Text += TEXT("\nFailed: ") + FString::Join(Failures, TEXT("; "));
				if (Notes.Num() > 0) Text += TEXT("\nSkipped: ") + FString::Join(Notes, TEXT("; "));
				Out.Text = Text;
				Done(Out);
			});
		});
	}

	void OpenInWindow(int64 PipelineId, TOptional<int64> RunId) { ZettaPipelineHooks::RequestOpen(PipelineId, RunId); }

	void Shutdown() { FZettaRunFollower::Get().Shutdown(); }

	// ─── events ──────────────────────────────────────────────────────────────────────────────────────────────────────

	FOnRunEvent& OnRunFinished()
	{
		static FOnRunEvent Delegate;
		static bool bHooked = false;
		if (!bHooked)
		{
			bHooked = true;
			auto Raise = [](const FZettaPipelineRunPtr& Run, const FZettaRunFollower::FFollowed& Entry)
			{
				FRunEvent E;
				E.RunId = Run->Id;
				E.PipelineId = Run->PipelineId;
				E.Status = Run->Status;
				E.ImportedPaths = Entry.ImportedPaths;
				E.AgentSessionId = Entry.AgentSessionId;
				E.Text = RunStatusText(*Run, Prices(), Gates());
				Delegate.Broadcast(E);
			};
			FZettaRunFollower::Get().OnRunFinished.AddLambda(Raise);
			FZettaRunFollower::Get().OnRunNeedsUser.AddLambda(Raise);
		}
		return Delegate;
	}

	// ─── tool input ──────────────────────────────────────────────────────────────────────────────────────────────────

	static TOptional<int64> LongOf(const FVal& V)
	{
		if (JIsNull(V)) return {};
		if (JIsNumber(V)) return (int64)FMath::RoundToDouble(V->AsNumber());
		if (JIsString(V))
		{
			FString S = V->AsString().TrimStartAndEnd();
			S.RemoveFromStart(TEXT("#"));
			if (!S.IsEmpty() && S.IsNumeric() && !S.Contains(TEXT("."))) return FCString::Atoi64(*S);
		}
		return {};
	}

	static bool Present(const FObj& O, const FString& Key) { return JHas(O, Key) && !JIsNull(ZettaPJson::JGet(O, Key)); }

	static FString TextOf(const FVal& V) { return JIsNull(V) ? FString() : JIsString(V) ? V->AsString() : JWriteValue(V); }

	bool ParseId(const TSharedPtr<FJsonObject>& Input, const FString& Key, int64& OutId, FString& OutError)
	{
		const TOptional<int64> Id = LongOf(ZettaPJson::JGet(Input, Key));
		if (!Id.IsSet() || Id.GetValue() <= 0)
		{
			OutError = FString::Printf(TEXT("'%s' is required (a positive number%s)."), *Key,
				Key == TEXT("pipelineId") ? TEXT(" from list_pipelines") : Key == TEXT("runId") ? TEXT(" from run_pipeline or pipeline_run_status") : TEXT(""));
			return false;
		}
		OutId = Id.GetValue();
		return true;
	}

	TArray<FString> StepKinds()
	{
		TArray<FString> Out;
		for (const FZettaNodeSpec* S : ZettaNodeCatalog::OfferedHere()) if (!ZettaNodeCatalog::IsSeed(S->Kind)) Out.Add(S->Kind);
		return Out;
	}

	static bool CheckKind(const FString& Kind, const FString& Where, FString& OutError)
	{
		if (ZettaNodeCatalog::IsSeed(Kind))
		{
			OutError = FString::Printf(TEXT("%s: “%s” is a seed — build_pipeline adds it from startsFrom."), *Where, *Kind);
			return false;
		}
		const FZettaNodeSpec* Spec = ZettaNodeCatalog::Spec(Kind);
		if (!Spec)
		{
			OutError = FString::Printf(TEXT("%s: “%s” is not a step kind. Use one of: %s."), *Where, *Kind, *FString::Join(StepKinds(), TEXT(", ")));
			return false;
		}
		if (!Spec->IsOfferedHere())
		{
			OutError = FString::Printf(TEXT("%s: %s (%s) is not available in Unreal — it can be added on zetta3d.ai only."), *Where, *Spec->Label, *Kind);
			return false;
		}
		return true;
	}

	static bool IsOutput(const FString& Output)
	{
		return Output == TEXT("character") || Output == TEXT("each") || Output.StartsWith(ZettaNodeCatalog::PartHandlePrefix, ESearchCase::CaseSensitive);
	}

	bool ParseBuild(const TSharedPtr<FJsonObject>& Input, FBuildRequest& Out, FString& OutError)
	{
		Out = FBuildRequest();
		Out.Title = TextOf(ZettaPJson::JGet(Input, TEXT("title"))).TrimStartAndEnd();
		if (Out.Title.Len() > MaxTitle) { OutError = FString::Printf(TEXT("'title' is longer than %d characters."), MaxTitle); return false; }
		if (Out.Title.IsEmpty()) { OutError = TEXT("'title' is required (≤200 characters)."); return false; }
		Out.StartsFrom = TextOf(ZettaPJson::JGet(Input, TEXT("startsFrom"))).TrimStartAndEnd().ToLower();
		if (Out.StartsFrom != TEXT("picture") && Out.StartsFrom != TEXT("model")) { OutError = TEXT("'startsFrom' must be picture or model."); return false; }
		Out.bOpen = JBoolOr(Input, TEXT("open"), true);
		const TArray<FVal> Steps = JArrayOf(ZettaPJson::JGet(Input, TEXT("steps")));
		if (Steps.Num() == 0) { OutError = TEXT("'steps' is required: 1–24 of { kind, after?, output?, settings? }."); return false; }
		if (Steps.Num() > MaxSteps) { OutError = FString::Printf(TEXT("A pipeline is built from at most %d steps (%d given)."), MaxSteps, Steps.Num()); return false; }
		for (int32 I = 0; I < Steps.Num(); ++I)
		{
			const FObj O = JObjectOf(Steps[I]);
			if (!O.IsValid()) { OutError = FString::Printf(TEXT("steps[%d] must be an object { kind, after?, output?, settings? }."), I); return false; }
			FBuildStep Step;
			Step.Kind = TextOf(ZettaPJson::JGet(O, TEXT("kind"))).TrimStartAndEnd();
			if (Step.Kind.IsEmpty()) { OutError = FString::Printf(TEXT("steps[%d].kind is required."), I); return false; }
			if (!CheckKind(Step.Kind, FString::Printf(TEXT("steps[%d].kind"), I), OutError)) return false;
			if (Present(O, TEXT("after")))
			{
				const TOptional<int64> A = LongOf(ZettaPJson::JGet(O, TEXT("after")));
				if (!A.IsSet() || A.GetValue() < 0 || A.GetValue() >= I)
				{
					OutError = FString::Printf(TEXT("steps[%d].after must be the index of an earlier step (%s); omit it to follow the seed."), I,
						I == 0 ? TEXT("step 0 has none — omit it to follow the seed") : *FString::Printf(TEXT("0–%d"), I - 1));
					return false;
				}
				Step.After = (int32)A.GetValue();
			}
			if (Present(O, TEXT("output")))
			{
				Step.Output = TextOf(ZettaPJson::JGet(O, TEXT("output"))).TrimStartAndEnd();
				if (!IsOutput(Step.Output)) { OutError = FString::Printf(TEXT("steps[%d].output must be character, each or part:<key>."), I); return false; }
				const FString ParentKind = Step.After.IsSet() ? TextOf(ZettaPJson::JGet(JObjectOf(Steps[Step.After.GetValue()]), TEXT("kind"))) : FString();
				if (ParentKind != ZettaNodeKinds::IsolateParts) { OutError = FString::Printf(TEXT("steps[%d].output is only for a step after isolate_parts."), I); return false; }
			}
			if (Present(O, TEXT("settings")) && !JIsObject(ZettaPJson::JGet(O, TEXT("settings")))) { OutError = FString::Printf(TEXT("steps[%d].settings must be an object."), I); return false; }
			Step.Settings = JObject(O, TEXT("settings"));
			Out.Steps.Add(MoveTemp(Step));
		}
		return true;
	}

	bool ParseEdit(const TSharedPtr<FJsonObject>& Input, FEditRequest& Out, FString& OutError)
	{
		Out = FEditRequest();
		if (!ParseId(Input, TEXT("pipelineId"), Out.PipelineId, OutError)) return false;
		if (JHas(Input, TEXT("title")))
		{
			const FString Title = TextOf(ZettaPJson::JGet(Input, TEXT("title"))).TrimStartAndEnd();
			if (Title.Len() > MaxTitle) { OutError = FString::Printf(TEXT("'title' is longer than %d characters."), MaxTitle); return false; }
			if (Title.IsEmpty()) { OutError = TEXT("'title' may not be empty."); return false; }
			Out.Title = Title;
		}
		const FVal ChangesValue = ZettaPJson::JGet(Input, TEXT("changes"));
		const TArray<FVal> Changes = JArrayOf(ChangesValue);
		// A title with no changes renames only.
		if (Out.Title.IsSet() && Changes.Num() == 0) return true;
		if (Changes.Num() == 0)
		{
			OutError = TEXT("'changes' is required: 1–24 of { op: \"add\", kind, after, output?, settings? } | { op: \"set\", nodeId, settings } | { op: \"remove\", nodeId }.");
			return false;
		}
		if (Changes.Num() > MaxChanges) { OutError = FString::Printf(TEXT("At most %d changes per call (%d given)."), MaxChanges, Changes.Num()); return false; }
		for (int32 I = 0; I < Changes.Num(); ++I)
		{
			const FObj O = JObjectOf(Changes[I]);
			if (!O.IsValid()) { OutError = FString::Printf(TEXT("changes[%d] must be an object."), I); return false; }
			FChange C;
			C.Op = TextOf(ZettaPJson::JGet(O, TEXT("op")));
			C.Kind = TextOf(ZettaPJson::JGet(O, TEXT("kind")));
			C.After = TextOf(ZettaPJson::JGet(O, TEXT("after")));
			C.Output = JIsString(ZettaPJson::JGet(O, TEXT("output"))) ? ZettaPJson::JGet(O, TEXT("output"))->AsString() : FString();
			C.NodeId = TextOf(ZettaPJson::JGet(O, TEXT("nodeId")));
			C.Settings = JObject(O, TEXT("settings"));
			if (C.Op == TEXT("add"))
			{
				if (C.Kind.TrimStartAndEnd().IsEmpty()) { OutError = FString::Printf(TEXT("changes[%d] (add) needs a kind."), I); return false; }
				if (!CheckKind(C.Kind, FString::Printf(TEXT("changes[%d]"), I), OutError)) return false;
				const FVal After = ZettaPJson::JGet(O, TEXT("after"));
				if (Present(O, TEXT("after")) && !JIsString(After) && !JIsNumber(After)) { OutError = FString::Printf(TEXT("changes[%d].after must be a node id (see get_pipeline)."), I); return false; }
				if (!C.Output.IsEmpty() && !IsOutput(C.Output)) { OutError = FString::Printf(TEXT("changes[%d].output must be character, each or part:<key>."), I); return false; }
			}
			else if (C.Op == TEXT("set"))
			{
				if (C.NodeId.TrimStartAndEnd().IsEmpty()) { OutError = FString::Printf(TEXT("changes[%d] (set) needs a nodeId."), I); return false; }
				if (!C.Settings.IsValid()) { OutError = FString::Printf(TEXT("changes[%d] (set) needs settings (an object of what to change)."), I); return false; }
			}
			else if (C.Op == TEXT("remove"))
			{
				if (C.NodeId.TrimStartAndEnd().IsEmpty()) { OutError = FString::Printf(TEXT("changes[%d] (remove) needs a nodeId."), I); return false; }
			}
			else
			{
				OutError = FString::Printf(TEXT("changes[%d].op must be add, set or remove (“%s” given)."), I, *C.Op);
				return false;
			}
			if (Present(O, TEXT("settings")) && !JIsObject(ZettaPJson::JGet(O, TEXT("settings")))) { OutError = FString::Printf(TEXT("changes[%d].settings must be an object."), I); return false; }
			Out.Changes.Add(MoveTemp(C));
		}
		return true;
	}

	bool ParseRun(const TSharedPtr<FJsonObject>& Input, FRunRequest& Out, FString& OutError)
	{
		Out = FRunRequest();
		if (!ParseId(Input, TEXT("pipelineId"), Out.PipelineId, OutError)) return false;
		Out.Mode = TextOf(ZettaPJson::JGet(Input, TEXT("mode"))).TrimStartAndEnd().ToLower();
		if (Out.Mode.IsEmpty()) Out.Mode = ZettaRunStatus::ModeManual;
		if (Out.Mode != ZettaRunStatus::ModeManual && Out.Mode != ZettaRunStatus::ModeAuto) { OutError = TEXT("'mode' must be manual or auto."); return false; }

		const TArray<FVal> Seeds = JArrayOf(ZettaPJson::JGet(Input, TEXT("seeds")));
		if (Seeds.Num() == 0) { OutError = TEXT("'seeds' is required: 1–20 of { imageId } | { generationId } | { assetPath }."); return false; }
		if (Seeds.Num() > MaxSeeds) { OutError = FString::Printf(TEXT("At most %d seeds per call (%d given) — split them over several run_pipeline calls."), MaxSeeds, Seeds.Num()); return false; }
		for (int32 I = 0; I < Seeds.Num(); ++I)
		{
			const FObj O = JObjectOf(Seeds[I]);
			if (!O.IsValid()) { OutError = FString::Printf(TEXT("seeds[%d] must be an object: { imageId } | { generationId } | { assetPath }."), I); return false; }
			TArray<FString> Given;
			for (const TCHAR* Key : { TEXT("imageId"), TEXT("generationId"), TEXT("assetPath") }) if (Present(O, Key)) Given.Add(Key);
			if (Given.Num() != 1)
			{
				OutError = FString::Printf(TEXT("seeds[%d] must name exactly one of imageId, generationId, assetPath (%s given)."), I,
					Given.Num() == 0 ? TEXT("none") : *FString::Join(Given, TEXT(" and ")));
				return false;
			}
			FSeed Seed;
			if (Given[0] == TEXT("imageId"))
			{
				Seed.ImageId = LongOf(ZettaPJson::JGet(O, TEXT("imageId")));
				if (!Seed.ImageId.IsSet() || Seed.ImageId.GetValue() <= 0) { OutError = FString::Printf(TEXT("seeds[%d].imageId must be a picture id from generate_image."), I); return false; }
			}
			else if (Given[0] == TEXT("generationId"))
			{
				Seed.GenerationId = LongOf(ZettaPJson::JGet(O, TEXT("generationId")));
				if (!Seed.GenerationId.IsSet() || Seed.GenerationId.GetValue() <= 0) { OutError = FString::Printf(TEXT("seeds[%d].generationId must be a model id from list_generations."), I); return false; }
			}
			else
			{
				FString Path = TextOf(ZettaPJson::JGet(O, TEXT("assetPath"))).TrimStartAndEnd().TrimQuotes().Replace(TEXT("\\"), TEXT("/"));
				while (Path.StartsWith(TEXT("./"))) Path = Path.Mid(2);
				if (Path.IsEmpty()) { OutError = FString::Printf(TEXT("seeds[%d].assetPath is empty."), I); return false; }
				const bool bContent = Path.StartsWith(TEXT("/Game/")) || (Path.StartsWith(TEXT("/")) && !FPaths::FileExists(Path));
				if (!bContent && !IsPictureFile(Path) && !IsModelFile(Path))
				{
					OutError = FString::Printf(TEXT("seeds[%d].assetPath must be a project asset (/Game/…) or a .png, .jpg, .jpeg or .glb file (\"%s\" given)."), I, *Path);
					return false;
				}
				Seed.AssetPath = Path;
			}
			Out.Seeds.Add(MoveTemp(Seed));
		}

		const FVal InputsValue = ZettaPJson::JGet(Input, TEXT("inputs"));
		if (!JIsNull(InputsValue))
		{
			const FObj Map = JObjectOf(InputsValue);
			if (!Map.IsValid()) { OutError = TEXT("'inputs' must be an object keyed by node id: { \"<nodeId>\": { prompt?, imageId? } }."); return false; }
			for (const TPair<FString, FVal>& Pair : Map->Values)
			{
				const FObj O = JObjectOf(Pair.Value);
				if (!O.IsValid()) { OutError = FString::Printf(TEXT("inputs[\"%s\"] must be { prompt?, imageId? }."), *Pair.Key); return false; }
				FRunInput RunInput;
				RunInput.NodeId = Pair.Key;
				if (Present(O, TEXT("prompt")))
				{
					RunInput.Prompt = TextOf(ZettaPJson::JGet(O, TEXT("prompt"))).TrimStartAndEnd();
					if (RunInput.Prompt.Len() > MaxPrompt) { OutError = FString::Printf(TEXT("inputs[\"%s\"].prompt is longer than 2,000 characters."), *Pair.Key); return false; }
				}
				if (Present(O, TEXT("imageId")))
				{
					RunInput.ImageId = LongOf(ZettaPJson::JGet(O, TEXT("imageId")));
					if (!RunInput.ImageId.IsSet() || RunInput.ImageId.GetValue() <= 0) { OutError = FString::Printf(TEXT("inputs[\"%s\"].imageId must be a picture id from generate_image."), *Pair.Key); return false; }
				}
				if (RunInput.Prompt.IsEmpty() && !RunInput.ImageId.IsSet()) { OutError = FString::Printf(TEXT("inputs[\"%s\"] gives neither a prompt nor an imageId."), *Pair.Key); return false; }
				Out.Inputs.Add(MoveTemp(RunInput));
			}
		}
		return true;
	}

	static bool ParsePartList(const FVal& Value, const FString& Name, TArray<FString>& Out, FString& OutError)
	{
		Out.Reset();
		if (JIsNull(Value)) return true;
		if (!JIsArray(Value)) { OutError = FString::Printf(TEXT("'%s' must be an array of strings."), *Name); return false; }
		const TArray<FVal>& Items = Value->AsArray();
		if (Items.Num() > MaxPartList) { OutError = FString::Printf(TEXT("'%s' holds at most %d entries (%d given)."), *Name, MaxPartList, Items.Num()); return false; }
		for (const FVal& Item : Items)
		{
			const FString S = TextOf(Item).TrimStartAndEnd();
			if (S.IsEmpty()) continue;
			if (S.Len() > MaxPartLength) { OutError = FString::Printf(TEXT("'%s' entries are at most %d characters (“%s” is %d)."), *Name, MaxPartLength, *Short(S, 30), S.Len()); return false; }
			Out.Add(S);
		}
		return true;
	}

	bool ParseManage(const TSharedPtr<FJsonObject>& Input, FManageRequest& Out, FString& OutError)
	{
		Out = FManageRequest();
		if (!ParseId(Input, TEXT("runId"), Out.RunId, OutError)) return false;
		Out.Action = TextOf(ZettaPJson::JGet(Input, TEXT("action"))).TrimStartAndEnd().ToLower();
		Out.Mode = TextOf(ZettaPJson::JGet(Input, TEXT("mode"))).TrimStartAndEnd().ToLower();
		Out.StepId = LongOf(ZettaPJson::JGet(Input, TEXT("stepId")));
		if (Out.Action == TEXT("choose_parts"))
		{
			if (!ParsePartList(ZettaPJson::JGet(Input, TEXT("keys")), TEXT("keys"), Out.Keys, OutError)) return false;
			if (!ParsePartList(ZettaPJson::JGet(Input, TEXT("extraParts")), TEXT("extraParts"), Out.ExtraParts, OutError)) return false;
		}
		OutError = CheckManageInput(Out);
		return OutError.IsEmpty();
	}

	bool ParseImport(const TSharedPtr<FJsonObject>& Input, FImportRequest& Out, FString& OutError)
	{
		Out = FImportRequest();
		if (!ParseId(Input, TEXT("runId"), Out.RunId, OutError)) return false;
		for (const FString& N : JStringsOf(ZettaPJson::JGet(Input, TEXT("nodeIds"))))
		{
			const FString Id = N.TrimStartAndEnd();
			if (!Id.IsEmpty()) Out.NodeIds.AddUnique(Id);
		}
		if (Out.NodeIds.Num() > MaxImportNodes) { OutError = FString::Printf(TEXT("At most %d nodeIds per call."), MaxImportNodes); return false; }
		Out.bPlace = Present(Input, TEXT("place"));
		return true;
	}

	// ─── labels ──────────────────────────────────────────────────────────────────────────────────────────────────────

	FString DescribeCall(const FString& Name, const TSharedPtr<FJsonObject>& I)
	{
		auto S = [&I](const TCHAR* Key) { return TextOf(ZettaPJson::JGet(I, Key)); };
		if (Name == TEXT("list_pipelines")) return TEXT("Listing your 3D pipelines");
		if (Name == TEXT("get_pipeline")) return TEXT("Reading pipeline #") + S(TEXT("pipelineId"));
		if (Name == TEXT("build_pipeline")) return FString::Printf(TEXT("Building pipeline “%s”"), *Short(S(TEXT("title")), 60));
		if (Name == TEXT("edit_pipeline")) return TEXT("Editing pipeline #") + S(TEXT("pipelineId"));
		if (Name == TEXT("run_pipeline")) return FString::Printf(TEXT("Running pipeline #%s on %s"), *S(TEXT("pipelineId")), *Plural(JArrayOf(ZettaPJson::JGet(I, TEXT("seeds"))).Num(), TEXT("seed")));
		if (Name == TEXT("pipeline_run_status")) return TEXT("Checking pipeline run #") + S(TEXT("runId"));
		if (Name == TEXT("manage_pipeline_run"))
		{
			const FString Run = S(TEXT("runId"));
			const FString A = S(TEXT("action"));
			if (A == TEXT("pause")) return TEXT("Pausing run #") + Run;
			if (A == TEXT("resume")) return TEXT("Resuming run #") + Run;
			if (A == TEXT("cancel")) return TEXT("Cancelling run #") + Run;
			if (A == TEXT("set_mode")) return FString::Printf(TEXT("Switching run #%s to %s"), *Run, *S(TEXT("mode")));
			if (A == TEXT("run_waiting")) return FString::Printf(TEXT("Running the waiting steps of run #%s"), *Run);
			if (A == TEXT("run_step")) return FString::Printf(TEXT("Running step %s of run #%s"), *S(TEXT("stepId")), *Run);
			if (A == TEXT("retry_step")) return FString::Printf(TEXT("Retrying step %s of run #%s"), *S(TEXT("stepId")), *Run);
			if (A == TEXT("choose_parts")) return TEXT("Choosing parts for run #") + Run;
			return TEXT("Managing run #") + Run;
		}
		if (Name == TEXT("import_pipeline_results")) return TEXT("Importing the results of run #") + S(TEXT("runId"));
		return FString();
	}

	FString SummarizeCall(const FString& Name, const TSharedPtr<FJsonObject>& I)
	{
		auto S = [&I](const TCHAR* Key) { return TextOf(ZettaPJson::JGet(I, Key)); };
		if (Name == TEXT("build_pipeline"))
		{
			FString Out = FString::Printf(TEXT("New pipeline “%s”, starts from a %s."), *S(TEXT("title")), *S(TEXT("startsFrom")));
			const TArray<FVal> Steps = JArrayOf(ZettaPJson::JGet(I, TEXT("steps")));
			for (int32 N = 0; N < Steps.Num(); ++N)
			{
				const FObj Step = JObjectOf(Steps[N]);
				const FVal After = ZettaPJson::JGet(Step, TEXT("after"));
				Out += FString::Printf(TEXT("\n%d. %s%s"), N, *TextOf(ZettaPJson::JGet(Step, TEXT("kind"))), JIsNull(After) ? TEXT("") : *(TEXT(" after ") + TextOf(After)));
				if (JHas(Step, TEXT("output"))) Out += FString::Printf(TEXT(" (%s)"), *TextOf(ZettaPJson::JGet(Step, TEXT("output"))));
			}
			return Out;
		}
		if (Name == TEXT("edit_pipeline"))
		{
			FString Out = TEXT("Pipeline #") + S(TEXT("pipelineId"));
			if (JHas(I, TEXT("title"))) Out += FString::Printf(TEXT(" → “%s”"), *S(TEXT("title")));
			for (const FVal& V : JArrayOf(ZettaPJson::JGet(I, TEXT("changes"))))
			{
				const FObj C = JObjectOf(V);
				if (!C.IsValid()) continue;
				const FString Op = TextOf(ZettaPJson::JGet(C, TEXT("op")));
				if (Op == TEXT("add")) Out += FString::Printf(TEXT("\n+ %s%s"), *TextOf(ZettaPJson::JGet(C, TEXT("kind"))), JHas(C, TEXT("after")) ? *(TEXT(" after ") + TextOf(ZettaPJson::JGet(C, TEXT("after")))) : TEXT(""));
				else if (Op == TEXT("set")) Out += FString::Printf(TEXT("\n~ %s: %s"), *TextOf(ZettaPJson::JGet(C, TEXT("nodeId"))), *Short(JWriteValue(ZettaPJson::JGet(C, TEXT("settings"))), 160));
				else if (Op == TEXT("remove")) Out += TEXT("\n− ") + TextOf(ZettaPJson::JGet(C, TEXT("nodeId")));
				else Out += TEXT("\n") + Op;
			}
			return Out;
		}
		if (Name == TEXT("manage_pipeline_run"))
		{
			return FString::Printf(TEXT("Run #%s: %s%s%s"), *S(TEXT("runId")), *S(TEXT("action")),
				JHas(I, TEXT("mode")) ? *(TEXT(" ") + S(TEXT("mode"))) : TEXT(""), JHas(I, TEXT("stepId")) ? *FString::Printf(TEXT(" (step %s)"), *S(TEXT("stepId"))) : TEXT(""));
		}
		if (Name == TEXT("import_pipeline_results"))
		{
			const TArray<FString> Nodes = JStringsOf(ZettaPJson::JGet(I, TEXT("nodeIds")));
			return FString::Printf(TEXT("Imports the models of run #%s (%s) into %s/%s"), *S(TEXT("runId")),
				Nodes.Num() > 0 ? *FString::Join(Nodes, TEXT(", ")) : TEXT("its final steps"), *ModelsFolder(), JHas(I, TEXT("place")) ? TEXT(", placed in the level.") : TEXT("."));
		}
		return FString();
	}
}
