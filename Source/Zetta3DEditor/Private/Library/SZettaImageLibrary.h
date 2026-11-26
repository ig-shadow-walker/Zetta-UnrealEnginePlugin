// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "ZettaModels.h"

class SWrapBox;

/** The 2D half of the library: the user's pictures, ready to save into the project as textures. */
class SZettaImageLibrary : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaImageLibrary) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Loads the first page the first time the tab is shown. */
	void EnsureLoaded();
	void Reload();
	/** Forgets what was loaded (signed out). */
	void Reset();

private:
	void Load(int32 Page);
	void Redraw();
	TSharedRef<SWidget> MakeTile(const FZettaImagePost& Post);
	void Save(const FZettaImagePost& Post);
	void Delete(const FZettaImagePost& Post);

	TArray<FZettaImagePost> Posts;
	TSet<int64> Saving;
	FString Search;
	FString Error;
	int32 PageNumber = 1;
	int32 Total = 0;
	int32 Request = 0;
	bool bLoaded = false;
	bool bLoading = false;
	double LastTyped = 0;
	bool bSearchPending = false;

	TSharedPtr<SWrapBox> Grid;
};
