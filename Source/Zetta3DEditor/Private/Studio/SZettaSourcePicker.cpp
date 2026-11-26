// Copyright Zetta 3D. See LICENSE.md.

#include "SZettaSourcePicker.h"

#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "ZettaApi.h"
#include "ZettaCatalog.h"
#include "ZettaStudioAssets.h"
#include "ZettaStudioUI.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

static constexpr int32 SourcePageSize = 24;

void SZettaSourcePicker::Construct(const FArguments& InArgs, FRefuse InRefuse)
{
	Refuse = MoveTemp(InRefuse);
	OnChanged = InArgs._OnChanged;

	ChildSlot
	[
		ZettaStudioUI::Group(LOCTEXT("SourceModel", "Source model"),
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SAssignNew(Current, SBox)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
			[
				SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.f, 4.f))
				+ SWrapBox::Slot()
				[
					ZettaStudioUI::SmallButton(MakeAttributeLambda([this]() { return bBrowserOpen ? LOCTEXT("HideLibrary", "Hide library") : LOCTEXT("PickLibrary", "Pick from library"); }),
						[this]() { ToggleBrowser(); })
				]
				+ SWrapBox::Slot()
				[
					ZettaStudioUI::SmallButton(LOCTEXT("UploadModel", "Upload a model file…"), [this]() { UploadFile(); },
						MakeAttributeLambda([this]() { return !bWorking; }),
						LOCTEXT("UploadModelTip", "A .glb or .fbx from your computer goes into your library (free) and is used here."))
				]
				+ SWrapBox::Slot()
				[
					ZettaStudioUI::SmallButton(LOCTEXT("UseSelected", "Use selected asset"), [this]() { UseSelected(); },
						MakeAttributeLambda([this]() { return !bWorking; }),
						LOCTEXT("UseSelectedTip", "Select a mesh in the Content Browser. Models imported from Zetta 3D are used directly; others are uploaded from the .glb or .fbx they were imported from (free)."))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[ZettaStudioUI::Hint(MakeAttributeLambda([this]() { return FText::FromString(Status); }))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[ZettaStudioUI::Error(MakeAttributeLambda([this]() { return FText::FromString(Error); }))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
			[
				SNew(SVerticalBox)
				.Visibility_Lambda([this]() { return ZettaStudioUI::Shown(bBrowserOpen); })
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
				[
					SNew(SSearchBox)
					.HintText(LOCTEXT("SearchLibrary", "Search your library"))
					.OnTextChanged(this, &SZettaSourcePicker::SearchChanged)
					.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type)
					{
						Search = Text.ToString().TrimStartAndEnd();
						Reload();
					})
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).MaxDesiredHeight(320)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()[SAssignNew(Grid, SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.f, 6.f))]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0).HAlign(HAlign_Left)
				[
					SNew(SBox).Visibility_Lambda([this]() { return ZettaStudioUI::Shown(bHasMore || bLoading); })
					[
						ZettaStudioUI::SmallButton(MakeAttributeLambda([this]() { return bLoading ? LOCTEXT("Loading", "Loading…") : LOCTEXT("LoadMore", "Load more"); }),
							[this]() { LoadPage(PageLoaded + 1); }, MakeAttributeLambda([this]() { return !bLoading; }))
					]
				]
			])
	];
	RenderCurrent();
}

