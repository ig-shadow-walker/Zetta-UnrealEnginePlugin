// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaPipelineUI.h"

#include "DragAndDrop/AssetDragDropOp.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Input/DragAndDrop.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "ZettaApi.h"
#include "ZettaCatalog.h"
#include "ZettaImageCache.h"
#include "ZettaPipelineJson.h"
#include "ZettaPipelineServices.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

using namespace ZettaPJson;

// ─── Overlay ────────────────────────────────────────────────────────────────────────────────────────────────────────

TFunction<void()> FZettaPlOverlay::Show(const TSharedRef<SWidget>& Card) const
{
	const TSharedPtr<SOverlay> Overlay = Host.Pin();
	if (!Overlay.IsValid()) return []() {};
	TSharedRef<SWidget> Layer = SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0, 0, 0, 0.55f))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(24)
		[
			Card
		];
	Overlay->AddSlot()[Layer];
	TWeakPtr<SOverlay> WeakHost = Overlay;
	TWeakPtr<SWidget> WeakLayer = Layer;
	return [WeakHost, WeakLayer]()
	{
		const TSharedPtr<SOverlay> H = WeakHost.Pin();
		const TSharedPtr<SWidget> L = WeakLayer.Pin();
		if (H.IsValid() && L.IsValid()) H->RemoveSlot(L.ToSharedRef());
	};
}

// ─── Look ───────────────────────────────────────────────────────────────────────────────────────────────────────────

namespace ZettaPlUI
{
	static FLinearColor Hex(const TCHAR* Code) { return FLinearColor::FromSRGBColor(FColor::FromHex(Code)); }

	static FHue MakeHue(const TCHAR* Plate, const TCHAR* Edge, const TCHAR* Hi) { return { Hex(Plate), Hex(Edge), Hex(Hi) }; }

	FHue Amber() { return MakeHue(TEXT("2e1f0a"), TEXT("f5b347"), TEXT("ffdca0")); }

	FHue HueOf(const FString& Kind)
	{
		using namespace ZettaNodeKinds;
		static const FHue Signal = MakeHue(TEXT("0d1d42"), TEXT("5ea4ff"), TEXT("a9d0ff"));
		static const FHue Teal = MakeHue(TEXT("0a2827"), TEXT("42d3ba"), TEXT("a8f3e5"));
		static const FHue Violet = MakeHue(TEXT("1f1842"), TEXT("a58bff"), TEXT("dccfff"));
		static const FHue Rose = MakeHue(TEXT("36131e"), TEXT("ff7690"), TEXT("ffc6cf"));
		static const FHue Lime = MakeHue(TEXT("1a2a0c"), TEXT("a8de52"), TEXT("dff7ad"));
		static const FHue Magenta = MakeHue(TEXT("300f33"), TEXT("e57cf5"), TEXT("f8c6ff"));
		static const FHue Grey = MakeHue(TEXT("2A2620"), TEXT("A8A196"), TEXT("FAF8F4"));
		if (Kind == SeedModel || Kind == Generate3D || Kind == Generate3DMultiview) return Signal;
		if (Kind == SeedPicture || Kind == SeedViews || Kind == RemoveBackground || Kind == AiEdit || Kind == DrawViews || Kind == IsolateParts) return Magenta;
		if (Kind == GenerateSmart3D || Kind == GenerateSmart3DMultiview) return Violet;
		if (Kind == GenerateSplat) return Teal;
		if (Kind == Retopology || Kind == Segmentation || Kind == SmartSegmentation || Kind == SmartUv || Kind == Export) return Lime;
		if (Kind == Texture || Kind == TextureMultiview) return Amber();
		if (Kind == Rigging) return Rose;
		return Grey;
	}

	FString GlyphOf(const FString& Kind)
	{
		using namespace ZettaNodeKinds;
		static const TMap<FString, FString> Glyphs = {
			{ SeedPicture, TEXT("Pic") }, { SeedModel, TEXT("Mdl") }, { SeedViews, TEXT("4V") }, { RemoveBackground, TEXT("Cut") },
			{ AiEdit, TEXT("Edit") }, { DrawViews, TEXT("Vw") }, { IsolateParts, TEXT("Iso") }, { Generate3D, TEXT("3D") },
			{ GenerateSmart3D, TEXT("S3D") }, { Generate3DMultiview, TEXT("MV") }, { GenerateSmart3DMultiview, TEXT("MVS") },
			{ GenerateSplat, TEXT("Spl") }, { Retopology, TEXT("Top") }, { Segmentation, TEXT("Seg") }, { SmartSegmentation, TEXT("Seg") },
			{ Texture, TEXT("Tex") }, { TextureMultiview, TEXT("MVT") }, { SmartUv, TEXT("UV") }, { Rigging, TEXT("Rig") }, { Export, TEXT("Exp") },
		};
		const FString* G = Glyphs.Find(Kind);
		return G ? *G : FString(TEXT("?"));
	}

	static const FSlateBrush* Rounded(const FLinearColor& Fill, const FLinearColor& Outline, float Radius)
	{
		// Brushes live for the editor's session; one per colour combination the page uses.
		static TMap<FString, TSharedPtr<FSlateBrush>> Cache;
		const FString Key = Fill.ToString() + Outline.ToString() + FString::SanitizeFloat(Radius);
		if (const TSharedPtr<FSlateBrush>* Found = Cache.Find(Key)) return Found->Get();
		TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateRoundedBoxBrush>(Fill, Radius, Outline, 1.f);
		Cache.Add(Key, Brush);
		return Brush.Get();
	}

