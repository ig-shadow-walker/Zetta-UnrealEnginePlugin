// Copyright Zetta 3D. See LICENSE.md.

// The graph (lossless JSON) and the node catalogue. The server's own rules are in the backend's pipeline-graph;
// change one, change the others.

#include "ZettaPipelineRecipe.h"

#include "ZettaPipelineJson.h"

using namespace ZettaPJson;

// ─── Graph ──────────────────────────────────────────────────────────────────────────────────────────────────────────

static TSharedPtr<FJsonObject> PlCloneOrNull(const TSharedPtr<FJsonObject>& O)
{
	return O.IsValid() ? TSharedPtr<FJsonObject>(ZettaPJson::JClone(O)) : nullptr;
}

FZettaPipelineNode& FZettaPipelineNode::operator=(const FZettaPipelineNode& Other)
{
	if (this == &Other) return *this;
	Id = Other.Id;
	Kind = Other.Kind;
	X = Other.X;
	Y = Other.Y;
	Settings = PlCloneOrNull(Other.Settings);
	Request = PlCloneOrNull(Other.Request);
	Label = Other.Label;
	Extra = PlCloneOrNull(Other.Extra);
	return *this;
}

FString FZettaPipelineNode::DisplayName() const
{
	if (Label.IsSet() && !Label->IsEmpty()) return Label.GetValue();
	const FZettaNodeSpec* Spec = ZettaNodeCatalog::Spec(Kind);
	return Spec ? Spec->Label : Kind;
}

TSharedRef<FJsonObject> FZettaPipelineNode::ToJson() const
{
	TSharedRef<FJsonObject> Json = ZettaPJson::JClone(Extra);
	Json->SetStringField(TEXT("id"), Id);
	Json->SetStringField(TEXT("kind"), Kind);
	TSharedRef<FJsonObject> Position = MakeShared<FJsonObject>();
	Position->SetNumberField(TEXT("x"), X);
	Position->SetNumberField(TEXT("y"), Y);
	Json->SetObjectField(TEXT("position"), Position);
	if (Settings.IsValid()) Json->SetObjectField(TEXT("settings"), ZettaPJson::JClone(Settings));
	if (Request.IsValid()) Json->SetObjectField(TEXT("request"), ZettaPJson::JClone(Request));
	if (Label.IsSet()) Json->SetStringField(TEXT("label"), Label.GetValue());
	return Json;
}

static double PlCoordinate(const FVal& V)
{
	if (!V.IsValid()) return 0;
	if (V->Type == EJson::Number) return V->AsNumber();
	if (V->Type == EJson::String)
	{
		const FString S = V->AsString().TrimStartAndEnd();
		return S.IsNumeric() ? FCString::Atod(*S) : 0;
	}
	return 0;
}

TOptional<FZettaPipelineNode> FZettaPipelineNode::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid() || !JIsString(JGet(Json, TEXT("id")))) return {};
	FZettaPipelineNode Node;
	Node.Extra = MakeShared<FJsonObject>();
	for (const TPair<FString, FVal>& Pair : Json->Values)
	{
		const FString& Key = Pair.Key;
		if (Key == TEXT("id")) Node.Id = Pair.Value->AsString();
		else if (Key == TEXT("kind")) Node.Kind = JIsString(Pair.Value) ? Pair.Value->AsString() : JAsText(Pair.Value);
		else if (Key == TEXT("position"))
		{
			const FObj P = JObjectOf(Pair.Value);
			Node.X = PlCoordinate(JGet(P, TEXT("x")));
			Node.Y = PlCoordinate(JGet(P, TEXT("y")));
		}
		else if (Key == TEXT("settings") && JIsObject(Pair.Value)) Node.Settings = ZettaPJson::JClone(Pair.Value->AsObject());
		else if (Key == TEXT("request") && JIsObject(Pair.Value)) Node.Request = ZettaPJson::JClone(Pair.Value->AsObject());
		else if (Key == TEXT("label") && JIsString(Pair.Value)) Node.Label = Pair.Value->AsString();
		else JSet(Node.Extra.ToSharedRef(), Key, Pair.Value);
	}
	return Node;
}

