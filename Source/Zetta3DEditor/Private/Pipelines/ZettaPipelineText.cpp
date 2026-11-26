// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaPipelineText.h"

#include "ZettaPipelineJson.h"

using namespace ZettaPJson;

namespace ZettaPipelineText
{
	FString Plural(int32 N, const FString& One, const FString& Many)
	{
		return N == 1 ? TEXT("1 ") + One : FString::Printf(TEXT("%d %s"), N, *(Many.IsEmpty() ? One + TEXT("s") : Many));
	}

	FString Short(const FString& Text, int32 Max)
	{
		const FString S = Text.Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" ")).TrimStartAndEnd();
		return S.Len() <= Max ? S : S.Left(FMath::Max(0, Max - 1)) + TEXT("…");
	}

	FString Ago(const FDateTime& When, const FDateTime& NowUtc)
	{
		if (When.GetTicks() == 0) return TEXT("—");
		const FTimespan Span = NowUtc - When;
		if (Span.GetTotalSeconds() < 60) return TEXT("just now");
		if (Span.GetTotalMinutes() < 60) return FString::Printf(TEXT("%d min ago"), (int32)Span.GetTotalMinutes());
		if (Span.GetTotalHours() < 24) return FString::Printf(TEXT("%d h ago"), (int32)Span.GetTotalHours());
		if (Span.GetTotalDays() < 7) return FString::Printf(TEXT("%d d ago"), (int32)Span.GetTotalDays());
		return When.ToString(TEXT("%Y-%m-%d"));
	}

	FString WhenRun(const FDateTime& When)
	{
		if (When.GetTicks() == 0) return FString();
		const int32 Minutes = FMath::RoundToInt((FDateTime::UtcNow() - When).GetTotalMinutes());
		if (Minutes < 1) return TEXT("just now");
		if (Minutes < 60) return FString::Printf(TEXT("%d min ago"), Minutes);
		const int32 Hours = FMath::RoundToInt(Minutes / 60.0);
		if (Hours < 24) return FString::Printf(TEXT("%d h ago"), Hours);
		const int32 Days = FMath::RoundToInt(Hours / 24.0);
		if (Days < 7) return FString::Printf(TEXT("%d day%s ago"), Days, Days == 1 ? TEXT("") : TEXT("s"));
		return When.ToString(TEXT("%d %b"));
	}

	FString SettingsBrief(const FZettaPipelineNode& Node, int32 Max)
	{
		const TSharedPtr<FJsonObject> S = Node.Settings;
		if (!S.IsValid() || ZettaNodeCatalog::IsSeed(Node.Kind)) return FString();
		TArray<FString> Parts;
		auto Add = [&Parts](const FString& Key, const FVal& Value)
		{
			if (JIsNull(Value)) return;
			if (JIsString(Value) && Value->AsString().IsEmpty()) return;
			FString Text;
			if (JIsArray(Value))
			{
				TArray<FString> Items;
				for (const FVal& Item : Value->AsArray())
				{
					const FObj O = JObjectOf(Item);
					Items.Add(O.IsValid() ? (JHas(O, TEXT("key")) ? JAsText(JGet(O, TEXT("key"))) : JWriteValue(Item)) : JAsText(Item));
				}
				Text = FString::Join(Items, TEXT("/"));
				if (Text.IsEmpty()) return;
			}
			else Text = JAsText(Value);
			Parts.Add(Key == TEXT("prompt") || Key == TEXT("hint") ? FString::Printf(TEXT("%s=“%s”"), *Key, *Short(Text, 60)) : Key + TEXT("=") + Text);
		};
		if (ZettaNodeCatalog::IsGenerate(Node.Kind))
		{
			Add(TEXT("model"), JGet(S, TEXT("model")));
			Add(TEXT("quality"), JGet(S, TEXT("quality")));
			Add(TEXT("output"), JGet(S, TEXT("output")));
			Add(TEXT("pbr"), JGet(S, TEXT("pbrMaterials")));
			const FString Count = JAsText(JGet(S, TEXT("polygonCount"))).TrimStartAndEnd();
			if (!Count.IsEmpty() && Count.IsNumeric() && !Count.Contains(TEXT("."))) Parts.Add(TEXT("faceCount=") + Count);
			Add(TEXT("polygonType"), JGet(S, TEXT("polygonType")));
			Add(TEXT("textureQuality"), JGet(S, TEXT("textureQuality")));
			const FVal Segment = JGet(S, TEXT("segmentParts"));
			if (JIsBool(Segment) && Segment->AsBool()) Parts.Add(TEXT("segmentParts=true"));
		}
		else if (Node.Kind == ZettaNodeKinds::Rigging)
		{
			for (const TPair<FString, FVal>& Pair : S->Values) Add(Pair.Key, Pair.Value);
		}
		else if (Node.Kind == ZettaNodeKinds::Texture || Node.Kind == ZettaNodeKinds::TextureMultiview)
		{
			for (const TPair<FString, FVal>& Pair : S->Values)
			{
				// Given per run.
				if (Pair.Key == TEXT("prompt") || Pair.Key == TEXT("reference") || Pair.Key == TEXT("referenceViews")) continue;
				Add(Pair.Key, Pair.Value);
			}
		}
		else
		{
			for (const TPair<FString, FVal>& Pair : S->Values)
			{
				if (Pair.Key == TEXT("model") && JIsNull(Pair.Value)) continue;
				Add(Pair.Key == TEXT("knownParts") ? FString(TEXT("parts")) : Pair.Key, Pair.Value);
			}
		}
		const FString Joined = FString::Join(Parts, TEXT(", "));
		return Joined.Len() <= Max ? Joined : Joined.Left(Max - 1) + TEXT("…");
	}

	FString StepTree(const FZettaPipelineGraph& Graph, const FZettaPriceList* Prices, const FZettaEntitlements3D* Gates)
	{
		if (Graph.Nodes.Num() == 0) return TEXT("(no steps)");
		const TSet<FString> PerPart = ZettaGraphRules::FanOutNodes(Graph);
		TSet<FString> Seen;
		FString Out;
		TFunction<void(const FZettaPipelineNode&, int32, const FZettaPipelineEdge*)> Line = [&](const FZettaPipelineNode& Node, int32 Depth, const FZettaPipelineEdge* From)
		{
			bool bAlready = false;
			Seen.Add(Node.Id, &bAlready);
			if (bAlready) return;
			Out += FString::ChrN(Depth * 2, TEXT(' ')) + TEXT("- ") + Node.Id + TEXT(" ") + Node.Kind;
			if (Node.Label.IsSet() && !Node.Label->IsEmpty()) Out += FString::Printf(TEXT(" “%s”"), *Node.Label.GetValue());
			else if (ZettaNodeCatalog::IsKnown(Node.Kind)) Out += FString::Printf(TEXT(" (%s)"), *ZettaNodeCatalog::Label(Node.Kind));
			if (From) Out += FString::Printf(TEXT(" ← %s.%s"), *From->Source, *From->SourceHandle);
			else if (!ZettaNodeCatalog::IsSeed(Node.Kind)) Out += TEXT(" ← NOT CONNECTED");
			const FString Summary = ZettaNodeSettings::Summary(Node);
			if (!Summary.IsEmpty()) Out += TEXT(" · ") + Summary;
			const FString Brief = SettingsBrief(Node);
			if (!Brief.IsEmpty()) Out += TEXT(" · {") + Brief + TEXT("}");
			if (!ZettaNodeCatalog::IsSeed(Node.Kind) && ZettaNodeCatalog::IsKnown(Node.Kind))
			{
				const TOptional<int32> Price = ZettaRequestBuilder::PriceFor(Node, Prices, { ZettaGraphRules::IncomingType(Graph, Node.Id), Gates });
				Out += TEXT(" · ") + (Price.IsSet() ? ZettaRequestBuilder::PriceLabel(Price, PerPart.Contains(Node.Id)) : FString(TEXT("price unknown")));
			}
			else if (!ZettaNodeCatalog::IsKnown(Node.Kind)) Out += TEXT(" · unknown step (kept as it is)");
			if (PerPart.Contains(Node.Id)) Out += TEXT(" · runs once per part");
			if (ZettaNodeSettings::SetOnEachRun(Node)) Out += TEXT(" · input given on each run");
			if (ZettaNodeCatalog::IsKnown(Node.Kind) && ZettaNodeCatalog::NotAvailableHere(Node.Kind)) Out += TEXT(" · web-only (cannot be changed in Unreal)");
			Out += TEXT("\n");
			for (const FZettaPipelineEdge& E : Graph.Edges)
			{
				if (E.Source != Node.Id) continue;
				if (const FZettaPipelineNode* Child = Graph.Node(E.Target)) Line(*Child, Depth + 1, &E);
			}
		};
		TArray<const FZettaPipelineNode*> Roots;
		for (const FZettaPipelineNode& N : Graph.Nodes)
		{
			bool bFed = false;
			for (const FZettaPipelineEdge& E : Graph.Edges) if (E.Target == N.Id && Graph.Node(E.Source)) bFed = true;
			if (!bFed) Roots.Add(&N);
		}
		Roots.StableSort([](const FZettaPipelineNode& A, const FZettaPipelineNode& B) { return ZettaNodeCatalog::IsSeed(A.Kind) && !ZettaNodeCatalog::IsSeed(B.Kind); });
		for (const FZettaPipelineNode* Root : Roots) Line(*Root, 0, nullptr);
		// Anything in a loop.
		for (const FZettaPipelineNode& N : Graph.Nodes) Line(N, 0, ZettaGraphRules::InputEdge(Graph, N.Id));
		return Out.TrimEnd();
	}

	FString EstimateText(const FZettaGraphEstimate& Estimate)
	{
		if (!Estimate.Credits.IsSet()) return TEXT("Estimate unknown (prices not loaded).");
		FString Text = Estimate.Credits.GetValue() == 0 ? FString(TEXT("Free to run (no paid steps)"))
			: FString::Printf(TEXT("%s%d credits a run"), Estimate.bPerPart ? TEXT("Up to ") : TEXT(""), Estimate.Credits.GetValue());
		if (Estimate.bPerPart)
		{
			int32 Count = 1;
			for (const FZettaGraphEstimate::FLine& L : Estimate.Lines) if (L.bPerPart) Count = FMath::Max(Count, L.Count);
			Text += FString::Printf(TEXT(" (steps after Isolate parts › each run once per part — counted for up to %d parts)"), Count);
		}
		return Text + TEXT(".");
	}

	FString ProblemsText(const TArray<FZettaPipelineProblem>& Problems, const FZettaPipelineGraph& Graph)
	{
		TArray<FString> Lines;
		for (const FZettaPipelineProblem& P : Problems)
		{
			if (P.NodeId.IsEmpty()) Lines.Add(TEXT("- ") + P.Message);
			else
			{
				const FZettaPipelineNode* Node = Graph.Node(P.NodeId);
				Lines.Add(FString::Printf(TEXT("- [%s%s] %s"), *P.NodeId, Node ? *(TEXT(" ") + Node->DisplayName()) : TEXT(""), *P.Message));
			}
		}
		return FString::Join(Lines, TEXT("\n"));
	}

	FString WarningsText(const FZettaPipelineGraph& Graph)
	{
		TArray<FString> Lines;
		for (const FZettaPipelineNode& N : Graph.Nodes)
		{
			for (const FString& W : ZettaGraphRules::Warnings(Graph, N.Id)) Lines.Add(FString::Printf(TEXT("- [%s] %s"), *N.Id, *W));
		}
		return FString::Join(Lines, TEXT("\n"));
	}

	FString GraphReport(const FZettaPipelineGraph& Graph, const FZettaPriceList* Prices, const FZettaEntitlements3D* Gates)
	{
		FString Out;
		const TArray<FZettaPipelineProblem> Problems = ZettaGraphRules::Problems(Graph, Gates);
		Out += Problems.Num() == 0 ? FString(TEXT("Problems: none — runnable.\n")) : TEXT("Problems (it cannot run until fixed):\n") + ProblemsText(Problems, Graph) + TEXT("\n");
		const FString Warnings = WarningsText(Graph);
		if (!Warnings.IsEmpty()) Out += TEXT("Warnings:\n") + Warnings + TEXT("\n");
		Out += TEXT("Estimate: ") + EstimateText(ZettaGraphRules::Estimate(Graph, Prices, Gates)) + TEXT("\n");
		Out += TEXT("Steps:\n") + StepTree(Graph, Prices, Gates);
		return Out;
	}

	FString SeedText(const TSharedPtr<FJsonObject>& Seed)
	{
		if (!Seed.IsValid()) return TEXT("—");
		FString Type = JTextOr(Seed, TEXT("type"));
		if (Type.IsEmpty()) Type = JHas(Seed, TEXT("gen3dPostId")) ? TEXT("model") : TEXT("picture");
		const FString Name = JTextOr(Seed, TEXT("name"));
		FString Id = JAsText(JGet(Seed, TEXT("gen3dPostId")));
		if (Id.IsEmpty()) Id = JAsText(JGet(Seed, TEXT("gen2dPostId")));
		if (!Name.IsEmpty()) return FString::Printf(TEXT("%s “%s”%s"), *Type, *Short(Name, 60), Id.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (#%s)"), *Id));
		return Id.IsEmpty() ? Type : FString::Printf(TEXT("%s #%s"), *Type, *Id);
	}

	TOptional<int32> StepPrice(const FZettaPipelineRun& Run, const FZettaPipelineRunStep& Step, const FZettaPriceList* Prices, const FZettaEntitlements3D* Gates)
	{
		const FZettaPipelineNode* Node = Run.Graph.Node(Step.NodeId);
		if (!Node || !Prices) return {};
		return ZettaRequestBuilder::PriceFor(*Node, Prices, { ZettaGraphRules::IncomingType(Run.Graph, Node->Id), Gates });
	}

	TOptional<int32> StepsPrice(const FZettaPipelineRun& Run, const TArray<const FZettaPipelineRunStep*>& Steps, const FZettaPriceList* Prices, const FZettaEntitlements3D* Gates)
	{
		int32 Total = 0;
		for (const FZettaPipelineRunStep* S : Steps)
		{
			const TOptional<int32> P = StepPrice(Run, *S, Prices, Gates);
			if (!P.IsSet()) return {};
			Total += P.GetValue();
		}
		return Total;
	}

	FString StepName(const FZettaPipelineRun& Run, const FZettaPipelineRunStep& Step)
	{
		const FZettaPipelineNode* Node = Run.Graph.Node(Step.NodeId);
		const FString Part = Step.PartKey.IsEmpty() ? FString() : FString::Printf(TEXT(" [part %s]"), *Run.PartLabel(Step.PartKey));
		return FString::Printf(TEXT("step %lld · %s %s%s"), Step.Id, *Step.NodeId, *(Node ? Node->DisplayName() : ZettaNodeCatalog::Label(Step.NodeId)), *Part);
	}

	FString RunStatusText(const FZettaPipelineRun& Run, const FZettaPriceList* Prices, const FZettaEntitlements3D* Gates)
	{
		const FDateTime Now = FDateTime::UtcNow();
		FString Out = FString::Printf(TEXT("Run #%lld of pipeline #%lld — %s (%s%s)"), Run.Id, Run.PipelineId, *ZettaRunStatus::Label(Run.Status), *Run.Status,
			Run.bPaused && Run.Status != ZettaRunStatus::Paused ? TEXT(", paused") : TEXT(""));
		Out += FString::Printf(TEXT(" · %s mode · spent %d of an estimated %d credits"), *ZettaRunStatus::ModeLabel(Run.Mode), Run.SpentCredits, Run.EstimatedCredits);
		Out += FString::Printf(TEXT(" · seed: %s · created %s"), *SeedText(Run.Seed), *Ago(Run.CreatedAt, Now));
		if (Run.FinishedAt.IsSet()) Out += TEXT(" · finished ") + Ago(Run.FinishedAt.GetValue(), Now);
		Out += TEXT("\n");

		const TOptional<TArray<FString>> Order = ZettaGraphRules::TopologicalOrder(Run.Graph);
		auto Rank = [&Order](const FString& NodeId)
		{
			const int32 I = Order.IsSet() ? Order->IndexOfByKey(NodeId) : INDEX_NONE;
			return I == INDEX_NONE ? MAX_int32 : I;
		};
		TArray<const FZettaPipelineRunStep*> Steps;
		for (const FZettaPipelineRunStep& S : Run.Steps) Steps.Add(&S);
		Steps.StableSort([&Rank](const FZettaPipelineRunStep& A, const FZettaPipelineRunStep& B)
		{
			const int32 RA = Rank(A.NodeId), RB = Rank(B.NodeId);
			if (RA != RB) return RA < RB;
			if (A.PartKey != B.PartKey) return A.PartKey < B.PartKey;
			return A.Id < B.Id;
		});

		if (Steps.Num() == 0) Out += Run.Status == ZettaRunStatus::Draft ? TEXT("No steps yet — the run has not started.\n") : TEXT("No steps yet.\n");
		else Out += TEXT("Steps:\n");
		TArray<FString> Pictures;
		for (const FZettaPipelineRunStep* Step : Steps)
		{
			Out += TEXT("- ") + StepName(Run, *Step) + TEXT(" · ") + Step->Status;
			if (Step->Attempt > 1) Out += FString::Printf(TEXT(" (attempt %d)"), Step->Attempt);
			if (Step->Credits > 0) Out += FString::Printf(TEXT(" · %d cr"), Step->Credits);
			else if (Step->Status == ZettaStepStatus::Waiting)
			{
				const TOptional<int32> Price = StepPrice(Run, *Step, Prices, Gates);
				Out += !Price.IsSet() ? FString(TEXT(" · price unknown")) : Price.GetValue() == 0 ? FString(TEXT(" · free")) : FString::Printf(TEXT(" · will cost %d cr"), Price.GetValue());
			}
			TOptional<int64> GenId = Step->Gen3dPostId;
			if (!GenId.IsSet() && Step->Output.IsSet()) GenId = Step->Output->Gen3dPost();
			if (GenId.IsSet())
			{
				Out += FString::Printf(TEXT(" · model #%lld"), GenId.GetValue());
				if (Step->Generation.IsValid())
				{
					Out += FString::Printf(TEXT(" “%s”%s"), *Step->Generation->DisplayTitle(),
						Step->Generation->IsSuccess() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Step->Generation->Status));
				}
			}
			if (Step->Output.IsSet() && Step->Output->IsSplat()) Out += TEXT(" · Gaussian splat (not usable in Unreal)");
			if (Step->Output.IsSet() && Step->Output->IsFile()) Out += FString::Printf(TEXT(" · export file %s (web only)"), *Step->Output->Format);
			if (!Step->ErrorMessage.IsEmpty()) Out += TEXT(" · error: ") + Short(Step->ErrorMessage, 300);
			Out += TEXT("\n");

			if (!Step->Output.IsSet()) continue;
			const FZettaStepOutput& O = Step->Output.GetValue();
			if (O.IsPicture() && !O.Picture.Url.IsEmpty()) Pictures.Add(FString::Printf(TEXT("- step %lld (%s): %s"), Step->Id, *Step->NodeId, *O.Picture.Url));
			else if (O.IsViews())
			{
				for (const TPair<FString, FZettaSignedPicture>& V : O.Views)
					if (!V.Value.Url.IsEmpty()) Pictures.Add(FString::Printf(TEXT("- step %lld (%s) %s: %s"), Step->Id, *Step->NodeId, *V.Key, *V.Value.Url));
			}
			else if (O.IsIsolated())
			{
				if (!O.Character.Url.IsEmpty()) Pictures.Add(FString::Printf(TEXT("- step %lld (%s) character: %s"), Step->Id, *Step->NodeId, *O.Character.Url));
				for (const FZettaIsolatedPart& P : O.Parts)
					if (!P.Picture.Url.IsEmpty()) Pictures.Add(FString::Printf(TEXT("- step %lld (%s) part %s “%s”: %s"), Step->Id, *Step->NodeId, *P.Key, *P.Label, *P.Picture.Url));
			}
		}

		for (const FZettaPipelineRunStep* Step : Steps)
		{
			if (Step->Status != ZettaStepStatus::Review) continue;
			Out += FString::Printf(TEXT("Isolate parts review — %s waits for parts to be chosen:\n"), *StepName(Run, *Step));
			if (Step->Review.IsSet())
			{
				const FZettaStepReview& R = Step->Review.GetValue();
				if (!R.Subject.IsEmpty()) Out += FString::Printf(TEXT("  subject: %s%s\n"), *R.Subject, R.bHumanoid ? TEXT(" (humanoid)") : TEXT(""));
				if (R.Parts.Num() > 0)
				{
					TArray<FString> Found;
					for (const FZettaIdentifiedPart& P : R.Parts) Found.Add(FString::Printf(TEXT("%s “%s”%s"), *P.Key, *P.Label, P.Kind.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *P.Kind)));
					Out += TEXT("  parts found: ") + FString::Join(Found, TEXT(", ")) + TEXT("\n");
				}
				Out += TEXT("  proposedKeys: [") + FString::Join(R.ProposedKeys, TEXT(", ")) + TEXT("]\n");
			}
			Out += FString::Printf(TEXT("  → manage_pipeline_run { runId: %lld, action: \"choose_parts\", stepId: %lld, keys: [...], extraParts?: [...] }\n"), Run.Id, Step->Id);
		}

		TArray<const FZettaPipelineRunStep*> Waiting, Failed;
		for (const FZettaPipelineRunStep* S : Steps)
		{
			if (S->Status == ZettaStepStatus::Waiting) Waiting.Add(S);
			if (S->Status == ZettaStepStatus::Failed) Failed.Add(S);
		}
		if (Waiting.Num() > 0 && Run.Status != ZettaRunStatus::Draft)
		{
			const TOptional<int32> Total = StepsPrice(Run, Waiting, Prices, Gates);
			Out += FString::Printf(TEXT("Waiting for a Run: %s (%s) — manage_pipeline_run run_step (stepId) or run_waiting (all); the user approves the credits.\n"),
				*Plural(Waiting.Num(), TEXT("step")), Total.IsSet() ? *FString::Printf(TEXT("%d credits"), Total.GetValue()) : TEXT("price unknown"));
		}
		if (Failed.Num() > 0)
		{
			TArray<FString> Ids;
			for (const FZettaPipelineRunStep* S : Failed) Ids.Add(FString::Printf(TEXT("step %lld"), S->Id));
			Out += TEXT("Failed: ") + FString::Join(Ids, TEXT(", ")) + TEXT(" — retry_step (stepId) tries one again.\n");
		}
		if (Pictures.Num() > 0)
		{
			Out += TEXT("Picture outputs (signed URLs, they expire):\n");
			for (int32 I = 0; I < FMath::Min(40, Pictures.Num()); ++I) Out += Pictures[I] + TEXT("\n");
			if (Pictures.Num() > 40) Out += FString::Printf(TEXT("… +%d more\n"), Pictures.Num() - 40);
		}
		const TArray<const FZettaPipelineRunStep*> Importable = Run.ImportableSteps(EZettaRunImport::FinalSteps);
		if (Importable.Num() > 0)
		{
			TArray<FString> Lines;
			for (const FZettaPipelineRunStep* S : Importable)
			{
				Lines.AddUnique(FString::Printf(TEXT("%s (model #%lld)"), *S->NodeId, S->Generation.IsValid() ? S->Generation->Id : 0));
			}
			Out += TEXT("Importable results: ") + FString::Join(Lines, TEXT(", ")) + TEXT(" — import_pipeline_results.\n");
		}
		return Out.TrimEnd();
	}

	FString PipelineLine(const FZettaPipelineSummary& P)
	{
		const FDateTime Now = FDateTime::UtcNow();
		FString Out = FString::Printf(TEXT("#%lld “%s” — %s"), P.Id, *P.Title, *Plural(P.Steps, TEXT("step")));
		if (P.LastRunId.IsSet())
		{
			Out += FString::Printf(TEXT(" · last run #%lld %s (%s) %s"), P.LastRunId.GetValue(), *ZettaRunStatus::Label(P.LastRunStatus).ToLower(), *P.LastRunStatus, *Ago(P.LastRunAt, Now));
		}
		else Out += TEXT(" · never run");
		TArray<FString> Counts;
		for (const TPair<FString, int32>& C : P.RunCounts) if (C.Value > 0) Counts.Add(FString::Printf(TEXT("%d %s"), C.Value, *C.Key));
		if (Counts.Num() > 0) Out += TEXT(" · runs: ") + FString::Join(Counts, TEXT(", "));
		Out += TEXT(" · edited ") + Ago(P.UpdatedAt, Now);
		return Out;
	}

	FString RunLine(const FZettaRunSummary& R)
	{
		FString Out = FString::Printf(TEXT("#%lld %s%s · %s"), R.Id, *R.Status, R.bPaused && R.Status != ZettaRunStatus::Paused ? TEXT(" (paused)") : TEXT(""), *R.Mode);
		if (R.bHasProgress && R.Total > 0)
		{
			Out += FString::Printf(TEXT(" · %d/%d steps done"), R.Done, R.Total);
			if (R.Waiting > 0) Out += FString::Printf(TEXT(", %d waiting"), R.Waiting);
			if (R.Failed > 0) Out += FString::Printf(TEXT(", %d failed"), R.Failed);
		}
		if (R.InputsNeeded > 0) Out += FString::Printf(TEXT(" · %s needed"), *Plural(R.InputsNeeded, TEXT("input")));
		Out += FString::Printf(TEXT(" · %d/%d cr"), R.SpentCredits, R.EstimatedCredits);
		if (R.bHasSeed) Out += FString::Printf(TEXT(" · seed: %s%s"), *R.SeedType, R.SeedName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" “%s”"), *Short(R.SeedName, 40)));
		Out += TEXT(" · ") + Ago(R.CreatedAt, FDateTime::UtcNow());
		return Out;
	}
}
