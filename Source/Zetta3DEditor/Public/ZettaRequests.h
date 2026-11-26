// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "ZettaModels.h"

/** The Generate / Smart 3D form, in the Studio's (and the agent's) vocabulary. */
struct ZETTA3DEDITOR_API FZettaGenerateOptions
{
	FString Prompt;
	/** An uploaded, engine-readable image URL (from FZettaApi::Upload). */
	FString ImageUrl;
	/** The image was drawn from Prompt (text-to-3D): both are sent. */
	bool bImageFromPrompt = false;
	/** Multi-view: (viewType, url) in submission order; front becomes the primary image server-side. */
	TArray<TPair<FString, FString>> Views;

	FString Model = TEXT("n25");
	/** low-poly | high-res | ultra */
	FString Quality = TEXT("high-res");
	/** textured | geometry */
	FString Output = TEXT("textured");
	bool bPbr = true;
	/** standard | detailed | extreme */
	FString TextureQuality = TEXT("standard");
	/** Unset = the engine's default for the rung (no custom-count charge). */
	TOptional<int32> FaceCount;
	/** triangle | quadrilateral */
	FString PolygonType = TEXT("triangle");
	FString Title;
	bool bSegmentParts = false;
	bool bRealWorldScale = false;
	bool bDelight = false;
	bool bAutofixImage = false;
	bool bCompressGeometry = false;
	/** geometry | original_image */
	FString TextureAlignment = TEXT("geometry");
	bool bExportUv = true;
	bool bControlMode = false;
	/** Empty = the server's default (plan-aware), or public | private. */
	FString Availability;

	bool IsMultiView() const { return Views.Num() > 0; }
};

struct ZETTA3DEDITOR_API FZettaBuiltRequest
{
	TSharedPtr<FJsonObject> Body;
	FString Base;
	TArray<FString> Modifiers;
	TArray<FString> Problems;
	bool IsOk() const { return Problems.Num() == 0; }
};

/**
 * A port of the web Studio's request builder. Every cross-field rule here exists because the server refuses
 * the combination after credits have moved — checking it on this side costs nothing.
 */
namespace ZettaRequests
{
	ZETTA3DEDITOR_API void Spec(const FZettaGenerateOptions& O, const FZettaEntitlements3D* Gates, FString& OutBase, TArray<FString>& OutModifiers);
	ZETTA3DEDITOR_API FZettaBuiltRequest Build(const FZettaGenerateOptions& O, const FZettaEntitlements3D* Gates = nullptr);
	/** The credits the job will cost, from the price list (the server's quote is authoritative). */
	ZETTA3DEDITOR_API TOptional<int32> Price(const FZettaPriceList& Prices, const FZettaBuiltRequest& Request, const FString& Model);
	/** A post-process job on an existing generation: exactly one operation code. */
	ZETTA3DEDITOR_API TSharedRef<FJsonObject> PostProcess(const FString& Code, int64 SourcePostId, const TSharedPtr<FJsonObject>& Options = nullptr);
}