FZettaPipelineEdge& FZettaPipelineEdge::operator=(const FZettaPipelineEdge& Other)
{
	if (this == &Other) return *this;
	Id = Other.Id;
	Source = Other.Source;
	SourceHandle = Other.SourceHandle;
	Target = Other.Target;
	TargetHandle = Other.TargetHandle;
	Extra = PlCloneOrNull(Other.Extra);
	return *this;
}

TSharedRef<FJsonObject> FZettaPipelineEdge::ToJson() const
{
	TSharedRef<FJsonObject> Json = ZettaPJson::JClone(Extra);
	Json->SetStringField(TEXT("id"), Id);
	Json->SetStringField(TEXT("source"), Source);
	Json->SetStringField(TEXT("sourceHandle"), SourceHandle);
	Json->SetStringField(TEXT("target"), Target);
	Json->SetStringField(TEXT("targetHandle"), TargetHandle);
	return Json;
}

TOptional<FZettaPipelineEdge> FZettaPipelineEdge::FromJson(const TSharedPtr<FJsonObject>& Json, int32 Index)
{
	// The server's parse rules: source and target required; handles default to out / in.
	if (!Json.IsValid() || !JIsString(JGet(Json, TEXT("source"))) || !JIsString(JGet(Json, TEXT("target")))) return {};
	FZettaPipelineEdge Edge;
	Edge.Extra = MakeShared<FJsonObject>();
	for (const TPair<FString, FVal>& Pair : Json->Values)
	{
		const bool bText = JIsString(Pair.Value);
		const FString Text = bText ? Pair.Value->AsString() : FString();
		if (Pair.Key == TEXT("id") && bText) Edge.Id = Text;
		else if (Pair.Key == TEXT("source")) Edge.Source = Text;
		else if (Pair.Key == TEXT("target")) Edge.Target = Text;
		else if (Pair.Key == TEXT("sourceHandle") && bText) Edge.SourceHandle = Text;
		else if (Pair.Key == TEXT("targetHandle") && bText) Edge.TargetHandle = Text;
		else JSet(Edge.Extra.ToSharedRef(), Pair.Key, Pair.Value);
	}
	if (Edge.Id.IsEmpty()) Edge.Id = FString::Printf(TEXT("e%d"), Index);
	return Edge;
}

FZettaPipelineGraph& FZettaPipelineGraph::operator=(const FZettaPipelineGraph& Other)
{
	if (this == &Other) return *this;
	Version = Other.Version;
	Nodes = Other.Nodes;
	Edges = Other.Edges;
	Extra = PlCloneOrNull(Other.Extra);
	return *this;
}

const FZettaPipelineNode* FZettaPipelineGraph::Node(const FString& Id) const
{
	if (Id.IsEmpty()) return nullptr;
	for (const FZettaPipelineNode& N : Nodes) if (N.Id == Id) return &N;
	return nullptr;
}

FZettaPipelineNode* FZettaPipelineGraph::Node(const FString& Id)
{
	if (Id.IsEmpty()) return nullptr;
	for (FZettaPipelineNode& N : Nodes) if (N.Id == Id) return &N;
	return nullptr;
}

const FZettaPipelineEdge* FZettaPipelineGraph::Edge(const FString& Id) const
{
	if (Id.IsEmpty()) return nullptr;
	for (const FZettaPipelineEdge& E : Edges) if (E.Id == Id) return &E;
	return nullptr;
}

const FZettaPipelineNode* FZettaPipelineGraph::Seed() const
{
	for (const FZettaPipelineNode& N : Nodes) if (ZettaNodeCatalog::IsSeed(N.Kind)) return &N;
	return nullptr;
}

TSharedRef<FJsonObject> FZettaPipelineGraph::ToJson() const
{
	TSharedRef<FJsonObject> Json = ZettaPJson::JClone(Extra);
	Json->SetNumberField(TEXT("version"), Version);
	TArray<TSharedPtr<FJsonValue>> NodeValues;
	for (const FZettaPipelineNode& N : Nodes) NodeValues.Add(MakeShared<FJsonValueObject>(N.ToJson()));
	Json->SetArrayField(TEXT("nodes"), NodeValues);
	TArray<TSharedPtr<FJsonValue>> EdgeValues;
	for (const FZettaPipelineEdge& E : Edges) EdgeValues.Add(MakeShared<FJsonValueObject>(E.ToJson()));
	Json->SetArrayField(TEXT("edges"), EdgeValues);
	return Json;
}

