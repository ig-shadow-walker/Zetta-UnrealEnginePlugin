// Copyright Zetta 3D. See LICENSE.md.

#include "SZettaStudioTool.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "ZettaWidgets.h"

TSharedRef<SWidget> SZettaStudioTool::Frame(const FText& Intro, const TSharedRef<SWidget>& Form, const TSharedRef<SWidget>& Footer, const TSharedRef<SWidget>& AboveFooter)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot().Padding(FMargin(12, 10, 12, 12))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[ZettaUI::Hint(Intro)]
				+ SVerticalBox::Slot().AutoHeight()[Form]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()[AboveFooter]
		+ SVerticalBox::Slot().AutoHeight()[Footer];
}
