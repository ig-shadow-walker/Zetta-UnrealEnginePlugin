// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "ZettaJobs.h"
#include "ZettaModels.h"

class SWindow;
class SWrapBox;
class SZettaGenerationTile;
class SZettaImageLibrary;

/**
 * My Generations: every 3D model the user has made on Zetta 3D — on the web, in Unreal or another plugin — and,
 * on a second tab, their pictures. Import (with a progress bar), place, drag into the level, rename, delete one
 * or several, send to a Studio tool, fork. A pipeline run's models arrive as one entry (groupRuns) and draw as
 * one tile that opens them in a pop-up. Gaussian splats and worlds made on the web are listed but only open there.
 */
class SZettaLibraryPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaLibraryPage) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SZettaLibraryPage() override;

	// What the tiles ask and tell the page.
	bool IsPicking() const { return bPicking; }
	bool IsPicked(int64 Id) const { return bPicking && Picked.Contains(Id); }
	bool IsDeleting() const { return Deleting.IsSet(); }
	bool IsRunPicked(const FZettaRunGroup& Group) const;
	void TogglePick(int64 Id);
	/** Ticks every model in the run — or, when all of them already are, unticks them. */
	void PickRun(const TSharedPtr<FZettaRunGroup>& Group);

	/** Downloads a model into the project with a progress bar on its tile; bPlace also puts it in the level. */
	void Import(const FZettaGenerationPtr& Row, bool bPlace);
	/** Places a model that is already in the project — no download. Imports it first when its assets are gone. */
	void Place(const FZettaGenerationPtr& Row);
	void Rename(const FZettaGenerationPtr& Row, const FString& NewTitle);
	void Delete(const FZettaGenerationPtr& Row);

	TSharedRef<SWidget> MakeMenu(const FZettaGenerationPtr& Row, const TSharedPtr<FZettaRunGroup>& Group, TWeakPtr<SZettaGenerationTile> Tile);
	TSharedRef<SWidget> MakeRunMenu(const TSharedPtr<FZettaRunGroup>& Group);
	/** A run's models in a pop-up, in run order, with Import all, Delete all and Open run. */
	void ShowRun(const TSharedPtr<FZettaRunGroup>& Group);

private:
	TSharedRef<SWidget> MakeModelsView();
	TSharedRef<SWidget> MakeToolbar();
	TSharedRef<SWidget> MakeSelectionBar();
	TSharedRef<SWidget> MakeEmptyState();

	void Load();
	void Redraw();
	void ClearList();
	bool HasFilters() const;

	void SetPicking(bool bOn);
	bool AllOnPagePicked() const;
	void ToggleAllOnPage();
	void DeleteSelected();
	void DeleteRun(const TSharedPtr<FZettaRunGroup>& Group);
	void ImportRun(const TSharedPtr<FZettaRunGroup>& Group);
	void SaveUvLayout(const FZettaGenerationPtr& Row);

	/** The run as the library holds it now (after a refresh), or the one given. */
	TSharedPtr<FZettaRunGroup> CurrentGroup(const TSharedPtr<FZettaRunGroup>& Group) const;
	void FillRunPopup(const TSharedPtr<FZettaRunGroup>& Group);
	/** The library was drawn again: the open pop-up shows the run as it is now — or closes, when it is gone. */
	void RefreshRunPopup();
	void CloseRunPopup();

	void OnAuthChanged();
	void OnJobFinished(const FZettaJobPtr& Job);
	void OnSearchChanged(const FText& Text);
	void ShowError(const FString& Message);
	void ShowNotice(const FString& Message);

	// Mode: "models" or "images".
	TSharedRef<FString> Mode = MakeShared<FString>(TEXT("models"));
	TSharedPtr<SZettaImageLibrary> Images;

	// Filters
	FString Search;
	TSharedRef<FString> Feature = MakeShared<FString>();
	TSharedRef<FString> StatusFilter = MakeShared<FString>(TEXT("all"));
	double LastTyped = 0;
	bool bSearchPending = false;

	// The page as the server sent it: a pipeline run is one entry carrying its models.
	TArray<FZettaGenerationPtr> Items;
	int32 PageNumber = 1;
	int32 Total = 0;
	int32 Request = 0;
	bool bLoading = false;
	bool bLoaded = false;
	FString Error;
	FString Notice;

	// Select mode and bulk delete
	bool bPicking = false;
	TSet<int64> Picked;
	TOptional<TPair<int32, int32>> Deleting;

	TSharedPtr<SWrapBox> Grid;

	// The run open in its pop-up.
	TWeakPtr<SWindow> RunWindow;
	TSharedPtr<SWrapBox> RunGrid;
	int64 RunWindowId = 0;

	FDelegateHandle AuthHandle;
	FDelegateHandle FinishedHandle;
};
