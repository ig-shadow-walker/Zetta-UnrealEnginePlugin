// Copyright Zetta 3D. See LICENSE.md.

#include "SZettaAgentSettings.h"

#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "ZettaAgentCards.h"
#include "ZettaAgentCatalog.h"
#include "ZettaSecureStore.h"
#include "ZettaSettings.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

void SZettaAgentSettings::Construct(const FArguments& InArgs)
{
	const FSimpleDelegate OnBack = InArgs._OnBack;
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(10, 8)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
			[
				ZettaAgentCards::QuietButton(LOCTEXT("AgentUI.BackToChat", "‹ Back to the chat"), OnBack)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				ZettaUI::Title(LOCTEXT("AgentUI.RunsOnTitle", "Zetta Agent — what it runs on"))
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot().Padding(14, 4, 14, 14)
			[
				SAssignNew(Body, SBox)
			]
		]
	];
	CatalogHandle = FZettaAgentCatalog::Get().OnChanged.AddSP(this, &SZettaAgentSettings::Rebuild);
	FZettaAgentCatalog::Get().Refresh();
	Rebuild();
}

SZettaAgentSettings::~SZettaAgentSettings()
{
	FZettaAgentCatalog::Get().OnChanged.Remove(CatalogHandle);
}

TSharedRef<SWidget> SZettaAgentSettings::Choice(bool bOwn, const FText& Title, const FText& Blurb)
{
	return SNew(SCheckBox)
		.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("RadioButton"))
		.IsChecked_Lambda([bOwn]() { return UZettaUserSettings::Get()->bAgentUseOwnKeys == bOwn ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([this, bOwn](ECheckBoxState)
		{
			UZettaUserSettings* Settings = UZettaUserSettings::Get();
			if (Settings->bAgentUseOwnKeys == bOwn) return;
			Settings->bAgentUseOwnKeys = bOwn;
			Settings->Save();
			// The menus under the message box show the same choice; they and this page redraw from the event.
			FZettaAgentCatalog::Get().OnChanged.Broadcast();
		})
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Title).Font(FZettaStyle::Font(10, true))]
			+ SVerticalBox::Slot().AutoHeight()[ZettaUI::Hint(Blurb)]
		];
}

void SZettaAgentSettings::Rebuild()
{
	FZettaAgentCatalog& Catalog = FZettaAgentCatalog::Get();
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	if (!Catalog.OffersOwnKeys())
	{
		Box->AddSlot().AutoHeight()
		[
			ZettaUI::Hint(LOCTEXT("AgentUI.OwnKeysUnavailable", "The agent runs on your Zetta account. Using your own API keys isn't available on this server yet."))
		];
		Body->SetContent(Box);
		return;
	}

	TArray<FString> Names;
	for (const FZettaAgentProvider& P : Catalog.Providers()) Names.Add(P.Label);
	const FString List = Names.Num() > 1 ? FString::Join(TArrayView<const FString>(Names.GetData(), Names.Num() - 1), TEXT(", ")) + TEXT(" or ") + Names.Last() : Names[0];

	Box->AddSlot().AutoHeight().Padding(0, 4)
	[
		Choice(false, LOCTEXT("AgentUI.ZettaAccount", "Zetta Account"),
			LOCTEXT("AgentUI.ZettaAccountBlurb", "Included with your plan. Auto picks the model and how much it thinks for each message."))
	];
	Box->AddSlot().AutoHeight().Padding(0, 4, 0, 10)
	[
		Choice(true, LOCTEXT("AgentUI.OwnKeys", "Your own API keys"),
			FText::Format(LOCTEXT("AgentUI.OwnKeysBlurb", "Use your key from {0}. Billed by the provider, not in Zetta credits."), FText::FromString(List)))
	];

	if (UZettaUserSettings::Get()->bAgentUseOwnKeys)
	{
		if (Catalog.KeyedProviders().Num() == 0)
		{
			Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FSlateColor(FZettaStyle::Warning()))
				.Text(LOCTEXT("AgentUI.PasteToStart", "Paste a key for any provider below to start."))
			];
		}
		for (const FZettaAgentProvider& Provider : Catalog.Providers())
		{
			Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[KeyRow(Provider)];
		}
		Box->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
		[
			ZettaUI::Hint(FText::Format(LOCTEXT("AgentUI.KeysNote",
				"Keys are kept in {0}. They are sent to Zetta 3D only to run your messages, and are never saved on our servers. "
				"What you use is billed by the provider, not in Zetta credits; making 3D models and pictures still uses Zetta credits. "
				"Pick the provider and model under the agent's message box."), ZettaSecureStore::Describe()))
		];
	}
	Body->SetContent(Box);
}