SZettaSourcePicker::~SZettaSourcePicker()
{
	if (SearchTimer.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(SearchTimer);
}

void SZettaSourcePicker::Set(const FZettaGenerationPtr& Generation)
{
	if (Generation.IsValid() && Refuse)
	{
		const FString Reason = Refuse(*Generation);
		if (!Reason.IsEmpty())
		{
			Error = Reason;
			return;
		}
	}
	Error.Reset();
	Source = Generation;
	bBrowserOpen = false;
	RenderCurrent();
	OnChanged.ExecuteIfBound();
}

void SZettaSourcePicker::Forget()
{
	++LoadTicket;
	bLoading = false;
	bHasMore = false;
	bBrowserOpen = false;
	Rows.Reset();
	if (Grid.IsValid()) Grid->ClearChildren();
	Error.Reset();
	Status.Reset();
	if (Source.IsValid())
	{
		Source.Reset();
		RenderCurrent();
		OnChanged.ExecuteIfBound();
	}
}

void SZettaSourcePicker::RenderCurrent()
{
	if (!Current.IsValid()) return;
	if (!Source.IsValid())
	{
		Current->SetContent(ZettaUI::Hint(LOCTEXT("NoSource", "Choose a finished model from your library, upload a .glb or .fbx, or use a model from the Content Browser.")));
		return;
	}
	TSharedRef<SWrapBox> Chips = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.f, 4.f))
		+ SWrapBox::Slot()[ZettaStudioUI::Chip(FText::FromString(ZettaCatalog::FeatureLabel(Source->Feature)))];
	if (Source->FaceCount.IsSet() && Source->FaceCount.GetValue() > 0)
	{
		Chips->AddSlot()[ZettaStudioUI::Chip(FText::Format(LOCTEXT("FacesChip", "{0} faces"), FText::AsNumber(Source->FaceCount.GetValue())))];
	}
	if (!Source->Model.IsEmpty()) Chips->AddSlot()[ZettaStudioUI::Chip(FText::FromString(ZettaCatalog::ModelLabel(Source->Model)))];

	Current->SetContent(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
		[
			SNew(SZettaRemoteImage).Url(Source->ThumbnailUrl).Size(FVector2D(64, 64)).Placeholder(LOCTEXT("ThumbNone", "3D"))
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
			[
				SNew(STextBlock).Text(FText::FromString(Source->DisplayTitle())).Font(FZettaStyle::Font(10, true)).AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight()[Chips]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
		[
			SNew(SButton)
			.ButtonStyle(&FZettaStyle::Get().GetWidgetStyle<FButtonStyle>("Zetta.Button.Close"))
				.ContentPadding(FMargin(4))
			.ToolTipText(LOCTEXT("ClearSource", "Choose another model"))
			.OnClicked_Lambda([this]()
			{
				Set(nullptr);
				return FReply::Handled();
			})
			[
				SNew(SBox).WidthOverride(10).HeightOverride(10)[SNew(SImage).Image(FAppStyle::GetBrush("Icons.X")).ColorAndOpacity(FLinearColor::White)]
			]
		]);
}

void SZettaSourcePicker::ToggleBrowser()
{
	bBrowserOpen = !bBrowserOpen;
	if (bBrowserOpen && Rows.Num() == 0) Reload();
}

void SZettaSourcePicker::SearchChanged(const FText& Text)
{
	Search = Text.ToString().TrimStartAndEnd();
	if (SearchTimer.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(SearchTimer);
	// Reload once typing pauses, not on every key.
	TWeakPtr<SZettaSourcePicker> Weak = SharedThis(this);
	SearchTimer = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
	{
		if (const TSharedPtr<SZettaSourcePicker> Self = Weak.Pin())
		{
			Self->SearchTimer.Reset();
			Self->Reload();
		}
		return false;
	}), 0.4f);
}

void SZettaSourcePicker::Reload()
{
	Rows.Reset();
	if (Grid.IsValid()) Grid->ClearChildren();
	bLoading = false;
	++LoadTicket;
	LoadPage(1);
}

