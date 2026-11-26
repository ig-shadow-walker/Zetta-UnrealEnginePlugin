// Copyright Zetta 3D. See LICENSE.md.

// A maintainer's tool: `Zetta.CaptureDocs [OutputFolder]` in the editor's console takes the README's screenshots.
// It opens each page of the Zetta 3D window at one fixed size, waits for its pictures to load, and renders the window
// itself to PNG with Slate — so nothing else on the screen can end up in an image, and every release can refresh them
// the same way. Shots land in Saved/Zetta3D/DocShots unless a folder is given.

#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"
#include "SZettaWindow.h"
#include "Zetta3DEditorModule.h"
#include "ZettaAuth.h"
#include "ZettaSettings.h"
#include "ZettaPipelines.h"

namespace ZettaDocsCaptureImpl
{
	/** One shot: what to open, how long to let it settle, and the file it becomes. */
	struct FShot
	{
		FString Name;
		TFunction<void()> Open;
		float Settle = 3.f;
	};

	static const FVector2D WindowSize(1440, 900);

	/**
	 * The window the shots are taken of: its own, at a fixed size, holding the same Zetta 3D view the tab does. The
	 * tab can't be relied on — at startup the editor's saved layout may hold it without a window, or dock it anywhere.
	 */
	TSharedPtr<SWindow> CaptureWindow;
	TSharedPtr<SWindow> ZettaWindow() { return CaptureWindow; }

	void OpenCaptureWindow()
	{
		if (CaptureWindow.IsValid()) return;
		// Only this window answers page requests while the shots are taken: the tab is closed and stays closed.
		FZetta3DEditorModule& Module = FZetta3DEditorModule::Get();
		if (const TSharedPtr<SDockTab> Tab = Module.GetLiveTab()) Tab->RequestCloseTab();
		Module.SetCaptureMode(true);
		CaptureWindow = SNew(SWindow)
			.Title(FText::FromString(TEXT("Zetta 3D")))
			.ClientSize(WindowSize)
			.SizingRule(ESizingRule::FixedSize)
			.SupportsMaximize(false)
			.SupportsMinimize(false)
			[
				SNew(SZettaWindow).InitialPage(TEXT("Studio"))
			];
		FSlateApplication::Get().AddWindow(CaptureWindow.ToSharedRef());
	}

	bool SavePng(const TArray<FColor>& Pixels, const FIntVector& Size, const FString& Path)
	{
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Png = Module.CreateImageWrapper(EImageFormat::PNG);
		TArray<FColor> Opaque = Pixels;
		// A widget readback can leave alpha at zero; a screenshot is opaque.
		for (FColor& C : Opaque) C.A = 255;
		if (!Png.IsValid() || !Png->SetRaw(Opaque.GetData(), Opaque.Num() * sizeof(FColor), Size.X, Size.Y, ERGBFormat::BGRA, 8)) return false;
		return FFileHelper::SaveArrayToFile(Png->GetCompressed(), *Path);
	}

