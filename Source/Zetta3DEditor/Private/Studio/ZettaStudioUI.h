// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"

/**
 * Small building blocks the Studio's tools share, on top of ZettaUI: choices whose options can be locked
 * (with the reason as their tooltip), lines that disappear when they have nothing to say, and the
 * "available on a paid plan" line with its way to the pricing page.
 */
namespace ZettaStudioUI
{
	struct FOption
	{
		FString Key;
		FText Label;
		FText Tooltip;
	};

	/** Why an option cannot be chosen right now (shown as its tooltip), or empty when it can. */
	using FLock = TFunction<FString(const FString& /*Key*/)>;

	/** Toggle buttons for a few choices; wraps onto a second line in a narrow panel. */
	TSharedRef<SWidget> Segmented(TFunction<FString()> Get, TFunction<void(const FString&)> Set, const TArray<FOption>& Options, FLock Lock = nullptr);

	/** A drop-down over choices read each time it opens. Locked choices are greyed, with the reason as their tooltip. */
	TSharedRef<SWidget> Dropdown(TFunction<FString()> Get, TFunction<void(const FString&)> Set, TFunction<TArray<FOption>()> Options, FLock Lock = nullptr);

	TSharedRef<SWidget> Check(const TAttribute<FText>& Label, TFunction<bool()> Get, TFunction<void(bool)> Set,
		const TAttribute<FText>& Tooltip = FText(), const TAttribute<bool>& Enabled = true);

	/** A field's name, above its control. */
	TSharedRef<SWidget> FieldLabel(const TAttribute<FText>& Text);
	/** A field's name with its control below it. */
	TSharedRef<SWidget> Field(const TAttribute<FText>& Label, const TSharedRef<SWidget>& Control);

	/** A quiet line that collapses when it has nothing to say. */
	TSharedRef<SWidget> Hint(const TAttribute<FText>& Text);
	/** An amber line (a limit, a reason) that collapses when empty. */
	TSharedRef<SWidget> Warn(const TAttribute<FText>& Text);
	/** A green line (something went well) that collapses when empty. */
	TSharedRef<SWidget> Success(const TAttribute<FText>& Text);
	/** A red line that collapses when empty. */
	TSharedRef<SWidget> Error(const TAttribute<FText>& Text);
	/** What the plan does not include, with "See plans" opening zetta3d.ai/pricing. Collapses when empty. */
	TSharedRef<SWidget> Locked(const TAttribute<FText>& Reason);

	TSharedRef<SWidget> SmallButton(const TAttribute<FText>& Text, TFunction<void()> OnClicked, const TAttribute<bool>& Enabled = true,
		const TAttribute<FText>& Tooltip = FText());

	/** Shows Content only while When() is true. */
	TSharedRef<SWidget> ShownWhen(TFunction<bool()> When, const TSharedRef<SWidget>& Content);

	/** A raised group for a set of related fields, with an optional title. */
	TSharedRef<SWidget> Group(const FText& Title, const TSharedRef<SWidget>& Content);

	/** A chip: a small rounded label ("Image to 3D", "12,400 faces"). */
	TSharedRef<SWidget> Chip(const FText& Text);

	/** "1,240" with the user's grouping. */
	FString Number(int64 Value);
	/** "+12", "Free", or empty for an unknown price. */
	FString PriceTag(const TOptional<int32>& Credits);

	inline EVisibility Shown(bool bVisible) { return bVisible ? EVisibility::Visible : EVisibility::Collapsed; }
	inline FText Str(const FString& S) { return FText::FromString(S); }
}
