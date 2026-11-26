// Copyright Zetta 3D. See LICENSE.md.

#include "SZettaAgentPage.h"
#include "HAL/PlatformApplicationMisc.h"

#include "ContentBrowserModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "IContentBrowserSingleton.h"
#include "Input/DragAndDrop.h"
#include "Misc/Base64.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Zetta3DEditorModule.h"
#include "ZettaAgentCards.h"
#include "ZettaAgentCatalog.h"
#include "ZettaAgentMarkdown.h"
#include "ZettaAuth.h"
#include "ZettaCatalog.h"
#include "ZettaHttp.h"
#include "ZettaImageCache.h"
#include "ZettaImporter.h"
#include "ZettaJson.h"
#include "ZettaSettings.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"
#include "SZettaAgentSettings.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

/**
 * A chat message with Copy under it (Unity 0.7.7). The row is there while the pointer is over the message — hidden, not
 * collapsed, so the transcript doesn't jump — and for a moment after a copy, while it says "Copied". It copies the words
 * as written: a reply's Markdown included, so it pastes with its code blocks and lists intact.
 */
class SZettaCopyableMessage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaCopyableMessage) : _bUser(false) {}
		SLATE_ARGUMENT(FString, Text)
		SLATE_ARGUMENT(bool, bUser)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Text = InArgs._Text;
		const bool bUser = InArgs._bUser;
		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[InArgs._Content.Widget]
			+ SVerticalBox::Slot().AutoHeight().HAlign(bUser ? HAlign_Right : HAlign_Left)
			[
				SNew(SBox).HeightOverride(20)
				.Visibility_Lambda([this]() { return IsHovered() || bCopied ? EVisibility::Visible : EVisibility::Hidden; })
				[
					SNew(SButton)
					.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
					.ContentPadding(FMargin(6, 1))
					.ToolTipText(bUser ? LOCTEXT("AgentUI.CopyMineTip", "Copy your message") : LOCTEXT("AgentUI.CopyReplyTip", "Copy the reply"))
					.OnClicked(this, &SZettaCopyableMessage::Copy)
					[
						SNew(STextBlock)
						.Font(FZettaStyle::Font(8))
						.ColorAndOpacity(FSlateColor(FZettaStyle::Muted()))
						.Text_Lambda([this]() { return bCopied ? LOCTEXT("AgentUI.Copied", "Copied") : LOCTEXT("AgentUI.CopyMessage", "Copy"); })
					]
				]
			]
		];
	}

private:
	FReply Copy()
	{
		FPlatformApplicationMisc::ClipboardCopy(*Text);
		bCopied = true;
		if (ResetTimer.IsValid()) UnRegisterActiveTimer(ResetTimer.ToSharedRef());
		ResetTimer = RegisterActiveTimer(1.5f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
		{
			bCopied = false;
			ResetTimer.Reset();
			return EActiveTimerReturnType::Stop;
		}));
		return FReply::Handled();
	}

	FString Text;
	bool bCopied = false;
	TSharedPtr<FActiveTimerHandle> ResetTimer;
};

TArray<FZettaMention> SZettaAgentPage::PendingMentions;
bool SZettaAgentPage::bPendingSettings = false;

namespace
{
	/** The server takes at most 16 MiB a request: pictures, PDFs and inlined text stay under ~14 MB of it. */
	constexpr int32 PageMaxAttachedChars = 14000000;

	TArray<FString> PageSplitLines(const FString& Text)
	{
		TArray<FString> Lines;
		int32 Start = 0;
		for (int32 i = 0; i < Text.Len(); ++i)
		{
			if (Text[i] == TEXT('\n'))
			{
				Lines.Add(Text.Mid(Start, i - Start));
				Start = i + 1;
			}
		}
		Lines.Add(Text.Mid(Start));
		for (FString& Line : Lines) Line.RemoveFromEnd(TEXT("\r"));
		return Lines;
	}

	FString PageProjectId() { return UZettaProjectSettings::Get()->EnsureProjectId(); }

	FString PageQuery(const FString& Key, const FString& Value)
	{
		return FString::Printf(TEXT("?%s=%s"), *Key, *FGenericPlatformHttp::UrlEncode(Value));
	}

	/** A server time, in the user's own clock. */
	FDateTime PageLocalTime(const FString& Iso)
	{
		FDateTime Utc;
		if (!FDateTime::ParseIso8601(*Iso, Utc)) return FDateTime(0);
		return Utc + (FDateTime::Now() - FDateTime::UtcNow());
	}

	FText PageGroupOf(const FDateTime& When)
	{
		if (When.GetTicks() == 0) return LOCTEXT("AgentUI.Earlier", "Earlier");
		const int32 Days = (FDateTime::Now().GetDate() - When.GetDate()).GetDays();
		if (Days < 1) return LOCTEXT("AgentUI.Today", "Today");
		if (Days < 2) return LOCTEXT("AgentUI.Yesterday", "Yesterday");
		if (Days < 7) return LOCTEXT("AgentUI.ThisWeek", "This week");
		return LOCTEXT("AgentUI.Earlier", "Earlier");
	}

	FString PageTitleOf(const TSharedPtr<FJsonObject>& Session)
	{
		const FString Title = ZettaJson::Str(Session, TEXT("title")).TrimStartAndEnd();
		return Title.IsEmpty() ? FString::Printf(TEXT("Conversation #%lld"), ZettaJson::Int(Session, TEXT("id"))) : Title;
	}

	/** A picture in the chat, at its own aspect, at most 320 px on a side, with a placeholder until it loads. */
	class SZettaChatImage : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SZettaChatImage) {}
			SLATE_ARGUMENT(FString, Url)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Url = InArgs._Url;
			ChildSlot
			[
				SNew(SBox)
				.WidthOverride_Lambda([this]() { return FOptionalSize(Size().X); })
				.HeightOverride_Lambda([this]() { return FOptionalSize(Size().Y); })
				[
					SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Thumb")).Padding(0)
					[
						SNew(SOverlay)
						+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(Url.IsEmpty() ? LOCTEXT("AgentUI.ImageGone", "The picture's link is not available.") : LOCTEXT("AgentUI.ImageLoading", "Loading the picture…"))
							.ColorAndOpacity(FSlateColor(FZettaStyle::Muted()))
							.Visibility_Lambda([this]() { return Brush.IsValid() ? EVisibility::Collapsed : EVisibility::Visible; })
						]
						+ SOverlay::Slot()
						[
							SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
							.Visibility_Lambda([this]() { return Brush.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; })
							[
								SNew(SImage).Image_Lambda([this]() { return Brush.IsValid() ? Brush.Get() : FAppStyle::GetNoBrush(); })
							]
						]
					]
				]
			];
			if (Url.IsEmpty()) return;
			Brush = FZettaImageCache::Get().Find(Url);
			if (Brush.IsValid()) return;
			TWeakPtr<SWidget> Weak = AsShared();
			FZettaImageCache::Get().Request(Url, [this, Weak](TSharedPtr<FSlateBrush> Loaded)
			{
				if (Weak.IsValid()) Brush = Loaded;
			});
		}

	private:
		FVector2D Size() const
		{
			if (!Brush.IsValid()) return FVector2D(240, 160);
			const FVector2D Image(Brush->GetImageSize().X, Brush->GetImageSize().Y);
			if (Image.X <= 0 || Image.Y <= 0) return FVector2D(240, 160);
			const double Scale = FMath::Min(1.0, 320.0 / FMath::Max(Image.X, Image.Y));
			return Image * Scale;
		}

		FString Url;
		TSharedPtr<FSlateBrush> Brush;
	};

	const TArray<FString>& PageExamples()
	{
		static const TArray<FString> List = {
			TEXT("Look at my level and tell me what's here"),
			TEXT("Add a third-person character with a follow camera"),
			TEXT("Make a coin pickup that adds to a score on screen"),
			TEXT("Generate a low-poly treasure chest and place it by the player start"),
		};
		return List;
	}
}

// ─── Construction ─────────────────────────────────────────────────────────────

void SZettaAgentPage::Construct(const FArguments& InArgs)
{
	RuntimeHandle = FZettaAgentRuntime::Get().OnChanged.AddSP(this, &SZettaAgentPage::MarkDirty);
	CatalogHandle = FZettaAgentCatalog::Get().OnChanged.AddSP(this, &SZettaAgentPage::MarkDirty);
	// Signing in asks the server again: a list fetched before (or without) sign-in would hide the own-key menus.
	AuthHandle = FZettaAuth::Get().OnChanged.AddLambda([]() { FZettaAgentCatalog::Get().Refresh(); });
	FZettaAgentCatalog::Get().Refresh();

	ChildSlot
	[
		SAssignNew(Main, SWidgetSwitcher)
		+ SWidgetSwitcher::Slot()[MakeChat()]
		+ SWidgetSwitcher::Slot()
		[
			SNew(SZettaAgentSettings).OnBack(FSimpleDelegate::CreateLambda([this]() { SetSettingsVisible(false); }))
		]
	];
	RenderTranscript();
}

SZettaAgentPage::~SZettaAgentPage()
{
	FZettaAgentRuntime::Get().OnChanged.Remove(RuntimeHandle);
	FZettaAgentCatalog::Get().OnChanged.Remove(CatalogHandle);
	FZettaAuth::Get().OnChanged.Remove(AuthHandle);
}

int32 SZettaAgentPage::AddToChat(const TArray<FZettaMention>& InMentions)
{
	if (InMentions.Num() == 0) return 0;
	PendingMentions.Append(InMentions);
	FZetta3DEditorModule::Get().OpenWindow(TEXT("Agent"));
	return InMentions.Num();
}

void SZettaAgentPage::ShowSettings()
{
	bPendingSettings = true;
	FZetta3DEditorModule::Get().OpenWindow(TEXT("Agent"));
}

void SZettaAgentPage::SetSettingsVisible(bool bVisible)
{
	if (Main.IsValid()) Main->SetActiveWidgetIndex(bVisible ? 1 : 0);
	if (bVisible) FZettaAgentCatalog::Get().Refresh();
}

void SZettaAgentPage::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (bPendingSettings)
	{
		bPendingSettings = false;
		SetSettingsVisible(true);
	}
	if (PendingMentions.Num() > 0) TakePending();
	if (bDirty) RenderTranscript();
}

