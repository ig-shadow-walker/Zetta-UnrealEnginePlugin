// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "SZettaStudioTool.h"
#include "SZettaSubmitBar.h"
#include "ZettaModels.h"

class SZettaImageInput;
class SZettaMultiViewInput;
class SZettaSourcePicker;
class SMultiLineEditableTextBox;
struct FSlateBrush;

/**
 * A finishing tool (Segment, Retopology, Smart UV, Texture, Auto Rig): a source model, the tool's options, the
 * live price, and a job body with only the fields the web Studio sends.
 */
class SZettaPostProcessTool : public SZettaStudioTool
{
public:
	using FOptionsDone = TFunction<void(const FString& /*Error*/, const TSharedPtr<FJsonObject>& /*Options*/)>;

	/** A library row as the source (from "Send to tool", or the Content Browser). */
	void SetSource(const FZettaGenerationPtr& Generation);
	virtual void OnContextChanged() override;

protected:
	/** Builds the frame around the tool's options; each tool calls it from its Construct. */
	void ConstructTool();

	/** One line saying what the tool does. */
	virtual FText Intro() const = 0;
	/** The operation code the job runs (and is priced by). */
	virtual FString Code() const = 0;
	/** The model the price row is looked up with (only retopology has model rows). */
	virtual FString PriceModel() const { return FString(); }
	virtual FText ButtonText() const = 0;
	virtual TSharedRef<SWidget> BuildOptions() = 0;
	/** The job's fields besides spec / sourcePostId. */
	virtual void MakeOptions(FOptionsDone Done) = 0;
	/** Why the job cannot go yet, or empty. */
	virtual FString Problem() const { return FString(); }
	/** Why this tool refuses a row, or empty. */
	virtual FString Refuse(const FZettaGeneration& Row) const;
	virtual void OnSourceChanged() {}

	FZettaGenerationPtr Source() const;
	/** Re-price and re-check; call after any option changes. */
	void Refresh();
	void Stage(const FString& Text);

	TSharedPtr<SZettaSourcePicker> Picker;
	TSharedPtr<SZettaSubmitBar> Bar;

private:
	void Submit(SZettaSubmitBar::FFinish Finish);
};

/** Splits a model into named parts: Segment (as it is) or Smart Segment (rebuilds as it splits). */
class SZettaSegmentTool : public SZettaPostProcessTool
{
public:
	SLATE_BEGIN_ARGS(SZettaSegmentTool) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs) { ConstructTool(); }

protected:
	virtual FText Intro() const override;
	virtual FString Code() const override;
	virtual FText ButtonText() const override;
	virtual TSharedRef<SWidget> BuildOptions() override;
	virtual void MakeOptions(FOptionsDone Done) override;

private:
	FString Mode = TEXT("segment");
	FString Granularity = TEXT("balanced");
	bool bSplitConnectivity = true;
	FString PromptText;
};

/** Rebuilds a model's topology: Zetta R1 to an exact polygon budget, Zetta N1 at a level of detail. */
class SZettaRetopologyTool : public SZettaPostProcessTool
{
public:
	SLATE_BEGIN_ARGS(SZettaRetopologyTool) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs) { ConstructTool(); }
	virtual void OnContextChanged() override;

protected:
	virtual FText Intro() const override;
	virtual FString Code() const override { return TEXT("smart_topology"); }
	virtual FString PriceModel() const override { return Engine; }
	virtual FText ButtonText() const override;
	virtual TSharedRef<SWidget> BuildOptions() override;
	virtual void MakeOptions(FOptionsDone Done) override;
	virtual FString Problem() const override;
	virtual FString Refuse(const FZettaGeneration& Row) const override;

private:
	int32 Max() const;
	TOptional<int32> Count() const;
	bool OutOfRange() const;
	bool QuadsLocked() const;

	FString Engine = TEXT("r1");
	FString PolygonType = TEXT("triangle");
	/** The budget exactly as typed — free text, so a number over the ceiling stays and is named out of range. */
	FString CountText = TEXT("8000");
	FString Level = TEXT("medium");
	bool bBake = true;
};

