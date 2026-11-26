// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"

/**
 * Pictures for the UI (thumbnails, previews, pictures in the chat) as Slate brushes: downloaded once,
 * decoded (PNG / JPEG / BMP), and kept for the session. WebP can't be decoded by the engine and shows
 * as nothing; callers draw their own placeholder.
 */
class ZETTA3DEDITOR_API FZettaImageCache
{
public:
	static FZettaImageCache& Get();

	/** The brush for a URL, or null while it loads (OnLoaded fires when it arrives). */
	TSharedPtr<FSlateBrush> Find(const FString& Url);
	void Request(const FString& Url, TFunction<void(TSharedPtr<FSlateBrush>)> OnLoaded = nullptr);

	/** A brush from bytes already in memory (an attached picture, a generated image). Keyed for reuse. */
	TSharedPtr<FSlateBrush> FromBytes(const FString& Key, const TArray<uint8>& Bytes);

	/** Decodes PNG / JPEG / BMP into BGRA8. */
	static bool Decode(const TArray<uint8>& Bytes, TArray<uint8>& OutBgra, int32& OutWidth, int32& OutHeight);

	void Clear();

private:
	TMap<FString, TSharedPtr<FSlateBrush>> Brushes;
	TMap<FString, TArray<TFunction<void(TSharedPtr<FSlateBrush>)>>> Waiting;
	TSet<FString> Failed;
};
