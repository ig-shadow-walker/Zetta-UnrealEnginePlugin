// Copyright Zetta 3D. See LICENSE.md.

#include "SZettaPipelineEditor.h"

#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "SZettaPipelineCanvas.h"
#include "Zetta3DEditorModule.h"
#include "ZettaCatalog.h"
#include "ZettaNodeForms.h"
#include "ZettaPipelineJson.h"
#include "ZettaPipelineServices.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

using namespace ZettaPJson;

// ─── Palette ────────────────────────────────────────────────────────────────────────────────────────────────────────

/** One step in the palette: click it to add it after the selected step, or drag it onto the canvas. */
class SZettaPaletteItem : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaPaletteItem) {}
		SLATE_ATTRIBUTE(bool, Disabled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FZettaNodeSpec& Spec, TFunction<void(const FString&)> InOnAdd)
	{
		Kind = Spec.Kind;
		Label = Spec.Label;
		OnAdd = MoveTemp(InOnAdd);
		Disabled = InArgs._Disabled;
		const FText Blurb = FText::FromString(Spec.Blurb);
		ChildSlot
		[
			SNew(SBorder)
			.BorderImage_Lambda([this]() { return IsHovered() && !Disabled.Get() ? FZettaStyle::Get().GetBrush("Zetta.CardRaised") : FAppStyle::GetNoBrush(); })
			.Padding(FMargin(6, 4))
			.ColorAndOpacity_Lambda([this]() { return FLinearColor(1, 1, 1, Disabled.Get() ? 0.4f : 1.f); })
			.ToolTipText_Lambda([this, Blurb]() { return Disabled.Get() ? LOCTEXT("PlOneSeed", "A pipeline starts from one seed.") : Blurb; })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[ZettaPlUI::HueIcon(Kind)]
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Label))]
			]
		];
	}

	virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent) override
	{
		if (Disabled.Get() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
		bPressed = true;
		return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
	}

	virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& MouseEvent) override
	{
		if (!bPressed || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
		bPressed = false;
		if (OnAdd) OnAdd(Kind);
		return FReply::Handled();
	}

	virtual FReply OnDragDetected(const FGeometry&, const FPointerEvent&) override
	{
		bPressed = false;
		return FReply::Handled().BeginDragDrop(FZettaStepDragOp::New(Kind, Label));
	}

private:
	FString Kind;
	FString Label;
	TFunction<void(const FString&)> OnAdd;
	TAttribute<bool> Disabled;
	bool bPressed = false;
};

/** THE STEPS TO BUILD WITH, in the groups Seed, Picture, Make, Refine — only what Unreal offers. */
static TSharedRef<SWidget> MakePalette(TFunction<void(const FString&)> OnAdd, TFunction<bool()> HasSeed)
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	for (const TPair<EZettaNodeGroup, FString>& Group : ZettaNodeCatalog::Groups())
	{
		TArray<const FZettaNodeSpec*> Specs;
		for (const FZettaNodeSpec* S : ZettaNodeCatalog::OfferedHere()) if (S->Group == Group.Key) Specs.Add(S);
		// A group with nothing on offer (Output) is not shown at all.
		if (Specs.Num() == 0) continue;
		Box->AddSlot().AutoHeight().Padding(4, 10, 4, 4)[ZettaPlUI::Heading(FText::FromString(Group.Value))];
		const bool bSeedGroup = Group.Key == EZettaNodeGroup::Seed;
		for (const FZettaNodeSpec* S : Specs)
		{
			Box->AddSlot().AutoHeight()
			[
				SNew(SZettaPaletteItem, *S, OnAdd).Disabled_Lambda([HasSeed, bSeedGroup]() { return bSeedGroup && HasSeed(); })
			];
		}
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(10, 10, 10, 0)[SNew(STextBlock).Text(LOCTEXT("PlSteps", "Steps")).Font(FZettaStyle::Font(10, true))]
		+ SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox) + SScrollBox::Slot().Padding(6, 0)[Box]];
}

// ─── Describe it ────────────────────────────────────────────────────────────────────────────────────────────────────

/**
 * DESCRIBE IT, GET THE RECIPE — a box over the canvas: write what the pipeline should do and the recipe is built on the
 * canvas. With a recipe there, it edits it ("add AI texturing at the end"). Either way it is one change, so Undo brings
 * back what was there.
 */
