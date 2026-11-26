// Copyright Zetta 3D. See LICENSE.md.

#include "SZettaLibraryPage.h"
#include "ZettaPipelines.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "ContentBrowserModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IContentBrowserSingleton.h"
#include "Misc/MessageDialog.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "SZettaImageLibrary.h"
#include "SZettaLibraryTiles.h"
#include "Zetta3DEditorModule.h"
#include "ZettaApi.h"
#include "ZettaAuth.h"
#include "ZettaCatalog.h"
#include "ZettaHttp.h"
#include "ZettaImporter.h"
#include "ZettaLibrary.h"
#include "ZettaPages.h"
#include "ZettaSettings.h"
#include "ZettaStudioLink.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace ZettaLibraryPage
{
	static constexpr int32 PageSize = 24;
	static constexpr double SearchDelay = 0.4;

	struct FStatusFilter
	{
		const TCHAR* Id;
		FText Label;
		TArray<FString> Statuses;
	};

	static const TArray<FStatusFilter>& StatusFilters()
	{
		static const TArray<FStatusFilter> Filters = {
			{ TEXT("all"), LOCTEXT("Library.StatusAll", "All"), {} },
			{ TEXT("ready"), LOCTEXT("Library.StatusReady", "Ready"), { TEXT("success") } },
			{ TEXT("running"), LOCTEXT("Library.StatusRunning", "In progress"), { TEXT("pending"), TEXT("queued"), TEXT("processing"), TEXT("saving") } },
			{ TEXT("failed"), LOCTEXT("Library.StatusFailed", "Failed"), { TEXT("failed"), TEXT("cancelled") } },
		};
		return Filters;
	}

	static const TArray<FString>& Features()
	{
		static const TArray<FString> List = {
			TEXT(""), TEXT("txt23d"), TEXT("img23d"), TEXT("multiView23d"), TEXT("retopology"), TEXT("segmentation"), TEXT("smartSegmentation"),
			TEXT("uvUnwrap"), TEXT("retexture"), TEXT("autoRigging"), TEXT("convertFormat"), TEXT("upload"),
		};
		return List;
	}

	/** The Studio tools a model can be sent to, and the feature a tool's own result carries (it does not run on it again). */
	struct FSendTo
	{
		const TCHAR* Tool;
		FText Label;
		const TCHAR* MadeBy;
	};

	static const TArray<FSendTo>& SendTo()
	{
		static const TArray<FSendTo> Tools = {
			{ TEXT("Segment"), LOCTEXT("Library.SendSegment", "Segment"), TEXT("") },
			{ TEXT("Retopology"), LOCTEXT("Library.SendRetopology", "Retopology"), TEXT("retopology") },
			{ TEXT("SmartUv"), LOCTEXT("Library.SendSmartUv", "Smart UV"), TEXT("uvUnwrap") },
			{ TEXT("Texture"), LOCTEXT("Library.SendTexture", "Texture"), TEXT("") },
			{ TEXT("AutoRig"), LOCTEXT("Library.SendAutoRig", "Auto Rig"), TEXT("") },
		};
		return Tools;
	}

	static FString PlaceLabel(const FZettaGeneration& Row)
	{
		return FString::Printf(TEXT("%lld-%s"), Row.Id, *ZettaImporter::Slug(Row.DisplayTitle()));
	}

	static void SyncTo(const FString& ObjectPath, bool bFocus)
	{
		const FAssetData Asset = IAssetRegistry::GetChecked().GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
		if (!Asset.IsValid()) return;
		FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().SyncBrowserToAssets({ Asset }, false, bFocus);
	}
}

// ─── Building ─────────────────────────────────────────────────────────────────

void SZettaLibraryPage::Construct(const FArguments&)
{
	const TArray<ZettaCatalog::FChoice> Modes = {
		{ TEXT("models"), TEXT("3D models"), TEXT("Your 3D generations") },
		{ TEXT("images"), TEXT("Images"), TEXT("Pictures from the Image Studio") },
	};

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 2)
		[
			ZettaUI::Segmented(Mode, Modes, [this]()
			{
				if (*Mode == TEXT("images") && Images.IsValid() && FZettaAuth::Get().IsSignedIn()) Images->EnsureLoaded();
			})
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SWidgetSwitcher)
			.WidgetIndex_Lambda([this]() { return *Mode == TEXT("images") ? 1 : 0; })
			+ SWidgetSwitcher::Slot()[MakeModelsView()]
			+ SWidgetSwitcher::Slot()[SAssignNew(Images, SZettaImageLibrary)]
		]
	];

	AuthHandle = FZettaAuth::Get().OnChanged.AddSP(this, &SZettaLibraryPage::OnAuthChanged);
	FinishedHandle = FZettaJobs::Get().OnFinished.AddSP(this, &SZettaLibraryPage::OnJobFinished);
	if (FZettaAuth::Get().IsSignedIn()) Load();
}

SZettaLibraryPage::~SZettaLibraryPage()
{
	FZettaAuth::Get().OnChanged.Remove(AuthHandle);
	FZettaJobs::Get().OnFinished.Remove(FinishedHandle);
	CloseRunPopup();
}

