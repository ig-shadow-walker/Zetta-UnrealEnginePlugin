// Copyright Zetta 3D. See LICENSE.md.

#include "SZettaImageList.h"

#include "DragAndDrop/AssetDragDropOp.h"
#include "Engine/Texture.h"
#include "Input/DragAndDrop.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "ZettaImageCache.h"
#include "ZettaStudioAssets.h"
#include "ZettaStudioUI.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

void SZettaImageList::Construct(const FArguments& InArgs)
{
	Max = FMath::Max(1, InArgs._Max);
	bNumbered = InArgs._Numbered;
	OnChanged = InArgs._OnChanged;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[ZettaStudioUI::FieldLabel(InArgs._Label)]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				ZettaStudioUI::Hint(MakeAttributeLambda([this]() { return Max > 1 ? FText::Format(LOCTEXT("ListCount", "{0}/{1}"), List.Num(), Max) : FText(); }))
			]
		]
		+ SVerticalBox::Slot().AutoHeight()[SAssignNew(Row, SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.f, 6.f))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)[ZettaStudioUI::Hint(InArgs._Hint)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)[ZettaStudioUI::Error(MakeAttributeLambda([this]() { return FText::FromString(Error); }))]
	];
	Render();
}

bool SZettaImageList::Add(const FItem& Item)
{
	if (Item.PostId.IsSet() && List.ContainsByPredicate([&Item](const FItem& I) { return I.PostId == Item.PostId; })) return true;
	if (Item.Bytes.Num() > 0)
	{
		const FString Problem = ZettaStudioAssets::CheckImage(Item.Bytes, Item.ContentType);
		if (!Problem.IsEmpty())
		{
			Error = Problem;
			return false;
		}
	}
	if (Max == 1) List.Reset();
	if (List.Num() >= Max)
	{
		Error = FString::Printf(TEXT("Only %d images are allowed here, so one was left out."), Max);
		return false;
	}
	Error.Reset();
	List.Add(Item);
	Render();
	OnChanged.ExecuteIfBound();
	return true;
}

void SZettaImageList::ClearImages()
{
	List.Reset();
	Error.Reset();
	Render();
	OnChanged.ExecuteIfBound();
}

void SZettaImageList::Render()
{
	if (!Row.IsValid()) return;
	Row->ClearChildren();
	Brushes.Reset();
	for (int32 i = 0; i < List.Num(); ++i)
	{
		const FItem& Item = List[i];
		const TSharedPtr<FSlateBrush> Brush = FZettaImageCache::Get().FromBytes(
			FString::Printf(TEXT("list:%u:%d"), FCrc::MemCrc32(Item.Bytes.GetData(), Item.Bytes.Num()), Item.Bytes.Num()), Item.Bytes);
		Brushes.Add(Brush);
		const int32 Index = i;
		Row->AddSlot()
		[
			SNew(SBox).WidthOverride(72).HeightOverride(72).ToolTipText(FText::FromString(Item.Name))
			[
				SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Thumb")).Padding(2)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
						[
							SNew(SImage).Image(Brush.IsValid() ? Brush.Get() : FAppStyle::GetNoBrush())
						]
					]
					+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(2)
					[
						SNew(SBox).Visibility(bNumbered && List.Num() > 1 ? EVisibility::Visible : EVisibility::Collapsed)
						[
							ZettaStudioUI::Chip(FText::AsNumber(i + 1))
						]
					]
					+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(4)
					[
						SNew(SButton)
						.ButtonStyle(&FZettaStyle::Get().GetWidgetStyle<FButtonStyle>("Zetta.Button.Close"))
				.ContentPadding(FMargin(4))
						.ToolTipText(LOCTEXT("RemovePicture", "Remove"))
						.OnClicked_Lambda([this, Index]()
						{
							if (List.IsValidIndex(Index)) List.RemoveAt(Index);
							Error.Reset();
							Render();
							OnChanged.ExecuteIfBound();
							return FReply::Handled();
						})
						[
							SNew(SBox).WidthOverride(10).HeightOverride(10)[SNew(SImage).Image(FAppStyle::GetBrush("Icons.X")).ColorAndOpacity(FLinearColor::White)]
						]
					]
				]
			]
		];
	}
	if (List.Num() < Max || Max == 1)
	{
		Row->AddSlot()
		[
			SNew(SBox).WidthOverride(72).HeightOverride(72)
			[
				SNew(SButton)
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				.ToolTipText(LOCTEXT("AddPictureTip", "Click to choose, or drop pictures (or textures from the Content Browser) here."))
				.OnClicked_Lambda([this]()
				{
					Browse();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(List.Num() > 0 && Max == 1 ? LOCTEXT("Replace", "Replace") : LOCTEXT("AddPicture", "+ Add"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]
		];
	}
}

void SZettaImageList::Browse()
{
	for (const FString& Path : ZettaUI::PickFiles(TEXT("Choose pictures"), ZettaUI::ImageFilter())) AddFile(Path);
}

void SZettaImageList::AddFile(const FString& Path)
{
	FItem Item;
	const FString Problem = ZettaUI::ReadImageFile(Path, Item.Bytes, Item.ContentType);
	if (!Problem.IsEmpty())
	{
		Error = Problem;
		return;
	}
	Item.Name = FPaths::GetCleanFilename(Path);
	Add(Item);
}

FReply SZettaImageList::OnDragOver(const FGeometry&, const FDragDropEvent& Event)
{
	if (const TSharedPtr<FExternalDragOperation> External = Event.GetOperationAs<FExternalDragOperation>())
	{
		return External->HasFiles() ? FReply::Handled() : FReply::Unhandled();
	}
	return Event.GetOperationAs<FAssetDragDropOp>().IsValid() ? FReply::Handled() : FReply::Unhandled();
}

FReply SZettaImageList::OnDrop(const FGeometry&, const FDragDropEvent& Event)
{
	if (const TSharedPtr<FExternalDragOperation> External = Event.GetOperationAs<FExternalDragOperation>())
	{
		if (!External->HasFiles()) return FReply::Unhandled();
		for (const FString& Path : External->GetFiles()) AddFile(Path);
		return FReply::Handled();
	}
	if (const TSharedPtr<FAssetDragDropOp> Assets = Event.GetOperationAs<FAssetDragDropOp>())
	{
		for (const FAssetData& Asset : Assets->GetAssets())
		{
			UTexture* Texture = Cast<UTexture>(Asset.GetAsset());
			if (!Texture)
			{
				Error = TEXT("Drop pictures or textures here.");
				continue;
			}
			FItem Item;
			if (ZettaStudioAssets::TextureBytes(Texture, Item.Bytes, Item.ContentType, Item.Name)) Add(Item);
			else Error = TEXT("That texture's pixels could not be read.");
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