	TSharedRef<SWidget> HueIcon(const FString& Kind, float Size)
	{
		const FHue Hue = HueOf(Kind);
		return SNew(SBox).WidthOverride(Size + 8).HeightOverride(Size)
			[
				SNew(SBorder)
				.BorderImage(Rounded(Hue.Plate, Hue.Edge, 4.f))
				.HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(0)
				[
					SNew(STextBlock).Text(FText::FromString(GlyphOf(Kind))).Font(FZettaStyle::Font(Size > 24 ? 9 : 8, true)).ColorAndOpacity(Hue.Hi)
				]
			];
	}

	FLinearColor ToneColour(EZettaRunTone Tone)
	{
		switch (Tone)
		{
		case EZettaRunTone::Running: return Hex(TEXT("5ea4ff"));
		case EZettaRunTone::Attention: return Hex(TEXT("f5b347"));
		case EZettaRunTone::Done: return Hex(TEXT("3FB96B"));
		case EZettaRunTone::Failed: return Hex(TEXT("E4756B"));
		default: return Hex(TEXT("A8A196"));
		}
	}

	TSharedRef<SWidget> RunDot(EZettaRunTone Tone, const FText& Tooltip)
	{
		return SNew(SBox).WidthOverride(8).HeightOverride(8).VAlign(VAlign_Center).ToolTipText(Tooltip)
			[
				SNew(SBorder).BorderImage(Rounded(ToneColour(Tone), ToneColour(Tone), 4.f)).Padding(0)
			];
	}

	TSharedRef<SWidget> RunPill(const TAttribute<FString>& Status)
	{
		return SNew(SBorder)
			.BorderImage(FZettaStyle::Get().GetBrush("Zetta.Chip"))
			.Padding(FMargin(8, 2))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
				[
					SNew(SBox).WidthOverride(8).HeightOverride(8)
					[
						SNew(SImage).Image(FAppStyle::GetBrush("Icons.FilledCircle"))
						.ColorAndOpacity_Lambda([Status]() { return FSlateColor(ToneColour(ZettaRunStatus::Tone(Status.Get()))); })
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(FZettaStyle::Font(8, true)).Text_Lambda([Status]() { return FText::FromString(ZettaRunStatus::Label(Status.Get())); })
				]
			];
	}

