// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "ZettaHttp.h"
#include "ZettaModels.h"

/** A typed call's answer: the response (check bOk / Message) and, on success, the parsed value. */
template <typename T>
using TZettaDone = TFunction<void(const FZettaResponse&, const T&)>;

/** Typed calls for every Zetta 3D route the plugin uses. Callbacks run on the game thread. */
namespace ZettaApi
{
	// Account
	ZETTA3DEDITOR_API void Me(TZettaDone<FZettaUser> Done);

	// 3D: gates and prices
	ZETTA3DEDITOR_API void Entitlements3D(TZettaDone<FZettaEntitlements3D> Done);
	ZETTA3DEDITOR_API void Pricing(TZettaDone<FZettaPriceList> Done);
	ZETTA3DEDITOR_API void Quote(const FString& Base, const TArray<FString>& Modifiers, const FString& Model, TZettaDone<FZettaQuote> Done);

	/** Uploads a file through the API's relay; the ticket's FileUrl is what the engines read (24h). An expired ticket is renewed once. */
	ZETTA3DEDITOR_API void Upload(const TArray<uint8>& Bytes, const FString& ContentType, TZettaDone<FZettaUploadTicket> Done, FZettaProgress Progress = nullptr);
	/** Turns an uploaded GLB into a finished library row (free). */
	ZETTA3DEDITOR_API void CreateUploadPost(const FString& Key, int64 Bytes, const FString& Title, TZettaDone<FZettaGenerationPtr> Done);

	// 3D: jobs
	/** Creates a generation or post-process job; the origin is stamped "unreal". */
	ZETTA3DEDITOR_API void CreateGeneration(const TSharedRef<FJsonObject>& Body, TZettaDone<FZettaGenerationPtr> Done, const FString& IdempotencyKey = FString());
	ZETTA3DEDITOR_API void GetGeneration(int64 Id, TFunction<void(const FZettaResponse&, const FZettaGenerationPtr&, TOptional<int32> /*QueuePosition*/)> Done);

	struct FListQuery
	{
		int32 Page = 1;
		int32 PageSize = 24;
		FString Search;
		FString Feature;
		TArray<FString> Statuses;
		bool bGroupRuns = false;
	};
	ZETTA3DEDITOR_API void ListGenerations(const FListQuery& Query, TZettaDone<FZettaGenerationPage> Done);
	/** A fresh signed URL (15 min) for one of a job's files; an empty key = the main mesh. */
	ZETTA3DEDITOR_API void DownloadUrl(int64 Id, const FString& Key, TZettaDone<FString> Done);
	ZETTA3DEDITOR_API void UpdateGeneration(int64 Id, const TOptional<FString>& Title, const TOptional<TArray<FString>>& Tags, const TOptional<FString>& Availability, TZettaDone<FZettaGenerationPtr> Done);
	ZETTA3DEDITOR_API void CancelGeneration(int64 Id, TZettaDone<FZettaGenerationPtr> Done);
	ZETTA3DEDITOR_API void DeleteGeneration(int64 Id, FZettaCallback Done);

	// Tools and AI helpers
	/** Background removal; returns PNG bytes. */
	ZETTA3DEDITOR_API void RemoveBackground(const TArray<uint8>& Image, TZettaDone<TArray<uint8>> Done);
	ZETTA3DEDITOR_API void SuggestTags(const TArray<TArray<uint8>>& JpegFrames, TFunction<void(const FZettaResponse&, const FString& /*Category*/, const TArray<FString>& /*Tags*/)> Done);
	ZETTA3DEDITOR_API void RiggingPrecheck(const TArray<TArray<uint8>>& JpegFrames, TFunction<void(const FZettaResponse&, const FString& /*BodyType*/, const FString& /*Confidence*/, const FString& /*Reason*/)> Done);

	// Image Studio (2D)
	ZETTA3DEDITOR_API void Entitlements2D(TZettaDone<FZettaEntitlements2D> Done);
	ZETTA3DEDITOR_API void ImageModels(TZettaDone<FZettaImageModels> Done);

	struct FImageOptions
	{
		FString AspectRatio = TEXT("1:1");
		FString Resolution = TEXT("1K");
		FString Model;
		/** A picture for 3D: forced square 1K server-side, so no frame is sent. */
		bool bFor3D = false;
	};
	ZETTA3DEDITOR_API void GenerateImage(const FString& Prompt, const FImageOptions& Options, const TArray<FZettaInlineImage>& StyleReferences, TZettaDone<FZettaImageResult> Done);
	/** Edits one picture: either inline bytes or a library post id. */
	ZETTA3DEDITOR_API void EditImage(const FString& Prompt, const FImageOptions& Options, const TOptional<FZettaInlineImage>& Image, TOptional<int64> SourcePostId, TZettaDone<FZettaImageResult> Done);
	ZETTA3DEDITOR_API void ComposeImages(const FString& Prompt, const FImageOptions& Options, const TArray<FZettaInlineImage>& Images, const TArray<int64>& SourcePostIds, TZettaDone<FZettaImageResult> Done);
	ZETTA3DEDITOR_API void ListImages(int32 Page, int32 PageSize, const FString& Search, TFunction<void(const FZettaResponse&, const TArray<FZettaImagePost>&, int32 /*Total*/)> Done);
	ZETTA3DEDITOR_API void DeleteImage(int64 Id, FZettaCallback Done);
}
