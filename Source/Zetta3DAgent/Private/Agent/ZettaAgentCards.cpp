// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaAgentCards.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ContentBrowserModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IContentBrowserSingleton.h"
#include "Misc/MessageDialog.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "ZettaAgentChangeLog.h"
#include "ZettaAgentMarkdown.h"
#include "ZettaAuth.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace ZettaAgentCards
{
	void Toast(const FText& Text, bool bError)
	{
		FNotificationInfo Info(Text);
		Info.ExpireDuration = bError ? 5.f : 3.f;
		Info.bUseSuccessFailIcons = bError;
		TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid() && bError) Item->SetCompletionState(SNotificationItem::CS_Fail);
	}

	TSharedRef<SWidget> Chip(const FText& Text, const FLinearColor& Color)
	{
		return SNew(SBorder)
			.BorderImage(FZettaStyle::Get().GetBrush("Zetta.Chip"))
			.Padding(FMargin(8, 1))
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Text).Font(FZettaStyle::Font(8, true)).ColorAndOpacity(FSlateColor(Color))
			];
	}

	TSharedRef<SWidget> QuietButton(const TAttribute<FText>& Text, FSimpleDelegate OnClicked, const FText& Tooltip, const TAttribute<bool>& Enabled)
	{
		return SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
			.ContentPadding(FMargin(6, 2))
			.ToolTipText(Tooltip)
			.IsEnabled(Enabled)
			.OnClicked_Lambda([OnClicked]() { OnClicked.ExecuteIfBound(); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(Text).Font(FZettaStyle::Font(9))
			];
	}

	TSharedRef<SWidget> MentionRow(const FZettaMention& Mention, const TAttribute<bool>& Highlighted, FSimpleDelegate OnPicked)
	{
		const FText Detail = FText::FromString(Mention.IsActor()
			? FString::Printf(TEXT("%s · %s"), *Mention.Class, *Mention.Path)
			: Mention.Class.IsEmpty() ? Mention.Path : FString::Printf(TEXT("%s · %s"), *Mention.Class, *Mention.Path));
		const FSlateBrush* Icon = Mention.Icon();
		return SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
			.BorderBackgroundColor_Lambda([Highlighted]() { return Highlighted.Get() ? FLinearColor(FZettaStyle::Accent().R, FZettaStyle::Accent().G, FZettaStyle::Accent().B, 0.35f) : FLinearColor::Transparent; })
			.Padding(0)
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
				.IsFocusable(false)
				.ContentPadding(FMargin(8, 3))
				.OnClicked_Lambda([OnPicked]() { OnPicked.ExecuteIfBound(); return FReply::Handled(); })
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
					[
						SNew(SBox).WidthOverride(16).HeightOverride(16)[SNew(SImage).Image(Icon)]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Text(FText::FromString(Mention.Name())).Font(FZettaStyle::Font(9, true))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Text(Detail).Font(FZettaStyle::Font(8)).ColorAndOpacity(FSlateColor(FZettaStyle::Muted()))
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						]
					]
				]
			];
	}

	TSharedRef<SWidget> MentionChip(const FZettaMention& Mention, FSimpleDelegate OnRemove)
	{
		const FText Tip = FText::FromString(Mention.IsActor() ? FString::Printf(TEXT("Actor %s (%s)"), *Mention.Path, *Mention.ObjectName) : Mention.Path);
		return SNew(SBorder)
			.BorderImage(FZettaStyle::Get().GetBrush("Zetta.Chip"))
			.Padding(FMargin(6, 1, 2, 1))
			.ToolTipText(Tip)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
				[
					SNew(SBox).WidthOverride(14).HeightOverride(14)[SNew(SImage).Image(Mention.Icon())]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(Mention.Name())).Font(FZettaStyle::Font(9))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
					.ContentPadding(FMargin(3, 0))
					.ToolTipText(LOCTEXT("AgentUI.RemoveTag", "Remove"))
					.OnClicked_Lambda([OnRemove]() { OnRemove.ExecuteIfBound(); return FReply::Handled(); })
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("✕"))).Font(FZettaStyle::Font(8))
					]
				]
			];
	}

	// ─── Search picker ─────────────────────────────────────────────────────────

	TSharedRef<SWidget> SearchPicker(const FString& Accept, TFunction<void(const FZettaMention&)> OnPicked, const FString& AssetClass)
	{
		TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
		TSharedRef<TFunction<void(const FString&)>> Fill = MakeShared<TFunction<void(const FString&)>>();
		TWeakPtr<SVerticalBox> WeakList = List;
		*Fill = [WeakList, Accept, OnPicked, AssetClass](const FString& Query)
		{
			TSharedPtr<SVerticalBox> Box = WeakList.Pin();
			if (!Box.IsValid()) return;
			Box->ClearChildren();
			TArray<FZettaMention> Found;
			if (Accept != TEXT("asset")) Found.Append(ZettaMentions::SearchLevel(Query, 30));
			if (Accept != TEXT("level"))
			{
				TArray<FZettaMention> Assets = ZettaMentions::SearchProject(Query, AssetClass.IsEmpty() ? 30 : 400);
				if (!AssetClass.IsEmpty()) Assets.RemoveAll([&AssetClass](const FZettaMention& M) { return M.Class != AssetClass; });
				if (Assets.Num() > 30) Assets.SetNum(30);
				Found.Append(Assets);
			}
			if (Found.Num() == 0)
			{
				Box->AddSlot().AutoHeight().Padding(8)[ZettaUI::Hint(LOCTEXT("AgentUI.NothingMatches", "Nothing matches."))];
			}
			for (const FZettaMention& M : Found)
			{
				Box->AddSlot().AutoHeight()[MentionRow(M, false, FSimpleDelegate::CreateLambda([OnPicked, M]()
				{
					OnPicked(M);
					FSlateApplication::Get().DismissAllMenus();
				}))];
			}
		};
		(*Fill)(FString());
		return SNew(SBox)
			.WidthOverride(380)
			.MaxDesiredHeight(360)
			.Padding(6)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
				[
					SNew(SSearchBox)
					.HintText(!AssetClass.IsEmpty() ? LOCTEXT("AgentUI.SearchPictures", "Search the project's textures") : Accept == TEXT("level") ? LOCTEXT("AgentUI.SearchLevel", "Search the level") : Accept == TEXT("asset") ? LOCTEXT("AgentUI.SearchProject", "Search the project") : LOCTEXT("AgentUI.SearchBoth", "Search the level and the project"))
					.OnTextChanged_Lambda([Fill](const FText& Text) { (*Fill)(Text.ToString()); })
				]
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SNew(SScrollBox) + SScrollBox::Slot()[List]
				]
			];
	}

	// ─── Approval ──────────────────────────────────────────────────────────────

	TSharedRef<SWidget> Approval(const FZettaChatItemPtr& Item)
	{
		const bool bGenerate = Item->ApprovalKind == TEXT("generate");
		const bool bDestructive = Item->ApprovalKind == TEXT("destructive");
		const FText Kind = bGenerate ? LOCTEXT("AgentUI.SpendCredits", "Spend credits?")
			: !Item->Code.IsEmpty() ? LOCTEXT("AgentUI.RunCode", "Run this code?")
			: bDestructive ? LOCTEXT("AgentUI.RemoveThis", "Remove this?")
			: LOCTEXT("AgentUI.AllowChange", "Allow this change?");

		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, 0, 0, 6)
		[
			Chip(Kind, bDestructive ? FZettaStyle::Error() : FZettaStyle::Warning())
		];
		Box->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
		[
			SNew(STextBlock).Text(FText::FromString(Item->Text)).Font(FZettaStyle::Font(11, true)).AutoWrapText(true)
		];
		if (Item->Reasons.Num() > 0)
		{
			Box->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
			[
				SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FSlateColor(FZettaStyle::Warning()))
				.Text(FText::Format(LOCTEXT("AgentUI.WhyLook", "Why this needs a look: {0}"), FText::FromString(FString::Join(Item->Reasons, TEXT("; ")))))
			];
		}
		if (!Item->Detail.IsEmpty())
		{
			Box->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[ZettaAgentMarkdown::Plain(Item->Detail, TEXT("Md.Muted"))];
		}
		if (!Item->Code.IsEmpty())
		{
			Box->AddSlot().AutoHeight().Padding(0, 2, 0, 4)
			[
				SNew(SBox).MaxDesiredHeight(280)
				[
					SNew(SScrollBox) + SScrollBox::Slot()[ZettaAgentMarkdown::Code(Item->Code, Item->ToolName == TEXT("run_python") ? TEXT("python") : FString())]
				]
			];
		}
		if (!Item->PreviewError.IsEmpty())
		{
			Box->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[ZettaUI::ErrorText(FText::FromString(Item->PreviewError))];
		}
		if (Item->Credits.IsSet())
		{
			const int64 Have = FZettaAuth::Get().GetUser().Credits3D.Total();
			Box->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
			[
				SNew(STextBlock).Font(FZettaStyle::Font(10, true))
				.Text(FText::Format(LOCTEXT("AgentUI.CreditsHave", "{0} credits · you have {1}"), FText::AsNumber(Item->Credits.GetValue()), FText::AsNumber(Have)))
			];
		}

		if (Item->bResolved)
		{
			const FText Outcome = Item->Status == TEXT("done") ? LOCTEXT("AgentUI.Allowed", "Allowed")
				: Item->Status == TEXT("stopped") ? LOCTEXT("AgentUI.ApprovalStopped", "Stopped") : LOCTEXT("AgentUI.Declined", "Declined");
			Box->AddSlot().AutoHeight()[ZettaUI::Hint(Outcome)];
		}
		else
		{
			TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
			if (Item->PreviewError.IsEmpty())
			{
				Buttons->AddSlot().AutoWidth().Padding(0, 0, 6, 0)
				[
					ZettaUI::PrimaryButton(FText::FromString(Item->ConfirmLabel), FSimpleDelegate::CreateLambda([Item]() { FZettaAgentRuntime::Get().Resolve(Item, true); }))
				];
				Buttons->AddSlot().AutoWidth()
				[
					ZettaUI::Button(LOCTEXT("AgentUI.Decline", "Decline"), FSimpleDelegate::CreateLambda([Item]() { FZettaAgentRuntime::Get().Resolve(Item, false); }))
				];
			}
			else
			{
				Buttons->AddSlot().AutoWidth()
				[
					ZettaUI::Button(LOCTEXT("AgentUI.Skip", "Skip"), FSimpleDelegate::CreateLambda([Item]() { FZettaAgentRuntime::Get().Resolve(Item, false); }))
				];
			}
			Box->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[Buttons];
		}
		return SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.CardRaised")).Padding(12)[Box];
	}

	// ─── Question (ask_options) ────────────────────────────────────────────────

	TSharedRef<SWidget> Question(const FZettaChatItemPtr& Item)
	{
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		Box->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[ZettaAgentMarkdown::Plain(Item->Text)];
		for (const FString& Option : Item->Options)
		{
			const bool bChosen = Item->Detail == Option;
			Box->AddSlot().AutoHeight().Padding(0, 1)
			[
				SNew(SButton)
				.ButtonStyle(bChosen ? &FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton") : &FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
				.HAlign(HAlign_Left)
				.ContentPadding(FMargin(10, 4))
				.IsEnabled(!Item->bResolved)
				.OnClicked_Lambda([Item, Option]() { FZettaAgentRuntime::Get().Answer(Item, Option); return FReply::Handled(); })
				[
					SNew(STextBlock).Text(FText::FromString(Option)).AutoWrapText(true)
				]
			];
		}
		return SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.CardRaised")).Padding(12)[Box];
	}
}

