// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "ZettaModels.h"

/**
 * How other parts of the plugin hand work to the 3D Studio: "Send to tool" from the library, "Fork" a
 * generation's settings back into Generate. A request made before the Studio page exists waits for it.
 *
 * Tool ids: Generate, Smart, Segment, Retopology, SmartUv, Texture, AutoRig, ImageStudio.
 */
namespace ZettaStudioLink
{
	struct FRequest
	{
		FName Tool;
		FZettaGenerationPtr Source;
		bool bFork = false;
	};

	DECLARE_MULTICAST_DELEGATE(FOnRequest);
	ZETTA3DEDITOR_API FOnRequest& OnRequest();

	/** Opens the Studio on a tool with a library model as its source. */
	ZETTA3DEDITOR_API void OpenTool(FName Tool, const FZettaGenerationPtr& Source);
	/** Opens Generate (or Smart 3D) filled in with a generation's settings. */
	ZETTA3DEDITOR_API void Fork(const FZettaGenerationPtr& Generation);
	/** The waiting request, if any, handed over once. */
	ZETTA3DEDITOR_API TOptional<FRequest> TakePending();
}