TSharedRef<SWidget> SZettaLibraryPage::MakeModelsView()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
		[
			MakeToolbar()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 4)
		[
			MakeSelectionBar()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 4)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return Notice.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			[
				ZettaUI::Hint(MakeAttributeLambda([this]() { return FText::FromString(Notice); }))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 4)
		[
			ZettaUI::ErrorText(MakeAttributeLambda([this]() { return FText::FromString(Error); }))
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot().Padding(8, 4)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					// Wraps as many tiles to a row as the window is wide.
					SAssignNew(Grid, SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(10, 10))
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 32)
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda([this]() { return bLoading && Items.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[SNew(SThrobber)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ZettaUI::Hint(LOCTEXT("Library.LoadingLibrary", "Loading your library…"))]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 32)
				[
					MakeEmptyState()
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([this]() { return Total > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
			+ SHorizontalBox::Slot().AutoWidth()
			[
				ZettaUI::Button(LOCTEXT("Library.Previous", "‹ Previous"), FSimpleDelegate::CreateLambda([this]()
				{
					PageNumber = FMath::Max(1, PageNumber - 1);
					Load();
				}), MakeAttributeLambda([this]() { return !bLoading && PageNumber > 1; }))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				ZettaUI::Hint(MakeAttributeLambda([this]()
				{
					const int32 Pages = FMath::Max(1, FMath::DivideAndRoundUp(Total, ZettaLibraryPage::PageSize));
					return FText::Format(LOCTEXT("Library.PageSummary", "Page {0} of {1} · {2} {2}|plural(one=model,other=models)"),
						FText::AsNumber(PageNumber), FText::AsNumber(Pages), Total);
				}))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				ZettaUI::Button(LOCTEXT("Library.Next", "Next ›"), FSimpleDelegate::CreateLambda([this]()
				{
					PageNumber++;
					Load();
				}), MakeAttributeLambda([this]()
				{
					return !bLoading && PageNumber < FMath::DivideAndRoundUp(Total, ZettaLibraryPage::PageSize);
				}))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 8)
		[
			ZettaUI::Hint(LOCTEXT("Library.DragTip", "Tip: drag a model that's in the project into the level viewport to place it there, or double-click any ready model to place it (importing it first)."))
		];
}

TSharedRef<SWidget> SZettaLibraryPage::MakeToolbar()
{
	using namespace ZettaLibraryPage;
	TArray<ZettaCatalog::FChoice> StatusChoices;
	for (const FStatusFilter& S : StatusFilters()) StatusChoices.Add({ S.Id, S.Label.ToString(), FString() });

	auto Refilter = [this]()
	{
		PageNumber = 1;
		Load();
	};

	// Wraps onto a second line in a narrow docked window rather than squeezing.
	return SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6, 6))
		+ SWrapBox::Slot().FillEmptySpace(true).FillLineWhenSizeLessThan(420.f)
		[
			SNew(SBox).MinDesiredWidth(220.f)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("Library.SearchModels", "Search by title or prompt"))
				.OnTextChanged(this, &SZettaLibraryPage::OnSearchChanged)
				.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type How)
				{
					if (How != ETextCommit::OnEnter) return;
					Search = Text.ToString();
					bSearchPending = false;
					PageNumber = 1;
					Load();
				})
			]
		]
		+ SWrapBox::Slot().VAlign(VAlign_Center)
		[
			SNew(SBox).MinDesiredWidth(150.f).ToolTipText(LOCTEXT("Library.FeatureTip", "Show one kind of model"))
			[
				ZettaUI::Choice(Feature, []()
				{
					TArray<ZettaCatalog::FChoice> Out;
					for (const FString& F : Features()) Out.Add({ F, F.IsEmpty() ? FString(TEXT("All kinds")) : ZettaCatalog::FeatureLabel(F), FString() });
					return Out;
				}, Refilter)
			]
		]
		+ SWrapBox::Slot().VAlign(VAlign_Center)
		[
			ZettaUI::Segmented(StatusFilter, StatusChoices, Refilter)
		]
		+ SWrapBox::Slot().VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SBox)
				.Visibility_Lambda([this]() { return !bPicking && Items.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					ZettaUI::Button(LOCTEXT("Library.Select", "Select"), FSimpleDelegate::CreateLambda([this]() { SetPicking(true); }), true,
						LOCTEXT("Library.SelectTip", "Select several models to delete them together"))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				ZettaUI::Button(LOCTEXT("Library.Refresh", "Refresh"), FSimpleDelegate::CreateLambda([this]() { Load(); }),
					MakeAttributeLambda([this]() { return FZettaAuth::Get().IsSignedIn(); }))
			]
		];
}

