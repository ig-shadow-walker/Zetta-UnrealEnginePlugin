// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaStudioUI.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace ZettaStudioUI
{
	TSharedRef<SWidget> Segmented(TFunction<FString()> Get, TFunction<void(const FString&)> Set, const TArray<FOption>& Options, FLock Lock)
	{
		TSharedRef<SWrapBox> Box = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(2.f, 2.f));
		for (const FOption& O : Options)
		{
			const FString Key = O.Key;
			const FText Tip = O.Tooltip;
			Box->AddSlot()
			[
				SNew(SCheckBox)
				.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox"))
				.IsEnabled_Lambda([Lock, Key]() { return !Lock || Lock(Key).IsEmpty(); })
				.ToolTipText_Lambda([Lock, Key, Tip]()
				{
					const FString Why = Lock ? Lock(Key) : FString();
					return Why.IsEmpty() ? Tip : FText::FromString(Why);
				})
				.IsChecked_Lambda([Get, Key]() { return Get() == Key ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([Get, Set, Key](ECheckBoxState)
				{
					if (Get() != Key) Set(Key);
				})
				[
					SNew(SBox).Padding(FMargin(10, 3)).HAlign(HAlign_Center)
					[
						SNew(STextBlock).Text(O.Label)
					]
				]
			];
		}
		return Box;
	}

	TSharedRef<SWidget> Dropdown(TFunction<FString()> Get, TFunction<void(const FString&)> Set, TFunction<TArray<FOption>()> Options, FLock Lock)
	{
		return SNew(SComboButton)
			.OnGetMenuContent_Lambda([Get, Set, Options, Lock]()
			{
				FMenuBuilder Menu(true, nullptr);
				for (const FOption& O : Options())
				{
					const FString Key = O.Key;
					const FString Why = Lock ? Lock(Key) : FString();
					Menu.AddMenuEntry(O.Label, Why.IsEmpty() ? O.Tooltip : FText::FromString(Why), FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateLambda([Set, Key]() { Set(Key); }),
							FCanExecuteAction::CreateLambda([Lock, Key]() { return !Lock || Lock(Key).IsEmpty(); }),
							FIsActionChecked::CreateLambda([Get, Key]() { return Get() == Key; })),
						NAME_None, EUserInterfaceActionType::RadioButton);
				}
				return Menu.MakeWidget();
			})
			.ButtonContent()
			[
				SNew(STextBlock).Text_Lambda([Get, Options]()
				{
					const FString Current = Get();
					for (const FOption& O : Options()) if (O.Key == Current) return O.Label;
					return FText::FromString(Current);
				})
			];
	}

	TSharedRef<SWidget> Check(const TAttribute<FText>& Label, TFunction<bool()> Get, TFunction<void(bool)> Set, const TAttribute<FText>& Tooltip, const TAttribute<bool>& Enabled)
	{
		return SNew(SCheckBox)
			.IsEnabled(Enabled)
			.ToolTipText(Tooltip)
			.IsChecked_Lambda([Get]() { return Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([Set](ECheckBoxState S) { Set(S == ECheckBoxState::Checked); })
			[
				SNew(STextBlock).Text(Label).AutoWrapText(true)
			];
	}

	TSharedRef<SWidget> FieldLabel(const TAttribute<FText>& Text)
	{
		return SNew(STextBlock).Text(Text).Font(FZettaStyle::Font(9, true)).ColorAndOpacity(FSlateColor::UseSubduedForeground());
	}

	TSharedRef<SWidget> Field(const TAttribute<FText>& Label, const TSharedRef<SWidget>& Control)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)[FieldLabel(Label)]
			+ SVerticalBox::Slot().AutoHeight()[Control];
	}

	static TSharedRef<SWidget> Line(const TAttribute<FText>& Text, const TAttribute<FSlateColor>& Color, int32 Size = 9)
	{
		return SNew(STextBlock)
			.Text(Text)
			.AutoWrapText(true)
			.Font(FZettaStyle::Font(Size))
			.ColorAndOpacity(Color)
			.Visibility_Lambda([Text]() { return Text.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; });
	}

	TSharedRef<SWidget> Hint(const TAttribute<FText>& Text) { return Line(Text, FSlateColor(FZettaStyle::Muted())); }
	TSharedRef<SWidget> Warn(const TAttribute<FText>& Text) { return Line(Text, FSlateColor(FZettaStyle::Warning())); }
	TSharedRef<SWidget> Success(const TAttribute<FText>& Text) { return Line(Text, FSlateColor(FZettaStyle::Success()), 10); }
	TSharedRef<SWidget> Error(const TAttribute<FText>& Text) { return Line(Text, FSlateColor(FZettaStyle::Error()), 10); }

	TSharedRef<SWidget> Locked(const TAttribute<FText>& Reason)
	{
		return SNew(SHorizontalBox)
			.Visibility_Lambda([Reason]() { return Reason.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Reason).AutoWrapText(true).Font(FZettaStyle::Font(9)).ColorAndOpacity(FSlateColor(FZettaStyle::Warning()))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[
				SNew(SHyperlink)
				.Text(LOCTEXT("SeePlans", "See plans"))
				.ToolTipText(LOCTEXT("SeePlansTip", "Opens zetta3d.ai/pricing"))
				.OnNavigate_Lambda([]() { ZettaUI::OpenSite(TEXT("/pricing")); })
			];
	}

	TSharedRef<SWidget> SmallButton(const TAttribute<FText>& Text, TFunction<void()> OnClicked, const TAttribute<bool>& Enabled, const TAttribute<FText>& Tooltip)
	{
		return SNew(SButton)
			.ContentPadding(FMargin(8, 2))
			.IsEnabled(Enabled)
			.ToolTipText(Tooltip)
			.OnClicked_Lambda([OnClicked]()
			{
				if (OnClicked) OnClicked();
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(Text).Font(FZettaStyle::Font(9))
			];
	}

	TSharedRef<SWidget> ShownWhen(TFunction<bool()> When, const TSharedRef<SWidget>& Content)
	{
		return SNew(SBox).Visibility_Lambda([When]() { return When() ? EVisibility::Visible : EVisibility::Collapsed; })[Content];
	}

	TSharedRef<SWidget> Group(const FText& Title, const TSharedRef<SWidget>& Content)
	{
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		if (!Title.IsEmpty()) Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[ZettaUI::Section(Title)];
		Box->AddSlot().AutoHeight()[Content];
		return ZettaUI::Card(Box, 10.f);
	}

	TSharedRef<SWidget> Chip(const FText& Text)
	{
		return SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Chip")).Padding(FMargin(7, 1))
			[
				SNew(STextBlock).Text(Text).Font(FZettaStyle::Font(8))
			];
	}

	FString Number(int64 Value) { return FText::AsNumber(Value).ToString(); }

	FString PriceTag(const TOptional<int32>& Credits)
	{
		if (!Credits.IsSet()) return FString();
		return Credits.GetValue() == 0 ? FString(TEXT("Free")) : FString::Printf(TEXT("+%d"), Credits.GetValue());
	}
}

#undef LOCTEXT_NAMESPACE
