// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "ZettaPipelineRecipe.h"
#include "ZettaPipelineUI.h"

class SVerticalBox;

/**
 * What a node's form edits and how it reports a change. A form is built once for a node, then refreshed in place on
 * every change of the graph (so a field being typed in keeps its caret). Every control writes a PATCH; the owner merges
 * it (the recipe through ZettaGraphEditing, a run through ZettaRequestBuilder::RunNodeChange) and calls Refresh.
 */
struct FZettaFormContext : public TSharedFromThis<FZettaFormContext>
{
	FZettaPipelineNode Node;
	FZettaPipelineGraph Graph;
	/** The settings shown — the node's own over the Studio's defaults. */
	TSharedRef<FJsonObject> Settings = MakeShared<FJsonObject>();
	FString Incoming;
	/** In a run: a step set on each run asks for this run's own input here. */
	bool bInRun = false;
	TFunction<void(const TSharedRef<FJsonObject>&)> Patch;
	FZettaPlOverlayRef Overlay = MakeShared<FZettaPlOverlay>();

	void Refresh(const FZettaPipelineNode& InNode, const FZettaPipelineGraph& InGraph);
	FString Str(const FString& Key, const FString& Fallback = FString()) const;
	bool Bool(const FString& Key, bool bFallback = false) const;
	void Set(const FString& Key, const TSharedPtr<FJsonValue>& Value) const;
};

/** THE NODES' SETTINGS FORMS — each Studio tool's own options, in the Studio's words. */
namespace ZettaNodeForms
{
	TSharedRef<SWidget> Build(const TSharedRef<FZettaFormContext>& Ctx);
}

/**
 * THE SELECTED STEP'S SETTINGS: its mark, its name, the blurb, Remove, what stops it running and what may go wrong,
 * then the Studio form for its tool. With no step selected, the recipe overview.
 */
class SZettaNodeInspector : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaNodeInspector) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FZettaPlOverlayRef& InOverlay);

	TFunction<void(const FString& /*NodeId*/, const TSharedRef<FJsonObject>& /*Patch*/)> OnPatch;
	TFunction<void(const FString& /*NodeId*/, const FString& /*Label*/)> OnLabel;
	TFunction<void(const FString& /*NodeId*/)> OnRemove;
	TFunction<void()> OnShowRuns;

	/** Shows the step (or the overview when none) as the graph now has it. */
	void Show(const FZettaPipelineGraph& Graph, const FString& NodeId, const TArray<FZettaPipelineProblem>& Problems, int32 RunCount);

private:
	void BuildOverview();
	void BuildStep(const FZettaPipelineNode& Node, const FZettaPipelineGraph& Graph);

	FZettaPlOverlayRef Overlay = MakeShared<FZettaPlOverlay>();
	TSharedPtr<SVerticalBox> Body;
	TSharedPtr<FZettaFormContext> Form;
	FString ShownId;
	FString ShownKind;
	bool bOverview = false;
	FText Heading;
	FString Lead;
	int32 Runs = 0;
	FString Notes;
	TArray<FString> NoteWarnings;
};
