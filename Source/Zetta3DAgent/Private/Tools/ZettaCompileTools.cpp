// Copyright Zetta 3D. See LICENSE.md.
// compile: the project's C++ through Live Coding (Windows) or, when Live Coding is off or missing, the editor's own
// hot reload — the same two paths the editor's Compile button takes. Errors come back in the protocol's form.

#include "Containers/Ticker.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Interfaces/IProjectManager.h"
#include "Internationalization/Regex.h"
#include "Misc/HotReloadInterface.h"
#include "Misc/Paths.h"
#include "ProjectDescriptor.h"
#include "ZettaAgentTools.h"
#include "ZettaCompile.h"
#include "ZettaLogBuffer.h"
#include "ZettaToolCommon.h"

#if WITH_LIVE_CODING
#include "ILiveCodingModule.h"
#endif

namespace ZettaCompileImpl
{
	TArray<ZettaCompile::FError> LastErrors;
	/** Since the editor started: C++ files the agent created, and reflected headers it changed. */
	TSet<FString> NewFiles;
	TSet<FString> ReflectedHeaders;
	bool bBuildRulesChanged = false;

	bool IsReflected(const FString& Content)
	{
		static const TCHAR* Markers[] = { TEXT("UCLASS("), TEXT("USTRUCT("), TEXT("UENUM("), TEXT("UINTERFACE("), TEXT("UPROPERTY("), TEXT("UFUNCTION("), TEXT("GENERATED_BODY") };
		for (const TCHAR* M : Markers)
		{
			if (Content.Contains(M, ESearchCase::CaseSensitive)) return true;
		}
		return false;
	}

	/** What the user should know beyond the result: changes Live Coding / hot reload can't take. */
	FString RestartNote()
	{
		TArray<FString> Why;
		if (NewFiles.Num() > 0) Why.Add(FString::Printf(TEXT("new C++ files (%s)"), *FString::Join(NewFiles.Array(), TEXT(", "))));
		if (ReflectedHeaders.Num() > 0) Why.Add(FString::Printf(TEXT("changes to reflected types (UCLASS/UPROPERTY/UFUNCTION) in %s"), *FString::Join(ReflectedHeaders.Array(), TEXT(", "))));
		if (bBuildRulesChanged) Why.Add(TEXT("changed build rules (.Build.cs / .Target.cs)"));
		if (Why.Num() == 0) return FString();
		return FString::Printf(TEXT("\nNote: %s may not take effect until the editor is closed, the project is built from the IDE, and the editor reopened. Tell the user if the new code doesn't show up."),
			*FString::Join(Why, TEXT(" and ")));
	}

	FString Format(const TArray<ZettaCompile::FError>& Errors)
	{
		TArray<FString> Lines;
		for (int32 i = 0; i < Errors.Num() && i < 20; ++i)
		{
			const ZettaCompile::FError& E = Errors[i];
			Lines.Add(E.File.IsEmpty() ? E.Message : FString::Printf(TEXT("%s(%d): %s"), *E.File, E.Line, *E.Message));
		}
		FString Text = TEXT("Compile errors:\n") + FString::Join(Lines, TEXT("\n"));
		if (Errors.Num() > 20) Text += FString::Printf(TEXT("\n… and %d more."), Errors.Num() - 20);
		return Text;
	}

	FZettaToolResult Finish(bool bSucceeded, bool bNoChanges, const TArray<FString>& Output, const FString& How)
	{
		LastErrors = ZettaCompile::ParseErrors(Output);
		if (bSucceeded)
		{
			LastErrors.Reset();
			FString Text = bNoChanges ? TEXT("Compiled OK (nothing had changed).") : FString::Printf(TEXT("Compiled OK (%s)."), *How);
			Text += RestartNote();
			// A successful build takes the agent's pending C++ with it.
			NewFiles.Reset();
			ReflectedHeaders.Reset();
			return ZettaTool::Ok(Text, TEXT("Compiled OK"));
		}
		if (LastErrors.Num() == 0)
		{
			// No recognisable error lines: hand back the tail of the output so the model can read it.
			TArray<FString> Tail;
			for (int32 i = FMath::Max(0, Output.Num() - 30); i < Output.Num(); ++i) Tail.Add(Output[i]);
			ZettaCompile::FError E;
			E.Message = TEXT("The build failed. Its last output lines:\n") + FString::Join(Tail, TEXT("\n")).Right(6000);
			LastErrors.Add(E);
		}
		FZettaToolResult R = ZettaTool::Ok(Format(LastErrors) + RestartNote(), TEXT("Compile errors"));
		return R;
	}

	FZettaToolResult NoRebuild(const FString& Why)
	{
		return ZettaTool::Fail(FString::Printf(TEXT("The C++ can't be rebuilt from inside this editor: %s. The files are saved; ask the user to close the editor, build the project from their IDE "
			"(or Unreal's build script), and reopen it. Then check output_log for errors."), *Why));
	}

