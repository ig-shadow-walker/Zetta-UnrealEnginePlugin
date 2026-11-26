// Copyright Zetta 3D. See LICENSE.md.

#include "SZettaPipelineCanvas.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ConfigCacheIni.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "ZettaPipelineUI.h"
#include "ZettaStyle.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace ZettaCanvasLayout
{
	/** The card's fixed parts, in graph units: sockets sit at known places, so wires never wait for a layout pass. */
	constexpr float HeaderHeight = 46.f;
	constexpr float PortTop = HeaderHeight + 4.f;
	constexpr float RowHeight = 20.f;
	constexpr float Socket = 10.f;
}

static const FSlateBrush* CanvasBrush(const FLinearColor& Fill, const FLinearColor& Outline, float Radius, float Width = 1.f)
{
	static TMap<FString, TSharedPtr<FSlateBrush>> Cache;
	const FString Key = FString::Printf(TEXT("%s|%s|%g|%g"), *Fill.ToString(), *Outline.ToString(), Radius, Width);
	if (const TSharedPtr<FSlateBrush>* Found = Cache.Find(Key)) return Found->Get();
	TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateRoundedBoxBrush>(Fill, Radius, Outline, Width);
	Cache.Add(Key, Brush);
	return Brush.Get();
}

/** A layer that only paints (the grid and wires under the cards; the wire being drawn over them). */
class SZettaCanvasLayer : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaCanvasLayer) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&) {}
	TFunction<void(const FGeometry&, FSlateWindowElementList&, int32)> Paint;

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
	{
		if (Paint) Paint(Geometry, Out, Layer);
		return Layer + 1;
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
};

// ─── One step on the canvas ─────────────────────────────────────────────────────────────────────────────────────────

/**
 * ONE STEP: the tool's mark in its area's hue, its name and what it is set to, what it costs, and — during a run — what
 * it is doing. Inputs on the left, outputs on the right, each in the colour of what it carries. Drawn only: the
 * canvas handles every click.
 */
