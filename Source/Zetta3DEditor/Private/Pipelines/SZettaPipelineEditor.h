// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "ZettaPipelineRecipe.h"
#include "ZettaPipelineRuns.h"
#include "ZettaPipelineUI.h"

class SZettaPipelineCanvas;
class SZettaNodeInspector;
class SZettaRunsView;
class SVerticalBox;
class SWidgetSwitcher;

/**
 * ONE PIPELINE, OPEN — its RECIPE (palette | node canvas | step settings) and its RUNS.
 *
 * THE GRAPH MODEL IS THE SOURCE OF TRUTH: every edit is a ZettaGraphEditing call committed to the undo stack; the
 * canvas, palette and inspector re-sync from it. SAVED AS YOU GO: 800 ms after the last change the whole graph is
 * PATCHed with every request rebuilt.
 */
class SZettaPipelineEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaPipelineEditor) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, int64 InPipelineId, const FZettaPlOverlayRef& InOverlay, TFunction<void()> InBack);
	virtual ~SZettaPipelineEditor() override;

	int64 GetPipelineId() const { return PipelineId; }
	const FZettaPipelineGraph& GetGraph() const { return Stack.Present(); }
	FString GetTitle() const { return Title; }
	const FZettaEntitlements3D* Gates() const;
	const FZettaPriceList* Prices() const;
	FZettaPlOverlayRef GetOverlay() const { return Overlay; }
	bool HasUnsavedChanges() const { return bDirty || bSaving; }
	TOptional<int32> CreditsPerRun() const { return Estimate.Credits; }

	/** Reads the pipeline and its runs, then opens on the Recipe or Runs (with a run open). */
	void Load(bool bRuns, TOptional<int64> RunId);
	/** Recipe, or Runs with a run open. */
	void Go(bool bRuns, TOptional<int64> RunId = {});
	/** Someone else (the agent) saved this pipeline: their version replaces the editor's, never the other way round. */
	void ReloadFromServer();
	void RefreshRuns();
	/** Isolate Parts learns its parts from a run — the editor's own doing: kept and saved, never undone. */
	void LearnParts(const FZettaPipelineRun& Run);
	void OpenRunDialog();
	/** Saves now what is waiting to be saved. */
	void Flush();
	/** The runs were just made with the graph (saved onto the pipeline with them): nothing is waiting any more. */
	void MarkSavedByBatch();

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	TSharedRef<SWidget> MakeBar();
	TSharedRef<SWidget> MakeRecipe();
	void Apply(const FZettaPipeline& Pipeline);
	void MarkDirty();
	void Save();
	void Commit(const FZettaPipelineGraph& Next, const FString& Coalesce = FString());
	void Undo();
	void Redo();
	void AddStep(const FString& Kind, const TOptional<FVector2D>& At);
	void Patch(const FString& NodeId, const TSharedRef<FJsonObject>& Patch);
	void RemoveItems(const TArray<FString>& Nodes, const TArray<FString>& Edges);
	void ApplyDraft(const FZettaPipelineDraft& Draft);
	void Render();
	void ShowNote(const FString& Text, const FString& Tone = FString());
	void SetRuns(const TArray<FZettaRunSummary>& List);
	void OnAppActivated(bool bActive);

	int64 PipelineId = 0;
	FZettaPlOverlayRef Overlay = MakeShared<FZettaPlOverlay>();
	TFunction<void()> Back;
	FZettaUndoStack Stack;
	FString Title;
	bool bLoaded = false;
	bool bDirty = false;
	bool bSaving = false;
	bool bSaveFailed = false;
	double SaveAt = 0;
	bool bRunsView = false;
	FString SelectedNode;
	TArray<FZettaPipelineProblem> Problems;
	FZettaGraphEstimate Estimate;
	TSet<FString> FanOut;
	TOptional<TArray<FZettaRunSummary>> Runs;
	TOptional<TPair<bool, TOptional<int64>>> PendingView;
	FString LoadingText;

	TSharedPtr<SWidgetSwitcher> Switcher;
	TSharedPtr<SVerticalBox> Notes;
	TSharedPtr<SZettaPipelineCanvas> Canvas;
	TSharedPtr<SZettaNodeInspector> Inspector;
	TSharedPtr<SZettaRunsView> RunsView;
	TSharedPtr<class SZettaPlText> TitleField;
	FDelegateHandle PlanHandle;
	FDelegateHandle RunsHandle;
	FDelegateHandle ActivationHandle;
};

/**
 * RUNS: every run of the pipeline, newest first, filtered by what it is doing; one open shows its steps exactly as they
 * ran on its own copy of the graph, what each made, and what can still be done.
 */
class SZettaRunsView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaRunsView) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<SZettaPipelineEditor>& InEditor);
	void SetRuns(const TArray<FZettaRunSummary>& InRuns);
	void OpenRun(TOptional<int64> RunId);
	TOptional<int64> OpenRunId() const;

private:
	void RenderList();
	TSharedRef<SWidget> Row(const FZettaRunSummary& Run);
	void DeleteFromList(const FZettaRunSummary& Run);

	TWeakPtr<SZettaPipelineEditor> Editor;
	TOptional<TArray<FZettaRunSummary>> Runs;
	FString Filter = TEXT("all");
	TSharedPtr<SVerticalBox> List;
	TSharedPtr<class SHorizontalBox> Filters;
	TSharedPtr<class SBox> Right;
	TSharedPtr<class SZettaRunPanel> Panel;
};

/** RUN ON…: the seeds to run on (each its own run, up to 20), Auto or Manual. It makes the runs; it does not start them. */
namespace ZettaRunDialog
{
	void Show(const TSharedRef<SZettaPipelineEditor>& Editor, TFunction<void(const TArray<FZettaRunSummary>& /*Created*/, bool /*bImport*/)> Created);
}
