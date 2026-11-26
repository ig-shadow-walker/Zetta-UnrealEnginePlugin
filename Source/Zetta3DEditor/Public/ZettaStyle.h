// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/**
 * The plugin's look: the editor's own widgets and fonts, with Zetta's accent. Everything else comes from
 * FAppStyle so the window sits naturally beside the editor's panels.
 */
class ZETTA3DEDITOR_API FZettaStyle
{
public:
	static void Initialize();
	static void Shutdown();
	static const ISlateStyle& Get();
	static FName GetStyleSetName();

	// Colours
	static FLinearColor Accent();      // Zetta blue
	static FLinearColor AccentHover();
	static FLinearColor Panel();       // card background
	static FLinearColor PanelRaised();
	static FLinearColor Border();
	static FLinearColor Muted();       // secondary text
	static FLinearColor Success();
	static FLinearColor Warning();
	static FLinearColor Error();

	static FSlateFontInfo Font(int32 Size, bool bBold = false);

private:
	static TSharedPtr<FSlateStyleSet> Instance;
};
