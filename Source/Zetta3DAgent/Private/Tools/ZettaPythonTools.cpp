// Copyright Zetta 3D. See LICENSE.md.
// run_python: the agent's escape hatch for anything the other tools don't cover — batch edits, Blueprint graphs,
// editor scripting. It runs in the editor's own Python, so it can't be sandboxed; instead a guard reads the code first
// and refuses, whatever the user's autonomy setting, a short list of things an assistant has no business doing:
// running programs, the network, native code, deleting files, quitting or reconfiguring the editor, this plugin's
// own folder and settings, the system key store, and code built from strings.
//
// The guard is a careful token scan, not a sandbox: comments are dropped, string literals are read separately (for
// paths), import aliases are followed, and the dangerous names are matched as whole words. Known gaps: names computed
// at run time from data, and endless loops (the editor's Python runs on the game thread and can't be interrupted).

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "IPythonScriptPlugin.h"
#include "Internationalization/Regex.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "ZettaAgentTools.h"
#include "ZettaTargets.h"
#include "ZettaToolCommon.h"

namespace ZettaPythonImpl
{
	using ZettaTool::Fail;

	constexpr int32 MaxCode = 20000;

	// ─── The guard ───────────────────────────────────────────────────────────

	struct FRule
	{
		const TCHAR* What;
		const TCHAR* Instead;
	};

	enum class ERule : uint8 { Process, Network, Native, Delete, Editor, Plugin, KeyStore, Dynamic, Config };

	const FRule& RuleText(ERule R)
	{
		static const FRule Rules[] = {
			{ TEXT("running other programs is not allowed"), TEXT("Do it with the unreal module inside the editor, or ask the user to run the program themselves.") },
			{ TEXT("the network is off limits"), TEXT("Ask the user for the file, or use the agent's own tools to fetch things.") },
			{ TEXT("loading native code is not allowed"), TEXT("Use the unreal module, or write C++ in the project and compile it.") },
			{ TEXT("deleting, moving or renaming files is not allowed"), TEXT("Leave files where they are and ask the user to delete or move them; assets are moved and deleted with asset_operation and delete_asset.") },
			{ TEXT("quitting or restarting the editor is not allowed"), TEXT("Ask the user to do it.") },
			{ TEXT("the Zetta 3D plugin's own folder and settings are private"), TEXT("Work with the project's own files and assets.") },
			{ TEXT("the system key store is private"), TEXT("Never read passwords or keys; ask the user for what is needed.") },
			{ TEXT("code built from strings or reached through introspection is not allowed"), TEXT("Write the code out plainly, with literal names and imports.") },
			{ TEXT("writing engine .ini files outside the project is not allowed"), TEXT("Change the project's own Config/ files with edit_file, or ask the user to change editor settings.") },
		};
		return Rules[(int32)R];
	}

	struct FRefusal
	{
		ERule Rule = ERule::Process;
		FString Found;
	};

	/** The code split for scanning: Code without comments and with string contents blanked (f-strings kept), and the literals. */
	struct FScan
	{
		FString Code;
		TArray<FString> Literals;
	};

	FScan Tokenize(const FString& Source)
	{
		FScan S;
		S.Code.Reserve(Source.Len());
		const int32 N = Source.Len();
		int32 i = 0;
		while (i < N)
		{
			const TCHAR C = Source[i];
			if (C == TEXT('#'))
			{
				while (i < N && Source[i] != TEXT('\n')) ++i;
				continue;
			}
			if (C == TEXT('\'') || C == TEXT('"'))
			{
				// String prefix letters just before the quote (r, b, f, u and pairs).
				bool bFormat = false;
				for (int32 k = S.Code.Len() - 1; k >= 0 && k >= S.Code.Len() - 2 && FChar::IsAlpha(S.Code[k]); --k)
				{
					if (FChar::ToLower(S.Code[k]) == TEXT('f')) bFormat = true;
				}
				const bool bTriple = i + 2 < N && Source[i + 1] == C && Source[i + 2] == C;
				const int32 QuoteLen = bTriple ? 3 : 1;
				int32 j = i + QuoteLen;
				FString Literal;
				while (j < N)
				{
					if (Source[j] == TEXT('\\') && j + 1 < N)
					{
						Literal.AppendChar(Source[j]);
						Literal.AppendChar(Source[j + 1]);
						j += 2;
						continue;
					}
					if (bTriple ? (j + 2 < N && Source[j] == C && Source[j + 1] == C && Source[j + 2] == C) : Source[j] == C) break;
					if (!bTriple && Source[j] == TEXT('\n')) break;
					Literal.AppendChar(Source[j]);
					++j;
				}
				S.Literals.Add(Literal);
				S.Code += FString::ChrN(QuoteLen, C);
				// f-string fields are code: keep them visible to the name rules.
				S.Code += bFormat ? Literal : FString::ChrN(FMath::Min(Literal.Len(), 1), TEXT(' '));
				S.Code += FString::ChrN(QuoteLen, C);
				i = FMath::Min(N, j + QuoteLen);
				continue;
			}
			S.Code.AppendChar(C);
			++i;
		}
		return S;
	}