FString FZettaPipelineGraph::ToString() const { return ZettaPJson::JWrite(ToJson()); }

FZettaPipelineGraph FZettaPipelineGraph::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	FZettaPipelineGraph Graph;
	Graph.Extra = MakeShared<FJsonObject>();
	if (!Json.IsValid()) return Graph;
	for (const TPair<FString, FVal>& Pair : Json->Values)
	{
		if (Pair.Key == TEXT("version"))
		{
			Graph.Version = JIsNumber(Pair.Value) ? (int32)Pair.Value->AsNumber() : 1;
		}
		else if (Pair.Key == TEXT("nodes") && JIsArray(Pair.Value))
		{
			for (const FVal& V : Pair.Value->AsArray())
			{
				TOptional<FZettaPipelineNode> Node = FZettaPipelineNode::FromJson(JObjectOf(V));
				if (Node.IsSet()) Graph.Nodes.Add(MoveTemp(Node.GetValue()));
			}
		}
		else if (Pair.Key == TEXT("edges") && JIsArray(Pair.Value))
		{
			int32 Index = 0;
			for (const FVal& V : Pair.Value->AsArray())
			{
				if (!JIsObject(V)) continue;
				TOptional<FZettaPipelineEdge> Edge = FZettaPipelineEdge::FromJson(V->AsObject(), Index++);
				if (Edge.IsSet()) Graph.Edges.Add(MoveTemp(Edge.GetValue()));
			}
		}
		else if (Pair.Key != TEXT("nodes") && Pair.Key != TEXT("edges"))
		{
			JSet(Graph.Extra.ToSharedRef(), Pair.Key, Pair.Value);
		}
	}
	return Graph;
}

TArray<FZettaPipelineProblem> FZettaPipelineProblem::ListFrom(const TArray<TSharedPtr<FJsonValue>>& Values)
{
	TArray<FZettaPipelineProblem> Out;
	for (const FVal& V : Values)
	{
		const FObj O = JObjectOf(V);
		if (!O.IsValid()) continue;
		Out.Add({ JTextOr(O, TEXT("nodeId")), JTextOr(O, TEXT("message")) });
	}
	return Out;
}

// ─── Kinds and ports ────────────────────────────────────────────────────────────────────────────────────────────────

namespace ZettaNodeKinds
{
	const TCHAR* SeedPicture = TEXT("seed_picture");
	const TCHAR* SeedModel = TEXT("seed_model");
	const TCHAR* SeedViews = TEXT("seed_views");
	const TCHAR* RemoveBackground = TEXT("remove_background");
	const TCHAR* AiEdit = TEXT("ai_edit");
	const TCHAR* DrawViews = TEXT("draw_views");
	const TCHAR* IsolateParts = TEXT("isolate_parts");
	const TCHAR* Generate3D = TEXT("generate_3d");
	const TCHAR* GenerateSmart3D = TEXT("generate_smart_3d");
	const TCHAR* Generate3DMultiview = TEXT("generate_3d_multiview");
	const TCHAR* GenerateSmart3DMultiview = TEXT("generate_smart_3d_multiview");
	const TCHAR* GenerateSplat = TEXT("generate_splat");
	const TCHAR* Retopology = TEXT("retopology");
	const TCHAR* Segmentation = TEXT("segmentation");
	const TCHAR* SmartSegmentation = TEXT("smart_segmentation");
	const TCHAR* Texture = TEXT("texture");
	const TCHAR* TextureMultiview = TEXT("texture_multiview");
	const TCHAR* SmartUv = TEXT("smart_uv");
	const TCHAR* Rigging = TEXT("rigging");
	const TCHAR* Export = TEXT("export");

	const TArray<FString>& All()
	{
		static const TArray<FString> List = {
			SeedPicture, SeedModel, SeedViews, RemoveBackground, AiEdit, DrawViews, IsolateParts, Generate3D, GenerateSmart3D,
			Generate3DMultiview, GenerateSmart3DMultiview, GenerateSplat, Retopology, Segmentation, SmartSegmentation, Texture,
			TextureMultiview, SmartUv, Rigging, Export,
		};
		return List;
	}
}

namespace ZettaPortTypes
{
	const TCHAR* Picture = TEXT("picture");
	const TCHAR* Views = TEXT("views");
	const TCHAR* Mesh = TEXT("mesh");
	const TCHAR* Splat = TEXT("splat");
	const TCHAR* File = TEXT("file");