	TSharedRef<SWidget> Note(const TAttribute<FText>& Text, const FString& Tone)
	{
		FLinearColor Fill = FZettaStyle::PanelRaised();
		FLinearColor Edge = FZettaStyle::Border();
		FSlateColor Ink = FSlateColor::UseSubduedForeground();
		if (Tone == TEXT("error")) { Fill = Hex(TEXT("36131e")); Edge = Hex(TEXT("E4756B")); Ink = FSlateColor(Hex(TEXT("ffc6cf"))); }
		else if (Tone == TEXT("amber")) { Fill = Hex(TEXT("2e1f0a")); Edge = Hex(TEXT("f5b347")); Ink = FSlateColor(Hex(TEXT("ffdca0"))); }
		else if (Tone == TEXT("signal")) { Fill = Hex(TEXT("0d1d42")); Edge = Hex(TEXT("5ea4ff")); Ink = FSlateColor(Hex(TEXT("a9d0ff"))); }
		return SNew(SBorder)
			.BorderImage(Rounded(Fill, Edge, 4.f))
			.Padding(FMargin(8, 6))
			.Visibility_Lambda([Text]() { return Text.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			[
				SNew(STextBlock).Text(Text).AutoWrapText(true).ColorAndOpacity(Ink)
			];
	}

	TSharedRef<SWidget> Heading(const FText& Text)
	{
		return SNew(STextBlock).Text(FText::FromString(Text.ToString().ToUpper())).Font(FZettaStyle::Font(8, true)).ColorAndOpacity(FSlateColor::UseSubduedForeground());
	}

	TSharedRef<SWidget> Field(const FText& Label, const TSharedRef<SWidget>& Control, const FText& Aside)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 3)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(Label).Font(FZettaStyle::Font(9, true))]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(Aside).Font(FZettaStyle::Font(8)).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
			]
			+ SVerticalBox::Slot().AutoHeight()[Control];
	}

	TSharedRef<SWidget> SmallButton(const TAttribute<FText>& Text, FSimpleDelegate OnClicked, const TAttribute<bool>& Enabled, const FText& Tooltip, bool bPrimary)
	{
		return SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>(bPrimary ? "PrimaryButton" : "Button"))
			.ContentPadding(FMargin(8, 2))
			.IsEnabled(Enabled)
			.ToolTipText(Tooltip)
			.OnClicked_Lambda([OnClicked]() { OnClicked.ExecuteIfBound(); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(Text).Font(FZettaStyle::Font(9, bPrimary))
			];
	}

	TSharedRef<SWidget> GhostButton(const TAttribute<FText>& Text, FSimpleDelegate OnClicked, const TAttribute<bool>& Enabled, const FText& Tooltip)
	{
		return SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
			.ContentPadding(FMargin(6, 2))
			.IsEnabled(Enabled)
			.ToolTipText(Tooltip)
			.OnClicked_Lambda([OnClicked]() { OnClicked.ExecuteIfBound(); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(Text).Font(FZettaStyle::Font(9))
			];
	}

	TSharedRef<SWidget> Segmented(const TArray<FZettaPlOption>& Options, TFunction<FString()> Value, TFunction<void(const FString&)> OnPick, TFunction<FString(const FString&)> DisabledReason)
	{
		TSharedRef<SWrapBox> Box = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(2, 2));
		for (const FZettaPlOption& O : Options)
		{
			const FString Key = O.Value;
			const FString Hint = O.Hint;
			Box->AddSlot()
			[
				SNew(SCheckBox)
				.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox"))
				.IsEnabled_Lambda([DisabledReason, Key]() { return !DisabledReason || DisabledReason(Key).IsEmpty(); })
				.ToolTipText_Lambda([DisabledReason, Key, Hint]()
				{
					const FString Reason = DisabledReason ? DisabledReason(Key) : FString();
					return FText::FromString(Reason.IsEmpty() ? Hint : Reason);
				})
				.IsChecked_Lambda([Value, Key]() { return Value() == Key ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([OnPick, Value, Key](ECheckBoxState)
				{
					if (Value() != Key) OnPick(Key);
				})
				[
					SNew(SBox).Padding(FMargin(8, 2))[SNew(STextBlock).Text(FText::FromString(O.Label)).Font(FZettaStyle::Font(9))]
				]
			];
		}
		return Box;
	}

	TSharedRef<SWidget> Dropdown(const TArray<FZettaPlOption>& Options, TFunction<FString()> Value, TFunction<void(const FString&)> OnPick, const TAttribute<bool>& Enabled)
	{
		return SNew(SComboButton)
			.IsEnabled(Enabled)
			.OnGetMenuContent_Lambda([Options, Value, OnPick]()
			{
				FMenuBuilder Menu(true, nullptr);
				for (const FZettaPlOption& O : Options)
				{
					const FString Key = O.Value;
					Menu.AddMenuEntry(FText::FromString(O.Label), FText::FromString(O.Hint), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([OnPick, Key]() { OnPick(Key); }), FCanExecuteAction(),
							FIsActionChecked::CreateLambda([Value, Key]() { return Value() == Key; })),
						NAME_None, EUserInterfaceActionType::RadioButton);
				}
				return Menu.MakeWidget();
			})
			.ButtonContent()
			[
				SNew(STextBlock).Text_Lambda([Options, Value]()
				{
					const FString Current = Value();
					for (const FZettaPlOption& O : Options) if (O.Value == Current) return FText::FromString(O.Label);
					return FText::FromString(Current);
				})
			];
	}

	TSharedRef<SWidget> Check(const TAttribute<FText>& Label, TFunction<bool()> Value, TFunction<void(bool)> OnChanged, const FText& Tooltip, const TAttribute<bool>& Enabled)
	{
		return SNew(SBox).Padding(FMargin(0, 2))
			[
				SNew(SCheckBox)
				.IsEnabled(Enabled)
				.ToolTipText(Tooltip)
				.IsChecked_Lambda([Value]() { return Value() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([OnChanged](ECheckBoxState S) { OnChanged(S == ECheckBoxState::Checked); })
				[
					SNew(STextBlock).Text(Label).AutoWrapText(true)
				]
			];
	}

	TArray<FZettaPlOption> Options(const TArray<FZettaSettingOption>& Settings)
	{
		TArray<FZettaPlOption> Out;
		for (const FZettaSettingOption& S : Settings) Out.Add({ S.Value, S.Label, S.Hint });
		return Out;
	}

	void Toast(const FString& Message, bool bError)
	{
		FNotificationInfo Info(FText::FromString(Message));
		Info.ExpireDuration = bError ? 6.f : 4.f;
		Info.bUseLargeFont = false;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bError ? SNotificationItem::CS_Fail : SNotificationItem::CS_None);
		}
	}

	bool Confirm(const FString& Title, const FString& Message)
	{
		return FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(Message), FText::FromString(Title)) == EAppReturnType::Yes;
	}

	TSharedRef<SWidget> Dialog(const FText& Title, const FText& Lead, const TSharedRef<SWidget>& Body, const TSharedRef<SWidget>& Footer, float Width)
	{
		return SNew(SBox).WidthOverride(Width).MaxDesiredHeight(720)
			[
				SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.CardRaised")).Padding(16)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ZettaUI::Title(Title)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 10)[ZettaUI::Hint(Lead)]
					+ SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox) + SScrollBox::Slot()[Body]]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)[Footer]
				]
			];
	}

	FString PickPicture(const FString& Title) { return ZettaUI::PickFile(Title, ZettaUI::ImageFilter()); }

	FString StableKey(const FString& Url)
	{
		int32 Q = INDEX_NONE;
		return Url.FindChar(TEXT('?'), Q) && !Url.StartsWith(TEXT("data:")) ? Url.Left(Q) : Url;
	}
}

// ─── Thumb ──────────────────────────────────────────────────────────────────────────────────────────────────────────

/** Signed URLs change on every read; the picture behind them does not. Kept by address without the query. */
static TMap<FString, TSharedPtr<FSlateBrush>>& ThumbCache()
{
	static TMap<FString, TSharedPtr<FSlateBrush>> Cache;
	return Cache;
}