	struct FTables
	{
		TMap<FString, ERule> Modules;
		TMap<FString, ERule> Names;
		TMap<FString, ERule> Methods;

		FTables()
		{
			auto Add = [](TMap<FString, ERule>& Map, ERule R, std::initializer_list<const TCHAR*> List)
			{
				for (const TCHAR* Item : List) Map.Add(Item, R);
			};
			Add(Modules, ERule::Process, { TEXT("subprocess"), TEXT("_posixsubprocess"), TEXT("pty"), TEXT("multiprocessing"), TEXT("_multiprocessing"), TEXT("webbrowser"), TEXT("posix"),
				TEXT("nt"), TEXT("_winapi"), TEXT("msvcrt"), TEXT("winreg"), TEXT("_winreg"), TEXT("pexpect"), TEXT("sh"), TEXT("plumbum") });
			Add(Modules, ERule::Network, { TEXT("socket"), TEXT("_socket"), TEXT("ssl"), TEXT("_ssl"), TEXT("urllib"), TEXT("urllib2"), TEXT("urllib3"), TEXT("http"), TEXT("httplib"),
				TEXT("requests"), TEXT("httpx"), TEXT("aiohttp"), TEXT("ftplib"), TEXT("smtplib"), TEXT("poplib"), TEXT("imaplib"), TEXT("nntplib"), TEXT("telnetlib"), TEXT("xmlrpc"),
				TEXT("socketserver"), TEXT("asyncio"), TEXT("websocket"), TEXT("websockets"), TEXT("paramiko"), TEXT("selectors") });
			Add(Modules, ERule::Native, { TEXT("ctypes"), TEXT("_ctypes"), TEXT("cffi"), TEXT("_cffi_backend") });
			Add(Modules, ERule::Dynamic, { TEXT("importlib"), TEXT("imp"), TEXT("runpy"), TEXT("code"), TEXT("codeop"), TEXT("pkgutil"), TEXT("zipimport"), TEXT("marshal"),
				TEXT("builtins"), TEXT("__builtin__") });
			Add(Modules, ERule::KeyStore, { TEXT("keyring"), TEXT("_keyring"), TEXT("win32cred"), TEXT("secretstorage") });

			Add(Names, ERule::Process, { TEXT("os.system"), TEXT("os.popen"), TEXT("os.fork"), TEXT("os.forkpty"), TEXT("os.posix_spawn"), TEXT("os.posix_spawnp"), TEXT("os.startfile"),
				TEXT("os.kill"), TEXT("os.killpg"), TEXT("os.execl"), TEXT("os.execle"), TEXT("os.execlp"), TEXT("os.execlpe"), TEXT("os.execv"), TEXT("os.execve"), TEXT("os.execvp"),
				TEXT("os.execvpe"), TEXT("os.spawnl"), TEXT("os.spawnle"), TEXT("os.spawnlp"), TEXT("os.spawnlpe"), TEXT("os.spawnv"), TEXT("os.spawnve"), TEXT("os.spawnvp"),
				TEXT("os.spawnvpe"), TEXT("platform.popen") });
			Add(Names, ERule::Delete, { TEXT("os.remove"), TEXT("os.unlink"), TEXT("os.rmdir"), TEXT("os.removedirs"), TEXT("os.rename"), TEXT("os.renames"), TEXT("os.replace"),
				TEXT("os.truncate"), TEXT("shutil.rmtree"), TEXT("shutil.move") });
			Add(Names, ERule::Editor, { TEXT("sys.exit"), TEXT("os._exit"), TEXT("os.abort") });
			Add(Names, ERule::Dynamic, { TEXT("sys.modules"), TEXT("sys._getframe"), TEXT("sys.settrace"), TEXT("sys.setprofile"), TEXT("gc.get_objects"), TEXT("gc.get_referrers"),
				TEXT("inspect.currentframe"), TEXT("inspect.stack"), TEXT("types.FunctionType"), TEXT("types.CodeType") });

			Add(Methods, ERule::Process, { TEXT("system"), TEXT("popen"), TEXT("Popen"), TEXT("check_output"), TEXT("check_call"), TEXT("getoutput"), TEXT("getstatusoutput"),
				TEXT("spawnl"), TEXT("spawnv"), TEXT("execv"), TEXT("execl"), TEXT("startfile"), TEXT("posix_spawn") });
			Add(Methods, ERule::Network, { TEXT("urlopen"), TEXT("urlretrieve") });
			Add(Methods, ERule::Delete, { TEXT("rmtree"), TEXT("rmdir"), TEXT("removedirs"), TEXT("unlink") });
			Add(Methods, ERule::Editor, { TEXT("quit_editor"), TEXT("request_exit") });
			Add(Methods, ERule::Dynamic, { TEXT("execute_python_command"), TEXT("execute_python_command_ex"), TEXT("exec_module"), TEXT("import_module") });
		}
	};

