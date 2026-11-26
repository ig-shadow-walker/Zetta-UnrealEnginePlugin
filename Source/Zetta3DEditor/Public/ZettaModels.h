// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** The account's credits, by pool. */
struct ZETTA3DEDITOR_API FZettaCreditPool
{
	int64 Monthly = 0, Purchased = 0, BonusMonthly = 0, Bonus = 0;
	int64 Total() const { return Monthly + Purchased + BonusMonthly + Bonus; }
	static FZettaCreditPool From(const TSharedPtr<FJsonObject>& O);
};

struct ZETTA3DEDITOR_API FZettaUser
{
	int64 Id = 0;
	FString Email;
	FString DisplayName;
	FString ProfilePicture;
	FString PaymentPlan;
	FString SubscriptionStatus;
	FZettaCreditPool Credits3D;
	FZettaCreditPool CreditsAi;

	FString Name() const { return DisplayName.IsEmpty() ? Email : DisplayName; }
	FString PlanLabel() const;
	bool IsFree() const;
	static FZettaUser From(const TSharedPtr<FJsonObject>& O);
};

/** What the plan allows in 3D. Unset fields are "not limited". */
struct ZETTA3DEDITOR_API FZettaEntitlements3D
{
	TOptional<int64> MaxMultiViewImages, MaxFaceCount, MaxConcurrentJobs;
	TOptional<bool> AllowsLowPoly, AllowsQuadMesh, AllowsCustomFaceCount, AllowsZettaN25, AllowsZettaNp1, AllowsPostProcess, AllowsPrivateGenerations;
	bool Allows(const TOptional<bool>& Gate) const { return !Gate.IsSet() || Gate.GetValue(); }
	static FZettaEntitlements3D From(const TSharedPtr<FJsonObject>& O);
};

struct ZETTA3DEDITOR_API FZettaEntitlements2D
{
	TOptional<bool> AllowsImageEditing, Allows4kImages, AllowsProImageModel;
	static FZettaEntitlements2D From(const TSharedPtr<FJsonObject>& O);
};

struct ZETTA3DEDITOR_API FZettaPriceItem
{
	FString Code, Kind, Label, Model;
	int32 Credits = 0;
};

/** The price list, with the Studio's lookup rule: an exact model row beats the null-model default. */
struct ZETTA3DEDITOR_API FZettaPriceList
{
	TArray<FZettaPriceItem> Items;
	TOptional<int32> Price(const FString& Code, const FString& Model) const;
	TOptional<int32> Total(const FString& Model, const TArray<FString>& Codes) const;
	static FZettaPriceList From(const TSharedPtr<FJsonObject>& O);
};

struct ZETTA3DEDITOR_API FZettaResultFile
{
	FString Type, Folder, Key, Url, Label;
	int64 Bytes = 0;

	bool IsPicture() const;
	bool IsSplat() const;
	FString Extension() const;
	FString FileName() const;
	static FZettaResultFile From(const TSharedPtr<FJsonObject>& O);
};

struct ZETTA3DEDITOR_API FZettaRunStep
{
	int32 Position = 0;
	FString Label, Part;
};

struct FZettaGeneration;

/** A pipeline run's models, listed as one library entry when the list is asked with groupRuns. */
struct ZETTA3DEDITOR_API FZettaRunGroup
{
	int64 RunId = 0;
	TOptional<int64> PipelineId;
	FString PipelineTitle;
	int32 Count = 0;
	TArray<TSharedPtr<FZettaGeneration>> Members;
};

/** One row of the 3D library: a generation or a post-process job. */
struct ZETTA3DEDITOR_API FZettaGeneration
{
	int64 Id = 0;
	FString Status, Feature, Model, Title, Availability, InputPrompt, ThumbnailUrl, PolygonType, ErrorMessage;
	TArray<FString> Tags;
	TArray<FString> InputImageUrls;
	TOptional<int64> FaceCount, PipelineRunId;
	TArray<FZettaResultFile> Files;
	FDateTime CreatedAt, UpdatedAt;
	TSharedPtr<FJsonObject> JobSpec;
	TSharedPtr<FZettaRunGroup> RunGroup;
	TOptional<FZettaRunStep> RunStep;
	/** The raw row, for anything the plugin does not model. */
	TSharedPtr<FJsonObject> Raw;

	bool IsTerminal() const { return Status == TEXT("success") || Status == TEXT("failed") || Status == TEXT("cancelled"); }
	bool IsSuccess() const { return Status == TEXT("success"); }
	bool IsRunning() const { return !IsTerminal(); }
	bool IsSplat() const;
	FString DisplayTitle() const;
	FString StatusLabel() const;
	FString SpecBase() const;
	TSet<FString> SpecModifiers() const;
	TArray<FZettaResultFile> MeshFiles() const;
	const FZettaResultFile* UvLayout() const;

	static TSharedRef<FZettaGeneration> From(const TSharedPtr<FJsonObject>& O);
};

using FZettaGenerationPtr = TSharedPtr<FZettaGeneration>;

struct ZETTA3DEDITOR_API FZettaGenerationPage
{
	TArray<FZettaGenerationPtr> Items;
	int32 Total = 0, Page = 1, PageSize = 24;
};

struct ZETTA3DEDITOR_API FZettaQuote
{
	int32 Credits = 0;
	TArray<TPair<FString, int32>> Lines;
	static FZettaQuote From(const TSharedPtr<FJsonObject>& O);
};

struct ZETTA3DEDITOR_API FZettaUploadTicket
{
	FString UploadUrl, Method, Key, FileUrl;
	TMap<FString, FString> Headers;
	static FZettaUploadTicket From(const TSharedPtr<FJsonObject>& O);
};

struct ZETTA3DEDITOR_API FZettaImageModel
{
	FString Id, Label, LockedReason;
	TArray<FString> Resolutions;
	bool bLocked = false;
};

struct ZETTA3DEDITOR_API FZettaImageModels
{
	TArray<FZettaImageModel> Models;
	FString Default;
};

/** A picture made by the 2D tools: inline bytes plus the library post it was saved as. */
struct ZETTA3DEDITOR_API FZettaImageResult
{
	TOptional<int64> PostId;
	TArray<TArray<uint8>> Images;
	TArray<FString> ContentTypes;
	FString Text, Model;
	static FZettaImageResult From(const TSharedPtr<FJsonObject>& O);
};

/** A row of the 2D library. */
struct ZETTA3DEDITOR_API FZettaImagePost
{
	int64 Id = 0;
	FString Feature, Prompt, AspectRatio, Resolution, ErrorMessage;
	TArray<FString> ImageUrls;
	FDateTime CreatedAt;
	static FZettaImagePost From(const TSharedPtr<FJsonObject>& O);
};

/** An image going up in a request body: raw base64, no data: prefix. */
struct ZETTA3DEDITOR_API FZettaInlineImage
{
	FString Data;
	FString ContentType;
	static FZettaInlineImage From(const TArray<uint8>& Bytes, const FString& ContentType);
	TSharedRef<FJsonObject> ToJson() const;
};

namespace ZettaImages
{
	/** "image/png", "image/jpeg", "image/webp" by magic bytes, or empty. */
	ZETTA3DEDITOR_API FString SniffContentType(const TArray<uint8>& Bytes);
	ZETTA3DEDITOR_API FString ExtensionFor(const FString& ContentType);
}
