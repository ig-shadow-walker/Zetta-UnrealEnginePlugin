// Copyright Zetta 3D. See LICENSE.md.

#include "SZettaStudioPage.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "SZettaGenerateTool.h"
#include "SZettaImageStudioTool.h"
#include "SZettaJobsStrip.h"
#include "SZettaPostProcessTool.h"
#include "ZettaPages.h"
#include "ZettaStudio.h"
#include "ZettaStudioContext.h"
#include "ZettaStudioFork.h"
#include "ZettaStudioLink.h"
#include "ZettaStudioUI.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace
{
	const TCHAR* StudioConfigSection = TEXT("Zetta3D.Studio");

	/** Each area's hue (the web Studio's): identity, not a control. */
	FLinearColor StudioHue(uint32 Rgb) { return FLinearColor::FromSRGBColor(FColor((Rgb >> 16) & 0xFF, (Rgb >> 8) & 0xFF, Rgb & 0xFF)); }

	void StudioNotify(const FText& Text)
	{
		FNotificationInfo Info(Text);
		Info.ExpireDuration = 5.f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
}

TSharedRef<SWidget> ZettaPages::MakeStudioPage()
{
	return SNew(SZettaStudioPage);
}

void SZettaStudioPage::Construct(const FArguments& InArgs)
{
	TWeakPtr<SZettaStudioPage> Weak = SharedThis(this);
	auto UseFor3D = [Weak](const TArray<uint8>& Bytes, const FString& Name)
	{
		if (const TSharedPtr<SZettaStudioPage> Self = Weak.Pin()) Self->UseImageFor3D(Bytes, Name);
	};

	const FLinearColor Signal = FZettaStyle::Accent();
	const FLinearColor Violet = StudioHue(0x8B5CF6);
	const FLinearColor Lime = StudioHue(0x84CC16);
	const FLinearColor Amber = StudioHue(0xF59E0B);
	const FLinearColor Rose = StudioHue(0xF43F5E);
	const FLinearColor Magenta = StudioHue(0xD946EF);

	TSharedPtr<SZettaPostProcessTool> Segment, Retopology, SmartUv, Texture, AutoRig;
	Tools = {
		{ TEXT("Generate"), LOCTEXT("ToolGenerate", "Generate 3D"), Signal, SAssignNew(Generate, SZettaGenerateTool).Smart(false) },
		{ TEXT("Smart"), LOCTEXT("ToolSmart", "Smart 3D"), Violet, SAssignNew(Smart, SZettaGenerateTool).Smart(true) },
		{ TEXT("Segment"), LOCTEXT("ToolSegment", "Segment"), Lime, SAssignNew(Segment, SZettaSegmentTool) },
		{ TEXT("Retopology"), LOCTEXT("ToolRetopology", "Retopology"), Lime, SAssignNew(Retopology, SZettaRetopologyTool) },
		{ TEXT("SmartUv"), LOCTEXT("ToolSmartUv", "Smart UV"), Lime, SAssignNew(SmartUv, SZettaSmartUvTool) },
		{ TEXT("Texture"), LOCTEXT("ToolTexture", "Texture"), Amber, SAssignNew(Texture, SZettaTextureTool) },
		{ TEXT("AutoRig"), LOCTEXT("ToolAutoRig", "Auto Rig"), Rose, SAssignNew(AutoRig, SZettaAutoRigTool) },
		{ TEXT("ImageStudio"), LOCTEXT("ToolImageStudio", "Image Studio"), Magenta, SAssignNew(Images, SZettaImageStudioTool, UseFor3D) },
	};
	Finishing.Add(TEXT("Segment"), Segment);
	Finishing.Add(TEXT("Retopology"), Retopology);
	Finishing.Add(TEXT("SmartUv"), SmartUv);
	Finishing.Add(TEXT("Texture"), Texture);
	Finishing.Add(TEXT("AutoRig"), AutoRig);

	SAssignNew(Switcher, SWidgetSwitcher);
	for (const FTool& Tool : Tools) Switcher->AddSlot()[Tool.Widget.ToSharedRef()];

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[MakeBanner()]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[MakeToolList()]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SSeparator).Orientation(Orient_Vertical).Thickness(1.f)]
			+ SHorizontalBox::Slot().FillWidth(1.f)[Switcher.ToSharedRef()]
		]
		+ SVerticalBox::Slot().AutoHeight()[SNew(SSeparator).Thickness(1.f)]
		+ SVerticalBox::Slot().AutoHeight()[SNew(SZettaJobsStrip)]
	];

	FString Remembered;
	GConfig->GetString(StudioConfigSection, TEXT("LastTool"), Remembered, GEditorPerProjectIni);
	// A remembered tool that no longer exists opens the default one.
	const bool bKnown = Tools.ContainsByPredicate([&Remembered](const FTool& T) { return T.Id.ToString() == Remembered; });
	ShowTool(bKnown ? FName(*Remembered) : FName(TEXT("Generate")));

	ContextHandle = FZettaStudioContext::Get().OnChanged.AddSP(this, &SZettaStudioPage::OnContextChanged);
	LinkHandle = ZettaStudioLink::OnRequest().AddSP(this, &SZettaStudioPage::HandleLink);
	ImageHandle = ZettaStudio::OnImageSent().AddSP(this, &SZettaStudioPage::HandleImage);
	FZettaStudioContext::Get().EnsureLoaded();
	OnContextChanged();
	HandleLink();
	HandleImage();
}

SZettaStudioPage::~SZettaStudioPage()
{
	FZettaStudioContext::Get().OnChanged.Remove(ContextHandle);
	ZettaStudioLink::OnRequest().Remove(LinkHandle);
	ZettaStudio::OnImageSent().Remove(ImageHandle);
}

