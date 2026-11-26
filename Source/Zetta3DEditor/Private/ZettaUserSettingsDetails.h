// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

/**
 * Editor Preferences › Plugins › Zetta 3D: adds the "Zetta Agent" section — what the agent runs on, and the way into
 * the own-API-key settings. It's drawn here rather than left to a CallInEditor button, which the Preferences window
 * doesn't show for settings objects.
 */
class FZettaUserSettingsDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance() { return MakeShared<FZettaUserSettingsDetails>(); }
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
