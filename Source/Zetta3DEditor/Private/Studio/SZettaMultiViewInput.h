// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SZettaImageInput;

/**
 * Four view slots (Front, Back, Left, Right). Any one is enough; the rest can be drawn from it ("Generate
 * missing views"), which re-renders the same object from each missing angle.
 */
class SZettaMultiViewInput : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaMultiViewInput) {}
		SLATE_EVENT(FSimpleDelegate, OnChanged)
	SLATE_END_ARGS()

	static const TArray<FString>& Slots();
	/** The order views are submitted in; the server promotes front to the primary image. */
	static const TArray<FString>& SubmitOrder();

	struct FView
	{
		FString Slot;
		TArray<uint8> Bytes;
		FString ContentType;
	};

	void Construct(const FArguments& InArgs);

	/** The plan's view allowance (unset = unknown = all four). */
	void SetBudget(const TOptional<int64>& MaxViews);
	int32 Filled() const;
	bool IsBusy() const { return bGenerating; }
	bool HasView(const FString& Slot) const;
	/** The filled views in submission order. */
	TArray<FView> Views() const;
	/** The filled slots in submission order (no picture data copied). */
	TArray<FString> FilledSlots() const;
	/** The file name of the reference view (for the title suggestion). */
	FString ReferenceName() const;
	void SetView(const FString& Slot, const TArray<uint8>& Bytes, const FString& Name);
	void ClearViews();
	/** Set while code (not the user) fills a slot, so the owner can tell the two apart. */
	bool IsSettingFromCode() const { return bSetting; }

private:
	FString ReferenceSlot() const;
	void OnPicked(FString Slot);
	void EditView(FString Slot);
	void GenerateViews();
	FText HelperText() const;
	bool SlotLocked(const FString& Slot) const;

	TMap<FString, TSharedPtr<SZettaImageInput>> Pickers;
	FSimpleDelegate OnChanged;
	FString Error;
	int32 Budget = 4;
	FString BudgetReason;
	bool bGenerating = false;
	bool bSetting = false;
};
