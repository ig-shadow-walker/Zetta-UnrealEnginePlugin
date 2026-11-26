// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "ZettaModels.h"

class UTexture;
struct FAssetData;

/**
 * The project side of the Studio: checking pictures before they go up, uploading pictures and models,
 * finding a generation's imported copy, turning assets into pictures, and saving pictures as textures.
 */
namespace ZettaStudioAssets
{
	inline constexpr int32 MinSide = 128;
	inline constexpr int32 MaxSide = 5000;
	inline constexpr int64 MaxImageBytes = 8ll * 1024 * 1024;
	inline constexpr int64 MaxModelBytes = 100ll * 1024 * 1024;

	using FDone = TFunction<void(const FString& /*Error*/)>;

	/** Empty when the picture is fine for 3D; otherwise a plain reason. */
	FString CheckImage(const TArray<uint8>& Bytes, const FString& ContentType);
	/** Width and height from a PNG, JPEG or WebP header, without decoding the picture. */
	bool ImageSize(const TArray<uint8>& Bytes, int32& OutWidth, int32& OutHeight);
	/** ".png", ".jpg" or ".webp". */
	FString DotExtension(const FString& ContentType);
	/** The file name kept, the extension following the new type. */
	FString Renamed(const FString& Original, const FString& ContentType);
	/** A title suggested from a file name: "old_helmet-2.png" → "old helmet-2". */
	FString TitleFromFile(const FString& FileName);
	/** "Uploading… 45%". */
	FString UploadingText(float Progress, const FString& What = TEXT("Uploading"));

	/**
	 * Uploads a picture for the 3D engines and hands back its URL — removing the background first when asked
	 * (the result is then a PNG), the same order the web Studio uses.
	 */
	void UploadImage(const TArray<uint8>& Bytes, const FString& ContentType, bool bRemoveBackground,
		TFunction<void(const FString& /*Error*/, const FString& /*Url*/)> Done, TFunction<void(float)> Progress = nullptr);

	/** A GLB or FBX on disk into the library (free); hands back the new row. */
	void UploadModelFile(const FString& Path, TFunction<void(const FString& /*Error*/, const FZettaGenerationPtr&)> Done,
		TFunction<void(const FString& /*Status*/)> Status = nullptr);

	/** The generation a project asset came from, when Zetta 3D imported it (zetta.json beside it). */
	TOptional<int64> GenerationIdOf(const FString& ObjectPath);
	/** The file on disk an asset was imported from, or empty. */
	FString SourceFileOf(const UObject* Asset);

	/**
	 * The library row for a project asset: its own generation when Zetta 3D imported it, otherwise an upload of
	 * the GLB / FBX it was imported from.
	 */
	void SourceFromAsset(const FAssetData& Asset, TFunction<void(const FString& /*Error*/, const FZettaGenerationPtr&)> Done,
		TFunction<void(const FString& /*Status*/)> Status = nullptr);
	/** The same, for the asset selected in the Content Browser. */
	void SourceFromSelectedAsset(TFunction<void(const FString& /*Error*/, const FZettaGenerationPtr&)> Done,
		TFunction<void(const FString& /*Status*/)> Status = nullptr);

	/** A JPEG of an asset as the editor draws its thumbnail (a model seen from its usual angle). */
	bool RenderAssetJpeg(const FString& ObjectPath, int32 Size, TArray<uint8>& OutJpeg);
	/** Re-encodes PNG / JPEG bytes as JPEG (smaller than Size on its long side). False for anything else. */
	bool ToJpeg(const TArray<uint8>& Bytes, int32 MaxSize, TArray<uint8>& OutJpeg);
	/** A reference larger than 1536 px goes up as a JPEG of that size, when that is smaller. */
	void Downscale(TArray<uint8>& Bytes, FString& ContentType);

	/** A texture asset as picture bytes: the PNG / JPEG it was imported from, or its pixels as a PNG. */
	bool TextureBytes(UTexture* Texture, TArray<uint8>& OutBytes, FString& OutContentType, FString& OutName);

	/** Saves a picture into the project's images folder under a free name. Returns the object path, or empty with an error. */
	FString SaveImage(const TArray<uint8>& Bytes, const FString& Name, FString& OutError);

	/** Selects assets in the Content Browser. */
	void ShowInContentBrowser(const TArray<FString>& ObjectPaths);

	/** Downloads a picture by URL (no session). */
	void DownloadPicture(const FString& Url, TFunction<void(const FString& /*Error*/, const TArray<uint8>&, const FString& /*ContentType*/)> Done);
}