TSharedRef<SWidget> SZettaAgentPage::MakeChat()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[MakeToolbar()]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return Panel == EPanel::History ? EVisibility::Visible : EVisibility::Collapsed; })
			.MaxDesiredHeight(340)
			.Padding(FMargin(10, 0, 10, 6))
			[
				SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Card")).Padding(8)[SAssignNew(HistoryBody, SBox)]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return Panel == EPanel::Memory ? EVisibility::Visible : EVisibility::Collapsed; })
			.MaxDesiredHeight(420)
			.Padding(FMargin(10, 0, 10, 6))
			[
				SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Card")).Padding(8)[SAssignNew(MemoryBody, SBox)]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SAssignNew(Scroll, SScrollBox)
			+ SScrollBox::Slot().Padding(FMargin(14, 6, 14, 6))
			[
				SAssignNew(List, SVerticalBox)
			]
			+ SScrollBox::Slot().Padding(FMargin(14, 0, 14, 10))
			[
				MakeWorking()
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(10, 0, 10, 4))
		[
			MakeMentionPicker()
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(10, 0, 10, 10))
		[
			MakeComposer()
		];
}

TSharedRef<SWidget> SZettaAgentPage::MakeToolbar()
{
	auto PanelButton = [this](const FText& Label, const FText& Tip, EPanel Which)
	{
		return SNew(SCheckBox)
			.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox"))
			.ToolTipText(Tip)
			.IsChecked_Lambda([this, Which]() { return Panel == Which ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Which](ECheckBoxState) { TogglePanel(Which); })
			[
				SNew(SBox).Padding(FMargin(8, 2))[SNew(STextBlock).Text(Label)]
			];
	};

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("NoBorder"))
		.Padding(FMargin(10, 6))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				PanelButton(LOCTEXT("AgentUI.History", "History"), LOCTEXT("AgentUI.HistoryTip", "Your conversations in this project"), EPanel::History)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				ZettaAgentCards::QuietButton(LOCTEXT("AgentUI.NewChat", "New chat"), FSimpleDelegate::CreateLambda([this]()
				{
					FZettaAgentRuntime::Get().NewChat();
					Panel = EPanel::None;
					FSlateApplication::Get().SetKeyboardFocus(Input);
				}), LOCTEXT("AgentUI.NewChatTip", "Start a new conversation"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			// Text size: smaller and larger, kept for next time — also Cmd − / Cmd = (Ctrl on Windows), Cmd 0 back to standard.
			+ SHorizontalBox::Slot().AutoWidth()
			[
				ZettaAgentCards::QuietButton(FText::FromString(TEXT("A−")),
					FSimpleDelegate::CreateLambda([this]() { SetTextSize(UZettaUserSettings::Get()->GetAgentTextSize() - 1); }),
					FText::Format(LOCTEXT("AgentUI.SmallerTip", "Smaller text ({0}−). {0}0 sets it back."), FText::FromString(PLATFORM_MAC ? TEXT("Cmd") : TEXT("Ctrl+"))),
					MakeAttributeLambda([]() { return UZettaUserSettings::Get()->GetAgentTextSize() > UZettaUserSettings::AgentTextSizeMin; }))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
			[
				ZettaAgentCards::QuietButton(FText::FromString(TEXT("A+")),
					FSimpleDelegate::CreateLambda([this]() { SetTextSize(UZettaUserSettings::Get()->GetAgentTextSize() + 1); }),
					FText::Format(LOCTEXT("AgentUI.LargerTip", "Larger text ({0}=). {0}0 sets it back."), FText::FromString(PLATFORM_MAC ? TEXT("Cmd") : TEXT("Ctrl+"))),
					MakeAttributeLambda([]() { return UZettaUserSettings::Get()->GetAgentTextSize() < UZettaUserSettings::AgentTextSizeMax; }))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				PanelButton(LOCTEXT("AgentUI.Memory", "Memory"), LOCTEXT("AgentUI.MemoryTip", "What the agent remembers about this project, and its brief"), EPanel::Memory)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SComboButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
				.HasDownArrow(false)
				.ToolTipText(LOCTEXT("AgentUI.MoreTip", "Mode, autonomy and what the agent runs on"))
				.OnGetMenuContent(this, &SZettaAgentPage::MoreMenu)
				.ButtonContent()[SNew(SImage).Image(FAppStyle::GetBrush("Icons.EllipsisVerticalNarrow")).ColorAndOpacity(FSlateColor::UseForeground())]
			]
		];
}

TSharedRef<SWidget> SZettaAgentPage::MoreMenu()
{
	FMenuBuilder Menu(true, nullptr);
	UZettaUserSettings* Prefs = UZettaUserSettings::Get();
	auto Radio = [&Menu](const FText& Label, const FText& Tip, TFunction<bool()> IsOn, TFunction<void()> Set)
	{
		Menu.AddMenuEntry(Label, Tip, FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([Set]() { Set(); }), FCanExecuteAction(), FIsActionChecked::CreateLambda([IsOn]() { return IsOn(); })),
			NAME_None, EUserInterfaceActionType::RadioButton);
	};

	Menu.BeginSection(TEXT("Mode"), LOCTEXT("AgentUI.ModeSection", "Mode"));
	Radio(LOCTEXT("AgentUI.ModeAgent", "Agent — works in the editor"), LOCTEXT("AgentUI.ModeAgentTip", "Reads, then writes code, builds the level and makes art."),
		[Prefs]() { return Prefs->AgentMode != TEXT("ask"); }, [Prefs]() { Prefs->AgentMode = TEXT("agent"); Prefs->Save(); });
	Radio(LOCTEXT("AgentUI.ModeAsk", "Ask — reads and explains, changes nothing"), LOCTEXT("AgentUI.ModeAskTip", "Answers questions about your project, and changes nothing."),
		[Prefs]() { return Prefs->AgentMode == TEXT("ask"); }, [Prefs]() { Prefs->AgentMode = TEXT("ask"); Prefs->Save(); });
	Menu.EndSection();

	Menu.BeginSection(TEXT("Autonomy"), LOCTEXT("AgentUI.AutonomySection", "Autonomy"));
	Radio(LOCTEXT("AgentUI.AskFirst", "Ask first — before deleting, running code or spending credits"), FText(),
		[Prefs]() { return Prefs->AgentApproval != TEXT("auto"); }, [Prefs]() { Prefs->AgentApproval = TEXT("confirm_destructive"); Prefs->Save(); });
	Radio(LOCTEXT("AgentUI.FullAutonomy", "Full autonomy — runs to the end on its own"), FText(),
		[Prefs]() { return Prefs->AgentApproval == TEXT("auto"); }, [Prefs]() { Prefs->AgentApproval = TEXT("auto"); Prefs->Save(); });
	Menu.EndSection();

	Menu.BeginSection(TEXT("More"));
	Menu.AddMenuEntry(LOCTEXT("AgentUI.RunsOn", "What the agent runs on…"), LOCTEXT("AgentUI.RunsOnTip", "Your Zetta account, or your own API keys"), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { SetSettingsVisible(true); })));
	Menu.AddMenuEntry(LOCTEXT("AgentUI.OpenStudio", "Open the 3D Studio"), FText(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]() { FZetta3DEditorModule::Get().OpenWindow(TEXT("Studio")); })));
	Menu.AddMenuEntry(FText::Format(LOCTEXT("AgentUI.TokensThisChat", "Tokens this chat: {0}"), FText::AsNumber(FZettaAgentRuntime::Get().TokensThisSession())),
		FText(), FSlateIcon(), FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
	Menu.EndSection();
	return Menu.MakeWidget();
}

// ─── Working indicator ────────────────────────────────────────────────────────