class SZettaDescribeRecipe : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaDescribeRecipe) {}
	SLATE_END_ARGS()

	TFunction<const FZettaPipelineGraph&()> Current;
	TFunction<const FZettaEntitlements3D*()> Gates;
	TFunction<void(const FZettaPipelineDraft&)> OnDrafted;

	void Construct(const FArguments&)
	{
		ChildSlot
		[
			SNew(SBox).WidthOverride(560)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.CardRaised")).Padding(FMargin(8, 6))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 4, 8, 0)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("✦"))).ColorAndOpacity(FSlateColor(FZettaStyle::Accent()))
						]
						+ SHorizontalBox::Slot().FillWidth(1)
						[
							SNew(SBox).MaxDesiredHeight(120)
							[
								SAssignNew(Input, SZettaPlText, nullptr, nullptr)
								.MultiLine(true).MaxLength(2000)
								.IsEnabled_Lambda([this]() { return !bBusy; })
								.Hint_Lambda([this]()
								{
									return Editing() ? LOCTEXT("PlDescribeEdit", "Change the recipe — e.g. add AI texturing at the end, or use quads")
										: LOCTEXT("PlDescribeNew", "Describe the recipe — e.g. a game-ready character from a photo, rigged for Unreal");
								})
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(6, 0, 0, 0)
						[
							SNew(SButton)
							.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
							.ToolTipText(LOCTEXT("PlDescribeSend", "Build the recipe (Enter)"))
							.IsEnabled_Lambda([this]() { return !bBusy && Input.IsValid() && !Input->GetText().TrimStartAndEnd().IsEmpty(); })
							.OnClicked_Lambda([this]() { Submit(); return FReply::Handled(); })
							[
								SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(bBusy ? TEXT("…") : TEXT("↑")); })
							]
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(4, 4, 4, 0)
				[
					SNew(STextBlock).AutoWrapText(true)
					.Text_Lambda([this]() { return FText::FromString(Line); })
					.ColorAndOpacity_Lambda([this]() { return bLineError ? FSlateColor(FZettaStyle::Error()) : FSlateColor::UseSubduedForeground(); })
					.Visibility_Lambda([this]() { return Line.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				]
			]
		];
		Input->OnEnter = [this]() { Submit(); };
	}

private:
	/** Steps beyond a seed: something to edit rather than a blank canvas. */
	bool Editing() const
	{
		if (!Current) return false;
		for (const FZettaPipelineNode& N : Current().Nodes) if (!ZettaNodeCatalog::IsSeed(N.Kind)) return true;
		return false;
	}

	void Say(const FString& Text, bool bError = false)
	{
		Line = Text;
		bLineError = bError;
	}

	void Submit()
	{
		const FString Description = Input->GetText().TrimStartAndEnd();
		if (bBusy || Description.IsEmpty()) return;
		if (Description.Len() < 3)
		{
			Say(TEXT("Say a little more about what the recipe should do."), true);
			return;
		}
		const bool bEditing = Editing();
		bBusy = true;
		Say(bEditing ? TEXT("Changing your recipe…") : TEXT("Designing your recipe…"));
		const FZettaPipelineGraph CurrentGraph = Current();
		TWeakPtr<SZettaDescribeRecipe> Weak = SharedThis(this);
		ZettaPipelinesApi::Draft(Description, bEditing ? &CurrentGraph : nullptr, Gates ? Gates() : nullptr, [Weak](const FZettaResponse& R, const FZettaPipelineDraft& Draft)
		{
			const TSharedPtr<SZettaDescribeRecipe> Self = Weak.Pin();
			if (!Self.IsValid()) return;
			Self->bBusy = false;
			if (!R.bOk)
			{
				Self->Say(R.Status == 429 ? FString(TEXT("That's a lot of recipes in a minute — wait a moment and try again.")) : ZettaCatalog::Explain(R), true);
				return;
			}
			// The server names only what each step changes; the Studio's defaults fill the rest.
			FZettaPipelineDraft Normalized = Draft;
			Normalized.Graph = ZettaRequestBuilder::Normalize(Draft.Graph).Graph;
			Self->Input->SetText(FString());
			const FString Summary = Draft.Summary.TrimStartAndEnd();
			Self->Say(Summary.IsEmpty() ? FString(TEXT("Done. Undo brings back what was there.")) : Summary + TEXT(" Undo brings back what was there."));
			if (Self->OnDrafted) Self->OnDrafted(Normalized);
		});
	}

	TSharedPtr<SZettaPlText> Input;
	FString Line;
	bool bLineError = false;
	bool bBusy = false;
};

// ─── Editor ─────────────────────────────────────────────────────────────────────────────────────────────────────────

const FZettaEntitlements3D* SZettaPipelineEditor::Gates() const { return FZettaPipelinePlan::Get().Gates(); }
const FZettaPriceList* SZettaPipelineEditor::Prices() const { return FZettaPipelinePlan::Get().Prices(); }

