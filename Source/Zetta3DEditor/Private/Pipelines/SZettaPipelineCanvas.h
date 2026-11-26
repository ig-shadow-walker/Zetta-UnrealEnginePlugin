// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "ZettaPipelineRecipe.h"
#include "ZettaPipelineRuns.h"

class SConstraintCanvas;
class SZettaStepCard;

/** What one step's card shows. */
struct FZettaStepCardData
{
	FString Summary;
	/** One run of it, in credits; unset while prices load. */
	TOptional<int32> Price;
	/** Runs once per part — the price is per part. */
	bool bPerPart = false;
	TArray<FString> Problems;
	TArray<FString> Warnings;
	/** In a run: what it is doing. Unset in the recipe. */
	TOptional<FZettaNodeRunState> Run;
	/** In a run: what it made, as small pictures (url, label). */
	TArray<TPair<FString, FString>> Outputs;
	/** In a run: the seed this run started from (url, name). */
	bool bHasSeed = false;
	FString SeedUrl, SeedName;
	/** In the recipe: its input is given on each run. */
	bool bPerRun = false;
	/** In a draft run: what this step still needs before Start; Flash after a refused Start. */
	FString NeedsInput;
	bool bFlash = false;
};

/** One socket as the canvas draws it. */
struct FZettaSocket
{
	FString Handle;
	FString Label;
	/** What it carries (an input: the first type it accepts); empty for a kind this version does not know. */
	FString Type;
	bool bPart = false;
};

/**
 * THE GRAPH, DRAWN — a custom Slate canvas over the pipeline's own graph, which stays the one source of truth: every
 * move, connection and removal here is raised as a request; the owner applies it (through ZettaGraphEditing) and calls
 * Show again. The canvas only ever draws what the graph says.
 *
 * Moving around as the web canvas does: drag the empty canvas (or two fingers on a trackpad) to move, the mouse wheel,
 * a pinch or Cmd/Ctrl + scroll to zoom, Shift-drag to select several steps. Wires are painted under the step cards;
 * the cards are plain widgets laid out at the view's zoom.
 */
