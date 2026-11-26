// Copyright Zetta 3D. See LICENSE.md.

// THE GRAPH'S RULES (the web's canConnect, fanOutNodes, graphProblems, runInputProblems, nodeWarnings, estimateFor,
// with the server's structural checks added so nothing the server would refuse is missed).

#include "ZettaPipelineRecipe.h"

#include "ZettaPipelineJson.h"

using namespace ZettaPJson;

FString FZettaGraphEstimate::LabelFor(const TOptional<int32>& Credits, bool bPerPart)
{
	if (!Credits.IsSet()) return TEXT("—");
	return FString::Printf(TEXT("%s%d credits a run"), bPerPart ? TEXT("Up to ") : TEXT(""), Credits.GetValue());
}

namespace ZettaGraphRules
{
	/** Whether To is downstream of (or is) From, leaving out the edges Skip names. */
	static bool ReachesExcept(const FZettaPipelineGraph& Graph, const FString& From, const FString& To, TFunctionRef<bool(const FZettaPipelineEdge&)> Skip)
	{
		TSet<FString> Seen;
		TArray<FString> Stack = { From };
		while (Stack.Num() > 0)
		{
			const FString Id = Stack.Pop(EAllowShrinking::No);
			if (Id == To) return true;
			bool bAlready = false;
			Seen.Add(Id, &bAlready);
			if (bAlready) continue;
			for (const FZettaPipelineEdge& E : Graph.Edges)
			{
				if (E.Source == Id && !Skip(E)) Stack.Push(E.Target);
			}
		}
		return false;
	}

	bool Reaches(const FZettaPipelineGraph& Graph, const FString& From, const FString& To)
	{
		return ReachesExcept(Graph, From, To, [](const FZettaPipelineEdge&) { return false; });
	}

	static bool CanConnectExcept(const FZettaPipelineGraph& Graph, const FString& Source, const FString& SourceHandle, const FString& Target,
		const FString& TargetHandle, TFunctionRef<bool(const FZettaPipelineEdge&)> Skip)
	{
		if (Source == Target) return false;
		const FZettaPipelineNode* From = Graph.Node(Source);
		const FZettaPipelineNode* To = Graph.Node(Target);
		if (!From || !To) return false;
		const FString Carries = ZettaNodeCatalog::OutputType(From->Kind, SourceHandle);
		const FZettaInputPort* Port = ZettaNodeCatalog::InputPort(To->Kind, TargetHandle);
		if (Carries.IsEmpty() || !Port || !Port->AcceptsType(Carries)) return false;
		// A loop would never finish.
		return !ReachesExcept(Graph, Target, Source, Skip);
	}

	bool CanConnect(const FZettaPipelineGraph& Graph, const FString& Source, const FString& SourceHandle, const FString& Target, const FString& TargetHandle)
	{
		return CanConnectExcept(Graph, Source, SourceHandle, Target, TargetHandle, [](const FZettaPipelineEdge&) { return false; });
	}

	bool MayWire(const FZettaPipelineGraph& Graph, const FString& Source, const FString& SourceHandle, const FString& Target, const FString& TargetHandle, const FString& IgnoreEdgeId)
	{
		return CanConnectExcept(Graph, Source, SourceHandle, Target, TargetHandle, [&](const FZettaPipelineEdge& E)
		{
			return (E.Target == Target && E.TargetHandle == TargetHandle) || (!IgnoreEdgeId.IsEmpty() && E.Id == IgnoreEdgeId);
		});
	}

	FString ConnectionProblem(const FZettaPipelineGraph& Graph, const FString& Source, const FString& SourceHandle, const FString& Target, const FString& TargetHandle, const FString& IgnoreEdgeId)
	{
		if (MayWire(Graph, Source, SourceHandle, Target, TargetHandle, IgnoreEdgeId)) return FString();
		const FZettaPipelineNode* From = Graph.Node(Source);
		const FZettaPipelineNode* To = Graph.Node(Target);
		if (!From || !To || Source == Target) return FString();
		const FString Carries = ZettaNodeCatalog::OutputType(From->Kind, SourceHandle);
		const FZettaInputPort* Port = ZettaNodeCatalog::InputPort(To->Kind, TargetHandle);
		if (Carries.IsEmpty() || !Port) return FString();
		if (!Port->AcceptsType(Carries)) return ZettaNodeCatalog::MismatchMessage(To->Kind, Carries, Port->Accepts);
		return TEXT("That would loop back on itself.");
	}

	const FZettaPipelineEdge* InputEdge(const FZettaPipelineGraph& Graph, const FString& NodeId, const FString& Handle)
	{
		for (const FZettaPipelineEdge& E : Graph.Edges) if (E.Target == NodeId && E.TargetHandle == Handle) return &E;
		return nullptr;
	}

	const FZettaPipelineNode* SourceOf(const FZettaPipelineGraph& Graph, const FString& NodeId, const FString& Handle)
	{
		const FZettaPipelineEdge* E = InputEdge(Graph, NodeId, Handle);
		return E ? Graph.Node(E->Source) : nullptr;
	}