	void Compile(const TSharedPtr<FJsonObject>& In, FZettaToolDone Done)
	{
		const TArray<FString> Modules = ZettaCompile::ProjectModules();
		if (Modules.Num() == 0 && !IFileManager::Get().DirectoryExists(*(ZettaTool::ProjectDir() + TEXT("Source"))))
		{
			Done(ZettaTool::Ok(TEXT("This is a Blueprint-only project (no C++ modules), so there is no C++ to compile. Blueprints compile with edit_blueprint. "
				"To add C++, the user can use Tools › New C++ Class, which sets the project up for it.")));
			return;
		}
		const uint64 Before = ZettaLogBuffer::LastSequence();
		auto Output = [Before]()
		{
			TArray<FString> Lines;
			for (const FZettaLogEntry& E : ZettaLogBuffer::Since(Before)) Lines.Add(E.Message);
			return Lines;
		};

#if WITH_LIVE_CODING
		if (ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME))
		{
			if (LiveCoding->IsEnabledByDefault())
			{
				LiveCoding->EnableForSession(true);
				if (!LiveCoding->IsEnabledForSession())
				{
					const FString Why = LiveCoding->GetEnableErrorText().ToString();
					Done(NoRebuild(Why.IsEmpty() ? TEXT("Live Coding could not be turned on for this session") : Why));
					return;
				}
				if (LiveCoding->IsCompiling())
				{
					Done(ZettaTool::Fail(TEXT("A Live Coding compile is already running. Wait for it to finish, then call compile again.")));
					return;
				}
				ELiveCodingCompileResult Result = ELiveCodingCompileResult::NotStarted;
				LiveCoding->Compile(ELiveCodingCompileFlags::WaitForCompletion, &Result);
				switch (Result)
				{
				case ELiveCodingCompileResult::Success: Done(Finish(true, false, Output(), TEXT("Live Coding patched the running editor"))); return;
				case ELiveCodingCompileResult::NoChanges: Done(Finish(true, true, Output(), TEXT("Live Coding"))); return;
				case ELiveCodingCompileResult::Failure: Done(Finish(false, false, Output(), TEXT("Live Coding"))); return;
				case ELiveCodingCompileResult::Cancelled: Done(ZettaTool::Fail(TEXT("The compile was cancelled."))); return;
				case ELiveCodingCompileResult::CompileStillActive: Done(ZettaTool::Fail(TEXT("A Live Coding compile is already running. Wait for it to finish, then call compile again."))); return;
				default: Done(NoRebuild(TEXT("Live Coding could not start"))); return;
				}
			}
		}
#endif

		// Live Coding is Windows-only (and may be turned off). The editor's hot reload is the other in-editor path: it runs
		// Unreal's own build tool on one module, writes the result under a new name, and loads it in place of the old one —
		// what the editor's Compile button did before Live Coding. RecompileModule is the call that BUILDS;
		// DoHotReloadFromEditor only rebinds modules something else already built (it answered "Unsupported" here).
		IHotReloadInterface* HotReload = FModuleManager::Get().ModuleExists(TEXT("HotReload")) ? FModuleManager::LoadModulePtr<IHotReloadInterface>(TEXT("HotReload")) : nullptr;
		if (!HotReload)
		{
			Done(NoRebuild(PLATFORM_WINDOWS ? TEXT("Live Coding is turned off and hot reload is not available") : TEXT("Live Coding is Windows-only and hot reload is not available in this editor")));
			return;
		}
		if (HotReload->IsCurrentlyCompiling())
		{
			Done(ZettaTool::Fail(TEXT("A compile is already running. Wait for it to finish, then call compile again.")));
			return;
		}
		if (ZettaTool::IsPlaying())
		{
			Done(ZettaTool::Fail(TEXT("Play In Editor is running; hot reload can't rebuild while it plays. Stop it first (play_in_editor stop).")));
			return;
		}
		if (Modules.Num() == 0)
		{
			Done(NoRebuild(TEXT("the project lists no C++ modules in its .uproject")));
			return;
		}

		// What the build tool prints comes back through the compiler's finished event; the log buffer adds the rest.
		struct FCapture
		{
			TArray<FString> Lines;
			FDelegateHandle Handle;
		};
		TSharedRef<FCapture> Capture = MakeShared<FCapture>();
		Capture->Handle = HotReload->OnModuleCompilerFinished().AddLambda([Capture](const FString& Log, ECompilationResult::Type, bool)
		{
			TArray<FString> Lines;
			Log.ParseIntoArrayLines(Lines);
			Capture->Lines.Append(Lines);
		});

		// One module at a time, in the .uproject's order, each reloaded as it finishes. The call blocks the editor with
		// its own progress dialog while the build runs — the same as the old Compile button.
		bool bAllOk = true;
		for (const FString& Module : Modules)
		{
			const bool bOk = HotReload->RecompileModule(FName(*Module), *GLog, ERecompileModuleFlags::ReloadAfterRecompile);
			if (!bOk)
			{
				bAllOk = false;
				break;
			}
		}
		HotReload->OnModuleCompilerFinished().Remove(Capture->Handle);

		TArray<FString> Lines = Capture->Lines;
		Lines.Append(Output());
		Done(Finish(bAllOk, false, Lines, FString::Printf(TEXT("hot reload rebuilt and reloaded %s"), *FString::Join(Modules, TEXT(", ")))));
	}
}