void SZettaSourcePicker::LoadPage(int32 Page)
{
	if (bLoading) return;
	bLoading = true;
	Error.Reset();
	ZettaApi::FListQuery Query;
	Query.Page = Page;
	Query.PageSize = SourcePageSize;
	Query.Search = Search;
	Query.Statuses = { TEXT("success") };
	const int32 Ticket = LoadTicket;
	TWeakPtr<SZettaSourcePicker> Weak = SharedThis(this);
	ZettaApi::ListGenerations(Query, [Weak, Page, Ticket](const FZettaResponse& R, const FZettaGenerationPage& Result)
	{
		const TSharedPtr<SZettaSourcePicker> Self = Weak.Pin();
		if (!Self || Ticket != Self->LoadTicket) return;
		Self->bLoading = false;
		if (!R.bOk)
		{
			Self->Error = ZettaCatalog::Explain(R);
			return;
		}
		Self->PageLoaded = Page;
		Self->bHasMore = Result.Total > Page * SourcePageSize;
		for (const FZettaGenerationPtr& Row : Result.Items)
		{
			if (!Row.IsValid() || !Row->IsSuccess() || !ZettaCatalog::IsProcessable(Row->Feature)) continue;
			Self->Rows.Add(Row);
			Self->Grid->AddSlot()[Self->Tile(Row)];
		}
		if (Self->Rows.Num() == 0 && !Self->bHasMore)
		{
			Self->Grid->AddSlot()
			[
				ZettaUI::Hint(Self->Search.IsEmpty()
					? LOCTEXT("NoFinished", "No finished models yet. Generate one first, or upload a model file.")
					: LOCTEXT("NoMatch", "Nothing matches that search."))
			];
		}
	});
}

TSharedRef<SWidget> SZettaSourcePicker::Tile(const FZettaGenerationPtr& Row)
{
	const FString Reason = Refuse ? Refuse(*Row) : FString();
	return SNew(SBox).WidthOverride(96)
		[
			SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
			.ToolTipText(FText::FromString(Reason.IsEmpty() ? Row->DisplayTitle() : Reason))
			.OnClicked_Lambda([this, Row]()
			{
				Set(Row);
				return FReply::Handled();
			})
			[
				SNew(SVerticalBox)
				.RenderOpacity(Reason.IsEmpty() ? 1.f : 0.45f)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(SZettaRemoteImage).Url(Row->ThumbnailUrl).Size(FVector2D(88, 88)).Placeholder(LOCTEXT("ThumbNone", "3D"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
				[
					SNew(STextBlock).Text(FText::FromString(Row->DisplayTitle())).Font(FZettaStyle::Font(8)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
			]
		];
}

void SZettaSourcePicker::UploadFile()
{
	const FString Path = ZettaUI::PickFile(TEXT("Choose a model"), TEXT("3D models (*.glb;*.fbx)|*.glb;*.fbx"));
	if (Path.IsEmpty()) return;
	bWorking = true;
	Error.Reset();
	TWeakPtr<SZettaSourcePicker> Weak = SharedThis(this);
	ZettaStudioAssets::UploadModelFile(Path, [Weak](const FString& Problem, const FZettaGenerationPtr& G)
	{
		const TSharedPtr<SZettaSourcePicker> Self = Weak.Pin();
		if (!Self) return;
		Self->bWorking = false;
		Self->Status.Reset();
		if (!Problem.IsEmpty()) Self->Error = Problem;
		else Self->Set(G);
	}, [Weak](const FString& Text)
	{
		if (const TSharedPtr<SZettaSourcePicker> Self = Weak.Pin()) Self->Status = Text;
	});
}

void SZettaSourcePicker::UseSelected()
{
	bWorking = true;
	Error.Reset();
	TWeakPtr<SZettaSourcePicker> Weak = SharedThis(this);
	ZettaStudioAssets::SourceFromSelectedAsset([Weak](const FString& Problem, const FZettaGenerationPtr& G)
	{
		const TSharedPtr<SZettaSourcePicker> Self = Weak.Pin();
		if (!Self) return;
		Self->bWorking = false;
		Self->Status.Reset();
		if (!Problem.IsEmpty()) Self->Error = Problem;
		else Self->Set(G);
	}, [Weak](const FString& Text)
	{
		if (const TSharedPtr<SZettaSourcePicker> Self = Weak.Pin()) Self->Status = Text;
	});
}

#undef LOCTEXT_NAMESPACE
