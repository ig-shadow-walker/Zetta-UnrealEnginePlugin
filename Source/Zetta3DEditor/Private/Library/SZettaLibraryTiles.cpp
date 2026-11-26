// Copyright Zetta 3D. See LICENSE.md.

#include "SZettaLibraryTiles.h"

#include "DragAndDrop/AssetDragDropOp.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "SZettaLibraryPage.h"
#include "ZettaCatalog.h"
#include "ZettaImporter.h"
#include "ZettaLibrary.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace ZettaLibraryUI
{
	TSharedRef<SWidget> Chip(const FText& Text, const FSlateColor& Color, const FText& Tooltip)
	{
		return SNew(SBorder)
			.BorderImage(FZettaStyle::Get().GetBrush("Zetta.Chip"))
			.Padding(FMargin(6, 1))
			.ToolTipText(Tooltip)
			[
				SNew(STextBlock).Text(Text).Font(FZettaStyle::Font(8)).ColorAndOpacity(Color)
			];
	}

	TSharedRef<SWidget> PipelineChip(int64 RunId)
	{
		return Chip(LOCTEXT("Library.PipelineChip", "Pipeline"), FSlateColor(FZettaStyle::Accent()),
			FText::Format(LOCTEXT("Library.PipelineChipTip", "Made by pipeline run #{0}"), FText::AsNumber(RunId, &FNumberFormattingOptions::DefaultNoGrouping())));
	}

	FSlateColor StatusColor(const FZettaGeneration& Row)
	{
		if (Row.IsSuccess()) return FSlateColor(FZettaStyle::Success());
		if (Row.Status == TEXT("failed") || Row.Status == TEXT("cancelled")) return FSlateColor(FZettaStyle::Error());
		return FSlateColor(FZettaStyle::Warning());
	}

	/** A "more actions" drop-down: the small arrow every tile ends with. */
	static TSharedRef<SWidget> MoreButton(TFunction<TSharedRef<SWidget>()> Menu, const FText& Tooltip)
	{
		return SNew(SComboButton)
			.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
			.HasDownArrow(true)
			.ToolTipText(Tooltip)
			.OnGetMenuContent_Lambda([Menu]() { return Menu(); });
	}

	/** A button that shows only while Visible says so. */
	static TSharedRef<SWidget> Shown(TFunction<bool()> Visible, const TSharedRef<SWidget>& Content, const FMargin& Padding = FMargin(0, 0, 4, 0))
	{
		return SNew(SBox)
			.Padding(Padding)
			.Visibility_Lambda([Visible]() { return Visible() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				Content
			];
	}
}

/**
 * The drag for a model that is not in the project yet. The level viewport and the Content Browser only take
 * assets that already exist, and an import is a download that can take a while — so rather than a drag that
 * stalls the editor or drops nothing, it says what to do. (Double-click imports and places in one go.)
 */
class FZettaImportFirstDragOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FZettaImportFirstDragOp, FDecoratedDragDropOp)

	static TSharedRef<FZettaImportFirstDragOp> New()
	{
		TSharedRef<FZettaImportFirstDragOp> Op = MakeShared<FZettaImportFirstDragOp>();
		Op->DefaultHoverText = LOCTEXT("Library.ImportFirst", "Import first, then drag — or double-click to import and place it");
		Op->DefaultHoverIcon = FAppStyle::GetBrush("Icons.Warning");
		Op->DefaultHoverIconColorAndOpacity = FSlateColor(FZettaStyle::Warning());
		Op->ResetToDefaultToolTip();
		Op->Construct();
		return Op;
	}
};

// ─── SZettaGenerationTile ─────────────────────────────────────────────────────