class SZettaStepCard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaStepCard) {}
	SLATE_END_ARGS()

	FString NodeId;
	FString Kind;
	FString Name;
	FZettaStepCardData Data;
	TArray<FZettaSocket> Inputs;
	TArray<FZettaSocket> Outputs;
	TWeakPtr<SZettaPipelineCanvas> Canvas;

	void Construct(const FArguments&, const TSharedRef<SZettaPipelineCanvas>& InCanvas, const FString& InNodeId)
	{
		Canvas = InCanvas;
		NodeId = InNodeId;
		Rebuild();
	}

	void Rebuild()
	{
		using namespace ZettaCanvasLayout;
		const int32 Rows = FMath::Max(Inputs.Num(), Outputs.Num());
		TSharedRef<SVerticalBox> PortRows = SNew(SVerticalBox);
		for (int32 R = 0; R < Rows; ++R)
		{
			const FZettaSocket* In = Inputs.IsValidIndex(R) ? &Inputs[R] : nullptr;
			const FZettaSocket* Out = Outputs.IsValidIndex(R) ? &Outputs[R] : nullptr;
			PortRows->AddSlot().AutoHeight()
			[
				SNew(SBox).HeightOverride(RowHeight).Padding(FMargin(12, 0))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Font(FZettaStyle::Font(8)).Text(FText::FromString(In ? In->Label : FString()))
						.ColorAndOpacity(FSlateColor(In ? ZettaPortTypes::Colour(In->Type) : FLinearColor::White))
					]
					+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).HAlign(HAlign_Right)
					[
						SNew(STextBlock).Font(FZettaStyle::Font(8, Out && Out->bPart)).Text(FText::FromString(Out ? Out->Label : FString()))
						.ColorAndOpacity(FSlateColor(Out ? ZettaPortTypes::Colour(Out->Type) : FLinearColor::White))
					]
				]
			];
		}

		TSharedRef<SWrapBox> Thumbs = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(3, 3));
		for (int32 I = 0; I < FMath::Min(4, Data.Outputs.Num()); ++I)
		{
			Thumbs->AddSlot()[SNew(SBox).ToolTipText(FText::FromString(Data.Outputs[I].Value))[SNew(SZettaPlThumb).Url(Data.Outputs[I].Key).Size(FVector2D(44, 44))]];
		}
		if (Data.Outputs.Num() > 4)
		{
			Thumbs->AddSlot()
			[
				SNew(SBox).WidthOverride(44).HeightOverride(44).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("+%d"), Data.Outputs.Num() - 4))).ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			];
		}

		const bool bNotHere = ZettaNodeCatalog::NotAvailableHere(Kind);
		const FZettaNodeSpec* Spec = ZettaNodeCatalog::Spec(Kind);
		FString Note;
		bool bAmberNote = false;
		if (!Data.NeedsInput.IsEmpty())
		{
			int32 Colon = Data.NeedsInput.Find(TEXT(": "));
			Note = TEXT("Needs input — ") + (Colon >= 0 ? Data.NeedsInput.Mid(Colon + 2) : Data.NeedsInput);
			bAmberNote = true;
		}
		else if (Data.bPerRun) Note = TEXT("↻ Set on each run");

		FString Status;
		const FString State = Data.Run.IsSet() ? Data.Run->Status : FString(TEXT("idle"));
		if (!Data.NeedsInput.IsEmpty()) Status = TEXT("Needs input");
		else if (Data.Run.IsSet() && State != TEXT("idle")) Status = Data.Run->Total > 1 ? FString::Printf(TEXT("%s · %d/%d"), *Data.Run->Label(), Data.Run->Done, Data.Run->Total) : Data.Run->Label();
		else if (bNotHere) Status = TEXT("Not available in Unreal");
		else Status = Spec && Spec->bThreeD ? TEXT("Queued as a 3D job") : TEXT("Instant");
		const FString Price = Data.Run.IsSet() && Data.Run->Credits > 0 ? FString::Printf(TEXT("%d cr"), Data.Run->Credits) : ZettaRequestBuilder::PriceLabel(Data.Price, Data.bPerPart);

		// The status mark beside the name.
		FString Mark;
		FLinearColor MarkColour = FZettaStyle::Warning();
		const int32 Issues = Data.Problems.Num() + Data.Warnings.Num();
		if (!Data.NeedsInput.IsEmpty()) Mark = TEXT("!");
		else if (State == ZettaStepStatus::Running) { Mark = TEXT("…"); MarkColour = ZettaPlUI::ToneColour(EZettaRunTone::Running); }
		else if (State == ZettaStepStatus::Success) { Mark = TEXT("✓"); MarkColour = ZettaPlUI::ToneColour(EZettaRunTone::Done); }
		else if (State == ZettaStepStatus::Failed || State == ZettaStepStatus::Cancelled) { Mark = TEXT("✕"); MarkColour = ZettaPlUI::ToneColour(EZettaRunTone::Failed); }
		else if (State == ZettaStepStatus::Waiting || State == ZettaStepStatus::Review) Mark = TEXT("||");
		else if (Issues > 0) Mark = TEXT("!");

		const FString Summary = bNotHere ? FString(TEXT("Not available in Unreal")) : Data.Summary;

		ChildSlot
		[
			SNew(SBox).WidthOverride(SZettaPipelineCanvas::CardWidth)
			[
				SNew(SBorder)
				.BorderImage(this, &SZettaStepCard::Frame)
				.Padding(0)
				.ColorAndOpacity(this, &SZettaStepCard::Fade)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox).HeightOverride(HeaderHeight).Padding(FMargin(8, 6))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[ZettaPlUI::HueIcon(Kind)]
							+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Name)).Font(FZettaStyle::Font(10, true)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
								+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Summary)).Font(FZettaStyle::Font(8)).ColorAndOpacity(FSlateColor::UseSubduedForeground()).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
							[
								SNew(STextBlock).Text(FText::FromString(Mark)).Font(FZettaStyle::Font(10, true)).ColorAndOpacity(FSlateColor(MarkColour))
								.Visibility(Mark.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox).HeightOverride(Rows * RowHeight + 8).Padding(FMargin(0, 4))[PortRows]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(8, 0, 8, 4))
					[
						SNew(STextBlock).Text(FText::FromString(Note)).Font(FZettaStyle::Font(8, bAmberNote)).AutoWrapText(true)
						.ColorAndOpacity(bAmberNote ? FSlateColor(FZettaStyle::Warning()) : FSlateColor::UseSubduedForeground())
						.Visibility(Note.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(8, 0, 8, 4))
					[
						SNew(SHorizontalBox)
						.Visibility(Data.bHasSeed ? EVisibility::Visible : EVisibility::Collapsed)
						+ SHorizontalBox::Slot().AutoWidth()[SNew(SZettaPlThumb).Url(Data.SeedUrl).Size(FVector2D(56, 56))]
						+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(6, 0, 0, 0)
						[
							SNew(STextBlock).Text(FText::FromString(Data.SeedName)).Font(FZettaStyle::Font(8)).AutoWrapText(true)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(8, 0, 8, 4))
					[
						SNew(SBox).Visibility(Data.Outputs.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed)[Thumbs]
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBorder).BorderImage(FAppStyle::GetNoBrush()).Padding(FMargin(8, 4, 8, 6))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(FText::FromString(Status)).Font(FZettaStyle::Font(8)).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
							+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(FText::FromString(Price)).Font(FZettaStyle::Font(8, true))]
						]
					]
				]
			]
		];
	}

	const FSlateBrush* Frame() const
	{
		const TSharedPtr<SZettaPipelineCanvas> C = Canvas.Pin();
		const bool bSelected = C.IsValid() && C->IsSelected(NodeId);
		const FString State = Data.Run.IsSet() ? Data.Run->Status : FString();
		const bool bWaiting = !Data.NeedsInput.IsEmpty() || State == ZettaStepStatus::Waiting || State == ZettaStepStatus::Review;
		FLinearColor Edge = FZettaStyle::Border();
		float Width = 1.f;
		if (State == ZettaStepStatus::Running) { Edge = ZettaPlUI::HueOf(Kind).Edge; Width = 2.f; }
		else if (bWaiting) { Edge = ZettaPlUI::Amber().Edge; Width = 2.f; }
		if (bSelected) { Edge = FZettaStyle::Accent(); Width = 2.f; }
		// A refused Start flashes the steps that still need their input.
		if (!Data.NeedsInput.IsEmpty() && Data.bFlash && FMath::Fmod(FPlatformTime::Seconds(), 1.0) < 0.5) Edge = FZettaStyle::Error();
		const FLinearColor Fill = ZettaNodeCatalog::NotAvailableHere(Kind) ? FZettaStyle::Panel() : FZettaStyle::PanelRaised();
		return CanvasBrush(Fill, Edge, 6.f, Width);
	}

	FLinearColor Fade() const
	{
		const TSharedPtr<SZettaPipelineCanvas> C = Canvas.Pin();
		const double At = C.IsValid() ? C->ArriveAt(NodeId) : 0;
		const float Alpha = At <= 0 ? 1.f : FMath::Clamp((float)((FPlatformTime::Seconds() - At) / 0.25), 0.f, 1.f);
		return FLinearColor(1, 1, 1, ZettaNodeCatalog::NotAvailableHere(Kind) ? 0.75f * Alpha : Alpha);
	}
};

// ─── Canvas ─────────────────────────────────────────────────────────────────────────────────────────────────────────

static const TCHAR* GuideSection = TEXT("Zetta3D.Pipelines");

void SZettaPipelineCanvas::Construct(const FArguments& InArgs)
{
	bReadOnly = InArgs._ReadOnly;
	bool bClosed = false;
	if (GConfig) GConfig->GetBool(GuideSection, TEXT("GuideClosed"), bClosed, GEditorPerProjectIni);
	bGuideOpen = !bClosed;

	TSharedRef<SZettaCanvasLayer> Back = SNew(SZettaCanvasLayer);
	Back->Paint = [this](const FGeometry& G, FSlateWindowElementList& Out, int32 Layer) { PaintBack(G, Out, Layer); };
	TSharedRef<SZettaCanvasLayer> Front = SNew(SZettaCanvasLayer);
	Front->Paint = [this](const FGeometry& G, FSlateWindowElementList& Out, int32 Layer) { PaintFront(G, Out, Layer); };

	SetToolTipText(MakeAttributeLambda([this]() { return FText::FromString(HoverTooltip); }));

	// Cards and wires panned past the edge are cut there, not drawn over the step list or the inspector.
	SetClipping(EWidgetClipping::ClipToBounds);

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor::FromSRGBColor(FColor(0x15, 0x15, 0x15)))]
		+ SOverlay::Slot()[SNew(SBox).Visibility(EVisibility::HitTestInvisible)[Back]]
		+ SOverlay::Slot()[SAssignNew(CardLayer, SConstraintCanvas).Visibility(EVisibility::HitTestInvisible)]
		+ SOverlay::Slot()[SNew(SBox).Visibility(EVisibility::HitTestInvisible)[Front]]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Visibility_Lambda([this]() { return bShowEmptyHint && !bReadOnly && Graph.Nodes.Num() == 0 ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.Text(LOCTEXT("PlCanvasHint", "Drag a seed in from the left to start — or describe the recipe above."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(12)[InArgs._Overlay.Widget]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(10)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)[MakeControls()]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(8, 0, 0, 0)[MakeGuide()]
		]
	];
}

