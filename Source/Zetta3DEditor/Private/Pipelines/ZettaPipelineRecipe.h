// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "ZettaModels.h"
#include "ZettaRequests.h"

// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// A 3D PIPELINE'S RECIPE — the graph the web editor and the server keep, its node catalogue, what each node sends, and
// the rules that say what may connect and what stops it running. Ports of the web editor's own modules, so a pipeline
// means the same thing in every editor. Everything here is pure: no UI, no network.
// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

/** One step of a pipeline. Copies are deep: a copy never shares its settings with the original. */
struct FZettaPipelineNode
{
	FString Id;
	/** One of ZettaNodeKinds — or a kind this version does not know, kept as it came. */
	FString Kind;
	/** Where it sits on the canvas, in the web editor's pixels. */
	double X = 0;
	double Y = 0;
	/** The node's form state, web-shaped (the Studio form's own value). Null when the graph has none. */
	TSharedPtr<FJsonObject> Settings;
	/** What the form builds for the API, minus its inputs — the only thing the server reads. */
	TSharedPtr<FJsonObject> Request;
	/** A name the user gave it. */
	TOptional<FString> Label;
	/** Properties of the node this plugin does not model, kept verbatim. */
	TSharedPtr<FJsonObject> Extra;

	FZettaPipelineNode() = default;
	FZettaPipelineNode(const FZettaPipelineNode& Other) { *this = Other; }
	FZettaPipelineNode& operator=(const FZettaPipelineNode& Other);
	FZettaPipelineNode(FZettaPipelineNode&&) = default;
	FZettaPipelineNode& operator=(FZettaPipelineNode&&) = default;

	/** The user's name for it, else the catalogue's. */
	FString DisplayName() const;
	TSharedRef<FJsonObject> ToJson() const;
	static TOptional<FZettaPipelineNode> FromJson(const TSharedPtr<FJsonObject>& Json);
};

/** A connection from one node's output to another's input. */
struct FZettaPipelineEdge
{
	FString Id;
	FString Source;
	FString SourceHandle = TEXT("out");
	FString Target;
	FString TargetHandle = TEXT("in");
	TSharedPtr<FJsonObject> Extra;

	FZettaPipelineEdge() = default;
	FZettaPipelineEdge(const FZettaPipelineEdge& Other) { *this = Other; }
	FZettaPipelineEdge& operator=(const FZettaPipelineEdge& Other);
	FZettaPipelineEdge(FZettaPipelineEdge&&) = default;
	FZettaPipelineEdge& operator=(FZettaPipelineEdge&&) = default;

	TSharedRef<FJsonObject> ToJson() const;
	static TOptional<FZettaPipelineEdge> FromJson(const TSharedPtr<FJsonObject>& Json, int32 Index);
};

/**
 * The recipe: nodes that are the Studio's own tools, wired from ONE seed. LOSSLESS — every property this plugin
 * does not model is kept and written back as it came, so a graph saved by a newer web editor survives a round trip.
 * Treated as a value: edits return a new graph (copies are deep).
 */
struct FZettaPipelineGraph
{
	int32 Version = 1;
	TArray<FZettaPipelineNode> Nodes;
	TArray<FZettaPipelineEdge> Edges;
	TSharedPtr<FJsonObject> Extra;

	FZettaPipelineGraph() = default;
	FZettaPipelineGraph(const FZettaPipelineGraph& Other) { *this = Other; }
	FZettaPipelineGraph& operator=(const FZettaPipelineGraph& Other);
	FZettaPipelineGraph(FZettaPipelineGraph&&) = default;
	FZettaPipelineGraph& operator=(FZettaPipelineGraph&&) = default;

	const FZettaPipelineNode* Node(const FString& Id) const;
	FZettaPipelineNode* Node(const FString& Id);
	const FZettaPipelineEdge* Edge(const FString& Id) const;
	/** The seed node (the first, if a broken graph has several), or null. */
	const FZettaPipelineNode* Seed() const;

	TSharedRef<FJsonObject> ToJson() const;
	FString ToString() const;
	/** Reads a graph as the web editor would: missing or malformed parts become empty, never an error. */
	static FZettaPipelineGraph FromJson(const TSharedPtr<FJsonObject>& Json);
};

