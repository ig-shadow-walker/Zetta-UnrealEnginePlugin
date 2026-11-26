// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "ZettaCatalog.h"

class SImage;
class SBox;

/** Building blocks every page uses, so the window reads as one thing. */
namespace ZettaUI
{
	ZETTA3DEDITOR_API TSharedRef<SWidget> Title(const FText& Text);
	ZETTA3DEDITOR_API TSharedRef<SWidget> Section(const FText& Text);
	ZETTA3DEDITOR_API TSharedRef<SWidget> Hint(const TAttribute<FText>& Text);
	ZETTA3DEDITOR_API TSharedRef<SWidget> ErrorText(const TAttribute<FText>& Text);
	/** A label on the left, the control on the right. */
	ZETTA3DEDITOR_API TSharedRef<SWidget> Row(const FText& Label, const TSharedRef<SWidget>& Control, const FText& Tooltip = FText());
	ZETTA3DEDITOR_API TSharedRef<SWidget> Card(const TSharedRef<SWidget>& Content, float Padding = 12.f);
	ZETTA3DEDITOR_API TSharedRef<SWidget> PrimaryButton(const TAttribute<FText>& Text, FSimpleDelegate OnClicked, const TAttribute<bool>& Enabled = true);
	ZETTA3DEDITOR_API TSharedRef<SWidget> Button(const TAttribute<FText>& Text, FSimpleDelegate OnClicked, const TAttribute<bool>& Enabled = true, const FText& Tooltip = FText());
	ZETTA3DEDITOR_API TSharedRef<SWidget> Check(const FText& Label, TSharedRef<bool> Value, const FText& Tooltip = FText(), const TAttribute<bool>& Enabled = true);

	/**
	 * A drop-down over choices (key, label, hint). Value holds the chosen key; OnChanged fires after it changes.
	 * Choices are read each time the menu opens, so a page can change them.
	 */
	ZETTA3DEDITOR_API TSharedRef<SWidget> Choice(TSharedRef<FString> Value, TFunction<TArray<ZettaCatalog::FChoice>()> Choices,
		TFunction<void()> OnChanged = nullptr, const TAttribute<bool>& Enabled = true);
	/** Segmented buttons for two to four choices. */
	ZETTA3DEDITOR_API TSharedRef<SWidget> Segmented(TSharedRef<FString> Value, const TArray<ZettaCatalog::FChoice>& Choices, TFunction<void()> OnChanged = nullptr);

	/** Asks for a file from disk. Returns an empty string when cancelled. */
	ZETTA3DEDITOR_API FString PickFile(const FString& Title, const FString& Filter);
	ZETTA3DEDITOR_API TArray<FString> PickFiles(const FString& Title, const FString& Filter);
	ZETTA3DEDITOR_API FString ImageFilter();

	/** Opens a URL on zetta3d.ai (pricing, a generation page…). */
	ZETTA3DEDITOR_API void OpenSite(const FString& Path);

	/** Credit count as the header shows it: "1,240". */
	ZETTA3DEDITOR_API FText Credits(int64 Value);
}

/** A picture from a URL (thumbnail, preview) with a placeholder until it loads. */
class ZETTA3DEDITOR_API SZettaRemoteImage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaRemoteImage) : _Size(FVector2D(96, 96)) {}
		SLATE_ATTRIBUTE(FString, Url)
		SLATE_ARGUMENT(FVector2D, Size)
		SLATE_ARGUMENT(FText, Placeholder)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetUrl(const FString& Url);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	const FSlateBrush* GetBrush() const;
	TAttribute<FString> UrlAttribute;
	FString Current;
	TSharedPtr<FSlateBrush> Brush;
	FVector2D Size;
};

/** A picture the user gives: a file from disk (button or drag & drop), shown as a preview, with Clear. */
class ZETTA3DEDITOR_API SZettaImageInput : public SCompoundWidget
{
public:
	DECLARE_DELEGATE(FOnImageChanged);

	SLATE_BEGIN_ARGS(SZettaImageInput) : _Size(FVector2D(160, 160)) {}
		SLATE_ARGUMENT(FText, Label)
		SLATE_ARGUMENT(FVector2D, Size)
		SLATE_EVENT(FOnImageChanged, OnChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	bool HasImage() const { return Bytes.Num() > 0; }
	const TArray<uint8>& GetBytes() const { return Bytes; }
	FString GetContentType() const { return ContentType; }
	FString GetFileName() const { return FileName; }
	/** Sets the picture from bytes (e.g. one generated in the Image Studio). */
	void SetImage(const TArray<uint8>& InBytes, const FString& Name);
	void Clear();
	/** Loads a file; returns an error message, or empty on success. */
	FString LoadFile(const FString& Path);

	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

private:
	void Refresh();
	TArray<uint8> Bytes;
	FString ContentType;
	FString FileName;
	FString Error;
	TSharedPtr<FSlateBrush> Brush;
	TSharedPtr<SImage> Preview;
	FVector2D Size;
	FOnImageChanged OnChanged;
};

/** Reads an image file for upload: PNG, JPEG or WebP, ≤32 MB. Returns an error message, or empty on success. */
namespace ZettaUI
{
	ZETTA3DEDITOR_API FString ReadImageFile(const FString& Path, TArray<uint8>& OutBytes, FString& OutContentType);
}