TSharedRef<SWidget> SZettaLibraryPage::MakeSelectionBar()
{
	return SNew(SBorder)
		.BorderImage(FZettaStyle::Get().GetBrush("Zetta.CardRaised"))
		.Padding(FMargin(8, 4))
		.Visibility_Lambda([this]() { return bPicking ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text_Lambda([this]()
				{
					if (Deleting.IsSet()) return FText::Format(LOCTEXT("Library.DeletingProgress", "Deleting {0} of {1}…"), Deleting->Key, Deleting->Value);
					if (Picked.Num() == 0) return LOCTEXT("Library.ClickToSelect", "Click the models to select");
					return FText::Format(LOCTEXT("Library.NSelected", "{0} selected"), FText::AsNumber(Picked.Num()));
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				ZettaUI::Button(MakeAttributeLambda([this]() { return AllOnPagePicked() ? LOCTEXT("Library.ClearPage", "Clear this page") : LOCTEXT("Library.SelectPage", "Select all on this page"); }),
					FSimpleDelegate::CreateSP(this, &SZettaLibraryPage::ToggleAllOnPage),
					MakeAttributeLambda([this]() { return !Deleting.IsSet() && Items.Num() > 0; }))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
			[
				ZettaUI::Button(MakeAttributeLambda([this]()
				{
					return Picked.Num() > 0 ? FText::Format(LOCTEXT("Library.DeleteN", "Delete {0}"), FText::AsNumber(Picked.Num())) : LOCTEXT("Library.DeletePicked", "Delete");
				}), FSimpleDelegate::CreateSP(this, &SZettaLibraryPage::DeleteSelected),
					MakeAttributeLambda([this]() { return !Deleting.IsSet() && Picked.Num() > 0; }))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
			[
				ZettaUI::Button(LOCTEXT("Library.Done", "Done"), FSimpleDelegate::CreateLambda([this]() { SetPicking(false); }),
					MakeAttributeLambda([this]() { return !Deleting.IsSet(); }))
			]
		];
}

TSharedRef<SWidget> SZettaLibraryPage::MakeEmptyState()
{
	return SNew(SVerticalBox)
		.Visibility_Lambda([this]() { return bLoaded && !bLoading && Error.IsEmpty() && Items.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			ZettaUI::Hint(MakeAttributeLambda([this]()
			{
				return HasFilters()
					? LOCTEXT("Library.NothingMatches", "Nothing matches those filters.")
					: LOCTEXT("Library.NoModels", "Models you generate appear here — on the web, in Unreal or in any Zetta 3D plugin.");
			}))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return HasFilters() ? EVisibility::Collapsed : EVisibility::Visible; })
			[
				ZettaUI::PrimaryButton(LOCTEXT("Library.OpenStudio", "Make one in the 3D Studio"),
					FSimpleDelegate::CreateLambda([]() { FZetta3DEditorModule::Get().OpenWindow(TEXT("Studio")); }))
			]
		];
}

// ─── Loading ──────────────────────────────────────────────────────────────────

bool SZettaLibraryPage::HasFilters() const
{
	return !Search.TrimStartAndEnd().IsEmpty() || !Feature->IsEmpty() || *StatusFilter != TEXT("all");
}

void SZettaLibraryPage::Load()
{
	if (!FZettaAuth::Get().IsSignedIn()) return;
	const int32 This = ++Request;
	bLoaded = true;
	bLoading = true;
	Error.Reset();

	ZettaApi::FListQuery Query;
	Query.Page = PageNumber;
	Query.PageSize = ZettaLibraryPage::PageSize;
	Query.Search = Search;
	Query.Feature = *Feature;
	// A pipeline run comes as one entry carrying its models, so a run is never split across pages.
	Query.bGroupRuns = true;
	for (const ZettaLibraryPage::FStatusFilter& S : ZettaLibraryPage::StatusFilters())
	{
		if (*StatusFilter == S.Id) Query.Statuses = S.Statuses;
	}

	TWeakPtr<SZettaLibraryPage> Weak = SharedThis(this);
	ZettaApi::ListGenerations(Query, [Weak, This](const FZettaResponse& R, const FZettaGenerationPage& Result)
	{
		const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin();
		if (!Self.IsValid() || This != Self->Request) return;
		Self->bLoading = false;
		if (!R.bOk)
		{
			Self->Items.Reset();
			Self->Total = 0;
			Self->Redraw();
			Self->Error = TEXT("Your library could not be loaded. ") + ZettaCatalog::Explain(R);
			return;
		}
		Self->Total = Result.Total;
		const int32 Pages = FMath::Max(1, FMath::DivideAndRoundUp(Result.Total, ZettaLibraryPage::PageSize));
		if (Self->PageNumber > Pages && Result.Total > 0)
		{
			// The last page emptied (a delete): show the one before it.
			Self->PageNumber = Pages;
			Self->Load();
			return;
		}
		Self->Items = Result.Items.FilterByPredicate([](const FZettaGenerationPtr& G) { return G.IsValid(); });
		Self->Redraw();
	});
}

void SZettaLibraryPage::Redraw()
{
	if (!Grid.IsValid()) return;
	Grid->ClearChildren();
	const TWeakPtr<SZettaLibraryPage> Weak = SharedThis(this);
	for (const FZettaGenerationPtr& Row : Items)
	{
		if (ZettaLibrary::IsRunEntry(Row)) Grid->AddSlot()[SNew(SZettaRunTile).Page(Weak).Group(Row->RunGroup)];
		else Grid->AddSlot()[SNew(SZettaGenerationTile).Page(Weak).Row(Row)];
	}
	RefreshRunPopup();
}

void SZettaLibraryPage::ClearList()
{
	++Request;
	Items.Reset();
	Total = 0;
	PageNumber = 1;
	bLoaded = false;
	bLoading = false;
	Error.Reset();
	Notice.Reset();
	bPicking = false;
	Picked.Reset();
	Redraw();
}

void SZettaLibraryPage::OnAuthChanged()
{
	const FZettaAuth& Auth = FZettaAuth::Get();
	if (Auth.IsSignedIn())
	{
		// Signed in (or back in as someone else): load once. Credit refreshes also land here and change nothing.
		if (!bLoaded) Load();
		if (*Mode == TEXT("images") && Images.IsValid()) Images->EnsureLoaded();
	}
	else if (Auth.GetState() == FZettaAuth::EState::SignedOut)
	{
		CloseRunPopup();
		ClearList();
		if (Images.IsValid()) Images->Reset();
	}
}

void SZettaLibraryPage::OnJobFinished(const FZettaJobPtr&)
{
	// A model finished (made here, by the agent or a pipeline): it shows up, or its status changes.
	if (bLoaded && !Deleting.IsSet()) Load();
}

void SZettaLibraryPage::OnSearchChanged(const FText& Text)
{
	Search = Text.ToString();
	LastTyped = FPlatformTime::Seconds();
	if (bSearchPending) return;
	bSearchPending = true;
	// Typing pauses before the library is asked again, so each key doesn't make a request.
	RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
	{
		if (!bSearchPending) return EActiveTimerReturnType::Stop;
		if (FPlatformTime::Seconds() - LastTyped < ZettaLibraryPage::SearchDelay) return EActiveTimerReturnType::Continue;
		bSearchPending = false;
		PageNumber = 1;
		Load();
		return EActiveTimerReturnType::Stop;
	}));
}

void SZettaLibraryPage::ShowError(const FString& Message) { Error = Message; }
void SZettaLibraryPage::ShowNotice(const FString& Message) { Notice = Message; }

// ─── Selecting ────────────────────────────────────────────────────────────────

void SZettaLibraryPage::SetPicking(bool bOn)
{
	if (Deleting.IsSet()) return;
	bPicking = bOn;
	if (!bOn) Picked.Reset();
}

void SZettaLibraryPage::TogglePick(int64 Id)
{
	if (Deleting.IsSet()) return;
	if (Picked.Contains(Id)) Picked.Remove(Id);
	else Picked.Add(Id);
}

bool SZettaLibraryPage::IsRunPicked(const FZettaRunGroup& Group) const
{
	if (Group.Members.Num() == 0) return false;
	for (const FZettaGenerationPtr& M : Group.Members)
	{
		if (M.IsValid() && !Picked.Contains(M->Id)) return false;
	}
	return true;
}

void SZettaLibraryPage::PickRun(const TSharedPtr<FZettaRunGroup>& Group)
{
	if (Deleting.IsSet() || !Group.IsValid()) return;
	const bool bAll = IsRunPicked(*Group);
	for (const FZettaGenerationPtr& M : Group->Members)
	{
		if (!M.IsValid()) continue;
		if (bAll) Picked.Remove(M->Id);
		else Picked.Add(M->Id);
	}
}

bool SZettaLibraryPage::AllOnPagePicked() const
{
	const TArray<FZettaGenerationPtr> Models = ZettaLibrary::Flatten(Items);
	if (Models.Num() == 0) return false;
	for (const FZettaGenerationPtr& M : Models)
	{
		if (!Picked.Contains(M->Id)) return false;
	}
	return true;
}

void SZettaLibraryPage::ToggleAllOnPage()
{
	const bool bAll = AllOnPagePicked();
	for (const FZettaGenerationPtr& M : ZettaLibrary::Flatten(Items))
	{
		if (bAll) Picked.Remove(M->Id);
		else Picked.Add(M->Id);
	}
}

// ─── Import and place ─────────────────────────────────────────────────────────

void SZettaLibraryPage::Import(const FZettaGenerationPtr& Row, bool bPlace)
{
	if (!Row.IsValid() || ZettaLibrary::IsImporting(Row->Id)) return;
	if (Row->IsSplat())
	{
		ShowError(ZettaImporter::SplatUnsupported);
		return;
	}
	ShowNotice(FString());
	Error.Reset();

	const int64 Id = Row->Id;
	const FString Title = Row->DisplayTitle();
	ZettaLibrary::Importing().Add(Id, { 0.f, TEXT("Starting…") });

	FZettaImportOptions Options;
	Options.Folder = UZettaProjectSettings::Get()->GetModelsFolder();
	Options.bPlace = bPlace;

	TWeakPtr<SZettaLibraryPage> Weak = SharedThis(this);
	ZettaImporter::Import(Row, Options, [Weak, Id, Title, bPlace](const FZettaImportResult& Result)
	{
		ZettaLibrary::Importing().Remove(Id);
		const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin();
		if (!Result.bOk)
		{
			const FString Message = FString::Printf(TEXT("%s: %s"), *Title, *Result.Error);
			if (Self.IsValid()) Self->ShowError(Message);
			else ZettaLibrary::Toast(FText::FromString(Message), true);
			return;
		}
		// Imported only: point at it in the Content Browser without pulling focus from the Zetta window.
		if (!bPlace) ZettaLibraryPage::SyncTo(Result.MainAssetPath(), false);
		ZettaLibrary::Toast(FText::Format(bPlace && Result.Placed.Num() > 0 ? LOCTEXT("Library.Placed", "Placed {0}") : LOCTEXT("Library.Imported", "Imported {0}"), FText::FromString(Title)));
		if (Self.IsValid()) Self->ShowNotice(Result.Note);
	}, [Id](const FString& Status, float Progress)
	{
		if (ZettaLibrary::FImporting* State = ZettaLibrary::Importing().Find(Id))
		{
			State->Status = Status;
			State->Progress = Progress;
		}
	});
}

void SZettaLibraryPage::Place(const FZettaGenerationPtr& Row)
{
	if (!Row.IsValid()) return;
	const TArray<FAssetData> Meshes = ZettaLibrary::ImportedMeshes(Row->Id);
	// Its assets were deleted from the project (or never made it): bring a fresh copy and place that.
	if (Meshes.Num() == 0)
	{
		Import(Row, true);
		return;
	}
	TArray<FString> Paths;
	for (const FAssetData& A : Meshes) Paths.Add(A.GetObjectPathString());
	FZettaImportOptions Options;
	Options.bPlace = true;
	const TArray<AActor*> Placed = ZettaImporter::Place(Paths, ZettaLibraryPage::PlaceLabel(*Row), Options);
	ZettaLibrary::Toast(Placed.Num() > 0
		? FText::Format(LOCTEXT("Library.PlacedExisting", "Placed {0}"), FText::FromString(Row->DisplayTitle()))
		: FText::Format(LOCTEXT("Library.PlaceFailed", "{0} could not be placed — open a level first"), FText::FromString(Row->DisplayTitle())), Placed.Num() == 0);
}

void SZettaLibraryPage::ImportRun(const TSharedPtr<FZettaRunGroup>& Group)
{
	if (!Group.IsValid()) return;
	ShowNotice(FString());
	Error.Reset();

	struct FRunImport
	{
		TArray<FZettaGenerationPtr> Members;
		int32 Index = 0;
		int32 Imported = 0;
		FString LastError;
	};
	TSharedRef<FRunImport> State = MakeShared<FRunImport>();
	// Already in the project, or on its way: nothing to download again. First step first.
	State->Members = ZettaLibrary::InRunOrder(Group->Members).FilterByPredicate([](const FZettaGenerationPtr& M)
	{
		return M->IsSuccess() && !M->IsSplat() && !ZettaProjectIndex::Contains(M->Id) && !ZettaLibrary::IsImporting(M->Id);
	});
	if (State->Members.Num() == 0)
	{
		ZettaLibrary::Toast(LOCTEXT("Library.RunAllHere", "Every model from this run is already in the project"));
		return;
	}
	for (const FZettaGenerationPtr& M : State->Members) ZettaLibrary::Importing().Add(M->Id, { 0.f, TEXT("Waiting…") });

	const TWeakPtr<SZettaLibraryPage> Weak = SharedThis(this);
	const FString Folder = UZettaProjectSettings::Get()->GetModelsFolder();
	// One at a time, so the downloads don't fight and the run lands in order. Each import holds the chain alive.
	TSharedRef<TFunction<void()>> Next = MakeShared<TFunction<void()>>();
	TWeakPtr<TFunction<void()>> WeakNext = Next;
	*Next = [Weak, State, WeakNext, Folder]()
	{
		const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin();
		const int32 Count = State->Members.Num();
		if (State->Index >= Count)
		{
			if (Self.IsValid())
			{
				Self->ShowNotice(FString());
				if (!State->LastError.IsEmpty())
				{
					Self->ShowError(FString::Printf(TEXT("%d of %d could not be imported. %s"), Count - State->Imported, Count, *State->LastError));
				}
			}
			ZettaLibrary::Toast(FText::Format(LOCTEXT("Library.RunImported", "Imported {0} of {1} models"), State->Imported, Count), State->Imported < Count);
			return;
		}
		const FZettaGenerationPtr Member = State->Members[State->Index];
		const int32 Position = State->Index + 1;
		if (Self.IsValid()) Self->ShowNotice(FString::Printf(TEXT("Importing %d of %d…"), Position, Count));

		FZettaImportOptions Options;
		Options.Folder = Folder;
		TSharedPtr<TFunction<void()>> Keep = WeakNext.Pin();
		ZettaImporter::Import(Member, Options, [State, Member, Keep](const FZettaImportResult& Result)
		{
			ZettaLibrary::Importing().Remove(Member->Id);
			if (Result.bOk) State->Imported++;
			else State->LastError = Result.Error;
			State->Index++;
			if (Keep.IsValid()) (*Keep)();
		}, [Weak, Member, Position, Count](const FString& Status, float Progress)
		{
			if (ZettaLibrary::FImporting* I = ZettaLibrary::Importing().Find(Member->Id))
			{
				I->Status = Status;
				I->Progress = Progress;
			}
			if (const TSharedPtr<SZettaLibraryPage> Page = Weak.Pin())
			{
				Page->ShowNotice(FString::Printf(TEXT("Importing %d of %d… %d%%"), Position, Count, FMath::RoundToInt(Progress * 100.f)));
			}
		});
	};
	(*Next)();
}

void SZettaLibraryPage::SaveUvLayout(const FZettaGenerationPtr& Row)
{
	const FZettaResultFile* File = Row.IsValid() ? Row->UvLayout() : nullptr;
	if (!File)
	{
		ShowError(TEXT("This model has no UV layout picture."));
		return;
	}
	const FString Key = File->Key;
	const FString Fallback = File->Url;
	const FString PackagePath = UZettaProjectSettings::Get()->GetImagesFolder()
		/ FString::Printf(TEXT("T_%s_%lld_UVLayout"), *ZettaImporter::Slug(Row->DisplayTitle()).Replace(TEXT("-"), TEXT("_")), Row->Id);
	TWeakPtr<SZettaLibraryPage> Weak = SharedThis(this);
	auto Fail = [Weak](const FString& Message)
	{
		if (const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin()) Self->ShowError(Message);
		else ZettaLibrary::Toast(FText::FromString(Message), true);
	};
	// The row's links expire after a while — ask for a fresh one by key.
	ZettaApi::DownloadUrl(Row->Id, Key, [Fallback, PackagePath, Fail](const FZettaResponse& R, const FString& Fresh)
	{
		FZettaHttp::Download(R.bOk && !Fresh.IsEmpty() ? Fresh : Fallback, [PackagePath, Fail](const FZettaResponse& D)
		{
			if (!D.bOk)
			{
				Fail(ZettaCatalog::Explain(D));
				return;
			}
			FString ObjectPath, Problem;
			if (!ZettaImporter::SaveTexture(D.Bytes, PackagePath, ObjectPath, Problem, true))
			{
				Fail(Problem);
				return;
			}
			ZettaLibraryPage::SyncTo(ObjectPath, true);
			ZettaLibrary::Toast(FText::Format(LOCTEXT("Library.SavedUv", "Saved the UV layout to {0}"), FText::FromString(PackagePath)));
		});
	});
}

// ─── Rename, delete ───────────────────────────────────────────────────────────

void SZettaLibraryPage::Rename(const FZettaGenerationPtr& Row, const FString& NewTitle)
{
	const FString Title = NewTitle.TrimStartAndEnd();
	if (!Row.IsValid() || Title.IsEmpty() || Title == Row->DisplayTitle()) return;
	const FString Old = Row->Title;
	// Shown straight away; put back if the server says no.
	Row->Title = Title;
	TWeakPtr<SZettaLibraryPage> Weak = SharedThis(this);
	ZettaApi::UpdateGeneration(Row->Id, Title, TOptional<TArray<FString>>(), TOptional<FString>(),
		[Weak, Row, Old, Title](const FZettaResponse& R, const FZettaGenerationPtr& Updated)
	{
		if (R.bOk)
		{
			if (Updated.IsValid() && !Updated->Title.IsEmpty()) Row->Title = Updated->Title;
			return;
		}
		Row->Title = Old;
		if (const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin()) Self->ShowError(ZettaCatalog::Explain(R));
	});
}

void SZettaLibraryPage::Delete(const FZettaGenerationPtr& Row)
{
	if (!Row.IsValid()) return;
	const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNo,
		FText::Format(LOCTEXT("Library.DeleteOneBody", "Delete “{0}” from your Zetta 3D library? Copies already imported into this project are kept."), FText::FromString(Row->DisplayTitle())),
		LOCTEXT("Library.DeleteOneTitle", "Delete model"));
	if (Answer != EAppReturnType::Yes) return;

	const int64 Id = Row->Id;
	TWeakPtr<SZettaLibraryPage> Weak = SharedThis(this);
	ZettaApi::DeleteGeneration(Id, [Weak, Id](const FZettaResponse& R)
	{
		const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		if (!R.bOk)
		{
			Self->ShowError(ZettaCatalog::Explain(R));
			return;
		}
		Self->Picked.Remove(Id);
		Self->Load();
	});
}

