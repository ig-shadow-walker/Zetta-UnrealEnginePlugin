// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Input/DragAndDrop.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "ZettaPipelineRecipe.h"
#include "ZettaPipelineRuns.h"

class SOverlay;
class SEditableTextBox;
class SMultiLineEditableTextBox;
struct FSlateBrush;

/** Where the Pipelines page opens its pop-ups (dialogs, pickers): over the whole page, dimming it. */
struct FZettaPlOverlay
{
	TWeakPtr<SOverlay> Host;
	/** Shows a card; returns what closes it. */
	TFunction<void()> Show(const TSharedRef<SWidget>& Card) const;
};

using FZettaPlOverlayRef = TSharedRef<FZettaPlOverlay>;

/** A choice for a segmented control or a drop-down. */
struct FZettaPlOption
{
	FString Value;
	FString Label;
	FString Hint;
};

/** The pipeline page's look and small shared controls. */
namespace ZettaPlUI
{
	/** The hue each step wears: plate (fill), edge (outline, lit wires) and hi (its mark). */
	struct FHue
	{
		FLinearColor Plate, Edge, Hi;
	};
	FHue HueOf(const FString& Kind);
	FHue Amber();
	FString GlyphOf(const FString& Kind);
	/** The kind's mark on a plate in its area's hue. */
	TSharedRef<SWidget> HueIcon(const FString& Kind, float Size = 22.f);

	FLinearColor ToneColour(EZettaRunTone Tone);
	TSharedRef<SWidget> RunDot(EZettaRunTone Tone, const FText& Tooltip = FText());
	TSharedRef<SWidget> RunPill(const TAttribute<FString>& Status);
	/** A note in the panel's words; tone "error", "amber", "dashed" or empty. */
	TSharedRef<SWidget> Note(const TAttribute<FText>& Text, const FString& Tone = FString());
	TSharedRef<SWidget> Heading(const FText& Text);
	TSharedRef<SWidget> Field(const FText& Label, const TSharedRef<SWidget>& Control, const FText& Aside = FText());
	TSharedRef<SWidget> SmallButton(const TAttribute<FText>& Text, FSimpleDelegate OnClicked, const TAttribute<bool>& Enabled = true, const FText& Tooltip = FText(), bool bPrimary = false);
	TSharedRef<SWidget> GhostButton(const TAttribute<FText>& Text, FSimpleDelegate OnClicked, const TAttribute<bool>& Enabled = true, const FText& Tooltip = FText());
	/** Segmented buttons; an option whose DisabledReason is not empty is greyed out with it as its tooltip. */
	TSharedRef<SWidget> Segmented(const TArray<FZettaPlOption>& Options, TFunction<FString()> Value, TFunction<void(const FString&)> OnPick,
		TFunction<FString(const FString&)> DisabledReason = nullptr);
	TSharedRef<SWidget> Dropdown(const TArray<FZettaPlOption>& Options, TFunction<FString()> Value, TFunction<void(const FString&)> OnPick, const TAttribute<bool>& Enabled = true);
	TSharedRef<SWidget> Check(const TAttribute<FText>& Label, TFunction<bool()> Value, TFunction<void(bool)> OnChanged, const FText& Tooltip = FText(), const TAttribute<bool>& Enabled = true);
	TArray<FZettaPlOption> Options(const TArray<FZettaSettingOption>& Settings);

	/** A small notification in the editor's corner. */
	void Toast(const FString& Message, bool bError = false);
	/** A yes/no question in a native dialog. */
	bool Confirm(const FString& Title, const FString& Message);
	/** A dialog card: title, lead, body, footer — for FZettaPlOverlay::Show. */
	TSharedRef<SWidget> Dialog(const FText& Title, const FText& Lead, const TSharedRef<SWidget>& Body, const TSharedRef<SWidget>& Footer, float Width = 640.f);

	/** A picture file chosen from disk (PNG, JPEG, WebP). Empty when cancelled. */
	FString PickPicture(const FString& Title);
	/** The URL without its query — signed URLs change on every read, the picture does not. */
	FString StableKey(const FString& Url);
}

/** A picture from a URL, a data: URL or bytes, with a placeholder until it loads. Signed URLs are cached without their query. */
class SZettaPlThumb : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaPlThumb) : _Size(FVector2D(48, 48)) {}
		SLATE_ATTRIBUTE(FString, Url)
		SLATE_ARGUMENT(FVector2D, Size)
		SLATE_ATTRIBUTE(FText, Placeholder)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	/** Shows bytes already in memory (a seed's preview). */
	void SetBytes(const TArray<uint8>& Bytes);