TSharedRef<SWidget> SZettaAgentPage::MakeWorking()
{
	return SNew(SHorizontalBox)
		.Visibility_Lambda([]() { return FZettaAgentRuntime::Get().IsBusy() ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
		[
			// Three dots that light up in turn, so a long turn reads as running, not stuck; they hold still while it waits on the user.
			SNew(STextBlock)
			.Font(FZettaStyle::Font(10, true))
			.ColorAndOpacity_Lambda([]() { return FSlateColor(FZettaAgentRuntime::Get().IsWaitingForUser() ? FZettaStyle::Warning() : FZettaStyle::Accent()); })
			.Text_Lambda([]()
			{
				if (FZettaAgentRuntime::Get().IsWaitingForUser()) return FText::FromString(TEXT("● ● ●"));
				const int32 Frame = (int32)(FPlatformTime::Seconds() / 0.26) % 3;
				return FText::FromString(Frame == 0 ? TEXT("● ○ ○") : Frame == 1 ? TEXT("○ ● ○") : TEXT("○ ○ ●"));
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor(FZettaStyle::Muted()))
			.Text_Lambda([]()
			{
				const FZettaAgentRuntime& Runtime = FZettaAgentRuntime::Get();
				switch (Runtime.Phase())
				{
				case FZettaAgentRuntime::EPhase::Streaming: return LOCTEXT("AgentUI.PhaseThinking", "Thinking");
				case FZettaAgentRuntime::EPhase::Sending: return LOCTEXT("AgentUI.PhaseCarrying", "Carrying on");
				case FZettaAgentRuntime::EPhase::Tools:
					return Runtime.IsWaitingForUser() ? LOCTEXT("AgentUI.PhaseWaiting", "Waiting for you") : LOCTEXT("AgentUI.PhaseWorking", "Working in the editor");
				default: return FText();
				}
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox)
			.Visibility_Lambda([]() { return FZettaAgentRuntime::Get().RouteText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			.ToolTipText_Lambda([]() { return FZettaAgentRuntime::Get().RouteWhy(); })
			[
				SNew(SBorder)
				.BorderImage(FZettaStyle::Get().GetBrush("Zetta.Chip"))
				.Padding(FMargin(8, 1))
				[
					SNew(STextBlock).Font(FZettaStyle::Font(8, true)).Text_Lambda([]() { return FZettaAgentRuntime::Get().RouteText(); })
				]
			]
		];
}

// ─── Transcript ───────────────────────────────────────────────────────────────

TSharedRef<SWidget> SZettaAgentPage::Welcome()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[ZettaUI::Title(LOCTEXT("AgentUI.WelcomeTitle", "What are we building?"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
		[
			ZettaUI::Hint(LOCTEXT("AgentUI.WelcomeBody",
				"The agent reads your level, Blueprints and C++, writes code, builds actors, Blueprints and materials, looks at the "
				"viewport to check its work, and makes 3D models and pictures with Zetta. Everything it changes can be undone."))
		];
	for (const FString& Example : PageExamples())
	{
		Box->AddSlot().AutoHeight().Padding(0, 1)
		[
			SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
			.HAlign(HAlign_Left)
			.ContentPadding(FMargin(8, 4))
			.OnClicked_Lambda([this, Example]()
			{
				Input->SetText(FText::FromString(Example));
				FSlateApplication::Get().SetKeyboardFocus(Input);
				Input->GoTo(ETextLocation::EndOfDocument);
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("“") + Example + TEXT("”"))).ColorAndOpacity(FSlateColor(FZettaStyle::AccentHover()))
			]
		];
	}
	return ZettaUI::Card(Box, 14.f);
}

void SZettaAgentPage::RenderTranscript()
{
	bDirty = false;
	if (!List.IsValid()) return;
	const TArray<FZettaChatItemPtr>& Items = FZettaAgentRuntime::Get().Items();
	const bool bNearEnd = Scroll.IsValid() && Scroll->GetScrollOffsetOfEnd() - Scroll->GetScrollOffset() < 80.f;

	if (Items.Num() == 0)
	{
		Rows.Reset();
		List->ClearChildren();
		WelcomeWidget = Welcome();
		List->AddSlot().AutoHeight()[WelcomeWidget.ToSharedRef()];
		LastCount = 0;
		return;
	}
	if (WelcomeWidget.IsValid())
	{
		List->RemoveSlot(WelcomeWidget.ToSharedRef());
		WelcomeWidget.Reset();
	}

	// Rows that no longer line up with the transcript (a new chat, a reopened session) go; the rest are reused.
	int32 Keep = 0;
	while (Keep < Rows.Num() && Keep < Items.Num() && Rows[Keep].Item == Items[Keep]) ++Keep;
	for (int32 i = Rows.Num() - 1; i >= Keep; --i)
	{
		List->RemoveSlot(Rows[i].Box.ToSharedRef());
		Rows.RemoveAt(i);
	}
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const FZettaChatItemPtr& Item = Items[i];
		// A reply still being written keeps its row: its live text updates in place (RenderItem). Its signature here
		// only says "streaming", so the row is rebuilt once — into the finished reply — when it stops.
		const bool bStreaming = FZettaAgentRuntime::Get().IsStreaming(Item);
		const uint32 Signature = bStreaming ? HashCombine(GetTypeHash((uint8)Item->Kind), 0x5752u) : Item->Signature();
		if (i < Rows.Num())
		{
			if (Rows[i].Signature == Signature) continue;
			Rows[i].Box->SetContent(RenderItem(Item));
			Rows[i].Signature = Signature;
			continue;
		}
		FRow Row;
		Row.Item = Item;
		Row.Signature = Signature;
		SAssignNew(Row.Box, SBox)[RenderItem(Item)];
		List->AddSlot().AutoHeight().Padding(0, 3)[Row.Box.ToSharedRef()];
		Rows.Add(Row);
	}

	// Follow the conversation as it grows — unless the user scrolled up to read.
	const uint32 Last = Items.Last()->Signature();
	if (Items.Num() != LastCount || Last != LastSignature)
	{
		const bool bOwnMessage = Items.Num() != LastCount && Items.Last()->Kind == FZettaChatItem::EKind::User;
		if (bNearEnd || bOwnMessage) Scroll->ScrollToEnd();
		LastCount = Items.Num();
		LastSignature = Last;
	}
}

TSharedRef<SWidget> SZettaAgentPage::RenderItem(const FZettaChatItemPtr& Item)
{
	using EKind = FZettaChatItem::EKind;
	switch (Item->Kind)
	{
	case EKind::User:
	{
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ZettaAgentMarkdown::Plain(Item->Text)];
		if (!Item->Detail.IsEmpty()) Box->AddSlot().AutoHeight().Padding(0, 3, 0, 0)[ZettaUI::Hint(FText::FromString(Item->Detail))];
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.15f)
			+ SHorizontalBox::Slot().FillWidth(0.85f).HAlign(HAlign_Right)
			[
				SNew(SZettaCopyableMessage).Text(Item->Text).bUser(true)
				[
					SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Bubble.User")).Padding(FMargin(12, 8))[Box]
				]
			];
	}
	case EKind::Assistant:
		if (FZettaAgentRuntime::Get().IsStreaming(Item))
		{
			const TWeakPtr<FZettaChatItem> Weak = Item;
			return ZettaAgentMarkdown::RenderLive([Weak]()
			{
				const TSharedPtr<FZettaChatItem> Live = Weak.Pin();
				return Live.IsValid() ? Live->Text : FString();
			});
		}
		return SNew(SZettaCopyableMessage).Text(Item->Text)[ZettaAgentMarkdown::Render(Item->Text)];
	case EKind::Step:
		return StepRow(Item);
	case EKind::Approval:
		return ZettaAgentCards::Approval(Item);
	case EKind::Ask:
		return ZettaAgentCards::Ask(Item);
	case EKind::Question:
		return ZettaAgentCards::Question(Item);
	case EKind::Changes:
		return ZettaAgentCards::Changes(Item->Turn);
	case EKind::Memory:
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				ZettaAgentCards::Chip(FText::FromString(Item->Detail), FZettaStyle::Accent())
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[ZettaUI::Hint(FText::FromString(Item->Text))];
	case EKind::Image:
		return ImageCard(Item);
	case EKind::Event:
		return ZettaAgentMarkdown::Plain(Item->Text, TEXT("Md.Muted"));
	case EKind::Error:
		return ErrorCard(Item);
	default:
		return ZettaUI::Hint(FText::FromString(Item->Text));
	}
}

TSharedRef<SWidget> SZettaAgentPage::StepRow(const FZettaChatItemPtr& Item)
{
	const FString Status = Item->Status;
	FString Text = Item->Text;
	if (Status == TEXT("running")) Text += TEXT("…");
	else if (Status == TEXT("declined")) Text += LOCTEXT("AgentUI.StepDeclined", " — declined").ToString();
	else if (Status == TEXT("stopped")) Text += LOCTEXT("AgentUI.StepStopped", " — stopped").ToString();

	const bool bRunning = Status == TEXT("running");
	const FLinearColor Color = Status == TEXT("done") ? FZettaStyle::Success()
		: Status == TEXT("error") || Status == TEXT("declined") || Status == TEXT("stopped") ? FZettaStyle::Error()
		: Status == TEXT("waiting") ? FZettaStyle::Warning() : FZettaStyle::Accent();

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Text)).AutoWrapText(true)];
	if (!Item->Detail.IsEmpty())
	{
		Column->AddSlot().AutoHeight()[ZettaAgentMarkdown::Plain(Item->Detail, Status == TEXT("error") ? TEXT("Md.Error") : TEXT("Md.Muted"))];
	}
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(2, 1, 8, 0)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("●")))
			.Font(FZettaStyle::Font(8))
			// A running step's dot breathes.
			.ColorAndOpacity_Lambda([Color, bRunning]()
			{
				FLinearColor C = Color;
				if (bRunning) C.A = 0.45f + 0.55f * (0.5f + 0.5f * FMath::Sin((float)FPlatformTime::Seconds() * 5.f));
				return FSlateColor(C);
			})
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Column];
}

TSharedRef<SWidget> SZettaAgentPage::ErrorCard(const FZettaChatItemPtr& Item)
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[ZettaAgentMarkdown::Plain(Item->Text, TEXT("Md.Error"))];
	// A key the provider refused, an empty balance, a model the key can't use: the fix is in the settings.
	const FString Code = Item->Detail;
	if (Code == TEXT("provider_key_invalid") || Code == TEXT("provider_quota") || Code == TEXT("provider_model_unavailable") || Code == TEXT("provider_key_missing"))
	{
		Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, 6, 0, 0)
		[
			ZettaUI::Button(LOCTEXT("AgentUI.OpenSettings", "Open settings"), FSimpleDelegate::CreateLambda([this]() { SetSettingsVisible(true); }))
		];
	}
	return SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Card")).Padding(10)[Box];
}

TSharedRef<SWidget> SZettaAgentPage::ImageCard(const FZettaChatItemPtr& Item)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
		[
			SNew(SZettaChatImage).Url(Item->ImageUrl)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[ZettaUI::Hint(FText::FromString(Item->Text))]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				ZettaAgentCards::QuietButton(LOCTEXT("AgentUI.SaveToProject", "Save to project"), FSimpleDelegate::CreateLambda([this, Item]() { SaveImage(Item); }),
					LOCTEXT("AgentUI.SaveToProjectTip", "Save the picture as a texture in the project's Zetta 3D images folder"),
					MakeAttributeLambda([Item]() { return !Item->ImageUrl.IsEmpty(); }))
			]
		];
}

void SZettaAgentPage::SaveImage(const FZettaChatItemPtr& Item)
{
	if (Item->ImageUrl.IsEmpty()) return;
	const FString Name = Item->ImageId > 0 ? FString::Printf(TEXT("T_Agent_%lld"), Item->ImageId) : FString::Printf(TEXT("T_Agent_%s"), *FDateTime::Now().ToString(TEXT("%H%M%S")));
	const FString Target = UZettaProjectSettings::Get()->GetImagesFolder() / Name;
	FZettaHttp::Download(Item->ImageUrl, [Target](const FZettaResponse& R)
	{
		if (!R.bOk)
		{
			ZettaAgentCards::Toast(FText::FromString(R.Message), true);
			return;
		}
		FString ObjectPath, Error;
		if (!ZettaImporter::SaveTexture(R.Bytes, Target, ObjectPath, Error))
		{
			ZettaAgentCards::Toast(FText::Format(LOCTEXT("AgentUI.CouldNotSave", "Could not save: {0}"), FText::FromString(Error)), true);
			return;
		}
		ZettaAgentCards::Toast(FText::Format(LOCTEXT("AgentUI.SavedAs", "Saved as {0}"), FText::FromString(ObjectPath)));
		FContentBrowserModule& Browser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		if (UObject* Saved = FindObject<UObject>(nullptr, *ObjectPath)) Browser.Get().SyncBrowserToAssets(TArray<UObject*>{ Saved });
	});
}