SZettaPipelineCanvas::~SZettaPipelineCanvas() = default;

TSharedRef<SWidget> SZettaPipelineCanvas::MakeControls()
{
	// The editor's button style pads its content by more than these small buttons are wide, which squeezed the glyphs to
	// a dot: a copy without that padding, and the editor's own icons rather than characters the font may not have.
	static const FButtonStyle Small = FButtonStyle(FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button")).SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
	auto Make = [](const FName Icon, const FText& Text, const FText& Tip, TFunction<void()> Act, TAttribute<EVisibility> Visible = EVisibility::Visible) -> TSharedRef<SWidget>
	{
		TSharedRef<SWidget> Content = Icon.IsNone()
			? StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(Text).Font(FZettaStyle::Font(10, true)))
			: StaticCastSharedRef<SWidget>(SNew(SImage).Image(FAppStyle::GetBrush(Icon)).ColorAndOpacity(FSlateColor::UseForeground()));
		return SNew(SBox).WidthOverride(28).HeightOverride(26).Visibility(Visible)
			[
				SNew(SButton)
				.ButtonStyle(&Small)
				.ContentPadding(0)
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				.ToolTipText(Tip)
				.OnClicked_Lambda([Act]() { Act(); return FReply::Handled(); })
				[
					Content
				]
			];
	};
	auto Centre = [this]() { return LastSize * 0.5; };
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)[Make(TEXT("Icons.Plus"), LOCTEXT("PlZoomIn", "+"), LOCTEXT("PlZoomInTip", "Zoom in (or pinch, Cmd/Ctrl + scroll, or the mouse wheel)"), [this, Centre]() { ZoomAt(Centre(), 1.2f); })]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)[Make(TEXT("Icons.Minus"), LOCTEXT("PlZoomOut", "−"), LOCTEXT("PlZoomOutTip", "Zoom out"), [this, Centre]() { ZoomAt(Centre(), 1 / 1.2f); })]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)[Make(TEXT("Icons.Fullscreen"), LOCTEXT("PlFit", "Fit"), LOCTEXT("PlFitTip", "Fit every step in view"), [this]() { FrameGraph(); })]
		+ SVerticalBox::Slot().AutoHeight()
		[
			Make(NAME_None, LOCTEXT("PlHelp", "?"), LOCTEXT("PlHelpTip", "How to move around and edit the canvas"), [this]() { ShowGuide(true); },
				MakeAttributeLambda([this]() { return bGuideOpen ? EVisibility::Collapsed : EVisibility::Visible; }))
		];
}

TSharedRef<SWidget> SZettaPipelineCanvas::MakeGuide()
{
	const bool bMac = PLATFORM_MAC != 0;
	// Spelled out: the editor's font has no ⌘ or ⇧ glyph.
	const FString Key = bMac ? TEXT("Cmd") : TEXT("Ctrl");
	TArray<TPair<FString, FString>> Lines = {
		{ TEXT("Move around"), TEXT("Drag the empty canvas, or two fingers on the trackpad") },
		{ TEXT("Zoom"), FString::Printf(TEXT("Pinch, the mouse wheel or %s + scroll"), *Key) },
	};
	if (!bReadOnly)
	{
		Lines.Append({
			{ TEXT("Add a step"), TEXT("Drag it in from the list on the left") },
			{ TEXT("Connect"), TEXT("Drag from a step's output dot to another's input") },
			{ TEXT("Change a connection"), TEXT("Drag either end to another dot — or off, to remove it") },
			{ TEXT("Select several"), TEXT("Shift + drag") },
			{ TEXT("Remove"), TEXT("Select it, then Delete (or ✕ on a connection)") },
			{ TEXT("Undo / redo"), bMac ? TEXT("Cmd+Z / Shift+Cmd+Z") : TEXT("Ctrl+Z / Ctrl+Y") },
		});
	}
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("PlControls", "Controls")).Font(FZettaStyle::Font(9, true))]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
				.ToolTipText(LOCTEXT("PlGuideClose", "Close — ? under the zoom buttons opens it again"))
				.OnClicked_Lambda([this]() { ShowGuide(false); return FReply::Handled(); })
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("✕")))
				]
			]
		];
	for (const TPair<FString, FString>& L : Lines)
	{
		Box->AddSlot().AutoHeight().Padding(0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(120)[SNew(STextBlock).Text(FText::FromString(L.Key)).Font(FZettaStyle::Font(8, true))]]
			+ SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(FText::FromString(L.Value)).AutoWrapText(true).Font(FZettaStyle::Font(8)).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
		];
	}
	return SNew(SBox).WidthOverride(380).Visibility_Lambda([this]() { return bGuideOpen ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.CardRaised")).Padding(10)[Box]
		];
}

void SZettaPipelineCanvas::ShowGuide(bool bShow)
{
	bGuideOpen = bShow;
	if (GConfig) GConfig->SetBool(GuideSection, TEXT("GuideClosed"), !bShow, GEditorPerProjectIni);
}

