// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "ZettaModels.h"

/**
 * Fork — start a new generation from an existing one (the web Studio's fork): the Generate 3D or Smart 3D form
 * filled with the settings it was made with, its reference picture(s) included, and nothing run. Only for the
 * three ways in (text, image, multi-view to 3D) on an engine one of the two tools offers. The settings are read
 * back from the row's job spec the way the request builder writes it, so a fork lands on the price the original paid.
 */
namespace ZettaStudioFork
{
	enum class EWayIn : uint8 { Text, Image, MultiView };

	/** The settings a fork puts on the form; unset fields leave the form's own value. */
	struct FSettings
	{
		FString Model;
		FString Quality;
		FString Output;
		bool bPbr = false;
		bool bSegmentParts = false;
		FString TextureQuality;
		FString PolygonType;
		TOptional<int32> FaceCount;
		FString Prompt;
	};

	struct FPicture
	{
		FString Url;
		FString Name;
		/** The multi-view slot (front / back / left / right), or empty for the single picture. */
		FString View;
	};

	struct FSetup
	{
		int64 FromId = 0;
		FString FromTitle;
		/** "Generate" or "Smart". */
		FName Tool;
		EWayIn WayIn = EWayIn::Text;
		FSettings Settings;
		TArray<FPicture> Pictures;
	};

	/** The tool whose engines include this model ("Generate" / "Smart"), or none. */
	FName ToolFor(const FString& Model);
	bool CanFork(const FZettaGeneration& Generation);
	FSettings SettingsOf(const FZettaGeneration& Generation);
	/** The fork of a row, or unset when it cannot be forked. */
	TOptional<FSetup> Of(const FZettaGeneration& Generation);
}