void SZettaLibraryPage::DeleteSelected()
{
	const TArray<int64> Ids = Picked.Array();
	if (Ids.Num() == 0 || Deleting.IsSet()) return;
	FText Title, Message;
	ZettaLibrary::BulkDeleteText(Ids.Num(), Title, Message);
	if (FMessageDialog::Open(EAppMsgType::YesNo, Message, Title) != EAppReturnType::Yes) return;

	Error.Reset();
	TWeakPtr<SZettaLibraryPage> Weak = SharedThis(this);
	ZettaLibrary::RunDeletes(Ids, [Weak](int32 Done, int32 Count)
	{
		if (const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin()) Self->Deleting = TPair<int32, int32>(Done, Count);
	}, [Weak](const TArray<int64>& Deleted, const TArray<int64>& Failed, const FString& LastError)
	{
		const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		Self->Deleting.Reset();
		// The ones that could not be deleted stay selected, and say so; the rest are gone either way.
		Self->Picked = TSet<int64>(Failed);
		Self->Load();
		if (Failed.Num() > 0)
		{
			Self->ShowError(FString::Printf(TEXT("%d of %d could not be deleted — they are still selected. %s"),
				Failed.Num(), Deleted.Num() + Failed.Num(), LastError.IsEmpty() ? TEXT("Try again in a moment.") : *LastError));
		}
	});
}

