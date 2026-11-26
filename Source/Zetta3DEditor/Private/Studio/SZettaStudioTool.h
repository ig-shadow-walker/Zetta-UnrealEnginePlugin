// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 * One tool of the Studio. Each is built once and kept while the page lives, so switching tools never
 * loses what was typed.
 */
class SZettaStudioTool : public SCompoundWidget
{
public:
	/** Plan gates, prices or image models changed (or finished loading). */
	virtual void OnContextChanged() {}
	/** The tool became the one on show. */
	virtual void OnShown() {}

protected:
	/** The tools' common frame: an intro line and the form, scrolling, then a note and the footer pinned below. */
	static TSharedRef<SWidget> Frame(const FText& Intro, const TSharedRef<SWidget>& Form, const TSharedRef<SWidget>& Footer,
		const TSharedRef<SWidget>& AboveFooter = SNullWidget::NullWidget);
};