// ─── Composer ─────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SZettaAgentPage::MakeComposer()
{
	return SNew(SBorder)
		.BorderImage(FZettaStyle::Get().GetBrush("Zetta.CardRaised"))
		.BorderBackgroundColor_Lambda([this]() { return bDragOver ? FZettaStyle::Accent() : FLinearColor::White; })
		.Padding(FMargin(8, 6))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
			[
				SAssignNew(AttachmentsBox, SWrapBox)
				.UseAllottedSize(true)
				.InnerSlotPadding(FVector2D(4, 4))
				.Visibility_Lambda([this]() { return Images.Num() + Files.Num() + Mentions.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).MinDesiredHeight(54).MaxDesiredHeight(220)
				[
					SAssignNew(Input, SMultiLineEditableTextBox)
					.Font_Lambda([]() { return FZettaStyle::Font(10 + UZettaUserSettings::Get()->GetAgentTextSize()); })
					.HintText(LOCTEXT("AgentUI.ComposerHint", "Ask the agent to build something — “add a third-person character with a follow camera”… Type @ to tag actors and assets."))
					.AutoWrapText(true)
					.AllowMultiLine(true)
					.OnKeyDownHandler(this, &SZettaAgentPage::OnInputKeyDown)
					.OnTextChanged_Lambda([this](const FText&) { UpdateMention(); })
					.OnCursorMoved_Lambda([this](const FTextLocation&) { UpdateMention(); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
				[
					SNew(SComboButton)
					.HasDownArrow(false)
					.ToolTipText(LOCTEXT("AgentUI.AttachTip", "Attach a picture of the level viewport or any file — or drag files, actors and assets onto the chat. Type @, or select things and press Ctrl+Shift+M (Shift+Cmd+M on a Mac), to tag actors and assets."))
					.OnGetMenuContent(this, &SZettaAgentPage::AttachMenu)
					.ButtonContent()[SNew(STextBlock).Text(FText::FromString(TEXT("+"))).Font(FZettaStyle::Font(11, true))]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MakePickers()]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("AgentUI.NewLineHint", "Shift+Enter: new line")).Font(FZettaStyle::Font(8)).ColorAndOpacity(FSlateColor(FZettaStyle::Muted()))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
					.ContentPadding(FMargin(14, 4))
					.ToolTipText_Lambda([]() { return FZettaAgentRuntime::Get().IsBusy() ? LOCTEXT("AgentUI.StopTip", "Stop the agent (Esc)") : LOCTEXT("AgentUI.SendTip", "Send (Enter)"); })
					.OnClicked_Lambda([this]() { Submit(true); return FReply::Handled(); })
					[
						SNew(STextBlock).Font(FZettaStyle::Font(10, true))
						.Text_Lambda([]() { return FZettaAgentRuntime::Get().IsBusy() ? LOCTEXT("AgentUI.Stop", "Stop") : LOCTEXT("AgentUI.Send", "Send"); })
					]
				]
			]
		];
}

TSharedRef<SWidget> SZettaAgentPage::MakePickers()
{
	auto Combo = [](TAttribute<FText> Label, TAttribute<FText> Tip, FOnGetContent Menu, TAttribute<EVisibility> Visible, TAttribute<FSlateColor> Color)
	{
		return SNew(SBox).Visibility(Visible).Padding(FMargin(0, 0, 4, 0))
			[
				SNew(SComboButton)
				.ToolTipText(Tip)
				.IsEnabled_Lambda([]() { return !FZettaAgentRuntime::Get().IsBusy(); })
				.OnGetMenuContent(Menu)
				.ButtonContent()[SNew(STextBlock).Text(Label).Font(FZettaStyle::Font(9)).ColorAndOpacity(Color)]
			];
	};
	auto Radio = [](FMenuBuilder& Menu, const FText& Label, const FText& Tip, bool bChecked, bool bEnabled, TFunction<void()> Set)
	{
		Menu.AddMenuEntry(Label, Tip, FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([Set]() { Set(); }), FCanExecuteAction::CreateLambda([bEnabled]() { return bEnabled; }),
				FIsActionChecked::CreateLambda([bChecked]() { return bChecked; })),
			NAME_None, EUserInterfaceActionType::RadioButton);
	};
	const FSlateColor Normal = FSlateColor::UseForeground();

	// With no key saved yet, a button to add one stands in for the menus.
	const TAttribute<EVisibility> AddKeyVisible = MakeAttributeLambda([]()
	{
		FZettaAgentCatalog& C = FZettaAgentCatalog::Get();
		return C.OwnKeys() && C.KeyedProviders().Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed;
	});
	const TAttribute<EVisibility> MenusVisible = MakeAttributeLambda([]()
	{
		FZettaAgentCatalog& C = FZettaAgentCatalog::Get();
		return C.OwnKeys() && C.KeyedProviders().Num() == 0 ? EVisibility::Collapsed : EVisibility::Visible;
	});

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).Visibility(AddKeyVisible).Padding(FMargin(0, 0, 4, 0))
			[
				ZettaUI::Button(LOCTEXT("AgentUI.AddKey", "Add an API key"), FSimpleDelegate::CreateLambda([this]() { SetSettingsVisible(true); }), true,
					LOCTEXT("AgentUI.AddKeyTip", "The agent is set to run on your own API keys, and none is saved yet. Add one, or switch back to your Zetta account."))
			]
		]
		// What the agent runs on: the Zetta account, or a provider the user saved a key for.
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Combo(MakeAttributeLambda([]()
				{
					const FZettaAgentProvider* P = FZettaAgentCatalog::Get().ActiveProvider();
					return P ? FText::FromString(P->Label) : LOCTEXT("AgentUI.ZettaAccountMenu", "Zetta Account");
				}),
				LOCTEXT("AgentUI.AccountTip", "What the agent runs on: your Zetta account and credits, or your own API key with a provider."),
				FOnGetContent::CreateLambda([Radio]()
				{
					FMenuBuilder Menu(true, nullptr);
					FZettaAgentCatalog& Catalog = FZettaAgentCatalog::Get();
					UZettaUserSettings* Prefs = UZettaUserSettings::Get();
					Radio(Menu, LOCTEXT("AgentUI.ZettaAccountMenu", "Zetta Account"), LOCTEXT("AgentUI.ZettaAccountMenuTip", "Your Zetta account and credits"),
						!Catalog.OwnKeys(), true, [Prefs]()
						{
							Prefs->bAgentUseOwnKeys = false;
							Prefs->Save();
							FZettaAgentCatalog::Get().OnChanged.Broadcast();
						});
					const FZettaAgentProvider* Active = Catalog.ActiveProvider();
					for (const FZettaAgentProvider* P : Catalog.KeyedProviders())
					{
						const FString Key = P->Key;
						Radio(Menu, FText::FromString(P->Label), FText::Format(LOCTEXT("AgentUI.YourKey", "Your own {0} key"), FText::FromString(P->Label)),
							Active == P, true, [Prefs, Key]()
							{
								Prefs->bAgentUseOwnKeys = true;
								Prefs->AgentProvider = Key;
								Prefs->Save();
								FZettaAgentCatalog::Get().OnChanged.Broadcast();
							});
					}
					Menu.AddMenuSeparator();
					Menu.AddMenuEntry(LOCTEXT("AgentUI.ManageKeys", "Manage API keys…"), FText(), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([]() { SZettaAgentPage::ShowSettings(); })));
					return Menu.MakeWidget();
				}),
				MakeAttributeLambda([]() { return FZettaAgentCatalog::Get().KeyedProviders().Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed; }),
				Normal)
		]
		// The model: a tier on the Zetta account, the provider's models on the user's own key.
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Combo(MakeAttributeLambda([]()
				{
					FZettaAgentCatalog& Catalog = FZettaAgentCatalog::Get();
					if (Catalog.OwnKeys())
					{
						const FZettaAgentModel* M = Catalog.ActiveModel();
						return M ? FText::FromString(M->Label) : LOCTEXT("AgentUI.NoModel", "Model");
					}
					const FString Tier = UZettaUserSettings::Get()->AgentTier;
					for (const FZettaAgentOption& O : Catalog.Tiers())
					{
						if (O.Key == Tier) return FText::FromString(O.Label);
					}
					return LOCTEXT("AgentUI.AutoModel", "Auto");
				}),
				MakeAttributeLambda([]()
				{
					FZettaAgentCatalog& Catalog = FZettaAgentCatalog::Get();
					const FZettaAgentModel* M = Catalog.OwnKeys() ? Catalog.ActiveModel() : nullptr;
					return M ? FText::Format(LOCTEXT("AgentUI.ModelTipOwn", "{0} — {1} Used for every message: Auto never changes the model on your own key."), FText::FromString(M->Label), FText::FromString(M->Blurb))
						: LOCTEXT("AgentUI.ModelTip", "Model — Auto picks the model for each message.");
				}),
				FOnGetContent::CreateLambda([Radio]()
				{
					FMenuBuilder Menu(true, nullptr);
					FZettaAgentCatalog& Catalog = FZettaAgentCatalog::Get();
					UZettaUserSettings* Prefs = UZettaUserSettings::Get();
					if (Catalog.OwnKeys())
					{
						const FZettaAgentProvider* P = Catalog.ActiveProvider();
						const FZettaAgentModel* Active = Catalog.ActiveModel();
						if (P)
						{
							const FString ProviderKey = P->Key;
							for (const FZettaAgentModel& M : P->Models)
							{
								const FString ModelKey = M.Key;
								Radio(Menu, FText::FromString(M.Label), FText::FromString(M.Blurb), Active && Active->Key == M.Key, true,
									[ProviderKey, ModelKey]() { FZettaAgentCatalog::Get().SetModelFor(ProviderKey, ModelKey); });
							}
						}
					}
					else
					{
						for (const FZettaAgentOption& O : Catalog.Tiers())
						{
							const FString Key = O.Key;
							Radio(Menu, FText::FromString(O.Label), O.bAvailable ? FText::FromString(O.Blurb) : LOCTEXT("AgentUI.NotOnPlan", "Not on your plan — upgrade at zetta3d.ai/pricing."),
								Prefs->AgentTier == Key, O.bAvailable, [Prefs, Key]() { Prefs->AgentTier = Key; Prefs->Save(); });
						}
					}
					return Menu.MakeWidget();
				}),
				MenusVisible, Normal)
		]
		// The thinking, with its own Auto: Auto + Auto lets the agent decide both for each message.
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Combo(MakeAttributeLambda([]()
				{
					FZettaAgentCatalog& Catalog = FZettaAgentCatalog::Get();
					const FString Wanted = UZettaUserSettings::Get()->AgentThinking;
					const FString Shown = Catalog.OwnKeys() ? FZettaAgentCatalog::ThinkingFor(Catalog.ActiveModel(), Wanted) : Wanted;
					return FZettaAgentCatalog::ThinkingLabel(Shown);
				}),
				LOCTEXT("AgentUI.ThinkingTip", "How hard the model thinks — Auto decides for each message, and thinks harder on long jobs."),
				FOnGetContent::CreateLambda([Radio]()
				{
					FMenuBuilder Menu(true, nullptr);
					FZettaAgentCatalog& Catalog = FZettaAgentCatalog::Get();
					UZettaUserSettings* Prefs = UZettaUserSettings::Get();
					TArray<TPair<FString, bool>> Levels;
					if (Catalog.OwnKeys())
					{
						// Auto, then only the levels this model has.
						Levels.Add({ TEXT("auto"), true });
						if (const FZettaAgentModel* M = Catalog.ActiveModel())
						{
							for (const FString& L : M->Thinking) Levels.Add({ L, true });
						}
					}
					else
					{
						for (const FZettaAgentOption& O : Catalog.ThinkingLevels()) Levels.Add({ O.Key, O.bAvailable });
					}
					const FString Shown = Catalog.OwnKeys() ? FZettaAgentCatalog::ThinkingFor(Catalog.ActiveModel(), Prefs->AgentThinking) : Prefs->AgentThinking;
					for (const TPair<FString, bool>& L : Levels)
					{
						const FString Key = L.Key;
						Radio(Menu, FZettaAgentCatalog::ThinkingLabel(Key), L.Value ? FText() : LOCTEXT("AgentUI.ThinkNotOnPlan", "Not on your plan"),
							Shown == Key, L.Value, [Prefs, Key]() { Prefs->AgentThinking = Key; Prefs->Save(); });
					}
					return Menu.MakeWidget();
				}),
				MenusVisible, Normal)
		]
		// Autonomy: Ask first, or Full autonomy.
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Combo(MakeAttributeLambda([]()
				{
					return UZettaUserSettings::Get()->AgentApproval == TEXT("auto") ? LOCTEXT("AgentUI.FullAutonomyShort", "Full autonomy") : LOCTEXT("AgentUI.AskFirstShort", "Ask first");
				}),
				MakeAttributeLambda([]()
				{
					return UZettaUserSettings::Get()->AgentApproval == TEXT("auto")
						? LOCTEXT("AgentUI.FullTip", "Full autonomy — the agent doesn't stop to ask: it edits, deletes and runs code on its own, and keeps going until the job is done. Spending credits still waits for your click, and everything it changes can be undone.")
						: LOCTEXT("AgentUI.AskFirstTip", "Ask first — the agent asks before deleting anything, running Python and spending credits.");
				}),
				FOnGetContent::CreateLambda([Radio]()
				{
					FMenuBuilder Menu(true, nullptr);
					UZettaUserSettings* Prefs = UZettaUserSettings::Get();
					Radio(Menu, LOCTEXT("AgentUI.AskFirstShort", "Ask first"), LOCTEXT("AgentUI.AskFirstMenuTip", "Asks before deleting, running code or spending credits"),
						Prefs->AgentApproval != TEXT("auto"), true, [Prefs]() { Prefs->AgentApproval = TEXT("confirm_destructive"); Prefs->Save(); });
					Radio(Menu, LOCTEXT("AgentUI.FullAutonomyShort", "Full autonomy"), LOCTEXT("AgentUI.FullMenuTip", "Runs to the end on its own; credits still need your click"),
						Prefs->AgentApproval == TEXT("auto"), true, [Prefs]() { Prefs->AgentApproval = TEXT("auto"); Prefs->Save(); });
					return Menu.MakeWidget();
				}),
				EVisibility::Visible,
				MakeAttributeLambda([]() { return FSlateColor(UZettaUserSettings::Get()->AgentApproval == TEXT("auto") ? FZettaStyle::Warning() : FSlateColor::UseForeground()); }))
		];
}

