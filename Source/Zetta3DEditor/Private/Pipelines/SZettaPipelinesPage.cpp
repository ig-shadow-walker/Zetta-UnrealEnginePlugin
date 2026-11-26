// Copyright Zetta 3D. See LICENSE.md.

// 3D PIPELINES in Unreal: the list of the user's pipelines, and one open — its recipe on a node canvas and its runs.
// The open pipeline is remembered for the editor session. A page of the Zetta 3D window.

#include "ZettaPages.h"

#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "SZettaPipelineEditor.h"
#include "ZettaAuth.h"
#include "ZettaCatalog.h"
#include "ZettaPipelineServices.h"
#include "ZettaPipelineText.h"
#include "ZettaPipelines.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

/**
 * YOUR PIPELINES. Empty, it is one centred invitation — "Build your first pipeline" and New pipeline. Otherwise a card
 * each: its title, its steps, its runs at a glance; Open, Duplicate, Delete (pressed twice).
 */
class SZettaPipelineList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaPipelineList) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&, TFunction<void(int64)> InOpen)
	{
		Open = MoveTemp(InOpen);
		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(16, 8, 16, 0)[ZettaUI::ErrorText(MakeAttributeLambda([this]() { return FText::FromString(Error); }))]
			+ SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox) + SScrollBox::Slot().Padding(16)[SAssignNew(Body, SBox)]]
		];
		Body->SetContent(ZettaUI::Hint(LOCTEXT("PlLoadingPipelines", "Loading your pipelines…")));
		Load();
	}

	void Load()
	{
		TWeakPtr<SZettaPipelineList> Weak = SharedThis(this);
		ZettaPipelinesApi::List([Weak](const FZettaResponse& R, const TArray<FZettaPipelineSummary>& List)
		{
			const TSharedPtr<SZettaPipelineList> Self = Weak.Pin();
			if (!Self.IsValid()) return;
			if (!R.bOk)
			{
				Self->Error = ZettaCatalog::Explain(R);
				if (!Self->Pipelines.IsSet())
				{
					Self->Body->SetContent(SNew(SBox).HAlign(HAlign_Left)[ZettaPlUI::SmallButton(LOCTEXT("PlTryAgain", "Try again"),
						FSimpleDelegate::CreateLambda([Weak]() { if (const TSharedPtr<SZettaPipelineList> S = Weak.Pin()) S->Load(); }))]);
				}
				return;
			}
			Self->Error.Reset();
			Self->Pipelines = List;
			Self->Render();
		});
	}