void SZettaPlThumb::Construct(const FArguments& InArgs)
{
	UrlAttribute = InArgs._Url;
	const FVector2D Size = InArgs._Size;
	ChildSlot
	[
		SNew(SBox).WidthOverride(Size.X).HeightOverride(Size.Y)
		[
			SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Thumb")).Padding(0).HAlign(HAlign_Fill).VAlign(VAlign_Fill)
			[
				SNew(SOverlay)
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(InArgs._Placeholder).Font(FZettaStyle::Font(8)).ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Visibility_Lambda([this]() { return Brush.IsValid() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
				]
				+ SOverlay::Slot()
				[
					SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
					.Visibility_Lambda([this]() { return Brush.IsValid() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					[
						SNew(SImage).Image_Lambda([this]() { return Brush.IsValid() ? Brush.Get() : FAppStyle::GetNoBrush(); })
					]
				]
			]
		]
	];
	Load(UrlAttribute.Get());
}

void SZettaPlThumb::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (UrlAttribute.IsBound()) Load(UrlAttribute.Get());
}

void SZettaPlThumb::SetBytes(const TArray<uint8>& Bytes)
{
	UrlAttribute = FString();
	CurrentKey = FString::Printf(TEXT("bytes:%u:%d"), FCrc::MemCrc32(Bytes.GetData(), Bytes.Num()), Bytes.Num());
	Brush = Bytes.Num() > 0 ? FZettaImageCache::Get().FromBytes(CurrentKey, Bytes) : nullptr;
}

void SZettaPlThumb::Load(const FString& Url)
{
	const FString Key = ZettaPlUI::StableKey(Url);
	if (Key == CurrentKey) return;
	CurrentKey = Key;
	Brush.Reset();
	if (Url.IsEmpty()) return;
	if (Url.StartsWith(TEXT("data:")))
	{
		const TArray<uint8> Bytes = FZettaPipelineSeed::FromDataUrl(Url);
		if (Bytes.Num() > 0) Brush = FZettaImageCache::Get().FromBytes(FString::Printf(TEXT("data:%u"), GetTypeHash(Url)), Bytes);
		return;
	}
	if (const TSharedPtr<FSlateBrush>* Cached = ThumbCache().Find(Key))
	{
		Brush = *Cached;
		return;
	}
	TWeakPtr<SZettaPlThumb> Weak = SharedThis(this);
	FZettaImageCache::Get().Request(Url, [Weak, Key](TSharedPtr<FSlateBrush> Loaded)
	{
		if (Loaded.IsValid()) ThumbCache().Add(Key, Loaded);
		if (const TSharedPtr<SZettaPlThumb> Self = Weak.Pin())
		{
			if (Self->CurrentKey == Key) Self->Brush = Loaded;
		}
	});
}

// ─── Text ───────────────────────────────────────────────────────────────────────────────────────────────────────────

void SZettaPlText::Construct(const FArguments& InArgs, TFunction<FString()> InValue, TFunction<void(const FString&)> InOnChanged)
{
	Value = MoveTemp(InValue);
	OnChanged = MoveTemp(InOnChanged);
	MaxLength = InArgs._MaxLength;
	Shown = Value ? Value() : FString();
	auto Changed = [this](const FText& Text)
	{
		// A value shown by SetText is not the user's change.
		if (bSetting) return;
		FString S = Text.ToString();
		if (MaxLength > 0 && S.Len() > MaxLength)
		{
			S = S.Left(MaxLength);
			SetText(S);
		}
		Shown = S;
		if (OnChanged) OnChanged(S);
	};
	if (InArgs._MultiLine)
	{
		ChildSlot
		[
			SAssignNew(Multi, SMultiLineEditableTextBox)
			.Text(FText::FromString(Shown))
			.HintText(InArgs._Hint)
			.AutoWrapText(true)
			.IsEnabled(InArgs._IsEnabled)
			.OnTextChanged_Lambda(Changed)
			.OnKeyDownHandler_Lambda([this](const FGeometry&, const FKeyEvent& Key)
			{
				if (OnEnter && (Key.GetKey() == EKeys::Enter) && !Key.IsShiftDown())
				{
					OnEnter();
					return FReply::Handled();
				}
				return FReply::Unhandled();
			})
		];
	}
	else
	{
		ChildSlot
		[
			SAssignNew(Single, SEditableTextBox)
			.Text(FText::FromString(Shown))
			.HintText(InArgs._Hint)
			.IsEnabled(InArgs._IsEnabled)
			.OnTextChanged_Lambda(Changed)
			.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type Commit)
			{
				if (Commit == ETextCommit::OnEnter && OnEnter) OnEnter();
			})
		];
	}
}

bool SZettaPlText::IsTyping() const
{
	const TSharedPtr<SWidget> Box = Single.IsValid() ? StaticCastSharedPtr<SWidget>(Single) : StaticCastSharedPtr<SWidget>(Multi);
	return Box.IsValid() && (Box->HasKeyboardFocus() || Box->HasFocusedDescendants());
}

FString SZettaPlText::GetText() const
{
	if (Single.IsValid()) return Single->GetText().ToString();
	if (Multi.IsValid()) return Multi->GetText().ToString();
	return FString();
}

void SZettaPlText::SetText(const FString& Text)
{
	Shown = Text;
	TGuardValue<bool> Guard(bSetting, true);
	if (Single.IsValid()) Single->SetText(FText::FromString(Text));
	if (Multi.IsValid()) Multi->SetText(FText::FromString(Text));
}

