// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "ZettaModels.h"

struct FZettaResponse;

/** The network steps the Zetta tools share: sign-in, the plan's gates and prices, picture links, downloads, uploads. */
namespace ZettaCloud
{
	/** False (with the message for the model) when the user is not signed in. */
	bool RequireSignIn(FString& OutError);
	/** An API error in plain words, with the next step. */
	FString Explain(const FZettaResponse& Response);

	/** Loads (once a session) what the plan allows and the price list. Error is empty on success. */
	void LoadPlan(TFunction<void(const FString& Error)> Done);
	const FZettaEntitlements3D& Gates();
	const FZettaPriceList& Prices();

	/** A library picture's download link. */
	void ImageUrl(int64 ImageId, TFunction<void(const FString& Url, const FString& Error)> Done);
	/** A library picture's bytes. */
	void DownloadImage(int64 ImageId, TFunction<void(const TArray<uint8>& Bytes, const FString& Error)> Done);
	/** Uploads bytes; the URL the engines read. */
	void Upload(const TArray<uint8>& Bytes, const FString& ContentType, TFunction<void(const FString& Url, const FString& Error)> Done);
	/** "/Game/Zetta3D/Models/501-helmet/" for a job. */
	FString FolderFor(const FZettaGeneration& Generation);
}