void SZettaGenerationTile::Construct(const FArguments& InArgs)
{
	using namespace ZettaLibraryUI;
	Page = InArgs._Page;
	Row = InArgs._Row;
	Group = InArgs._Group;
	check(Row.IsValid());
	const FZettaGenerationPtr R = Row;
	const bool bSplat = R->IsSplat();

	TSharedRef<SWrapBox> Chips = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(3, 3));
	if (!InArgs._Step.IsEmpty()) Chips->AddSlot()[Chip(FText::FromString(InArgs._Step), FSlateColor::UseForeground(), FText::FromString(InArgs._Step))];
	Chips->AddSlot()[Chip(FText::FromString(R->StatusLabel()), StatusColor(*R), FText::FromString(R->ErrorMessage))];
	// Made by a pipeline: told apart from models made by hand. Otherwise it behaves like any other row.
	if (R->PipelineRunId.IsSet()) Chips->AddSlot()[PipelineChip(R->PipelineRunId.GetValue())];
	Chips->AddSlot()
	[
		SNew(SBox)
		.Visibility_Lambda([this]() { return InProject() ? EVisibility::Visible : EVisibility::Collapsed; })
		.ToolTipText_Lambda([this]() { return FText::Format(LOCTEXT("Library.InProjectTip", "Already imported to {0}"), FText::FromString(ZettaProjectIndex::FolderOf(Row->Id))); })
		[
			Chip(LOCTEXT("Library.InProject", "In project"), FSlateColor(FZettaStyle::Success()))
		]
	];
	Chips->AddSlot()[Chip(FText::FromString(ZettaCatalog::FeatureLabel(R->Feature)))];
	const FString Badge = ZettaLibrary::ModelBadge(R->Model);
	if (!Badge.IsEmpty()) Chips->AddSlot()[Chip(FText::FromString(Badge))];
	if (bSplat)
	{
		Chips->AddSlot()[Chip(LOCTEXT("Library.SplatChip", "Splat — open on the web"), FSlateColor(FZettaStyle::Warning()), FText::FromString(ZettaImporter::SplatUnsupported))];
	}
	// A world's mesh export holds both meshes: say which is which.
	if (R->Feature == TEXT("worldMeshExport"))
	{
		for (const FZettaResultFile& File : R->MeshFiles())
		{
			if (File.Label.IsEmpty()) continue;
			const FString Type = (File.Type.IsEmpty() ? File.Extension() : File.Type).ToUpper();
			Chips->AddSlot()[Chip(FText::FromString(FString::Printf(TEXT("%s · %s"), *Type, *File.Label)))];
		}
	}

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(TileWidth)
		[
			SNew(SBorder)
			.BorderImage_Lambda([this]()
			{
				const TSharedPtr<SZettaLibraryPage> P = Page.Pin();
				return FZettaStyle::Get().GetBrush(P.IsValid() && P->IsPicked(Row->Id) ? "Zetta.CardSelected" : "Zetta.Card");
			})
			.Padding(8)
			.ToolTipText_Lambda([this]() { return TooltipText(); })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SZettaRemoteImage)
						.Url(R->ThumbnailUrl)
						.Size(FVector2D(ThumbSize, ThumbSize))
						.Placeholder(R->IsRunning() ? FText::FromString(R->StatusLabel()) : FText::FromString(ZettaCatalog::FeatureLabel(R->Feature)))
					]
					+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(4)
					[
						SNew(SBox)
						.Visibility_Lambda([this]()
						{
							const TSharedPtr<SZettaLibraryPage> P = Page.Pin();
							return P.IsValid() && P->IsPicked(Row->Id) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
						})
						[
							Chip(LOCTEXT("Library.Selected", "Selected"), FSlateColor(FZettaStyle::Accent()))
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					SAssignNew(TitleBlock, SInlineEditableTextBlock)
					.Text_Lambda([this]() { return FText::FromString(Row->DisplayTitle()); })
					.Font(FZettaStyle::Font(10, true))
					.IsReadOnly_Lambda([this]() { return !bRenaming; })
					.MaximumLength(255)
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.OnExitEditingMode_Lambda([this]() { bRenaming = false; })
					.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type How)
					{
						const bool bWasRenaming = bRenaming;
						bRenaming = false;
						if (!bWasRenaming || How == ETextCommit::OnCleared) return;
						if (const TSharedPtr<SZettaLibraryPage> P = Page.Pin()) P->Rename(Row, Text.ToString());
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
				[
					ZettaUI::Hint(ZettaLibrary::DateText(R->CreatedAt))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
				[
					Chips
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					SNew(SVerticalBox)
					.Visibility_Lambda([this]() { return ZettaLibrary::IsImporting(Row->Id) ? EVisibility::Visible : EVisibility::Collapsed; })
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SProgressBar)
						.Percent_Lambda([this]() -> TOptional<float>
						{
							const ZettaLibrary::FImporting* I = ZettaLibrary::Importing().Find(Row->Id);
							return I ? TOptional<float>(I->Progress) : TOptional<float>();
						})
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
					[
						ZettaUI::Hint(MakeAttributeLambda([this]()
						{
							const ZettaLibrary::FImporting* I = ZettaLibrary::Importing().Find(Row->Id);
							return I ? FText::FromString(I->Status) : FText::GetEmpty();
						}))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					MakeActions()
				]
			]
		]
	];
}