/** What stops a graph from running, pinned to its node (empty NodeId = the graph as a whole). */
struct FZettaPipelineProblem
{
	FString NodeId;
	FString Message;
	FString ToString() const { return NodeId.IsEmpty() ? Message : FString::Printf(TEXT("[%s] %s"), *NodeId, *Message); }
	static TArray<FZettaPipelineProblem> ListFrom(const TArray<TSharedPtr<FJsonValue>>& Values);
};

// ─── The catalogue ──────────────────────────────────────────────────────────────────────────────────────────────────

/** The node kinds the server knows. */
namespace ZettaNodeKinds
{
	extern const TCHAR* SeedPicture;
	extern const TCHAR* SeedModel;
	extern const TCHAR* SeedViews;
	extern const TCHAR* RemoveBackground;
	extern const TCHAR* AiEdit;
	extern const TCHAR* DrawViews;
	extern const TCHAR* IsolateParts;
	extern const TCHAR* Generate3D;
	extern const TCHAR* GenerateSmart3D;
	extern const TCHAR* Generate3DMultiview;
	extern const TCHAR* GenerateSmart3DMultiview;
	extern const TCHAR* GenerateSplat;
	extern const TCHAR* Retopology;
	extern const TCHAR* Segmentation;
	extern const TCHAR* SmartSegmentation;
	extern const TCHAR* Texture;
	extern const TCHAR* TextureMultiview;
	extern const TCHAR* SmartUv;
	extern const TCHAR* Rigging;
	extern const TCHAR* Export;
	const TArray<FString>& All();
}

/** What travels along a connection. */
namespace ZettaPortTypes
{
	extern const TCHAR* Picture;
	extern const TCHAR* Views;
	extern const TCHAR* Mesh;
	extern const TCHAR* Splat;
	extern const TCHAR* File;
	/** The colour each kind of connection wears on the canvas. */
	FLinearColor Colour(const FString& Type);
	FString Label(const FString& Type);
}

enum class EZettaNodeGroup : uint8 { Seed, Picture, Make, Refine, Output };

struct FZettaInputPort
{
	FString Handle;
	FString Label;
	TArray<FString> Accepts;
	bool bRequired = true;
	bool AcceptsType(const FString& Type) const { return !Type.IsEmpty() && Accepts.Contains(Type); }
};

struct FZettaOutputPort
{
	FString Handle;
	FString Label;
	FString Type;
	/** A part:<key> output learnt from a run (Isolate Parts' settings.knownParts). */
	bool bDynamic = false;
};

/** What one kind of node is. */
struct FZettaNodeSpec
{
	FString Kind;
	FString Label;
	/** One line for the palette. */
	FString Blurb;
	EZettaNodeGroup Group = EZettaNodeGroup::Refine;
	TArray<FZettaInputPort> Inputs;
	TArray<FZettaOutputPort> Outputs;
	/** A 3D job — queued, charged in 3D credits, and watched. Otherwise the step is instant. */
	bool bThreeD = false;
	/** Shown in the web palette. */
	bool bOffered = false;
	/**
	 * Out of this plugin's scope (Gaussian splats, the legacy Smart segmentation node, Export). A graph that has one
	 * still loads, shows and round-trips untouched — the node is just not offered, edited or rebuilt here.
	 */
	bool bNotAvailableHere = false;
	bool bEarlyAccess = false;

	bool IsOfferedHere() const { return bOffered && !bNotAvailableHere; }
	bool HasEngineChoice() const;
	const FZettaInputPort* Input(const FString& Handle) const;
};

/** The node catalogue (labels, blurbs, groups, sockets) plus this plugin's scope. */
namespace ZettaNodeCatalog
{
	inline const TCHAR* PartHandlePrefix = TEXT("part:");

	/** Every known kind, in the server's order. */
	TArray<const FZettaNodeSpec*> All();
	/** What the palette (and the agent) may add here, in order. */
	TArray<const FZettaNodeSpec*> OfferedHere();
	/** The groups and their palette titles. */
	const TArray<TPair<EZettaNodeGroup, FString>>& Groups();