void SZettaLibraryPage::DeleteRun(const TSharedPtr<FZettaRunGroup>& Group)
{
	if (!Group.IsValid() || Deleting.IsSet()) return;
	TArray<int64> Ids;
	for (const FZettaGenerationPtr& M : Group->Members) if (M.IsValid()) Ids.Add(M->Id);
	if (Ids.Num() == 0) return;

	FString Name, Detail;
	ZettaLibrary::RunName(*Group, Name, Detail);
	FText Title, Message;
	ZettaLibrary::BulkDeleteText(Ids.Num(), Title, Message);
	Title = FText::Format(LOCTEXT("Library.DeleteRunTitle", "Delete all {0} {0}|plural(one=model,other=models) from “{1}”?"), Ids.Num(), FText::FromString(Name));
	Message = FText::Format(LOCTEXT("Library.DeleteRunBody", "{0}\n\nThe run itself stays in Pipelines."), Message);
	if (FMessageDialog::Open(EAppMsgType::YesNo, Message, Title) != EAppReturnType::Yes) return;

	Error.Reset();
	TWeakPtr<SZettaLibraryPage> Weak = SharedThis(this);
	ZettaLibrary::RunDeletes(Ids, [Weak](int32 Done, int32 Count)
	{
		if (const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin())
		{
			Self->Deleting = TPair<int32, int32>(Done, Count);
			Self->ShowNotice(FString::Printf(TEXT("Deleting %d of %d…"), Done, Count));
		}
	}, [Weak](const TArray<int64>& Deleted, const TArray<int64>& Failed, const FString& LastError)
	{
		const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		Self->Deleting.Reset();
		Self->ShowNotice(FString());
		for (const int64 Id : Deleted) Self->Picked.Remove(Id);
		Self->Load();
		if (Failed.Num() > 0)
		{
			Self->ShowError(FString::Printf(TEXT("%d of %d could not be deleted. %s"), Failed.Num(), Deleted.Num() + Failed.Num(),
				LastError.IsEmpty() ? TEXT("Try again in a moment.") : *LastError));
		}
	});
}

