// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"

class SWidget;

/** The window's own pages. Each lives in its own folder under Private/. */
namespace ZettaPages
{
	TSharedRef<SWidget> MakeStudioPage();
	TSharedRef<SWidget> MakeLibraryPage();
	TSharedRef<SWidget> MakePipelinesPage();
}