void SZettaPipelineEditor::Construct(const FArguments&, int64 InPipelineId, const FZettaPlOverlayRef& InOverlay, TFunction<void()> InBack)
{
	PipelineId = InPipelineId;
	Overlay = InOverlay;
	Back = MoveTemp(InBack);
	LoadingText = TEXT("Loading the pipeline…");

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[MakeBar()]
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 0, 12, 4)
		[
			SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FSlateColor(FZettaStyle::Warning()))
			.Visibility_Lambda([this]() { return !bRunsView && bLoaded && Problems.Num() > 0 && GetGraph().Nodes.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
			.Text_Lambda([this]()
			{
				if (Problems.Num() == 0) return FText();
				return FText::FromString(FString::Printf(TEXT("%d thing%s to finish before it can run — %s"), Problems.Num(), Problems.Num() == 1 ? TEXT("") : TEXT("s"), *Problems[0].Message));
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 0)[SAssignNew(Notes, SVerticalBox)]
		+ SVerticalBox::Slot().FillHeight(1)
		[
			SAssignNew(Switcher, SWidgetSwitcher)
			+ SWidgetSwitcher::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(LoadingText); }).ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SWidgetSwitcher::Slot()[MakeRecipe()]
			+ SWidgetSwitcher::Slot()[SAssignNew(RunsView, SZettaRunsView, SharedThis(this))]
		]
	];

	TWeakPtr<SZettaPipelineEditor> Weak = SharedThis(this);
	Stack.OnChanged.AddLambda([Weak]() { if (const TSharedPtr<SZettaPipelineEditor> Self = Weak.Pin()) Self->Render(); });
	PlanHandle = FZettaPipelinePlan::Get().OnChanged.AddLambda([Weak]() { if (const TSharedPtr<SZettaPipelineEditor> Self = Weak.Pin()) Self->Render(); });
	RunsHandle = FZettaRunFollower::Get().OnRunsUpdated.AddLambda([Weak](int64 Id, const TArray<FZettaRunSummary>& List)
	{
		const TSharedPtr<SZettaPipelineEditor> Self = Weak.Pin();
		if (Self.IsValid() && Id == Self->PipelineId) Self->SetRuns(List);
	});
	if (FSlateApplication::IsInitialized())
	{
		ActivationHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddLambda([Weak](bool bActive)
		{
			if (const TSharedPtr<SZettaPipelineEditor> Self = Weak.Pin()) Self->OnAppActivated(bActive);
		});
	}
	FZettaPipelinePlan::Get().EnsureLoaded();
}

SZettaPipelineEditor::~SZettaPipelineEditor()
{
	FZettaPipelinePlan::Get().OnChanged.Remove(PlanHandle);
	FZettaRunFollower::Get().OnRunsUpdated.Remove(RunsHandle);
	if (FSlateApplication::IsInitialized()) FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationHandle);
	// Leaving the editor never loses a change: what is pending is saved now (the call outlives the widget).
	if (bDirty && bLoaded) ZettaPipelinesApi::Save(PipelineId, Stack.Present(), Title, Gates(), nullptr);
}