TSharedRef<SWidget> SZettaStudioPage::MakeBanner()
{
	auto Message = []()
	{
		const FZettaStudioContext& C = FZettaStudioContext::Get();
		return C.LoadError.IsEmpty() || C.bLoading
			? FString()
			: FString::Printf(TEXT("Some account details did not load (%s). Prices and plan limits may be missing — click to retry."), *C.LoadError);
	};
	return SNew(SBox)
		.Visibility_Lambda([Message]() { return ZettaStudioUI::Shown(!Message().IsEmpty()); })
		[
			SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
			.ContentPadding(FMargin(12, 6))
			.OnClicked_Lambda([]()
			{
				FZettaStudioContext::Get().Load();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor(FZettaStyle::Warning()))
				.Text_Lambda([Message]() { return FText::FromString(Message()); })
			]
		];
}

TSharedRef<SWidget> SZettaStudioPage::MakeToolButton(const FTool& Tool)
{
	const FName Id = Tool.Id;
	return SNew(SButton)
		.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
		.ContentPadding(0)
		.OnClicked_Lambda([this, Id]()
		{
			ShowTool(Id);
			return FReply::Handled();
		})
		[
			SNew(SBorder)
			.BorderImage_Lambda([this, Id]() { return Active == Id ? FZettaStyle::Get().GetBrush("Zetta.CardSelected") : FAppStyle::GetNoBrush(); })
			.Padding(FMargin(8, 5))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[
					SNew(SBox).WidthOverride(7).HeightOverride(7)
					[
						SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.AccentFill")).BorderBackgroundColor(FSlateColor(Tool.Hue))
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Tool.Label)
					.Font_Lambda([this, Id]() { return FZettaStyle::Font(10, Active == Id); })
					.ColorAndOpacity_Lambda([this, Id]() { return Active == Id ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground(); })
				]
			]
		];
}

TSharedRef<SWidget> SZettaStudioPage::MakeToolList()
{
	auto Heading = [](const FText& Text)
	{
		return SNew(STextBlock).Text(Text).Font(FZettaStyle::Font(8, true)).ColorAndOpacity(FSlateColor(FZettaStyle::Muted()));
	};
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	auto Add = [this, &List](FName Id)
	{
		for (const FTool& T : Tools)
		{
			if (T.Id == Id) List->AddSlot().AutoHeight().Padding(0, 0, 0, 2)[MakeToolButton(T)];
		}
	};
	List->AddSlot().AutoHeight().Padding(4, 0, 0, 4)[Heading(LOCTEXT("ListCreate", "CREATE"))];
	Add(TEXT("Generate"));
	Add(TEXT("Smart"));
	List->AddSlot().AutoHeight().Padding(4, 10, 0, 4)[Heading(LOCTEXT("ListFinish", "FINISH"))];
	Add(TEXT("Segment"));
	Add(TEXT("Retopology"));
	Add(TEXT("SmartUv"));
	Add(TEXT("Texture"));
	Add(TEXT("AutoRig"));
	List->AddSlot().AutoHeight().Padding(4, 10, 0, 4)[Heading(LOCTEXT("ListPictures", "PICTURES"))];
	Add(TEXT("ImageStudio"));

	return SNew(SBox).WidthOverride(150)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot().Padding(FMargin(6, 10))[List]
		];
}

void SZettaStudioPage::ShowTool(FName Id)
{
	const int32 Index = Tools.IndexOfByPredicate([Id](const FTool& T) { return T.Id == Id; });
	if (Index == INDEX_NONE || !Switcher.IsValid()) return;
	Active = Id;
	Switcher->SetActiveWidgetIndex(Index);
	GConfig->SetString(StudioConfigSection, TEXT("LastTool"), *Id.ToString(), GEditorPerProjectIni);
	Tools[Index].Widget->OnShown();
}

void SZettaStudioPage::OnContextChanged()
{
	for (const FTool& T : Tools) T.Widget->OnContextChanged();
}

void SZettaStudioPage::UseImageFor3D(const TArray<uint8>& Bytes, const FString& Name)
{
	ShowTool(TEXT("Generate"));
	Generate->SetImage(Bytes, Name);
}

void SZettaStudioPage::HandleLink()
{
	const TOptional<ZettaStudioLink::FRequest> Request = ZettaStudioLink::TakePending();
	if (!Request.IsSet()) return;
	const FZettaGenerationPtr& Source = Request->Source;

	if (Request->bFork)
	{
		const TOptional<ZettaStudioFork::FSetup> Setup = Source.IsValid() ? ZettaStudioFork::Of(*Source) : TOptional<ZettaStudioFork::FSetup>();
		if (!Setup.IsSet())
		{
			ShowTool(Request->Tool);
			StudioNotify(LOCTEXT("CannotFork", "Only text, picture and multi-view generations made on Zetta N1, N2.5, Np1 or Np1.5 can be forked."));
			return;
		}
		ShowTool(Setup->Tool);
		const TSharedPtr<SZettaGenerateTool> Target = Setup->Tool == FName(TEXT("Smart")) ? Smart : Generate;
		Target->ApplyFork(Setup.GetValue());
		return;
	}

	ShowTool(Request->Tool);
	if (const TSharedPtr<SZettaPostProcessTool>* Tool = Finishing.Find(Request->Tool))
	{
		if (Source.IsValid()) (*Tool)->SetSource(Source);
	}
}

void SZettaStudioPage::HandleImage()
{
	TArray<uint8> Bytes;
	FString Name;
	if (ZettaStudio::TakePendingImage(Bytes, Name)) UseImageFor3D(Bytes, Name);
}

#undef LOCTEXT_NAMESPACE
