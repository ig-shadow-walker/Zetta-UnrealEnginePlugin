// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "ZettaModels.h"

/**
 * The footer every job tool ends with: the import choices, the live total, the balance, what (if anything)
 * stops the job, the API's answer in plain words, and the button.
 */
class SZettaSubmitBar : public SCompoundWidget
{
public:
	/** Called by the action when it is over: empty on success, otherwise the message to show. */
	using FFinish = TFunction<void(const FString& /*Error*/)>;
	using FAction = TFunction<void(FFinish)>;

	SLATE_BEGIN_ARGS(SZettaSubmitBar) : _ShowImportChoices(true) {}
		SLATE_ATTRIBUTE(FText, ButtonText)
		SLATE_ARGUMENT(bool, ShowImportChoices)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetAction(FAction InAction) { Action = MoveTemp(InAction); }
	/** Unset = unknown (the price list has not loaded, or has no row for it). */
	void SetPrice(const TOptional<int32>& Credits) { Price = Credits; }
	/** Why the button is disabled; empty when the job can go. */
	void SetProblem(const FString& InProblem) { Problem = InProblem; }
	/** What the button says while the job is on its way ("Uploading… 40%"). Cleared when it finishes. */
	void SetStage(const FString& Text) { Stage = Text; }

	void ShowError(const FString& Message) { Error = Message; }
	void ShowDone(const FString& Message) { Done = Message; }
	void ClearMessages() { Error.Reset(); Done.Reset(); }

	bool IsBusy() const { return bBusy; }

	/** Hands a created job to the jobs list with this bar's import choices, and says it started. */
	void Track(const FZettaGenerationPtr& Generation);

private:
	void Submit();
	FText CostText() const;
	FText BalanceText() const;
	bool Shortfall() const;

	TAttribute<FText> ButtonText;
	FAction Action;
	TOptional<int32> Price;
	FString Problem;
	FString Stage;
	FString Error;
	FString Done;
	bool bBusy = false;
	bool bAutoImport = true;
	bool bPlace = true;
};