void SZettaPlText::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (!Value || IsTyping()) return;
	const FString Now = Value();
	if (Now != Shown) SetText(Now);
}

// ─── Drops ──────────────────────────────────────────────────────────────────────────────────────────────────────────

void SZettaPlDropTarget::Construct(const FArguments& InArgs)
{
	ChildSlot[InArgs._Content.Widget];
}

FReply SZettaPlDropTarget::OnDragOver(const FGeometry&, const FDragDropEvent& Event)
{
	if (!Accepts.Get()) return FReply::Unhandled();
	const TSharedPtr<FExternalDragOperation> Files = Event.GetOperationAs<FExternalDragOperation>();
	if (Files.IsValid() && Files->HasFiles()) return FReply::Handled();
	if (Event.GetOperationAs<FAssetDragDropOp>().IsValid()) return FReply::Handled();
	return FReply::Unhandled();
}

FReply SZettaPlDropTarget::OnDrop(const FGeometry&, const FDragDropEvent& Event)
{
	if (!Accepts.Get() || !OnDropped) return FReply::Unhandled();
	TArray<FString> Files, Assets;
	if (const TSharedPtr<FExternalDragOperation> Op = Event.GetOperationAs<FExternalDragOperation>())
	{
		if (Op->HasFiles()) Files = Op->GetFiles();
	}
	if (const TSharedPtr<FAssetDragDropOp> Op = Event.GetOperationAs<FAssetDragDropOp>())
	{
		for (const FAssetData& A : Op->GetAssets()) Assets.Add(A.GetObjectPathString());
	}
	if (Files.Num() == 0 && Assets.Num() == 0) return FReply::Unhandled();
	OnDropped(Files, Assets);
	return FReply::Handled();
}

TSharedRef<FZettaStepDragOp> FZettaStepDragOp::New(const FString& Kind, const FString& Label)
{
	TSharedRef<FZettaStepDragOp> Op = MakeShared<FZettaStepDragOp>();
	Op->Kind = Kind;
	Op->Label = Label;
	Op->Construct();
	return Op;
}

TSharedPtr<SWidget> FZettaStepDragOp::GetDefaultDecorator() const
{
	return SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.CardRaised")).Padding(FMargin(8, 4))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[ZettaPlUI::HueIcon(Kind)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Label))]
		];
}

// ─── Library pickers ────────────────────────────────────────────────────────────────────────────────────────────────

namespace ZettaPlPickers
{
	static constexpr int32 PageSize = 24;