	const FTables& Tables()
	{
		static const FTables T;
		return T;
	}

	/** The rule for a module ("urllib.request" → network), or none. */
	TOptional<ERule> ModuleRule(const FString& Module)
	{
		TArray<FString> Parts;
		Module.ParseIntoArray(Parts, TEXT("."));
		FString Prefix;
		for (const FString& P : Parts)
		{
			Prefix = Prefix.IsEmpty() ? P : Prefix + TEXT(".") + P;
			if (const ERule* R = Tables().Modules.Find(Prefix)) return *R;
		}
		if (Module.Contains(TEXT("zetta"), ESearchCase::IgnoreCase)) return ERule::Plugin;
		return {};
	}

	TOptional<ERule> NameRule(const FString& Dotted)
	{
		if (const ERule* R = Tables().Names.Find(Dotted)) return *R;
		// os.exec*, os.spawn* families.
		if (Dotted.StartsWith(TEXT("os.exec")) || Dotted.StartsWith(TEXT("os.spawn")) || Dotted.StartsWith(TEXT("os.posix_spawn"))) return ERule::Process;
		return ModuleRule(Dotted);
	}

	TOptional<FRefusal> Check(const FString& Source)
	{
		const FScan S = Tokenize(Source);
		auto Refuse = [](ERule R, const FString& Found) { return TOptional<FRefusal>(FRefusal{ R, Found }); };

		// Imports, and the names they bring in under other names.
		TMap<FString, FString> Alias;
		{
			const FRegexPattern Import(TEXT("(?m)^[ \\t]*import[ \\t]+([^\\n;]+)"));
			FRegexMatcher M(Import, S.Code);
			while (M.FindNext())
			{
				TArray<FString> Items;
				M.GetCaptureGroup(1).ParseIntoArray(Items, TEXT(","));
				for (FString Item : Items)
				{
					Item = Item.Replace(TEXT("("), TEXT("")).Replace(TEXT(")"), TEXT("")).Replace(TEXT("\\"), TEXT("")).TrimStartAndEnd();
					FString Module = Item, As;
					if (Item.Split(TEXT(" as "), &Module, &As)) Alias.Add(As.TrimStartAndEnd(), Module.TrimStartAndEnd());
					Module = Module.TrimStartAndEnd();
					if (const TOptional<ERule> R = ModuleRule(Module)) return Refuse(R.GetValue(), TEXT("import ") + Module);
					FString First = Module;
					Module.Split(TEXT("."), &First, nullptr);
					if (!Alias.Contains(First)) Alias.Add(First, First);
				}
			}
			const FRegexPattern From(TEXT("(?m)^[ \\t]*from[ \\t]+([\\w.]+)[ \\t]+import[ \\t]+([^\\n;]+)"));
			FRegexMatcher F(From, S.Code);
			while (F.FindNext())
			{
				const FString Module = F.GetCaptureGroup(1);
				if (const TOptional<ERule> R = ModuleRule(Module)) return Refuse(R.GetValue(), TEXT("from ") + Module + TEXT(" import"));
				TArray<FString> Items;
				F.GetCaptureGroup(2).ParseIntoArray(Items, TEXT(","));
				for (FString Item : Items)
				{
					Item = Item.Replace(TEXT("("), TEXT("")).Replace(TEXT(")"), TEXT("")).Replace(TEXT("\\"), TEXT("")).TrimStartAndEnd();
					FString Name = Item, As;
					if (!Item.Split(TEXT(" as "), &Name, &As)) As = Item;
					Name = Name.TrimStartAndEnd();
					const FString Dotted = Module + TEXT(".") + Name;
					if (Name == TEXT("*")) continue;
					if (const TOptional<ERule> R = NameRule(Dotted)) return Refuse(R.GetValue(), TEXT("from ") + Module + TEXT(" import ") + Name);
					Alias.Add(As.TrimStartAndEnd(), Dotted);
				}
			}
		}

		// Dotted names, with import aliases followed: os.system, o.system (import os as o), shutil.rmtree.
		{
			const FRegexPattern Dotted(TEXT("(?<![\\w.])([A-Za-z_]\\w*)\\s*\\.\\s*([A-Za-z_]\\w*)(?:\\s*\\.\\s*([A-Za-z_]\\w*))?"));
			FRegexMatcher M(Dotted, S.Code);
			while (M.FindNext())
			{
				const FString Head = M.GetCaptureGroup(1);
				const FString* Real = Alias.Find(Head);
				const FString Base = Real ? *Real : Head;
				const FString Two = Base + TEXT(".") + M.GetCaptureGroup(2);
				if (const TOptional<ERule> R = NameRule(Two)) return Refuse(R.GetValue(), Two);
				if (!M.GetCaptureGroup(3).IsEmpty())
				{
					const FString Three = Two + TEXT(".") + M.GetCaptureGroup(3);
					if (const TOptional<ERule> R = NameRule(Three)) return Refuse(R.GetValue(), Three);
				}
			}
			// A name brought in bare: "from os import system as run" then run(...).
			for (const TPair<FString, FString>& Pair : Alias)
			{
				if (const TOptional<ERule> R = NameRule(Pair.Value))
				{
					const FRegexPattern Use(FString::Printf(TEXT("(?<![\\w.])%s\\b"), *Pair.Key));
					FRegexMatcher U(Use, S.Code);
					if (U.FindNext()) return Refuse(R.GetValue(), Pair.Value);
				}
			}
		}

		// Methods no editor API has, on any receiver.
		{
			const FRegexPattern Method(TEXT("(\\w*)\\s*\\.\\s*([A-Za-z_]\\w*)\\s*\\("));
			FRegexMatcher M(Method, S.Code);
			while (M.FindNext())
			{
				const FString Receiver = M.GetCaptureGroup(1);
				const FString Name = M.GetCaptureGroup(2);
				// platform.system() only names the operating system.
				if (Receiver == TEXT("platform") && Name == TEXT("system")) continue;
				if (const ERule* R = Tables().Methods.Find(Name)) return Refuse(*R, TEXT(".") + Name + TEXT("()"));
			}
		}

		// Builtins that run strings, and the doors from any object to globals and frames.
		{
			const FRegexPattern Builtin(TEXT("(?<![\\w.])(exec|eval|compile|__import__|globals|breakpoint|exit|quit)\\s*\\("));
			FRegexMatcher M(Builtin, S.Code);
			if (M.FindNext())
			{
				const FString Name = M.GetCaptureGroup(1);
				return Refuse(Name == TEXT("exit") || Name == TEXT("quit") ? ERule::Editor : ERule::Dynamic, Name + TEXT("()"));
			}
			const FRegexPattern Dunder(TEXT("\\b(__import__|__builtins__|__subclasses__|__globals__|__code__|__closure__|__getattribute__|__loader__|__spec__|__reduce__|__reduce_ex__|f_globals|f_locals|f_back|gi_frame|tb_frame)\\b"));
			FRegexMatcher D(Dunder, S.Code);
			if (D.FindNext()) return Refuse(ERule::Dynamic, D.GetCaptureGroup(1));
			// getattr on a module that holds dangerous names: the name could be built at run time.
			const FRegexPattern Reflect(TEXT("\\b(getattr|setattr|delattr|vars)\\s*\\(\\s*([A-Za-z_][\\w.]*)"));
			FRegexMatcher G(Reflect, S.Code);
			while (G.FindNext())
			{
				FString Target = G.GetCaptureGroup(2);
				if (const FString* Real = Alias.Find(Target)) Target = *Real;
				if (Target == TEXT("os") || Target == TEXT("sys") || Target == TEXT("shutil") || Target == TEXT("subprocess") || Target == TEXT("builtins") || Target == TEXT("gc")
					|| Target == TEXT("inspect") || Target == TEXT("unreal.SystemLibrary") || Target == TEXT("unreal.PythonScriptLibrary") || ModuleRule(Target).IsSet())
				{
					return Refuse(ERule::Dynamic, G.GetCaptureGroup(1) + TEXT("(") + Target + TEXT(", …)"));
				}
			}
		}

		// The editor itself.
		{
			const FRegexPattern Quit(TEXT("\\b(quit_editor|QUIT_EDITOR|request_exit|RequestExit)\\b"));
			FRegexMatcher M(Quit, S.Code);
			if (M.FindNext()) return Refuse(ERule::Editor, M.GetCaptureGroup(1));
			if (S.Code.Contains(TEXT("execute_console_command")))
			{
				for (const FString& L : S.Literals)
				{
					const FString C = L.TrimStartAndEnd().ToLower();
					if (C == TEXT("quit") || C == TEXT("exit") || C.StartsWith(TEXT("quit ")) || C.StartsWith(TEXT("exit "))) return Refuse(ERule::Editor, TEXT("console command \"") + L + TEXT("\""));
					if (C.StartsWith(TEXT("py ")) || C.StartsWith(TEXT("python "))) return Refuse(ERule::Dynamic, TEXT("console command \"") + L.Left(40) + TEXT("\""));
				}
			}
		}

		// This plugin: its classes (settings), its folder, its module names.
		{
			const FRegexPattern Plugin(TEXT("\\b(Zetta\\w*Settings|Zetta3DEditor|Zetta3DAgent|ZettaAgent\\w*|ZettaSecureStore)\\b"));
			FRegexMatcher M(Plugin, S.Code);
			if (M.FindNext()) return Refuse(ERule::Plugin, M.GetCaptureGroup(1));
			const FString PluginDir = ZettaTool::PluginDir().ToLower();
			for (const FString& L : S.Literals)
			{
				const FString P = L.Replace(TEXT("\\\\"), TEXT("/")).Replace(TEXT("\\"), TEXT("/")).ToLower();
				if ((!PluginDir.IsEmpty() && P.Contains(PluginDir.LeftChop(1))) || P.Contains(TEXT("plugins/zetta3d")) || P.Contains(TEXT("zetta3d.uplugin"))
					|| P.Contains(TEXT("/script/zetta3d")) || P.Contains(TEXT("saved/zetta3d")))
				{
					return Refuse(ERule::Plugin, TEXT("\"") + L.Left(120) + TEXT("\""));
				}
				if (P.Contains(TEXT("keychain")) || P.Contains(TEXT("credential manager")) || P.Contains(TEXT("/library/keychains")))
				{
					return Refuse(ERule::KeyStore, TEXT("\"") + L.Left(120) + TEXT("\""));
				}
			}
		}

		// Writing an .ini outside the project (engine or user-wide settings).
		{
			bool bWrites = S.Code.Contains(TEXT("write_text")) || S.Code.Contains(TEXT("write_bytes")) || S.Code.Contains(TEXT("shutil.copy")) || S.Code.Contains(TEXT("copyfile"))
				|| S.Code.Contains(TEXT("configparser"));
			if (!bWrites && S.Code.Contains(TEXT("open")))
			{
				for (const FString& L : S.Literals)
				{
					const FString Mode = L.TrimStartAndEnd();
					if (Mode.Len() > 0 && Mode.Len() <= 4 && (Mode.Contains(TEXT("w")) || Mode.Contains(TEXT("a")) || Mode.Contains(TEXT("x")) || Mode.Contains(TEXT("+")))
						&& !Mode.Contains(TEXT(" ")) && Mode.Replace(TEXT("w"), TEXT("")).Replace(TEXT("a"), TEXT("")).Replace(TEXT("x"), TEXT("")).Replace(TEXT("+"), TEXT("")).Replace(TEXT("b"), TEXT("")).Replace(TEXT("t"), TEXT("")).Replace(TEXT("r"), TEXT("")).IsEmpty())
					{
						bWrites = true;
					}
				}
			}
			if (bWrites)
			{
				const FString Project = ZettaTool::ProjectDir().ToLower();
				for (const FString& L : S.Literals)
				{
					const FString P = L.Replace(TEXT("\\\\"), TEXT("/")).Replace(TEXT("\\"), TEXT("/")).ToLower();
					if (!P.Contains(TEXT(".ini"))) continue;
					const bool bAbsolute = P.StartsWith(TEXT("/")) || (P.Len() > 2 && P[1] == TEXT(':')) || P.StartsWith(TEXT("~"));
					if ((bAbsolute && !P.StartsWith(Project)) || P.Contains(TEXT("engine/config")) || P.Contains(TEXT("../")))
					{
						return Refuse(ERule::Config, TEXT("\"") + L.Left(120) + TEXT("\""));
					}
				}
			}
		}
		return {};
	}

