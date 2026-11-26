// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UObject;

/** Saving and compiling, shared by the asset, Blueprint and property tools. */
namespace ZettaAssets
{
	/** Saves an asset's package to disk. */
	bool Save(UObject* Asset, FString& OutError);
	/** Compiles a Blueprint. Returns "Compiled OK." (with warnings) or the Blueprint's errors, one per line; bOutOk is false on errors. */
	FString Compile(UBlueprint* Blueprint, bool& bOutOk);
	/** Compiles and saves; the text says how both went. */
	FString CompileAndSave(UBlueprint* Blueprint, bool& bOutOk);
	/** inspect_asset for a Blueprint: parent, components (with their changed properties), variables, functions, graph, status. */
	FString DescribeBlueprint(UBlueprint* Blueprint, bool bAll);
}