	const FZettaNodeSpec* Spec(const FString& Kind);
	bool IsKnown(const FString& Kind);
	bool IsSeed(const FString& Kind);
	bool IsMultiViewGenerate(const FString& Kind);
	/** Any of the four Generate steps — they share the Studio's Generate form. */
	bool IsGenerate(const FString& Kind);
	bool IsSmartGenerate(const FString& Kind);
	bool IsThreeD(const FString& Kind);
	/** Out of this plugin's scope — or unknown to it. Such a node is shown and kept, never edited. */
	bool NotAvailableHere(const FString& Kind);
	FString Label(const FString& Kind);

	/** Why a connection does not fit, in words. A one-picture generation fed views names its multi-view step. */
	FString MismatchMessage(const FString& Kind, const FString& Carries, const TArray<FString>& Accepts);
	/** A generation fed views becomes its multi-view step (as the server renames it). Unset when nothing changes. */
	TOptional<FZettaPipelineGraph> WithMultiViewSteps(const FZettaPipelineGraph& Graph);

	/** The parts an Isolate Parts node has learnt from its runs: (key, label). */
	TArray<TPair<FString, FString>> KnownParts(const FZettaPipelineNode& Node);
	/** A node's outputs as the canvas shows them, Isolate Parts' learnt parts included. */
	TArray<FZettaOutputPort> OutputsOf(const FZettaPipelineNode& Node);
	/** What an output handle carries, or empty for one the node does not have. */
	FString OutputType(const FString& Kind, const FString& Handle);
	const FZettaInputPort* InputPort(const FString& Kind, const FString& Handle);
}

// ─── Settings and requests ──────────────────────────────────────────────────────────────────────────────────────────

/** A choice a settings form offers: the stored value, its label and a hint. */
struct FZettaSettingOption
{
	FString Value;
	FString Label;
	FString Hint;
};

/**
 * The Studio tools' settings as data — web-shaped values, their defaults, the request each builds and the price each
 * quotes — without origin or sourcePostId (the server adds those).
 */
namespace ZettaToolSettings
{
	extern const TCHAR* SmartTopology;
	extern const TCHAR* AiSegmentation;
	extern const TCHAR* SmartSegmentationCode;
	extern const TCHAR* Retexture;
	extern const TCHAR* RetextureHd;
	extern const TCHAR* Retexture8K;
	extern const TCHAR* AutoRigging;
	extern const TCHAR* SmartUvCode;
	extern const TCHAR* SplatBase;
	extern const TCHAR* SplatModel;
	inline constexpr int32 SmartUvMaxTriangles = 80000;
	inline constexpr int32 DefaultRetopologyFaces = 8000;
	inline constexpr int32 IsolateMaxPartsLimit = 16;

	const TArray<FZettaSettingOption>& RetopologyEngines();
	const TArray<FZettaSettingOption>& FaceLevels();
	const TArray<FZettaSettingOption>& PolygonTypes();
	const TArray<FZettaSettingOption>& SegmentModes();
	const TArray<FZettaSettingOption>& SegmentGranularities();
	const TArray<FZettaSettingOption>& TextureTiers();
	const TArray<FZettaSettingOption>& TextureAlignments();
	const TArray<FZettaSettingOption>& SmartUvOrientations();
	const TArray<FZettaSettingOption>& RigTypes();
	const TArray<FZettaSettingOption>& RigSpecs();
	const TArray<FZettaSettingOption>& RigOutFormats();
	const TArray<FZettaSettingOption>& IsolateSensitivities();
	const TArray<FZettaSettingOption>& IsolatePoses();
	const TArray<FZettaSettingOption>& DrawViewsChoices();
	FString LabelOf(const TArray<FZettaSettingOption>& Options, const FString& Value);
	const FZettaSettingOption* Find(const TArray<FZettaSettingOption>& Options, const FString& Value);

	TSharedRef<FJsonObject> RetopologyDefaults();
	TSharedRef<FJsonObject> SegmentDefaults();
	TSharedRef<FJsonObject> TextureDefaults();
	TSharedRef<FJsonObject> SmartUvDefaults();
	TSharedRef<FJsonObject> AutoRigDefaults();
	TSharedRef<FJsonObject> SplatDefaults();
	TSharedRef<FJsonObject> IsolateDefaults();

