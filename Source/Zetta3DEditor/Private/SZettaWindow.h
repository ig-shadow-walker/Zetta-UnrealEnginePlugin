// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SWidgetSwitcher;
class SHorizontalBox;

/**
 * The Zetta 3D window: a header with a tab per page and the account, and the pages below. Pages are built
 * once and kept, so a half-written prompt or an open pipeline is still there when you come back. Signed out,
 * the whole window is the sign-in screen.
 */
class SZettaWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaWindow) {}
		SLATE_ARGUMENT(FName, InitialPage)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SZettaWindow() override;

	void ShowPage(FName Page);

private:
	void Rebuild();
	void RebuildTabs();
	TSharedRef<SWidget> MakeHeader();
	TSharedRef<SWidget> MakeAccountMenu();
	TSharedRef<SWidget> MakeSignIn();

	TSharedPtr<SWidgetSwitcher> Root;
	TSharedPtr<SWidgetSwitcher> Pages;
	TSharedPtr<SHorizontalBox> Tabs;
	TMap<FName, int32> PageIndex;
	TMap<FName, TSharedPtr<SWidget>> Built;
	FName Current;
	FDelegateHandle AuthHandle, PagesHandle, ShowHandle;
};
