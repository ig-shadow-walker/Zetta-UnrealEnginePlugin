// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SWrapBox;
struct FSlateBrush;

/**
 * A small ordered set of pictures (style references, the image to edit, images to compose): thumbnails with
 * remove buttons and an Add tile that takes files from disk, textures dragged from the Content Browser, or
 * files dropped from the desktop. Items can also be library pictures (a post id) so they are not sent again.
 */
class SZettaImageList : public SCompoundWidget
{
public:
	struct FItem
	{
		TArray<uint8> Bytes;
		FString ContentType;
		FString Name;
		TOptional<int64> PostId;
	};

	SLATE_BEGIN_ARGS(SZettaImageList) : _Max(1), _Numbered(false) {}
		SLATE_ARGUMENT(FText, Label)
		SLATE_ARGUMENT(FText, Hint)
		SLATE_ARGUMENT(int32, Max)
		SLATE_ARGUMENT(bool, Numbered)
		SLATE_EVENT(FSimpleDelegate, OnChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	const TArray<FItem>& Items() const { return List; }
	/** Adds (or, for a one-picture list, replaces with) a picture. False, with the reason shown, when refused. */
	bool Add(const FItem& Item);
	void ClearImages();

	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

private:
	void Render();
	void Browse();
	void AddFile(const FString& Path);

	TArray<FItem> List;
	TArray<TSharedPtr<FSlateBrush>> Brushes;
	TSharedPtr<SWrapBox> Row;
	FString Error;
	int32 Max = 1;
	bool bNumbered = false;
	FSimpleDelegate OnChanged;
};