	FString RefusalText(const FRefusal& R)
	{
		const FRule& Rule = RuleText(R.Rule);
		return FString::Printf(TEXT("Refused by the plugin's safety rules: %s (%s). %s"), Rule.What, *R.Found, Rule.Instead);
	}

	// ─── Before and after ────────────────────────────────────────────────────

	struct FState
	{
		TMap<FString, TPair<FString, FTransform>> Actors;
		TSet<FName> Assets;
		TSet<FString> Dirty;
	};

	FState Capture()
	{
		FState S;
		for (AActor* A : ZettaTargets::LevelActors())
		{
			S.Actors.Add(A->GetPathName(), TPair<FString, FTransform>(A->GetActorLabel(), A->GetActorTransform()));
		}
		FARFilter Filter;
		Filter.PackagePaths.Add(TEXT("/Game"));
		Filter.bRecursivePaths = true;
		TArray<FAssetData> Assets;
		FAssetRegistryModule::GetRegistry().GetAssets(Filter, Assets);
		for (const FAssetData& A : Assets) S.Assets.Add(A.PackageName);
		for (TObjectIterator<UPackage> It; It; ++It)
		{
			if (It->IsDirty() && !It->HasAnyFlags(RF_Transient) && It->GetName().StartsWith(TEXT("/Game/"))) S.Dirty.Add(It->GetName());
		}
		return S;
	}