void SZettaPipelineCanvas::SocketsOf(const FZettaPipelineNode& Node, const FZettaPipelineGraph& InGraph, TArray<FZettaSocket>& OutInputs, TArray<FZettaSocket>& OutOutputs)
{
	OutInputs.Reset();
	OutOutputs.Reset();
	if (const FZettaNodeSpec* Spec = ZettaNodeCatalog::Spec(Node.Kind))
	{
		for (const FZettaInputPort& P : Spec->Inputs)
			OutInputs.Add({ P.Handle, P.Label + (P.bRequired ? FString() : FString(TEXT(" (optional)"))), P.Accepts.Num() > 0 ? P.Accepts[0] : FString(), false });
		for (const FZettaOutputPort& P : ZettaNodeCatalog::OutputsOf(Node)) OutOutputs.Add({ P.Handle, P.Label, P.Type, P.bDynamic });
		return;
	}
	// A kind this version does not know: the sockets its wires use, uncoloured.
	for (const FZettaPipelineEdge& E : InGraph.Edges)
	{
		if (E.Target == Node.Id && !OutInputs.ContainsByPredicate([&E](const FZettaSocket& S) { return S.Handle == E.TargetHandle; })) OutInputs.Add({ E.TargetHandle, E.TargetHandle, FString(), false });
		if (E.Source == Node.Id && !OutOutputs.ContainsByPredicate([&E](const FZettaSocket& S) { return S.Handle == E.SourceHandle; })) OutOutputs.Add({ E.SourceHandle, E.SourceHandle, FString(), false });
	}
}

void SZettaPipelineCanvas::Show(const FZettaPipelineGraph& InGraph, TFunction<FZettaStepCardData(const FZettaPipelineNode&)> InDataFor, bool bInShowEmptyHint)
{
	Graph = InGraph;
	DataFor = MoveTemp(InDataFor);
	bShowEmptyHint = bInShowEmptyHint;

	TSet<FString> Ids;
	for (const FZettaPipelineNode& N : Graph.Nodes) Ids.Add(N.Id);
	for (auto It = Cards.CreateIterator(); It; ++It)
	{
		if (Ids.Contains(It.Key())) continue;
		if (It.Value().Widget.IsValid()) CardLayer->RemoveSlot(It.Value().Widget.ToSharedRef());
		It.RemoveCurrent();
	}
	Selected.RemoveAll([&Ids](const FString& Id) { return !Ids.Contains(Id); });
	if (!SelectedEdge.IsEmpty() && !Graph.Edge(SelectedEdge)) SelectedEdge.Reset();

	for (const FZettaPipelineNode& Node : Graph.Nodes)
	{
		FCard& Card = Cards.FindOrAdd(Node.Id);
		const FZettaStepCardData Data = DataFor ? DataFor(Node) : FZettaStepCardData();
		SocketsOf(Node, Graph, Card.Inputs, Card.Outputs);
		if (!Card.Widget.IsValid())
		{
			Card.Widget = SNew(SZettaStepCard, SharedThis(this), Node.Id);
			const FString Id = Node.Id;
			TWeakPtr<SZettaPipelineCanvas> Weak = SharedThis(this);
			CardLayer->AddSlot()
				.AutoSize(true)
				// Positioned by the top-left corner. SConstraintCanvas centres a slot on its offset by default, which drew
				// every card half its size up and left of the sockets and wires computed for it.
				.Alignment(FVector2D::ZeroVector)
				.Offset_Lambda([Weak, Id]()
				{
					const TSharedPtr<SZettaPipelineCanvas> Self = Weak.Pin();
					if (!Self.IsValid()) return FMargin(0);
					const FVector2D P = Self->ToLocal(Self->CardPosition(Id));
					return FMargin(P.X, P.Y, 0, 0);
				})
				[
					Card.Widget.ToSharedRef()
				];
			Card.Widget->SetRenderTransformPivot(FVector2D::ZeroVector);
		}
		Card.Widget->Kind = Node.Kind;
		Card.Widget->Name = Node.DisplayName();
		Card.Widget->Data = Data;
		Card.Widget->Inputs = Card.Inputs;
		Card.Widget->Outputs = Card.Outputs;
		Card.Widget->Rebuild();
		Card.Widget->SetRenderTransform(FSlateRenderTransform(Zoom));
	}
	// Read only while drawing: what it refers to may not outlive this call.
	DataFor = nullptr;
}

FVector2D SZettaPipelineCanvas::CardPosition(const FString& NodeId) const
{
	const FZettaPipelineNode* Node = Graph.Node(NodeId);
	if (!Node) return FVector2D::ZeroVector;
	FVector2D P(Node->X, Node->Y);
	if (Mode == EMode::Dragging && Selected.Contains(NodeId)) P += DragDelta;
	return P;
}

FVector2D SZettaPipelineCanvas::CardSize(const FString& NodeId) const
{
	const FCard* Card = Cards.Find(NodeId);
	FVector2D Size = Card && Card->Widget.IsValid() ? Card->Widget->GetDesiredSize() : FVector2D::ZeroVector;
	if (Size.X < 1) Size.X = CardWidth;
	if (Size.Y < 1) Size.Y = 120;
	return Size;
}

FVector2D SZettaPipelineCanvas::SocketLocal(const FString& NodeId, const FString& Handle, bool bOutput) const
{
	using namespace ZettaCanvasLayout;
	const FCard* Card = Cards.Find(NodeId);
	if (!Card) return FVector2D::ZeroVector;
	const TArray<FZettaSocket>& List = bOutput ? Card->Outputs : Card->Inputs;
	const int32 Row = List.IndexOfByPredicate([&Handle](const FZettaSocket& S) { return S.Handle == Handle; });
	const FVector2D Graph0 = CardPosition(NodeId) + FVector2D(bOutput ? CardSize(NodeId).X : 0.f, PortTop + FMath::Max(0, Row) * RowHeight + RowHeight * 0.5f);
	return ToLocal(Graph0);
}

void SZettaPipelineCanvas::SetView(const FVector2D& InOffset, float InZoom)
{
	Zoom = FMath::Clamp(InZoom, MinZoom, MaxZoom);
	Offset = InOffset;
	for (const TPair<FString, FCard>& Pair : Cards)
	{
		if (Pair.Value.Widget.IsValid()) Pair.Value.Widget->SetRenderTransform(FSlateRenderTransform(Zoom));
	}
}

void SZettaPipelineCanvas::ZoomAt(const FVector2D& Local, float Factor)
{
	const float Next = FMath::Clamp(Zoom * Factor, MinZoom, MaxZoom);
	const float Ratio = Next / Zoom;
	SetView(Local - (Local - Offset) * Ratio, Next);
}

void SZettaPipelineCanvas::FrameGraph()
{
	if (Cards.Num() == 0 || LastSize.X < 10) return;
	FBox2D Bounds(ForceInit);
	for (const FZettaPipelineNode& N : Graph.Nodes)
	{
		const FVector2D P(N.X, N.Y);
		Bounds += P;
		Bounds += P + CardSize(N.Id);
	}
	const float Pad = 48.f;
	const FVector2D Extent = Bounds.GetSize();
	const float Scale = (float)FMath::Clamp(FMath::Min((LastSize.X - 2 * Pad) / FMath::Max(1.0, Extent.X), (LastSize.Y - 2 * Pad) / FMath::Max(1.0, Extent.Y)), (double)MinZoom, 1.0);
	SetView(LastSize * 0.5 - Bounds.GetCenter() * Scale, Scale);
}