private:
	TSharedRef<SWidget> NewButton()
	{
		return ZettaPlUI::SmallButton(MakeAttributeLambda([this]() { return bCreating ? LOCTEXT("PlCreatingPipeline", "Creating…") : LOCTEXT("PlNewPipeline", "+ New pipeline"); }),
			FSimpleDelegate::CreateLambda([this]() { Create(); }), MakeAttributeLambda([this]() { return !bCreating; }), FText(), true);
	}

	/** A new pipeline is completely empty — no seed placed for you. */
	void Create()
	{
		if (bCreating) return;
		bCreating = true;
		TWeakPtr<SZettaPipelineList> Weak = SharedThis(this);
		const FZettaPipelineGraph Empty;
		ZettaPipelinesApi::Create(TEXT("Untitled pipeline"), &Empty, [Weak](const FZettaResponse& R, const FZettaPipeline& Pipeline)
		{
			const TSharedPtr<SZettaPipelineList> Self = Weak.Pin();
			if (!Self.IsValid()) return;
			Self->bCreating = false;
			if (!R.bOk)
			{
				Self->Error = ZettaCatalog::Explain(R);
				return;
			}
			if (Self->Open) Self->Open(Pipeline.Id);
		});
	}

	TSharedRef<SWidget> RunCounts(const FZettaPipelineSummary& P)
	{
		struct FChip
		{
			EZettaRunTone Tone;
			int32 Count;
			FString Label;
		};
		const TArray<FChip> Chips = {
			{ EZettaRunTone::Running, P.Count(ZettaRunStatus::Running) + P.Count(ZettaRunStatus::Paused), TEXT("running") },
			{ EZettaRunTone::Attention, P.Count(ZettaRunStatus::Awaiting) + P.Count(ZettaRunStatus::Draft), TEXT("need you") },
			{ EZettaRunTone::Done, P.Count(ZettaRunStatus::Done), TEXT("done") },
			{ EZettaRunTone::Failed, P.Count(ZettaRunStatus::Failed) + P.Count(ZettaRunStatus::Cancelled), TEXT("stopped") },
		};
		TSharedRef<SWrapBox> Row = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8, 4));
		for (const FChip& C : Chips)
		{
			if (C.Count <= 0) continue;
			Row->AddSlot()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)[ZettaPlUI::RunDot(C.Tone)]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextBlock).Font(FZettaStyle::Font(8))
					.Text(FText::FromString(FString::Printf(TEXT("%d %s"), C.Count, C.Tone == EZettaRunTone::Attention && C.Count == 1 ? TEXT("needs you") : *C.Label)))
				]
			];
		}
		return Row;
	}

	TSharedRef<SWidget> Card(const FZettaPipelineSummary& P)
	{
		const int64 Id = P.Id;
		const FString Title = P.Title;
		TWeakPtr<SZettaPipelineList> Weak = SharedThis(this);
		return SNew(SBox).WidthOverride(300)
			[
				SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.CardRaised")).Padding(12)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SButton)
						.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
						.ContentPadding(0)
						.OnClicked_Lambda([this, Id]() { if (Open) Open(Id); return FReply::Handled(); })
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 0, 8, 0)
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("◆"))).ColorAndOpacity(FSlateColor(FZettaStyle::Accent())).Font(FZettaStyle::Font(12, true))
							]
							+ SHorizontalBox::Slot().FillWidth(1)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Title)).Font(FZettaStyle::Font(11, true)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
								+ SVerticalBox::Slot().AutoHeight()
								[
									ZettaUI::Hint(FText::FromString(FString::Printf(TEXT("%d step%s%s"), P.Steps, P.Steps == 1 ? TEXT("") : TEXT("s"), P.LastRunId.IsSet() ? TEXT("") : TEXT(" · Not run yet"))))
								]
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)[RunCounts(P)]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()[ZettaPlUI::SmallButton(LOCTEXT("PlOpen", "Open"), FSimpleDelegate::CreateLambda([this, Id]() { if (Open) Open(Id); }))]
						+ SHorizontalBox::Slot().FillWidth(1)
						+ SHorizontalBox::Slot().AutoWidth()
						[
							ZettaPlUI::GhostButton(LOCTEXT("PlDuplicate", "Duplicate"), FSimpleDelegate::CreateLambda([Weak, Id]()
							{
								ZettaPipelinesApi::Duplicate(Id, [Weak](const FZettaResponse& R, const FZettaPipeline&)
								{
									const TSharedPtr<SZettaPipelineList> Self = Weak.Pin();
									if (!Self.IsValid()) return;
									if (!R.bOk) Self->Error = ZettaCatalog::Explain(R);
									Self->Load();
								});
							}), true, FText::FromString(TEXT("Duplicate ") + Title))
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SButton)
							.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
							.ToolTipText_Lambda([this, Id, Title]() { return FText::FromString(Confirming == Id ? TEXT("Press again to delete ") + Title : TEXT("Delete ") + Title); })
							.OnClicked_Lambda([this, Weak, Id]()
							{
								if (Confirming != Id)
								{
									Confirming = Id;
									return FReply::Handled();
								}
								Confirming = 0;
								ZettaPipelinesApi::Delete(Id, [Weak](const FZettaResponse& R)
								{
									const TSharedPtr<SZettaPipelineList> Self = Weak.Pin();
									if (!Self.IsValid()) return;
									if (!R.bOk) Self->Error = ZettaCatalog::Explain(R);
									Self->Load();
								});
								return FReply::Handled();
							})
							[
								SNew(STextBlock)
								.Text_Lambda([this, Id]() { return Confirming == Id ? LOCTEXT("PlDeleteQ", "Delete?") : LOCTEXT("PlDelete", "Delete"); })
								.ColorAndOpacity_Lambda([this, Id]() { return Confirming == Id ? FSlateColor(FZettaStyle::Error()) : FSlateColor::UseForeground(); })
							]
						]
					]
				]
			];
	}

	void Render()
	{
		if (!Pipelines.IsSet()) return;
		if (Pipelines->Num() == 0)
		{
			Body->SetContent(
				SNew(SBox).HAlign(HAlign_Center).Padding(FMargin(0, 60, 0, 0)).MaxDesiredWidth(460)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ZettaUI::Title(LOCTEXT("PlFirst", "Build your first pipeline"))]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
					[
						SNew(STextBlock).AutoWrapText(true).Justification(ETextJustify::Center).ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text(LOCTEXT("PlFirstLead", "Chain the Studio’s tools into one run from a picture or a model — generate, isolate parts, retopologise, unwrap, texture and rig — automatically to the end, or a step at a time."))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6)[NewButton()]
				]);
			return;
		}
		TSharedRef<SWrapBox> Grid = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(12, 12));
		for (const FZettaPipelineSummary& P : Pipelines.GetValue()) Grid->AddSlot()[Card(P)];
		const int32 N = Pipelines->Num();
		Body->SetContent(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ZettaUI::Title(LOCTEXT("PlTitle3D", "3D Pipelines"))]
					+ SVerticalBox::Slot().AutoHeight()[ZettaUI::Hint(FText::FromString(FString::Printf(TEXT("%d pipeline%s · the Studio’s tools, chained"), N, N == 1 ? TEXT("") : TEXT("s"))))]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[NewButton()]
			]
			+ SVerticalBox::Slot().AutoHeight()[Grid]);
	}

	TFunction<void(int64)> Open;
	TOptional<TArray<FZettaPipelineSummary>> Pipelines;
	TSharedPtr<SBox> Body;
	FString Error;
	bool bCreating = false;
	int64 Confirming = 0;
};