bool SZettaGenerationTile::InProject() const
{
	return Row->IsSuccess() && !Row->IsSplat() && ZettaProjectIndex::Contains(Row->Id);
}

bool SZettaGenerationTile::CanUseInLevel() const
{
	return Row->IsSuccess() && !Row->IsSplat();
}

TSharedRef<SWidget> SZettaGenerationTile::MakeActions()
{
	using namespace ZettaLibraryUI;
	const FZettaGenerationPtr R = Row;
	const bool bSplat = R->IsSplat();
	const bool bReady = R->IsSuccess() && !bSplat;
	const TWeakPtr<SZettaLibraryPage> WeakPage = Page;
	const TWeakPtr<SZettaGenerationTile> WeakTile = SharedThis(this);

	auto Importing = [R]() { return ZettaLibrary::IsImporting(R->Id); };
	auto Here = [this]() { return InProject(); };

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Shown([bSplat]() { return bSplat; }, ZettaUI::Button(LOCTEXT("Library.OpenOnWeb", "Open on web"),
				FSimpleDelegate::CreateLambda([R]() { ZettaLibrary::OpenOnSite(*R); }), true, FText::FromString(ZettaImporter::SplatUnsupported)))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Shown([bReady, Importing]() { return bReady && Importing(); }, ZettaUI::Button(LOCTEXT("Library.Importing", "Importing…"), FSimpleDelegate(), false))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Shown([bReady, Importing, Here]() { return bReady && !Importing() && Here(); }, ZettaUI::Button(LOCTEXT("Library.Show", "Show"),
				FSimpleDelegate::CreateLambda([R]() { ZettaLibrary::ShowInContentBrowser(R->Id); }), true,
				LOCTEXT("Library.ShowTip", "Select it in the Content Browser")))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Shown([bReady, Importing, Here]() { return bReady && !Importing() && Here(); }, ZettaUI::Button(LOCTEXT("Library.Place", "Place"),
				FSimpleDelegate::CreateLambda([WeakPage, R]() { if (const TSharedPtr<SZettaLibraryPage> P = WeakPage.Pin()) P->Place(R); }), true,
				LOCTEXT("Library.PlaceTip", "Put the imported model in the open level — no download")))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Shown([bReady, Importing, Here]() { return bReady && !Importing() && !Here(); }, ZettaUI::Button(LOCTEXT("Library.Import", "Import"),
				FSimpleDelegate::CreateLambda([WeakPage, R]() { if (const TSharedPtr<SZettaLibraryPage> P = WeakPage.Pin()) P->Import(R, false); }), true,
				LOCTEXT("Library.ImportTip", "Download it into the project (the Models folder in Project Settings › Zetta 3D)")))
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNew(SSpacer)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			MoreButton([WeakPage, R, WeakTile, this]() -> TSharedRef<SWidget>
			{
				const TSharedPtr<SZettaLibraryPage> P = WeakPage.Pin();
				return P.IsValid() ? P->MakeMenu(R, Group, WeakTile) : SNullWidget::NullWidget;
			}, LOCTEXT("Library.MoreTip", "More: send to a tool, fork, rename, delete…"))
		];
}