void SZettaPipelineCanvas::Assemble(const TArray<FString>& NodeIds)
{
	const double Now = FPlatformTime::Seconds();
	for (int32 I = 0; I < NodeIds.Num(); ++I) Arrivals.Add(NodeIds[I], Now + 0.12 + I * 0.17);
	FrameAt = Now + 0.03;
}

void SZettaPipelineCanvas::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	LastSize = AllottedGeometry.GetLocalSize();
	// Framed once it has a size and the cards have theirs.
	if (!bFramed && Graph.Nodes.Num() > 0 && LastSize.X >= 10)
	{
		bFramed = true;
		FrameAt = FPlatformTime::Seconds() + 0.05;
	}
	if (FrameAt > 0 && FPlatformTime::Seconds() >= FrameAt)
	{
		FrameAt = 0;
		FrameGraph();
	}
}

void SZettaPipelineCanvas::Select(const FString& NodeId)
{
	Selected.Reset();
	if (!NodeId.IsEmpty() && Graph.Node(NodeId)) Selected.Add(NodeId);
	if (!NodeId.IsEmpty()) SelectedEdge.Reset();
	LastReported = SelectedNodeId();
}

void SZettaPipelineCanvas::ChangeSelection(const TArray<FString>& Nodes, const FString& Edge)
{
	Selected = Nodes;
	SelectedEdge = Edge;
	const FString Now = SelectedNodeId();
	if (Now == LastReported) return;
	LastReported = Now;
	if (OnSelectionChanged) OnSelectionChanged(Now);
}

// ─── Hit testing ────────────────────────────────────────────────────────────────────────────────────────────────────

SZettaPipelineCanvas::FPortHit SZettaPipelineCanvas::PortAt(const FVector2D& Local, float Slack) const
{
	const float Radius = ZettaCanvasLayout::Socket * 0.5f * Zoom + Slack;
	FPortHit Best;
	float BestDistance = Radius;
	for (const TPair<FString, FCard>& Pair : Cards)
	{
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const bool bOutput = Side == 1;
			for (const FZettaSocket& S : bOutput ? Pair.Value.Outputs : Pair.Value.Inputs)
			{
				const float D = FVector2D::Distance(SocketLocal(Pair.Key, S.Handle, bOutput), Local);
				if (D <= BestDistance)
				{
					BestDistance = D;
					Best = { Pair.Key, S.Handle, bOutput };
				}
			}
		}
	}
	return Best;
}

FString SZettaPipelineCanvas::NodeAt(const FVector2D& Local) const
{
	const FVector2D G = ToGraph(Local);
	// The last drawn is on top.
	for (int32 I = Graph.Nodes.Num() - 1; I >= 0; --I)
	{
		const FString& Id = Graph.Nodes[I].Id;
		const FVector2D P = CardPosition(Id);
		const FVector2D S = CardSize(Id);
		if (G.X >= P.X && G.Y >= P.Y && G.X <= P.X + S.X && G.Y <= P.Y + S.Y) return Id;
	}
	return FString();
}

static void WireTangents(const FVector2D& Start, const FVector2D& End, float Zoom, FVector2D& OutStartDir, FVector2D& OutEndDir)
{
	const float Tension = FMath::Max((float)FMath::Abs(End.X - Start.X), 80.f * Zoom);
	OutStartDir = FVector2D(Tension, 0);
	OutEndDir = FVector2D(Tension, 0);
}

FString SZettaPipelineCanvas::EdgeAt(const FVector2D& Local) const
{
	FString Best;
	float BestDistance = 6.f;
	for (const FZettaPipelineEdge& E : Graph.Edges)
	{
		if (!Cards.Contains(E.Source) || !Cards.Contains(E.Target)) continue;
		const FVector2D A = SocketLocal(E.Source, E.SourceHandle, true);
		const FVector2D B = SocketLocal(E.Target, E.TargetHandle, false);
		FVector2D TA, TB;
		WireTangents(A, B, Zoom, TA, TB);
		FVector2D Prev = A;
		for (int32 I = 1; I <= 24; ++I)
		{
			const FVector2D P = FMath::CubicInterp(A, TA, B, TB, I / 24.f);
			const float D = FMath::PointDistToSegment(FVector(Local, 0), FVector(Prev, 0), FVector(P, 0));
			if (D < BestDistance)
			{
				BestDistance = D;
				Best = E.Id;
			}
			Prev = P;
		}
	}
	return Best;
}

FVector2D SZettaPipelineCanvas::EdgeMiddle(const FZettaPipelineEdge& E) const
{
	const FVector2D A = SocketLocal(E.Source, E.SourceHandle, true);
	const FVector2D B = SocketLocal(E.Target, E.TargetHandle, false);
	FVector2D TA, TB;
	WireTangents(A, B, Zoom, TA, TB);
	return FMath::CubicInterp(A, TA, B, TB, 0.5f);
}

bool SZettaPipelineCanvas::RemoveButtonAt(const FVector2D& Local) const
{
	if (bReadOnly || SelectedEdge.IsEmpty()) return false;
	const FZettaPipelineEdge* E = Graph.Edge(SelectedEdge);
	return E && FVector2D::Distance(EdgeMiddle(*E), Local) <= 10.f;
}

// ─── Painting ───────────────────────────────────────────────────────────────────────────────────────────────────────