	FLinearColor Colour(const FString& Type)
	{
		if (Type == Picture) return FLinearColor::FromSRGBColor(FColor(0xe5, 0x7c, 0xf5));
		if (Type == Views) return FLinearColor::FromSRGBColor(FColor(0xf8, 0xc6, 0xff));
		if (Type == Mesh) return FLinearColor::FromSRGBColor(FColor(0x5e, 0xa4, 0xff));
		if (Type == Splat) return FLinearColor::FromSRGBColor(FColor(0x42, 0xd3, 0xba));
		if (Type == File) return FLinearColor::FromSRGBColor(FColor(0xa8, 0xde, 0x52));
		return FLinearColor::FromSRGBColor(FColor(0x55, 0x4d, 0x42));
	}

	FString Label(const FString& Type)
	{
		if (Type == Picture) return TEXT("Picture");
		if (Type == Views) return TEXT("Views");
		if (Type == Mesh) return TEXT("Model");
		if (Type == Splat) return TEXT("Splat");
		if (Type == File) return TEXT("File");
		return Type;
	}
}

// ─── Catalogue ──────────────────────────────────────────────────────────────────────────────────────────────────────

bool FZettaNodeSpec::HasEngineChoice() const
{
	using namespace ZettaNodeKinds;
	return Kind == Generate3D || Kind == GenerateSmart3D || Kind == Generate3DMultiview || Kind == GenerateSmart3DMultiview
		|| Kind == GenerateSplat || Kind == Retopology;
}

const FZettaInputPort* FZettaNodeSpec::Input(const FString& Handle) const
{
	for (const FZettaInputPort& P : Inputs) if (P.Handle == Handle) return &P;
	return nullptr;
}

namespace ZettaNodeCatalog
{
	static TArray<FZettaInputPort> In(const FString& Label, const FString& Type)
	{
		return { FZettaInputPort{ TEXT("in"), Label, { Type }, true } };
	}

	static TArray<FZettaOutputPort> Out(const FString& Label, const FString& Type)
	{
		return { FZettaOutputPort{ TEXT("out"), Label, Type, false } };
	}