/** The page: the list, or one pipeline open — and the layer its pop-ups open on. */
class SZettaPipelinesPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaPipelinesPage) {}
	SLATE_END_ARGS()

	/** The pipeline the page shows (0 = the list), kept for this editor session. */
	static int64 Remembered;

	void Construct(const FArguments&)
	{
		TSharedPtr<SOverlay> Root;
		ChildSlot
		[
			SAssignNew(Root, SOverlay)
			+ SOverlay::Slot()[SAssignNew(Host, SBox)]
		];
		Overlay->Host = Root;

		TWeakPtr<SZettaPipelinesPage> Weak = SharedThis(this);
		OpenHandle = ZettaPipelineHooks::OnOpenRequested().AddLambda([Weak]() { if (const TSharedPtr<SZettaPipelinesPage> Self = Weak.Pin()) Self->TakeRequest(); });
		SavedHandle = ZettaPipelineHooks::OnSavedElsewhere().AddLambda([Weak](int64 PipelineId)
		{
			const TSharedPtr<SZettaPipelinesPage> Self = Weak.Pin();
			if (!Self.IsValid()) return;
			if (Self->Editor.IsValid() && Self->Editor->GetPipelineId() == PipelineId) Self->Editor->ReloadFromServer();
			else if (Self->List.IsValid()) Self->List->Load();
		});
		AuthHandle = FZettaAuth::Get().OnChanged.AddLambda([Weak]()
		{
			const TSharedPtr<SZettaPipelinesPage> Self = Weak.Pin();
			if (Self.IsValid() && FZettaAuth::Get().IsSignedIn() && Self->List.IsValid()) Self->List->Load();
		});
		// Followed runs say when they finish, wherever the user is.
		RunHandle = FZettaRunFollower::Get().OnRunFinished.AddLambda([](const FZettaPipelineRunPtr& Run, const FZettaRunFollower::FFollowed& Entry)
		{
			if (!Entry.AgentSessionId.IsEmpty()) return;
			const FString Imported = Entry.ImportedPaths.Num() > 0 ? FString::Printf(TEXT(" — %d asset%s imported"), Entry.ImportedPaths.Num(), Entry.ImportedPaths.Num() == 1 ? TEXT("") : TEXT("s")) : FString();
			ZettaPlUI::Toast(FString::Printf(TEXT("Pipeline run #%lld: %s%s."), Run->Id, *ZettaRunStatus::Label(Run->Status).ToLower(), *Imported), Run->Status == ZettaRunStatus::Failed);
		});

		if (!TakeRequest())
		{
			if (Remembered != 0) ShowPipeline(Remembered, false, {});
			else ShowList();
		}
	}

	virtual ~SZettaPipelinesPage() override
	{
		ZettaPipelineHooks::OnOpenRequested().Remove(OpenHandle);
		ZettaPipelineHooks::OnSavedElsewhere().Remove(SavedHandle);
		FZettaAuth::Get().OnChanged.Remove(AuthHandle);
		FZettaRunFollower::Get().OnRunFinished.Remove(RunHandle);
	}

private:
	bool TakeRequest()
	{
		const TOptional<ZettaPipelineHooks::FOpenRequest> Request = ZettaPipelineHooks::TakePending();
		if (!Request.IsSet()) return false;
		ShowPipeline(Request->PipelineId, Request->RunId.IsSet(), Request->RunId);
		return true;
	}

	void ShowList()
	{
		Remembered = 0;
		Editor.Reset();
		TWeakPtr<SZettaPipelinesPage> Weak = SharedThis(this);
		SAssignNew(List, SZettaPipelineList, [Weak](int64 Id) { if (const TSharedPtr<SZettaPipelinesPage> Self = Weak.Pin()) Self->ShowPipeline(Id, false, {}); });
		Host->SetContent(List.ToSharedRef());
	}

	void ShowPipeline(int64 PipelineId, bool bRuns, TOptional<int64> RunId)
	{
		if (Editor.IsValid() && Editor->GetPipelineId() == PipelineId)
		{
			Editor->Go(bRuns, RunId);
			return;
		}
		Remembered = PipelineId;
		List.Reset();
		TWeakPtr<SZettaPipelinesPage> Weak = SharedThis(this);
		SAssignNew(Editor, SZettaPipelineEditor, PipelineId, Overlay, [Weak]() { if (const TSharedPtr<SZettaPipelinesPage> Self = Weak.Pin()) Self->ShowList(); });
		Host->SetContent(Editor.ToSharedRef());
		Editor->Load(bRuns, RunId);
	}

	FZettaPlOverlayRef Overlay = MakeShared<FZettaPlOverlay>();
	TSharedPtr<SBox> Host;
	TSharedPtr<SZettaPipelineList> List;
	TSharedPtr<SZettaPipelineEditor> Editor;
	FDelegateHandle OpenHandle, SavedHandle, AuthHandle, RunHandle;
};

int64 SZettaPipelinesPage::Remembered = 0;

TSharedRef<SWidget> ZettaPages::MakePipelinesPage()
{
	// Runs the agent started are reported through the service; hooking it here makes sure it is listening.
	ZettaPipelines::OnRunFinished();
	return SNew(SZettaPipelinesPage);
}

#undef LOCTEXT_NAMESPACE