	void PickImages(const FZettaPlOverlayRef& Overlay, bool bMultiple, TFunction<TArray<int64>()> Chosen, TFunction<bool(const FZettaPickedImage&)> OnPick)
	{
		struct FState
		{
			TArray<FZettaImagePost> Items;
			int32 Page = 0;
			int32 Total = 0;
			bool bLoading = false;
			FString Status;
			TFunction<void()> Close;
			TSharedPtr<SWrapBox> Grid;
		};
		TSharedRef<FState> State = MakeShared<FState>();
		TSharedRef<TFunction<void()>> Render = MakeShared<TFunction<void()>>();
		TSharedRef<TFunction<void()>> Load = MakeShared<TFunction<void()>>();
		TWeakPtr<FState> WeakState = State;

		*Render = [WeakState, bMultiple, Chosen, OnPick]()
		{
			const TSharedPtr<FState> S = WeakState.Pin();
			if (!S.IsValid() || !S->Grid.IsValid()) return;
			S->Grid->ClearChildren();
			for (const FZettaImagePost& Post : S->Items)
			{
				if (Post.ImageUrls.Num() == 0) continue;
				const FZettaPickedImage Picked{ Post.Id, Post.ImageUrls[0], Post.Prompt.IsEmpty() ? FString::Printf(TEXT("Picture #%lld"), Post.Id) : Post.Prompt.Left(60) };
				S->Grid->AddSlot().Padding(3)
				[
					SNew(SButton)
					.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
					.ContentPadding(2)
					.ToolTipText(FText::FromString(Post.Prompt))
					.OnClicked_Lambda([WeakState, Picked, bMultiple, OnPick]()
					{
						const bool bNow = OnPick(Picked);
						const TSharedPtr<FState> St = WeakState.Pin();
						if (!bMultiple && bNow && St.IsValid() && St->Close) St->Close();
						return FReply::Handled();
					})
					[
						SNew(SBorder)
						.BorderImage_Lambda([Chosen, Id = Post.Id]()
						{
							return Chosen && Chosen().Contains(Id) ? FZettaStyle::Get().GetBrush("Zetta.CardSelected") : FZettaStyle::Get().GetBrush("Zetta.Card");
						})
						.Padding(2)
						[
							SNew(SZettaPlThumb).Url(Picked.Url).Size(FVector2D(96, 96))
						]
					]
				];
			}
		};
		*Load = [WeakState, Render]()
		{
			const TSharedPtr<FState> S = WeakState.Pin();
			if (!S.IsValid() || S->bLoading) return;
			S->bLoading = true;
			S->Status = TEXT("Loading your images…");
			const int32 Page = S->Page + 1;
			ZettaApi::ListImages(Page, PageSize, FString(), [WeakState, Render, Page](const FZettaResponse& R, const TArray<FZettaImagePost>& Posts, int32 Total)
			{
				const TSharedPtr<FState> St = WeakState.Pin();
				if (!St.IsValid()) return;
				St->bLoading = false;
				if (!R.bOk)
				{
					St->Status = ZettaCatalog::Explain(R);
					return;
				}
				St->Page = Page;
				St->Total = Total;
				for (const FZettaImagePost& P : Posts) if (P.ImageUrls.Num() > 0) St->Items.Add(P);
				St->Status = St->Items.Num() == 0 ? TEXT("No images yet — upload one or make one instead.") : FString();
				(*Render)();
			});
		};

		TSharedRef<SWidget> Body = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ZettaUI::Hint(MakeAttributeLambda([State]() { return FText::FromString(State->Status); }))]
			+ SVerticalBox::Slot().AutoHeight()[SAssignNew(State->Grid, SWrapBox).UseAllottedSize(true)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6).HAlign(HAlign_Left)
			[
				SNew(SBox).Visibility_Lambda([State]() { return State->Items.Num() < State->Total && !State->bLoading ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					ZettaPlUI::SmallButton(LOCTEXT("PlMore", "Load more"), FSimpleDelegate::CreateLambda([Load]() { (*Load)(); }))
				]
			];
		TSharedRef<SWidget> Footer = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				ZettaPlUI::SmallButton(bMultiple ? LOCTEXT("PlDone", "Done") : LOCTEXT("PlCancel", "Cancel"),
					FSimpleDelegate::CreateLambda([State, Render, Load]() { if (State->Close) State->Close(); }), true, FText(), bMultiple)
			];
		State->Close = Overlay->Show(ZettaPlUI::Dialog(LOCTEXT("PlYourImages", "Your images"),
			bMultiple ? LOCTEXT("PlImagesLeadMany", "Pick one or more — each becomes a run.") : LOCTEXT("PlImagesLead", "Pick a picture from your Image Studio library."),
			Body, Footer));
		(*Load)();
	}

	void PickModels(const FZettaPlOverlayRef& Overlay, TFunction<TArray<int64>()> Chosen, TFunction<bool(const FZettaGenerationPtr&)> OnPick)
	{
		struct FState
		{
			TArray<FZettaGenerationPtr> Items;
			int32 Page = 0;
			int32 Total = 0;
			int32 Version = 0;
			bool bLoading = false;
			FString Search;
			FString Status;
			TFunction<void()> Close;
			TSharedPtr<SWrapBox> Grid;
		};
		TSharedRef<FState> State = MakeShared<FState>();
		TWeakPtr<FState> WeakState = State;
		TSharedRef<TFunction<void()>> Render = MakeShared<TFunction<void()>>();
		TSharedRef<TFunction<void(bool)>> Load = MakeShared<TFunction<void(bool)>>();

		*Render = [WeakState, Chosen, OnPick]()
		{
			const TSharedPtr<FState> S = WeakState.Pin();
			if (!S.IsValid() || !S->Grid.IsValid()) return;
			S->Grid->ClearChildren();
			for (const FZettaGenerationPtr& G : S->Items)
			{
				const int64 Id = G->Id;
				S->Grid->AddSlot().Padding(3)
				[
					SNew(SButton)
					.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
					.ContentPadding(2)
					.ToolTipText(FText::FromString(FString::Printf(TEXT("%s · %s"), *G->DisplayTitle(), *ZettaCatalog::FeatureLabel(G->Feature))))
					.OnClicked_Lambda([G, OnPick]() { OnPick(G); return FReply::Handled(); })
					[
						SNew(SBorder)
						.BorderImage_Lambda([Chosen, Id]() { return Chosen && Chosen().Contains(Id) ? FZettaStyle::Get().GetBrush("Zetta.CardSelected") : FZettaStyle::Get().GetBrush("Zetta.Card"); })
						.Padding(4)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()[SNew(SZettaPlThumb).Url(G->ThumbnailUrl).Size(FVector2D(96, 96)).Placeholder(LOCTEXT("PlModelPh", "Model"))]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
							[
								SNew(SBox).WidthOverride(96)[SNew(STextBlock).Text(FText::FromString(G->DisplayTitle())).Font(FZettaStyle::Font(8)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
							]
						]
					]
				];
			}
		};
		*Load = [WeakState, Render](bool bFresh)
		{
			const TSharedPtr<FState> S = WeakState.Pin();
			if (!S.IsValid()) return;
			if (bFresh)
			{
				S->Items.Reset();
				S->Page = 0;
				S->Total = 0;
				++S->Version;
			}
			else if (S->bLoading) return;
			S->bLoading = true;
			S->Status = TEXT("Loading your models…");
			ZettaApi::FListQuery Query;
			Query.Page = S->Page + 1;
			Query.PageSize = PageSize;
			Query.Search = S->Search.TrimStartAndEnd();
			Query.Statuses = { TEXT("success") };
			const int32 Version = S->Version;
			ZettaApi::ListGenerations(Query, [WeakState, Render, Version](const FZettaResponse& R, const FZettaGenerationPage& Page)
			{
				const TSharedPtr<FState> St = WeakState.Pin();
				if (!St.IsValid() || St->Version != Version) return;
				St->bLoading = false;
				if (!R.bOk)
				{
					St->Status = ZettaCatalog::Explain(R);
					return;
				}
				St->Page = Page.Page;
				St->Total = Page.Total;
				// Splats are made on the web and cannot seed a run here.
				for (const FZettaGenerationPtr& G : Page.Items) if (G.IsValid() && !G->IsSplat() && !ZettaCatalog::IsSplatFeature(G->Feature)) St->Items.Add(G);
				St->Status = St->Items.Num() > 0 ? FString() : !St->Search.IsEmpty() ? TEXT("No model matches that.") : TEXT("No finished models yet — generate one in the Studio first.");
				(*Render)();
			});
		};

		TSharedRef<SWidget> Body = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("PlSearchModels", "Search your models"))
				.OnTextCommitted_Lambda([State, Load](const FText& Text, ETextCommit::Type)
				{
					State->Search = Text.ToString();
					(*Load)(true);
				})
			]
			+ SVerticalBox::Slot().AutoHeight()[ZettaUI::Hint(MakeAttributeLambda([State]() { return FText::FromString(State->Status); }))]
			+ SVerticalBox::Slot().AutoHeight()[SAssignNew(State->Grid, SWrapBox).UseAllottedSize(true)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6).HAlign(HAlign_Left)
			[
				SNew(SBox).Visibility_Lambda([State]() { return State->Items.Num() > 0 && State->Page * PageSize < State->Total && !State->bLoading ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					ZettaPlUI::SmallButton(LOCTEXT("PlMoreModels", "Load more"), FSimpleDelegate::CreateLambda([Load]() { (*Load)(false); }))
				]
			];
		TSharedRef<SWidget> Footer = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				ZettaPlUI::SmallButton(LOCTEXT("PlModelsDone", "Done"), FSimpleDelegate::CreateLambda([State, Render, Load]() { if (State->Close) State->Close(); }), true, FText(), true)
			];
		State->Close = Overlay->Show(ZettaPlUI::Dialog(LOCTEXT("PlYourModels", "Your models"),
			LOCTEXT("PlModelsLead", "Finished models in your library — each one picked becomes a run."), Body, Footer));
		(*Load)(true);
	}
}