	FString IncomingType(const FZettaPipelineGraph& Graph, const FString& NodeId)
	{
		const FZettaPipelineEdge* E = InputEdge(Graph, NodeId);
		const FZettaPipelineNode* Source = E ? Graph.Node(E->Source) : nullptr;
		return Source ? ZettaNodeCatalog::OutputType(Source->Kind, E->SourceHandle) : FString();
	}

	TSet<FString> FanOutNodes(const FZettaPipelineGraph& Graph)
	{
		TSet<FString> Result;
		TFunction<void(const FString&)> Visit = [&](const FString& NodeId)
		{
			for (const FZettaPipelineEdge& E : Graph.Edges)
			{
				if (E.Source != NodeId || Result.Contains(E.Target)) continue;
				Result.Add(E.Target);
				Visit(E.Target);
			}
		};
		for (const FZettaPipelineEdge& E : Graph.Edges)
		{
			if (E.SourceHandle != TEXT("each") || Result.Contains(E.Target)) continue;
			Result.Add(E.Target);
			Visit(E.Target);
		}
		return Result;
	}

	TOptional<TArray<FString>> TopologicalOrder(const FZettaPipelineGraph& Graph)
	{
		TMap<FString, int32> Incoming;
		TArray<FString> Ids;
		for (const FZettaPipelineNode& N : Graph.Nodes)
		{
			if (!Incoming.Contains(N.Id))
			{
				Incoming.Add(N.Id, 0);
				Ids.Add(N.Id);
			}
		}
		for (const FZettaPipelineEdge& E : Graph.Edges)
		{
			if (int32* Count = Incoming.Find(E.Target)) ++*Count;
		}
		TArray<FString> Queue;
		for (const FString& Id : Ids) if (Incoming[Id] == 0) Queue.Add(Id);
		TArray<FString> Order;
		int32 Head = 0;
		while (Head < Queue.Num())
		{
			const FString Id = Queue[Head++];
			Order.Add(Id);
			for (const FZettaPipelineEdge& E : Graph.Edges)
			{
				if (E.Source != Id) continue;
				int32* Count = Incoming.Find(E.Target);
				if (!Count) continue;
				if (--*Count == 0) Queue.Add(E.Target);
			}
		}
		if (Order.Num() != Incoming.Num()) return {};
		return Order;
	}

	TArray<FZettaPipelineProblem> Problems(const FZettaPipelineGraph& Graph, const FZettaEntitlements3D* Gates)
	{
		const FZettaPipelineGraph Built = ZettaRequestBuilder::WithRequests(Graph, Gates);
		TArray<FZettaPipelineProblem> Out = GraphProblems(Built);
		for (const FZettaPipelineNode& Node : Graph.Nodes)
		{
			if (ZettaNodeCatalog::IsSeed(Node.Kind) || ZettaNodeCatalog::NotAvailableHere(Node.Kind)) continue;
			const FZettaNodeContext Context{ IncomingType(Graph, Node.Id), Gates };
			for (const FString& M : ZettaRequestBuilder::NodeProblems(Node.Kind, ZettaNodeSettings::Of(Node), Context)) Out.Add({ Node.Id, M });
		}
		return Out;
	}

