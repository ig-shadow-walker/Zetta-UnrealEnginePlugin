// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FZettaGeneration;

/**
 * The pure half of the agent's paid post-process tool (process_model): input validation and the exact job bodies,
 * with no network and no UI. The rules are the Studio panels' (Retopology, Texture, Auto Rig, Segment, Smart UV):
 * only the fields those send, and only in combinations the server accepts, because a refused combination is found
 * after the charge. Unknown options are refused rather than dropped, so the model learns before anything is spent.
 */
namespace ZettaRequestBuilders
{
	inline constexpr int32 RetopologyMin = 500;
	inline constexpr int32 RetopologyMaxTriangles = 20000;
	inline constexpr int32 RetopologyMaxQuads = 10000;
	/** The Studio's starting count: under both ceilings. */
	inline constexpr int32 RetopologyDefaultCount = 8000;

	/** A validated post-process: what to bill, what to send, and how to say it. */
	struct FProcessPlan
	{
		/** retopologize | smart_uv | texture | rig | segment */
		FString Operation;
		/** The server's operation code (smart_topology, smart_uv, retexture…, auto_rigging, ai_/smart_segmentation). */
		FString Code;
		/** The model the price row is looked up with (only smart_topology has model rows). */
		FString PriceModel;
		/** The job's fields besides spec / sourcePostId (a texture's imageUrl is added later). */
		TSharedRef<FJsonObject> Options = MakeShared<FJsonObject>();
		/** Plain-language lines for the approval card. */
		TArray<FString> Summary;
		/** texture: the surface description, drawn into a reference picture before the job. */
		FString Prompt;
		/** texture: the reference picture. */
		TOptional<int64> ImageId;

		bool NeedsPicture() const { return Operation == TEXT("texture"); }
	};

	bool NormalizeOperation(const FString& Operation, FString& Out, FString& OutError);
	bool Process(const FString& Operation, const TSharedPtr<FJsonObject>& Options, FProcessPlan& Out, FString& OutError);
	/** Why a row can't be post-processed by this operation; empty when it can. */
	FString RefuseSource(const FString& Operation, const FZettaGeneration& Source);
	/** Smart UV refuses sources over 80,000 triangles (a quad counts as two); empty when fine or unknown. */
	FString SmartUvProblem(const FZettaGeneration& Source);
	/** The job body: the plan's options (plus a texture's uploaded reference) under spec.postProcess. */
	TSharedRef<FJsonObject> ProcessBody(const FProcessPlan& Plan, int64 SourcePostId, const FString& ImageUrl);
	FString OperationLabel(const FString& Operation);
}