/**
 * Smart UV: a clean new UV layout, with the model's textures, rig and animation carried across onto it. The
 * server refuses a model over 80,000 triangles, so that is said before the press.
 */
class SZettaSmartUvTool : public SZettaPostProcessTool
{
public:
	SLATE_BEGIN_ARGS(SZettaSmartUvTool) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs) { ConstructTool(); }

	/** The source's triangle count: quads count twice. Unset when the row does not say. */
	static TOptional<int64> Triangles(const FZettaGenerationPtr& Row);

protected:
	virtual FText Intro() const override;
	virtual FString Code() const override { return TEXT("smart_uv"); }
	virtual FText ButtonText() const override;
	virtual TSharedRef<SWidget> BuildOptions() override;
	virtual void MakeOptions(FOptionsDone Done) override;
	virtual FString Problem() const override;
	virtual FString Refuse(const FZettaGeneration& Row) const override;

private:
	FString Orientation = TEXT("as-is");
	bool bBake = true;
	bool bCompress = false;
};

/**
 * Re-textures a model from a reference picture (or four views). A description of the surface is first turned
 * into a new reference picture you approve ("Preview"), then applied.
 */
class SZettaTextureTool : public SZettaPostProcessTool
{
public:
	SLATE_BEGIN_ARGS(SZettaTextureTool) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs) { ConstructTool(); }

protected:
	virtual FText Intro() const override;
	virtual FString Code() const override { return Tier; }
	virtual FText ButtonText() const override;
	virtual TSharedRef<SWidget> BuildOptions() override;
	virtual void MakeOptions(FOptionsDone Done) override;
	virtual FString Problem() const override;
	virtual void OnSourceChanged() override;

private:
	FString Description() const { return DescribeText.TrimStartAndEnd(); }
	FString SourceImageUrl() const;
	FString ImportedAsset() const;
	FText ReferenceHint() const;
	void ClearPreview();
	void Preview();
	/** The picture a description is applied to: the user's, the model's input, or a render of the imported copy. */
	void Reference(bool bRequired, TFunction<void(const FString& /*Error*/, const TArray<uint8>&, const FString& /*Type*/)> Done);

	FString Mode = TEXT("single");
	FString Tier = TEXT("retexture");
	bool bFast = false;
	FString Alignment = TEXT("original_image");
	bool bPbr = true;
	bool bDelight = true;
	bool bBake = true;
	bool bRemoveBackground = true;
	FString DescribeText;

	TSharedPtr<SZettaImageInput> Picture;
	TSharedPtr<SZettaMultiViewInput> Multi;
	TArray<uint8> Approved;
	FString ApprovedType;
	FString ApprovedFor;
	TSharedPtr<FSlateBrush> ApprovedBrush;
	FString PreviewError;
	bool bPreviewing = false;
	bool bSettingPicture = false;
	/** The source's imported copy in this project, looked up once per source (a disk read). */
	mutable int64 ImportedFor = -1;
	mutable FString ImportedPath;
};

/** Adds a skeleton and skin weights, ready to animate. */
class SZettaAutoRigTool : public SZettaPostProcessTool
{
public:
	SLATE_BEGIN_ARGS(SZettaAutoRigTool) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs) { ConstructTool(); }

protected:
	virtual FText Intro() const override;
	virtual FString Code() const override { return TEXT("auto_rigging"); }
	virtual FText ButtonText() const override;
	virtual TSharedRef<SWidget> BuildOptions() override;
	virtual void MakeOptions(FOptionsDone Done) override;
	virtual FString Problem() const override;
	virtual void OnSourceChanged() override { Note.Reset(); }

private:
	void Detect();
	void DetectFrom(const TArray<uint8>& Jpeg);

	FString Body = TEXT("biped");
	/** Bone naming: the UE5 Mannequin first and by default — it retargets straight onto Unreal's animations. */
	FString Spec = TEXT("unreal");
	FString Format = TEXT("fbx");
	FString Note;
	bool bDetecting = false;
};
