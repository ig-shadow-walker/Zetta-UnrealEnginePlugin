// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "SZettaStudioTool.h"
#include "ZettaModels.h"

class SWrapBox;
class SZettaImageList;
struct FSlateBrush;

/**
 * Image Studio: generate pictures from a prompt, edit one, or compose several into one — then save them into the
 * project as textures, or send one on to Generate 3D.
 */
class SZettaImageStudioTool : public SZettaStudioTool
{
public:
	/** Sends a picture to Generate 3D's image input. */
	using FUseFor3D = TFunction<void(const TArray<uint8>& /*Bytes*/, const FString& /*Name*/)>;

	SLATE_BEGIN_ARGS(SZettaImageStudioTool) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FUseFor3D InUseFor3D);
	virtual void OnContextChanged() override;
	virtual void OnShown() override;

	/** Opens Edit with this picture (from a result, or the library). */
	void StartEdit(const TArray<uint8>& Bytes, const FString& ContentType, const TOptional<int64>& PostId, const FString& Label);

private:
	struct FResult
	{
		TArray<uint8> Bytes;
		FString ContentType;
		FString Prompt;
		FString AspectRatio;
		FString Resolution;
		TOptional<int64> PostId;
		TSharedPtr<FSlateBrush> Brush;
	};

	const FZettaImageModel* ActiveModel() const;
	bool EditLocked() const;
	FString ResolutionLock(const FString& Size) const;
	void FitResolution();
	FString Prompt() const { return PromptText.TrimStartAndEnd(); }
	FString NotReady() const;
	FText SubmitText() const;
	void Submit();
	void ShowResults(const TArray<TArray<uint8>>& Images, const TArray<FString>& Types, const FString& ForPrompt, const FString& AspectRatio,
		const FString& InResolution, const TOptional<int64>& PostId);
	void RenderResults();
	TSharedRef<SWidget> ResultCard(int32 Index);
	void Save(int32 Index);
	void LoadHistory();
	void Open(const FZettaImagePost& Post);

	FUseFor3D UseFor3D;
	FString Mode = TEXT("generate");
	FString Ratio = TEXT("1:1");
	FString Resolution = TEXT("1K");
	FString Model;
	FString PromptText;
	FString Error;
	FString Notice;
	FString HistoryNote;
	bool bBusy = false;
	bool bHistoryLoaded = false;

	TSharedPtr<SZettaImageList> Styles;
	TSharedPtr<SZettaImageList> EditSource;
	TSharedPtr<SZettaImageList> ComposeSources;
	TSharedPtr<SWrapBox> ResultsBox;
	TSharedPtr<SWrapBox> HistoryBox;
	TArray<FResult> Results;
};