	void Capture(const FString& Folder, const FString& Name)
	{
		const TSharedPtr<SWindow> Window = ZettaWindow();
		if (!Window.IsValid())
		{
			UE_LOG(LogZetta, Warning, TEXT("CaptureDocs: the Zetta 3D window isn't open; skipped %s."), *Name);
			return;
		}
		TArray<FColor> Pixels;
		FIntVector Size;
		if (!FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(), Pixels, Size) || Pixels.Num() == 0)
		{
			UE_LOG(LogZetta, Warning, TEXT("CaptureDocs: could not read back the window for %s."), *Name);
			return;
		}
		const FString Path = FPaths::Combine(Folder, Name + TEXT(".png"));
		UE_LOG(LogZetta, Display, TEXT("CaptureDocs: %s %s (%dx%d)"), SavePng(Pixels, Size, Path) ? TEXT("saved") : TEXT("FAILED"), *Path, Size.X, Size.Y);
	}

	void Run(const TArray<FString>& Args)
	{
		// `live` anywhere in the arguments: take the shots against the live server, whatever the editor is set to (in
		// memory only, put back when done — a developer's local server is often not running, and shots need real data).
		TArray<FString> Rest = Args;
		const bool bLive = Rest.RemoveAll([](const FString& A) { return A.Equals(TEXT("live"), ESearchCase::IgnoreCase); }) > 0;
		const FString Folder = Rest.Num() > 0 ? Rest[0] : FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Zetta3D"), TEXT("DocShots"));
		struct FServer { FString Api, Site; };
		TSharedRef<TOptional<FServer>> Previous = MakeShared<TOptional<FServer>>();
		if (bLive)
		{
			UZettaUserSettings* Settings = UZettaUserSettings::Get();
			*Previous = FServer{ Settings->ApiBaseOverride, Settings->SiteBaseOverride };
			Settings->ApiBaseOverride.Reset();
			Settings->SiteBaseOverride.Reset();
			FZettaAuth::Get().Refresh();
		}
		IFileManager::Get().MakeDirectory(*Folder, true);

		FZetta3DEditorModule& Module = FZetta3DEditorModule::Get();
		TArray<FShot> Shots = {
			{ TEXT("studio"), [&Module]() { Module.OpenWindow(TEXT("Studio")); }, 4.f },
			{ TEXT("library"), [&Module]() { Module.OpenWindow(TEXT("Library")); }, 10.f },
			{ TEXT("pipelines"), [&Module]()
				{
					Module.OpenWindow(TEXT("Pipelines"));
					// The newest pipeline, open on its canvas: a recipe says more than the list.
					ZettaPipelines::List([](const ZettaPipelines::FListResult& R)
					{
						if (R.bOk && R.Pipelines.Num() > 0) ZettaPipelines::OpenInWindow(R.Pipelines[0].Id);
					});
				}, 10.f },
			{ TEXT("agent"), [&Module]() { Module.OpenWindow(TEXT("Agent")); }, 4.f },
			{ TEXT("agent-keys"), [&Module]() { Module.OpenAgentSettings(); }, 4.f },
		};

		// Started from -ExecCmds the command runs before the editor's main window exists, when the tab can't open yet:
		// keep asking (once a second, for a minute) until the Zetta 3D window is up, size it, then start the shots.
		TSharedRef<int32> Tries = MakeShared<int32>(0);
		// One shot after another: open, let it settle, capture.
		TSharedRef<int32> Index = MakeShared<int32>(0);
		TSharedRef<TArray<FShot>> Queue = MakeShared<TArray<FShot>>(MoveTemp(Shots));
		TSharedRef<TFunction<void()>> Next = MakeShared<TFunction<void()>>();
		*Next = [Index, Queue, Next, Folder, Previous]()
		{
			if (*Index >= Queue->Num())
			{
				UE_LOG(LogZetta, Display, TEXT("CaptureDocs: done — %d shots in %s"), Queue->Num(), *Folder);
				if (CaptureWindow.IsValid()) CaptureWindow->RequestDestroyWindow();
				CaptureWindow.Reset();
				FZetta3DEditorModule::Get().SetCaptureMode(false);
				if (Previous->IsSet())
				{
					UZettaUserSettings::Get()->ApiBaseOverride = Previous->GetValue().Api;
					UZettaUserSettings::Get()->SiteBaseOverride = Previous->GetValue().Site;
				}
				// Break the cycle on the next tick, not from inside this call.
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Next](float) { *Next = nullptr; return false; }));
				return;
			}
			const FShot Shot = (*Queue)[*Index];
			Shot.Open();
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Index, Next, Folder, Name = Shot.Name](float)
			{
				Capture(Folder, Name);
				++*Index;
				(*Next)();
				return false;
			}), Shot.Settle);
		};
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Tries, Next, &Module](float)
		{
			// Wait for the editor's main window: a window added before it has nothing to sit beside.
			if (!FSlateApplication::Get().GetActiveTopLevelRegularWindow().IsValid() && ++*Tries < 60) return true;
			OpenCaptureWindow();
			// Give the resized window a moment to lay out before the first shot.
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Next](float) { (*Next)(); return false; }), 2.f);
			return false;
		}), 1.f);
	}

	static FAutoConsoleCommand Command(
		TEXT("Zetta.CaptureDocs"),
		TEXT("Takes the README's screenshots of the Zetta 3D window (Studio, My Generations, Pipelines, Agent) into Saved/Zetta3D/DocShots, or the folder given."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&Run));
}
