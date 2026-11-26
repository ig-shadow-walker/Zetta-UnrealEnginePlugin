// Copyright Zetta 3D. See LICENSE.md.

#include "Zetta3DEditorModule.h"

#include "Framework/Docking/TabManager.h"
#include "LevelEditor.h"
#include "SZettaWindow.h"
#include "Studio/ZettaStudio.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "ZettaAuth.h"
#include "ZettaJobs.h"
#include "ZettaPages.h"
#include "ZettaPipelines.h"
#include "ZettaSettings.h"
#include "ZettaStyle.h"
#include "ZettaUserSettingsDetails.h"
#include "PropertyEditorModule.h"

DEFINE_LOG_CATEGORY(LogZetta);

#define LOCTEXT_NAMESPACE "Zetta3D"

const FName FZetta3DEditorModule::TabName(TEXT("Zetta3D"));

void FZetta3DEditorModule::StartupModule()
{
	FZettaStyle::Initialize();

	RegisterPage({ TEXT("Studio"), LOCTEXT("PageStudio", "3D Studio"), 10, &ZettaPages::MakeStudioPage });
	RegisterPage({ TEXT("Library"), LOCTEXT("PageLibrary", "My Generations"), 20, &ZettaPages::MakeLibraryPage });
	RegisterPage({ TEXT("Pipelines"), LOCTEXT("PagePipelines", "Pipelines"), 30, &ZettaPages::MakePipelinesPage });

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName, FOnSpawnTab::CreateRaw(this, &FZetta3DEditorModule::SpawnTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Zetta 3D"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Generate 3D models and pictures, and build your game with the Zetta Agent."))
		.SetIcon(FSlateIcon(FZettaStyle::GetStyleSetName(), "Zetta.Icon"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	// Editor Preferences › Plugins › Zetta 3D gets its "Zetta Agent" section (own API keys).
	FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor").RegisterCustomClassLayout(
		UZettaUserSettings::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FZettaUserSettingsDetails::MakeInstance));

	FZettaAuth::Get().Initialize();
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FZetta3DEditorModule::RegisterMenus));
	ShowWelcomeOnce();
}

void FZetta3DEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	ZettaPipelines::Shutdown();
	if (FPropertyEditorModule* PropertyEditor = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		PropertyEditor->UnregisterCustomClassLayout(UZettaUserSettings::StaticClass()->GetFName());
	}
	FZettaJobs::Get().Shutdown();
	FZettaAuth::Get().Shutdown();
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
	}
	FZettaStyle::Shutdown();
}

void FZetta3DEditorModule::RegisterPage(const FZettaPage& Page)
{
	Pages.RemoveAll([&Page](const FZettaPage& P) { return P.Id == Page.Id; });
	Pages.Add(Page);
	Pages.StableSort([](const FZettaPage& A, const FZettaPage& B) { return A.Order < B.Order; });
	OnPagesChanged.Broadcast();
}

void FZetta3DEditorModule::UnregisterPage(FName Id)
{
	if (Pages.RemoveAll([Id](const FZettaPage& P) { return P.Id == Id; }) > 0)
	{
		OnPagesChanged.Broadcast();
	}
}

void FZetta3DEditorModule::OpenWindow(FName PageId)
{
	PendingPage = PageId;
	if (!bCaptureMode) FGlobalTabmanager::Get()->TryInvokeTab(TabName);
	if (!PageId.IsNone()) OnShowPage.Broadcast(PageId);
}

TSharedRef<SDockTab> FZetta3DEditorModule::SpawnTab(const FSpawnTabArgs& Args)
{
	const FName Initial = PendingPage;
	PendingPage = NAME_None;
	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SZettaWindow).InitialPage(Initial)
		];
	LiveTab = Tab;
	return Tab;
}

void FZetta3DEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	// Content Browser items: make a 3D model from a texture, send a mesh to a Studio tool.
	ZettaStudio::RegisterMenus();

	// Window › Zetta 3D › …
	if (UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window"))
	{
		FToolMenuSection& Section = WindowMenu->FindOrAddSection("Zetta3D", LOCTEXT("WindowSection", "Zetta 3D"));
		Section.AddSubMenu("Zetta3DMenu", LOCTEXT("WindowMenu", "Zetta 3D"), LOCTEXT("WindowMenuTip", "Open the Zetta 3D window"),
			FNewToolMenuDelegate::CreateLambda([this](UToolMenu* Menu)
			{
				FToolMenuSection& Pages3D = Menu->AddSection("Pages", LOCTEXT("PagesSection", "Open"));
				for (const FZettaPage& Page : GetPages())
				{
					const FName Id = Page.Id;
					Pages3D.AddMenuEntry(Id, Page.Label, FText(), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([this, Id]() { OpenWindow(Id); })));
				}
				FToolMenuSection& Account = Menu->AddSection("Account", LOCTEXT("AccountSection", "Account"));
				Account.AddMenuEntry("Zetta3DSettings", LOCTEXT("OpenSettings", "Settings…"), FText(), FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([]()
					{
						FGlobalTabmanager::Get()->TryInvokeTab(FName("ProjectSettings"));
					})));
			}),
			false, FSlateIcon(FZettaStyle::GetStyleSetName(), "Zetta.Icon"));
	}

	// A button on the level editor's toolbar.
	if (UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User"))
	{
		FToolMenuSection& Section = Toolbar->FindOrAddSection("Zetta3D");
		FToolMenuEntry Entry = FToolMenuEntry::InitToolBarButton("Zetta3DOpen",
			FUIAction(FExecuteAction::CreateLambda([this]() { OpenWindow(); })),
			LOCTEXT("ToolbarLabel", "Zetta 3D"), LOCTEXT("ToolbarTip", "Open Zetta 3D: the 3D Studio, your generations, pipelines and the agent"),
			FSlateIcon(FZettaStyle::GetStyleSetName(), "Zetta.Icon"));
		Section.AddEntry(Entry);
	}
}

void FZetta3DEditorModule::ShowWelcomeOnce()
{
	// The first time the plugin loads, the window opens on its own — install, then sign in, nothing else.
	UZettaUserSettings* Settings = UZettaUserSettings::Get();
	if (Settings->bWelcomeShown) return;
	Settings->bWelcomeShown = true;
	Settings->Save();
	FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([this]() { OpenWindow(); });
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FZetta3DEditorModule, Zetta3DEditor)
