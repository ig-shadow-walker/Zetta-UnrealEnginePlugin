// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "ZettaModels.h"

class SInlineEditableTextBlock;
class SZettaLibraryPage;

/** Small pieces the library's tiles and pop-ups share. */
namespace ZettaLibraryUI
{
	/** Width of one tile in the grid; the grid wraps as many as fit. */
	inline constexpr float TileWidth = 184.f;
	inline constexpr float ThumbSize = 166.f;

	/** A rounded tag ("Ready", "In project", "Pipeline"). An unset colour is the normal text colour. */
	TSharedRef<SWidget> Chip(const FText& Text, const FSlateColor& Color = FSlateColor::UseForeground(), const FText& Tooltip = FText());
	/** The "Pipeline" tag; the run's number rides in the tooltip. */
	TSharedRef<SWidget> PipelineChip(int64 RunId);
	FSlateColor StatusColor(const FZettaGeneration& Row);
}

/**
 * One model in the library: thumbnail, title (renamed in place), its tags, the import progress bar and its
 * actions. Click picks it while selecting; double-click places it in the level (importing it first if needed);
 * dragging it — once it is in the project — hands the editor its meshes, so the level viewport or the Content
 * Browser take the drop as they would from the Content Browser.
 */
class SZettaGenerationTile : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaGenerationTile) {}
		SLATE_ARGUMENT(TWeakPtr<SZettaLibraryPage>, Page)
		SLATE_ARGUMENT(FZettaGenerationPtr, Row)
		/** The pipeline run it belongs to, when it is drawn inside the run's pop-up. */
		SLATE_ARGUMENT(TSharedPtr<FZettaRunGroup>, Group)
		/** Its step tag inside the run ("Step 2 · Retopology"). */
		SLATE_ARGUMENT(FString, Step)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Turns the title into a text box (the menu's Rename…). */
	void BeginRename();

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	TSharedRef<SWidget> MakeActions();
	FText TooltipText() const;
	bool InProject() const;
	bool CanUseInLevel() const;

	TWeakPtr<SZettaLibraryPage> Page;
	FZettaGenerationPtr Row;
	TSharedPtr<FZettaRunGroup> Group;
	TSharedPtr<SInlineEditableTextBlock> TitleBlock;
	bool bRenaming = false;
	bool bPressed = false;
};

/**
 * A pipeline run's models as one tile (the web's RunGroupTile): a deck — the run's cover on the top card, up to
 * two more fanned behind it — with the model count, "Still making" while it runs, its name and the Pipeline tag.
 * Clicked, the run opens in a pop-up; while selecting, a click takes all of the run's models instead.
 */
class SZettaRunTile : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaRunTile) {}
		SLATE_ARGUMENT(TWeakPtr<SZettaLibraryPage>, Page)
		SLATE_ARGUMENT(TSharedPtr<FZettaRunGroup>, Group)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	TWeakPtr<SZettaLibraryPage> Page;
	TSharedPtr<FZettaRunGroup> Group;
	bool bPressed = false;
};
