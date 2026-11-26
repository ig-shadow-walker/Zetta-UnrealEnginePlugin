// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"

/**
 * "Add to Zetta Agent": tags the selection in the agent's message box — from the Content Browser's right-click
 * menu, the actor right-click menu (Outliner and level viewport), or Ctrl+Shift+M (⇧⌘M on a Mac) anywhere in the
 * level editor. Rebind it in Editor Preferences › Keyboard Shortcuts, under Zetta Agent.
 */
namespace ZettaAgentContextMenu
{
	void Register();
	void Unregister();
}
