// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class SWidget;
class SDockTab;

/** One page of the Zetta 3D window: Studio, My Generations, Pipelines, and (from the agent module) Agent. */
struct FZettaPage
{
	FName Id;
	FText Label;
	/** Lower comes first in the header. */
	int32 Order = 100;
	TFunction<TSharedRef<SWidget>()> Build;
};

DECLARE_LOG_CATEGORY_EXTERN(LogZetta, Log, All);

class ZETTA3DEDITOR_API FZetta3DEditorModule : public IModuleInterface
{
public:
	static FZetta3DEditorModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FZetta3DEditorModule>("Zetta3DEditor");
	}

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Adds a page to the Zetta 3D window. The agent module adds "Agent" this way. */
	void RegisterPage(const FZettaPage& Page);
	void UnregisterPage(FName Id);
	const TArray<FZettaPage>& GetPages() const { return Pages; }

	/** Opens (or focuses) the Zetta 3D window on a page. */
	void OpenWindow(FName PageId = NAME_None);

	/** Raised when a page is added or removed, so an open window can rebuild its header. */
	DECLARE_MULTICAST_DELEGATE(FOnPagesChanged);
	FOnPagesChanged OnPagesChanged;

	/** Raised when something asks the open window to show a page (OpenWindow while it is open). */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnShowPage, FName);
	FOnShowPage OnShowPage;

	/** Raised to open the agent's "What it runs on" settings (own API keys). The agent module answers it. */
	FSimpleMulticastDelegate OnOpenAgentSettings;
	/** Opens the agent's own-key settings, if the agent module is loaded. */
	void OpenAgentSettings()
	{
		if (OnOpenAgentSettings.IsBound()) OnOpenAgentSettings.Broadcast();
		else OpenWindow(TEXT("Agent"));
	}

	static const FName TabName;

	/**
	 * While on, OpenWindow only tells open Zetta 3D views which page to show and never opens the editor tab — the README
	 * screenshot command uses it so its own window gets every request, not a tab the editor's layout keeps hidden.
	 */
	void SetCaptureMode(bool bOn) { bCaptureMode = bOn; }

	/** The open Zetta 3D tab, if any (it remembers the one it spawned). */
	TSharedPtr<SDockTab> GetLiveTab() const { return LiveTab.Pin(); }

private:
	TSharedRef<SDockTab> SpawnTab(const class FSpawnTabArgs& Args);
	void RegisterMenus();
	void ShowWelcomeOnce();

	TArray<FZettaPage> Pages;
	TWeakPtr<SDockTab> LiveTab;
	bool bCaptureMode = false;
	FName PendingPage;
};
