// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaAgentContextMenu.h"

#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "LevelEditor.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "SZettaAgentPage.h"
#include "ZettaAgentCards.h"
#include "ZettaAgentMentions.h"
#include "ZettaStyle.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace
{
	const FName AgentMenuOwner(TEXT("Zetta3DAgent"));

	class FZettaAgentCommands : public TCommands<FZettaAgentCommands>
	{
	public:
		FZettaAgentCommands()
			: TCommands<FZettaAgentCommands>(TEXT("ZettaAgent"), LOCTEXT("AgentUI.ZettaAgentCommands", "Zetta Agent"), NAME_None, FAppStyle::GetAppStyleSetName())
		{
		}

		virtual void RegisterCommands() override
		{
			UI_COMMAND(AddToAgent, "Add to Zetta Agent", "Tag the selected actors or assets in the Zetta Agent's message box",
				EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::M));
		}

		TSharedPtr<FUICommandInfo> AddToAgent;
	};

	TSharedPtr<FUICommandList> AgentMenuCommands;
	FDelegateHandle AgentMenuStartupHandle;

	void AddToAgentChat(const TArray<FZettaMention>& Mentions)
	{
		if (SZettaAgentPage::AddToChat(Mentions) == 0)
		{
			ZettaAgentCards::Toast(LOCTEXT("AgentUI.NothingToTag", "Select actors in the level or assets in the Content Browser to add them to the agent."));
		}
	}

	/** The shortcut: the actors selected in the level, or — with none — the assets selected in the Content Browser. */
	void AddSelectionToAgent()
	{
		TArray<FZettaMention> Mentions = ZettaMentions::SelectedActors();
		if (Mentions.Num() == 0) Mentions = ZettaMentions::SelectedAssets();
		AddToAgentChat(Mentions);
	}

	FText AgentMenuLabel(int32 Count)
	{
		return Count > 1 ? FText::Format(LOCTEXT("AgentUI.AddToAgentCount", "Add to Zetta Agent ({0})"), Count) : LOCTEXT("AgentUI.AddToAgent", "Add to Zetta Agent");
	}

	void RegisterAgentMenus()
	{
		FToolMenuOwnerScoped Owner(AgentMenuOwner);
		const FSlateIcon Icon(FZettaStyle::GetStyleSetName(), "Zetta.Icon");

		if (UToolMenu* Assets = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.AssetContextMenu")))
		{
			FToolMenuSection& Section = Assets->FindOrAddSection(TEXT("Zetta3D"), LOCTEXT("AgentUI.Zetta3DSection", "Zetta 3D"));
			Section.AddMenuEntry(TEXT("AddToZettaAgent"),
				MakeAttributeLambda([]() { return AgentMenuLabel(ZettaMentions::SelectedAssets().Num()); }),
				LOCTEXT("AgentUI.AddAssetsTip", "Tag the selected assets in the Zetta Agent's message box"),
				Icon,
				FUIAction(FExecuteAction::CreateLambda([]() { AddToAgentChat(ZettaMentions::SelectedAssets()); })));
		}

		// The level editor's actor menu: the one the Outliner and the level viewport both show.
		if (UToolMenu* Actors = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.ActorContextMenu")))
		{
			FToolMenuSection& Section = Actors->FindOrAddSection(TEXT("Zetta3D"), LOCTEXT("AgentUI.Zetta3DSection", "Zetta 3D"));
			Section.AddMenuEntryWithCommandList(FZettaAgentCommands::Get().AddToAgent, AgentMenuCommands,
				MakeAttributeLambda([]() { return AgentMenuLabel(ZettaMentions::SelectedActors().Num()); }),
				LOCTEXT("AgentUI.AddActorsTip", "Tag the selected actors in the Zetta Agent's message box"),
				Icon);
		}
	}
}

namespace ZettaAgentContextMenu
{
	void Register()
	{
		FZettaAgentCommands::Register();
		AgentMenuCommands = MakeShared<FUICommandList>();
		AgentMenuCommands->MapAction(FZettaAgentCommands::Get().AddToAgent, FExecuteAction::CreateStatic(&AddSelectionToAgent));

		// The shortcut works wherever the level editor handles keys: the viewport, the Outliner, the Content Browser.
		FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditor.GetGlobalLevelEditorActions()->Append(AgentMenuCommands.ToSharedRef());

		AgentMenuStartupHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateStatic(&RegisterAgentMenus));
	}

	void Unregister()
	{
		if (UObjectInitialized() && UToolMenus::TryGet())
		{
			UToolMenus::UnRegisterStartupCallback(AgentMenuStartupHandle);
			UToolMenus::UnregisterOwner(AgentMenuOwner);
		}
		// The level editor holds the appended list weakly: letting it go takes the shortcut with it.
		AgentMenuCommands.Reset();
		FZettaAgentCommands::Unregister();
	}
}

#undef LOCTEXT_NAMESPACE