	static const TMap<FString, FZettaNodeSpec>& Specs()
	{
		static const TMap<FString, FZettaNodeSpec> Map = []()
		{
			using namespace ZettaNodeKinds;
			using namespace ZettaPortTypes;
			TArray<FZettaNodeSpec> List;
			auto Add = [&List](const TCHAR* Kind, const TCHAR* Label, const TCHAR* Blurb, EZettaNodeGroup Group,
				TArray<FZettaInputPort> Inputs, TArray<FZettaOutputPort> Outputs, bool bThreeD, bool bOffered, bool bNotHere = false, bool bEarly = false)
			{
				FZettaNodeSpec S;
				S.Kind = Kind;
				S.Label = Label;
				S.Blurb = Blurb;
				S.Group = Group;
				S.Inputs = MoveTemp(Inputs);
				S.Outputs = MoveTemp(Outputs);
				S.bThreeD = bThreeD;
				S.bOffered = bOffered;
				S.bNotAvailableHere = bNotHere;
				S.bEarlyAccess = bEarly;
				List.Add(MoveTemp(S));
			};
			Add(SeedPicture, TEXT("Picture seed"), TEXT("The picture the pipeline starts from — upload one, pick one, or make one."), EZettaNodeGroup::Seed, {}, Out(TEXT("Picture"), Picture), false, true);
			Add(SeedModel, TEXT("Model seed"), TEXT("A model from your library, or one you upload."), EZettaNodeGroup::Seed, {}, Out(TEXT("Model"), Mesh), false, true);
			// Each run gives pictures of each side, which reach a generation as a multi-view job — the shape Draw views makes.
			Add(SeedViews, TEXT("Multi-view seed"), TEXT("Pictures of each side — front, back, left, right — for a multi-view generation."), EZettaNodeGroup::Seed, {}, Out(TEXT("Views"), Views), false, true);
			Add(RemoveBackground, TEXT("Remove background"), TEXT("Cuts the subject out of its background. Free."), EZettaNodeGroup::Picture, In(TEXT("Picture"), Picture), Out(TEXT("Picture"), Picture), false, true);
			Add(AiEdit, TEXT("AI edit"), TEXT("Changes the picture by an instruction."), EZettaNodeGroup::Picture, In(TEXT("Picture"), Picture), Out(TEXT("Picture"), Picture), false, true);
			Add(DrawViews, TEXT("Draw views"), TEXT("Draws the back and sides from one picture, for a multi-view generation."), EZettaNodeGroup::Picture, In(TEXT("Picture"), Picture), Out(TEXT("Views"), Views), false, true);
			Add(IsolateParts, TEXT("Isolate parts"), TEXT("Splits a picture into its parts — the character, the weapon, the shield — each on its own."), EZettaNodeGroup::Picture,
				In(TEXT("Picture"), Picture), { { TEXT("character"), TEXT("Character"), Picture, false }, { TEXT("each"), TEXT("Each part"), Picture, false } }, false, true);
			// One picture, or pictures of each side — two steps each, as the Studio's two sources. Fed the wrong one, a
			// generation's error names the other (MismatchMessage).
			Add(Generate3D, TEXT("Generate 3D"), TEXT("A model from one picture. Zetta N1 or N2.5."), EZettaNodeGroup::Make, In(TEXT("Picture"), Picture), Out(TEXT("Model"), Mesh), true, true);
			Add(GenerateSmart3D, TEXT("Generate Smart 3D"), TEXT("Clean topology from one picture. Zetta Np1 or Np1.5."), EZettaNodeGroup::Make, In(TEXT("Picture"), Picture), Out(TEXT("Model"), Mesh), true, true);
			Add(Generate3DMultiview, TEXT("Multi-view - Generate 3D"), TEXT("A model from pictures of each side — a Multi-view seed or Draw views. Zetta N1 or N2.5."), EZettaNodeGroup::Make, In(TEXT("Views"), Views), Out(TEXT("Model"), Mesh), true, true);
			Add(GenerateSmart3DMultiview, TEXT("Multi-view - Smart 3D"), TEXT("Clean topology from pictures of each side — a Multi-view seed or Draw views. Zetta Np1 or Np1.5."), EZettaNodeGroup::Make, In(TEXT("Views"), Views), Out(TEXT("Model"), Mesh), true, true);
			Add(GenerateSplat, TEXT("Generate Splat"), TEXT("A splat object or environment from a picture."), EZettaNodeGroup::Make, In(TEXT("Picture"), Picture), Out(TEXT("Splat"), Splat), true, false, true, true);
			Add(Retopology, TEXT("Retopology"), TEXT("A clean mesh at the polygon budget you set."), EZettaNodeGroup::Refine, In(TEXT("Model"), Mesh), Out(TEXT("Model"), Mesh), true, true);
			Add(Segmentation, TEXT("Segmentation"), TEXT("Splits the model into parts — AI or Smart."), EZettaNodeGroup::Refine, In(TEXT("Model"), Mesh), Out(TEXT("Model"), Mesh), true, true);
			Add(SmartSegmentation, TEXT("Smart segmentation"), TEXT("Splits the model into named parts."), EZettaNodeGroup::Refine, In(TEXT("Model"), Mesh), Out(TEXT("Model"), Mesh), true, false, true);
			Add(Texture, TEXT("AI texturing"), TEXT("New textures, from a reference picture or a description."), EZettaNodeGroup::Refine, In(TEXT("Model"), Mesh), Out(TEXT("Model"), Mesh), true, true);
			// The model in, and pictures of each side — wired into Views from a Multi-view seed or Draw views (the same
			// pictures the model was made from), or, with nothing wired, given in each run.
			Add(TextureMultiview, TEXT("Multi-view - AI texturing"), TEXT("New textures from pictures of each side — wired from a Multi-view seed or Draw views, or given in each run."), EZettaNodeGroup::Refine,
				{ { TEXT("in"), TEXT("Model"), { Mesh }, true }, { TEXT("views"), TEXT("Views"), { Views }, false } }, Out(TEXT("Model"), Mesh), true, true);
			Add(SmartUv, TEXT("Smart UV"), TEXT("A new UV layout, textures re-baked onto it. Up to 80,000 triangles."), EZettaNodeGroup::Refine, In(TEXT("Model"), Mesh), Out(TEXT("Model"), Mesh), true, true);
			Add(Rigging, TEXT("Auto rig"), TEXT("A skeleton, skinned, ready to animate."), EZettaNodeGroup::Refine, In(TEXT("Model"), Mesh), Out(TEXT("Model"), Mesh), true, true);
			Add(Export, TEXT("Export"), TEXT("GLB, FBX, OBJ or STL. Free."), EZettaNodeGroup::Output, In(TEXT("Model"), Mesh), Out(TEXT("File"), File), false, false, true);
			TMap<FString, FZettaNodeSpec> Out;
			for (FZettaNodeSpec& S : List) Out.Add(S.Kind, MoveTemp(S));
			return Out;
		}();
		return Map;
	}