// ─── Menus ────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SZettaLibraryPage::MakeMenu(const FZettaGenerationPtr& Row, const TSharedPtr<FZettaRunGroup>& Group, TWeakPtr<SZettaGenerationTile> Tile)
{
	FMenuBuilder Menu(true, nullptr);
	if (!Row.IsValid()) return Menu.MakeWidget();
	const TWeakPtr<SZettaLibraryPage> Weak = SharedThis(this);
	auto Act = [Weak](TFunction<void(SZettaLibraryPage&)> Do)
	{
		return FUIAction(FExecuteAction::CreateLambda([Weak, Do]() { if (const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin()) Do(*Self); }));
	};
	const FSlateIcon NoIcon;

	// A splat is not supported in Unreal: it only opens on the site, or is deleted.
	if (Row->IsSplat())
	{
		Menu.AddMenuEntry(LOCTEXT("Library.MenuOpenSite", "Open on zetta3d.ai"), FText::FromString(ZettaImporter::SplatUnsupported), NoIcon,
			FUIAction(FExecuteAction::CreateLambda([Row]() { ZettaLibrary::OpenOnSite(*Row); })));
		Menu.AddMenuSeparator();
		Menu.AddMenuEntry(LOCTEXT("Library.MenuDelete", "Delete…"), FText(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
			Act([Row](SZettaLibraryPage& P) { P.Delete(Row); }));
		return Menu.MakeWidget();
	}

	if (Row->IsSuccess())
	{
		Menu.BeginSection(NAME_None, LOCTEXT("Library.SectionProject", "In this project"));
		if (ZettaProjectIndex::Contains(Row->Id))
		{
			Menu.AddMenuEntry(LOCTEXT("Library.MenuShow", "Show in Content Browser"), FText(), NoIcon,
				FUIAction(FExecuteAction::CreateLambda([Row]() { ZettaLibrary::ShowInContentBrowser(Row->Id); })));
			Menu.AddMenuEntry(LOCTEXT("Library.MenuPlace", "Place in level"), LOCTEXT("Library.MenuPlaceTip", "Put the imported model in the open level — no download"), NoIcon,
				Act([Row](SZettaLibraryPage& P) { P.Place(Row); }));
			Menu.AddMenuEntry(LOCTEXT("Library.MenuReimport", "Import again (download a fresh copy)"), FText(), NoIcon,
				Act([Row](SZettaLibraryPage& P) { P.Import(Row, false); }));
		}
		else if (!ZettaLibrary::IsImporting(Row->Id))
		{
			Menu.AddMenuEntry(LOCTEXT("Library.MenuImport", "Import"), LOCTEXT("Library.MenuImportTip", "Download it into the project"), NoIcon,
				Act([Row](SZettaLibraryPage& P) { P.Import(Row, false); }));
			Menu.AddMenuEntry(LOCTEXT("Library.MenuImportPlace", "Import and place in level"), FText(), NoIcon,
				Act([Row](SZettaLibraryPage& P) { P.Import(Row, true); }));
		}
		if (Row->UvLayout())
		{
			Menu.AddMenuEntry(LOCTEXT("Library.MenuUv", "Save UV layout picture"), LOCTEXT("Library.MenuUvTip", "Save the UV layout as a texture in the Images folder"), NoIcon,
				Act([Row](SZettaLibraryPage& P) { P.SaveUvLayout(Row); }));
		}
		Menu.EndSection();

		Menu.BeginSection(NAME_None, LOCTEXT("Library.SectionTools", "3D Studio"));
		const bool bProcessable = ZettaCatalog::IsProcessable(Row->Feature);
		Menu.AddSubMenu(LOCTEXT("Library.MenuSendTo", "Send to"),
			bProcessable ? LOCTEXT("Library.MenuSendToTip", "Open a Studio tool with this model as its source") : LOCTEXT("Library.MenuSendToNo", "This kind of model can't be sent to the Studio's tools"),
			FNewMenuDelegate::CreateLambda([Row, bProcessable](FMenuBuilder& Sub)
			{
				for (const ZettaLibraryPage::FSendTo& Tool : ZettaLibraryPage::SendTo())
				{
					// The tool that made a row does not run on it again.
					const bool bMade = *Tool.MadeBy && Row->Feature == Tool.MadeBy;
					const FName ToolId(Tool.Tool);
					Sub.AddMenuEntry(Tool.Label, FText(), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([ToolId, Row]() { ZettaStudioLink::OpenTool(ToolId, Row); }),
							FCanExecuteAction::CreateLambda([bProcessable, bMade]() { return bProcessable && !bMade; })));
				}
			}));
	}

	// Fork: a new generation on the form, with this one's settings and pictures. Nothing runs.
	if (ZettaLibrary::CanFork(*Row))
	{
		Menu.AddMenuEntry(LOCTEXT("Library.MenuFork", "Fork"), LOCTEXT("Library.MenuForkTip", "Open Generate with this model's settings and pictures — nothing runs until you press Generate"), NoIcon,
			FUIAction(FExecuteAction::CreateLambda([Row]() { ZettaStudioLink::Fork(Row); })));
	}
	if (Row->IsSuccess()) Menu.EndSection();

	Menu.AddMenuSeparator();
	if (Group.IsValid() && Group->PipelineId.IsSet())
	{
		Menu.AddMenuEntry(FText::Format(LOCTEXT("Library.MenuOpenRun", "Open run #{0}"), FText::AsNumber(Group->RunId, &FNumberFormattingOptions::DefaultNoGrouping())),
			LOCTEXT("Library.MenuOpenRunTip", "Open Pipelines"), NoIcon,
			Act([PipelineId = Group->PipelineId.GetValue(), RunId = Group->RunId](SZettaLibraryPage& P)
			{
				P.CloseRunPopup();
				ZettaPipelines::OpenInWindow(PipelineId, RunId);
			}));
	}
	Menu.AddMenuEntry(LOCTEXT("Library.MenuOpenSiteAll", "Open on zetta3d.ai"), FText(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.OpenInExternalEditor"),
		FUIAction(FExecuteAction::CreateLambda([Row]() { ZettaLibrary::OpenOnSite(*Row); })));
	Menu.AddMenuEntry(LOCTEXT("Library.MenuRename", "Rename…"), FText(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Edit"),
		FUIAction(FExecuteAction::CreateLambda([Tile]() { if (const TSharedPtr<SZettaGenerationTile> T = Tile.Pin()) T->BeginRename(); })));
	Menu.AddMenuSeparator();
	Menu.AddMenuEntry(LOCTEXT("Library.MenuDelete", "Delete…"), FText(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
		Act([Row](SZettaLibraryPage& P) { P.Delete(Row); }));
	return Menu.MakeWidget();
}

TSharedRef<SWidget> SZettaLibraryPage::MakeRunMenu(const TSharedPtr<FZettaRunGroup>& Group)
{
	FMenuBuilder Menu(true, nullptr);
	if (!Group.IsValid()) return Menu.MakeWidget();
	const TWeakPtr<SZettaLibraryPage> Weak = SharedThis(this);

	const int32 Importable = Group->Members.FilterByPredicate([](const FZettaGenerationPtr& M) { return M.IsValid() && M->IsSuccess() && !M->IsSplat(); }).Num();
	Menu.AddMenuEntry(Importable > 0 ? FText::Format(LOCTEXT("Library.RunImportN", "Import all ({0})"), Importable) : LOCTEXT("Library.RunImport", "Import all"),
		LOCTEXT("Library.RunImportTip", "Import every finished model this run made into the project, first step first"), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([Weak, Group]() { if (const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin()) Self->ImportRun(Group); }),
			FCanExecuteAction::CreateLambda([Importable]() { return Importable > 0; })));
	if (Group->PipelineId.IsSet())
	{
		Menu.AddMenuEntry(FText::Format(LOCTEXT("Library.RunOpen", "Open run #{0}"), FText::AsNumber(Group->RunId, &FNumberFormattingOptions::DefaultNoGrouping())),
			LOCTEXT("Library.RunOpenTip", "Open Pipelines"), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([PipelineId = Group->PipelineId.GetValue(), RunId = Group->RunId]() { ZettaPipelines::OpenInWindow(PipelineId, RunId); })));
	}
	Menu.AddMenuSeparator();
	Menu.AddMenuEntry(
		MakeAttributeLambda([Weak, Group]()
		{
			const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin();
			return Self.IsValid() && Self->IsPicking() && Self->IsRunPicked(*Group) ? LOCTEXT("Library.RunUnselect", "Unselect this run") : LOCTEXT("Library.RunSelect", "Select all in this run");
		}),
		FText(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([Weak, Group]()
		{
			const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin();
			if (!Self.IsValid()) return;
			if (!Self->IsPicking()) Self->SetPicking(true);
			Self->PickRun(Group);
		})));
	Menu.AddMenuEntry(LOCTEXT("Library.RunDelete", "Delete all…"), LOCTEXT("Library.RunDeleteTip", "Delete every model this run made — the run itself stays in Pipelines"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
		FUIAction(FExecuteAction::CreateLambda([Weak, Group]() { if (const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin()) Self->DeleteRun(Group); })));
	return Menu.MakeWidget();
}

// ─── A run's pop-up ───────────────────────────────────────────────────────────

TSharedPtr<FZettaRunGroup> SZettaLibraryPage::CurrentGroup(const TSharedPtr<FZettaRunGroup>& Group) const
{
	if (!Group.IsValid()) return Group;
	for (const FZettaGenerationPtr& Row : Items)
	{
		if (ZettaLibrary::IsRunEntry(Row) && Row->RunGroup->RunId == Group->RunId) return Row->RunGroup;
	}
	return Group;
}

void SZettaLibraryPage::ShowRun(const TSharedPtr<FZettaRunGroup>& Group)
{
	if (!Group.IsValid()) return;
	CloseRunPopup();
	using namespace ZettaLibraryUI;

	FString Name, Detail;
	ZettaLibrary::RunName(*Group, Name, Detail);
	const int64 RunId = Group->RunId;
	const bool bImportable = Group->Members.ContainsByPredicate([](const FZettaGenerationPtr& M) { return M.IsValid() && M->IsSuccess() && !M->IsSplat(); });
	const TWeakPtr<SZettaLibraryPage> Weak = SharedThis(this);

	// Its pictures, fanned: the first three models, overlapping a little.
	TSharedRef<SHorizontalBox> Thumbs = SNew(SHorizontalBox);
	int32 Shown = 0;
	for (const FZettaGenerationPtr& M : Group->Members)
	{
		if (!M.IsValid() || Shown >= 3) break;
		Thumbs->AddSlot().AutoWidth().Padding(Shown == 0 ? 0.f : -12.f, 0.f, 0.f, 0.f)
		[
			SNew(SZettaRemoteImage).Url(M->ThumbnailUrl).Size(FVector2D(40, 40))
		];
		++Shown;
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::FromString(Name))
		.ClientSize(FVector2D(840, 640))
		.SizingRule(ESizingRule::UserSized)
		.SupportsMinimize(false)
		.SupportsMaximize(true)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
			.Padding(12)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[Thumbs]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[ZettaUI::Section(FText::FromString(Name))]
						+ SVerticalBox::Slot().AutoHeight()[ZettaUI::Hint(FText::FromString(Detail))]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[PipelineChip(RunId)]
				]
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(RunGrid, SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(10, 10))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[SNew(SSeparator)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
					[
						SNew(SBox).Visibility(bImportable ? EVisibility::Visible : EVisibility::Collapsed)
						[
							ZettaUI::Button(LOCTEXT("Library.PopupImportAll", "Import all"), FSimpleDelegate::CreateLambda([Weak, Group]()
							{
								if (const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin()) Self->ImportRun(Self->CurrentGroup(Group));
							}), true, LOCTEXT("Library.PopupImportAllTip", "Import every finished model this run made into the project, first step first"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						ZettaUI::Button(LOCTEXT("Library.PopupDeleteAll", "Delete all"), FSimpleDelegate::CreateLambda([Weak, Group]()
						{
							const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin();
							if (!Self.IsValid()) return;
							const TSharedPtr<FZettaRunGroup> Current = Self->CurrentGroup(Group);
							Self->CloseRunPopup();
							Self->DeleteRun(Current);
						}), MakeAttributeLambda([Weak]() { const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin(); return Self.IsValid() && !Self->IsDeleting(); }),
							FText::Format(LOCTEXT("Library.PopupDeleteAllTip", "Delete every model run #{0} made — the run itself stays in Pipelines"), FText::AsNumber(RunId, &FNumberFormattingOptions::DefaultNoGrouping())))
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer)]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
					[
						SNew(SBox).Visibility(Group->PipelineId.IsSet() ? EVisibility::Visible : EVisibility::Collapsed)
						[
							ZettaUI::Button(LOCTEXT("Library.PopupOpenRun", "Open run"), FSimpleDelegate::CreateLambda([Weak, PipelineId = Group->PipelineId.Get(0), RunId]()
							{
								if (const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin()) Self->CloseRunPopup();
								ZettaPipelines::OpenInWindow(PipelineId, RunId);
							}), true, FText::Format(LOCTEXT("Library.PopupOpenRunTip", "Open run #{0} in Pipelines"), FText::AsNumber(RunId, &FNumberFormattingOptions::DefaultNoGrouping())))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						ZettaUI::Button(LOCTEXT("Library.PopupClose", "Close"), FSimpleDelegate::CreateLambda([Weak]()
						{
							if (const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin()) Self->CloseRunPopup();
						}))
					]
				]
			]
		];

	const TWeakPtr<SWindow> WeakWindow = Window;
	Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda([Weak, WeakWindow](const TSharedRef<SWindow>&)
	{
		const TSharedPtr<SZettaLibraryPage> Self = Weak.Pin();
		if (!Self.IsValid() || Self->RunWindow != WeakWindow) return;
		Self->RunWindow.Reset();
		Self->RunGrid.Reset();
		Self->RunWindowId = 0;
	}));

	RunWindow = Window;
	RunWindowId = RunId;
	FillRunPopup(Group);

	const TSharedPtr<SWindow> Parent = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (Parent.IsValid()) FSlateApplication::Get().AddWindowAsNativeChild(Window, Parent.ToSharedRef());
	else FSlateApplication::Get().AddWindow(Window);
}

void SZettaLibraryPage::FillRunPopup(const TSharedPtr<FZettaRunGroup>& Group)
{
	if (!RunGrid.IsValid() || !Group.IsValid()) return;
	RunGrid->ClearChildren();
	const TWeakPtr<SZettaLibraryPage> Weak = SharedThis(this);
	const TArray<FZettaGenerationPtr> Ordered = ZettaLibrary::InRunOrder(Group->Members);
	for (int32 i = 0; i < Ordered.Num(); ++i)
	{
		RunGrid->AddSlot()
		[
			SNew(SZettaGenerationTile).Page(Weak).Row(Ordered[i]).Group(Group).Step(ZettaLibrary::StepText(*Ordered[i], i))
		];
	}
}

void SZettaLibraryPage::RefreshRunPopup()
{
	if (!RunWindow.IsValid()) return;
	for (const FZettaGenerationPtr& Row : Items)
	{
		if (ZettaLibrary::IsRunEntry(Row) && Row->RunGroup->RunId == RunWindowId)
		{
			FillRunPopup(Row->RunGroup);
			return;
		}
	}
	CloseRunPopup();
}

void SZettaLibraryPage::CloseRunPopup()
{
	const TSharedPtr<SWindow> Window = RunWindow.Pin();
	RunWindow.Reset();
	RunGrid.Reset();
	RunWindowId = 0;
	if (Window.IsValid()) Window->RequestDestroyWindow();
}

// ─── The page ─────────────────────────────────────────────────────────────────

TSharedRef<SWidget> ZettaPages::MakeLibraryPage()
{
	return SNew(SZettaLibraryPage);
}

#undef LOCTEXT_NAMESPACE