// ─── View slots ─────────────────────────────────────────────────────────────────────────────────────────────────────

bool SZettaViewSlots::HasFront(const TSharedPtr<FJsonObject>& Set)
{
	const FObj Front = JObject(Set, TEXT("front"));
	return Front.IsValid() && (JIsString(ZettaPJson::JGet(Front, TEXT("uploadKey"))) || JIsNumber(ZettaPJson::JGet(Front, TEXT("gen2dPostId"))));
}

void SZettaViewSlots::Construct(const FArguments& InArgs, const FZettaPlOverlayRef& InOverlay, TFunction<TSharedPtr<FJsonObject>()> InValue, TFunction<void(FUpdate)> InUpdate)
{
	Overlay = InOverlay;
	Value = MoveTemp(InValue);
	Update = MoveTemp(InUpdate);
	Locked = InArgs._IsLocked;
	const FText Label = InArgs._Label.IsEmpty() ? LOCTEXT("PlViewsLabel", "Pictures of each side") : InArgs._Label;
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	const TArray<TPair<FString, FText>> Slots = {
		{ TEXT("front"), LOCTEXT("PlFront", "Front") }, { TEXT("back"), LOCTEXT("PlBack", "Back") },
		{ TEXT("left"), LOCTEXT("PlLeft", "Left") }, { TEXT("right"), LOCTEXT("PlRight", "Right") },
	};
	for (const TPair<FString, FText>& S : Slots) Row->AddSlot().AutoWidth().Padding(0, 0, 6, 0)[MakeSlot(S.Key, S.Value)];
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(Label).Font(FZettaStyle::Font(9, true))]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(LOCTEXT("PlFrontAtLeast", " — the front at least")).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
		]
		+ SVerticalBox::Slot().AutoHeight()[Row]
		+ SVerticalBox::Slot().AutoHeight()[ZettaUI::ErrorText(MakeAttributeLambda([this]() { return FText::FromString(Error); }))]
	];
}