TSharedRef<SWidget> SZettaPipelineEditor::MakeBar()
{
	const bool bMac = PLATFORM_MAC != 0;
	auto Tab = [this](const FText& Label, bool bRuns)
	{
		return SNew(SCheckBox)
			.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox"))
			.IsChecked_Lambda([this, bRuns]() { return bRunsView == bRuns ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, bRuns](ECheckBoxState)
			{
				Go(bRuns, bRuns && RunsView.IsValid() && bRunsView ? RunsView->OpenRunId() : TOptional<int64>());
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(10, 3).VAlign(VAlign_Center)[SNew(STextBlock).Text(Label)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
				[
					SNew(STextBlock).Font(FZettaStyle::Font(8)).ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Visibility(bRuns ? EVisibility::Visible : EVisibility::Collapsed)
					.Text_Lambda([this]() { return Runs.IsSet() && Runs->Num() > 0 ? FText::AsNumber(Runs->Num()) : FText(); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
				[
					SNew(SBox).Visibility_Lambda([this, bRuns]()
					{
						if (!bRuns || !Runs.IsSet()) return EVisibility::Collapsed;
						for (const FZettaRunSummary& R : Runs.GetValue()) if (R.Status == ZettaRunStatus::Running || R.Status == ZettaRunStatus::Paused) return EVisibility::Visible;
						return EVisibility::Collapsed;
					})[ZettaPlUI::RunDot(EZettaRunTone::Running, LOCTEXT("PlRunningDot", "Running"))]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[
					SNew(SBox).Visibility_Lambda([this, bRuns]()
					{
						if (!bRuns || !Runs.IsSet()) return EVisibility::Collapsed;
						for (const FZettaRunSummary& R : Runs.GetValue()) if (R.Status == ZettaRunStatus::Awaiting || R.Status == ZettaRunStatus::Draft) return EVisibility::Visible;
						return EVisibility::Collapsed;
					})[ZettaPlUI::RunDot(EZettaRunTone::Attention, LOCTEXT("PlNeedsYouDot", "Needs you"))]
				]
			];
	};

	return SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Card")).Padding(FMargin(8, 6))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				ZettaPlUI::GhostButton(LOCTEXT("PlBackToList", "‹ Pipelines"), FSimpleDelegate::CreateLambda([this]() { if (Back) Back(); }))
			]
			+ SHorizontalBox::Slot().FillWidth(0.5f).MaxWidth(320).VAlign(VAlign_Center).Padding(6, 0)
			[
				SAssignNew(TitleField, SZettaPlText, [this]() { return Title; }, [this](const FString& Text)
				{
					Title = Text;
					MarkDirty();
				}).MaxLength(200).Hint(LOCTEXT("PlPipelineName", "Pipeline name"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[Tab(LOCTEXT("PlRecipeTab", "Recipe"), false)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Tab(LOCTEXT("PlRunsTab", "Runs"), true)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]() { return bRunsView ? EVisibility::Collapsed : EVisibility::Visible; })
				+ SHorizontalBox::Slot().AutoWidth()
				[
					ZettaPlUI::GhostButton(FText::FromString(TEXT("↶")), FSimpleDelegate::CreateLambda([this]() { Undo(); }), MakeAttributeLambda([this]() { return Stack.CanUndo(); }),
						bMac ? LOCTEXT("PlUndoMac", "Undo (Cmd+Z)") : LOCTEXT("PlUndo", "Undo (Ctrl+Z)"))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					ZettaPlUI::GhostButton(FText::FromString(TEXT("↷")), FSimpleDelegate::CreateLambda([this]() { Redo(); }), MakeAttributeLambda([this]() { return Stack.CanRedo(); }),
						bMac ? LOCTEXT("PlRedoMac", "Redo (Shift+Cmd+Z)") : LOCTEXT("PlRedo", "Redo (Ctrl+Y)"))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)
				[
					SNew(STextBlock).Font(FZettaStyle::Font(8))
					.ColorAndOpacity_Lambda([this]() { return bSaveFailed && !bSaving ? FSlateColor(FZettaStyle::Error()) : FSlateColor::UseSubduedForeground(); })
					.Text_Lambda([this]()
					{
						if (!bLoaded) return FText();
						if (bSaving || bDirty) return LOCTEXT("PlSaving", "Saving…");
						return bSaveFailed ? LOCTEXT("PlNotSaved", "Not saved — retrying on the next change") : LOCTEXT("PlSaved", "Saved");
					})
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0)
			[
				SNew(STextBlock).Font(FZettaStyle::Font(9, true))
				.Text_Lambda([this]() { return FText::FromString(Estimate.Label()); })
				.ToolTipText_Lambda([this]() { return Estimate.bPerPart ? LOCTEXT("PlPerPartTip", "Counting every part at Max parts") : FText(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				ZettaPlUI::SmallButton(LOCTEXT("PlNewRun", "+ New run…"), FSimpleDelegate::CreateLambda([this]() { OpenRunDialog(); }),
					MakeAttributeLambda([this]() { return bLoaded && GetGraph().Seed() != nullptr && Problems.Num() == 0; }), FText(), true)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
			[
				ZettaPlUI::GhostButton(LOCTEXT("PlOpenWeb", "Open on web"), FSimpleDelegate::CreateLambda([this]()
				{
					ZettaUI::OpenSite(FString::Printf(TEXT("/pipelines?pipeline=%lld"), PipelineId));
				}), true, LOCTEXT("PlOpenWebTip", "Open this pipeline on zetta3d.ai"))
			]
		];
}

TSharedRef<SWidget> SZettaPipelineEditor::MakeRecipe()
{
	TWeakPtr<SZettaPipelineEditor> Weak = SharedThis(this);
	TSharedRef<SZettaDescribeRecipe> Describe = SNew(SZettaDescribeRecipe);
	Describe->Current = [this]() -> const FZettaPipelineGraph& { return Stack.Present(); };
	Describe->Gates = [this]() { return Gates(); };
	Describe->OnDrafted = [Weak](const FZettaPipelineDraft& Draft) { if (const TSharedPtr<SZettaPipelineEditor> Self = Weak.Pin()) Self->ApplyDraft(Draft); };

	SAssignNew(Canvas, SZettaPipelineCanvas).Overlay()[Describe];
	Canvas->OnSelectionChanged = [this](const FString& Id)
	{
		SelectedNode = Id;
		if (Inspector.IsValid()) Inspector->Show(Stack.Present(), SelectedNode, Problems, Runs.IsSet() ? Runs->Num() : 0);
	};
	Canvas->OnConnect = [this](const FString& S, const FString& SH, const FString& T, const FString& TH)
	{
		FZettaPipelineGraph Next;
		FString Error;
		if (ZettaGraphEditing::Connect(Stack.Present(), S, SH, T, TH, Gates(), Next, Error)) Commit(Next);
		else ZettaPlUI::Toast(Error);
	};
	Canvas->OnReconnect = [this](const FString& Edge, const FString& S, const FString& SH, const FString& T, const FString& TH)
	{
		FZettaPipelineGraph Next;
		FString Error;
		if (ZettaGraphEditing::Reconnect(Stack.Present(), Edge, S, SH, T, TH, Gates(), Next, Error)) Commit(Next);
		else ZettaPlUI::Toast(Error);
	};
	Canvas->OnRemove = [this](const TArray<FString>& Nodes, const TArray<FString>& Edges) { RemoveItems(Nodes, Edges); };
	Canvas->OnRefused = [](const FString& Why) { ZettaPlUI::Toast(Why); };
	Canvas->OnMove = [this](const TMap<FString, FVector2D>& Moved)
	{
		// One drag, one undo.
		TArray<FString> Ids;
		Moved.GetKeys(Ids);
		Ids.Sort();
		Commit(ZettaGraphEditing::Move(Stack.Present(), Moved), TEXT("move:") + FString::Join(Ids, TEXT(",")));
	};
	Canvas->OnDropKind = [this](const FString& Kind, const FVector2D& At) { AddStep(Kind, At); };

	SAssignNew(Inspector, SZettaNodeInspector, Overlay);
	Inspector->OnPatch = [this](const FString& Id, const TSharedRef<FJsonObject>& P) { Patch(Id, P); };
	Inspector->OnLabel = [this](const FString& Id, const FString& Label)
	{
		// Typing a name is one undo, not one per key.
		Commit(ZettaGraphEditing::SetLabel(Stack.Present(), Id, Label), TEXT("settings:") + Id);
	};
	Inspector->OnRemove = [this](const FString& Id) { RemoveItems({ Id }, {}); };
	Inspector->OnShowRuns = [this]() { Go(true); };

	return SNew(SSplitter)
		+ SSplitter::Slot().Value(0.16f)
		[
			SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Card")).Padding(0)
			[
				MakePalette([Weak](const FString& Kind) { if (const TSharedPtr<SZettaPipelineEditor> Self = Weak.Pin()) Self->AddStep(Kind, {}); },
					[Weak]() { const TSharedPtr<SZettaPipelineEditor> Self = Weak.Pin(); return Self.IsValid() && Self->GetGraph().Seed() != nullptr; })
			]
		]
		+ SSplitter::Slot().Value(0.56f)[Canvas.ToSharedRef()]
		+ SSplitter::Slot().Value(0.28f)
		[
			SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Card")).Padding(0)[Inspector.ToSharedRef()]
		];
}

// ─── Loading and saving ─────────────────────────────────────────────────────────────────────────────────────────────

static bool GWebWarningShown = false;

void SZettaPipelineEditor::Load(bool bRuns, TOptional<int64> RunId)
{
	TWeakPtr<SZettaPipelineEditor> Weak = SharedThis(this);
	ZettaPipelinesApi::Get(PipelineId, [Weak, bRuns, RunId](const FZettaResponse& R, const FZettaPipeline& Pipeline)
	{
		const TSharedPtr<SZettaPipelineEditor> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		if (!R.bOk)
		{
			Self->LoadingText = R.Status == 404 ? TEXT("This pipeline could not be opened — it may have been deleted.") : ZettaCatalog::Explain(R);
			return;
		}
		Self->Apply(Pipeline);
		Self->bLoaded = true;
		if (!GWebWarningShown)
		{
			GWebWarningShown = true;
			Self->ShowNote(TEXT("Saved as you go. Editing the same pipeline on zetta3d.ai at the same time overwrites — the last save wins."));
		}
		if (Self->PendingView.IsSet()) Self->Go(Self->PendingView->Key, Self->PendingView->Value);
		else Self->Go(bRuns, RunId);
		Self->PendingView.Reset();
		Self->RefreshRuns();
	});
}

/** The pipeline as saved: normalised as the web editor loads it; a graph without requests is saved straight back. */
void SZettaPipelineEditor::Apply(const FZettaPipeline& Pipeline)
{
	const ZettaRequestBuilder::FNormalized Normalized = ZettaRequestBuilder::Normalize(Pipeline.Graph);
	Title = Pipeline.Title;
	if (TitleField.IsValid()) TitleField->SetText(Title);
	bDirty = false;
	SaveAt = 0;
	if (Canvas.IsValid()) Canvas->Reframe();
	Stack.Load(Normalized.Graph);
	// An agent-drafted graph holds settings only: built and saved on open, so the server can price and check it.
	if (Normalized.bNeedsRequestRebuild)
	{
		bDirty = true;
		Save();
	}
}

void SZettaPipelineEditor::MarkDirty()
{
	bDirty = true;
	SaveAt = FPlatformTime::Seconds() + 0.8;
}

void SZettaPipelineEditor::Flush()
{
	if (bDirty) Save();
}

void SZettaPipelineEditor::MarkSavedByBatch()
{
	bDirty = false;
	SaveAt = 0;
}

void SZettaPipelineEditor::Save()
{
	if (!bDirty || !bLoaded) return;
	if (bSaving)
	{
		// One save at a time; the change waits for the next window.
		SaveAt = FPlatformTime::Seconds() + 0.8;
		return;
	}
	bDirty = false;
	bSaving = true;
	SaveAt = 0;
	TWeakPtr<SZettaPipelineEditor> Weak = SharedThis(this);
	ZettaPipelinesApi::Save(PipelineId, Stack.Present(), Title, Gates(), [Weak](const FZettaResponse& R, const FZettaPipeline&)
	{
		const TSharedPtr<SZettaPipelineEditor> Self = Weak.Pin();
		if (!R.bOk) UE_LOG(LogZetta, Warning, TEXT("The pipeline could not be saved: %s"), *ZettaCatalog::Explain(R));
		if (!Self.IsValid()) return;
		Self->bSaving = false;
		Self->bSaveFailed = !R.bOk;
	});
}

void SZettaPipelineEditor::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (bDirty && SaveAt > 0 && FPlatformTime::Seconds() >= SaveAt) Save();
}

void SZettaPipelineEditor::OnAppActivated(bool bActive)
{
	// Back in the editor: if nothing is waiting to be saved, read the pipeline again (the web may have changed it).
	if (!bActive || !bLoaded || HasUnsavedChanges()) return;
	TWeakPtr<SZettaPipelineEditor> Weak = SharedThis(this);
	ZettaPipelinesApi::Get(PipelineId, [Weak](const FZettaResponse& R, const FZettaPipeline& Pipeline)
	{
		const TSharedPtr<SZettaPipelineEditor> Self = Weak.Pin();
		if (!Self.IsValid() || !R.bOk || Self->HasUnsavedChanges()) return;
		const FZettaPipelineGraph Server = ZettaRequestBuilder::Normalize(Pipeline.Graph).Graph;
		const bool bSame = JEquals(ZettaRequestBuilder::WithRequests(Server, Self->Gates()).ToJson(), ZettaRequestBuilder::WithRequests(Self->Stack.Present(), Self->Gates()).ToJson());
		if (bSame && Pipeline.Title == Self->Title) return;
		Self->Apply(Pipeline);
	});
}

void SZettaPipelineEditor::ReloadFromServer()
{
	const bool bHadEdits = HasUnsavedChanges();
	bDirty = false;
	SaveAt = 0;
	TWeakPtr<SZettaPipelineEditor> Weak = SharedThis(this);
	ZettaPipelinesApi::Get(PipelineId, [Weak, bHadEdits](const FZettaResponse& R, const FZettaPipeline& Pipeline)
	{
		const TSharedPtr<SZettaPipelineEditor> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		if (!R.bOk)
		{
			UE_LOG(LogZetta, Warning, TEXT("The pipeline the Agent changed could not be read again: %s"), *ZettaCatalog::Explain(R));
			return;
		}
		Self->Apply(Pipeline);
		Self->bLoaded = true;
		Self->ShowNote(bHadEdits ? TEXT("Updated by the Agent — it replaced a change not saved yet.") : TEXT("Updated by the Agent."), TEXT("signal"));
	});
}

void SZettaPipelineEditor::ShowNote(const FString& Text, const FString& Tone)
{
	if (!Notes.IsValid()) return;
	TSharedRef<SBox> Holder = SNew(SBox).Padding(FMargin(0, 0, 0, 4));
	TWeakPtr<SBox> WeakHolder = Holder;
	TWeakPtr<SVerticalBox> WeakNotes = Notes;
	Holder->SetContent(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1)[ZettaPlUI::Note(FText::FromString(Text), Tone)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
		[
			ZettaPlUI::GhostButton(FText::FromString(TEXT("✕")), FSimpleDelegate::CreateLambda([WeakHolder, WeakNotes]()
			{
				const TSharedPtr<SVerticalBox> N = WeakNotes.Pin();
				const TSharedPtr<SBox> H = WeakHolder.Pin();
				if (N.IsValid() && H.IsValid()) N->RemoveSlot(H.ToSharedRef());
			}))
		]);
	Notes->AddSlot().AutoHeight()[Holder];
}

// ─── Edits ──────────────────────────────────────────────────────────────────────────────────────────────────────────

void SZettaPipelineEditor::Commit(const FZettaPipelineGraph& Next, const FString& Coalesce)
{
	Stack.Commit(Next, Coalesce);
	MarkDirty();
}

void SZettaPipelineEditor::Undo()
{
	if (Stack.Undo()) MarkDirty();
}

void SZettaPipelineEditor::Redo()
{
	if (Stack.Redo()) MarkDirty();
}

FReply SZettaPipelineEditor::OnKeyDown(const FGeometry&, const FKeyEvent& InKeyEvent)
{
	// A text field keeps its own undo (the words, not the pipeline): this sees only what no text field took.
	const bool bAction = PLATFORM_MAC ? InKeyEvent.IsCommandDown() : InKeyEvent.IsControlDown();
	if (bRunsView || !bAction || InKeyEvent.IsAltDown()) return FReply::Unhandled();
	if (InKeyEvent.GetKey() == EKeys::Z)
	{
		if (InKeyEvent.IsShiftDown()) Redo();
		else Undo();
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey() == EKeys::Y)
	{
		Redo();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SZettaPipelineEditor::AddStep(const FString& Kind, const TOptional<FVector2D>& At)
{
	const FZettaPipelineGraph& Graph = Stack.Present();
	if (ZettaNodeCatalog::IsSeed(Kind) && Graph.Seed()) return;
	// Dropped where it was dropped; clicked in after the selected step and wired to it.
	const FString After = !At.IsSet() && Graph.Node(SelectedNode) ? SelectedNode : FString();
	FZettaPipelineGraph Next;
	FString Id, Error;
	if (!ZettaGraphEditing::AddStep(Graph, Kind, After, FString(), nullptr, At, Gates(), Next, Id, Error))
	{
		ZettaPlUI::Toast(Error);
		return;
	}
	SelectedNode = Id;
	Commit(Next);
	if (Canvas.IsValid()) Canvas->Select(Id);
	Render();
}

void SZettaPipelineEditor::Patch(const FString& NodeId, const TSharedRef<FJsonObject>& Change)
{
	const FZettaPipelineNode* Node = Stack.Present().Node(NodeId);
	if (!Node) return;
	const TSharedRef<FJsonObject> Current = Node->Settings.IsValid() ? JClone(Node->Settings) : ZettaNodeSettings::Defaults(Node->Kind);
	TSharedRef<FJsonObject> Settings = Current;
	if (ZettaNodeCatalog::IsGenerate(Node->Kind)) Settings = ZettaGenerateForm::ApplyPatch(Current, Change);
	else JMerge(Settings, Change);
	FZettaPipelineGraph Next;
	FString Error;
	if (!ZettaGraphEditing::SetSettings(Stack.Present(), NodeId, Settings, Gates(), Next, Error))
	{
		ZettaPlUI::Toast(Error);
		return;
	}
	// Typing in one step's settings is one undo, not one per key.
	Commit(Next, TEXT("settings:") + NodeId);
}

void SZettaPipelineEditor::RemoveItems(const TArray<FString>& Nodes, const TArray<FString>& Edges)
{
	if (Nodes.Contains(SelectedNode)) SelectedNode.Reset();
	FZettaPipelineGraph Next = Stack.Present();
	for (const FString& Id : Nodes) Next = ZettaGraphEditing::Remove(Next, Id);
	for (const FString& Id : Edges) Next = ZettaGraphEditing::Disconnect(Next, Id, Gates());
	Commit(Next);
}

void SZettaPipelineEditor::LearnParts(const FZettaPipelineRun& Run)
{
	if (!bLoaded) return;
	FZettaPipelineGraph Next = Stack.Present();
	bool bChanged = false;
	for (const FZettaPipelineRunStep& Step : Run.Steps)
	{
		if (!Step.Output.IsSet() || !Step.Output->IsIsolated()) continue;
		FZettaPipelineNode* Node = Next.Node(Step.NodeId);
		if (!Node) continue;
		const TArray<TPair<FString, FString>> Known = ZettaNodeCatalog::KnownParts(*Node);
		TArray<TSharedPtr<FJsonValue>> List;
		for (const TPair<FString, FString>& K : Known)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("key"), K.Key);
			O->SetStringField(TEXT("label"), K.Value);
			List.Add(JObj(O));
		}
		bool bFresh = false;
		for (const FZettaIsolatedPart& Part : Step.Output->Parts)
		{
			if (Part.Key.IsEmpty() || Known.ContainsByPredicate([&Part](const TPair<FString, FString>& K) { return K.Key == Part.Key; })) continue;
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("key"), Part.Key);
			O->SetStringField(TEXT("label"), Part.Label.IsEmpty() ? Part.Key : Part.Label);
			List.Add(JObj(O));
			bFresh = true;
		}
		if (!bFresh) continue;
		if (!Node->Settings.IsValid()) Node->Settings = ZettaNodeSettings::Defaults(Node->Kind);
		Node->Settings->SetArrayField(TEXT("knownParts"), List);
		bChanged = true;
	}
	if (!bChanged) return;
	Stack.Replace(Next);
	MarkDirty();
}

void SZettaPipelineEditor::ApplyDraft(const FZettaPipelineDraft& Draft)
{
	TSet<FString> Before;
	for (const FZettaPipelineNode& N : Stack.Present().Nodes) Before.Add(N.Id);
	SelectedNode.Reset();
	// An untitled pipeline takes the recipe's title.
	if ((Title.TrimStartAndEnd().IsEmpty() || Title == TEXT("Untitled pipeline")) && !Draft.Title.TrimStartAndEnd().IsEmpty())
	{
		Title = Draft.Title.TrimStartAndEnd();
		if (TitleField.IsValid()) TitleField->SetText(Title);
	}
	Commit(Draft.Graph);
	// Only what is new assembles itself; kept steps stay where they were.
	TArray<const FZettaPipelineNode*> Added;
	for (const FZettaPipelineNode& N : Draft.Graph.Nodes) if (!Before.Contains(N.Id)) Added.Add(&N);
	Added.StableSort([](const FZettaPipelineNode& A, const FZettaPipelineNode& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
	TArray<FString> Ids;
	for (const FZettaPipelineNode* N : Added) Ids.Add(N->Id);
	if (Canvas.IsValid()) Canvas->Assemble(Ids);
}

// ─── Views and drawing ──────────────────────────────────────────────────────────────────────────────────────────────

void SZettaPipelineEditor::Go(bool bRuns, TOptional<int64> RunId)
{
	if (!bLoaded)
	{
		// Asked for while loading (a deep link): applied once it has loaded.
		PendingView = TPair<bool, TOptional<int64>>(bRuns, RunId);
		return;
	}
	bRunsView = bRuns;
	Switcher->SetActiveWidgetIndex(bRuns ? 2 : 1);
	if (bRuns)
	{
		if (Runs.IsSet()) RunsView->SetRuns(Runs.GetValue());
		RunsView->OpenRun(RunId);
		RefreshRuns();
	}
	Render();
}

void SZettaPipelineEditor::Render()
{
	if (!bLoaded) return;
	const FZettaPipelineGraph& Graph = Stack.Present();
	if (!SelectedNode.IsEmpty() && !Graph.Node(SelectedNode)) SelectedNode.Reset();
	Problems = ZettaGraphRules::Problems(Graph, Gates());
	Estimate = ZettaGraphRules::Estimate(Graph, Prices(), Gates());
	FanOut = ZettaGraphRules::FanOutNodes(Graph);
	if (bRunsView || !Canvas.IsValid()) return;

	bool bAnyEach = false;
	for (const FZettaPipelineEdge& E : Graph.Edges) bAnyEach |= E.SourceHandle == TEXT("each");
	const TArray<FZettaPipelineProblem> ProblemsNow = Problems;
	const TSet<FString> FanOutNow = FanOut;
	const FZettaEntitlements3D* GatesNow = Gates();
	const FZettaPriceList* PricesNow = Prices();
	Canvas->Show(Graph, [&Graph, ProblemsNow, FanOutNow, GatesNow, PricesNow, bAnyEach](const FZettaPipelineNode& Node)
	{
		FZettaStepCardData D;
		D.Summary = ZettaNodeSettings::Summary(Node);
		D.Price = ZettaRequestBuilder::PriceFor(Node, PricesNow, { ZettaGraphRules::IncomingType(Graph, Node.Id), GatesNow });
		D.bPerPart = bAnyEach && FanOutNow.Contains(Node.Id);
		for (const FZettaPipelineProblem& P : ProblemsNow) if (P.NodeId == Node.Id) D.Problems.Add(P.Message);
		D.Warnings = ZettaGraphRules::Warnings(Graph, Node.Id);
		D.bPerRun = ZettaNodeSettings::PerRunIn(Graph, Node);
		return D;
	});
	Canvas->Select(SelectedNode);
	if (Inspector.IsValid()) Inspector->Show(Graph, SelectedNode, Problems, Runs.IsSet() ? Runs->Num() : 0);
}

// ─── Runs ───────────────────────────────────────────────────────────────────────────────────────────────────────────

void SZettaPipelineEditor::RefreshRuns()
{
	TWeakPtr<SZettaPipelineEditor> Weak = SharedThis(this);
	const int64 Id = PipelineId;
	ZettaPipelinesApi::Runs(Id, [Weak, Id](const FZettaResponse& R, const TArray<FZettaRunSummary>& List)
	{
		if (!R.bOk) return;
		bool bLive = false;
		for (const FZettaRunSummary& S : List) bLive |= S.IsLive();
		if (bLive) FZettaRunFollower::Get().FollowRuns(Id);
		if (const TSharedPtr<SZettaPipelineEditor> Self = Weak.Pin()) Self->SetRuns(List);
	});
}

void SZettaPipelineEditor::SetRuns(const TArray<FZettaRunSummary>& List)
{
	Runs = List;
	if (RunsView.IsValid()) RunsView->SetRuns(List);
	if (!bRunsView && SelectedNode.IsEmpty() && Inspector.IsValid()) Inspector->Show(Stack.Present(), SelectedNode, Problems, List.Num());
}

void SZettaPipelineEditor::OpenRunDialog()
{
	if (!bLoaded) return;
	Flush();
	TWeakPtr<SZettaPipelineEditor> Weak = SharedThis(this);
	ZettaRunDialog::Show(SharedThis(this), [Weak](const TArray<FZettaRunSummary>& Created, bool bImport)
	{
		const TSharedPtr<SZettaPipelineEditor> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		// The graph went with the batch (and was saved onto the pipeline): nothing is waiting any more.
		Self->MarkSavedByBatch();
		if (bImport)
		{
			for (const FZettaRunSummary& Run : Created) FZettaRunFollower::Get().Follow(Run.Id, Self->PipelineId, FString(), EZettaRunImport::FinalSteps, FString());
		}
		Self->RefreshRuns();
		Self->Go(true, Created.Num() > 0 ? TOptional<int64>(Created[0].Id) : TOptional<int64>());
	});
}

#undef LOCTEXT_NAMESPACE
