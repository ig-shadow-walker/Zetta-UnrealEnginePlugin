// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SWidgetSwitcher;
class SZettaStudioTool;
class SZettaGenerateTool;
class SZettaPostProcessTool;
class SZettaImageStudioTool;

/**
 * The 3D Studio page: every tool from the web Studio, docked in the editor. The tools are listed on the left,
 * the chosen tool's form fills the right, and the jobs strip runs along the bottom. Every tool is built once and
 * kept, so switching tools never loses what was typed.
 */
class SZettaStudioPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaStudioPage) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SZettaStudioPage() override;

	/** Tool ids: Generate, Smart, Segment, Retopology, SmartUv, Texture, AutoRig, ImageStudio. */
	void ShowTool(FName Id);
	void UseImageFor3D(const TArray<uint8>& Bytes, const FString& Name);

private:
	struct FTool
	{
		FName Id;
		FText Label;
		FLinearColor Hue;
		TSharedPtr<SZettaStudioTool> Widget;
	};

	TSharedRef<SWidget> MakeToolList();
	TSharedRef<SWidget> MakeToolButton(const FTool& Tool);
	TSharedRef<SWidget> MakeBanner();
	void OnContextChanged();
	void HandleLink();
	void HandleImage();

	TArray<FTool> Tools;
	FName Active;
	TSharedPtr<SWidgetSwitcher> Switcher;
	TSharedPtr<SZettaGenerateTool> Generate;
	TSharedPtr<SZettaGenerateTool> Smart;
	TSharedPtr<SZettaImageStudioTool> Images;
	TMap<FName, TSharedPtr<SZettaPostProcessTool>> Finishing;
	FDelegateHandle ContextHandle;
	FDelegateHandle LinkHandle;
	FDelegateHandle ImageHandle;
};