private:
	void Load(const FString& Url);
	TAttribute<FString> UrlAttribute;
	FString CurrentKey;
	TSharedPtr<FSlateBrush> Brush;
};

/**
 * A text box bound to a value: shows the value while it is not being typed in, and reports every change. Typing keeps
 * its caret even as the graph changes underneath (the value comes back as typed).
 */
class SZettaPlText : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaPlText) : _MultiLine(false), _MaxLength(0) {}
		SLATE_ARGUMENT(bool, MultiLine)
		SLATE_ARGUMENT(int32, MaxLength)
		SLATE_ATTRIBUTE(FText, Hint)
		SLATE_ATTRIBUTE(bool, IsEnabled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TFunction<FString()> InValue, TFunction<void(const FString&)> InOnChanged);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	FString GetText() const;
	void SetText(const FString& Text);
	/** Enter (without Shift) in a multi-line box. */
	TFunction<void()> OnEnter;

private:
	bool IsTyping() const;
	TFunction<FString()> Value;
	TFunction<void(const FString&)> OnChanged;
	TSharedPtr<SEditableTextBox> Single;
	TSharedPtr<SMultiLineEditableTextBox> Multi;
	int32 MaxLength = 0;
	FString Shown;
	bool bSetting = false;
};

/** Content that runs a callback every frame it is shown — for lists rebuilt when what they show changes. */
class SZettaPlTicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaPlTicker) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TFunction<void()> InOnTick)
	{
		OnTick = MoveTemp(InOnTick);
		ChildSlot[InArgs._Content.Widget];
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
		if (OnTick) OnTick();
	}

private:
	TFunction<void()> OnTick;
};

/** Takes files dropped from the desktop and assets dropped from the Content Browser. */
class SZettaPlDropTarget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaPlDropTarget) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	/** Files from the desktop (absolute paths), or assets (object paths). */
	TFunction<void(const TArray<FString>& /*Files*/, const TArray<FString>& /*Assets*/)> OnDropped;
	TAttribute<bool> Accepts = true;

	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
};

/** A palette step being dragged onto the canvas. */
class FZettaStepDragOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FZettaStepDragOp, FDragDropOperation)
	FString Kind;
	FString Label;
	static TSharedRef<FZettaStepDragOp> New(const FString& Kind, const FString& Label);
	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;
};

/** A library picture someone picked. */
struct FZettaPickedImage
{
	int64 Id = 0;
	FString Url;
	FString Name;
};

/** The user's library, in a pop-up: pictures (Image Studio) or finished 3D models. */
namespace ZettaPlPickers
{
	/** OnPick answers whether the picture is now chosen (a second press unchooses it). Single pick closes on the first. */
	void PickImages(const FZettaPlOverlayRef& Overlay, bool bMultiple, TFunction<TArray<int64>()> Chosen, TFunction<bool(const FZettaPickedImage&)> OnPick);
	void PickModels(const FZettaPlOverlayRef& Overlay, TFunction<TArray<int64>()> Chosen, TFunction<bool(const FZettaGenerationPtr&)> OnPick);
}

/**
 * PICTURES OF EACH SIDE — the Studio's four multi-view slots (front, back, left, right), each uploaded, picked from
 * the user's images, or dropped in. Front is required. The set is keyed by side as the web keeps it:
 * { front: { uploadKey | gen2dPostId, name, thumbnail } }; changes go through an updater, so two uploads finishing
 * together never overwrite each other.
 */
class SZettaViewSlots : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaViewSlots) {}
		SLATE_ARGUMENT(FText, Label)
		SLATE_ATTRIBUTE(bool, IsLocked)
	SLATE_END_ARGS()

	using FUpdate = TFunction<TSharedRef<FJsonObject>(const TSharedRef<FJsonObject>&)>;
	void Construct(const FArguments& InArgs, const FZettaPlOverlayRef& InOverlay, TFunction<TSharedPtr<FJsonObject>()> InValue, TFunction<void(FUpdate)> InUpdate);

	/** A set with its front: what makes it a run (or a texturing reference). */
	static bool HasFront(const TSharedPtr<FJsonObject>& Set);

private:
	TSharedRef<SWidget> MakeSlot(const FString& Key, const FText& Name);
	void Put(const FString& Key, const TSharedPtr<FJsonObject>& Picture);
	void Upload(const FString& Key, const TArray<uint8>& Bytes, const FString& ContentType, const FString& Name);
	FZettaPlOverlayRef Overlay = MakeShared<FZettaPlOverlay>();
	TFunction<TSharedPtr<FJsonObject>()> Value;
	TFunction<void(FUpdate)> Update;
	TSet<FString> Uploading;
	FString Error;
	TAttribute<bool> Locked;
};