	TArray<FZettaPipelineProblem> GraphProblems(const FZettaPipelineGraph& Graph)
	{
		TArray<FZettaPipelineProblem> Out;
		if (Graph.Nodes.Num() > 200) Out.Add({ FString(), TEXT("A pipeline holds at most 200 steps.") });

		TSet<FString> Ids;
		for (const FZettaPipelineNode& N : Graph.Nodes)
		{
			if (!ZettaNodeCatalog::IsKnown(N.Kind)) Out.Add({ N.Id, TEXT("This step is not one Zetta runs.") });
			bool bAlready = false;
			Ids.Add(N.Id, &bAlready);
			if (bAlready) Out.Add({ N.Id, TEXT("Two steps share one id.") });
		}

		if (!Graph.Seed()) Out.Add({ FString(), TEXT("Add a seed — the picture or model the pipeline starts from.") });
		int32 Seeds = 0;
		for (const FZettaPipelineNode& N : Graph.Nodes)
		{
			if (ZettaNodeCatalog::IsSeed(N.Kind) && ++Seeds > 1) Out.Add({ N.Id, TEXT("A pipeline starts from one seed.") });
		}

		TSet<FString> Filled;
		for (const FZettaPipelineEdge& E : Graph.Edges)
		{
			const FZettaPipelineNode* Source = Graph.Node(E.Source);
			const FZettaPipelineNode* Target = Graph.Node(E.Target);
			if (!Source || !Target || !ZettaNodeCatalog::IsKnown(Source->Kind) || !ZettaNodeCatalog::IsKnown(Target->Kind))
			{
				Out.Add({ FString(), TEXT("A connection points at a step that is gone.") });
				continue;
			}
			const FString Carries = ZettaNodeCatalog::OutputType(Source->Kind, E.SourceHandle);
			const FZettaInputPort* Port = ZettaNodeCatalog::InputPort(Target->Kind, E.TargetHandle);
			if (Carries.IsEmpty() || !Port)
			{
				Out.Add({ Target->Id, TEXT("A connection uses a socket this step does not have.") });
				continue;
			}
			if (!Port->AcceptsType(Carries)) Out.Add({ Target->Id, ZettaNodeCatalog::MismatchMessage(Target->Kind, Carries, Port->Accepts) });
			bool bAlready = false;
			Filled.Add(E.Target + TEXT(":") + E.TargetHandle, &bAlready);
			if (bAlready) Out.Add({ Target->Id, TEXT("An input takes one connection.") });
		}

		for (const FZettaPipelineNode& N : Graph.Nodes)
		{
			const FZettaNodeSpec* Spec = ZettaNodeCatalog::Spec(N.Kind);
			if (!Spec) continue;
			for (const FZettaInputPort& Input : Spec->Inputs)
			{
				if (Input.bRequired && !Filled.Contains(N.Id + TEXT(":") + Input.Handle)) Out.Add({ N.Id, TEXT("Connect this step to the one before it.") });
			}
		}

		if (!TopologicalOrder(Graph).IsSet()) Out.Add({ FString(), TEXT("The pipeline loops back on itself.") });

		for (const FZettaPipelineNode& N : Graph.Nodes)
		{
			const FZettaNodeSpec* Spec = ZettaNodeCatalog::Spec(N.Kind);
			if (!Spec || ZettaNodeCatalog::IsSeed(N.Kind)) continue;
			// AI texturing is always set on each run; AI edit when chosen. Otherwise its input must be here.
			const bool bPerRun = ZettaNodeSettings::SetOnEachRun(N);
			bool bMissing = false;
			if (!bPerRun)
			{
				FZettaPipelineGraph Single;
				Single.Nodes.Add(N);
				Single.Edges = Graph.Edges;
				bMissing = RunInputProblems(Single).Num() > 0;
			}
			if (bMissing && N.Kind == ZettaNodeKinds::AiEdit) Out.Add({ N.Id, TEXT("Say how to change the picture — or set it on each run.") });
			else if (Spec->HasEngineChoice() && !JIsString(JGet(N.Request, TEXT("model")))) Out.Add({ N.Id, TEXT("Choose an engine.") });
			else if (N.Kind == ZettaNodeKinds::Export && !JIsString(JGet(N.Request, TEXT("format")))) Out.Add({ N.Id, TEXT("Choose a file format.") });
		}
		return Out;
	}

	TArray<FZettaPipelineProblem> RunInputProblems(const FZettaPipelineGraph& Graph)
	{
		auto HasText = [](const FVal& V) { return JIsString(V) && !V->AsString().TrimStartAndEnd().IsEmpty(); };
		auto NameOr = [](const FZettaPipelineNode& N, const TCHAR* Fallback) { return N.Label.IsSet() && !N.Label->IsEmpty() ? N.Label.GetValue() : FString(Fallback); };
		TArray<FZettaPipelineProblem> Out;
		for (const FZettaPipelineNode& N : Graph.Nodes)
		{
			const TSharedPtr<FJsonObject> Request = N.Request.IsValid() ? N.Request : MakeShared<FJsonObject>();
			if (N.Kind == ZettaNodeKinds::AiEdit && !HasText(JGet(Request, TEXT("prompt"))))
				Out.Add({ N.Id, NameOr(N, TEXT("AI edit")) + TEXT(": say how to change the picture.") });
			// Multi-view AI texturing: views wired in, or the run's own pictures of each side, the front at least.
			if (N.Kind == ZettaNodeKinds::TextureMultiview)
			{
				bool bFront = false;
				for (const FVal& V : JArrayOf(JGet(Request, TEXT("referenceViews")))) if (JAsText(JGet(JObjectOf(V), TEXT("viewType"))) == TEXT("front")) bFront = true;
				if (!ZettaNodeSettings::ViewsWired(Graph, N.Id) && !bFront)
					Out.Add({ N.Id, NameOr(N, TEXT("Multi-view - AI texturing")) + TEXT(": add the pictures of each side — a front one at least.") });
				continue;
			}
			if (N.Kind == ZettaNodeKinds::Texture)
			{
				bool bGiven = HasText(JGet(Request, TEXT("prompt"))) || JIsString(JGet(Request, TEXT("referenceUploadKey"))) || JIsNumber(JGet(Request, TEXT("referenceImageId")));
				for (const FZettaPipelineEdge& E : Graph.Edges) if (E.Target == N.Id && E.TargetHandle == TEXT("reference")) bGiven = true;
				if (!bGiven) Out.Add({ N.Id, NameOr(N, TEXT("AI texturing")) + TEXT(": describe the texture or add a reference picture.") });
			}
		}
		return Out;
	}

	static FString Capitalised(const FString& S) { return S.IsEmpty() ? S : S.Left(1).ToUpper() + S.Mid(1); }