	FInt32Range RetopologyBounds(const FString& PolygonType);
	bool RetopologyOutOfRange(const TSharedPtr<FJsonObject>& Value);
	/** Empty when in range. */
	FString RetopologyRangeProblem(const TSharedPtr<FJsonObject>& Value);
	TSharedRef<FJsonObject> RetopologyRequest(const TSharedPtr<FJsonObject>& Value);
	TOptional<int32> RetopologyPrice(const FZettaPriceList* Prices, const TSharedPtr<FJsonObject>& Value);

	FString SegmentCode(const TSharedPtr<FJsonObject>& Value);
	TSharedRef<FJsonObject> SegmentRequest(const TSharedPtr<FJsonObject>& Value);
	TOptional<int32> SegmentPrice(const FZettaPriceList* Prices, const TSharedPtr<FJsonObject>& Value);

	TSharedRef<FJsonObject> TextureRequest(const TSharedPtr<FJsonObject>& Value, const TOptional<FString>& Prompt);
	TOptional<int32> TexturePrice(const FZettaPriceList* Prices, const TSharedPtr<FJsonObject>& Value);
	FString TextureTierLabel(const FString& Tier);

	TSharedRef<FJsonObject> SmartUvRequest(const TSharedPtr<FJsonObject>& Value);
	TOptional<int32> SmartUvPrice(const FZettaPriceList* Prices);

	TSharedRef<FJsonObject> AutoRigRequest(const TSharedPtr<FJsonObject>& Value);
	TOptional<int32> AutoRigPrice(const FZettaPriceList* Prices);

	TSharedRef<FJsonObject> SplatRequest(const TSharedPtr<FJsonObject>& Value);
	TOptional<int32> SplatPrice(const FZettaPriceList* Prices, const TSharedPtr<FJsonObject>& Value);
}

/**
 * The web Studio's Generate form, as a Generate node keeps it in settings (web-shaped JSON). The form rules the
 * request builder does not own — applyFormPatch, polygon bounds, the default count, whether a count is custom — plus
 * the mapping onto FZettaGenerateOptions, so the body itself comes from ZettaRequests::Build.
 */
namespace ZettaGenerateForm
{
	inline constexpr int32 PolygonMin = 3100;
	inline constexpr int32 PolygonMax = 1480000;

	TSharedRef<FJsonObject> Defaults();
	FInt32Range PolygonBounds(const TSharedPtr<FJsonObject>& Form);
	int32 DefaultPolygonCount(const TSharedPtr<FJsonObject>& Form);
	bool HasCustomPolygonCount(const TSharedPtr<FJsonObject>& Form, bool bMayChoose = true);
	TArray<FString> ParameterProblems(const TSharedPtr<FJsonObject>& Form, bool bMayChoose = true);
	/** A change to the form with its coupled fields kept in step. Returns a new form. */
	TSharedRef<FJsonObject> ApplyPatch(const TSharedPtr<FJsonObject>& Current, const TSharedPtr<FJsonObject>& Patch);
	FZettaGenerateOptions ToOptions(const TSharedPtr<FJsonObject>& Form, bool bMayChooseFaceCount = true);
	/** The polygonType the web sends for this form, or unset when none is sent. */
	TOptional<FString> SentPolygonType(const TSharedPtr<FJsonObject>& Form, const FString& Base, const TArray<FString>& Modifiers);
	FString ModelLabel(const FString& Model);
	/** The engines each generate kind offers. */
	TArray<FString> ModelsFor(const FString& Kind);
	FString ModelOf(const TSharedPtr<FJsonObject>& Form);
	/** A form field as text (polygonCount may arrive as a number). */
	FString FieldText(const TSharedPtr<FJsonObject>& Form, const FString& Key);
}

/** What a node's request depends on beyond its settings. */
struct FZettaNodeContext
{
	/** What arrives on the node's main input (a port type), when it is connected. */
	FString Incoming;
	/** The account's 3D gates; null = unknown (allowed). */
	const FZettaEntitlements3D* Gates = nullptr;

	bool MayChooseFaceCount() const { return Gates == nullptr || Gates->Allows(Gates->AllowsCustomFaceCount); }
};