bool SZettaAgentPage::CanSee() const
{
	FZettaAgentCatalog& Catalog = FZettaAgentCatalog::Get();
	const FZettaAgentModel* Model = Catalog.OwnKeys() ? Catalog.ActiveModel() : nullptr;
	return !Model || Model->bVision;
}

TSharedRef<SWidget> SZettaAgentPage::AttachMenu()
{
	FMenuBuilder Menu(true, nullptr);
	const bool bSee = CanSee();
	Menu.AddMenuEntry(bSee ? LOCTEXT("AgentUI.AttachViewport", "Picture of the level viewport") : LOCTEXT("AgentUI.AttachViewportBlind", "Picture of the level viewport — this model can't see pictures"),
		LOCTEXT("AgentUI.AttachViewportTip", "What the level viewport shows now"), FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelViewport.HighResScreenshot"),
		FUIAction(FExecuteAction::CreateSP(this, &SZettaAgentPage::AttachViewport), FCanExecuteAction::CreateLambda([bSee]() { return bSee; })));
	Menu.AddMenuSeparator();
	TWeakPtr<SZettaAgentPage> Weak = SharedThis(this);
	Menu.AddSubMenu(LOCTEXT("AgentUI.AttachProjectPicture", "Picture from the project"), LOCTEXT("AgentUI.AttachProjectPictureTip", "Tag a texture, so the agent can look at it, edit it or build from it"),
		FNewMenuDelegate::CreateLambda([Weak](FMenuBuilder& Sub)
		{
			Sub.AddWidget(ZettaAgentCards::SearchPicker(TEXT("asset"), [Weak](const FZettaMention& M)
			{
				if (TSharedPtr<SZettaAgentPage> Self = Weak.Pin()) Self->AddMention(M);
			}, TEXT("Texture2D")), FText(), true);
		}));
	Menu.AddMenuEntry(LOCTEXT("AgentUI.AttachFile", "File…"), LOCTEXT("AgentUI.AttachFileTip", "Pictures and PDFs go to the agent, code and text are read in, anything else is copied into Content/Zetta3D/Attachments"),
		FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([Weak]()
		{
			const TArray<FString> Paths = ZettaUI::PickFiles(TEXT("Attach files"), TEXT("All files (*.*)|*.*"));
			if (TSharedPtr<SZettaAgentPage> Self = Weak.Pin()) Self->AddFiles(Paths);
		})));
	return Menu.MakeWidget();
}

void SZettaAgentPage::AttachViewport()
{
	TWeakPtr<SZettaAgentPage> Weak = SharedThis(this);
	auto Attach = [Weak](const TArray<uint8>& Jpeg)
	{
		const TSharedPtr<SZettaAgentPage> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		FZettaAgentFile File;
		const FString Problem = ZettaAgentAttachments::Picture(Jpeg, TEXT("level-viewport.jpg"), File);
		if (!Problem.IsEmpty())
		{
			ZettaAgentCards::Toast(FText::FromString(Problem), true);
			return;
		}
		Self->AddImage(File, TEXT("Level viewport"), Jpeg, TEXT("level-viewport.jpg"));
	};
	auto ReadViewport = [Attach]()
	{
		TArray<uint8> Jpeg;
		const FString Problem = ZettaAgentAttachments::CaptureViewport(Jpeg);
		if (!Problem.IsEmpty())
		{
			ZettaAgentCards::Toast(FText::FromString(Problem), true);
			return;
		}
		Attach(Jpeg);
	};

	// The agent's own capture_view, when this build has it, renders the view properly; otherwise the viewport is read back.
	if (!FZettaAgentTools::Get().Has(TEXT("capture_view")))
	{
		ReadViewport();
		return;
	}
	TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("view"), TEXT("viewport"));
	Request->SetStringField(TEXT("angles"), TEXT("current"));
	Request->SetNumberField(TEXT("width"), 1024);
	FZettaAgentTools::Get().Run(TEXT("capture_view"), Request, [Attach, ReadViewport](const FZettaToolResult& Result)
	{
		TArray<uint8> Jpeg;
		if (Result.bIsError || Result.Images.Num() == 0 || !FBase64::Decode(Result.Images[0].Data, Jpeg) || Jpeg.Num() == 0)
		{
			ReadViewport();
			return;
		}
		Attach(Jpeg);
	});
}

bool SZettaAgentPage::Fits(int32 Extra)
{
	int64 Total = Extra;
	for (const FZettaAgentFile& I : Images) Total += I.Data.Len();
	for (const FZettaAttachment& F : Files) Total += F.File.Data.Len() + F.Text.Len();
	if (Total <= PageMaxAttachedChars) return true;
	ZettaAgentCards::Toast(LOCTEXT("AgentUI.TooLargeMessage", "That would make the message too large to send — attach fewer or smaller files."), true);
	return false;
}

void SZettaAgentPage::AddImage(const FZettaAgentFile& File, const FString& Label, const TArray<uint8>& Original, const FString& Name)
{
	if (Images.Num() >= 4)
	{
		ZettaAgentCards::Toast(LOCTEXT("AgentUI.FourImages", "Up to 4 pictures per message."), true);
		return;
	}
	if (File.Data.Len() > ZettaAgentAttachments::MaxImageChars)
	{
		ZettaAgentCards::Toast(LOCTEXT("AgentUI.ImageTooLarge", "That picture is too large (about 3 MB at most)."), true);
		return;
	}
	if (!Fits(File.Data.Len())) return;
	Images.Add(File);
	ImageLabels.Add(Label);
	// Kept outside the content so the agent can edit or combine it by name; it is still sent if this fails.
	ImageKeys.Add(ZettaAgentAttachments::Keep(Original, Name));
	ImageBrushes.Add(FZettaImageCache::Get().FromBytes(FString::Printf(TEXT("agent-attach:%s:%u"), *Name, FCrc::MemCrc32(Original.GetData(), Original.Num())), Original));
	RenderAttachments();
}

void SZettaAgentPage::AddFiles(const TArray<FString>& Paths)
{
	for (const FString& Path : Paths)
	{
		FZettaAttachment Attachment;
		const FString Problem = ZettaAgentAttachments::FromPath(Path, Attachment);
		if (!Problem.IsEmpty())
		{
			ZettaAgentCards::Toast(FText::FromString(Problem), true);
			continue;
		}
		if (Attachment.Kind == TEXT("image"))
		{
			AddImage(Attachment.File, Attachment.Label, Attachment.Original, Attachment.OriginalName);
		}
		else if (Attachment.Kind == TEXT("mention"))
		{
			AddMention(Attachment.Mention);
			if (!Attachment.Mention.Note.IsEmpty())
			{
				ZettaAgentCards::Toast(FText::Format(LOCTEXT("AgentUI.CopiedInto", "Copied into {0}"), FText::FromString(Attachment.Label)));
			}
		}
		else
		{
			if (Attachment.Kind == TEXT("document") && Files.FilterByPredicate([](const FZettaAttachment& F) { return F.Kind == TEXT("document"); }).Num() >= 4)
			{
				ZettaAgentCards::Toast(LOCTEXT("AgentUI.FourPdfs", "Up to 4 PDFs per message."), true);
				continue;
			}
			if (!Fits(Attachment.File.Data.Len() + Attachment.Text.Len())) continue;
			Files.Add(MoveTemp(Attachment));
		}
	}
	RenderAttachments();
}

void SZettaAgentPage::AddMention(const FZettaMention& Mention)
{
	if (!Mentions.ContainsByPredicate([&Mention](const FZettaMention& M) { return M.SameAs(Mention); })) Mentions.Add(Mention);
	RenderAttachments();
}

void SZettaAgentPage::TakePending()
{
	if (!Input.IsValid()) return;
	for (const FZettaMention& M : PendingMentions) AddMention(M);
	PendingMentions.Reset();
	SetSettingsVisible(false);
	FSlateApplication::Get().SetKeyboardFocus(Input);
	Input->GoTo(ETextLocation::EndOfDocument);
}

void SZettaAgentPage::RenderAttachments()
{
	if (!AttachmentsBox.IsValid()) return;
	AttachmentsBox->ClearChildren();
	for (int32 i = 0; i < Images.Num(); ++i)
	{
		const TSharedPtr<FSlateBrush> Brush = ImageBrushes.IsValidIndex(i) ? ImageBrushes[i] : nullptr;
		const FString Label = ImageLabels[i];
		AttachmentsBox->AddSlot()
		[
			SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
			.ContentPadding(0)
			.ToolTipText(FText::Format(LOCTEXT("AgentUI.ImageChipTip", "{0} — click to remove"), FText::FromString(Label)))
			.OnClicked_Lambda([this, i]()
			{
				if (Images.IsValidIndex(i))
				{
					Images.RemoveAt(i);
					ImageLabels.RemoveAt(i);
					ImageKeys.RemoveAt(i);
					ImageBrushes.RemoveAt(i);
					RenderAttachments();
				}
				return FReply::Handled();
			})
			[
				SNew(SBox).WidthOverride(56).HeightOverride(40)
				[
					SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Thumb")).Padding(0)
					[
						SNew(SImage).Image(Brush.IsValid() ? Brush.Get() : FAppStyle::GetBrush("Icons.Image"))
					]
				]
			]
		];
	}
	for (int32 i = 0; i < Files.Num(); ++i)
	{
		const FZettaAttachment& F = Files[i];
		const FText Tip = F.Kind == TEXT("document") ? LOCTEXT("AgentUI.PdfTip", "PDF — the agent reads it") : LOCTEXT("AgentUI.TextTip", "Text — its contents go with your message");
		const FString Glyph = F.Kind == TEXT("document") ? TEXT("📄  ") : TEXT("📝  ");
		AttachmentsBox->AddSlot()
		[
			SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Chip")).Padding(FMargin(6, 1, 2, 1)).ToolTipText(Tip)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Glyph + F.Label)).Font(FZettaStyle::Font(9))]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
					.ContentPadding(FMargin(3, 0))
					.OnClicked_Lambda([this, i]()
					{
						if (Files.IsValidIndex(i)) Files.RemoveAt(i);
						RenderAttachments();
						return FReply::Handled();
					})
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("✕"))).Font(FZettaStyle::Font(8))
					]
				]
			]
		];
	}
	for (const FZettaMention& M : Mentions)
	{
		const FZettaMention Copy = M;
		AttachmentsBox->AddSlot()
		[
			ZettaAgentCards::MentionChip(M, FSimpleDelegate::CreateLambda([this, Copy]()
			{
				Mentions.RemoveAll([&Copy](const FZettaMention& X) { return X.SameAs(Copy); });
				RenderAttachments();
			}))
		];
	}
}