FText SZettaGenerationTile::TooltipText() const
{
	if (Row->IsSplat()) return FText::FromString(ZettaImporter::SplatUnsupported);
	if (!Row->ErrorMessage.IsEmpty() && !Row->IsSuccess()) return FText::FromString(Row->ErrorMessage);
	if (!Row->IsSuccess()) return FText::FromString(Row->DisplayTitle());
	return InProject()
		? LOCTEXT("Library.TipInProject", "In the project. Double-click to place it in the level, or drag it into the level viewport.")
		: LOCTEXT("Library.TipNotInProject", "Double-click to import it and place it in the level. Once it's imported you can drag it into the level viewport.");
}

void SZettaGenerationTile::BeginRename()
{
	if (!TitleBlock.IsValid()) return;
	bRenaming = true;
	TitleBlock->EnterEditingMode();
}

FReply SZettaGenerationTile::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || bRenaming) return FReply::Unhandled();
	bPressed = true;
	return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
}

FReply SZettaGenerationTile::OnMouseButtonUp(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bPressed) return FReply::Unhandled();
	bPressed = false;
	// Select mode: a click on the tile (not on one of its buttons, which take their own clicks) picks it.
	const TSharedPtr<SZettaLibraryPage> P = Page.Pin();
	if (P.IsValid() && P->IsPicking() && !P->IsDeleting()) P->TogglePick(Row->Id);
	return FReply::Handled();
}

FReply SZettaGenerationTile::OnMouseButtonDoubleClick(const FGeometry&, const FPointerEvent& MouseEvent)
{
	const TSharedPtr<SZettaLibraryPage> P = Page.Pin();
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !P.IsValid() || P->IsPicking() || bRenaming) return FReply::Unhandled();
	if (!CanUseInLevel() || ZettaLibrary::IsImporting(Row->Id)) return FReply::Handled();
	if (InProject()) P->Place(Row);
	else P->Import(Row, true);
	return FReply::Handled();
}

FReply SZettaGenerationTile::OnDragDetected(const FGeometry&, const FPointerEvent&)
{
	bPressed = false;
	const TSharedPtr<SZettaLibraryPage> P = Page.Pin();
	if (!P.IsValid() || P->IsPicking() || !CanUseInLevel()) return FReply::Unhandled();

	// In the project: the editor's own asset drag, so the level viewport places it where it lands (with its
	// usual preview and undo) and the Content Browser can copy it. A segmented model brings all its parts.
	TArray<FAssetData> Meshes = ZettaLibrary::ImportedMeshes(Row->Id);
	if (Meshes.Num() > 0) return FReply::Handled().BeginDragDrop(FAssetDragDropOp::New(MoveTemp(Meshes)));
	return FReply::Handled().BeginDragDrop(FZettaImportFirstDragOp::New());
}

// ─── SZettaRunTile ────────────────────────────────────────────────────────────

