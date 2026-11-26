// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "ZettaModels.h"

class SBox;
class SWrapBox;

/**
 * Chooses the model a finishing tool works on: a finished row from the library, a model file from disk
 * (GLB or FBX, uploaded to the library for free), or the asset selected in the Content Browser (its own
 * generation when Zetta 3D imported it, otherwise the file it was imported from).
 */
class SZettaSourcePicker : public SCompoundWidget
{
public:
	/** Why a row cannot be used by the tool, or empty when it can. */
	using FRefuse = TFunction<FString(const FZettaGeneration&)>;

	SLATE_BEGIN_ARGS(SZettaSourcePicker) {}
		SLATE_EVENT(FSimpleDelegate, OnChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FRefuse InRefuse);
	virtual ~SZettaSourcePicker() override;

	const FZettaGenerationPtr& GetSource() const { return Source; }
	/** Makes a row the source, unless the tool refuses it (the reason is shown instead). */
	void Set(const FZettaGenerationPtr& Generation);
	/** Forgets the chosen model and the loaded library (after sign-out). */
	void Forget();

private:
	void RenderCurrent();
	void ToggleBrowser();
	void Reload();
	void LoadPage(int32 Page);
	TSharedRef<SWidget> Tile(const FZettaGenerationPtr& Row);
	void UploadFile();
	void UseSelected();
	void SearchChanged(const FText& Text);

	FRefuse Refuse;
	FSimpleDelegate OnChanged;
	FZettaGenerationPtr Source;
	TSharedPtr<SBox> Current;
	TSharedPtr<SWrapBox> Grid;
	TArray<FZettaGenerationPtr> Rows;
	FString Search;
	FString Status;
	FString Error;
	int32 PageLoaded = 0;
	int32 LoadTicket = 0;
	bool bBrowserOpen = false;
	bool bLoading = false;
	bool bHasMore = false;
	bool bWorking = false;
	FTSTicker::FDelegateHandle SearchTimer;
};