/** What each node holds in settings — the Studio form's own value — its defaults, and the line under its name. */
namespace ZettaNodeSettings
{
	TSharedRef<FJsonObject> Defaults(const FString& Kind);
	/** A node's settings over the Studio's defaults (a named engine settles its coupled options). New object. */
	TSharedRef<FJsonObject> WithDefaults(const FString& Kind, const TSharedPtr<FJsonObject>& Settings);
	/** The settings a node is built from: its own, or its kind's defaults (shared, do not change it). */
	TSharedRef<FJsonObject> Of(const FZettaPipelineNode& Node);
	/** AI texturing always is; AI edit when its "Set on each run" is on. */
	bool SetOnEachRun(const FZettaPipelineNode& Node);
	/** SetOnEachRun, knowing the graph: Multi-view AI texturing fed views asks nothing of a run. */
	bool PerRunIn(const FZettaPipelineGraph& Graph, const FZettaPipelineNode& Node);
	bool ViewsWired(const FZettaPipelineGraph& Graph, const FString& NodeId);
	/** One line under a node's name: what it is set to, in the Studio's words. */
	FString Summary(const FZettaPipelineNode& Node);
	/** The sides of a multi-view set, in the Studio's slot order. */
	const TArray<FString>& ViewKeys();
	/** A multi-view set ({ front: { uploadKey | gen2dPostId, name, thumbnail } }) as the server reads it. */
	TArray<TSharedPtr<FJsonValue>> ViewsRequest(const TSharedPtr<FJsonObject>& Set);
}

/**
 * WHAT EACH NODE SENDS. A node keeps the Studio form's value in settings and the request that form builds in
 * request — built here with the Studio's own builders, so a node sends exactly what the Studio would. The server
 * reads only request, adding the inputs that arrive along the connections. The agent depends on this.
 */
namespace ZettaRequestBuilder
{
	/** A picture address the builder accepts; the server puts the real picture in its place. */
	extern const TCHAR* InputPlaceholder;

	TSharedRef<FJsonObject> RequestFor(const FZettaPipelineNode& Node, const FZettaPipelineGraph& Graph, const FZettaEntitlements3D* Gates);
	/** Never fails: a form the Studio would refuse yields the engine alone, and NodeProblems says why. */
	TSharedRef<FJsonObject> RequestFor(const FString& Kind, const TSharedPtr<FJsonObject>& Settings, const FZettaNodeContext& Context);
	TArray<FString> NodeProblems(const FString& Kind, const TSharedPtr<FJsonObject>& Settings, const FZettaNodeContext& Context);
	/** One run of this node, in credits. Unset while prices are unknown. */
	TOptional<int32> PriceFor(const FZettaPipelineNode& Node, const FZettaPriceList* Prices, const FZettaNodeContext& Context);
	/** "—" while unknown, "Free", "48 cr" or "33 cr / part". */
	FString PriceLabel(const TOptional<int32>& Price, bool bPerPart);
	/** Every node's request rebuilt from its settings — before saving, pricing or starting a run. */
	FZettaPipelineGraph WithRequests(const FZettaPipelineGraph& Graph, const FZettaEntitlements3D* Gates);

	struct FNormalized
	{
		FZettaPipelineGraph Graph;
		/** A 3D step had no request.spec (an agent-drafted, settings-only graph): save it with WithRequests. */
		bool bNeedsRequestRebuild = false;
	};
	/** The editor's load normalisation. */
	FNormalized Normalize(const FZettaPipelineGraph& Saved);
	/** The recipe as a new run's copy: a step set on each run starts empty. */
	FZettaPipelineGraph ForNewRun(const FZettaPipelineGraph& Graph);
	/** A run's per-run input for one step: the node's settings with the change, and the request rebuilt. */
	bool RunNodeChange(const FZettaPipelineGraph& RunGraph, const FString& NodeId, const TSharedPtr<FJsonObject>& Patch,
		const FZettaEntitlements3D* Gates, TSharedPtr<FJsonObject>& OutSettings, TSharedPtr<FJsonObject>& OutRequest, FString& OutError);
}

// ─── Rules ──────────────────────────────────────────────────────────────────────────────────────────────────────────