	TArray<const FZettaNodeSpec*> All()
	{
		TArray<const FZettaNodeSpec*> Out;
		for (const FString& Kind : ZettaNodeKinds::All()) Out.Add(Specs().Find(Kind));
		return Out;
	}

	TArray<const FZettaNodeSpec*> OfferedHere()
	{
		TArray<const FZettaNodeSpec*> Out;
		for (const FZettaNodeSpec* S : All()) if (S->IsOfferedHere()) Out.Add(S);
		return Out;
	}

	const TArray<TPair<EZettaNodeGroup, FString>>& Groups()
	{
		static const TArray<TPair<EZettaNodeGroup, FString>> List = {
			{ EZettaNodeGroup::Seed, TEXT("Seed") }, { EZettaNodeGroup::Picture, TEXT("Picture") }, { EZettaNodeGroup::Make, TEXT("Make") },
			{ EZettaNodeGroup::Refine, TEXT("Refine") }, { EZettaNodeGroup::Output, TEXT("Output") },
		};
		return List;
	}

	const FZettaNodeSpec* Spec(const FString& Kind) { return Kind.IsEmpty() ? nullptr : Specs().Find(Kind); }
	bool IsKnown(const FString& Kind) { return Spec(Kind) != nullptr; }

	bool IsSeed(const FString& Kind)
	{
		return Kind == ZettaNodeKinds::SeedPicture || Kind == ZettaNodeKinds::SeedModel || Kind == ZettaNodeKinds::SeedViews;
	}

	bool IsMultiViewGenerate(const FString& Kind)
	{
		return Kind == ZettaNodeKinds::Generate3DMultiview || Kind == ZettaNodeKinds::GenerateSmart3DMultiview;
	}

	bool IsGenerate(const FString& Kind)
	{
		return Kind == ZettaNodeKinds::Generate3D || Kind == ZettaNodeKinds::GenerateSmart3D || IsMultiViewGenerate(Kind);
	}

	bool IsSmartGenerate(const FString& Kind)
	{
		return Kind == ZettaNodeKinds::GenerateSmart3D || Kind == ZettaNodeKinds::GenerateSmart3DMultiview;
	}

	bool IsThreeD(const FString& Kind)
	{
		const FZettaNodeSpec* S = Spec(Kind);
		return S && S->bThreeD;
	}

	bool NotAvailableHere(const FString& Kind)
	{
		const FZettaNodeSpec* S = Spec(Kind);
		return S == nullptr || S->bNotAvailableHere;
	}

	FString Label(const FString& Kind)
	{
		const FZettaNodeSpec* S = Spec(Kind);
		return S ? S->Label : Kind;
	}

	static const TMap<FString, FString>& MultiViewOf()
	{
		static const TMap<FString, FString> Map = {
			{ ZettaNodeKinds::Generate3D, ZettaNodeKinds::Generate3DMultiview },
			{ ZettaNodeKinds::GenerateSmart3D, ZettaNodeKinds::GenerateSmart3DMultiview },
		};
		return Map;
	}

	static const TMap<FString, FString>& OnePictureOf()
	{
		static const TMap<FString, FString> Map = {
			{ ZettaNodeKinds::Generate3DMultiview, ZettaNodeKinds::Generate3D },
			{ ZettaNodeKinds::GenerateSmart3DMultiview, ZettaNodeKinds::GenerateSmart3D },
		};
		return Map;
	}

