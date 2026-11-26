// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"

/** Where the plugin talks to, and how it names itself to the server. */
namespace ZettaConfig
{
	/** What the plugin sends as X-Zetta-Client and as a job's origin. */
	inline const TCHAR* ClientName = TEXT("unreal");

	inline const TCHAR* ProductionApi = TEXT("https://api.zetta3d.ai/api");
	inline const TCHAR* ProductionSite = TEXT("https://zetta3d.ai");

	/** The plugin's version, from Zetta3D.uplugin. */
	ZETTA3DEDITOR_API FString Version();
	/** The engine's version ("5.8.3"). */
	ZETTA3DEDITOR_API FString EngineVersion();
	ZETTA3DEDITOR_API FString ApiBase();
	ZETTA3DEDITOR_API FString SiteBase();
	ZETTA3DEDITOR_API bool IsProduction();
}