/** The most a run of a graph can cost, worked out from the price list. */
struct FZettaGraphEstimate
{
	struct FLine
	{
		FString NodeId;
		TOptional<int32> Credits;
		int32 Count = 1;
		bool bPerPart = false;
	};
	/** The total, or unset while a price is unknown. */
	TOptional<int32> Credits;
	/** Some steps run once per part: the total assumes Max parts. */
	bool bPerPart = false;
	TArray<FLine> Lines;

	FString Label() const { return LabelFor(Credits, bPerPart); }
	/** "—", "48 credits a run", or "Up to 132 credits a run". */
	static FString LabelFor(const TOptional<int32>& Credits, bool bPerPart);
};

/** THE GRAPH'S RULES — what may connect, what stops it running, what a run will ask for, what it may cost. */
namespace ZettaGraphRules
{
	bool CanConnect(const FZettaPipelineGraph& Graph, const FString& Source, const FString& SourceHandle, const FString& Target, const FString& TargetHandle = TEXT("in"));
	/** CanConnect with an input already in use counted as free (connecting into it replaces its wire), and one wire left out. */
	bool MayWire(const FZettaPipelineGraph& Graph, const FString& Source, const FString& SourceHandle, const FString& Target, const FString& TargetHandle, const FString& IgnoreEdgeId = FString());
	/** Why a connection is refused, or empty when it can be made. */
	FString ConnectionProblem(const FZettaPipelineGraph& Graph, const FString& Source, const FString& SourceHandle, const FString& Target, const FString& TargetHandle = TEXT("in"), const FString& IgnoreEdgeId = FString());
	bool Reaches(const FZettaPipelineGraph& Graph, const FString& From, const FString& To);
	const FZettaPipelineEdge* InputEdge(const FZettaPipelineGraph& Graph, const FString& NodeId, const FString& Handle = TEXT("in"));
	const FZettaPipelineNode* SourceOf(const FZettaPipelineGraph& Graph, const FString& NodeId, const FString& Handle = TEXT("in"));
	FString IncomingType(const FZettaPipelineGraph& Graph, const FString& NodeId);
	/** The nodes that run once per part — everything downstream of an Isolate Parts "each". */
	TSet<FString> FanOutNodes(const FZettaPipelineGraph& Graph);
	/** Node ids where every node comes after the one feeding it; unset when there is a loop. */
	TOptional<TArray<FString>> TopologicalOrder(const FZettaPipelineGraph& Graph);
	/** Everything that stops this graph from running, as the editor shows it. Empty = runnable. */
	TArray<FZettaPipelineProblem> Problems(const FZettaPipelineGraph& Graph, const FZettaEntitlements3D* Gates);
	TArray<FZettaPipelineProblem> GraphProblems(const FZettaPipelineGraph& Graph);
	/** What a run needs from the user before it can start (the server's own rule). */
	TArray<FZettaPipelineProblem> RunInputProblems(const FZettaPipelineGraph& Graph);
	/** What may go wrong once it runs (Smart UV over its budget), and steps this plugin cannot edit. */
	TArray<FString> Warnings(const FZettaPipelineGraph& Graph, const FString& NodeId);
	FZettaGraphEstimate Estimate(const FZettaPipelineGraph& Graph, const FZettaPriceList* Prices, const FZettaEntitlements3D* Gates);
}

// ─── Edits ──────────────────────────────────────────────────────────────────────────────────────────────────────────

/** One change to a recipe, as the agent sends it: add, set or remove. */
struct FZettaPipelineChange
{
	FString Op;
	FString Kind;
	/** add: the step it follows (the seed when empty). */
	FString After;
	/** add after Isolate Parts: character | each | part:<key>. */
	FString Output;
	FString NodeId;
	TSharedPtr<FJsonObject> Settings;
};

/** A step of a built pipeline: the agent's build_pipeline steps. */
struct FZettaBuildStep
{
	FString Kind;
	/** The index of the earlier step this one follows; unset = the seed. */
	TOptional<int32> After;
	FString Output;
	TSharedPtr<FJsonObject> Settings;
};

/** The agent's settings vocabulary mapped onto the web form shapes a graph stores. */
namespace ZettaAgentSettings
{
	TSharedRef<FJsonObject> ToWeb(const FString& Kind, const TSharedPtr<FJsonObject>& AgentSettings);
}

