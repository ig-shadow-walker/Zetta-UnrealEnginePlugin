// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaStyle.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/StyleColors.h"

TSharedPtr<FSlateStyleSet> FZettaStyle::Instance;

FName FZettaStyle::GetStyleSetName() { return TEXT("ZettaStyle"); }

FLinearColor FZettaStyle::Accent() { return FLinearColor::FromSRGBColor(FColor(0x16, 0x77, 0xFF)); }
FLinearColor FZettaStyle::AccentHover() { return FLinearColor::FromSRGBColor(FColor(0x3D, 0x8D, 0xFF)); }
FLinearColor FZettaStyle::Panel() { return FLinearColor::FromSRGBColor(FColor(0x1E, 0x1E, 0x1E)); }
FLinearColor FZettaStyle::PanelRaised() { return FLinearColor::FromSRGBColor(FColor(0x26, 0x26, 0x26)); }
FLinearColor FZettaStyle::Border() { return FLinearColor::FromSRGBColor(FColor(0x38, 0x38, 0x38)); }
FLinearColor FZettaStyle::Muted() { return FLinearColor::FromSRGBColor(FColor(0x9A, 0x96, 0x8E)); }
FLinearColor FZettaStyle::Success() { return FLinearColor::FromSRGBColor(FColor(0x3F, 0xB9, 0x6B)); }
FLinearColor FZettaStyle::Warning() { return FLinearColor::FromSRGBColor(FColor(0xE8, 0xA3, 0x3D)); }
FLinearColor FZettaStyle::Error() { return FLinearColor::FromSRGBColor(FColor(0xE5, 0x53, 0x4B)); }

FSlateFontInfo FZettaStyle::Font(int32 Size, bool bBold)
{
	return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
}

void FZettaStyle::Initialize()
{
	if (Instance.IsValid()) return;
	Instance = MakeShared<FSlateStyleSet>(GetStyleSetName());

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Zetta3D"));
	if (Plugin.IsValid()) Instance->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources"));

	Instance->Set("Zetta.Icon", new FSlateImageBrush(Instance->RootToContentDir(TEXT("ZettaMark"), TEXT(".png")), FVector2D(16, 16)));
	Instance->Set("Zetta.Logo", new FSlateImageBrush(Instance->RootToContentDir(TEXT("Icon128"), TEXT(".png")), FVector2D(48, 48)));

	Instance->Set("Zetta.Card", new FSlateRoundedBoxBrush(Panel(), 6.f, Border(), 1.f));
	Instance->Set("Zetta.CardRaised", new FSlateRoundedBoxBrush(PanelRaised(), 6.f, Border(), 1.f));
	Instance->Set("Zetta.CardSelected", new FSlateRoundedBoxBrush(PanelRaised(), 6.f, Accent(), 2.f));
	Instance->Set("Zetta.Chip", new FSlateRoundedBoxBrush(PanelRaised(), 10.f, Border(), 1.f));
	Instance->Set("Zetta.AccentFill", new FSlateRoundedBoxBrush(Accent(), 4.f));
	Instance->Set("Zetta.Bubble.User", new FSlateRoundedBoxBrush(FLinearColor::FromSRGBColor(FColor(0x1B, 0x3A, 0x63)), 10.f));
	Instance->Set("Zetta.Bubble.Agent", new FSlateRoundedBoxBrush(FLinearColor::Transparent, 0.f));
	Instance->Set("Zetta.Thumb", new FSlateRoundedBoxBrush(FLinearColor::FromSRGBColor(FColor(0x14, 0x14, 0x14)), 4.f));

	// The ✕ on pictures: a dark round backdrop so it reads on any picture, light or dark.
	FButtonStyle Close = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton");
	Close.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.f, 0.f, 0.f, 0.72f), 10.f, FLinearColor(1.f, 1.f, 1.f, 0.25f), 1.f));
	Close.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.f, 0.f, 0.f, 0.9f), 10.f, FLinearColor(1.f, 1.f, 1.f, 0.6f), 1.f));
	Close.SetPressed(FSlateRoundedBoxBrush(Error(), 10.f, FLinearColor(1.f, 1.f, 1.f, 0.6f), 1.f));
	Close.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
	Instance->Set("Zetta.Button.Close", Close);

	FButtonStyle Primary = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton");
	Instance->Set("Zetta.Button.Primary", Primary);

	FTextBlockStyle Title = FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
	Title.SetFont(Font(14, true));
	Instance->Set("Zetta.Text.Title", Title);
	FTextBlockStyle Section = FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
	Section.SetFont(Font(11, true));
	Instance->Set("Zetta.Text.Section", Section);
	FTextBlockStyle Hint = FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
	Hint.SetFont(Font(9));
	Hint.SetColorAndOpacity(FSlateColor(Muted()));
	Instance->Set("Zetta.Text.Hint", Hint);
	FTextBlockStyle ErrorText = FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
	ErrorText.SetColorAndOpacity(FSlateColor(Error()));
	Instance->Set("Zetta.Text.Error", ErrorText);

	FSlateStyleRegistry::RegisterSlateStyle(*Instance);
}

void FZettaStyle::Shutdown()
{
	if (!Instance.IsValid()) return;
	FSlateStyleRegistry::UnRegisterSlateStyle(*Instance);
	Instance.Reset();
}

const ISlateStyle& FZettaStyle::Get()
{
	check(Instance.IsValid());
	return *Instance;
}
