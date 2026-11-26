// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaWidgets.h"

#include "DesktopPlatformModule.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDesktopPlatform.h"
#include "Input/DragAndDrop.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "ZettaConfig.h"
#include "ZettaImageCache.h"
#include "ZettaModels.h"
#include "ZettaStyle.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace ZettaUI
{
	TSharedRef<SWidget> Title(const FText& Text)
	{
		return SNew(STextBlock).Text(Text).TextStyle(&FZettaStyle::Get().GetWidgetStyle<FTextBlockStyle>("Zetta.Text.Title"));
	}

	TSharedRef<SWidget> Section(const FText& Text)
	{
		return SNew(STextBlock).Text(Text).TextStyle(&FZettaStyle::Get().GetWidgetStyle<FTextBlockStyle>("Zetta.Text.Section"));
	}

	TSharedRef<SWidget> Hint(const TAttribute<FText>& Text)
	{
		return SNew(STextBlock).Text(Text).AutoWrapText(true).TextStyle(&FZettaStyle::Get().GetWidgetStyle<FTextBlockStyle>("Zetta.Text.Hint"));
	}

	TSharedRef<SWidget> ErrorText(const TAttribute<FText>& Text)
	{
		return SNew(STextBlock).Text(Text).AutoWrapText(true)
			.TextStyle(&FZettaStyle::Get().GetWidgetStyle<FTextBlockStyle>("Zetta.Text.Error"))
			.Visibility_Lambda([Text]() { return Text.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; });
	}

	TSharedRef<SWidget> Row(const FText& Label, const TSharedRef<SWidget>& Control, const FText& Tooltip)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.38f).VAlign(VAlign_Center).Padding(0, 3, 8, 3)
			[
				SNew(STextBlock).Text(Label).ToolTipText(Tooltip).ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SHorizontalBox::Slot().FillWidth(0.62f).VAlign(VAlign_Center).Padding(0, 3)
			[
				Control
			];
	}

	TSharedRef<SWidget> Card(const TSharedRef<SWidget>& Content, float Padding)
	{
		return SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Card")).Padding(Padding)[Content];
	}

	TSharedRef<SWidget> PrimaryButton(const TAttribute<FText>& Text, FSimpleDelegate OnClicked, const TAttribute<bool>& Enabled)
	{
		return SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
			.HAlign(HAlign_Center)
			.ContentPadding(FMargin(14, 6))
			.IsEnabled(Enabled)
			.OnClicked_Lambda([OnClicked]() { OnClicked.ExecuteIfBound(); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(Text).Font(FZettaStyle::Font(10, true))
			];
	}

	TSharedRef<SWidget> Button(const TAttribute<FText>& Text, FSimpleDelegate OnClicked, const TAttribute<bool>& Enabled, const FText& Tooltip)
	{
		return SNew(SButton)
			.HAlign(HAlign_Center)
			.ContentPadding(FMargin(10, 4))
			.IsEnabled(Enabled)
			.ToolTipText(Tooltip)
			.OnClicked_Lambda([OnClicked]() { OnClicked.ExecuteIfBound(); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(Text)
			];
	}

	TSharedRef<SWidget> Check(const FText& Label, TSharedRef<bool> Value, const FText& Tooltip, const TAttribute<bool>& Enabled)
	{
		return SNew(SCheckBox)
			.IsEnabled(Enabled)
			.ToolTipText(Tooltip)
			.IsChecked_Lambda([Value]() { return *Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([Value](ECheckBoxState S) { *Value = S == ECheckBoxState::Checked; })
			[
				SNew(STextBlock).Text(Label)
			];
	}

	TSharedRef<SWidget> Choice(TSharedRef<FString> Value, TFunction<TArray<ZettaCatalog::FChoice>()> Choices, TFunction<void()> OnChanged, const TAttribute<bool>& Enabled)
	{
		return SNew(SComboButton)
			.IsEnabled(Enabled)
			.OnGetMenuContent_Lambda([Value, Choices, OnChanged]()
			{
				FMenuBuilder Menu(true, nullptr);
				for (const ZettaCatalog::FChoice& C : Choices())
				{
					const FString Key = C.Key;
					Menu.AddMenuEntry(FText::FromString(C.Label), FText::FromString(C.Hint), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([Value, Key, OnChanged]()
						{
							*Value = Key;
							if (OnChanged) OnChanged();
						}), FCanExecuteAction(), FIsActionChecked::CreateLambda([Value, Key]() { return *Value == Key; })),
						NAME_None, EUserInterfaceActionType::RadioButton);
				}
				return Menu.MakeWidget();
			})
			.ButtonContent()
			[
				SNew(STextBlock).Text_Lambda([Value, Choices]()
				{
					for (const ZettaCatalog::FChoice& C : Choices()) if (C.Key == *Value) return FText::FromString(C.Label);
					return FText::FromString(*Value);
				})
			];
	}

	TSharedRef<SWidget> Segmented(TSharedRef<FString> Value, const TArray<ZettaCatalog::FChoice>& Choices, TFunction<void()> OnChanged)
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		for (const ZettaCatalog::FChoice& C : Choices)
		{
			const FString Key = C.Key;
			Box->AddSlot().AutoWidth().Padding(0, 0, 2, 0)
			[
				SNew(SCheckBox)
				.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox"))
				.ToolTipText(FText::FromString(C.Hint))
				.IsChecked_Lambda([Value, Key]() { return *Value == Key ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([Value, Key, OnChanged](ECheckBoxState)
				{
					*Value = Key;
					if (OnChanged) OnChanged();
				})
				[
					SNew(SBox).Padding(FMargin(10, 3))[SNew(STextBlock).Text(FText::FromString(C.Label))]
				]
			];
		}
		return Box;
	}

	static void* ParentWindowHandle()
	{
		const TSharedPtr<SWindow> Parent = FSlateApplication::Get().GetActiveTopLevelWindow();
		return Parent.IsValid() && Parent->GetNativeWindow().IsValid() ? Parent->GetNativeWindow()->GetOSWindowHandle() : nullptr;
	}

	TArray<FString> PickFiles(const FString& Title, const FString& Filter)
	{
		TArray<FString> Out;
		if (IDesktopPlatform* Platform = FDesktopPlatformModule::Get())
		{
			Platform->OpenFileDialog(ParentWindowHandle(), Title, FString(), FString(), Filter, EFileDialogFlags::Multiple, Out);
		}
		return Out;
	}

	FString PickFile(const FString& Title, const FString& Filter)
	{
		TArray<FString> Out;
		if (IDesktopPlatform* Platform = FDesktopPlatformModule::Get())
		{
			Platform->OpenFileDialog(ParentWindowHandle(), Title, FString(), FString(), Filter, EFileDialogFlags::None, Out);
		}
		return Out.Num() > 0 ? Out[0] : FString();
	}

	FString ImageFilter() { return TEXT("Images (*.png;*.jpg;*.jpeg;*.webp)|*.png;*.jpg;*.jpeg;*.webp"); }

	void OpenSite(const FString& Path)
	{
		FPlatformProcess::LaunchURL(*(ZettaConfig::SiteBase() + Path), nullptr, nullptr);
	}

	FText Credits(int64 Value) { return FText::AsNumber(Value); }

	FString ReadImageFile(const FString& Path, TArray<uint8>& OutBytes, FString& OutContentType)
	{
		if (IFileManager::Get().FileSize(*Path) > 32 * 1024 * 1024) return TEXT("That picture is larger than 32 MB.");
		if (!FFileHelper::LoadFileToArray(OutBytes, *Path)) return TEXT("Could not read the file.");
		OutContentType = ZettaImages::SniffContentType(OutBytes);
		if (OutContentType.IsEmpty()) return TEXT("Use a PNG, JPEG or WebP image.");
		return FString();
	}
}

// ─── SZettaRemoteImage ────────────────────────────────────────────────────────

void SZettaRemoteImage::Construct(const FArguments& InArgs)
{
	UrlAttribute = InArgs._Url;
	Size = InArgs._Size;
	ChildSlot
	[
		SNew(SBox).WidthOverride(Size.X).HeightOverride(Size.Y)
		[
			SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Thumb")).Padding(0).HAlign(HAlign_Fill).VAlign(VAlign_Fill)
			[
				SNew(SOverlay)
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(InArgs._Placeholder).ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Visibility_Lambda([this]() { return Brush.IsValid() ? EVisibility::Collapsed : EVisibility::Visible; })
				]
				+ SOverlay::Slot()
				[
					SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
					.Visibility_Lambda([this]() { return Brush.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						SNew(SImage).Image(this, &SZettaRemoteImage::GetBrush)
					]
				]
			]
		]
	];
	SetUrl(UrlAttribute.Get());
}

void SZettaRemoteImage::SetUrl(const FString& Url)
{
	if (Url == Current) return;
	Current = Url;
	Brush = FZettaImageCache::Get().Find(Url);
	if (Brush.IsValid() || Url.IsEmpty()) return;
	TWeakPtr<SZettaRemoteImage> Weak = SharedThis(this);
	FZettaImageCache::Get().Request(Url, [Weak, Url](TSharedPtr<FSlateBrush> Loaded)
	{
		if (TSharedPtr<SZettaRemoteImage> Self = Weak.Pin())
		{
			if (Self->Current == Url) Self->Brush = Loaded;
		}
	});
}

void SZettaRemoteImage::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (UrlAttribute.IsBound()) SetUrl(UrlAttribute.Get());
}

const FSlateBrush* SZettaRemoteImage::GetBrush() const
{
	return Brush.IsValid() ? Brush.Get() : FAppStyle::GetNoBrush();
}

// ─── SZettaImageInput ─────────────────────────────────────────────────────────

void SZettaImageInput::Construct(const FArguments& InArgs)
{
	Size = InArgs._Size;
	OnChanged = InArgs._OnChanged;
	const FText Label = InArgs._Label.IsEmpty() ? LOCTEXT("DropPicture", "Drop a picture here\nor choose one") : InArgs._Label;

	ChildSlot
	[
		SNew(SVerticalBox)
		// Left-aligned at its own size: a fill slot would stretch the drop box (and the ✕ with it) across the panel.
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
		[
			SNew(SBox).WidthOverride(Size.X).HeightOverride(Size.Y)
			[
				SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Thumb")).Padding(4)
				[
					SNew(SOverlay)
					+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(SButton).ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
						.Visibility_Lambda([this]() { return HasImage() ? EVisibility::Collapsed : EVisibility::Visible; })
						.OnClicked_Lambda([this]()
						{
							const FString Path = ZettaUI::PickFile(TEXT("Choose a picture"), ZettaUI::ImageFilter());
							if (!Path.IsEmpty()) Error = LoadFile(Path);
							return FReply::Handled();
						})
						[
							SNew(STextBlock).Text(Label).Justification(ETextJustify::Center).ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
					]
					+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
					[
						SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
						.Visibility_Lambda([this]() { return HasImage() && Brush.IsValid() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
						[
							SAssignNew(Preview, SImage)
							.Image_Lambda([this]() { return Brush.IsValid() ? Brush.Get() : FAppStyle::GetNoBrush(); })
						]
					]
					+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(4)
					[
						SNew(SButton).ButtonStyle(&FZettaStyle::Get().GetWidgetStyle<FButtonStyle>("Zetta.Button.Close"))
				.ContentPadding(FMargin(4))
						.ToolTipText(LOCTEXT("ClearPicture", "Remove the picture"))
						.Visibility_Lambda([this]() { return HasImage() ? EVisibility::Visible : EVisibility::Collapsed; })
						.OnClicked_Lambda([this]() { Clear(); return FReply::Handled(); })
						[
							SNew(SBox).WidthOverride(10).HeightOverride(10)[SNew(SImage).Image(FAppStyle::GetBrush("Icons.X")).ColorAndOpacity(FLinearColor::White)]
						]
					]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
		[
			ZettaUI::ErrorText(MakeAttributeLambda([this]() { return FText::FromString(Error); }))
		]
	];
}

FString SZettaImageInput::LoadFile(const FString& Path)
{
	TArray<uint8> Data;
	FString Type;
	const FString Problem = ZettaUI::ReadImageFile(Path, Data, Type);
	if (!Problem.IsEmpty()) return Problem;
	SetImage(Data, FPaths::GetCleanFilename(Path));
	return FString();
}

void SZettaImageInput::SetImage(const TArray<uint8>& InBytes, const FString& Name)
{
	Bytes = InBytes;
	ContentType = ZettaImages::SniffContentType(Bytes);
	FileName = Name;
	Error.Reset();
	Refresh();
	OnChanged.ExecuteIfBound();
}

void SZettaImageInput::Clear()
{
	Bytes.Reset();
	ContentType.Reset();
	FileName.Reset();
	Brush.Reset();
	Error.Reset();
	OnChanged.ExecuteIfBound();
}

void SZettaImageInput::Refresh()
{
	Brush = FZettaImageCache::Get().FromBytes(FString::Printf(TEXT("input:%s:%d:%u"), *FileName, Bytes.Num(), FCrc::MemCrc32(Bytes.GetData(), Bytes.Num())), Bytes);
}

FReply SZettaImageInput::OnDragOver(const FGeometry&, const FDragDropEvent& Event)
{
	const TSharedPtr<FExternalDragOperation> Op = Event.GetOperationAs<FExternalDragOperation>();
	return Op.IsValid() && Op->HasFiles() ? FReply::Handled() : FReply::Unhandled();
}

FReply SZettaImageInput::OnDrop(const FGeometry&, const FDragDropEvent& Event)
{
	const TSharedPtr<FExternalDragOperation> Op = Event.GetOperationAs<FExternalDragOperation>();
	if (!Op.IsValid() || !Op->HasFiles() || Op->GetFiles().Num() == 0) return FReply::Unhandled();
	Error = LoadFile(Op->GetFiles()[0]);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
