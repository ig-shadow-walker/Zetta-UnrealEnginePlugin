// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "ZettaPipelineRuns.h"

/**
 * The text the agent's model reads about pipelines and runs (the same words every Zetta plugin's agent reads), kept apart from
 * the network so it stays easy to check: step trees, estimates, problems, run status, list lines.
 */
namespace ZettaPipelineText
{
	FString Plural(int32 N, const FString& One, const FString& Many = FString());
	FString Short(const FString& Text, int32 Max);
	/** "5 min ago", "3 h ago", "2 d ago" — or the date past a week. */
	FString Ago(const FDateTime& When, const FDateTime& NowUtc);
	/** "5 min ago", "2 days ago", then "12 Mar" — the Runs view's words. */
	FString WhenRun(const FDateTime& When);

	/** A step's settings in a few words, agent-shaped (pbr / faceCount for a generation). */
	FString SettingsBrief(const FZettaPipelineNode& Node, int32 Max = 220);
	/** The recipe as an indented tree from the seed, one line per step. */
	FString StepTree(const FZettaPipelineGraph& Graph, const FZettaPriceList* Prices, const FZettaEntitlements3D* Gates);
	/** "Up to 132 credits a run (steps after Isolate parts › each run once per part — counted for up to 6 parts)." */
	FString EstimateText(const FZettaGraphEstimate& Estimate);
	FString ProblemsText(const TArray<FZettaPipelineProblem>& Problems, const FZettaPipelineGraph& Graph);
	FString WarningsText(const FZettaPipelineGraph& Graph);
	/** Problems, warnings, estimate and the step tree — the tail of build / edit / get results. */
	FString GraphReport(const FZettaPipelineGraph& Graph, const FZettaPriceList* Prices, const FZettaEntitlements3D* Gates);

	FString SeedText(const TSharedPtr<FJsonObject>& Seed);
	TOptional<int32> StepPrice(const FZettaPipelineRun& Run, const FZettaPipelineRunStep& Step, const FZettaPriceList* Prices, const FZettaEntitlements3D* Gates);
	TOptional<int32> StepsPrice(const FZettaPipelineRun& Run, const TArray<const FZettaPipelineRunStep*>& Steps, const FZettaPriceList* Prices, const FZettaEntitlements3D* Gates);
	/** "step 302 · s1 Generate 3D [part sword]". */
	FString StepName(const FZettaPipelineRun& Run, const FZettaPipelineRunStep& Step);
	/** pipeline_run_status. */
	FString RunStatusText(const FZettaPipelineRun& Run, const FZettaPriceList* Prices, const FZettaEntitlements3D* Gates);
	FString PipelineLine(const FZettaPipelineSummary& Pipeline);
	FString RunLine(const FZettaRunSummary& Run);
}
