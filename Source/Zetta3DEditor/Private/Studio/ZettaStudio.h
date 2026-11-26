// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"

/** The 3D Studio's entry points for the rest of the plugin. */
namespace ZettaStudio
{
	/**
	 * Opens the Studio on Generate 3D with a picture in its image input. A picture sent before the Studio page
	 * exists waits for it.
	 */
	void SendImageToGenerate(const TArray<uint8>& Bytes, const FString& Name);

	/** Takes the waiting picture, if any (the Studio page calls this). */
	bool TakePendingImage(TArray<uint8>& OutBytes, FString& OutName);

	/** Raised when a picture is sent while the Studio page exists. */
	FSimpleMulticastDelegate& OnImageSent();

	/**
	 * Adds the Studio's Content Browser items: "Zetta 3D › Make a 3D model from this picture" on textures.
	 * Call once from the module's ToolMenus startup callback, inside its owner scope.
	 */
	void RegisterMenus();
}