FReply SZettaAgentPage::OnInputKeyDown(const FGeometry&, const FKeyEvent& Event)
{
	if (HandleTextSizeKey(Event)) return FReply::Handled();
	const FKey Key = Event.GetKey();
	if (bMentionOpen)
	{
		// While the @ picker is open it owns ↑ ↓ Enter Tab Esc.
		if (Key == EKeys::Up || Key == EKeys::Down)
		{
			if (MentionResults.Num() + (MentionMode == TEXT("choose") ? 2 : 0) > 0)
			{
				const int32 Count = MentionMode == TEXT("choose") ? 2 : MentionResults.Num();
				MentionSelected = (MentionSelected + (Key == EKeys::Up ? -1 : 1) + Count) % Count;
				RefreshMention();
			}
			return FReply::Handled();
		}
		if (Key == EKeys::Enter || Key == EKeys::Tab)
		{
			if (MentionMode == TEXT("choose")) SetMentionMode(MentionSelected == 0 ? TEXT("level") : TEXT("project"));
			else if (MentionResults.IsValidIndex(MentionSelected)) PickMention(MentionSelected);
			return FReply::Handled();
		}
		if (Key == EKeys::Escape)
		{
			// In the Level or Project list, Esc steps back to the first menu; there, it closes.
			if (MentionMode != TEXT("choose")) SetMentionMode(TEXT("choose"));
			else CloseMention();
			return FReply::Handled();
		}
	}
	if (Key == EKeys::Enter && !Event.IsShiftDown())
	{
		// Enter sends; Shift+Enter is a new line (the text box's own handling).
		Submit(false);
		return FReply::Handled();
	}
	if (Key == EKeys::Escape && FZettaAgentRuntime::Get().IsBusy())
	{
		FZettaAgentRuntime::Get().Stop();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

bool SZettaAgentPage::HandleTextSizeKey(const FKeyEvent& Event)
{
	// Cmd on a Mac, Ctrl elsewhere — the key the platform uses for its own zoom shortcuts.
	const bool bAction = PLATFORM_MAC ? Event.IsCommandDown() : Event.IsControlDown();
	if (!bAction || Event.IsAltDown()) return false;
	const FKey Key = Event.GetKey();
	const int32 Current = UZettaUserSettings::Get()->GetAgentTextSize();
	if (Key == EKeys::Equals || Key == EKeys::Add) SetTextSize(Current + 1);
	else if (Key == EKeys::Hyphen || Key == EKeys::Subtract) SetTextSize(Current - 1);
	else if (Key == EKeys::Zero || Key == EKeys::NumPadZero) SetTextSize(0);
	else return false;
	return true;
}

void SZettaAgentPage::SetTextSize(int32 Step)
{
	UZettaUserSettings* Settings = UZettaUserSettings::Get();
	const int32 Next = FMath::Clamp(Step, UZettaUserSettings::AgentTextSizeMin, UZettaUserSettings::AgentTextSizeMax);
	if (Next == Settings->GetAgentTextSize()) return;
	Settings->AgentTextSize = Next;
	Settings->Save();
	ZettaAgentMarkdown::SetTextSize(Next);
	// Rich text copies its styles when it's built: draw every message again at the new size.
	if (List.IsValid()) List->ClearChildren();
	Rows.Reset();
	WelcomeWidget.Reset();
	LastCount = 0;
	RenderTranscript();
}

FReply SZettaAgentPage::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (HandleTextSizeKey(InKeyEvent)) return FReply::Handled();
	if (InKeyEvent.GetKey() == EKeys::Escape && FZettaAgentRuntime::Get().IsBusy())
	{
		FZettaAgentRuntime::Get().Stop();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SZettaAgentPage::Submit(bool bFromButton)
{
	FZettaAgentRuntime& Runtime = FZettaAgentRuntime::Get();
	if (bFromButton && Runtime.IsBusy())
	{
		Runtime.Stop();
		return;
	}
	// While the agent replies, Enter waits; while it waits on tools or a card, a new message takes over.
	if (Runtime.IsBusy() && Runtime.Phase() != FZettaAgentRuntime::EPhase::Tools) return;
	if (!FZettaAuth::Get().IsSignedIn()) return;

	FString Text = Input->GetText().ToString();
	if (Text.TrimStartAndEnd().IsEmpty() && Images.Num() == 0 && Files.Num() == 0 && Mentions.Num() == 0) return;

	// A model on the user's own key may not take pictures or PDFs: say so, and keep the attachments.
	FZettaAgentCatalog& Catalog = FZettaAgentCatalog::Get();
	const FZettaAgentModel* Own = Catalog.OwnKeys() ? Catalog.ActiveModel() : nullptr;
	if (Own && !Own->bVision && Images.Num() > 0)
	{
		Runtime.Notice(FText::Format(LOCTEXT("AgentUI.CantSee", "{0} can't see pictures. Remove them, or pick a model that can."), FText::FromString(Own->Label)).ToString());
		return;
	}
	const bool bHasPdf = Files.ContainsByPredicate([](const FZettaAttachment& F) { return F.Kind == TEXT("document"); });
	if (Own && !Own->bDocuments && bHasPdf)
	{
		Runtime.Notice(FText::Format(LOCTEXT("AgentUI.CantRead", "{0} can't read PDFs. Remove them, or pick a model that can."), FText::FromString(Own->Label)).ToString());
		return;
	}
	if (Text.TrimStartAndEnd().IsEmpty())
	{
		Text = Mentions.Num() > 0 && Images.Num() == 0 && Files.Num() == 0 ? TEXT("Take a look at what I tagged.") : TEXT("Take a look at what I attached.");
	}
	CloseMention();

	// Say what each picture is, and its name for the picture tools.
	if (ImageLabels.Num() > 0)
	{
		TArray<FString> Parts;
		for (int32 i = 0; i < ImageLabels.Num(); ++i)
		{
			Parts.Add(ImageKeys.IsValidIndex(i) && !ImageKeys[i].IsEmpty() ? FString::Printf(TEXT("%s [%s]"), *ImageLabels[i], *ImageKeys[i]) : ImageLabels[i]);
		}
		Text += TEXT("\n\n(Attached: ") + FString::Join(Parts, TEXT("; ")) + TEXT(")");
	}
	TArray<FZettaAgentFile> Documents;
	for (const FZettaAttachment& F : Files)
	{
		if (F.Kind == TEXT("text")) Text += ZettaAgentAttachments::TextBlock(F);
		if (F.Kind == TEXT("document")) Documents.Add(F.File);
	}
	Text += ZettaMentions::Block(Mentions);
	if (Text.Len() > 16000)
	{
		Runtime.Notice(LOCTEXT("AgentUI.MessageTooLong", "That message is too long to send (16,000 characters at most, attached text included). Shorten it or attach less.").ToString());
		return;
	}

	Input->SetText(FText::GetEmpty());
	Runtime.Send(Text, Images, Documents);
	Images.Reset();
	ImageLabels.Reset();
	ImageKeys.Reset();
	ImageBrushes.Reset();
	Files.Reset();
	Mentions.Reset();
	RenderAttachments();
	Panel = EPanel::None;
}

// ─── Drag and drop ────────────────────────────────────────────────────────────

void SZettaAgentPage::OnDragEnter(const FGeometry&, const FDragDropEvent& Event)
{
	const TSharedPtr<FExternalDragOperation> Files_ = Event.GetOperationAs<FExternalDragOperation>();
	bDragOver = ZettaMentions::DragHasObjects(Event) || (Files_.IsValid() && Files_->HasFiles());
}

void SZettaAgentPage::OnDragLeave(const FDragDropEvent&)
{
	bDragOver = false;
}

FReply SZettaAgentPage::OnDragOver(const FGeometry&, const FDragDropEvent& Event)
{
	const TSharedPtr<FExternalDragOperation> External = Event.GetOperationAs<FExternalDragOperation>();
	return ZettaMentions::DragHasObjects(Event) || (External.IsValid() && External->HasFiles()) ? FReply::Handled() : FReply::Unhandled();
}

FReply SZettaAgentPage::OnDrop(const FGeometry&, const FDragDropEvent& Event)
{
	bDragOver = false;
	// Actors from the Outliner and assets from the Content Browser are tagged; files from disk are attached.
	const TArray<FZettaMention> Dropped = ZettaMentions::FromDrag(Event);
	for (const FZettaMention& M : Dropped) AddMention(M);
	if (Dropped.Num() == 0)
	{
		const TSharedPtr<FExternalDragOperation> External = Event.GetOperationAs<FExternalDragOperation>();
		if (!External.IsValid() || !External->HasFiles()) return FReply::Unhandled();
		AddFiles(External->GetFiles());
	}
	SetSettingsVisible(false);
	FSlateApplication::Get().SetKeyboardFocus(Input);
	return FReply::Handled();
}

// ─── @ mentions ───────────────────────────────────────────────────────────────

TSharedRef<SWidget> SZettaAgentPage::MakeMentionPicker()
{
	return SNew(SBorder)
		.Visibility_Lambda([this]() { return bMentionOpen ? EVisibility::Visible : EVisibility::Collapsed; })
		.BorderImage(FZettaStyle::Get().GetBrush("Zetta.CardRaised"))
		.Padding(6)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(2, 0, 2, 4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
				[
					SNew(SBox).Visibility_Lambda([this]() { return MentionMode == TEXT("choose") ? EVisibility::Collapsed : EVisibility::Visible; })
					[
						SNew(SButton)
						.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
						.IsFocusable(false)
						.ToolTipText(LOCTEXT("AgentUI.MentionBack", "Back to Level / Project"))
						.OnClicked_Lambda([this]() { SetMentionMode(TEXT("choose")); FSlateApplication::Get().SetKeyboardFocus(Input); return FReply::Handled(); })
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("‹")))
						]
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(FZettaStyle::Font(9, true)).Text_Lambda([this]()
					{
						if (MentionMode == TEXT("choose")) return LOCTEXT("AgentUI.TagSomething", "Tag something for the agent");
						const FText Where = MentionMode == TEXT("level") ? LOCTEXT("AgentUI.MentionLevel", "Level") : LOCTEXT("AgentUI.MentionProject", "Project");
						return MentionQuery.IsEmpty() ? Where : FText::Format(LOCTEXT("AgentUI.MentionWhereQuery", "{0} · “{1}”"), Where, FText::FromString(MentionQuery));
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).MaxDesiredHeight(240)
				[
					SAssignNew(MentionScroll, SScrollBox) + SScrollBox::Slot()[SAssignNew(MentionList, SVerticalBox)]
				]
			]
		];
}

void SZettaAgentPage::UpdateMention()
{
	if (bSuppressMention || !Input.IsValid()) return;
	const TArray<FString> Lines = PageSplitLines(Input->GetText().ToString());
	const FTextLocation Cursor = Input->GetCursorLocation();
	if (!Cursor.IsValid() || !Lines.IsValidIndex(Cursor.GetLineIndex()))
	{
		CloseMention();
		return;
	}
	const FString& Line = Lines[Cursor.GetLineIndex()];
	const int32 Offset = FMath::Clamp(Cursor.GetOffset(), 0, Line.Len());
	int32 At = INDEX_NONE;
	for (int32 i = Offset - 1; i >= 0; --i)
	{
		const TCHAR C = Line[i];
		if (C == TEXT('@'))
		{
			if (i == 0 || FChar::IsWhitespace(Line[i - 1])) At = i;
			break;
		}
		if (FChar::IsWhitespace(C)) break;
	}
	if (At == INDEX_NONE)
	{
		CloseMention();
		return;
	}
	const FString Query = Line.Mid(At + 1, Offset - At - 1);
	const bool bSame = bMentionOpen && MentionLine == Cursor.GetLineIndex() && MentionAt == At && MentionQuery == Query;
	MentionLine = Cursor.GetLineIndex();
	MentionAt = At;
	MentionCursor = Offset;
	if (bSame) return;
	if (!bMentionOpen)
	{
		bMentionOpen = true;
		MentionMode = TEXT("choose");
	}
	MentionQuery = Query;
	MentionSelected = 0;
	RefreshMention();
}

void SZettaAgentPage::SetMentionMode(const FString& Mode)
{
	MentionMode = Mode;
	MentionSelected = 0;
	RefreshMention();
}

void SZettaAgentPage::CloseMention()
{
	if (!bMentionOpen) return;
	bMentionOpen = false;
	MentionResults.Reset();
	if (MentionList.IsValid()) MentionList->ClearChildren();
}

void SZettaAgentPage::RefreshMention()
{
	if (!MentionList.IsValid()) return;
	MentionList->ClearChildren();
	if (MentionMode == TEXT("choose"))
	{
		MentionResults.Reset();
		const TPair<FText, FText> Choices[] = {
			{ LOCTEXT("AgentUI.ChooseLevel", "Level"), LOCTEXT("AgentUI.ChooseLevelDetail", "Actors in the open level") },
			{ LOCTEXT("AgentUI.ChooseProject", "Project"), LOCTEXT("AgentUI.ChooseProjectDetail", "Assets in this project") },
		};
		for (int32 i = 0; i < 2; ++i)
		{
			const FString Mode = i == 0 ? TEXT("level") : TEXT("project");
			MentionList->AddSlot().AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor_Lambda([this, i]() { return MentionSelected == i ? FLinearColor(FZettaStyle::Accent().R, FZettaStyle::Accent().G, FZettaStyle::Accent().B, 0.35f) : FLinearColor::Transparent; })
				.Padding(0)
				[
					SNew(SButton)
					.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
					.IsFocusable(false)
					.ContentPadding(FMargin(8, 3))
					.OnClicked_Lambda([this, Mode]() { SetMentionMode(Mode); FSlateApplication::Get().SetKeyboardFocus(Input); return FReply::Handled(); })
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Choices[i].Key).Font(FZettaStyle::Font(9, true))]
						+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Choices[i].Value).Font(FZettaStyle::Font(8)).ColorAndOpacity(FSlateColor(FZettaStyle::Muted()))]
					]
				]
			];
		}
		return;
	}

	MentionResults = MentionMode == TEXT("level") ? ZettaMentions::SearchLevel(MentionQuery) : ZettaMentions::SearchProject(MentionQuery);
	MentionSelected = FMath::Clamp(MentionSelected, 0, FMath::Max(0, MentionResults.Num() - 1));
	if (MentionResults.Num() == 0)
	{
		MentionList->AddSlot().AutoHeight().Padding(8, 4)
		[
			ZettaUI::Hint(MentionMode == TEXT("level") ? LOCTEXT("AgentUI.NoActorMatches", "No actor in the open level matches.") : LOCTEXT("AgentUI.NoAssetMatches", "No asset in the project matches."))
		];
		return;
	}
	TSharedPtr<SWidget> SelectedRow;
	for (int32 i = 0; i < MentionResults.Num(); ++i)
	{
		TSharedRef<SWidget> Row = ZettaAgentCards::MentionRow(MentionResults[i], MakeAttributeLambda([this, i]() { return MentionSelected == i; }),
			FSimpleDelegate::CreateLambda([this, i]() { PickMention(i); }));
		if (i == MentionSelected) SelectedRow = Row;
		MentionList->AddSlot().AutoHeight()[Row];
	}
	if (SelectedRow.IsValid() && MentionScroll.IsValid()) MentionScroll->ScrollDescendantIntoView(SelectedRow, false, EDescendantScrollDestination::IntoView);
}