	FString MismatchMessage(const FString& Kind, const FString& Carries, const TArray<FString>& Accepts)
	{
		if (Carries == ZettaPortTypes::Views)
		{
			if (const FString* Multi = MultiViewOf().Find(Kind))
				return FString::Printf(TEXT("%s takes one picture. For pictures of each side, use %s."), *Label(Kind), *Label(*Multi));
		}
		if (Carries == ZettaPortTypes::Picture)
		{
			if (const FString* One = OnePictureOf().Find(Kind))
				return FString::Printf(TEXT("%s takes pictures of each side — connect a Multi-view seed or Draw views, or use %s for one picture."), *Label(Kind), *Label(*One));
		}
		return FString::Printf(TEXT("%s takes a %s here, not %s."), *Label(Kind), *FString::Join(Accepts, TEXT(" or ")),
			*(Carries == ZettaPortTypes::Views ? FString(TEXT("views")) : TEXT("a ") + Carries));
	}

	TOptional<FZettaPipelineGraph> WithMultiViewSteps(const FZettaPipelineGraph& Graph)
	{
		TSet<FString> FedViews;
		for (const FZettaPipelineEdge& E : Graph.Edges)
		{
			const FZettaPipelineNode* Source = Graph.Node(E.Source);
			if (E.TargetHandle == TEXT("in") && Source && OutputType(Source->Kind, E.SourceHandle) == ZettaPortTypes::Views) FedViews.Add(E.Target);
		}
		// AI texturing in its multi-view mode (the first take) is its own step now.
		auto MultiViewTexture = [](const FZettaPipelineNode& N)
		{
			return N.Kind == ZettaNodeKinds::Texture
				&& (JAsText(JGet(N.Request, TEXT("referenceMode"))) == TEXT("multi-view") || JAsText(JGet(N.Settings, TEXT("referenceMode"))) == TEXT("multi-view"));
		};
		bool bAny = false;
		for (const FZettaPipelineNode& N : Graph.Nodes)
		{
			if ((FedViews.Contains(N.Id) && MultiViewOf().Contains(N.Kind)) || MultiViewTexture(N)) bAny = true;
		}
		if (!bAny) return {};
		FZettaPipelineGraph Next = Graph;
		for (FZettaPipelineNode& N : Next.Nodes)
		{
			if (FedViews.Contains(N.Id) && MultiViewOf().Contains(N.Kind)) N.Kind = MultiViewOf()[N.Kind];
			else if (MultiViewTexture(N))
			{
				N.Kind = ZettaNodeKinds::TextureMultiview;
				if (N.Settings.IsValid()) N.Settings->RemoveField(TEXT("referenceMode"));
				if (N.Request.IsValid()) N.Request->RemoveField(TEXT("referenceMode"));
			}
		}
		return Next;
	}

	TArray<TPair<FString, FString>> KnownParts(const FZettaPipelineNode& Node)
	{
		TArray<TPair<FString, FString>> Out;
		for (const FVal& V : JArrayOf(JGet(Node.Settings, TEXT("knownParts"))))
		{
			const FObj Part = JObjectOf(V);
			const TOptional<FString> Key = JText(Part, TEXT("key"));
			const TOptional<FString> PartLabel = JText(Part, TEXT("label"));
			if (Key.IsSet() && PartLabel.IsSet()) Out.Add({ Key.GetValue(), PartLabel.GetValue() });
		}
		return Out;
	}

	TArray<FZettaOutputPort> OutputsOf(const FZettaPipelineNode& Node)
	{
		const FZettaNodeSpec* S = Spec(Node.Kind);
		if (!S) return {};
		TArray<FZettaOutputPort> Outputs = S->Outputs;
		if (Node.Kind == ZettaNodeKinds::IsolateParts)
		{
			for (const TPair<FString, FString>& Part : KnownParts(Node))
				Outputs.Add({ PartHandlePrefix + Part.Key, Part.Value, ZettaPortTypes::Picture, true });
		}
		return Outputs;
	}

	FString OutputType(const FString& Kind, const FString& Handle)
	{
		const FZettaNodeSpec* S = Spec(Kind);
		if (!S || Handle.IsEmpty()) return FString();
		for (const FZettaOutputPort& P : S->Outputs) if (P.Handle == Handle) return P.Type;
		if (Kind == ZettaNodeKinds::IsolateParts && Handle.StartsWith(PartHandlePrefix, ESearchCase::CaseSensitive)) return ZettaPortTypes::Picture;
		return FString();
	}

	const FZettaInputPort* InputPort(const FString& Kind, const FString& Handle)
	{
		const FZettaNodeSpec* S = Spec(Kind);
		return S ? S->Input(Handle) : nullptr;
	}
}