	/** The triangle budget arriving at a node: the nearest step upstream that sets one (quads count twice). */
	static TOptional<TPair<FString, int64>> PolygonBudgetBefore(const FZettaPipelineGraph& Graph, const FString& NodeId)
	{
		TSet<FString> Seen;
		FString Current = NodeId;
		while (true)
		{
			bool bAlready = false;
			Seen.Add(Current, &bAlready);
			if (bAlready) return {};
			const FZettaPipelineNode* Source = SourceOf(Graph, Current);
			if (!Source) return {};
			const TSharedRef<FJsonObject> Settings = ZettaNodeSettings::Of(*Source);
			const FString Label = Source->Label.IsSet() && !Source->Label->IsEmpty() ? Source->Label.GetValue() : Capitalised(Source->Kind.Replace(TEXT("_"), TEXT(" ")));
			const bool bQuads = JAsText(JGet(Settings, TEXT("polygonType"))) == TEXT("quadrilateral");
			if (Source->Kind == ZettaNodeKinds::Retopology)
			{
				if (JAsText(JGet(Settings, TEXT("engine"))) != TEXT("r1") || !JIsNumber(JGet(Settings, TEXT("faces")))) return {};
				const int64 Faces = (int64)JGet(Settings, TEXT("faces"))->AsNumber();
				return TPair<FString, int64>(Label, Faces * (bQuads ? 2 : 1));
			}
			if (ZettaNodeCatalog::IsGenerate(Source->Kind))
			{
				const FString Raw = JAsText(JGet(Settings, TEXT("polygonCount"))).TrimStartAndEnd();
				if (Raw.IsEmpty() || !Raw.IsNumeric()) return {};
				const double Count = FCString::Atod(*Raw);
				if (!FMath::IsFinite(Count) || Count <= 0) return {};
				return TPair<FString, int64>(Label, (int64)(Count * (bQuads ? 2 : 1)));
			}
			if (Source->Kind == ZettaNodeKinds::SeedModel) return {};
			Current = Source->Id;
		}
	}

	TArray<FString> Warnings(const FZettaPipelineGraph& Graph, const FString& NodeId)
	{
		TArray<FString> Out;
		const FZettaPipelineNode* Node = Graph.Node(NodeId);
		if (!Node) return Out;
		if (!ZettaNodeCatalog::IsKnown(Node->Kind))
			Out.Add(TEXT("This step is newer than this version of the plugin — it is kept as it is. Update the plugin to edit it."));
		else if (ZettaNodeCatalog::NotAvailableHere(Node->Kind))
			Out.Add(TEXT("This step is not available in Unreal — it is kept as it is; open the pipeline on zetta3d.ai to change it."));
		if (Node->Kind != ZettaNodeKinds::SmartUv) return Out;
		const TOptional<TPair<FString, int64>> Budget = PolygonBudgetBefore(Graph, NodeId);
		if (!Budget.IsSet() || Budget->Value <= ZettaToolSettings::SmartUvMaxTriangles) return Out;
		Out.Add(FString::Printf(TEXT("%s can make about %s triangles — over Smart UV’s %s. Add Retopology before it, or lower the polygon count."),
			*Budget->Key, *JCount(Budget->Value), *JCount(ZettaToolSettings::SmartUvMaxTriangles)));
		return Out;
	}

	FZettaGraphEstimate Estimate(const FZettaPipelineGraph& Graph, const FZettaPriceList* Prices, const FZettaEntitlements3D* Gates)
	{
		const TSet<FString> PerPart = FanOutNodes(Graph);
		int32 MaxParts = 1;
		for (const FZettaPipelineNode& N : Graph.Nodes)
		{
			if (N.Kind != ZettaNodeKinds::IsolateParts) continue;
			const FVal Raw = JGet(N.Settings, TEXT("maxParts"));
			const int32 Value = JIsNumber(Raw) ? (int32)Raw->AsNumber() : 0;
			MaxParts = FMath::Max(MaxParts, Value != 0 ? Value : 6);
		}
		FZettaGraphEstimate E;
		E.bPerPart = PerPart.Num() > 0;
		E.Credits = 0;
		for (const FZettaPipelineNode& N : Graph.Nodes)
		{
			const TOptional<int32> Price = ZettaRequestBuilder::PriceFor(N, Prices, { IncomingType(Graph, N.Id), Gates });
			const bool bPart = PerPart.Contains(N.Id);
			const int32 Count = bPart ? MaxParts : 1;
			E.Lines.Add({ N.Id, Price, Count, bPart });
			if (!Price.IsSet()) E.Credits.Reset();
			else if (E.Credits.IsSet()) E.Credits = E.Credits.GetValue() + Price.GetValue() * Count;
		}
		return E;
	}
}

// ─── Agent settings ─────────────────────────────────────────────────────────────────────────────────────────────────