void SZettaAgentPage::PickMention(int32 Index)
{
	if (!MentionResults.IsValidIndex(Index)) return;
	const FZettaMention Picked = MentionResults[Index];
	// The "@query" typed to find it is removed — the chip above the message is the tag.
	TArray<FString> Lines = PageSplitLines(Input->GetText().ToString());
	if (Lines.IsValidIndex(MentionLine))
	{
		FString& Line = Lines[MentionLine];
		const int32 End = FMath::Clamp(MentionCursor, MentionAt, Line.Len());
		FString After = Line.Mid(End);
		if (MentionAt > 0 && After.StartsWith(TEXT(" ")) && FChar::IsWhitespace(Line[MentionAt - 1])) After.RightChopInline(1);
		Line = Line.Left(MentionAt) + After;
		bSuppressMention = true;
		Input->SetText(FText::FromString(FString::Join(Lines, TEXT("\n"))));
		Input->GoTo(FTextLocation(MentionLine, MentionAt));
		bSuppressMention = false;
	}
	CloseMention();
	AddMention(Picked);
	FSlateApplication::Get().SetKeyboardFocus(Input);
}

// ─── History ──────────────────────────────────────────────────────────────────

void SZettaAgentPage::TogglePanel(EPanel Which)
{
	Panel = Panel == Which ? EPanel::None : Which;
	if (Panel == EPanel::History) LoadHistory();
	if (Panel == EPanel::Memory) LoadMemory();
}

void SZettaAgentPage::LoadHistory()
{
	HistoryBody->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[ZettaUI::Section(LOCTEXT("AgentUI.HistoryTitle", "Conversations in this project"))]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				ZettaAgentCards::QuietButton(LOCTEXT("AgentUI.HistoryNewChat", "+ New chat"), FSimpleDelegate::CreateLambda([this]()
				{
					FZettaAgentRuntime::Get().NewChat();
					Panel = EPanel::None;
					FSlateApplication::Get().SetKeyboardFocus(Input);
				}))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				ZettaAgentCards::QuietButton(FText::FromString(TEXT("✕")), FSimpleDelegate::CreateLambda([this]() { Panel = EPanel::None; }), LOCTEXT("AgentUI.CloseHistory", "Close"))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(SSearchBox)
			.HintText(LOCTEXT("AgentUI.SearchConversations", "Search conversations"))
			.InitialText(FText::FromString(HistoryFilter))
			.OnTextChanged_Lambda([this](const FText& Text) { HistoryFilter = Text.ToString(); RenderHistory(); })
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(HistoryList, SVerticalBox)]
		]);
	HistoryList->AddSlot().AutoHeight()[ZettaUI::Hint(LOCTEXT("AgentUI.LoadingConversations", "Loading your conversations…"))];

	TWeakPtr<SZettaAgentPage> Weak = SharedThis(this);
	FZettaHttp::Get(FZettaAgentRuntime::Api(TEXT("/sessions") + PageQuery(TEXT("projectId"), PageProjectId())), [Weak](const FZettaResponse& R)
	{
		const TSharedPtr<SZettaAgentPage> Self = Weak.Pin();
		if (!Self.IsValid() || !Self->HistoryList.IsValid()) return;
		if (!R.bOk)
		{
			Self->HistoryList->ClearChildren();
			Self->HistoryList->AddSlot().AutoHeight()[ZettaUI::ErrorText(FText::FromString(ZettaCatalog::Explain(R)))];
			return;
		}
		Self->Sessions = ZettaJson::Objects(R.Object(), TEXT("sessions"));
		Self->RenderHistory();
	});
}

void SZettaAgentPage::RenderHistory()
{
	if (!HistoryList.IsValid()) return;
	HistoryList->ClearChildren();
	if (Sessions.Num() == 0)
	{
		HistoryList->AddSlot().AutoHeight()[ZettaUI::Hint(LOCTEXT("AgentUI.NoConversations", "No conversations yet — your chats in this project will appear here."))];
		return;
	}
	const FString Filter = HistoryFilter.TrimStartAndEnd();
	FString LastGroup;
	int32 Shown = 0;
	for (const TSharedPtr<FJsonObject>& Session : Sessions)
	{
		const FString Title = PageTitleOf(Session);
		if (!Filter.IsEmpty() && !Title.Contains(Filter)) continue;
		++Shown;
		const int64 Id = ZettaJson::Int(Session, TEXT("id"));
		const FDateTime When = PageLocalTime(ZettaJson::Str(Session, TEXT("lastMessageAt")));
		const FText Group = PageGroupOf(When);
		if (Group.ToString() != LastGroup)
		{
			LastGroup = Group.ToString();
			HistoryList->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
			[
				SNew(STextBlock).Text(Group).Font(FZettaStyle::Font(8, true)).ColorAndOpacity(FSlateColor(FZettaStyle::Muted()))
			];
		}
		const int64 Messages = ZettaJson::Int(Session, TEXT("messageCount"));
		const bool bToday = Group.ToString() == LOCTEXT("AgentUI.Today", "Today").ToString() || Group.ToString() == LOCTEXT("AgentUI.Yesterday", "Yesterday").ToString();
		FString Meta = When.GetTicks() == 0 ? FString() : When.ToString(bToday ? TEXT("%H:%M") : TEXT("%b %d, %H:%M"));
		if (Messages > 0) Meta += (Meta.IsEmpty() ? TEXT("") : TEXT(" · ")) + FString::Printf(TEXT("%lld message%s"), Messages, Messages == 1 ? TEXT("") : TEXT("s"));
		const bool bCurrent = Id == FZettaAgentRuntime::Get().SessionId();
		const bool bWaiting = ZettaJson::Bool(Session, TEXT("awaiting"));

		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
				.ContentPadding(FMargin(6, 3))
				.ToolTipText(FText::FromString(Title))
				.OnClicked_Lambda([this, Id, bCurrent]()
				{
					Panel = EPanel::None;
					if (!bCurrent) FZettaAgentRuntime::Get().OpenSession(Id);
					return FReply::Handled();
				})
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(FText::FromString(Title)).Font(FZettaStyle::Font(9, bCurrent)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(FText::FromString(Meta)).Font(FZettaStyle::Font(8)).ColorAndOpacity(FSlateColor(FZettaStyle::Muted()))
					]
				]
			];
		if (bCurrent) Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[ZettaAgentCards::Chip(LOCTEXT("AgentUI.OpenChip", "Open"), FZettaStyle::Accent())];
		if (bWaiting)
		{
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
			[
				SNew(SBox).ToolTipText(LOCTEXT("AgentUI.WaitingTip", "It paused waiting on the editor — open it to carry on"))
				[
					ZettaAgentCards::Chip(LOCTEXT("AgentUI.WaitingChip", "Waiting"), FZettaStyle::Warning())
				]
			];
		}
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
		[
			ZettaAgentCards::QuietButton(FText::FromString(TEXT("✕")), FSimpleDelegate::CreateLambda([this, Id, Title]() { DeleteSession(Id, Title); }),
				LOCTEXT("AgentUI.DeleteConversation", "Delete this conversation"))
		];
		HistoryList->AddSlot().AutoHeight()[Row];
	}
	if (Shown == 0) HistoryList->AddSlot().AutoHeight()[ZettaUI::Hint(LOCTEXT("AgentUI.NoMatch", "No conversation matches that search."))];
}

