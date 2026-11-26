// Copyright Zetta 3D. See LICENSE.md.

#include "SZettaSubmitBar.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "ZettaJobs.h"
#include "ZettaSettings.h"
#include "ZettaStudioContext.h"
#include "ZettaStudioUI.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

void SZettaSubmitBar::Construct(const FArguments& InArgs)
{
	ButtonText = InArgs._ButtonText;
	bPlace = UZettaProjectSettings::Get()->bPlaceInLevel;

	TSharedRef<SWidget> Choices = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(14.f, 2.f))
		+ SWrapBox::Slot()
		[
			ZettaStudioUI::Check(LOCTEXT("AutoImport", "Import into the project when ready"),
				[this]() { return bAutoImport; }, [this](bool b) { bAutoImport = b; },
				LOCTEXT("AutoImportTip", "Download the finished model into your project automatically."))
		]
		+ SWrapBox::Slot()
		[
			ZettaStudioUI::Check(LOCTEXT("PlaceInLevel", "Place in the open level"),
				[this]() { return bPlace && bAutoImport; }, [this](bool b) { bPlace = b; },
				LOCTEXT("PlaceInLevelTip", "Drop the imported model into the open level, resting on the ground in front of the camera."),
				MakeAttributeLambda([this]() { return bAutoImport; }))
		];

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
		.Padding(FMargin(12, 8))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[
				SNew(SBox).Visibility(InArgs._ShowImportChoices ? EVisibility::Visible : EVisibility::Collapsed)[Choices]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(LOCTEXT("Total", "Total")).ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).HAlign(HAlign_Right)
				[
					SNew(STextBlock).Text(this, &SZettaSubmitBar::CostText).Font(FZettaStyle::Font(11, true))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
			[
				SNew(STextBlock)
				.Text(this, &SZettaSubmitBar::BalanceText)
				.AutoWrapText(true)
				.Font(FZettaStyle::Font(9))
				.ColorAndOpacity_Lambda([this]() { return FSlateColor(Shortfall() ? FZettaStyle::Warning() : FZettaStyle::Muted()); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
			[
				ZettaStudioUI::Warn(MakeAttributeLambda([this]() { return FText::FromString(Problem); }))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
			[
				ZettaStudioUI::Error(MakeAttributeLambda([this]() { return FText::FromString(Error); }))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
			[
				ZettaStudioUI::Success(MakeAttributeLambda([this]() { return FText::FromString(Done); }))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
				.HAlign(HAlign_Center)
				.ContentPadding(FMargin(14, 6))
				.IsEnabled_Lambda([this]() { return !bBusy && Problem.IsEmpty() && static_cast<bool>(Action); })
				.ToolTipText_Lambda([this]() { return FText::FromString(Problem); })
				.OnClicked_Lambda([this]()
				{
					Submit();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Font(FZettaStyle::Font(10, true))
					.Text_Lambda([this]() { return bBusy && !Stage.IsEmpty() ? FText::FromString(Stage) : ButtonText.Get(); })
				]
			]
		]
	];
}

FText SZettaSubmitBar::CostText() const
{
	if (!Price.IsSet()) return FText::FromString(TEXT("—"));
	if (Price.GetValue() == 0) return LOCTEXT("Free", "Free");
	return FText::Format(LOCTEXT("CostCredits", "{0} credits"), FText::AsNumber(Price.GetValue()));
}

bool SZettaSubmitBar::Shortfall() const
{
	return Price.IsSet() && FZettaStudioContext::Get().Credits() < Price.GetValue();
}

FText SZettaSubmitBar::BalanceText() const
{
	const int64 Credits = FZettaStudioContext::Get().Credits();
	if (Shortfall())
	{
		return FText::Format(LOCTEXT("BalanceShort", "You have {0} credits — this needs {1}."), FText::AsNumber(Credits), FText::AsNumber(Price.GetValue()));
	}
	return FText::Format(LOCTEXT("Balance", "You have {0} credits"), FText::AsNumber(Credits));
}

void SZettaSubmitBar::Submit()
{
	if (bBusy || !Action || !Problem.IsEmpty()) return;
	bBusy = true;
	ClearMessages();
	TWeakPtr<SZettaSubmitBar> Weak = SharedThis(this);
	Action([Weak](const FString& Message)
	{
		const TSharedPtr<SZettaSubmitBar> Self = Weak.Pin();
		if (!Self) return;
		Self->bBusy = false;
		Self->Stage.Reset();
		if (!Message.IsEmpty()) Self->Error = Message;
	});
}

void SZettaSubmitBar::Track(const FZettaGenerationPtr& Generation)
{
	if (!Generation.IsValid()) return;
	FZettaImportOptions Options;
	Options.bPlace = bPlace && bAutoImport;
	FZettaJobs::Get().Track(Generation, bAutoImport, Options, TEXT("studio"));
	Done = TEXT("Started. Follow it in the jobs list below.");
	FZettaStudioContext::RefreshCredits();
}

#undef LOCTEXT_NAMESPACE