	FString Names(const TArray<FString>& Items, int32 Max = 8)
	{
		TArray<FString> Shown;
		for (int32 i = 0; i < Items.Num() && i < Max; ++i) Shown.Add(Items[i]);
		FString S = FString::Join(Shown, TEXT(", "));
		if (Items.Num() > Max) S += FString::Printf(TEXT(", +%d"), Items.Num() - Max);
		return S;
	}

	FString Changed(const FState& Before, const FState& After, const TSet<FString>& Saved)
	{
		TArray<FString> Added, Removed, Moved, Renamed;
		for (const auto& Pair : After.Actors)
		{
			const TPair<FString, FTransform>* Old = Before.Actors.Find(Pair.Key);
			if (!Old) Added.Add(Pair.Value.Key);
			else
			{
				if (!Old->Value.Equals(Pair.Value.Value, 0.01)) Moved.Add(Pair.Value.Key);
				if (Old->Key != Pair.Value.Key) Renamed.Add(FString::Printf(TEXT("%s → %s"), *Old->Key, *Pair.Value.Key));
			}
		}
		for (const auto& Pair : Before.Actors)
		{
			if (!After.Actors.Contains(Pair.Key)) Removed.Add(Pair.Value.Key);
		}
		TArray<FString> NewAssets, GoneAssets, Modified;
		for (const FName& A : After.Assets)
		{
			if (!Before.Assets.Contains(A)) NewAssets.Add(A.ToString());
		}
		for (const FName& A : Before.Assets)
		{
			if (!After.Assets.Contains(A)) GoneAssets.Add(A.ToString());
		}
		for (const FString& D : After.Dirty)
		{
			// One-file-per-actor packages are the level's actors, already counted above.
			if (D.Contains(TEXT("__External"))) continue;
			if (!Before.Dirty.Contains(D) && !NewAssets.Contains(D)) Modified.Add(D);
		}
		TArray<FString> SavedList = Saved.Array();
		SavedList.Sort();
		NewAssets.Sort();
		TArray<FString> Parts;
		auto Part = [&Parts](const TArray<FString>& Items, const TCHAR* One, const TCHAR* Many)
		{
			if (Items.Num() > 0) Parts.Add(FString::Printf(TEXT("%s (%s)"), *ZettaTool::Plural(Items.Num(), One, Many), *Names(Items)));
		};
		Part(Added, TEXT("actor added"), TEXT("actors added"));
		Part(Removed, TEXT("actor removed"), TEXT("actors removed"));
		Part(Moved, TEXT("actor moved"), TEXT("actors moved"));
		Part(Renamed, TEXT("actor renamed"), TEXT("actors renamed"));
		Part(NewAssets, TEXT("asset created"), TEXT("assets created"));
		Part(GoneAssets, TEXT("asset deleted"), TEXT("assets deleted"));
		Part(SavedList, TEXT("asset saved"), TEXT("assets saved"));
		Part(Modified, TEXT("asset changed (unsaved)"), TEXT("assets changed (unsaved)"));
		return Parts.Num() > 0 ? TEXT("Changed: ") + FString::Join(Parts, TEXT(", ")) : FString(TEXT("Changed: nothing in the level's actors or the project's assets."));
	}

