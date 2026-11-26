// Copyright Zetta 3D. See LICENSE.md.

#include "SZettaWindow.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ISettingsModule.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Zetta3DEditorModule.h"
#include "ZettaAuth.h"
#include "ZettaConfig.h"
#include "ZettaJobs.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

void SZettaWindow::Construct(const FArguments& InArgs)
{
	Current = InArgs._InitialPage;
	ChildSlot
	[
		SAssignNew(Root, SWidgetSwitcher)
		+ SWidgetSwitcher::Slot()[MakeSignIn()]
		+ SWidgetSwitcher::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[MakeHeader()]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SSeparator).Thickness(1.f)]
			+ SVerticalBox::Slot().FillHeight(1.f)[SAssignNew(Pages, SWidgetSwitcher)]
		]
	];

	AuthHandle = FZettaAuth::Get().OnChanged.AddSP(this, &SZettaWindow::Rebuild);
	PagesHandle = FZetta3DEditorModule::Get().OnPagesChanged.AddSP(this, &SZettaWindow::RebuildTabs);
	ShowHandle = FZetta3DEditorModule::Get().OnShowPage.AddSP(this, &SZettaWindow::ShowPage);
	RebuildTabs();
	Rebuild();
}

SZettaWindow::~SZettaWindow()
{
	FZettaAuth::Get().OnChanged.Remove(AuthHandle);
	if (FModuleManager::Get().IsModuleLoaded("Zetta3DEditor"))
	{
		FZetta3DEditorModule::Get().OnPagesChanged.Remove(PagesHandle);
		FZetta3DEditorModule::Get().OnShowPage.Remove(ShowHandle);
	}
}

void SZettaWindow::Rebuild()
{
	const FZettaAuth::EState State = FZettaAuth::Get().GetState();
	// While a stored session is being checked, show the window rather than flashing the sign-in screen.
	Root->SetActiveWidgetIndex(State == FZettaAuth::EState::SignedIn || State == FZettaAuth::EState::Checking ? 1 : 0);
}

void SZettaWindow::RebuildTabs()
{
	if (!Tabs.IsValid() || !Pages.IsValid()) return;
	Tabs->ClearChildren();
	const TArray<FZettaPage>& Registered = FZetta3DEditorModule::Get().GetPages();
	for (const FZettaPage& Page : Registered)
	{
		const FName Id = Page.Id;
		Tabs->AddSlot().AutoWidth().Padding(0, 0, 2, 0)
		[
			SNew(SCheckBox)
			.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox"))
			.IsChecked_Lambda([this, Id]() { return Current == Id ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Id](ECheckBoxState) { ShowPage(Id); })
			[
				SNew(SBox).Padding(FMargin(12, 5))
				[
					SNew(STextBlock).Text(Page.Label).Font(FZettaStyle::Font(10, true))
				]
			]
		];
	}
	if (Current.IsNone() || !Registered.ContainsByPredicate([this](const FZettaPage& P) { return P.Id == Current; }))
	{
		Current = Registered.Num() > 0 ? Registered[0].Id : NAME_None;
	}
	ShowPage(Current);
}

void SZettaWindow::ShowPage(FName Id)
{
	const FZettaPage* Page = FZetta3DEditorModule::Get().GetPages().FindByPredicate([Id](const FZettaPage& P) { return P.Id == Id; });
	if (!Page || !Pages.IsValid()) return;
	Current = Id;
	if (!PageIndex.Contains(Id))
	{
		const TSharedRef<SWidget> Widget = Page->Build ? Page->Build() : SNullWidget::NullWidget;
		Built.Add(Id, Widget);
		PageIndex.Add(Id, Pages->GetNumWidgets());
		Pages->AddSlot()[Widget];
	}
	Pages->SetActiveWidgetIndex(PageIndex[Id]);
}

TSharedRef<SWidget> SZettaWindow::MakeHeader()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(FMargin(8, 6))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
			[
				SNew(SImage).Image(FZettaStyle::Get().GetBrush("Zetta.Icon"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SAssignNew(Tabs, SHorizontalBox)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0)
			[
				SNew(STextBlock)
				.Visibility_Lambda([]() { return FZettaJobs::Get().RunningCount() > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
				.Text_Lambda([]() { return FText::Format(LOCTEXT("JobsRunning", "{0} running"), FZettaJobs::Get().RunningCount()); })
				.ColorAndOpacity(FSlateColor(FZettaStyle::Accent()))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SComboButton)
				.HasDownArrow(true)
				.OnGetMenuContent(this, &SZettaWindow::MakeAccountMenu)
				.ButtonContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text_Lambda([]()
						{
							const FZettaAuth& Auth = FZettaAuth::Get();
							if (!Auth.IsSignedIn()) return LOCTEXT("Checking", "Checking your account…");
							return FText::Format(LOCTEXT("CreditsShort", "{0} credits"), ZettaUI::Credits(Auth.GetUser().Credits3D.Total()));
						})
					]
				]
			]
		];
}