namespace ZettaAgentSettings
{
	TSharedRef<FJsonObject> ToWeb(const FString& Kind, const TSharedPtr<FJsonObject>& AgentSettings)
	{
		TSharedRef<FJsonObject> Web = MakeShared<FJsonObject>();
		if (!AgentSettings.IsValid()) return Web;
		for (const TPair<FString, FVal>& Pair : AgentSettings->Values)
		{
			const FVal Value = JClone(Pair.Value);
			if (ZettaNodeCatalog::IsGenerate(Kind))
			{
				if (Pair.Key == TEXT("pbr"))
				{
					JSet(Web, TEXT("pbrMaterials"), Value);
					continue;
				}
				if (Pair.Key == TEXT("faceCount") || Pair.Key == TEXT("polygonCount"))
				{
					Web->SetStringField(TEXT("polygonCount"), JIsNull(Value) ? FString() : JAsText(Value));
					if (!JHas(AgentSettings, TEXT("polygonCountTyped"))) Web->SetBoolField(TEXT("polygonCountTyped"), true);
					continue;
				}
			}
			else if (Kind == ZettaNodeKinds::Retopology && Pair.Key == TEXT("faceCount"))
			{
				JSet(Web, TEXT("faces"), Value);
				continue;
			}
			else if ((Kind == ZettaNodeKinds::Texture || Kind == ZettaNodeKinds::TextureMultiview) && Pair.Key == TEXT("quality") && JIsString(Value))
			{
				const FString Q = Value->AsString();
				Web->SetStringField(TEXT("tier"), Q == TEXT("detailed") ? ZettaToolSettings::RetextureHd
					: (Q == TEXT("extreme") || Q == TEXT("8k")) ? ZettaToolSettings::Retexture8K : ZettaToolSettings::Retexture);
				continue;
			}
			JSet(Web, Pair.Key, Value);
		}
		return Web;
	}
}

// ─── Edits ──────────────────────────────────────────────────────────────────────────────────────────────────────────

namespace ZettaGraphEditing
{
	FString NewId(const FString& Prefix)
	{
		static const TCHAR* Base36 = TEXT("0123456789abcdefghijklmnopqrstuvwxyz");
		FString Out = Prefix + TEXT("-");
		for (int32 I = 0; I < 7; ++I) Out.AppendChar(Base36[FMath::RandRange(0, 35)]);
		return Out;
	}

	FString NodeIdPrefix(const FString& Kind) { return (Kind.IsEmpty() ? FString(TEXT("node")) : Kind).Replace(TEXT("_"), TEXT("-")); }

	bool AddStep(const FZettaPipelineGraph& Graph, const FString& Kind, const FString& AfterNodeId, const FString& Output,
		const TSharedPtr<FJsonObject>& Settings, const TOptional<FVector2D>& Position, const FZettaEntitlements3D* Gates,
		FZettaPipelineGraph& Out, FString& OutNodeId, FString& OutError)
	{
		const FZettaNodeSpec* Spec = ZettaNodeCatalog::Spec(Kind);
		if (!Spec)
		{
			OutError = FString::Printf(TEXT("“%s” is not a step Zetta runs."), *Kind);
			return false;
		}
		if (Spec->bNotAvailableHere)
		{
			OutError = FString::Printf(TEXT("%s is not available in Unreal."), *Spec->Label);
			return false;
		}
		if (ZettaNodeCatalog::IsSeed(Kind) && Graph.Seed())
		{
			OutError = TEXT("A pipeline starts from one seed.");
			return false;
		}
		const FZettaPipelineNode* After = nullptr;
		if (!AfterNodeId.IsEmpty())
		{
			After = Graph.Node(AfterNodeId);
			if (!After)
			{
				OutError = FString::Printf(TEXT("There is no step %s."), *AfterNodeId);
				return false;
			}
		}

		FZettaPipelineGraph Next = Graph;
		const FString NodeId = NewId(NodeIdPrefix(Kind));
		int32 Siblings = 0;
		if (After) for (const FZettaPipelineEdge& E : Next.Edges) if (E.Source == After->Id) ++Siblings;
		FVector2D Place;
		if (Position.IsSet()) Place = Position.GetValue();
		else if (After) Place = FVector2D(After->X + 300, After->Y + Siblings * 180);
		else Place = FVector2D(Next.Nodes.Num() * 60, Next.Nodes.Num() * 40);

		const TSharedRef<FJsonObject> NodeSettings = ZettaNodeSettings::WithDefaults(Kind, Settings.IsValid() ? Settings : MakeShared<FJsonObject>());
		FZettaPipelineNode Node;
		Node.Id = NodeId;
		Node.Kind = Kind;
		Node.X = Place.X;
		Node.Y = Place.Y;
		Node.Settings = NodeSettings;
		Node.Request = ZettaRequestBuilder::RequestFor(Kind, NodeSettings, { FString(), Gates });
		Next.Nodes.Add(MoveTemp(Node));

		if (After)
		{
			const FString AfterId = After->Id;
			const FString AfterKind = After->Kind;
			TArray<FString> Handles;
			if (AfterKind == ZettaNodeKinds::IsolateParts) Handles.Add(Output.IsEmpty() ? FString(TEXT("character")) : Output);
			else if (const FZettaNodeSpec* AfterSpec = ZettaNodeCatalog::Spec(AfterKind)) for (const FZettaOutputPort& P : AfterSpec->Outputs) Handles.Add(P.Handle);
			for (const FString& Handle : Handles)
			{
				if (!ZettaGraphRules::CanConnect(Next, AfterId, Handle, NodeId)) continue;
				FZettaPipelineEdge Edge;
				Edge.Id = NewId(TEXT("e"));
				Edge.Source = AfterId;
				Edge.SourceHandle = Handle;
				Edge.Target = NodeId;
				Edge.TargetHandle = TEXT("in");
				Next.Edges.Add(MoveTemp(Edge));
				// Built again now that it knows what arrives — a views input changes a generation's request.
				Next.Node(NodeId)->Request = ZettaRequestBuilder::RequestFor(Kind, NodeSettings, { ZettaGraphRules::IncomingType(Next, NodeId), Gates });
				break;
			}
		}
		Out = MoveTemp(Next);
		OutNodeId = NodeId;
		return true;
	}

