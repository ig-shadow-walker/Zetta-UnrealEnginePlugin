// Copyright Zetta 3D. See LICENSE.md.

// WHAT EACH NODE SENDS (the web's node-config: requestFor / withRequests / nodeProblems / priceFor, and the editor's
// load normalisation). The agent builds and runs pipelines through this, so it must stay a faithful port.

#include "ZettaPipelineRecipe.h"

#include "ZettaPipelineJson.h"

using namespace ZettaPJson;

namespace ZettaRequestBuilder
{
	const TCHAR* InputPlaceholder = TEXT("https://pipeline.input/picture.png");

	TSharedRef<FJsonObject> RequestFor(const FZettaPipelineNode& Node, const FZettaPipelineGraph& Graph, const FZettaEntitlements3D* Gates)
	{
		return RequestFor(Node.Kind, ZettaNodeSettings::Of(Node), { ZettaGraphRules::IncomingType(Graph, Node.Id), Gates });
	}

	/** A multi-view step's context: views always arrive, whatever is wired in yet. */
	static FZettaNodeContext AsViews(const FZettaNodeContext& Context) { return { ZettaPortTypes::Views, Context.Gates }; }

	/**
	 * A generation's request: the plugin's ZettaRequests::Build over the form with a placeholder picture, then the
	 * inputs and the origin taken out (the server adds them); views coming in make it a multi-view job, priced as one;
	 * plus removeBackground. Problems are the web builder's.
	 */
	static TSharedRef<FJsonObject> GenerateRequest(const TSharedPtr<FJsonObject>& Form, const FZettaNodeContext& Context, TArray<FString>& OutProblems)
	{
		const bool bMayChoose = Context.MayChooseFaceCount();
		OutProblems = ZettaGenerateForm::ParameterProblems(Form, bMayChoose);
		FZettaGenerateOptions Options = ZettaGenerateForm::ToOptions(Form, bMayChoose);
		Options.ImageUrl = InputPlaceholder;
		Options.Prompt.Reset();
		Options.bImageFromPrompt = false;
		Options.Views.Reset();
		const FZettaBuiltRequest Built = ZettaRequests::Build(Options, Context.Gates);
		// The plugin builder bounds a count by its own table; the web's (stricter on N1) is the one ported above.
		for (const FString& P : Built.Problems)
		{
			if (!P.StartsWith(TEXT("The polygon count")) && !OutProblems.Contains(P)) OutProblems.Add(P);
		}
		if (OutProblems.Num() > 0 || !Built.Body.IsValid())
		{
			TSharedRef<FJsonObject> Engine = MakeShared<FJsonObject>();
			const FVal Model = JGet(Form, TEXT("model"));
			if (Model.IsValid()) JSet(Engine, TEXT("model"), Model);
			else Engine->SetStringField(TEXT("model"), ZettaGenerateForm::ModelOf(Form));
			return Engine;
		}

		TSharedRef<FJsonObject> Body = JClone(Built.Body);
		Body->RemoveField(TEXT("imageUrl"));
		Body->RemoveField(TEXT("prompt"));
		Body->RemoveField(TEXT("imageFromPrompt"));
		Body->RemoveField(TEXT("origin"));
		// The web sends the form's polygonType wherever the engine reads one (triangles beside parts too).
		const TOptional<FString> PolygonType = ZettaGenerateForm::SentPolygonType(Form, Built.Base, Built.Modifiers);
		if (PolygonType.IsSet()) Body->SetStringField(TEXT("polygonType"), PolygonType.GetValue());
		else Body->RemoveField(TEXT("polygonType"));
		if (Context.Incoming == ZettaPortTypes::Views)
		{
			Options.Views.Add(TPair<FString, FString>(TEXT("front"), InputPlaceholder));
			FString Base;
			TArray<FString> Modifiers;
			ZettaRequests::Spec(Options, Context.Gates, Base, Modifiers);
			TSharedRef<FJsonObject> Generation = MakeShared<FJsonObject>();
			Generation->SetStringField(TEXT("base"), Base);
			Generation->SetField(TEXT("modifiers"), JStrings(Modifiers));
			TSharedRef<FJsonObject> Spec = MakeShared<FJsonObject>();
			Spec->SetObjectField(TEXT("generation"), Generation);
			Body->SetObjectField(TEXT("spec"), Spec);
		}
		const FVal RemoveBg = JGet(Form, TEXT("removeBackground"));
		Body->SetBoolField(TEXT("removeBackground"), JIsBool(RemoveBg) && RemoveBg->AsBool());
		return Body;
	}

