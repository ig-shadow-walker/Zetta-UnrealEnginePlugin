// Copyright Zetta 3D. See LICENSE.md.
// Project files and the log: read_file, search_files, write_file, edit_file, output_log.

#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ZettaAgentTools.h"
#include "ZettaCompile.h"
#include "ZettaLogBuffer.h"
#include "ZettaToolCommon.h"

namespace ZettaFileImpl
{
	using ZettaTool::Fail;
	using ZettaTool::Ok;

	constexpr int32 MaxReadLines = 2000;
	constexpr int32 MaxWriteChars = 200000;
	constexpr int64 MaxSearchBytes = 2 * 1024 * 1024;

	bool IsBinaryExtension(const FString& Path)
	{
		static const TCHAR* Binary[] = { TEXT("uasset"), TEXT("umap"), TEXT("ubulk"), TEXT("uexp"), TEXT("png"), TEXT("jpg"), TEXT("jpeg"), TEXT("tga"), TEXT("exr"), TEXT("hdr"), TEXT("psd"),
			TEXT("fbx"), TEXT("glb"), TEXT("gltf"), TEXT("obj"), TEXT("wav"), TEXT("mp3"), TEXT("ogg"), TEXT("dll"), TEXT("exe"), TEXT("dylib"), TEXT("so"), TEXT("lib"), TEXT("a"), TEXT("pdb"),
			TEXT("zip"), TEXT("pak"), TEXT("utoc"), TEXT("ucas"), TEXT("bin"), TEXT("ttf"), TEXT("otf") };
		const FString Ext = FPaths::GetExtension(Path).ToLower();
		for (const TCHAR* B : Binary)
		{
			if (Ext == B) return true;
		}
		return false;
	}