TSharedRef<SWidget> SZettaViewSlots::MakeSlot(const FString& Key, const FText& Name)
{
	auto Picture = [this, Key]() { return JObject(Value ? Value() : nullptr, Key); };
	auto Busy = [this, Key]() { return Uploading.Contains(Key); };
	auto Free = [this, Busy]() { return !Locked.Get(false) && !Busy(); };
	TSharedRef<SZettaPlDropTarget> Drop = SNew(SZettaPlDropTarget)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SZettaPlThumb).Size(FVector2D(76, 76))
				.Url_Lambda([Picture]() { return JTextOr(Picture(), TEXT("thumbnail")); })
				.Placeholder_Lambda([Picture, Busy]()
				{
					if (Busy()) return LOCTEXT("PlUploading", "Uploading…");
					const FObj P = Picture();
					return P.IsValid() ? FText::FromString(JTextOr(P, TEXT("name"), TEXT("Chosen"))) : LOCTEXT("PlEmpty", "Empty");
				})
			]
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(4)
			[
				SNew(SButton)
				.ButtonStyle(&FZettaStyle::Get().GetWidgetStyle<FButtonStyle>("Zetta.Button.Close"))
				.ContentPadding(FMargin(4))
				.ToolTipText(FText::Format(LOCTEXT("PlRemoveView", "Remove the {0} picture"), FText::FromString(Name.ToString().ToLower())))
				.Visibility_Lambda([Picture, Free]() { return Picture().IsValid() && Free() ? EVisibility::Visible : EVisibility::Collapsed; })
				.OnClicked_Lambda([this, Key]() { Put(Key, nullptr); return FReply::Handled(); })
				[
					SNew(SBox).WidthOverride(10).HeightOverride(10)[SNew(SImage).Image(FAppStyle::GetBrush("Icons.X")).ColorAndOpacity(FLinearColor::White)]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(STextBlock).Font(FZettaStyle::Font(8, true))
			.Text(Key == TEXT("front") ? FText::Format(LOCTEXT("PlRequired", "{0} · required"), Name) : Name)
			.ColorAndOpacity_Lambda([Picture, Key]() { return Key == TEXT("front") && !Picture().IsValid() ? FSlateColor(FZettaStyle::Warning()) : FSlateColor::UseForeground(); })
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				ZettaPlUI::GhostButton(LOCTEXT("PlUpload", "Upload"), FSimpleDelegate::CreateLambda([this, Key, Name]()
				{
					const FString Path = ZettaPlUI::PickPicture(FString::Printf(TEXT("Choose the %s picture"), *Name.ToString().ToLower()));
					if (Path.IsEmpty()) return;
					TArray<uint8> Bytes;
					FString Type;
					Error = ZettaUI::ReadImageFile(Path, Bytes, Type);
					if (Error.IsEmpty()) Upload(Key, Bytes, Type, FPaths::GetCleanFilename(Path));
				}), MakeAttributeLambda(Free), FText::Format(LOCTEXT("PlUploadTip", "Upload the {0} picture — or drop one on the slot"), FText::FromString(Name.ToString().ToLower())))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				ZettaPlUI::GhostButton(LOCTEXT("PlImages", "Images"), FSimpleDelegate::CreateLambda([this, Key]()
				{
					Error.Reset();
					TWeakPtr<SZettaViewSlots> Weak = SharedThis(this);
					ZettaPlPickers::PickImages(Overlay, false, nullptr, [Weak, Key](const FZettaPickedImage& Picked)
					{
						FZettaPipelineSeed::ThumbnailFromUrl(Picked.Url, [Weak, Key, Picked](const TArray<uint8>& Preview)
						{
							const TSharedPtr<SZettaViewSlots> Self = Weak.Pin();
							if (!Self.IsValid()) return;
							TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
							P->SetNumberField(TEXT("gen2dPostId"), (double)Picked.Id);
							P->SetStringField(TEXT("name"), Picked.Name);
							if (Preview.Num() > 0) P->SetStringField(TEXT("thumbnail"), FZettaPipelineSeed::DataUrl(Preview));
							else P->SetField(TEXT("thumbnail"), JNull());
							Self->Put(Key, P);
						});
						return true;
					});
				}), MakeAttributeLambda(Free), FText::Format(LOCTEXT("PlPickTip", "Pick the {0} picture from your images"), FText::FromString(Name.ToString().ToLower())))
			]
		]
	];
	Drop->Accepts = MakeAttributeLambda(Free);
	Drop->OnDropped = [this, Key](const TArray<FString>& Files, const TArray<FString>& Assets)
	{
		TArray<uint8> Bytes;
		FString Type, Name;
		Error.Reset();
		if (Assets.Num() > 0)
		{
			if (!ZettaPipelineAssets::PictureOfTexture(Assets[0], Bytes, Type, Name, Error)) return;
		}
		else if (Files.Num() > 0)
		{
			Error = ZettaUI::ReadImageFile(Files[0], Bytes, Type);
			if (!Error.IsEmpty()) return;
			Name = FPaths::GetCleanFilename(Files[0]);
		}
		Upload(Key, Bytes, Type, Name);
	};
	return SNew(SBox).WidthOverride(84)[Drop];
}

void SZettaViewSlots::Put(const FString& Key, const TSharedPtr<FJsonObject>& Picture)
{
	if (!Update) return;
	Update([Key, Picture](const TSharedRef<FJsonObject>& Current)
	{
		TSharedRef<FJsonObject> Next = JClone(Current);
		if (Picture.IsValid()) Next->SetObjectField(Key, Picture);
		else Next->RemoveField(Key);
		return Next;
	});
}

void SZettaViewSlots::Upload(const FString& Key, const TArray<uint8>& Bytes, const FString& ContentType, const FString& Name)
{
	Error.Reset();
	Uploading.Add(Key);
	TWeakPtr<SZettaViewSlots> Weak = SharedThis(this);
	FZettaPipelineSeed::FromUpload(Bytes, ContentType, Name, [Weak, Key](const FZettaResponse& R, const FZettaPipelineSeed& Seed)
	{
		const TSharedPtr<SZettaViewSlots> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		Self->Uploading.Remove(Key);
		if (!R.bOk)
		{
			Self->Error = ZettaCatalog::Explain(R);
			return;
		}
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("uploadKey"), JTextOr(Seed.Request, TEXT("uploadKey")));
		P->SetStringField(TEXT("name"), Seed.Name);
		const FString Thumb = Seed.ThumbnailDataUrl();
		if (!Thumb.IsEmpty()) P->SetStringField(TEXT("thumbnail"), Thumb);
		Self->Put(Key, P);
	});
}

#undef LOCTEXT_NAMESPACE
