// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"

class AActor;

/** The open level as an indented outline, by Outliner folder: shared by level_outline and the editor snapshot. */
namespace ZettaOutline
{
	struct FOptions
	{
		/** Only this actor and what is attached under it. */
		AActor* Root = nullptr;
		int32 Depth = 3;
		bool bComponents = true;
		int32 Offset = 0;
		int32 Limit = 200;
		int32 MaxChars = 100000;
	};

	/** The outline text; OutTotal is the number of lines before offset/limit. */
	FString Build(const FOptions& Options, int32& OutTotal);

	/** "[StaticMeshActor · SM_Chair]": the class and its key components (mesh asset, light type). */
	FString Brief(const AActor* Actor, bool bComponents);
}