void SZettaAgentPage::DeleteSession(int64 Id, const FString& Title)
{
	const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::OkCancel,
		FText::Format(LOCTEXT("AgentUI.DeleteConversationBody", "Delete “{0}”? The memories it saved are kept."), FText::FromString(Title)),
		LOCTEXT("AgentUI.DeleteConversationTitle", "Delete conversation"));
	if (Answer != EAppReturnType::Ok) return;
	TWeakPtr<SZettaAgentPage> Weak = SharedThis(this);
	FZettaHttp::Delete(FZettaAgentRuntime::Api(FString::Printf(TEXT("/sessions/%lld"), Id)), [Weak, Id](const FZettaResponse& R)
	{
		if (!R.bOk)
		{
			ZettaAgentCards::Toast(FText::FromString(ZettaCatalog::Explain(R)), true);
			return;
		}
		if (Id == FZettaAgentRuntime::Get().SessionId()) FZettaAgentRuntime::Get().NewChat();
		if (const TSharedPtr<SZettaAgentPage> Self = Weak.Pin())
		{
			Self->Sessions.RemoveAll([Id](const TSharedPtr<FJsonObject>& S) { return ZettaJson::Int(S, TEXT("id")) == Id; });
			Self->RenderHistory();
		}
	});
}

// ─── Memory ───────────────────────────────────────────────────────────────────

void SZettaAgentPage::LoadMemory()
{
	MemoryBody->SetContent(ZettaUI::Hint(LOCTEXT("AgentUI.LoadingMemory", "Loading…")));
	TWeakPtr<SZettaAgentPage> Weak = SharedThis(this);
	FZettaHttp::Get(FZettaAgentRuntime::Api(TEXT("/memories") + PageQuery(TEXT("projectId"), PageProjectId())), [Weak](const FZettaResponse& R)
	{
		const TSharedPtr<SZettaAgentPage> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		if (!R.bOk)
		{
			Self->MemoryBody->SetContent(ZettaUI::ErrorText(FText::FromString(ZettaCatalog::Explain(R))));
			return;
		}
		Self->MemoryData = R.Object();
		Self->EditingMemory = 0;
		Self->RenderMemory();
	});
}

void SZettaAgentPage::RenderMemory()
{
	TWeakPtr<SZettaAgentPage> Weak = SharedThis(this);
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	// The brief: the agent's summary of the game, read every turn.
	TSharedPtr<SMultiLineEditableTextBox> Brief;
	Box->AddSlot().AutoHeight().Padding(0, 0, 0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[ZettaUI::Section(LOCTEXT("AgentUI.BriefTitle", "Project brief"))]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			ZettaAgentCards::QuietButton(FText::FromString(TEXT("✕")), FSimpleDelegate::CreateLambda([this]() { Panel = EPanel::None; }), LOCTEXT("AgentUI.CloseMemory", "Close"))
		]
	];
	Box->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
	[
		ZettaUI::Hint(LOCTEXT("AgentUI.BriefHint", "The agent's summary of this game — genre, core loop, systems, decisions. It reads this every turn."))
	];
	Box->AddSlot().AutoHeight()
	[
		SNew(SBox).MinDesiredHeight(60).MaxDesiredHeight(140)
		[
			SAssignNew(Brief, SMultiLineEditableTextBox)
			.Text(FText::FromString(ZettaJson::Str(MemoryData, TEXT("brief"))))
			.HintText(LOCTEXT("AgentUI.NoBrief", "No brief yet — the agent writes one as you build."))
			.AutoWrapText(true)
		]
	];
	TWeakPtr<SMultiLineEditableTextBox> WeakBrief = Brief;
	Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, 4, 0, 10)
	[
		ZettaUI::Button(LOCTEXT("AgentUI.SaveBrief", "Save brief"), FSimpleDelegate::CreateLambda([WeakBrief]()
		{
			const TSharedPtr<SMultiLineEditableTextBox> Field = WeakBrief.Pin();
			if (!Field.IsValid()) return;
			TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
			Body->SetStringField(TEXT("projectId"), PageProjectId());
			Body->SetStringField(TEXT("brief"), Field->GetText().ToString().Left(4000));
			FZettaHttp::Put(FZettaAgentRuntime::Api(TEXT("/brief")), Body, [](const FZettaResponse& R)
			{
				ZettaAgentCards::Toast(R.bOk ? LOCTEXT("AgentUI.BriefSaved", "Brief saved") : FText::FromString(ZettaCatalog::Explain(R)), !R.bOk);
			});
		}))
	];

	Box->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[ZettaUI::Section(LOCTEXT("AgentUI.RemembersTitle", "What the agent remembers"))];
	const TArray<TSharedPtr<FJsonObject>> Memories = ZettaJson::Objects(MemoryData, TEXT("memories"));
	if (Memories.Num() == 0)
	{
		Box->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
		[
			ZettaUI::Hint(LOCTEXT("AgentUI.NothingRemembered", "Nothing yet. Tell it your conventions (“we use Enhanced Input”, “Blueprints go in /Game/Blueprints”) and it will remember."))
		];
	}
	for (const TSharedPtr<FJsonObject>& Memory : Memories)
	{
		const int64 Id = ZettaJson::Int(Memory, TEXT("id"));
		const FString Text = ZettaJson::Str(Memory, TEXT("text"));
		const bool bGlobal = ZettaJson::Str(Memory, TEXT("scope")) == TEXT("global");
		const FString Category = ZettaJson::Str(Memory, TEXT("category"), TEXT("fact")).Replace(TEXT("_"), TEXT(" "));
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				ZettaAgentCards::Chip(bGlobal ? LOCTEXT("AgentUI.AllProjects", "All projects") : LOCTEXT("AgentUI.ThisProject", "This project"), FZettaStyle::Accent())
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[
				ZettaAgentCards::Chip(FText::FromString(Category), FZettaStyle::Muted())
			];
		if (EditingMemory == Id)
		{
			TSharedPtr<SEditableTextBox> Field;
			Row->AddSlot().FillWidth(1.f).VAlign(VAlign_Center)[SAssignNew(Field, SEditableTextBox).Text(FText::FromString(Text))];
			TWeakPtr<SEditableTextBox> WeakField = Field;
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
			[
				ZettaAgentCards::QuietButton(LOCTEXT("AgentUI.SaveMemory", "Save"), FSimpleDelegate::CreateLambda([Weak, WeakField, Id]()
				{
					const TSharedPtr<SEditableTextBox> F = WeakField.Pin();
					if (!F.IsValid() || F->GetText().IsEmptyOrWhitespace()) return;
					TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
					Body->SetStringField(TEXT("text"), F->GetText().ToString().TrimStartAndEnd().Left(300));
					FZettaHttp::Patch(FZettaAgentRuntime::Api(FString::Printf(TEXT("/memories/%lld"), Id)), Body, [Weak](const FZettaResponse& R)
					{
						if (!R.bOk) ZettaAgentCards::Toast(FText::FromString(ZettaCatalog::Explain(R)), true);
						if (const TSharedPtr<SZettaAgentPage> Self = Weak.Pin()) Self->LoadMemory();
					});
				}))
			];
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
			[
				ZettaAgentCards::QuietButton(LOCTEXT("AgentUI.CancelEdit", "Cancel"), FSimpleDelegate::CreateLambda([this]() { EditingMemory = 0; RenderMemory(); }))
			];
		}
		else
		{
			Row->AddSlot().FillWidth(1.f).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Text)).AutoWrapText(true)];
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
			[
				ZettaAgentCards::QuietButton(LOCTEXT("AgentUI.EditMemory", "Edit"), FSimpleDelegate::CreateLambda([this, Id]() { EditingMemory = Id; RenderMemory(); }),
					LOCTEXT("AgentUI.EditMemoryTip", "Change what it remembers"))
			];
		}
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
		[
			ZettaAgentCards::QuietButton(FText::FromString(TEXT("✕")), FSimpleDelegate::CreateLambda([Weak, Id]()
			{
				FZettaHttp::Delete(FZettaAgentRuntime::Api(FString::Printf(TEXT("/memories/%lld"), Id)), [Weak](const FZettaResponse& R)
				{
					if (!R.bOk) ZettaAgentCards::Toast(FText::FromString(ZettaCatalog::Explain(R)), true);
					if (const TSharedPtr<SZettaAgentPage> Self = Weak.Pin()) Self->LoadMemory();
				});
			}), LOCTEXT("AgentUI.ForgetMemory", "Forget this"))
		];
		Box->AddSlot().AutoHeight().Padding(0, 2)[Row];
	}

	// Something to remember, said by the user.
	TSharedPtr<SEditableTextBox> AddField;
	TSharedRef<FString> Scope = MakeShared<FString>(TEXT("project"));
	Box->AddSlot().AutoHeight().Padding(0, 8, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 4, 0)
		[
			SAssignNew(AddField, SEditableTextBox).HintText(LOCTEXT("AgentUI.AddMemoryHint", "Add something to remember…"))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
		[
			ZettaUI::Choice(Scope, []()
			{
				return TArray<ZettaCatalog::FChoice>{ { TEXT("project"), TEXT("This project"), FString() }, { TEXT("global"), TEXT("All projects"), FString() } };
			})
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			ZettaUI::Button(LOCTEXT("AgentUI.AddMemory", "Add"), FSimpleDelegate::CreateLambda([Weak, Scope, WeakAdd = TWeakPtr<SEditableTextBox>(AddField)]()
			{
				const TSharedPtr<SEditableTextBox> Field = WeakAdd.Pin();
				if (!Field.IsValid() || Field->GetText().IsEmptyOrWhitespace()) return;
				TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
				Body->SetStringField(TEXT("text"), Field->GetText().ToString().TrimStartAndEnd().Left(300));
				Body->SetStringField(TEXT("scope"), *Scope);
				Body->SetStringField(TEXT("category"), TEXT("preference"));
				Body->SetStringField(TEXT("projectId"), PageProjectId());
				FZettaHttp::Post(FZettaAgentRuntime::Api(TEXT("/memories")), Body, [Weak](const FZettaResponse& R)
				{
					if (!R.bOk) ZettaAgentCards::Toast(FText::FromString(ZettaCatalog::Explain(R)), true);
					if (const TSharedPtr<SZettaAgentPage> Self = Weak.Pin()) Self->LoadMemory();
				});
			}))
		]
	];

	MemoryBody->SetContent(SNew(SScrollBox) + SScrollBox::Slot()[Box]);
}

#undef LOCTEXT_NAMESPACE