TSharedRef<SWidget> SZettaAgentSettings::KeyRow(const FZettaAgentProvider& Provider)
{
	FZettaAgentCatalog& Catalog = FZettaAgentCatalog::Get();
	const FString Key = Provider.Key;
	const FString Label = Provider.Label;
	const FString KeyUrl = Provider.KeyUrl;
	FKeyState& State = States.FindOrAdd(Key);
	const bool bSaved = Catalog.HasKey(Key);

	TSharedRef<SHorizontalBox> Title = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
		[
			SNew(STextBlock).Text(FText::FromString(Label)).Font(FZettaStyle::Font(10, true))
		];
	if (bSaved)
	{
		Title->AddSlot().AutoWidth().VAlign(VAlign_Center)[ZettaAgentCards::Chip(LOCTEXT("AgentUI.KeySaved", "✓ Key saved"), FZettaStyle::Success())];
	}

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)[Title];
	if (Key == TEXT("glm"))
	{
		// Z.ai sells two kinds of key; a Coding Plan key is refused by its API for anything but its own coding tools.
		Box->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
		[
			ZettaUI::Hint(LOCTEXT("AgentUI.GlmKeyHint", "Use a Standard API Key from Z.ai's API Keys page. A GLM Coding Plan key doesn't work here."))
		];
	}

	if (bSaved && !State.bEditing)
	{
		Box->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				ZettaUI::Button(LOCTEXT("AgentUI.CheckKey", "Check key"), FSimpleDelegate::CreateLambda([this, Key]() { Check(Key); }),
					MakeAttributeLambda([this, Key]() { const FKeyState* S = States.Find(Key); return !S || !S->bChecking; }))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				ZettaUI::Button(LOCTEXT("AgentUI.Replace", "Replace"), FSimpleDelegate::CreateLambda([this, Key]()
				{
					States.FindOrAdd(Key).bEditing = true;
					Rebuild();
				}))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				ZettaUI::Button(LOCTEXT("AgentUI.RemoveKey", "Remove"), FSimpleDelegate::CreateLambda([this, Key]()
				{
					FKeyState& S = States.FindOrAdd(Key);
					S.Status.Reset();
					S.bEditing = false;
					FZettaAgentCatalog::Get().RemoveKey(Key);
				}))
			]
		];
	}
	else
	{
		TSharedPtr<SEditableTextBox> Field;
		TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 4, 0)
			[
				SAssignNew(Field, SEditableTextBox)
				.IsPassword(true)
				.HintText(FText::Format(LOCTEXT("AgentUI.PasteKey", "Paste your {0} API key"), FText::FromString(Label)))
			];
		TWeakPtr<SEditableTextBox> WeakField = Field;
		Line->AddSlot().AutoWidth()
		[
			ZettaUI::PrimaryButton(LOCTEXT("AgentUI.SaveKey", "Save"), FSimpleDelegate::CreateLambda([this, Key, WeakField]()
			{
				if (TSharedPtr<SEditableTextBox> F = WeakField.Pin())
				{
					const FString Typed = F->GetText().ToString();
					F->SetText(FText::GetEmpty());
					Save(Key, Typed);
				}
			}))
		];
		if (bSaved)
		{
			Line->AddSlot().AutoWidth().Padding(4, 0, 0, 0)
			[
				ZettaUI::Button(LOCTEXT("AgentUI.CancelReplace", "Cancel"), FSimpleDelegate::CreateLambda([this, Key]()
				{
					States.FindOrAdd(Key).bEditing = false;
					Rebuild();
				}))
			];
		}
		Box->AddSlot().AutoHeight()[Line];
		if (!KeyUrl.IsEmpty())
		{
			Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, 2, 0, 0)
			[
				ZettaAgentCards::QuietButton(FText::Format(LOCTEXT("AgentUI.GetKey", "Get a {0} key ↗"), FText::FromString(Label)),
					FSimpleDelegate::CreateLambda([KeyUrl]()
					{
						if (KeyUrl.StartsWith(TEXT("https://"))) FPlatformProcess::LaunchURL(*KeyUrl, nullptr, nullptr);
					}))
			];
		}
	}

	Box->AddSlot().AutoHeight().Padding(0, 2, 0, 0)
	[
		SNew(STextBlock).AutoWrapText(true)
		.Text_Lambda([this, Key]() { const FKeyState* S = States.Find(Key); return S ? FText::FromString(S->Status) : FText(); })
		.ColorAndOpacity_Lambda([this, Key]()
		{
			const FKeyState* S = States.Find(Key);
			return FSlateColor(!S || S->bChecking ? FZettaStyle::Muted() : S->bError ? FZettaStyle::Error() : FZettaStyle::Success());
		})
	];
	return ZettaUI::Card(Box, 10.f);
}

void SZettaAgentSettings::Save(const FString& Provider, const FString& Key)
{
	FKeyState& State = States.FindOrAdd(Provider);
	const FString Problem = FZettaAgentCatalog::Get().SetKey(Provider, Key);
	if (!Problem.IsEmpty())
	{
		State.Status = Problem;
		State.bError = true;
		return;
	}
	State.bEditing = false;
	// The first key saved: the agent runs on it, and the provider is the one picked.
	UZettaUserSettings* Settings = UZettaUserSettings::Get();
	if (Settings->AgentProvider.IsEmpty() || !FZettaAgentCatalog::Get().HasKey(Settings->AgentProvider))
	{
		Settings->AgentProvider = Provider;
		Settings->Save();
	}
	Rebuild();
	Check(Provider);
}

void SZettaAgentSettings::Check(const FString& Provider)
{
	FKeyState& State = States.FindOrAdd(Provider);
	State.Status = LOCTEXT("AgentUI.Checking", "Checking…").ToString();
	State.bError = false;
	State.bChecking = true;
	TWeakPtr<SZettaAgentSettings> Weak = SharedThis(this);
	FZettaAgentCatalog::Get().CheckKey(Provider, [Weak, Provider](bool bOk, const FString& Message)
	{
		const TSharedPtr<SZettaAgentSettings> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		FKeyState& S = Self->States.FindOrAdd(Provider);
		S.bChecking = false;
		S.bError = !bOk;
		S.Status = Message;
	});
}

#undef LOCTEXT_NAMESPACE