	bool SetSettings(const FZettaPipelineGraph& Graph, const FString& NodeId, const TSharedPtr<FJsonObject>& Settings,
		const FZettaEntitlements3D* Gates, FZettaPipelineGraph& Out, FString& OutError)
	{
		FZettaPipelineGraph Next = Graph;
		FZettaPipelineNode* Node = Next.Node(NodeId);
		if (!Node)
		{
			OutError = FString::Printf(TEXT("There is no step %s."), *NodeId);
			return false;
		}
		if (ZettaNodeCatalog::NotAvailableHere(Node->Kind))
		{
			OutError = FString::Printf(TEXT("%s cannot be changed in Unreal."), *Node->DisplayName());
			return false;
		}
		Node->Settings = Settings.IsValid() ? JClone(Settings) : ZettaNodeSettings::Defaults(Node->Kind);
		Node->Request = ZettaRequestBuilder::RequestFor(*Node, Next, Gates);
		Out = MoveTemp(Next);
		return true;
	}

	bool MergeSettings(const FZettaPipelineGraph& Graph, const FString& NodeId, const TSharedPtr<FJsonObject>& Patch,
		const FZettaEntitlements3D* Gates, FZettaPipelineGraph& Out, FString& OutError)
	{
		const FZettaPipelineNode* Node = Graph.Node(NodeId);
		if (!Node)
		{
			OutError = FString::Printf(TEXT("There is no step %s."), *NodeId);
			return false;
		}
		const TSharedRef<FJsonObject> Merged = JClone(Node->Settings);
		JMerge(Merged, ZettaAgentSettings::ToWeb(Node->Kind, Patch));
		return SetSettings(Graph, NodeId, ZettaNodeSettings::WithDefaults(Node->Kind, Merged), Gates, Out, OutError);
	}

	FZettaPipelineGraph SetLabel(const FZettaPipelineGraph& Graph, const FString& NodeId, const FString& Label)
	{
		FZettaPipelineGraph Next = Graph;
		if (FZettaPipelineNode* Node = Next.Node(NodeId))
		{
			const FString Trimmed = Label.TrimStartAndEnd();
			if (Trimmed.IsEmpty()) Node->Label.Reset();
			else Node->Label = Label.Len() > 80 ? Label.Left(80) : Label;
		}
		return Next;
	}

	FZettaPipelineGraph Move(const FZettaPipelineGraph& Graph, const TMap<FString, FVector2D>& Positions)
	{
		FZettaPipelineGraph Next = Graph;
		for (const TPair<FString, FVector2D>& Pair : Positions)
		{
			if (FZettaPipelineNode* Node = Next.Node(Pair.Key))
			{
				Node->X = Pair.Value.X;
				Node->Y = Pair.Value.Y;
			}
		}
		return Next;
	}

	static void RebuildRequest(FZettaPipelineGraph& Graph, const FString& NodeId, const FZettaEntitlements3D* Gates)
	{
		FZettaPipelineNode* Node = Graph.Node(NodeId);
		if (!Node || ZettaNodeCatalog::NotAvailableHere(Node->Kind)) return;
		Node->Request = ZettaRequestBuilder::RequestFor(*Node, Graph, Gates);
	}

	FZettaPipelineGraph Remove(const FZettaPipelineGraph& Graph, const FString& NodeId)
	{
		FZettaPipelineGraph Next = Graph;
		Next.Nodes.RemoveAll([&NodeId](const FZettaPipelineNode& N) { return N.Id == NodeId; });
		Next.Edges.RemoveAll([&NodeId](const FZettaPipelineEdge& E) { return E.Source == NodeId || E.Target == NodeId; });
		TArray<FString> Generations;
		for (const FZettaPipelineNode& N : Next.Nodes) if (ZettaNodeCatalog::IsGenerate(N.Kind)) Generations.Add(N.Id);
		for (const FString& Id : Generations) RebuildRequest(Next, Id, nullptr);
		return Next;
	}

	bool Connect(const FZettaPipelineGraph& Graph, const FString& Source, const FString& SourceHandle, const FString& Target,
		const FString& TargetHandle, const FZettaEntitlements3D* Gates, FZettaPipelineGraph& Out, FString& OutError)
	{
		// An input takes one connection, so one already there is replaced.
		FZettaPipelineGraph Without = Graph;
		Without.Edges.RemoveAll([&](const FZettaPipelineEdge& E) { return E.Target == Target && E.TargetHandle == TargetHandle; });
		if (!ZettaGraphRules::CanConnect(Without, Source, SourceHandle, Target, TargetHandle))
		{
			OutError = ZettaGraphRules::ConnectionProblem(Without, Source, SourceHandle, Target, TargetHandle);
			if (OutError.IsEmpty()) OutError = TEXT("Those two steps cannot be connected that way.");
			return false;
		}
		FZettaPipelineEdge Edge;
		Edge.Id = NewId(TEXT("e"));
		Edge.Source = Source;
		Edge.SourceHandle = SourceHandle;
		Edge.Target = Target;
		Edge.TargetHandle = TargetHandle;
		Without.Edges.Add(MoveTemp(Edge));
		RebuildRequest(Without, Target, Gates);
		Out = MoveTemp(Without);
		return true;
	}