TSharedRef<SWidget> SZettaWindow::MakeAccountMenu()
{
	FMenuBuilder Menu(true, nullptr);
	const FZettaAuth& Auth = FZettaAuth::Get();
	if (Auth.IsSignedIn())
	{
		const FZettaUser& U = Auth.GetUser();
		Menu.BeginSection("Account", FText::FromString(U.Name()));
		Menu.AddMenuEntry(FText::Format(LOCTEXT("Plan", "{0} plan"), FText::FromString(U.PlanLabel())), FText::FromString(U.Email), FSlateIcon(), FUIAction());
		Menu.AddMenuEntry(FText::Format(LOCTEXT("Credits3D", "3D credits: {0}"), ZettaUI::Credits(U.Credits3D.Total())), FText(), FSlateIcon(), FUIAction());
		Menu.AddMenuEntry(FText::Format(LOCTEXT("CreditsAi", "AI credits: {0}"), ZettaUI::Credits(U.CreditsAi.Total())), FText(), FSlateIcon(), FUIAction());
		Menu.EndSection();
	}
	Menu.BeginSection("Actions");
	Menu.AddMenuEntry(LOCTEXT("TopUp", "Plans and credits…"), LOCTEXT("TopUpTip", "Opens zetta3d.ai/pricing"), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]() { ZettaUI::OpenSite(TEXT("/pricing")); })));
	Menu.AddMenuEntry(LOCTEXT("Refresh", "Refresh account"), FText(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]() { FZettaAuth::Get().Refresh(); })));
	Menu.AddMenuEntry(LOCTEXT("Settings", "Settings…"), LOCTEXT("SettingsTip", "Project Settings › Plugins › Zetta 3D"), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "Plugins", "Zetta3D");
		})));
	Menu.AddMenuEntry(LOCTEXT("OwnKeys", "Your own API keys…"), LOCTEXT("OwnKeysTip", "Run the Zetta Agent on your own Gemini, OpenAI, DeepSeek, Claude, GLM or Meta key"), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]() { FZetta3DEditorModule::Get().OpenAgentSettings(); })));
	Menu.AddMenuEntry(LOCTEXT("SignOut", "Sign out"), FText(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]() { FZettaAuth::Get().SignOut(); })));
	Menu.EndSection();
	return Menu.MakeWidget();
}

TSharedRef<SWidget> SZettaWindow::MakeSignIn()
{
	auto Waiting = []() { return FZettaAuth::Get().GetState() == FZettaAuth::EState::WaitingForBrowser; };
	return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(24)
	[
		SNew(SBox).WidthOverride(420)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 12)
			[
				SNew(SImage).Image(FZettaStyle::Get().GetBrush("Zetta.Logo"))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 6)
			[
				ZettaUI::Title(LOCTEXT("Welcome", "Zetta 3D for Unreal"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 18)
			[
				SNew(STextBlock).AutoWrapText(true).Justification(ETextJustify::Center)
				.Text(LOCTEXT("WelcomeBody", "Make 3D models and pictures, bring them into your project, and build your game with the Zetta Agent — on your Zetta 3D account."))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SBox).Visibility_Lambda([Waiting]() { return Waiting() ? EVisibility::Collapsed : EVisibility::Visible; })
				[
					ZettaUI::PrimaryButton(LOCTEXT("SignIn", "Sign in with your browser"), FSimpleDelegate::CreateLambda([]() { FZettaAuth::Get().BeginSignIn(); }))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SVerticalBox).Visibility_Lambda([Waiting]() { return Waiting() ? EVisibility::Visible : EVisibility::Collapsed; })
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 8)[SNew(SThrobber)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
				[
					SNew(STextBlock).AutoWrapText(true).Justification(ETextJustify::Center)
					.Text(LOCTEXT("WaitingBrowser", "Finish in your browser: sign in on zetta3d.ai and click Connect Unreal Engine. This window updates on its own."))
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
					[
						ZettaUI::Button(LOCTEXT("OpenAgain", "Open the page again"), FSimpleDelegate::CreateLambda([]() { FZettaAuth::Get().BeginSignIn(); }))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
					[
						ZettaUI::Button(LOCTEXT("Cancel", "Cancel"), FSimpleDelegate::CreateLambda([]() { FZettaAuth::Get().CancelSignIn(); }))
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
			[
				ZettaUI::ErrorText(MakeAttributeLambda([]() { return FText::FromString(FZettaAuth::Get().GetLastError()); }))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 0)
			[
				ZettaUI::Hint(LOCTEXT("SignInHint", "The editor never sees your password. Your session is kept in your system's key store, not in the project."))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)
			[
				SNew(SButton).ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
				.OnClicked_Lambda([]() { ZettaUI::OpenSite(TEXT("/signup")); return FReply::Handled(); })
				[
					SNew(STextBlock).Text(LOCTEXT("NoAccount", "No account yet? Create one on zetta3d.ai")).ColorAndOpacity(FSlateColor(FZettaStyle::Accent()))
				]
			]
		]
	];
}

#undef LOCTEXT_NAMESPACE