void SZettaRunTile::Construct(const FArguments& InArgs)
{
	using namespace ZettaLibraryUI;
	Page = InArgs._Page;
	Group = InArgs._Group;
	check(Group.IsValid());
	const TSharedPtr<FZettaRunGroup> G = Group;
	const TWeakPtr<SZettaLibraryPage> WeakPage = Page;

	FString Name, Detail;
	ZettaLibrary::RunName(*G, Name, Detail);
	const int32 Count = ZettaLibrary::RunCount(*G);
	const bool bRunning = G->Members.ContainsByPredicate([](const FZettaGenerationPtr& M) { return M.IsValid() && M->IsRunning(); });
	const FZettaGenerationPtr* Cover = G->Members.FindByPredicate([](const FZettaGenerationPtr& M) { return M.IsValid() && !M->ThumbnailUrl.IsEmpty(); });
	const FString CoverUrl = Cover ? (*Cover)->ThumbnailUrl : FString();
	const FText CountText = FText::Format(LOCTEXT("Library.RunModels", "{0} {0}|plural(one=model,other=models)"), Count);

	TSharedRef<SOverlay> Deck = SNew(SOverlay);
	// The cards behind, each set a little up and to the right so their edges show above the front card. (Turned cards
	// read better on paper but their thin edges crossed into the neighbouring tiles.)
	for (int32 Depth = FMath::Min(Count - 1, 2); Depth >= 1; --Depth)
	{
		Deck->AddSlot()
		[
			SNew(SBorder)
			.Visibility(EVisibility::HitTestInvisible)
			.BorderImage(FZettaStyle::Get().GetBrush(Depth == 1 ? "Zetta.CardRaised" : "Zetta.Card"))
			.RenderTransform(FSlateRenderTransform(FVector2D(4.f * Depth, -4.f * Depth)))
		];
	}

	Deck->AddSlot()
	[
		SNew(SBorder)
		.BorderImage_Lambda([this]()
		{
			const TSharedPtr<SZettaLibraryPage> P = Page.Pin();
			return FZettaStyle::Get().GetBrush(P.IsValid() && P->IsPicking() && P->IsRunPicked(*Group) ? "Zetta.CardSelected" : "Zetta.Card");
		})
		.Padding(8)
		.ToolTipText(FText::Format(LOCTEXT("Library.RunTip", "Pipeline run: {0} — {1}. Click to see its models."), FText::FromString(Name), FText::FromString(Detail)))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SZettaRemoteImage).Url(CoverUrl).Size(FVector2D(ThumbSize, ThumbSize)).Placeholder(LOCTEXT("Library.RunPlaceholder", "Pipeline run"))
				]
				+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(4)
				[
					Chip(CountText)
				]
				+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(4)
				[
					SNew(SBox).Visibility(bRunning ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
					[
						Chip(LOCTEXT("Library.StillMaking", "Still making"), FSlateColor(FZettaStyle::Warning()))
					]
				]
				+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(4)
				[
					SNew(SBox)
					.Visibility_Lambda([this]()
					{
						const TSharedPtr<SZettaLibraryPage> P = Page.Pin();
						return P.IsValid() && P->IsPicking() && P->IsRunPicked(*Group) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					})
					[
						Chip(LOCTEXT("Library.Selected", "Selected"), FSlateColor(FZettaStyle::Accent()))
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
			[
				SNew(STextBlock).Text(FText::FromString(Name)).Font(FZettaStyle::Font(10, true)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)[PipelineChip(G->RunId)]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[ZettaUI::Hint(CountText)]
			]
			+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SSpacer)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer)]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MoreButton([WeakPage, G]() -> TSharedRef<SWidget>
					{
						const TSharedPtr<SZettaLibraryPage> P = WeakPage.Pin();
						return P.IsValid() ? P->MakeRunMenu(G) : SNullWidget::NullWidget;
					}, LOCTEXT("Library.RunMoreTip", "Actions for the whole run — import all, open the run, delete all"))
				]
			]
		]
	];

	ChildSlot
	[
		SNew(SBox).WidthOverride(TileWidth)
		[
			Deck
		]
	];
}

FReply SZettaRunTile::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	bPressed = true;
	return FReply::Handled();
}

FReply SZettaRunTile::OnMouseButtonUp(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bPressed) return FReply::Unhandled();
	bPressed = false;
	const TSharedPtr<SZettaLibraryPage> P = Page.Pin();
	if (!P.IsValid() || P->IsDeleting()) return FReply::Handled();
	if (P->IsPicking()) P->PickRun(Group);
	else P->ShowRun(Group);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
