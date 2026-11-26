// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "ZettaModels.h"

class AActor;

struct ZETTA3DEDITOR_API FZettaImportOptions
{
	/** Content folder to import under (defaults to the project's Models folder). */
	FString Folder;
	/** Place the model in the open level. */
	bool bPlace = false;
	/** World location to place at; unset = on the ground in front of the level viewport. */
	TOptional<FVector> Location;
	/** An actor to attach the placed model to. */
	TWeakObjectPtr<AActor> Parent;
	/** Select what was placed. */
	bool bSelect = true;
};

struct ZETTA3DEDITOR_API FZettaImportResult
{
	bool bOk = false;
	FString Error;
	/** The content folder the model went into ("/Game/Zetta3D/Models/501-helmet"). */
	FString Folder;
	/** Object paths of every asset the import made, main mesh first. */
	TArray<FString> AssetPaths;
	/** Labels of the actors placed in the level. */
	TArray<FString> PlacedActors;
	TArray<TWeakObjectPtr<AActor>> Placed;
	/** A note for the user or the agent ("this is a collider", …). */
	FString Note;
	FString UvLayoutPath;

	FString MainAssetPath() const { return AssetPaths.Num() > 0 ? AssetPaths[0] : FString(); }
};

using FZettaImportStatus = TFunction<void(const FString& /*Status*/, float /*Progress 0-1*/)>;

/**
 * Brings a finished generation into the project: downloads its mesh (GLB or FBX), imports it with the
 * engine's importers into <Models folder>/<id>-<name>/, writes a small zetta.json beside it, and
 * optionally places it in the open level resting on the ground. Segmentation results arrive as several
 * part GLBs, placed together. Gaussian splats (made on the web) are refused.
 */
namespace ZettaImporter
{
	ZETTA3DEDITOR_API extern const TCHAR* SplatUnsupported;

	ZETTA3DEDITOR_API FString Slug(const FString& Text);
	ZETTA3DEDITOR_API TArray<FZettaResultFile> PickFiles(const FZettaGeneration& Generation);
	ZETTA3DEDITOR_API void Import(const FZettaGenerationPtr& Generation, const FZettaImportOptions& Options,
		TFunction<void(const FZettaImportResult&)> Done, FZettaImportStatus Status = nullptr);

	/** Places already-imported meshes (object paths) in the open level. Returns the actors made (one undo step). */
	ZETTA3DEDITOR_API TArray<AActor*> Place(const TArray<FString>& AssetPaths, const FString& Label, const FZettaImportOptions& Options);

	/** Saves picture bytes as a texture asset ("/Game/Zetta3D/Images/name"); returns the object path or an error. */
	ZETTA3DEDITOR_API bool SaveTexture(const TArray<uint8>& Bytes, const FString& PackagePath, FString& OutObjectPath, FString& OutError, bool bReplace = false);

	/** Where on the ground in front of the level viewport a new thing should go. */
	ZETTA3DEDITOR_API FVector DropPoint();
}

/** Which library models are already in this project, by the zetta.json files the importer writes. */
namespace ZettaProjectIndex
{
	/** Generation id → content folder. Re-scanned when stale. */
	ZETTA3DEDITOR_API const TMap<int64, FString>& Models();
	ZETTA3DEDITOR_API bool Contains(int64 GenerationId);
	ZETTA3DEDITOR_API FString FolderOf(int64 GenerationId);
	/** The main mesh asset in a model's folder, or empty. */
	ZETTA3DEDITOR_API FString MainAssetOf(int64 GenerationId);
	ZETTA3DEDITOR_API void Invalidate();
}