	static bool NotFalse(const FVal& V) { return !(JIsBool(V) && !V->AsBool()); }

	TSharedRef<FJsonObject> RequestFor(const FString& Kind, const TSharedPtr<FJsonObject>& InSettings, const FZettaNodeContext& Context)
	{
		using namespace ZettaNodeKinds;
		const TSharedRef<FJsonObject> Settings = InSettings.IsValid() ? InSettings.ToSharedRef() : ZettaNodeSettings::Defaults(Kind);
		if (Kind == SeedPicture || Kind == SeedModel || Kind == SeedViews || Kind == RemoveBackground)
		{
			// A seed is chosen for each run, never kept in the pipeline.
			return MakeShared<FJsonObject>();
		}
		if (Kind == AiEdit)
		{
			TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("prompt"), JTextOr(Settings, TEXT("prompt")));
			R->SetBoolField(TEXT("for3D"), NotFalse(JGet(Settings, TEXT("for3D"))));
			if (JTruthy(JGet(Settings, TEXT("model")))) JSet(R, TEXT("model"), JGet(Settings, TEXT("model")));
			return R;
		}
		if (Kind == DrawViews)
		{
			TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
			const FVal Views = JGet(Settings, TEXT("views"));
			if (!JIsNull(Views)) JSet(R, TEXT("views"), Views);
			else R->SetField(TEXT("views"), JStrings({ TEXT("back"), TEXT("left"), TEXT("right") }));
			return R;
		}
		if (Kind == IsolateParts)
		{
			const TSharedRef<FJsonObject> V = ZettaToolSettings::IsolateDefaults();
			JMerge(V, Settings);
			TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
			for (const TCHAR* Key : { TEXT("identify"), TEXT("sensitivity"), TEXT("maxParts"), TEXT("pose"), TEXT("extraParts") }) JSet(R, Key, JGet(V, Key));
			if (JTruthy(JGet(V, TEXT("model")))) JSet(R, TEXT("model"), JGet(V, TEXT("model")));
			return R;
		}
		if (Kind == Generate3D || Kind == GenerateSmart3D)
		{
			TArray<FString> Problems;
			return GenerateRequest(Settings, Context, Problems);
		}
		// Pictures of each side, always — built as the Studio's Multi-View to 3D builds them.
		if (Kind == Generate3DMultiview || Kind == GenerateSmart3DMultiview)
		{
			TArray<FString> Problems;
			return GenerateRequest(Settings, AsViews(Context), Problems);
		}
		if (Kind == GenerateSplat) return ZettaToolSettings::SplatRequest(Settings);
		if (Kind == Retopology) return ZettaToolSettings::RetopologyRequest(Settings);
		if (Kind == Segmentation || Kind == SmartSegmentation) return ZettaToolSettings::SegmentRequest(Settings);
		// Pictures of each side in place of a description or one picture; views wired in win.
		if (Kind == TextureMultiview)
		{
			TSharedRef<FJsonObject> R = ZettaToolSettings::TextureRequest(Settings, {});
			R->SetArrayField(TEXT("referenceViews"), ZettaNodeSettings::ViewsRequest(JObject(Settings, TEXT("referenceViews"))));
			R->SetBoolField(TEXT("removeReferenceBackground"), NotFalse(JGet(Settings, TEXT("removeBg"))));
			return R;
		}
		if (Kind == Texture)
		{
			const FObj Reference = JObject(Settings, TEXT("reference"));
			// A picture replaces the description, as in the Studio — sending both is refused.
			const TOptional<FString> Prompt = Reference.IsValid() ? TOptional<FString>() : JText(Settings, TEXT("prompt"));
			TSharedRef<FJsonObject> R = ZettaToolSettings::TextureRequest(Settings, Prompt);
			// Read by the pipeline, never sent to the 3D API: the node's own reference, and its cut-out.
			if (Reference.IsValid() && JTruthy(JGet(Reference, TEXT("uploadKey")))) JSet(R, TEXT("referenceUploadKey"), JGet(Reference, TEXT("uploadKey")));
			if (Reference.IsValid() && JIsNumber(JGet(Reference, TEXT("gen2dPostId")))) JSet(R, TEXT("referenceImageId"), JGet(Reference, TEXT("gen2dPostId")));
			R->SetBoolField(TEXT("removeReferenceBackground"), NotFalse(JGet(Settings, TEXT("removeBg"))));
			return R;
		}
		if (Kind == SmartUv) return ZettaToolSettings::SmartUvRequest(Settings);
		if (Kind == Rigging) return ZettaToolSettings::AutoRigRequest(Settings);
		if (Kind == Export)
		{
			TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
			const FVal Format = JGet(Settings, TEXT("format"));
			if (!JIsNull(Format)) JSet(R, TEXT("format"), Format);
			else R->SetStringField(TEXT("format"), TEXT("glb"));
			return R;
		}
		return MakeShared<FJsonObject>();
	}

	TArray<FString> NodeProblems(const FString& Kind, const TSharedPtr<FJsonObject>& InSettings, const FZettaNodeContext& Context)
	{
		const TSharedRef<FJsonObject> Settings = InSettings.IsValid() ? InSettings.ToSharedRef() : ZettaNodeSettings::Defaults(Kind);
		if (Kind == ZettaNodeKinds::Retopology)
		{
			const FString Problem = ZettaToolSettings::RetopologyRangeProblem(Settings);
			return Problem.IsEmpty() ? TArray<FString>() : TArray<FString>{ Problem };
		}
		if (!ZettaNodeCatalog::IsGenerate(Kind)) return {};
		TArray<FString> Problems;
		GenerateRequest(Settings, ZettaNodeCatalog::IsMultiViewGenerate(Kind) ? AsViews(Context) : Context, Problems);
		return Problems;
	}

	TOptional<int32> PriceFor(const FZettaPipelineNode& Node, const FZettaPriceList* Prices, const FZettaNodeContext& Context)
	{
		using namespace ZettaNodeKinds;
		const TSharedRef<FJsonObject> Settings = ZettaNodeSettings::Of(Node);
		const FString& Kind = Node.Kind;
		if (ZettaNodeCatalog::IsGenerate(Kind))
		{
			const TSharedRef<FJsonObject> Request = RequestFor(Kind, Settings, Context);
			const FObj Generation = JObject(JObject(Request, TEXT("spec")), TEXT("generation"));
			if (!Generation.IsValid() || !Prices) return {};
			TArray<FString> Codes = { JAsText(JGet(Generation, TEXT("base"))) };
			Codes.Append(JStringsOf(JGet(Generation, TEXT("modifiers"))));
			return Prices->Total(JText(Request, TEXT("model")).Get(FString()), Codes);
		}
		if (Kind == GenerateSplat) return ZettaToolSettings::SplatPrice(Prices, Settings);
		if (Kind == Retopology) return ZettaToolSettings::RetopologyPrice(Prices, Settings);
		if (Kind == Segmentation || Kind == SmartSegmentation) return ZettaToolSettings::SegmentPrice(Prices, Settings);
		if (Kind == Texture || Kind == TextureMultiview) return ZettaToolSettings::TexturePrice(Prices, Settings);
		if (Kind == SmartUv) return ZettaToolSettings::SmartUvPrice(Prices);
		if (Kind == Rigging) return ZettaToolSettings::AutoRigPrice(Prices);
		return 0;
	}

	FString PriceLabel(const TOptional<int32>& Price, bool bPerPart)
	{
		if (!Price.IsSet()) return TEXT("—");
		if (Price.GetValue() == 0) return TEXT("Free");
		return FString::Printf(TEXT("%d cr%s"), Price.GetValue(), bPerPart ? TEXT(" / part") : TEXT(""));
	}

	FZettaPipelineGraph WithRequests(const FZettaPipelineGraph& Graph, const FZettaEntitlements3D* Gates)
	{
		FZettaPipelineGraph Next = Graph;
		for (FZettaPipelineNode& Node : Next.Nodes)
		{
			// Nodes out of scope (and unknown kinds) keep the request they came with, untouched.
			if (ZettaNodeCatalog::NotAvailableHere(Node.Kind)) continue;
			Node.Request = RequestFor(Node.Kind, ZettaNodeSettings::Of(Node), { ZettaGraphRules::IncomingType(Next, Node.Id), Gates });
		}
		return Next;
	}

	static void WithoutRunInputInPlace(FZettaPipelineNode& Node)
	{
		if (!ZettaNodeSettings::SetOnEachRun(Node)) return;
		const TSharedRef<FJsonObject> S = JClone(Node.Settings);
		Node.Settings = S;
		if (Node.Kind == ZettaNodeKinds::TextureMultiview)
		{
			S->SetObjectField(TEXT("referenceViews"), MakeShared<FJsonObject>());
			return;
		}
		S->SetStringField(TEXT("prompt"), TEXT(""));
		if (Node.Kind == ZettaNodeKinds::Texture)
		{
			S->SetField(TEXT("reference"), JNull());
			S->SetObjectField(TEXT("referenceViews"), MakeShared<FJsonObject>());
		}
	}

	FNormalized Normalize(const FZettaPipelineGraph& Saved)
	{
		// A generation fed views opens as its multi-view step (the server renames it the same way when it reads it).
		TOptional<FZettaPipelineGraph> Renamed = ZettaNodeCatalog::WithMultiViewSteps(Saved);
		FNormalized Out;
		Out.Graph = Renamed.IsSet() ? MoveTemp(Renamed.GetValue()) : Saved;
		FZettaPipelineGraph& Graph = Out.Graph;
		for (FZettaPipelineNode& Node : Graph.Nodes)
		{
			if (ZettaNodeCatalog::NotAvailableHere(Node.Kind)) continue;
			Node.Settings = ZettaNodeSettings::WithDefaults(Node.Kind, Node.Settings);
			if (ZettaNodeCatalog::IsSeed(Node.Kind))
			{
				Node.Settings = ZettaNodeSettings::Defaults(Node.Kind);
				Node.Request = MakeShared<FJsonObject>();
			}
			WithoutRunInputInPlace(Node);
		}
		// A wire into a socket a step does not have is dropped.
		Graph.Edges.RemoveAll([&Graph](const FZettaPipelineEdge& Edge)
		{
			const FZettaPipelineNode* Target = Graph.Node(Edge.Target);
			return !(Target == nullptr || !ZettaNodeCatalog::IsKnown(Target->Kind) || ZettaNodeCatalog::InputPort(Target->Kind, Edge.TargetHandle) != nullptr);
		});
		for (const FZettaPipelineNode& Node : Saved.Nodes)
		{
			if (ZettaNodeCatalog::IsThreeD(Node.Kind) && !ZettaNodeCatalog::NotAvailableHere(Node.Kind) && !JObject(Node.Request, TEXT("spec")).IsValid())
			{
				Out.bNeedsRequestRebuild = true;
			}
		}
		return Out;
	}

	FZettaPipelineGraph ForNewRun(const FZettaPipelineGraph& Graph)
	{
		FZettaPipelineGraph Next = Graph;
		for (FZettaPipelineNode& Node : Next.Nodes) WithoutRunInputInPlace(Node);
		return Next;
	}

	bool RunNodeChange(const FZettaPipelineGraph& RunGraph, const FString& NodeId, const TSharedPtr<FJsonObject>& Patch,
		const FZettaEntitlements3D* Gates, TSharedPtr<FJsonObject>& OutSettings, TSharedPtr<FJsonObject>& OutRequest, FString& OutError)
	{
		const FZettaPipelineNode* Node = RunGraph.Node(NodeId);
		if (!Node)
		{
			OutError = FString::Printf(TEXT("There is no step %s in this run."), *NodeId);
			return false;
		}
		TSharedRef<FJsonObject> Settings = ZettaNodeSettings::WithDefaults(Node->Kind, Node->Settings);
		if (Patch.IsValid())
		{
			if (ZettaNodeCatalog::IsGenerate(Node->Kind)) Settings = ZettaGenerateForm::ApplyPatch(Settings, Patch);
			else JMerge(Settings, Patch);
		}
		OutSettings = Settings;
		OutRequest = RequestFor(Node->Kind, Settings, { ZettaGraphRules::IncomingType(RunGraph, NodeId), Gates });
		return true;
	}
}
