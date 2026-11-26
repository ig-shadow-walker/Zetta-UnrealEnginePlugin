// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"

struct FZettaResponse;

/**
 * What the Studio offers, in Zetta's own vocabulary. Mirrors the web Studio and the other plugins:
 * models are Zetta N1, N2.5, Np1, Np1.5 and R1 — engine vendors are never named to users.
 */
namespace ZettaCatalog
{
	struct FModelInfo
	{
		FString Id;
		FString Label;
		FString Blurb;
		bool bPaid = false;
		bool bLowPoly = false;
		bool bQuadAnyBase = false;
		bool bDetailedGeometry = false;
		bool bDetailedTexture = false;
		bool bExtremeTexture = false;
		bool bImageTuning = false;
		bool bParts = false;
		/** Can generate from a prompt alone (others need a picture first). */
		bool bBarePrompt = false;
	};

	ZETTA3DEDITOR_API const FModelInfo& Model(const FString& Id);
	ZETTA3DEDITOR_API const TArray<FString>& GenerateModels(); // n1, n25
	ZETTA3DEDITOR_API const TArray<FString>& SmartModels();    // np1, np15

	inline constexpr int32 SmartUvMaxTriangles = 80000;

	ZETTA3DEDITOR_API bool IsSplatFeature(const FString& Feature);
	ZETTA3DEDITOR_API FString ModelLabel(const FString& Model);
	ZETTA3DEDITOR_API FString FeatureLabel(const FString& Feature);

	/** Face-count bounds per model, quality rung and polygon type. */
	ZETTA3DEDITOR_API FInt32Range FaceRange(const FString& Model, const FString& Quality, bool bQuads);
	/** The count the web Studio pre-fills — the baseline the paid custom count is measured against. */
	ZETTA3DEDITOR_API int32 FacePreset(const FString& Model, const FString& Quality, bool bQuads);

	struct FChoice
	{
		FString Key;
		FString Label;
		FString Hint;
	};

	ZETTA3DEDITOR_API const TArray<FChoice>& Qualities();
	ZETTA3DEDITOR_API const TArray<FChoice>& RigTypes();
	ZETTA3DEDITOR_API const TArray<FChoice>& RigSpecs();
	ZETTA3DEDITOR_API const TArray<FString>& Orientations();
	ZETTA3DEDITOR_API const TArray<FString>& AspectRatios();
	ZETTA3DEDITOR_API const TArray<FString>& Resolutions();
	inline const TCHAR* HumanoidRigType = TEXT("biped");

	/** Features whose rows can be post-processed. */
	ZETTA3DEDITOR_API bool IsProcessable(const FString& Feature);

	/** A plain-language message for an API error, with the next step. */
	ZETTA3DEDITOR_API FString Explain(const FZettaResponse& Error);
}
