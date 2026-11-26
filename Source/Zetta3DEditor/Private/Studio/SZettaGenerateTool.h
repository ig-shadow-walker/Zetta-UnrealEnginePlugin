// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "SZettaStudioTool.h"
#include "SZettaSubmitBar.h"
#include "ZettaRequests.h"
#include "ZettaStudioFork.h"

class SMultiLineEditableTextBox;
class SZettaImageInput;
class SZettaMultiViewInput;

/**
 * Generate 3D (Zetta N1 / N2.5) and Smart 3D (Zetta Np1 / Np1.5): from a prompt, a picture or several views.
 * A prompt is drawn as a picture first when the model builds from pictures (and always with Control Mode,
 * which stops to show it before any 3D credits are spent).
 *
 * The settings follow the web Studio's defaults, visibility and lock rules per model capability and plan.
 * Implemented in two parts: the input and submit flow here (SZettaGenerateTool.cpp), the settings and their
 * rules in ZettaGenerateSettings.cpp.
 */
class SZettaGenerateTool : public SZettaStudioTool
{
public:
	SLATE_BEGIN_ARGS(SZettaGenerateTool) : _Smart(false) {}
		SLATE_ARGUMENT(bool, Smart)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SZettaGenerateTool() override;
	virtual void OnContextChanged() override;

	/** A picture handed over from elsewhere (Image Studio, the Content Browser). */
	void SetImage(const TArray<uint8>& Bytes, const FString& Name);
	/** A fork: the row's settings onto the form, its prompt or pictures into the inputs — and nothing run. */
	void ApplyFork(const ZettaStudioFork::FSetup& Fork);

private:
	// ─── Input and submit (SZettaGenerateTool.cpp) ────────────────────────────────
	TSharedRef<SWidget> BuildInput();
	bool IsMultiView() const { return Source == TEXT("multi"); }
	void SetSource(const FString& Value);
	FString Prompt() const;
	bool HasPicture() const;
	bool HasOwnImage() const;
	bool DrawnIsCurrent() const;
	bool SendsBarePrompt() const;
	bool NextIsDrawing() const;
	void OnPicked();
	void OnViewsChanged();
	void SetPicture(const TArray<uint8>& Bytes, const FString& Name, const TOptional<FString>& InDrawnFor);
	void ClearImage();
	void OpenAiEdit();
	void LoadForkPicture(int32 Batch, const ZettaStudioFork::FPicture& Picture, bool bMulti);
	FZettaGenerateOptions Preview() const;
	void Refresh();
	void RequestQuote(const FZettaBuiltRequest& Built, const FString& Model);
	void Submit(SZettaSubmitBar::FFinish Finish);
	void SubmitSingle(SZettaSubmitBar::FFinish Finish);
	void SubmitMultiView(SZettaSubmitBar::FFinish Finish);
	void Draw(TFunction<void(const FString& /*Error*/)> Done);
	void Redraw();
	void Start(FZettaGenerateOptions Options, SZettaSubmitBar::FFinish Finish, TFunction<void()> OnStarted);
	void Stage(const FString& Text);

	// ─── Settings (ZettaGenerateSettings.cpp) ─────────────────────────────────────
	TSharedRef<SWidget> BuildSettings();
	TSharedRef<SWidget> BuildEngineChoice();
	bool QualityRungs() const { return O.Model == TEXT("n1") || O.Model == TEXT("n25"); }
	bool Quads() const { return O.PolygonType == TEXT("quadrilateral"); }
	int32 DefaultCount() const;
	FInt32Range Bounds() const;
	void SetCount(const TOptional<int32>& Count, bool bTyped);
	void TypeCount(const FString& Text);
	FString CountProblem() const;
	FString CountFieldError() const;
	void SetModel(const FString& Model, bool bPicked);
	void ApplyModel(const FString& Model);
	void SetQuality(const FString& Quality);
	void Invariants();
	void ApplySetUp(const ZettaStudioFork::FSettings& Settings);
	void ApplyContextDefaults();
	void SetMultiViewSettings(bool bMulti);
	void SetFile(const FString& FileName);
	void ResetInputs();
	void Changed();
	FString PriceTag(const FString& Code) const;
	FString EngineLock(const FString& Model) const;
	FString QualityLock(const FString& Rung) const;
	FString QualityLockLine() const;
	FString OutputLock(const FString& Output) const;
	FString PolygonLock(const FString& Type) const;
	FString PolygonLockLine() const;
	bool PrivateAllowed() const;
	bool IsPrivate() const;
	FZettaGenerateOptions Snapshot() const;
	TArray<FString> PlanProblems(const FZettaBuiltRequest& Built, int32 Views) const;

	bool bSmart = false;

	// Settings state
	FZettaGenerateOptions O;
	bool bRemoveBackground = true;
	bool bMultiViewSettings = false;
	bool bHasFile = false;
	bool bTitleTouched = false;
	bool bModelPicked = false;
	bool bOutputPicked = false;
	/** The polygon count exactly as it reads in the field (free text, so an out-of-range number stays as typed). */
	FString CountText;
	/** The user typed the count: it survives a topology switch as typed. A rung change resets it. */
	bool bCountTyped = false;

	// Input state
	FString Source = TEXT("single");
	FString PromptText;
	TSharedPtr<SZettaImageInput> Picker;
	TSharedPtr<SZettaMultiViewInput> Multi;
	TSharedPtr<SZettaSubmitBar> Bar;
	/** The picture in the slot was drawn from this prompt (unset = the user's own file). */
	TOptional<FString> DrawnFor;
	bool bSettingImage = false;
	bool bPlacingForkPicture = false;
	bool bBusy = false;
	/** A fork's pictures still on their way, by slot ("image" or a view). A newer fork starts a new batch. */
	TSet<FString> LoadingPictures;
	int32 PictureBatch = 0;
	FString ImageStatus;
	FString InputError;
	FString PictureProblem;
	FString ForkNote;

	// Derived on every change
	FString PromptHintText;

	// Server quote, asked for once the settings stop changing
	FString QuoteKey;
	TOptional<int32> Quoted;
	FTSTicker::FDelegateHandle QuoteTimer;
};