	bool Reconnect(const FZettaPipelineGraph& Graph, const FString& EdgeId, const FString& Source, const FString& SourceHandle,
		const FString& Target, const FString& TargetHandle, const FZettaEntitlements3D* Gates, FZettaPipelineGraph& Out, FString& OutError)
	{
		const FZettaPipelineEdge* Old = Graph.Edge(EdgeId);
		const FString OldTarget = Old ? Old->Target : FString();
		FZettaPipelineGraph Without = Graph;
		Without.Edges.RemoveAll([&EdgeId](const FZettaPipelineEdge& E) { return E.Id == EdgeId; });
		FZettaPipelineGraph Next;
		if (!Connect(Without, Source, SourceHandle, Target, TargetHandle, Gates, Next, OutError)) return false;
		if (!OldTarget.IsEmpty()) RebuildRequest(Next, OldTarget, Gates);
		Out = MoveTemp(Next);
		return true;
	}

	FZettaPipelineGraph Disconnect(const FZettaPipelineGraph& Graph, const FString& EdgeId, const FZettaEntitlements3D* Gates)
	{
		FZettaPipelineGraph Next = Graph;
		const FZettaPipelineEdge* Edge = Next.Edge(EdgeId);
		if (!Edge) return Next;
		const FString Target = Edge->Target;
		Next.Edges.RemoveAll([&EdgeId](const FZettaPipelineEdge& E) { return E.Id == EdgeId; });
		RebuildRequest(Next, Target, Gates);
		return Next;
	}

	FAgentChanges ApplyAgentChanges(const FZettaPipelineGraph& Graph, const TArray<FZettaPipelineChange>& Changes, const FZettaEntitlements3D* Gates)
	{
		FAgentChanges R;
		R.Graph = Graph;
		for (int32 Index = 0; Index < Changes.Num(); ++Index)
		{
			const FZettaPipelineChange& Change = Changes[Index];
			if (Change.Op == TEXT("remove"))
			{
				const FZettaPipelineNode* Node = R.Graph.Node(Change.NodeId);
				if (!Node || ZettaNodeCatalog::IsSeed(Node->Kind))
				{
					R.Skipped.Add({ Index, Node ? FString(TEXT("The seed stays.")) : FString::Printf(TEXT("There is no step %s."), *Change.NodeId) });
					continue;
				}
				const FString Id = Node->Id;
				R.Graph = Remove(R.Graph, Id);
				if (R.Focus == Id) R.Focus.Reset();
			}
			else if (Change.Op == TEXT("set"))
			{
				const FZettaPipelineNode* Node = R.Graph.Node(Change.NodeId);
				if (!Node || ZettaNodeCatalog::IsSeed(Node->Kind) || ZettaNodeCatalog::NotAvailableHere(Node->Kind))
				{
					R.Skipped.Add({ Index, Node ? FString::Printf(TEXT("%s cannot be changed here."), *Node->DisplayName()) : FString::Printf(TEXT("There is no step %s."), *Change.NodeId) });
					continue;
				}
				const FString Id = Node->Id;
				FZettaPipelineGraph Next;
				FString Error;
				if (!MergeSettings(R.Graph, Id, Change.Settings, Gates, Next, Error))
				{
					R.Skipped.Add({ Index, Error });
					continue;
				}
				R.Graph = MoveTemp(Next);
				R.Focus = Id;
			}
			else if (Change.Op == TEXT("add"))
			{
				const FZettaNodeSpec* Spec = ZettaNodeCatalog::Spec(Change.Kind);
				if (!Spec || !Spec->IsOfferedHere() || ZettaNodeCatalog::IsSeed(Change.Kind))
				{
					R.Skipped.Add({ Index, FString::Printf(TEXT("“%s” cannot be added here."), *Change.Kind) });
					continue;
				}
				const FZettaPipelineNode* After = !Change.After.IsEmpty() ? R.Graph.Node(Change.After) : R.Graph.Seed();
				if (!Change.After.IsEmpty() && !After)
				{
					R.Skipped.Add({ Index, FString::Printf(TEXT("There is no step %s."), *Change.After) });
					continue;
				}
				FZettaPipelineGraph Next;
				FString Id, Error;
				if (!AddStep(R.Graph, Change.Kind, After ? After->Id : FString(), Change.Output, ZettaAgentSettings::ToWeb(Change.Kind, Change.Settings), {}, Gates, Next, Id, Error))
				{
					R.Skipped.Add({ Index, Error });
					continue;
				}
				R.Graph = MoveTemp(Next);
				R.Added.Add(Id);
				R.Focus = Id;
			}
			else
			{
				R.Skipped.Add({ Index, FString::Printf(TEXT("Unknown change “%s”."), *Change.Op) });
			}
		}
		return R;
	}