/**
 * PURE EDITS OF A RECIPE, shared by the canvas and the agent — each returns a new graph. Requests are rebuilt for the
 * nodes an edit touches; WithRequests runs before saving anyway. A refused edit returns false with a one-line reason.
 */
namespace ZettaGraphEditing
{
	FString NewId(const FString& Prefix);
	FString NodeIdPrefix(const FString& Kind);

	bool AddStep(const FZettaPipelineGraph& Graph, const FString& Kind, const FString& AfterNodeId, const FString& Output,
		const TSharedPtr<FJsonObject>& Settings, const TOptional<FVector2D>& Position, const FZettaEntitlements3D* Gates,
		FZettaPipelineGraph& Out, FString& OutNodeId, FString& OutError);
	bool SetSettings(const FZettaPipelineGraph& Graph, const FString& NodeId, const TSharedPtr<FJsonObject>& Settings,
		const FZettaEntitlements3D* Gates, FZettaPipelineGraph& Out, FString& OutError);
	bool MergeSettings(const FZettaPipelineGraph& Graph, const FString& NodeId, const TSharedPtr<FJsonObject>& Patch,
		const FZettaEntitlements3D* Gates, FZettaPipelineGraph& Out, FString& OutError);
	FZettaPipelineGraph SetLabel(const FZettaPipelineGraph& Graph, const FString& NodeId, const FString& Label);
	FZettaPipelineGraph Move(const FZettaPipelineGraph& Graph, const TMap<FString, FVector2D>& Positions);
	FZettaPipelineGraph Remove(const FZettaPipelineGraph& Graph, const FString& NodeId);
	bool Connect(const FZettaPipelineGraph& Graph, const FString& Source, const FString& SourceHandle, const FString& Target,
		const FString& TargetHandle, const FZettaEntitlements3D* Gates, FZettaPipelineGraph& Out, FString& OutError);
	bool Reconnect(const FZettaPipelineGraph& Graph, const FString& EdgeId, const FString& Source, const FString& SourceHandle,
		const FString& Target, const FString& TargetHandle, const FZettaEntitlements3D* Gates, FZettaPipelineGraph& Out, FString& OutError);
	FZettaPipelineGraph Disconnect(const FZettaPipelineGraph& Graph, const FString& EdgeId, const FZettaEntitlements3D* Gates);

	struct FAgentChanges
	{
		FZettaPipelineGraph Graph;
		FString Focus;
		TArray<FString> Added;
		/** (index into the changes, reason) */
		TArray<TPair<int32, FString>> Skipped;
	};
	FAgentChanges ApplyAgentChanges(const FZettaPipelineGraph& Graph, const TArray<FZettaPipelineChange>& Changes, const FZettaEntitlements3D* Gates);

	struct FBuilt
	{
		FString Title;
		FZettaPipelineGraph Graph;
		/** The node id of each step, in step order; empty for a skipped step. */
		TArray<FString> StepNodeIds;
		TArray<TPair<int32, FString>> Skipped;
	};
	FBuilt Build(const FString& Title, const FString& StartsFrom, const TArray<FZettaBuildStep>& Steps, const FZettaEntitlements3D* Gates);
}

/**
 * UNDO AND REDO FOR A RECIPE. Every edit is one step back, except streams: changes sharing a coalesce key within a
 * second of the last fold into the step they started (one drag, one undo). Changes the editor makes on its own
 * (Isolate Parts learning its parts) are Replaced — kept, never undone.
 */
class FZettaUndoStack
{
public:
	static constexpr int32 Limit = 100;
	static constexpr double CoalesceSeconds = 1.0;

	const FZettaPipelineGraph& Present() const { return Current; }
	bool CanUndo() const { return Past.Num() > 0; }
	bool CanRedo() const { return Future.Num() > 0; }

	void Load(const FZettaPipelineGraph& Graph);
	void Commit(const FZettaPipelineGraph& Next, const FString& Coalesce = FString());
	void Replace(const FZettaPipelineGraph& Next);
	bool Undo();
	bool Redo();

	FSimpleMulticastDelegate OnChanged;

private:
	TArray<FZettaPipelineGraph> Past;
	TArray<FZettaPipelineGraph> Future;
	FZettaPipelineGraph Current;
	FString LastKey;
	double LastAt = -1000;
};