	// ─── run_python ──────────────────────────────────────────────────────────

	/** The wrapper: runs the agent's code in a fresh namespace, keeps what it printed and its last expression's value. */
	FString Wrapper(const FString& Code, const FString& ResultFile)
	{
		FTCHARToUTF8 Utf8(*Code);
		const FString Encoded = FBase64::Encode((const uint8*)Utf8.Get(), Utf8.Length());
		FString File = ResultFile.Replace(TEXT("\\"), TEXT("/"));
		return FString::Printf(TEXT(
			"import ast as _z_ast, base64 as _z_b64, io as _z_io, json as _z_json, sys as _z_sys, traceback as _z_tb\n"
			"import unreal\n"
			"_z_src = _z_b64.b64decode('%s').decode('utf-8')\n"
			"_z_res = {'ok': True, 'value': None, 'error': None, 'output': ''}\n"
			"_z_buf = _z_io.StringIO()\n"
			"_z_old = (_z_sys.stdout, _z_sys.stderr)\n"
			"_z_sys.stdout = _z_buf\n"
			"_z_sys.stderr = _z_buf\n"
			"try:\n"
			"    _z_tree = _z_ast.parse(_z_src, '<agent>', 'exec')\n"
			"    _z_last = None\n"
			"    if _z_tree.body and isinstance(_z_tree.body[-1], _z_ast.Expr):\n"
			"        _z_last = _z_ast.Expression(_z_tree.body.pop().value)\n"
			"    _z_ns = {'__name__': '__zetta_agent__', 'unreal': unreal}\n"
			"    exec(compile(_z_tree, '<agent>', 'exec'), _z_ns)\n"
			"    if _z_last is not None:\n"
			"        _z_val = eval(compile(_z_last, '<agent>', 'eval'), _z_ns)\n"
			"        if _z_val is not None:\n"
			"            _z_res['value'] = repr(_z_val)[:20000]\n"
			"except BaseException as _z_e:\n"
			"    _z_res['ok'] = False\n"
			"    _z_tbk = _z_e.__traceback__.tb_next if _z_e.__traceback__ is not None else None\n"
			"    _z_res['error'] = ''.join(_z_tb.format_exception(type(_z_e), _z_e, _z_tbk))[-20000:]\n"
			"finally:\n"
			"    _z_sys.stdout, _z_sys.stderr = _z_old\n"
			"_z_res['output'] = _z_buf.getvalue()[-40000:]\n"
			"with open('%s', 'w', encoding='utf-8') as _z_f:\n"
			"    _z_json.dump(_z_res, _z_f)\n"), *Encoded, *File);
	}