// ─── ask_user ─────────────────────────────────────────────────────────────────

namespace
{
	/** A slot an actor or asset is dropped into (or picked, or taken from the selection). */
	class SZettaAskSlot : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SZettaAskSlot) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FZettaChatItemPtr& InItem, int32 InIndex)
		{
			Item = InItem;
			Index = InIndex;
			const FZettaAskQuestion Q = Question();
			ChildSlot
			[
				SNew(SBorder)
				.BorderImage(FZettaStyle::Get().GetBrush("Zetta.Thumb"))
				.BorderBackgroundColor_Lambda([this]() { return bDragOver ? FZettaStyle::Accent() : FLinearColor::White; })
				.Padding(8)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ZettaUI::Hint(ZettaAgentAsk::SlotHint(Q))]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[SAssignNew(Chips, SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4, 4))]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
						[
							ZettaAgentCards::QuietButton(LOCTEXT("AgentUI.UseSelection", "Use selection"), FSimpleDelegate::CreateSP(this, &SZettaAskSlot::UseSelection),
								LOCTEXT("AgentUI.UseSelectionTip", "Put what is selected in the Outliner or the Content Browser here"))
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SComboButton)
							.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
							.HasDownArrow(false)
							.ToolTipText(LOCTEXT("AgentUI.PickTip", "Search the level or the project"))
							.ButtonContent()[SNew(STextBlock).Text(LOCTEXT("AgentUI.Pick", "Pick…")).Font(FZettaStyle::Font(9))]
							.OnGetMenuContent_Lambda([this]()
							{
								TWeakPtr<SZettaAskSlot> Weak = SharedThis(this);
								return ZettaAgentCards::SearchPicker(Question().Accept, [Weak](const FZettaMention& M)
								{
									if (TSharedPtr<SZettaAskSlot> Self = Weak.Pin()) Self->Take({ M });
								});
							})
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
					[
						ZettaUI::ErrorText(MakeAttributeLambda([this]() { return FText::FromString(Error); }))
					]
				]
			];
			Draw();
		}

		virtual void OnDragEnter(const FGeometry&, const FDragDropEvent& Event) override { bDragOver = ZettaMentions::DragHasObjects(Event); }
		virtual void OnDragLeave(const FDragDropEvent&) override { bDragOver = false; }
		virtual FReply OnDragOver(const FGeometry&, const FDragDropEvent& Event) override
		{
			return ZettaMentions::DragHasObjects(Event) ? FReply::Handled() : FReply::Unhandled();
		}
		virtual FReply OnDrop(const FGeometry&, const FDragDropEvent& Event) override
		{
			bDragOver = false;
			const TArray<FZettaMention> Dropped = ZettaMentions::FromDrag(Event);
			if (Dropped.Num() == 0) return FReply::Unhandled();
			Take(Dropped);
			return FReply::Handled();
		}

	private:
		const FZettaAskQuestion& Question() const { return Item->Ask->Questions[Index]; }
		FZettaAskAnswer& Answer() { return Item->Answers[Index]; }

		void UseSelection()
		{
			const TArray<FZettaMention> Selected = ZettaMentions::FromSelection();
			if (Selected.Num() == 0)
			{
				Error = LOCTEXT("AgentUI.NothingSelected", "Nothing is selected.").ToString();
				return;
			}
			Take(Selected);
		}

		/** Takes what fits; says why the rest did not. */
		void Take(const TArray<FZettaMention>& Offered)
		{
			const FZettaAskQuestion& Q = Question();
			FString Why;
			bool bTook = false;
			for (const FZettaMention& M : Offered)
			{
				FString Reason;
				if (!ZettaAgentAsk::Fits(Q, M, Reason))
				{
					if (Why.IsEmpty()) Why = FString::Printf(TEXT("“%s”: %s"), *M.Name(), *Reason);
					continue;
				}
				if (!Q.bMultiple) Answer().Objects.Reset();
				if (!Answer().Objects.ContainsByPredicate([&M](const FZettaMention& X) { return X.SameAs(M); })) Answer().Objects.Add(M);
				bTook = true;
				if (!Q.bMultiple) break;
			}
			Error = bTook ? FString() : Why;
			Draw();
		}

		void Draw()
		{
			Chips->ClearChildren();
			const TArray<FZettaMention> Objects = Answer().Objects;
			for (int32 i = 0; i < Objects.Num(); ++i)
			{
				const FZettaMention M = Objects[i];
				Chips->AddSlot()
				[
					ZettaAgentCards::MentionChip(M, FSimpleDelegate::CreateLambda([this, M]()
					{
						Answer().Objects.RemoveAll([&M](const FZettaMention& X) { return X.SameAs(M); });
						Draw();
					}))
				];
			}
		}

		FZettaChatItemPtr Item;
		int32 Index = 0;
		TSharedPtr<SWrapBox> Chips;
		FString Error;
		bool bDragOver = false;
	};

	/** The ask_user card: a slot, options or a box per question, and OK to let the agent carry on. */
	class SZettaAskCard : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SZettaAskCard) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FZettaChatItemPtr& InItem)
		{
			Item = InItem;
			TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
			const FText Status = !Item->bResolved ? LOCTEXT("AgentUI.NeedsInput", "Needs your input")
				: Item->Status == TEXT("answered") ? LOCTEXT("AgentUI.Answered", "Answered")
				: Item->Status == TEXT("skipped") ? LOCTEXT("AgentUI.Skipped", "Skipped")
				: Item->Status == TEXT("stopped") ? LOCTEXT("AgentUI.AskStopped", "Stopped")
				: LOCTEXT("AgentUI.Superseded", "Answered in the chat");
			Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, 0, 0, 6)
			[
				ZettaAgentCards::Chip(Status, Item->bResolved ? FZettaStyle::Muted() : FZettaStyle::Accent())
			];
			if (!Item->Text.IsEmpty())
			{
				Box->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
				[
					SNew(STextBlock).Text(FText::FromString(Item->Text)).Font(FZettaStyle::Font(11, true)).AutoWrapText(true)
				];
			}
			if (!Item->Ask.IsValid())
			{
				ChildSlot[SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.CardRaised")).Padding(12)[Box]];
				return;
			}

			const TArray<FZettaAskQuestion>& Questions = Item->Ask->Questions;
			for (int32 i = 0; i < Questions.Num() && i < Item->Answers.Num(); ++i)
			{
				Box->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[Item->bResolved ? Summary(i) : QuestionWidget(i)];
			}
			if (!Item->bResolved)
			{
				FZettaChatItemPtr Card = Item;
				Box->AddSlot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
					[
						ZettaUI::PrimaryButton(LOCTEXT("AgentUI.Ok", "OK"), FSimpleDelegate::CreateLambda([Card]() { FZettaAgentRuntime::Get().SubmitAnswers(Card, false); }),
							MakeAttributeLambda([Card]()
							{
								for (int32 i = 0; i < Card->Ask->Questions.Num() && i < Card->Answers.Num(); ++i)
								{
									if (!Card->Ask->Questions[i].bOptional && Card->Answers[i].IsEmpty()) return false;
								}
								return true;
							}))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						ZettaUI::Button(LOCTEXT("AgentUI.SkipAsk", "Skip"), FSimpleDelegate::CreateLambda([Card]() { FZettaAgentRuntime::Get().SubmitAnswers(Card, true); }),
							true, LOCTEXT("AgentUI.SkipAskTip", "Let the agent carry on without an answer"))
					]
				];
			}
			ChildSlot[SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.CardRaised")).Padding(12)[Box]];
		}

		// Drops anywhere on the card belong to its slots, never to the message box behind it.
		virtual FReply OnDragOver(const FGeometry&, const FDragDropEvent&) override { return FReply::Handled(); }
		virtual FReply OnDrop(const FGeometry&, const FDragDropEvent&) override { return FReply::Handled(); }

	private:
		TSharedRef<SWidget> Summary(int32 Index) const
		{
			const FZettaAskQuestion& Q = Item->Ask->Questions[Index];
			TArray<FString> Parts;
			if (Item->Status == TEXT("answered"))
			{
				const FZettaAskAnswer& A = Item->Answers[Index];
				for (const FZettaMention& M : A.Objects) Parts.Add(TEXT("@") + M.Name());
				Parts.Append(A.Choices);
				if (!A.Text.TrimStartAndEnd().IsEmpty()) Parts.Add(TEXT("“") + A.Text.TrimStartAndEnd() + TEXT("”"));
			}
			return SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ZettaUI::Hint(FText::FromString(Q.Question))]
				+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(Parts.Num() == 0 ? TEXT("—") : FString::Join(Parts, TEXT(", "))))];
		}

		TSharedRef<SWidget> TextBox(int32 Index, const FString& Hint)
		{
			FZettaChatItemPtr Card = Item;
			return SNew(SBox).MinDesiredHeight(44).MaxDesiredHeight(140)
				[
					SNew(SMultiLineEditableTextBox)
					.Text(FText::FromString(Item->Answers[Index].Text))
					.HintText(FText::FromString(Hint))
					.AutoWrapText(true)
					.OnTextChanged_Lambda([Card, Index](const FText& Text)
					{
						FZettaAskAnswer& A = Card->Answers[Index];
						A.Text = Text.ToString();
						// Their own words instead of an option, when only one answer is taken.
						const FZettaAskQuestion& Q = Card->Ask->Questions[Index];
						if (Q.Kind == TEXT("choice") && !Q.bMultiple && !A.Text.TrimStartAndEnd().IsEmpty()) A.Choices.Reset();
					})
				];
		}

		/** "Something else…" / "Describe it instead": a link that opens a box for the user's own words. */
		TSharedRef<SWidget> Other(int32 Index, const FText& Link, const FString& Hint)
		{
			FZettaChatItemPtr Card = Item;
			return SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
				[
					ZettaAgentCards::QuietButton(Link, FSimpleDelegate::CreateLambda([Card, Index]()
					{
						FZettaAskAnswer& A = Card->Answers[Index];
						A.bOtherOpen = !A.bOtherOpen || !A.Text.IsEmpty();
					}))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox)
					.Visibility_Lambda([Card, Index]()
					{
						const FZettaAskAnswer& A = Card->Answers[Index];
						return A.bOtherOpen || !A.Text.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
					})
					[
						TextBox(Index, Hint)
					]
				];
		}

		TSharedRef<SWidget> QuestionWidget(int32 Index)
		{
			const FZettaAskQuestion& Q = Item->Ask->Questions[Index];
			TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
			FString Label = Q.Question;
			if (Q.bOptional) Label += TEXT("  (optional)");
			Box->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
			[
				SNew(STextBlock).Text(FText::FromString(Label)).Font(FZettaStyle::Font(10, true)).AutoWrapText(true)
			];
			if (Q.Kind == TEXT("object"))
			{
				Box->AddSlot().AutoHeight()[SNew(SZettaAskSlot, Item, Index)];
				if (Q.bAllowOther)
				{
					Box->AddSlot().AutoHeight().Padding(0, 2, 0, 0)
					[
						Other(Index, LOCTEXT("AgentUI.DescribeInstead", "Describe it instead"), LOCTEXT("AgentUI.SayWhich", "Or say which one in your own words").ToString())
					];
				}
			}
			else if (Q.Kind == TEXT("choice"))
			{
				FZettaChatItemPtr Card = Item;
				for (const FZettaAskOption& Option : Q.Options)
				{
					const FString OptionLabel = Option.Label;
					const bool bMultiple = Q.bMultiple;
					Box->AddSlot().AutoHeight().Padding(0, 1)
					[
						SNew(SCheckBox)
						.Style(bMultiple ? &FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("Checkbox") : &FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("RadioButton"))
						.IsChecked_Lambda([Card, Index, OptionLabel]()
						{
							return Card->Answers[Index].Choices.Contains(OptionLabel) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([Card, Index, OptionLabel, bMultiple](ECheckBoxState)
						{
							FZettaAskAnswer& A = Card->Answers[Index];
							if (bMultiple)
							{
								if (A.Choices.Contains(OptionLabel)) A.Choices.Remove(OptionLabel);
								else A.Choices.Add(OptionLabel);
							}
							else
							{
								// One answer: a pick replaces the last one, and their own words with it.
								A.Choices = { OptionLabel };
								A.Text.Reset();
								A.bOtherOpen = false;
							}
						})
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Option.Label)).AutoWrapText(true)]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(FText::FromString(Option.Description)).AutoWrapText(true).Font(FZettaStyle::Font(8))
								.ColorAndOpacity(FSlateColor(FZettaStyle::Muted()))
								.Visibility(Option.Description.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
							]
						]
					];
				}
				if (Q.bAllowOther)
				{
					Box->AddSlot().AutoHeight().Padding(0, 2, 0, 0)
					[
						Other(Index, LOCTEXT("AgentUI.SomethingElse", "Something else…"), LOCTEXT("AgentUI.DescribeWant", "Describe what you want").ToString())
					];
				}
			}
			else
			{
				Box->AddSlot().AutoHeight()[TextBox(Index, Q.Placeholder)];
			}
			return Box;
		}

		FZettaChatItemPtr Item;
	};
}