void SZettaPipelineCanvas::PaintBack(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer) const
{
	const FVector2D Size = Geometry.GetLocalSize();
	// The grid, every 24 units (and every 120 a little brighter), when it is not too dense to read.
	const float Step = 24.f * Zoom;
	if (Step >= 6.f)
	{
		const FLinearColor Minor(1, 1, 1, 0.035f), Major(1, 1, 1, 0.07f);
		const int32 FirstX = FMath::FloorToInt(-Offset.X / Step), LastX = FMath::CeilToInt((Size.X - Offset.X) / Step);
		const int32 FirstY = FMath::FloorToInt(-Offset.Y / Step), LastY = FMath::CeilToInt((Size.Y - Offset.Y) / Step);
		for (int32 I = FirstX; I <= LastX; ++I)
		{
			const float X = Offset.X + I * Step;
			FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), TArray<FVector2f>{ FVector2f(X, 0), FVector2f(X, Size.Y) }, ESlateDrawEffect::None, I % 5 == 0 ? Major : Minor, false, 1.f);
		}
		for (int32 I = FirstY; I <= LastY; ++I)
		{
			const float Y = Offset.Y + I * Step;
			FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), TArray<FVector2f>{ FVector2f(0, Y), FVector2f(Size.X, Y) }, ESlateDrawEffect::None, I % 5 == 0 ? Major : Minor, false, 1.f);
		}
	}

	// Wires, in the colour of what they carry; a wire into a running step is lit; the selected one is drawn thicker.
	for (const FZettaPipelineEdge& E : Graph.Edges)
	{
		if (!Cards.Contains(E.Source) || !Cards.Contains(E.Target)) continue;
		if (Mode == EMode::Wiring && E.Id == MovingEdge) continue;
		const FZettaPipelineNode* Source = Graph.Node(E.Source);
		FLinearColor Colour = ZettaPortTypes::Colour(Source ? ZettaNodeCatalog::OutputType(Source->Kind, E.SourceHandle) : FString());
		float Thickness = 2.f;
		if (Running.Contains(E.Target))
		{
			Colour = FMath::Lerp(Colour, FLinearColor::White, 0.35f);
			Thickness = 3.f;
		}
		if (E.Id == SelectedEdge) Thickness = 4.f;
		const FVector2D A = SocketLocal(E.Source, E.SourceHandle, true);
		const FVector2D B = SocketLocal(E.Target, E.TargetHandle, false);
		FVector2D TA, TB;
		WireTangents(A, B, Zoom, TA, TB);
		FSlateDrawElement::MakeSpline(Out, Layer, Geometry.ToPaintGeometry(), A, TA, B, TB, Thickness * FMath::Max(0.6f, Zoom), ESlateDrawEffect::None, Colour);
	}
}

void SZettaPipelineCanvas::PaintFront(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer) const
{
	const FSlateBrush* Dot = CanvasBrush(FLinearColor::White, FLinearColor::Transparent, 5.f, 0.f);
	const float Diameter = ZettaCanvasLayout::Socket * FMath::Max(0.6f, Zoom);
	auto DrawDot = [&](const FVector2D& At, const FLinearColor& Colour, float Scale)
	{
		const float D = Diameter * Scale;
		FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(D, D), FSlateLayoutTransform(FVector2f(At - FVector2D(D, D) * 0.5))), Dot, ESlateDrawEffect::None, Colour);
	};

	// Sockets, over the cards' edges.
	for (const TPair<FString, FCard>& Pair : Cards)
	{
		for (const FZettaSocket& S : Pair.Value.Inputs)
		{
			const bool bHot = Mode == EMode::Wiring && WireHover.IsSet() && WireHover.NodeId == Pair.Key && WireHover.Handle == S.Handle && !WireHover.bOutput;
			DrawDot(SocketLocal(Pair.Key, S.Handle, false), ZettaPortTypes::Colour(S.Type), bHot ? 1.5f : 1.f);
		}
		for (const FZettaSocket& S : Pair.Value.Outputs)
		{
			const bool bHot = Mode == EMode::Wiring && WireHover.IsSet() && WireHover.NodeId == Pair.Key && WireHover.Handle == S.Handle && WireHover.bOutput;
			DrawDot(SocketLocal(Pair.Key, S.Handle, true), ZettaPortTypes::Colour(S.Type), bHot ? 1.5f : 1.f);
		}
	}

	// The wire being drawn.
	if (Mode == EMode::Wiring && WireFrom.IsSet())
	{
		const FVector2D Fixed = SocketLocal(WireFrom.NodeId, WireFrom.Handle, WireFrom.bOutput);
		const FVector2D A = WireFrom.bOutput ? Fixed : MouseLocal;
		const FVector2D B = WireFrom.bOutput ? MouseLocal : Fixed;
		FVector2D TA, TB;
		WireTangents(A, B, Zoom, TA, TB);
		FString Type;
		if (const FCard* Card = Cards.Find(WireFrom.NodeId))
			for (const FZettaSocket& S : WireFrom.bOutput ? Card->Outputs : Card->Inputs) if (S.Handle == WireFrom.Handle) Type = S.Type;
		FSlateDrawElement::MakeSpline(Out, Layer, Geometry.ToPaintGeometry(), A, TA, B, TB, 2.f * FMath::Max(0.6f, Zoom), ESlateDrawEffect::None, ZettaPortTypes::Colour(Type));
	}

	// The ✕ on the selected connection.
	if (!bReadOnly && !SelectedEdge.IsEmpty() && Mode != EMode::Wiring)
	{
		if (const FZettaPipelineEdge* E = Graph.Edge(SelectedEdge))
		{
			const FVector2D Mid = EdgeMiddle(*E);
			FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(20, 20), FSlateLayoutTransform(FVector2f(Mid - FVector2D(10, 10)))),
				CanvasBrush(FZettaStyle::PanelRaised(), FZettaStyle::Error(), 10.f, 1.f));
			FSlateDrawElement::MakeText(Out, Layer + 1, Geometry.ToPaintGeometry(FVector2f(20, 20), FSlateLayoutTransform(FVector2f(Mid - FVector2D(4.5, 8)))),
				FString(TEXT("✕")), FZettaStyle::Font(9, true), ESlateDrawEffect::None, FZettaStyle::Error());
		}
	}

	// Shift-drag selection.
	if (Mode == EMode::Marquee)
	{
		const FVector2D Min(FMath::Min(PressLocal.X, MouseLocal.X), FMath::Min(PressLocal.Y, MouseLocal.Y));
		const FVector2D Max(FMath::Max(PressLocal.X, MouseLocal.X), FMath::Max(PressLocal.Y, MouseLocal.Y));
		FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(Max - Min), FSlateLayoutTransform(FVector2f(Min))),
			CanvasBrush(FLinearColor(0.09f, 0.47f, 1.f, 0.12f), FZettaStyle::Accent(), 2.f, 1.f));
	}
}

// ─── Input ──────────────────────────────────────────────────────────────────────────────────────────────────────────