class SZettaPipelineCanvas : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaPipelineCanvas) : _ReadOnly(false) {}
		SLATE_ARGUMENT(bool, ReadOnly)
		/** Widgets laid over the canvas (the Describe box). */
		SLATE_NAMED_SLOT(FArguments, Overlay)
	SLATE_END_ARGS()

	static constexpr float MinZoom = 0.2f;
	static constexpr float MaxZoom = 1.75f;
	static constexpr float CardWidth = 232.f;

	void Construct(const FArguments& InArgs);
	virtual ~SZettaPipelineCanvas() override;

	// What the owner listens to.
	TFunction<void(const FString& /*NodeId or empty*/)> OnSelectionChanged;
	TFunction<void(const FString&, const FString&, const FString&, const FString&)> OnConnect;
	TFunction<void(const FString& /*EdgeId*/, const FString&, const FString&, const FString&, const FString&)> OnReconnect;
	TFunction<void(const TArray<FString>& /*Nodes*/, const TArray<FString>& /*Edges*/)> OnRemove;
	TFunction<void(const FString&)> OnRefused;
	TFunction<void(const TMap<FString, FVector2D>&)> OnMove;
	TFunction<void(const FString& /*Kind*/, const FVector2D& /*Graph position*/)> OnDropKind;

	/** Draws a graph: adds, updates and removes cards to match it. Cheap enough for every edit. */
	void Show(const FZettaPipelineGraph& Graph, TFunction<FZettaStepCardData(const FZettaPipelineNode&)> DataFor, bool bShowEmptyHint = true);
	/** Selects one step (or none) without raising OnSelectionChanged. */
	void Select(const FString& NodeId);
	FString SelectedNodeId() const { return Selected.Num() > 0 ? Selected[0] : FString(); }
	/** The nodes with a step running: their incoming wire is drawn lit. */
	void SetRunning(const TSet<FString>& NodeIds) { Running = NodeIds; }
	/** Fits every step in view, never zoomed in past 100%. */
	void FrameGraph();
	/** Frames the graph again the next time it has a size (a newly opened pipeline). */
	void Reframe() { bFramed = false; }
	/** A drafted recipe builds itself: the new steps appear one after another, then the recipe is framed. */
	void Assemble(const TArray<FString>& NodeIds);

	const FZettaPipelineGraph& GetGraph() const { return Graph; }
	bool IsReadOnly() const { return bReadOnly; }
	FVector2D ToLocal(const FVector2D& GraphPoint) const { return GraphPoint * Zoom + Offset; }
	FVector2D ToGraph(const FVector2D& LocalPoint) const { return (LocalPoint - Offset) / Zoom; }
	float GetZoom() const { return Zoom; }
	/** A card's top-left in graph space (moved by a drag in progress). */
	FVector2D CardPosition(const FString& NodeId) const;
	/** When a card should be fully shown (Assemble). */
	double ArriveAt(const FString& NodeId) const { return Arrivals.FindRef(NodeId); }
	bool IsSelected(const FString& NodeId) const { return Selected.Contains(NodeId); }

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnTouchGesture(const FGeometry& MyGeometry, const FPointerEvent& GestureEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

private:
	struct FCard
	{
		TSharedPtr<SZettaStepCard> Widget;
		TArray<FZettaSocket> Inputs;
		TArray<FZettaSocket> Outputs;
		FString Signature;
	};

	struct FPortHit
	{
		FString NodeId;
		FString Handle;
		bool bOutput = false;
		bool IsSet() const { return !NodeId.IsEmpty(); }
	};

	enum class EMode : uint8 { None, Panning, Dragging, Wiring, Marquee };

	void PaintBack(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer) const;
	void PaintFront(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer) const;
	FVector2D SocketLocal(const FString& NodeId, const FString& Handle, bool bOutput) const;
	FVector2D CardSize(const FString& NodeId) const;
	FPortHit PortAt(const FVector2D& Local, float Slack = 6.f) const;
	FString NodeAt(const FVector2D& Local) const;
	FString EdgeAt(const FVector2D& Local) const;
	bool RemoveButtonAt(const FVector2D& Local) const;
	FVector2D EdgeMiddle(const FZettaPipelineEdge& Edge) const;
	void SetView(const FVector2D& InOffset, float InZoom);
	void ZoomAt(const FVector2D& Local, float Factor);
	void ChangeSelection(const TArray<FString>& Nodes, const FString& Edge);
	void FinishWire(const FVector2D& Local);
	void Refuse(const FString& Source, const FString& SourceHandle, const FString& Target, const FString& TargetHandle, const FString& MovingEdge);
	TSharedRef<SWidget> MakeControls();
	TSharedRef<SWidget> MakeGuide();
	void ShowGuide(bool bShow);
	static void SocketsOf(const FZettaPipelineNode& Node, const FZettaPipelineGraph& Graph, TArray<FZettaSocket>& OutInputs, TArray<FZettaSocket>& OutOutputs);

	bool bReadOnly = false;
	FZettaPipelineGraph Graph;
	TFunction<FZettaStepCardData(const FZettaPipelineNode&)> DataFor;
	TMap<FString, FCard> Cards;
	TSharedPtr<SConstraintCanvas> CardLayer;
	TSet<FString> Running;
	TMap<FString, double> Arrivals;
	bool bShowEmptyHint = true;
	bool bGuideOpen = true;

	FVector2D Offset = FVector2D(80, 80);
	float Zoom = 1.f;
	FVector2D LastSize = FVector2D::ZeroVector;
	bool bFramed = false;
	double FrameAt = 0;

	TArray<FString> Selected;
	FString SelectedEdge;
	FString LastReported;

	EMode Mode = EMode::None;
	bool bMoved = false;
	FVector2D PressLocal = FVector2D::ZeroVector;
	FVector2D MouseLocal = FVector2D::ZeroVector;
	FVector2D PanStart = FVector2D::ZeroVector;
	/** A drag of cards in progress: the offset, in graph units, of the selected cards. */
	FVector2D DragDelta = FVector2D::ZeroVector;
	/** A wire being drawn: the end that stays put, and (when an existing connection is moved) its id. */
	FPortHit WireFrom;
	FString MovingEdge;
	FPortHit WireHover;
	FString HoverTooltip;
};
