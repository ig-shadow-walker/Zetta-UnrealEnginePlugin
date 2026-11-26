// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "ZettaJobs.h"

class SVerticalBox;

/**
 * The Studio's footer: every 3D job the plugin is following (from the Studio, the library or the agent), with
 * its state, queue position and import progress, and what can be done with it next.
 */
class SZettaJobsStrip : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaJobsStrip) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SZettaJobsStrip() override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** The line under a job's name: "Queued · #3 in line", "Ready · imported to /Game/…". */
	static FString Describe(const FZettaJob& Job);

private:
	void Render();
	TSharedRef<SWidget> Row(const FZettaJobPtr& Job);
	void Import(const FZettaJobPtr& Job);
	void Place(const FZettaJobPtr& Job);
	TArray<FString> ImportedAssets(const FZettaJobPtr& Job) const;

	TSharedPtr<SVerticalBox> List;
	FDelegateHandle ChangedHandle;
	/** The list's shape (ids and states) when it was last drawn; a change redraws it on the next tick. */
	FString Drawn;
	bool bCollapsed = false;
	bool bDirty = true;
};