	FZettaToolResult RunPython(const TSharedPtr<FJsonObject>& In)
	{
		const FString Code = ZettaTool::Str(In, TEXT("code"));
		if (Code.TrimStartAndEnd().IsEmpty()) return Fail(TEXT("'code' is required."));
		if (Code.Len() > MaxCode) return Fail(FString::Printf(TEXT("'code' is %d characters; at most %d. Put longer scripts in Content/Python with write_file and import them."), Code.Len(), MaxCode));
		if (const TOptional<FRefusal> Refusal = Check(Code)) return Fail(RefusalText(Refusal.GetValue()));

		IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
		if (!Python || !Python->IsPythonAvailable())
		{
			return Fail(TEXT("Python is not available in this editor. Ask the user to enable the Python Editor Script Plugin (Edit › Plugins) and restart the editor."));
		}
		if (!Python->IsPythonInitialized() && !Python->ForceEnablePythonAtRuntime())
		{
			return Fail(TEXT("The editor's Python could not be started. Ask the user to check the Python Editor Script Plugin (Edit › Plugins)."));
		}

		const FString ResultFile = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Zetta3D"), TEXT("Agent"), TEXT("python_result.json")));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(ResultFile), true);
		IFileManager::Get().Delete(*ResultFile, false, true, true);

		const FState Before = Capture();
		TSet<FString> Saved;
		const FDelegateHandle SaveHandle = UPackage::PackageSavedWithContextEvent.AddLambda([&Saved](const FString&, UPackage* Package, FObjectPostSaveContext)
		{
			if (Package) Saved.Add(Package->GetName());
		});

		const FString Purpose = ZettaTool::Short(ZettaTool::Str(In, TEXT("purpose"), TEXT("Run Python")), 80);
		const FString Title = ZettaTool::TransactionTitle(Purpose.IsEmpty() ? TEXT("Run Python") : Purpose);
		{
			FScopedTransaction Transaction(FText::FromString(Title));
			FPythonCommandEx Command;
			Command.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
			Command.FileExecutionScope = EPythonFileExecutionScope::Private;
			Command.Command = Wrapper(Code, ResultFile);
			Python->ExecPythonCommandEx(Command);
		}
		UPackage::PackageSavedWithContextEvent.Remove(SaveHandle);
		const FState After = Capture();
		ZettaAgentChanges::LevelTransaction(Title);

		FString Json;
		TSharedPtr<FJsonObject> Result;
		if (FFileHelper::LoadFileToString(Json, *ResultFile))
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
			FJsonSerializer::Deserialize(Reader, Result);
		}
		const FString Change = Changed(Before, After, Saved);
		if (!Result.IsValid())
		{
			return Fail(TEXT("The Python run did not finish normally (the editor's Python reported an error before the code ran). ") + Change);
		}
		const bool bOk = ZettaTool::BoolOr(Result, TEXT("ok"), true);
		FString Text;
		const FString Output = ZettaTool::Str(Result, TEXT("output")).TrimEnd();
		if (!Output.IsEmpty()) Text += TEXT("Output:\n") + Output + TEXT("\n");
		const FString Value = ZettaTool::Str(Result, TEXT("value"));
		if (!Value.IsEmpty()) Text += TEXT("Value: ") + Value + TEXT("\n");
		if (!bOk) Text += TEXT("Error:\n") + ZettaTool::Str(Result, TEXT("error")).Right(20000) + TEXT("\n");
		if (Output.IsEmpty() && Value.IsEmpty() && bOk) Text += TEXT("Ran without output.\n");
		Text += Change;
		if (!bOk) Text += TEXT("\nWhat ran before the error stays done; undo takes back the whole run.");
		FZettaToolResult R = ZettaTool::Ok(Text, bOk ? TEXT("Ran Python") : TEXT("Python error"));
		R.bIsError = !bOk;
		return R;
	}

	FZettaToolPreview DescribePython(const TSharedPtr<FJsonObject>& In)
	{
		const FString Code = ZettaTool::Str(In, TEXT("code"));
		FZettaToolPreview P = ZettaTool::Preview(TEXT("Run Python"), ZettaTool::Str(In, TEXT("purpose")));
		P.Code = Code.Left(MaxCode);
		if (Code.Len() > MaxCode) P.Error = FString::Printf(TEXT("The code is longer than %d characters."), MaxCode);
		else if (const TOptional<FRefusal> Refusal = Check(Code)) P.Error = RefusalText(Refusal.GetValue());
		return P;
	}
}

void ZettaToolAreas::RegisterPythonTools(FZettaAgentTools& Tools)
{
	Tools.Register(TEXT("run_python"), &ZettaPythonImpl::RunPython, &ZettaPythonImpl::DescribePython);
}