FReply SZettaPipelineCanvas::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton && MouseEvent.GetEffectingButton() != EKeys::MiddleMouseButton) return FReply::Unhandled();
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	PressLocal = MouseLocal = Local;
	bMoved = false;
	FReply Reply = FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);

	// The middle button always moves the canvas.
	if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		Mode = EMode::Panning;
		PanStart = Offset;
		return Reply;
	}

	if (RemoveButtonAt(Local))
	{
		const FString Edge = SelectedEdge;
		SelectedEdge.Reset();
		if (OnRemove) OnRemove({}, { Edge });
		return FReply::Handled();
	}

	if (!bReadOnly)
	{
		const FPortHit Port = PortAt(Local);
		if (Port.IsSet())
		{
			Mode = EMode::Wiring;
			MovingEdge.Reset();
			WireHover = FPortHit();
			if (Port.bOutput) WireFrom = Port;
			else if (const FZettaPipelineEdge* Existing = ZettaGraphRules::InputEdge(Graph, Port.NodeId, Port.Handle))
			{
				// An input's wire picked up: its source end stays, the input end follows the mouse.
				WireFrom = { Existing->Source, Existing->SourceHandle, true };
				MovingEdge = Existing->Id;
			}
			else WireFrom = Port;
			return Reply;
		}
	}

	const FString Node = NodeAt(Local);
	if (!Node.IsEmpty())
	{
		TArray<FString> Next = Selected;
		if (MouseEvent.IsShiftDown())
		{
			if (Next.Contains(Node)) Next.Remove(Node);
			else Next.Add(Node);
		}
		else if (!Next.Contains(Node)) Next = { Node };
		else
		{
			// The one pressed comes first: it is the one the inspector shows.
			Next.Remove(Node);
			Next.Insert(Node, 0);
		}
		ChangeSelection(Next, FString());
		Mode = bReadOnly ? EMode::None : EMode::Dragging;
		DragDelta = FVector2D::ZeroVector;
		return Reply;
	}

	const FString Edge = EdgeAt(Local);
	if (!Edge.IsEmpty())
	{
		ChangeSelection({}, Edge);
		Mode = EMode::None;
		return Reply;
	}

	if (MouseEvent.IsShiftDown() && !bReadOnly)
	{
		Mode = EMode::Marquee;
		return Reply;
	}
	Mode = EMode::Panning;
	PanStart = Offset;
	return Reply;
}

FReply SZettaPipelineCanvas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	MouseLocal = Local;
	if (!bMoved && FVector2D::DistSquared(Local, PressLocal) >= 9) bMoved = true;
	switch (Mode)
	{
	case EMode::Panning:
		if (bMoved) SetView(PanStart + (Local - PressLocal), Zoom);
		return FReply::Handled();
	case EMode::Dragging:
		if (bMoved) DragDelta = (Local - PressLocal) / Zoom;
		return FReply::Handled();
	case EMode::Wiring:
	{
		const FPortHit Hit = PortAt(Local);
		WireHover = Hit.IsSet() && Hit.bOutput != WireFrom.bOutput ? Hit : FPortHit();
		return FReply::Handled();
	}
	case EMode::Marquee:
		return FReply::Handled();
	default:
		break;
	}

	// Hover: what a step is, or what stops it.
	const FString Node = NodeAt(Local);
	FString Tip;
	if (const FZettaPipelineNode* N = Graph.Node(Node))
	{
		const FZettaNodeSpec* Spec = ZettaNodeCatalog::Spec(N->Kind);
		Tip = Spec ? Spec->Blurb : FString();
		if (const FCard* Card = Cards.Find(Node))
		{
			const FZettaStepCardData& D = Card->Widget->Data;
			if (D.Problems.Num() + D.Warnings.Num() > 0)
			{
				TArray<FString> Lines = D.Problems;
				Lines.Append(D.Warnings);
				Tip = FString::Printf(TEXT("%d to fix:\n"), Lines.Num()) + FString::Join(Lines, TEXT("\n"));
			}
		}
	}
	else if (!bReadOnly && !SelectedEdge.IsEmpty() && RemoveButtonAt(Local)) Tip = TEXT("Remove this connection (Delete)");
	HoverTooltip = Tip;
	return FReply::Unhandled();
}

FReply SZettaPipelineCanvas::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const EMode Was = Mode;
	Mode = EMode::None;
	FReply Reply = FReply::Handled().ReleaseMouseCapture();
	switch (Was)
	{
	case EMode::Panning:
		// A click on the empty canvas without moving clears the selection.
		if (!bMoved && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton) ChangeSelection({}, FString());
		break;
	case EMode::Dragging:
		if (bMoved && !DragDelta.IsNearlyZero() && OnMove)
		{
			TMap<FString, FVector2D> Moved;
			for (const FString& Id : Selected)
			{
				if (const FZettaPipelineNode* N = Graph.Node(Id))
				{
					Moved.Add(Id, FVector2D(FMath::RoundToDouble(N->X + DragDelta.X), FMath::RoundToDouble(N->Y + DragDelta.Y)));
				}
			}
			// The cards stay where they were dropped until the graph says so (next Show).
			for (const TPair<FString, FVector2D>& Pair : Moved)
			{
				if (FZettaPipelineNode* N = Graph.Node(Pair.Key))
				{
					N->X = Pair.Value.X;
					N->Y = Pair.Value.Y;
				}
			}
			DragDelta = FVector2D::ZeroVector;
			OnMove(Moved);
		}
		DragDelta = FVector2D::ZeroVector;
		break;
	case EMode::Wiring:
		FinishWire(Local);
		break;
	case EMode::Marquee:
	{
		const FVector2D A = ToGraph(PressLocal), B = ToGraph(Local);
		const FBox2D Box(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), FVector2D(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y)));
		TArray<FString> Inside;
		for (const FZettaPipelineNode& N : Graph.Nodes)
		{
			const FBox2D Card(FVector2D(N.X, N.Y), FVector2D(N.X, N.Y) + CardSize(N.Id));
			if (Box.Intersect(Card)) Inside.Add(N.Id);
		}
		ChangeSelection(Inside, FString());
		break;
	}
	default:
		break;
	}
	WireFrom = FPortHit();
	WireHover = FPortHit();
	MovingEdge.Reset();
	return Reply;
}

void SZettaPipelineCanvas::OnMouseCaptureLost(const FCaptureLostEvent&)
{
	Mode = EMode::None;
	DragDelta = FVector2D::ZeroVector;
	WireFrom = FPortHit();
	MovingEdge.Reset();
}

