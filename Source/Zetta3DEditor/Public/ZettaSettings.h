// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ZettaSettings.generated.h"

UENUM()
enum class EZettaMeshFormat : uint8
{
	GLB UMETA(DisplayName = "GLB (glTF binary)"),
	FBX UMETA(DisplayName = "FBX"),
};

/**
 * Zetta 3D settings that belong to this project and are shared through version control
 * (Config/DefaultZetta3D.ini). Commit that file: the agent's project memory is tied to ProjectId.
 */
UCLASS(config = Zetta3D, defaultconfig, meta = (DisplayName = "Zetta 3D"))
class ZETTA3DEDITOR_API UZettaProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UZettaProjectSettings();

	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	static UZettaProjectSettings* Get() { return GetMutableDefault<UZettaProjectSettings>(); }

	/** Where finished models are imported: <folder>/<id>-<name>/. */
	UPROPERTY(config, EditAnywhere, Category = "Import", meta = (ContentDir))
	FDirectoryPath ModelsFolder;

	/** Where pictures saved from the library land as textures. */
	UPROPERTY(config, EditAnywhere, Category = "Import", meta = (ContentDir))
	FDirectoryPath ImagesFolder;

	/** The mesh format to download when a model has both. */
	UPROPERTY(config, EditAnywhere, Category = "Import")
	EZettaMeshFormat PreferredFormat = EZettaMeshFormat::GLB;

	/** Place finished models in the open level, resting on the ground in front of the camera. */
	UPROPERTY(config, EditAnywhere, Category = "Import")
	bool bPlaceInLevel = true;

	/** The id the agent's project memory and brief are filed under. Made once; keep it in version control. */
	UPROPERTY(config, VisibleAnywhere, Category = "Agent")
	FString ProjectId;

	/** Returns ProjectId, creating and saving it the first time. */
	FString EnsureProjectId();

	/** Content paths, normalised to "/Game/…" without a trailing slash. */
	FString GetModelsFolder() const;
	FString GetImagesFolder() const;
};

/**
 * Zetta 3D settings that belong to this computer, not the project (the editor's per-user settings).
 * Secrets are never stored here: the session and any provider keys live in the system key store.
 */
UCLASS(config = EditorSettings, meta = (DisplayName = "Zetta 3D"))
class ZETTA3DEDITOR_API UZettaUserSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	static UZettaUserSettings* Get() { return GetMutableDefault<UZettaUserSettings>(); }

	/** The API the plugin talks to. Leave empty for production; developers can point it at a local backend. */
	UPROPERTY(config, EditAnywhere, Category = "Server", AdvancedDisplay)
	FString ApiBaseOverride;

	/** The website sign-in opens. Leave empty for production. */
	UPROPERTY(config, EditAnywhere, Category = "Server", AdvancedDisplay)
	FString SiteBaseOverride;

	/** The session's expiry (Unix seconds), read from the token. The token itself is in the key store. */
	UPROPERTY(config)
	int64 TokenExpiry = 0;

	/** Whether the Zetta 3D window has opened on its own once already. */
	UPROPERTY(config)
	bool bWelcomeShown = false;

	/** Agent: run on the user's own API keys instead of the Zetta account. */
	UPROPERTY(config)
	bool bAgentUseOwnKeys = false;

	/** Agent: the provider last picked in the chat (with own keys). */
	UPROPERTY(config)
	FString AgentProvider;

	/** Agent: the model last picked per provider ("provider=model"). */
	UPROPERTY(config)
	TArray<FString> AgentModels;

	/** Agent: autonomy — "confirm_destructive" (Ask first) or "auto" (Full autonomy). */
	UPROPERTY(config)
	FString AgentApproval = TEXT("confirm_destructive");

	/** Agent: the tier picked under the message box ("auto", "fast", "capable", "intelligent"). */
	UPROPERTY(config)
	FString AgentTier = TEXT("auto");

	/** Agent: the thinking level picked under the message box. */
	UPROPERTY(config)
	FString AgentThinking = TEXT("auto");

	/** Agent: Agent or Ask mode. */
	UPROPERTY(config)
	FString AgentMode = TEXT("agent");

	/** Agent: how far the chat's text is from its standard size, in points — the A− / A+ in its toolbar. */
	UPROPERTY(config)
	int32 AgentTextSize = 0;

	static constexpr int32 AgentTextSizeMin = -2;
	static constexpr int32 AgentTextSizeMax = 8;
	int32 GetAgentTextSize() const { return FMath::Clamp(AgentTextSize, AgentTextSizeMin, AgentTextSizeMax); }

	void Save() { SaveConfig(); }

};
