// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaConfig.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/EngineVersion.h"
#include "ZettaSettings.h"

namespace ZettaConfig
{
	FString Version()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Zetta3D"));
		return Plugin.IsValid() ? Plugin->GetDescriptor().VersionName : TEXT("0.0.0");
	}

	FString EngineVersion()
	{
		const FEngineVersion& V = FEngineVersion::Current();
		return FString::Printf(TEXT("%d.%d.%d"), V.GetMajor(), V.GetMinor(), V.GetPatch());
	}

	static FString Clean(FString Value, const TCHAR* Fallback)
	{
		Value.TrimStartAndEndInline();
		while (Value.EndsWith(TEXT("/"))) Value.LeftChopInline(1);
		// A dev server says it listens on 0.0.0.0 (every interface), but that is not an address to connect to: macOS
		// rejects it as a bad URL before a request leaves. The machine itself is what's meant.
		Value.ReplaceInline(TEXT("://0.0.0.0"), TEXT("://127.0.0.1"));
		return Value.IsEmpty() ? FString(Fallback) : Value;
	}

	FString ApiBase() { return Clean(UZettaUserSettings::Get()->ApiBaseOverride, ProductionApi); }
	FString SiteBase() { return Clean(UZettaUserSettings::Get()->SiteBaseOverride, ProductionSite); }
	bool IsProduction() { return ApiBase() == ProductionApi; }
}