	FBuilt Build(const FString& Title, const FString& StartsFrom, const TArray<FZettaBuildStep>& Steps, const FZettaEntitlements3D* Gates)
	{
		const FString SeedKind = StartsFrom == TEXT("model") ? ZettaNodeKinds::SeedModel : ZettaNodeKinds::SeedPicture;
		FZettaPipelineGraph Graph;
		{
			FZettaPipelineNode Seed;
			Seed.Id = TEXT("seed");
			Seed.Kind = SeedKind;
			Seed.Settings = ZettaNodeSettings::Defaults(SeedKind);
			Seed.Request = MakeShared<FJsonObject>();
			Graph.Nodes.Add(MoveTemp(Seed));
		}
		FBuilt R;
		const FString Trimmed = Title.TrimStartAndEnd();
		R.Title = Trimmed.IsEmpty() ? FString(TEXT("Pipeline from the Agent")) : Trimmed.Left(200);
		TArray<int32> Depth;
		TMap<int32, int32> Rows;
		const int32 Count = FMath::Min(Steps.Num(), 24);
		for (int32 I = 0; I < Count; ++I)
		{
			const FZettaBuildStep& Step = Steps[I];
			const FZettaNodeSpec* Spec = ZettaNodeCatalog::Spec(Step.Kind);
			if (!Spec || !Spec->IsOfferedHere() || ZettaNodeCatalog::IsSeed(Step.Kind))
			{
				R.Skipped.Add({ I, FString::Printf(TEXT("“%s” cannot be a step here."), *Step.Kind) });
				Depth.Add(1);
				R.StepNodeIds.Add(FString());
				continue;
			}
			const int32 After = Step.After.IsSet() && Step.After.GetValue() >= 0 && Step.After.GetValue() < I && !R.StepNodeIds[Step.After.GetValue()].IsEmpty()
				? Step.After.GetValue() : -1;
			const int32 Level = After < 0 ? 1 : Depth[After] + 1;
			Depth.Add(Level);
			const int32 Row = Rows.FindRef(Level);
			Rows.Add(Level, Row + 1);
			const FString NodeId = FString::Printf(TEXT("s%d"), I);
			FZettaPipelineNode Node;
			Node.Id = NodeId;
			Node.Kind = Step.Kind;
			Node.X = Level * 300;
			Node.Y = Row * 200;
			Node.Settings = ZettaNodeSettings::WithDefaults(Step.Kind, ZettaAgentSettings::ToWeb(Step.Kind, Step.Settings));
			Graph.Nodes.Add(MoveTemp(Node));
			const FString Source = After < 0 ? FString(TEXT("seed")) : R.StepNodeIds[After];
			const FString SourceKind = Graph.Node(Source)->Kind;
			FString Handle = TEXT("out");
			if (SourceKind == ZettaNodeKinds::IsolateParts)
			{
				Handle = Step.Output == TEXT("each") || Step.Output.StartsWith(ZettaNodeCatalog::PartHandlePrefix, ESearchCase::CaseSensitive) ? Step.Output : FString(TEXT("character"));
			}
			FZettaPipelineEdge Edge;
			Edge.Id = FString::Printf(TEXT("e%d"), I);
			Edge.Source = Source;
			Edge.SourceHandle = Handle;
			Edge.Target = NodeId;
			Edge.TargetHandle = TEXT("in");
			Graph.Edges.Add(MoveTemp(Edge));
			R.StepNodeIds.Add(NodeId);
		}
		// Draw views into Generate 3D becomes Multi-view - Generate 3D, as the server would rename it.
		TOptional<FZettaPipelineGraph> Renamed = ZettaNodeCatalog::WithMultiViewSteps(Graph);
		R.Graph = ZettaRequestBuilder::WithRequests(Renamed.IsSet() ? Renamed.GetValue() : Graph, Gates);
		return R;
	}
}

// ─── Undo ───────────────────────────────────────────────────────────────────────────────────────────────────────────

void FZettaUndoStack::Load(const FZettaPipelineGraph& Graph)
{
	Past.Reset();
	Future.Reset();
	LastKey.Reset();
	Current = Graph;
	OnChanged.Broadcast();
}

void FZettaUndoStack::Commit(const FZettaPipelineGraph& Next, const FString& Coalesce)
{
	const double Now = FPlatformTime::Seconds();
	const bool bContinues = !Coalesce.IsEmpty() && LastKey == Coalesce && Now - LastAt < CoalesceSeconds;
	if (!bContinues)
	{
		Past.Add(Current);
		if (Past.Num() > Limit) Past.RemoveAt(0, Past.Num() - Limit);
	}
	Current = Next;
	Future.Reset();
	LastKey = Coalesce;
	LastAt = Now;
	OnChanged.Broadcast();
}

void FZettaUndoStack::Replace(const FZettaPipelineGraph& Next)
{
	Current = Next;
	OnChanged.Broadcast();
}

bool FZettaUndoStack::Undo()
{
	if (Past.Num() == 0) return false;
	Future.Insert(MoveTemp(Current), 0);
	Current = Past.Pop();
	LastKey.Reset();
	OnChanged.Broadcast();
	return true;
}

bool FZettaUndoStack::Redo()
{
	if (Future.Num() == 0) return false;
	Past.Add(MoveTemp(Current));
	Current = Future[0];
	Future.RemoveAt(0);
	LastKey.Reset();
	OnChanged.Broadcast();
	return true;
}