namespace ZettaCompile
{
	void NoteWrite(const FString& RelativePath, bool bNewFile, const FString& Content)
	{
		const FString Ext = FPaths::GetExtension(RelativePath).ToLower();
		const bool bCpp = Ext == TEXT("h") || Ext == TEXT("hpp") || Ext == TEXT("cpp") || Ext == TEXT("c") || Ext == TEXT("inl");
		if (bCpp && bNewFile) ZettaCompileImpl::NewFiles.Add(RelativePath);
		if ((Ext == TEXT("h") || Ext == TEXT("hpp")) && ZettaCompileImpl::IsReflected(Content)) ZettaCompileImpl::ReflectedHeaders.Add(RelativePath);
		if (RelativePath.EndsWith(TEXT(".Build.cs")) || RelativePath.EndsWith(TEXT(".Target.cs"))) ZettaCompileImpl::bBuildRulesChanged = true;
	}

	TArray<TSharedPtr<FJsonValue>> LastErrorsJson()
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (int32 i = 0; i < ZettaCompileImpl::LastErrors.Num() && i < 20; ++i)
		{
			const FError& E = ZettaCompileImpl::LastErrors[i];
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			if (!E.File.IsEmpty()) O->SetStringField(TEXT("file"), E.File);
			if (E.Line > 0) O->SetNumberField(TEXT("line"), E.Line);
			O->SetStringField(TEXT("message"), E.Message.Left(500));
			Out.Add(MakeShared<FJsonValueObject>(O));
		}
		return Out;
	}

	bool LiveCodingAvailable()
	{
#if WITH_LIVE_CODING
		const ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
		return LiveCoding && LiveCoding->IsEnabledByDefault();
#else
		return false;
#endif
	}

	TArray<FString> ProjectModules()
	{
		TArray<FString> Out;
		if (const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject())
		{
			for (const FModuleDescriptor& M : Project->Modules) Out.Add(M.Name.ToString());
		}
		return Out;
	}

	TArray<FError> ParseErrors(const TArray<FString>& Lines)
	{
		TArray<FError> Out;
		// MSVC: File.cpp(42): error C2065: … — clang: File.cpp:42:10: error: …
		const FRegexPattern Msvc(TEXT("^\\s*(.+?)\\((\\d+)(?:,\\d+)?\\)\\s*:\\s*(?:fatal\\s+)?error\\s*(.*)$"));
		const FRegexPattern Clang(TEXT("^\\s*(.+?):(\\d+):(?:\\d+:)?\\s*(?:fatal\\s+)?error:\\s*(.*)$"));
		TSet<FString> Seen;
		for (const FString& Raw : Lines)
		{
			FString L = Raw;
			// Log prefixes ("LogLiveCoding: Display: ") come before the compiler's line.
			int32 At = L.Find(TEXT(": Display: "));
			if (At != INDEX_NONE) L = L.RightChop(At + 11);
			FError E;
			FRegexMatcher M1(Msvc, L);
			FRegexMatcher M2(Clang, L);
			if (M1.FindNext())
			{
				E.File = M1.GetCaptureGroup(1);
				E.Line = FCString::Atoi(*M1.GetCaptureGroup(2));
				E.Message = M1.GetCaptureGroup(3).TrimStartAndEnd();
				E.Message.RemoveFromStart(TEXT(":"));
				E.Message.TrimStartInline();
			}
			else if (M2.FindNext())
			{
				E.File = M2.GetCaptureGroup(1);
				E.Line = FCString::Atoi(*M2.GetCaptureGroup(2));
				E.Message = M2.GetCaptureGroup(3).TrimStartAndEnd();
			}
			else if (L.Contains(TEXT("error LNK")) || L.Contains(TEXT("Undefined symbols")) || L.Contains(TEXT("linker command failed")) || L.TrimStart().StartsWith(TEXT("error:")))
			{
				E.Message = L.TrimStartAndEnd();
			}
			else
			{
				continue;
			}
			if (!E.File.IsEmpty())
			{
				FString File = E.File.Replace(TEXT("\\"), TEXT("/"));
				const FString Relative = ZettaTool::ProjectRelative(File);
				E.File = Relative;
			}
			const FString Key = FString::Printf(TEXT("%s|%d|%s"), *E.File, E.Line, *E.Message);
			if (Seen.Contains(Key)) continue;
			Seen.Add(Key);
			Out.Add(E);
		}
		return Out;
	}
}

void ZettaToolAreas::RegisterCompileTools(FZettaAgentTools& Tools)
{
	Tools.RegisterAsync(TEXT("compile"), &ZettaCompileImpl::Compile, [](const TSharedPtr<FJsonObject>&, TFunction<void(const FZettaToolPreview&)> Done)
	{
		Done(ZettaTool::Preview(TEXT("Compile the C++"), ZettaCompile::LiveCodingAvailable()
			? TEXT("Live Coding builds the project's C++ and patches the running editor.")
			: TEXT("Hot reload builds the project's C++ and reloads it in the editor.")));
	});
}
