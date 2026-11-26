// Copyright Zetta 3D. See LICENSE.md.

#include "Agent/SZettaAgentPage.h"
#include "Agent/ZettaAgentAsk.h"
#include "Agent/ZettaAgentChangeLog.h"
#include "Agent/ZettaAgentContextMenu.h"
#include "Agent/ZettaAgentMarkdown.h"
#include "Agent/ZettaAgentRuntime.h"
#include "Modules/ModuleManager.h"
#include "Zetta3DEditorModule.h"
#include "ZettaPipelines.h"
#include "ZettaAgentTools.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

/**
 * Phase 2: the Zetta Agent — the Agent page of the Zetta 3D window, its client tools, and "Add to Zetta Agent"
 * in the Content Browser and the actor menus.
 */
class FZetta3DAgentModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		ZettaAgentMarkdown::Initialize();

		// The Unreal tools first, then ask_user, which needs the chat to show its card.
		ZettaRegisterAgentTools(FZettaAgentTools::Get());
		ZettaAgentAsk::Register();

		FZettaAgentRuntime::Get().Initialize();
		// When the agent needs the user (an approval, a question), the Agent page comes forward.
		NeedsUserHandle = FZettaAgentRuntime::Get().OnNeedsUser.AddLambda([]() { FZetta3DEditorModule::Get().OpenWindow(TEXT("Agent")); });
		FZettaAgentChangeLog::Get().Prune();

		FZetta3DEditorModule::Get().RegisterPage({ TEXT("Agent"), LOCTEXT("AgentUI.PageAgent", "Agent"), 40, []() -> TSharedRef<SWidget>
		{
			return SNew(SZettaAgentPage);
		} });
		ZettaAgentContextMenu::Register();
		SettingsHandle = FZetta3DEditorModule::Get().OnOpenAgentSettings.AddLambda([]() { SZettaAgentPage::ShowSettings(); });

		// Pipeline runs the agent started come back to the conversation that started them (pipeline_run_finished).
		// Attached at startup so the events arrive even when the Pipelines page was never opened.
		PipelineHandle = ZettaPipelines::OnRunFinished().AddLambda([](const ZettaPipelines::FRunEvent& Event)
		{
			ZettaAgentSession::PipelineRunFinished(Event.RunId, Event.Status, Event.ImportedPaths);
		});
	}

	virtual void ShutdownModule() override
	{
		ZettaPipelines::OnRunFinished().Remove(PipelineHandle);
		if (FModuleManager::Get().IsModuleLoaded("Zetta3DEditor")) FZetta3DEditorModule::Get().OnOpenAgentSettings.Remove(SettingsHandle);
		ZettaAgentContextMenu::Unregister();
		FZettaAgentRuntime::Get().OnNeedsUser.Remove(NeedsUserHandle);
		FZettaAgentRuntime::Get().Shutdown();
		if (FModuleManager::Get().IsModuleLoaded("Zetta3DEditor")) FZetta3DEditorModule::Get().UnregisterPage(TEXT("Agent"));
		ZettaAgentMarkdown::Shutdown();
	}

private:
	FDelegateHandle NeedsUserHandle;
	FDelegateHandle PipelineHandle;
	FDelegateHandle SettingsHandle;
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FZetta3DAgentModule, Zetta3DAgent)