void SZettaPipelineCanvas::Refuse(const FString& Source, const FString& SourceHandle, const FString& Target, const FString& TargetHandle, const FString& Moving)
{
	const FString Problem = ZettaGraphRules::ConnectionProblem(Graph, Source, SourceHandle, Target, TargetHandle, Moving);
	if (!Problem.IsEmpty() && OnRefused) OnRefused(Problem);
}

void SZettaPipelineCanvas::FinishWire(const FVector2D& Local)
{
	if (!WireFrom.IsSet()) return;
	const FPortHit Hit = PortAt(Local);
	if (!Hit.IsSet())
	{
		// Let go on empty canvas: a connection being moved is taken away; a new wire just disappears.
		if (!MovingEdge.IsEmpty() && Graph.Edge(MovingEdge) && OnRemove) OnRemove({}, { MovingEdge });
		return;
	}
	// Two outputs, two inputs, or a step's own sockets: nothing to connect.
	if (Hit.bOutput == WireFrom.bOutput || Hit.NodeId == WireFrom.NodeId) return;
	const FPortHit& Out = WireFrom.bOutput ? WireFrom : Hit;
	const FPortHit& In = WireFrom.bOutput ? Hit : WireFrom;
	if (!MovingEdge.IsEmpty())
	{
		const FZettaPipelineEdge* Old = Graph.Edge(MovingEdge);
		if (Old && Old->Source == Out.NodeId && Old->SourceHandle == Out.Handle && Old->Target == In.NodeId && Old->TargetHandle == In.Handle) return;
		if (!ZettaGraphRules::MayWire(Graph, Out.NodeId, Out.Handle, In.NodeId, In.Handle, MovingEdge))
		{
			Refuse(Out.NodeId, Out.Handle, In.NodeId, In.Handle, MovingEdge);
			return;
		}
		if (OnReconnect) OnReconnect(MovingEdge, Out.NodeId, Out.Handle, In.NodeId, In.Handle);
		return;
	}
	if (!ZettaGraphRules::MayWire(Graph, Out.NodeId, Out.Handle, In.NodeId, In.Handle))
	{
		Refuse(Out.NodeId, Out.Handle, In.NodeId, In.Handle, FString());
		return;
	}
	if (OnConnect) OnConnect(Out.NodeId, Out.Handle, In.NodeId, In.Handle);
}

FReply SZettaPipelineCanvas::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const float Delta = MouseEvent.GetWheelDelta();
	// A mouse wheel zooms, as the web canvas does; Shift + wheel moves sideways.
	if (MouseEvent.IsShiftDown() && !(MouseEvent.IsControlDown() || MouseEvent.IsCommandDown())) SetView(Offset + FVector2D(Delta * 40.f, 0), Zoom);
	else ZoomAt(Local, FMath::Exp(Delta * 0.15f));
	return FReply::Handled();
}

FReply SZettaPipelineCanvas::OnTouchGesture(const FGeometry& MyGeometry, const FPointerEvent& GestureEvent)
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(GestureEvent.GetScreenSpacePosition());
	const FVector2D Delta = GestureEvent.GetGestureDelta();
	switch (GestureEvent.GetGestureType())
	{
	case EGestureEvent::Magnify:
		ZoomAt(Local, FMath::Max(0.2f, 1.f + (float)Delta.X));
		return FReply::Handled();
	case EGestureEvent::Scroll:
		// Two fingers move the canvas any way they go; with Cmd/Ctrl held they zoom.
		if (GestureEvent.IsControlDown() || GestureEvent.IsCommandDown()) ZoomAt(Local, FMath::Exp(-(float)Delta.Y * 0.01f));
		else SetView(Offset + (GestureEvent.IsDirectionInvertedFromDevice() ? Delta : -Delta), Zoom);
		return FReply::Handled();
	default:
		return FReply::Unhandled();
	}
}

FReply SZettaPipelineCanvas::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape)
	{
		if (Mode != EMode::None)
		{
			Mode = EMode::None;
			DragDelta = FVector2D::ZeroVector;
			WireFrom = FPortHit();
			return FReply::Handled().ReleaseMouseCapture();
		}
		ChangeSelection({}, FString());
		return FReply::Handled();
	}
	if (!bReadOnly && (Key == EKeys::Delete || Key == EKeys::BackSpace) && (Selected.Num() > 0 || !SelectedEdge.IsEmpty()))
	{
		const TArray<FString> Nodes = Selected;
		TArray<FString> Edges;
		if (!SelectedEdge.IsEmpty()) Edges.Add(SelectedEdge);
		Selected.Reset();
		SelectedEdge.Reset();
		LastReported.Reset();
		if (OnRemove) OnRemove(Nodes, Edges);
		if (OnSelectionChanged) OnSelectionChanged(FString());
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SZettaPipelineCanvas::OnDragOver(const FGeometry&, const FDragDropEvent& DragDropEvent)
{
	return !bReadOnly && DragDropEvent.GetOperationAs<FZettaStepDragOp>().IsValid() ? FReply::Handled() : FReply::Unhandled();
}

FReply SZettaPipelineCanvas::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	const TSharedPtr<FZettaStepDragOp> Op = DragDropEvent.GetOperationAs<FZettaStepDragOp>();
	if (bReadOnly || !Op.IsValid()) return FReply::Unhandled();
	const FVector2D At = ToGraph(MyGeometry.AbsoluteToLocal(DragDropEvent.GetScreenSpacePosition()));
	// The card's middle-top under the pointer, as the web drops it.
	if (OnDropKind) OnDropKind(Op->Kind, FVector2D(FMath::RoundToDouble(At.X - 110), FMath::RoundToDouble(At.Y - 20)));
	return FReply::Handled();
}

FCursorReply SZettaPipelineCanvas::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (Mode == EMode::Panning && bMoved) return FCursorReply::Cursor(EMouseCursor::GrabHandClosed);
	if (Mode == EMode::Dragging && bMoved) return FCursorReply::Cursor(EMouseCursor::CardinalCross);
	const FVector2D Local = MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition());
	if (!bReadOnly && PortAt(Local).IsSet()) return FCursorReply::Cursor(EMouseCursor::Crosshairs);
	if (!NodeAt(Local).IsEmpty() || RemoveButtonAt(Local)) return FCursorReply::Cursor(EMouseCursor::Hand);
	return FCursorReply::Cursor(EMouseCursor::Default);
}

#undef LOCTEXT_NAMESPACE