	bool ReadText(const FString& Absolute, FString& Out, FString& OutError)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Absolute))
		{
			OutError = TEXT("could not be read");
			return false;
		}
		const int32 Probe = FMath::Min(Bytes.Num(), 8192);
		for (int32 i = 0; i < Probe; ++i)
		{
			// UTF-16 files start with a byte-order mark; a zero byte anywhere else means binary.
			if (Bytes[i] == 0 && !(Bytes.Num() >= 2 && ((Bytes[0] == 0xFF && Bytes[1] == 0xFE) || (Bytes[0] == 0xFE && Bytes[1] == 0xFF))))
			{
				OutError = TEXT("is a binary file");
				return false;
			}
		}
		FFileHelper::BufferToString(Out, Bytes.GetData(), Bytes.Num());
		return true;
	}

	TArray<FString> SplitLines(const FString& Text)
	{
		TArray<FString> Lines;
		Text.Replace(TEXT("\r\n"), TEXT("\n")).ParseIntoArray(Lines, TEXT("\n"), false);
		if (Lines.Num() > 0 && Lines.Last().IsEmpty() && Text.EndsWith(TEXT("\n"))) Lines.Pop();
		return Lines;
	}

	// ─── read_file ───────────────────────────────────────────────────────────

	FZettaToolResult ReadFile(const TSharedPtr<FJsonObject>& In)
	{
		FString Absolute, Relative, Error;
		const FString Path = ZettaTool::Str(In, TEXT("path"));
		if (Path.StartsWith(TEXT("attached:")))
		{
			Absolute = ZettaTool::AttachedFile(Path);
			Relative = Path;
			if (Absolute.IsEmpty()) return Fail(FString::Printf(TEXT("'%s' is no longer available. Ask the user to attach it again."), *Path));
		}
		else if (!ZettaTool::ProjectFile(Path, false, Absolute, Relative, Error))
		{
			return Fail(Error);
		}
		if (IFileManager::Get().DirectoryExists(*Absolute)) return Fail(FString::Printf(TEXT("%s is a folder. Use search_files with a glob (\"%s/**/*\") to list what is in it."), *Relative, *Relative));
		if (!IFileManager::Get().FileExists(*Absolute)) return Fail(FString::Printf(TEXT("%s does not exist."), *Relative));
		if (IsBinaryExtension(Absolute)) return Fail(FString::Printf(TEXT("%s is not a text file. Assets are read with inspect_asset."), *Relative));
		FString Text;
		if (!ReadText(Absolute, Text, Error)) return Fail(FString::Printf(TEXT("%s %s."), *Relative, *Error));
		const TArray<FString> Lines = SplitLines(Text);
		const int32 Total = Lines.Num();
		int32 Start = FMath::Max(1, (int32)ZettaTool::Int(In, TEXT("startLine")).Get(1));
		int32 End = (int32)ZettaTool::Int(In, TEXT("endLine")).Get(Start + MaxReadLines - 1);
		End = FMath::Min3(End, Total, Start + MaxReadLines - 1);
		if (Total == 0) return Ok(FString::Printf(TEXT("%s is empty."), *Relative));
		if (Start > Total) return Fail(FString::Printf(TEXT("%s has %d lines; startLine %d is past the end."), *Relative, Total, Start));
		FString Out = FString::Printf(TEXT("%s — lines %d–%d of %d\n"), *Relative, Start, End, Total);
		for (int32 i = Start; i <= End; ++i)
		{
			FString Line = Lines[i - 1];
			if (Line.Len() > 2000) Line = Line.Left(2000) + TEXT(" …(line cut)");
			Out += FString::Printf(TEXT("%5d| %s\n"), i, *Line);
		}
		if (End < Total) Out += FString::Printf(TEXT("… %d more line(s): read on with startLine %d."), Total - End, End + 1);
		return Ok(Out);
	}

	// ─── search_files ────────────────────────────────────────────────────────

	// A glob as a regular expression over project-relative paths: "**" crosses folders, "*" and "?" stay inside one.
	FString GlobToRegex(const FString& Glob)
	{
		FString R = TEXT("^");
		for (int32 i = 0; i < Glob.Len(); ++i)
		{
			const TCHAR C = Glob[i];
			if (C == TEXT('*'))
			{
				if (i + 1 < Glob.Len() && Glob[i + 1] == TEXT('*'))
				{
					const bool bSlash = i + 2 < Glob.Len() && Glob[i + 2] == TEXT('/');
					R += bSlash ? TEXT("(?:.*/)?") : TEXT(".*");
					i += bSlash ? 2 : 1;
				}
				else
				{
					R += TEXT("[^/]*");
				}
			}
			else if (C == TEXT('?'))
			{
				R += TEXT("[^/]");
			}
			else if (FCString::Strchr(TEXT("\\.+()[]{}^$|"), C))
			{
				R += TEXT("\\");
				R.AppendChar(C);
			}
			else
			{
				R.AppendChar(C);
			}
		}
		return R + TEXT("$");
	}

	bool SkippedFolder(const FString& Relative)
	{
		static const TCHAR* Skip[] = { TEXT("Binaries/"), TEXT("Intermediate/"), TEXT("Saved/"), TEXT("DerivedDataCache/"), TEXT(".git/"), TEXT(".vs/"), TEXT(".idea/"), TEXT("node_modules/") };
		for (const TCHAR* S : Skip)
		{
			if (Relative.StartsWith(S) || Relative.Contains(FString(TEXT("/")) + S)) return true;
		}
		// Content holds assets; only its Python folder is text.
		return Relative.StartsWith(TEXT("Content/")) && !Relative.StartsWith(TEXT("Content/Python/"));
	}

	FZettaToolResult SearchFiles(const TSharedPtr<FJsonObject>& In)
	{
		const FString Pattern = ZettaTool::Str(In, TEXT("pattern"));
		if (Pattern.IsEmpty()) return Fail(TEXT("'pattern' is required (a regular expression)."));
		const int32 Limit = ZettaTool::IntIn(In, TEXT("limit"), 50, 1, 200);
		FString Glob = ZettaTool::Str(In, TEXT("glob")).TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/"));
		Glob.RemoveFromStart(TEXT("./"));
		const FRegexPattern Regex(Pattern);

		// Where to walk: the glob's folder before its first wildcard, or the usual text folders.
		TArray<FString> Roots;
		const FString Root = ZettaTool::ProjectDir();
		if (!Glob.IsEmpty())
		{
			int32 Wild = Glob.Len();
			for (int32 i = 0; i < Glob.Len(); ++i)
			{
				if (Glob[i] == TEXT('*') || Glob[i] == TEXT('?'))
				{
					Wild = i;
					break;
				}
			}
			FString Prefix = Glob.Left(Wild);
			int32 Slash;
			Prefix = Prefix.FindLastChar(TEXT('/'), Slash) ? Prefix.Left(Slash) : FString();
			Roots.Add(Prefix);
		}
		else
		{
			Roots = { TEXT("Source"), TEXT("Config"), TEXT("Content/Python"), TEXT("Plugins") };
		}
		const FRegexPattern GlobRegex(GlobToRegex(Glob.IsEmpty() ? TEXT("**") : Glob));

		TArray<FString> Hits;
		int32 FilesSearched = 0;
		bool bMore = false;
		for (const FString& R : Roots)
		{
			const FString Dir = Root + R;
			if (!IFileManager::Get().DirectoryExists(*Dir)) continue;
			TArray<FString> Files;
			IFileManager::Get().FindFilesRecursive(Files, *Dir, TEXT("*"), true, false);
			Files.Sort();
			const FString Plugin = ZettaTool::PluginDir();
			for (const FString& File : Files)
			{
				const FString Relative = ZettaTool::ProjectRelative(File);
				if (SkippedFolder(Relative) || IsBinaryExtension(Relative)) continue;
				// Project plugins' sources only, never this plugin's.
				if (Relative.StartsWith(TEXT("Plugins/")) && (!Relative.Contains(TEXT("/Source/")) || (!Plugin.IsEmpty() && ZettaTool::IsUnder(File, Plugin)))) continue;
				FRegexMatcher GlobMatch(GlobRegex, Relative);
				if (!GlobMatch.FindNext()) continue;
				if (IFileManager::Get().FileSize(*File) > MaxSearchBytes) continue;
				FString Text, Error;
				if (!ReadText(File, Text, Error)) continue;
				++FilesSearched;
				const TArray<FString> Lines = SplitLines(Text);
				for (int32 i = 0; i < Lines.Num(); ++i)
				{
					FRegexMatcher M(Regex, Lines[i]);
					if (!M.FindNext()) continue;
					if (Hits.Num() >= Limit)
					{
						bMore = true;
						break;
					}
					Hits.Add(FString::Printf(TEXT("%s:%d: %s"), *Relative, i + 1, *ZettaTool::Short(Lines[i].TrimStartAndEnd(), 300)));
				}
				if (bMore) break;
			}
			if (bMore) break;
		}
		if (Hits.Num() == 0) return Ok(FString::Printf(TEXT("No matches in %d file(s)%s."), FilesSearched, Glob.IsEmpty() ? TEXT(" under Source/, Config/, Content/Python/ and project plugins") : *FString::Printf(TEXT(" matching %s"), *Glob)));
		return Ok(FString::Join(Hits, TEXT("\n")) + (bMore ? FString::Printf(TEXT("\n… more matches; showing the first %d. Narrow the pattern or glob."), Limit) : FString()));
	}

	// ─── write_file / edit_file ──────────────────────────────────────────────

	FString AfterWriteNote(const FString& Relative)
	{
		const FString Ext = FPaths::GetExtension(Relative).ToLower();
		if (Ext == TEXT("h") || Ext == TEXT("hpp") || Ext == TEXT("cpp") || Ext == TEXT("inl") || Ext == TEXT("c")) return TEXT(" It is not built until you call compile.");
		if (Relative.EndsWith(TEXT(".Build.cs")) || Relative.EndsWith(TEXT(".Target.cs"))) return TEXT(" Build rules are read by a full build: the user must rebuild from their IDE and restart the editor.");
		if (Ext == TEXT("ini")) return TEXT(" Most settings in it are read when the editor starts; some apply at once.");
		if (Ext == TEXT("py")) return TEXT(" Run it with run_python (e.g. import it; Content/Python is on the editor's Python path).");
		return FString();
	}

	bool Save(const FString& Absolute, const FString& Content, FString& OutError)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Absolute), true);
		if (!FFileHelper::SaveStringToFile(Content, *Absolute, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = TEXT("could not be written (read-only, or checked in under source control?)");
			return false;
		}
		return true;
	}

	FZettaToolResult WriteFile(const TSharedPtr<FJsonObject>& In)
	{
		FString Absolute, Relative, Error;
		if (!ZettaTool::ProjectFile(ZettaTool::Str(In, TEXT("path")), true, Absolute, Relative, Error)) return Fail(Error);
		if (!ZettaTool::Has(In, TEXT("content"))) return Fail(TEXT("'content' is required."));
		const FString Content = ZettaTool::Str(In, TEXT("content"));
		if (Content.Len() > MaxWriteChars) return Fail(FString::Printf(TEXT("'content' is %d characters; at most %d per call. Split the file, or write part and add the rest with edit_file."), Content.Len(), MaxWriteChars));
		const bool bExists = IFileManager::Get().FileExists(*Absolute);
		if (bExists && !ZettaTool::BoolOr(In, TEXT("overwrite"), false)) return Fail(FString::Printf(TEXT("%s already exists. Pass overwrite: true to replace it, or change it with edit_file."), *Relative));
		if (IFileManager::Get().DirectoryExists(*Absolute)) return Fail(FString::Printf(TEXT("%s is a folder."), *Relative));
		ZettaAgentChanges::WillWriteFile(Absolute);
		if (!Save(Absolute, Content, Error)) return Fail(FString::Printf(TEXT("%s %s."), *Relative, *Error));
		ZettaCompile::NoteWrite(Relative, !bExists, Content);
		const int32 Lines = SplitLines(Content).Num();
		return Ok(FString::Printf(TEXT("%s %s (%d lines).%s"), bExists ? TEXT("Replaced") : TEXT("Wrote"), *Relative, Lines, *AfterWriteNote(Relative)),
			FString::Printf(TEXT("%s %s"), bExists ? TEXT("Replaced") : TEXT("Wrote"), *FPaths::GetCleanFilename(Relative)));
	}

	FZettaToolResult EditFile(const TSharedPtr<FJsonObject>& In)
	{
		FString Absolute, Relative, Error;
		if (!ZettaTool::ProjectFile(ZettaTool::Str(In, TEXT("path")), true, Absolute, Relative, Error)) return Fail(Error);
		if (!IFileManager::Get().FileExists(*Absolute)) return Fail(FString::Printf(TEXT("%s does not exist. Create it with write_file."), *Relative));
		const TArray<TSharedPtr<FJsonValue>> Edits = ZettaTool::Arr(In, TEXT("edits"));
		if (Edits.Num() == 0 || Edits.Num() > 30) return Fail(TEXT("'edits' takes 1 to 30 edits: { find, replace, all? }."));
		FString Text;
		if (!ReadText(Absolute, Text, Error)) return Fail(FString::Printf(TEXT("%s %s."), *Relative, *Error));
		// Line endings: match against \n, write back what the file used.
		const bool bCrLf = Text.Contains(TEXT("\r\n"));
		Text.ReplaceInline(TEXT("\r\n"), TEXT("\n"));

		int32 Replacements = 0;
		for (int32 i = 0; i < Edits.Num(); ++i)
		{
			const TSharedPtr<FJsonObject> E = Edits[i].IsValid() && Edits[i]->Type == EJson::Object ? Edits[i]->AsObject() : nullptr;
			if (!E.IsValid() || !ZettaTool::Has(E, TEXT("find"))) return Fail(FString::Printf(TEXT("Edit %d needs 'find' and 'replace'. Nothing was changed."), i + 1));
			const FString Find = ZettaTool::Str(E, TEXT("find")).Replace(TEXT("\r\n"), TEXT("\n"));
			const FString Replace = ZettaTool::Str(E, TEXT("replace")).Replace(TEXT("\r\n"), TEXT("\n"));
			if (Find.IsEmpty()) return Fail(FString::Printf(TEXT("Edit %d: 'find' is empty. Nothing was changed."), i + 1));
			int32 Count = 0;
			for (int32 At = Text.Find(Find, ESearchCase::CaseSensitive); At != INDEX_NONE; At = Text.Find(Find, ESearchCase::CaseSensitive, ESearchDir::FromStart, At + Find.Len())) ++Count;
			if (Count == 0) return Fail(FString::Printf(TEXT("Edit %d: the 'find' text is not in %s (after the earlier edits). Read the file and copy the text exactly. Nothing was changed."), i + 1, *Relative));
			const bool bAll = ZettaTool::BoolOr(E, TEXT("all"), false);
			if (Count > 1 && !bAll) return Fail(FString::Printf(TEXT("Edit %d: the 'find' text appears %d times in %s. Add surrounding lines to make it unique, or set all: true. Nothing was changed."), i + 1, Count, *Relative));
			if (bAll)
			{
				Text.ReplaceInline(*Find, *Replace, ESearchCase::CaseSensitive);
			}
			else
			{
				const int32 At = Text.Find(Find, ESearchCase::CaseSensitive);
				Text = Text.Left(At) + Replace + Text.RightChop(At + Find.Len());
			}
			Replacements += Count;
		}
		if (Text.Len() > MaxWriteChars * 5) return Fail(TEXT("The edited file would be too large. Nothing was changed."));
		if (bCrLf) Text.ReplaceInline(TEXT("\n"), TEXT("\r\n"));
		ZettaAgentChanges::WillWriteFile(Absolute);
		if (!Save(Absolute, Text, Error)) return Fail(FString::Printf(TEXT("%s %s."), *Relative, *Error));
		ZettaCompile::NoteWrite(Relative, false, Text);
		return Ok(FString::Printf(TEXT("Edited %s: %s, %s.%s"), *Relative, *ZettaTool::Plural(Edits.Num(), TEXT("edit"), TEXT("edits")),
			*ZettaTool::Plural(Replacements, TEXT("replacement"), TEXT("replacements")), *AfterWriteNote(Relative)), FString::Printf(TEXT("Edited %s"), *FPaths::GetCleanFilename(Relative)));
	}

	// ─── output_log ──────────────────────────────────────────────────────────

	FZettaToolResult OutputLog(const TSharedPtr<FJsonObject>& In)
	{
		TArray<FString> Kinds = ZettaTool::Strings(In, TEXT("verbosity"));
		bool bErrors = Kinds.Num() == 0, bWarnings = Kinds.Num() == 0, bLogs = Kinds.Num() == 0;
		for (const FString& K : Kinds)
		{
			const FString L = K.ToLower();
			if (L.StartsWith(TEXT("err"))) bErrors = true;
			else if (L.StartsWith(TEXT("warn"))) bWarnings = true;
			else if (L == TEXT("log") || L == TEXT("display") || L == TEXT("info")) bLogs = true;
		}
		const FString Category = ZettaTool::Str(In, TEXT("category")).TrimStartAndEnd();
		const int32 Limit = ZettaTool::IntIn(In, TEXT("limit"), 50, 1, 200);
		const TArray<FZettaLogEntry> Entries = ZettaLogBuffer::Recent(bErrors, bWarnings, bLogs, Category, Limit);
		if (Entries.Num() == 0) return Ok(TEXT("Nothing in the Output Log matches."));
		TArray<FString> Lines;
		for (const FZettaLogEntry& E : Entries)
		{
			const TCHAR* Level = E.Verbosity <= ELogVerbosity::Error ? TEXT("Error") : E.Verbosity == ELogVerbosity::Warning ? TEXT("Warning") : TEXT("Log");
			Lines.Add(FString::Printf(TEXT("[%s] %s: %s"), Level, *E.Category.ToString(), *ZettaTool::Short(E.Message, 1500)));
		}
		return Ok(FString::Printf(TEXT("%d line(s), most recent first:\n%s"), Entries.Num(), *FString::Join(Lines, TEXT("\n"))));
	}
}