namespace ZettaAgentCards
{
	TSharedRef<SWidget> Ask(const FZettaChatItemPtr& Item)
	{
		return SNew(SZettaAskCard, Item);
	}
}

// ─── Changes ──────────────────────────────────────────────────────────────────

namespace
{
	/** What a message changed — each file, each asset, the level — with Keep and Undo; redrawn when anything is undone. */
	class SZettaChangesCard : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SZettaChangesCard) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FString& InTurn)
		{
			Turn = InTurn;
			ChildSlot[SAssignNew(Body, SBox)];
			Handle = FZettaAgentChangeLog::Get().OnChanged.AddSP(this, &SZettaChangesCard::Draw);
			Draw();
		}

		virtual ~SZettaChangesCard() override
		{
			FZettaAgentChangeLog::Get().OnChanged.Remove(Handle);
		}

	private:
		static FText ChangeLabel(const FZettaChangeRow& Row)
		{
			if (Row.Kind == TEXT("level")) return LOCTEXT("AgentUI.ChgLevel", "Level");
			if (Row.Change == TEXT("created")) return LOCTEXT("AgentUI.ChgNew", "New");
			if (Row.Change == TEXT("deleted")) return LOCTEXT("AgentUI.ChgDeleted", "Deleted");
			if (Row.Change == TEXT("moved")) return LOCTEXT("AgentUI.ChgMoved", "Moved");
			return LOCTEXT("AgentUI.ChgEdited", "Edited");
		}

		static FLinearColor ChangeColor(const FZettaChangeRow& Row)
		{
			if (Row.Kind == TEXT("level") || Row.Change == TEXT("moved")) return FZettaStyle::Accent();
			if (Row.Change == TEXT("created")) return FZettaStyle::Success();
			if (Row.Change == TEXT("deleted")) return FZettaStyle::Error();
			return FZettaStyle::Warning();
		}

		static FText UndoTip(const FZettaChangeRow& Row)
		{
			if (Row.Kind == TEXT("level")) return LOCTEXT("AgentUI.UndoLevelTip", "Undo the agent's steps in the level");
			if (Row.Change == TEXT("created")) return Row.Kind == TEXT("file") ? LOCTEXT("AgentUI.UndoNewFileTip", "Remove it (a copy is kept in Saved/Zetta3D)") : LOCTEXT("AgentUI.UndoNewAssetTip", "Delete it");
			if (Row.Change == TEXT("deleted")) return LOCTEXT("AgentUI.UndoDeletedTip", "Put it back");
			if (Row.Change == TEXT("moved")) return LOCTEXT("AgentUI.UndoMovedTip", "Move it back");
			return LOCTEXT("AgentUI.UndoEditedTip", "Put back the version from before this message");
		}

		bool CheckIdle() const
		{
			if (!FZettaAgentRuntime::Get().IsBusy()) return true;
			ZettaAgentCards::Toast(LOCTEXT("AgentUI.WaitForAgent", "Wait for the agent to finish, or stop it first."));
			return false;
		}

		void UndoRow(int32 Index)
		{
			if (!CheckIdle()) return;
			Problem = FZettaAgentChangeLog::Get().UndoRow(Turn, Index);
			Draw();
		}

		void UndoAll(bool bIncludeKept)
		{
			if (!CheckIdle()) return;
			const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::OkCancel,
				LOCTEXT("AgentUI.UndoAllBody", "Everything this message changed that you haven't kept goes back to how it was before it: files and assets are restored, new ones are removed, and its steps in the level are undone."),
				LOCTEXT("AgentUI.UndoAllTitle", "Undo these changes?"));
			if (Answer != EAppReturnType::Ok) return;
			Problem = FZettaAgentChangeLog::Get().UndoAll(Turn, bIncludeKept);
			Draw();
		}

		static void Show(const FZettaChangeRow& Row)
		{
			if (Row.Kind == TEXT("file"))
			{
				FPlatformProcess::LaunchFileInDefaultExternalApplication(*Row.DiskPath);
				return;
			}
			const FString Where = Row.Change == TEXT("moved") && !Row.bUndone && !Row.MovedTo.IsEmpty() ? Row.MovedTo : Row.Path;
			TArray<FAssetData> Assets;
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().GetAssetsByPackageName(FName(*Where), Assets, true);
			if (Assets.Num() == 0) return;
			FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().SyncBrowserToAssets(Assets);
		}

		void Draw()
		{
			TArray<FZettaChangeRow>* Rows = FZettaAgentChangeLog::Get().Rows(Turn);
			if (!Rows || Rows->Num() == 0)
			{
				Body->SetContent(SNullWidget::NullWidget);
				return;
			}
			int32 Files = 0, Open = 0, Kept = 0, Undone = 0;
			bool bLevel = false;
			for (const FZettaChangeRow& Row : *Rows)
			{
				if (Row.Kind == TEXT("level")) bLevel = true;
				else ++Files;
				if (!Row.IsSettled()) ++Open;
				if (Row.bKept && !Row.bUndone) ++Kept;
				if (Row.bUndone) ++Undone;
			}
			const FText What = Files == 1 ? LOCTEXT("AgentUI.OneFile", "1 file") : FText::Format(LOCTEXT("AgentUI.NFiles", "{0} files"), Files);
			const FText Title = Files == 0 ? LOCTEXT("AgentUI.ChangedLevel", "Changed the level")
				: bLevel ? FText::Format(LOCTEXT("AgentUI.ChangedFilesLevel", "Changed {0} and the level"), What)
				: FText::Format(LOCTEXT("AgentUI.ChangedFiles", "Changed {0}"), What);

			TSharedRef<SHorizontalBox> Head = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(Title).Font(FZettaStyle::Font(10, true))
				];
			if (Open > 0)
			{
				const bool bRest = Open < Rows->Num();
				Head->AddSlot().AutoWidth().VAlign(VAlign_Center)
				[
					ZettaAgentCards::QuietButton(bRest ? LOCTEXT("AgentUI.KeepRest", "Keep the rest") : LOCTEXT("AgentUI.KeepAll", "Keep all"),
						FSimpleDelegate::CreateLambda([this]() { FZettaAgentChangeLog::Get().KeepAll(Turn); }),
						LOCTEXT("AgentUI.KeepAllTip", "Keep what this message changed — what happens anyway if you leave it"))
				];
				Head->AddSlot().AutoWidth().VAlign(VAlign_Center)
				[
					ZettaAgentCards::QuietButton(bRest ? LOCTEXT("AgentUI.UndoRest", "Undo the rest") : LOCTEXT("AgentUI.UndoAll", "Undo all"),
						FSimpleDelegate::CreateLambda([this]() { UndoAll(false); }),
						LOCTEXT("AgentUI.UndoAllTip", "Put back what you haven't kept or undone yet"))
				];
			}
			else
			{
				const FText Settled = Undone == 0 ? LOCTEXT("AgentUI.AllKept", "All kept") : Kept == 0 ? LOCTEXT("AgentUI.AllUndone", "All undone")
					: FText::Format(LOCTEXT("AgentUI.KeptUndone", "{0} kept · {1} undone"), Kept, Undone);
				Head->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)[ZettaUI::Hint(Settled)];
				// Kept is not final: what was kept can still all be put back.
				if (Kept > 0)
				{
					Head->AddSlot().AutoWidth().VAlign(VAlign_Center)
					[
						ZettaAgentCards::QuietButton(LOCTEXT("AgentUI.UndoAll", "Undo all"), FSimpleDelegate::CreateLambda([this]() { UndoAll(true); }),
							LOCTEXT("AgentUI.UndoAllKeptTip", "Put back everything this message changed, kept or not"))
					];
				}
			}

			TSharedRef<SVerticalBox> Box = SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[Head];
			for (int32 i = 0; i < Rows->Num(); ++i)
			{
				const FZettaChangeRow& Row = (*Rows)[i];
				FText Path = FText::FromString(Row.Path);
				FText Tip = FText::FromString(Row.Change == TEXT("moved") && !Row.MovedTo.IsEmpty() ? Row.Path + TEXT(" → ") + Row.MovedTo : Row.Path);
				if (Row.Kind == TEXT("level"))
				{
					Path = FText::Format(LOCTEXT("AgentUI.LevelSteps", "The agent's steps in the level ({0})"), Row.Transactions.Num());
					Tip = FText::FromString(FString::Join(Row.TransactionTitles, TEXT("\n")));
				}
				TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
					[
						ZettaAgentCards::Chip(ChangeLabel(Row), ChangeColor(Row))
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(Path).ToolTipText(Tip).OverflowPolicy(ETextOverflowPolicy::MiddleEllipsis)
						.ColorAndOpacity(Row.bUndone ? FSlateColor(FZettaStyle::Muted()) : FSlateColor::UseForeground())
					];
				if (Row.Kind != TEXT("level") && !(Row.Change == TEXT("created") && Row.bUndone) && !(Row.Change == TEXT("deleted") && !Row.bUndone))
				{
					const FZettaChangeRow Copy = Row;
					Line->AddSlot().AutoWidth().VAlign(VAlign_Center)
					[
						ZettaAgentCards::QuietButton(Row.Kind == TEXT("file") ? LOCTEXT("AgentUI.OpenFile", "Open") : LOCTEXT("AgentUI.ShowAsset", "Show"),
							FSimpleDelegate::CreateLambda([Copy]() { Show(Copy); }),
							Row.Kind == TEXT("file") ? LOCTEXT("AgentUI.OpenFileTip", "Open it in its app") : LOCTEXT("AgentUI.ShowAssetTip", "Find it in the Content Browser"))
					];
				}
				if (Row.bUndone)
				{
					Line->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)[ZettaUI::Hint(LOCTEXT("AgentUI.RowUndone", "Undone"))];
				}
				else
				{
					if (Row.bKept)
					{
						Line->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)[ZettaUI::Hint(LOCTEXT("AgentUI.RowKept", "Kept"))];
					}
					else
					{
						Line->AddSlot().AutoWidth().VAlign(VAlign_Center)
						[
							ZettaAgentCards::QuietButton(LOCTEXT("AgentUI.Keep", "Keep"), FSimpleDelegate::CreateLambda([this, i]() { FZettaAgentChangeLog::Get().KeepRow(Turn, i); }),
								LOCTEXT("AgentUI.KeepTip", "Keep this change — what happens anyway if you leave it"))
						];
					}
					Line->AddSlot().AutoWidth().VAlign(VAlign_Center)
					[
						ZettaAgentCards::QuietButton(LOCTEXT("AgentUI.Undo", "Undo"), FSimpleDelegate::CreateLambda([this, i]() { UndoRow(i); }), UndoTip(Row))
					];
				}
				Box->AddSlot().AutoHeight().Padding(0, 1)[Line];
			}
			if (!Problem.IsEmpty())
			{
				Box->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[ZettaUI::ErrorText(FText::FromString(Problem))];
			}
			Body->SetContent(SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Card")).Padding(10)[Box]);
		}

		FString Turn;
		FString Problem;
		TSharedPtr<SBox> Body;
		FDelegateHandle Handle;
	};
}

namespace ZettaAgentCards
{
	TSharedRef<SWidget> Changes(const FString& Turn)
	{
		return SNew(SZettaChangesCard, Turn);
	}
}

#undef LOCTEXT_NAMESPACE