void ZettaToolAreas::RegisterFileTools(FZettaAgentTools& Tools)
{
	using namespace ZettaFileImpl;
	using ZettaTool::Preview;
	Tools.Register(TEXT("read_file"), &ReadFile, [](const TSharedPtr<FJsonObject>& In) { return Preview(FString::Printf(TEXT("Read %s"), *ZettaTool::Str(In, TEXT("path")))); });
	Tools.Register(TEXT("search_files"), &SearchFiles, [](const TSharedPtr<FJsonObject>& In) { return Preview(TEXT("Search the project's files"), ZettaTool::Str(In, TEXT("pattern"))); });
	Tools.Register(TEXT("write_file"), &WriteFile, [](const TSharedPtr<FJsonObject>& In)
	{
		FZettaToolPreview P = Preview(FString::Printf(TEXT("Write %s"), *ZettaTool::Str(In, TEXT("path"))));
		P.Code = ZettaTool::Str(In, TEXT("content")).Left(20000);
		FString Absolute, Relative, Error;
		if (!ZettaTool::ProjectFile(ZettaTool::Str(In, TEXT("path")), true, Absolute, Relative, Error)) P.Error = Error;
		else if (IFileManager::Get().FileExists(*Absolute)) P.Body = ZettaTool::BoolOr(In, TEXT("overwrite"), false) ? TEXT("Replaces the existing file.") : TEXT("The file exists; without overwrite this call will fail.");
		return P;
	});
	Tools.Register(TEXT("edit_file"), &EditFile, [](const TSharedPtr<FJsonObject>& In)
	{
		FZettaToolPreview P = Preview(FString::Printf(TEXT("Edit %s"), *ZettaTool::Str(In, TEXT("path"))));
		FString Diff;
		for (const TSharedPtr<FJsonValue>& E : ZettaTool::Arr(In, TEXT("edits")))
		{
			if (!E.IsValid() || E->Type != EJson::Object) continue;
			Diff += TEXT("- ") + ZettaTool::Str(E->AsObject(), TEXT("find")).Replace(TEXT("\n"), TEXT("\n- ")) + TEXT("\n");
			Diff += TEXT("+ ") + ZettaTool::Str(E->AsObject(), TEXT("replace")).Replace(TEXT("\n"), TEXT("\n+ ")) + TEXT("\n\n");
		}
		P.Code = Diff.Left(20000);
		FString Absolute, Relative, Error;
		if (!ZettaTool::ProjectFile(ZettaTool::Str(In, TEXT("path")), true, Absolute, Relative, Error)) P.Error = Error;
		return P;
	});
	Tools.Register(TEXT("output_log"), &OutputLog, [](const TSharedPtr<FJsonObject>&) { return Preview(TEXT("Read the Output Log")); });
}
