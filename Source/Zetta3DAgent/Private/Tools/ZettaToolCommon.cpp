// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaToolCommon.h"

#include "Editor.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace ZettaTool
{
	// ─── Input ───────────────────────────────────────────────────────────────

	TSharedPtr<FJsonValue> Get(const TSharedPtr<FJsonObject>& In, const FString& Key)
	{
		if (!In.IsValid()) return nullptr;
		const TSharedPtr<FJsonValue> V = In->TryGetField(Key);
		if (!V.IsValid() || V->IsNull()) return nullptr;
		return V;
	}

	bool Has(const TSharedPtr<FJsonObject>& In, const FString& Key) { return Get(In, Key).IsValid(); }

	FString ValueText(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid() || V->IsNull()) return FString();
		switch (V->Type)
		{
		case EJson::String: return V->AsString();
		case EJson::Boolean: return V->AsBool() ? TEXT("true") : TEXT("false");
		case EJson::Number: return Num(V->AsNumber());
		default: return FString();
		}
	}

	TOptional<double> ValueNumber(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid()) return {};
		if (V->Type == EJson::Number) return V->AsNumber();
		if (V->Type == EJson::Boolean) return V->AsBool() ? 1.0 : 0.0;
		if (V->Type == EJson::String)
		{
			const FString S = V->AsString().TrimStartAndEnd();
			if (S.IsNumeric()) return FCString::Atod(*S);
		}
		return {};
	}

	FString Str(const TSharedPtr<FJsonObject>& In, const FString& Key, const FString& Default)
	{
		const TSharedPtr<FJsonValue> V = Get(In, Key);
		return V.IsValid() && V->Type != EJson::Object && V->Type != EJson::Array ? ValueText(V) : Default;
	}

	TOptional<double> Num(const TSharedPtr<FJsonObject>& In, const FString& Key) { return ValueNumber(Get(In, Key)); }

	TOptional<int64> Int(const TSharedPtr<FJsonObject>& In, const FString& Key)
	{
		const TOptional<double> D = Num(In, Key);
		if (!D.IsSet() || FMath::Abs(D.GetValue()) > 9.0e15) return {};
		return (int64)FMath::RoundToDouble(D.GetValue());
	}

	int32 IntIn(const TSharedPtr<FJsonObject>& In, const FString& Key, int32 Default, int32 Min, int32 Max)
	{
		const TOptional<int64> V = Int(In, Key);
		return V.IsSet() ? (int32)FMath::Clamp<int64>(V.GetValue(), Min, Max) : Default;
	}

	TOptional<bool> Bool(const TSharedPtr<FJsonObject>& In, const FString& Key)
	{
		const TSharedPtr<FJsonValue> V = Get(In, Key);
		if (!V.IsValid()) return {};
		if (V->Type == EJson::Boolean) return V->AsBool();
		if (V->Type == EJson::Number) return V->AsNumber() != 0;
		if (V->Type == EJson::String)
		{
			const FString S = V->AsString().TrimStartAndEnd().ToLower();
			if (S == TEXT("true") || S == TEXT("yes") || S == TEXT("1") || S == TEXT("on")) return true;
			if (S == TEXT("false") || S == TEXT("no") || S == TEXT("0") || S == TEXT("off")) return false;
		}
		return {};
	}

	bool BoolOr(const TSharedPtr<FJsonObject>& In, const FString& Key, bool Default)
	{
		const TOptional<bool> B = Bool(In, Key);
		return B.IsSet() ? B.GetValue() : Default;
	}

	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& In, const FString& Key)
	{
		const TSharedPtr<FJsonValue> V = Get(In, Key);
		return V.IsValid() && V->Type == EJson::Object ? V->AsObject() : nullptr;
	}

	TArray<TSharedPtr<FJsonValue>> Arr(const TSharedPtr<FJsonObject>& In, const FString& Key)
	{
		const TSharedPtr<FJsonValue> V = Get(In, Key);
		return V.IsValid() && V->Type == EJson::Array ? V->AsArray() : TArray<TSharedPtr<FJsonValue>>();
	}

	TArray<FString> Strings(const TSharedPtr<FJsonObject>& In, const FString& Key)
	{
		TArray<FString> Out;
		const TSharedPtr<FJsonValue> V = Get(In, Key);
		if (!V.IsValid()) return Out;
		if (V->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Item : V->AsArray())
			{
				const FString S = ValueText(Item).TrimStartAndEnd();
				if (!S.IsEmpty()) Out.Add(S);
			}
		}
		else
		{
			const FString S = ValueText(V).TrimStartAndEnd();
			if (!S.IsEmpty()) Out.Add(S);
		}
		return Out;
	}

	bool ValueVector(const TSharedPtr<FJsonValue>& V, FVector& Out)
	{
		if (!V.IsValid()) return false;
		TArray<double> Numbers;
		if (V->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Item : V->AsArray())
			{
				const TOptional<double> N = ValueNumber(Item);
				if (!N.IsSet()) return false;
				Numbers.Add(N.GetValue());
			}
		}
		else if (V->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			for (const TCHAR* K : { TEXT("x"), TEXT("y"), TEXT("z") })
			{
				TOptional<double> N = ValueNumber(Get(O, K));
				if (!N.IsSet()) N = ValueNumber(Get(O, FString(K).ToUpper()));
				if (!N.IsSet()) return false;
				Numbers.Add(N.GetValue());
			}
		}
		else if (V->Type == EJson::String)
		{
			TArray<FString> Parts;
			V->AsString().Replace(TEXT("["), TEXT("")).Replace(TEXT("]"), TEXT("")).ParseIntoArray(Parts, TEXT(","));
			for (const FString& P : Parts)
			{
				const FString T = P.TrimStartAndEnd();
				if (!T.IsNumeric()) return false;
				Numbers.Add(FCString::Atod(*T));
			}
		}
		else if (V->Type == EJson::Number)
		{
			// A single number for a scale means uniform.
			Numbers = { V->AsNumber(), V->AsNumber(), V->AsNumber() };
		}
		if (Numbers.Num() != 3) return false;
		Out = FVector(Numbers[0], Numbers[1], Numbers[2]);
		return true;
	}

	bool OptVector(const TSharedPtr<FJsonObject>& In, const FString& Key, TOptional<FVector>& OutValue, FString& OutError)
	{
		const TSharedPtr<FJsonValue> V = Get(In, Key);
		if (!V.IsValid()) return true;
		FVector Vec;
		if (!ValueVector(V, Vec))
		{
			OutError = FString::Printf(TEXT("'%s' must be [x, y, z]."), *Key);
			return false;
		}
		OutValue = Vec;
		return true;
	}

	// ─── Results ─────────────────────────────────────────────────────────────

	FString Clip(const FString& Text, int32 Max)
	{
		if (Text.Len() <= Max) return Text;
		const int32 Keep = FMath::Max(0, Max - 120);
		return Text.Left(Keep) + FString::Printf(TEXT("\n… (cut: %d more characters. Narrow the request to see the rest.)"), Text.Len() - Keep);
	}

	FZettaToolResult Ok(const FString& Text, const FString& Summary)
	{
		FZettaToolResult R = FZettaToolResult::Ok(Clip(Text));
		R.Summary = Summary;
		return R;
	}

	FZettaToolResult Fail(const FString& Text)
	{
		return FZettaToolResult::Error(Clip(Text, 4000));
	}

	FString Short(const FString& Text, int32 Max)
	{
		FString S = Text.Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" ")).TrimStartAndEnd();
		if (S.Len() > Max) S = S.Left(FMath::Max(0, Max - 1)) + TEXT("…");
		return S;
	}

	FString Plural(int64 N, const TCHAR* One, const TCHAR* Many)
	{
		return FString::Printf(TEXT("%lld %s"), N, N == 1 ? One : Many);
	}

	FString Num(double V)
	{
		if (FMath::IsNearlyEqual(V, FMath::RoundToDouble(V), 1e-6) && FMath::Abs(V) < 1e15) return FString::Printf(TEXT("%lld"), (int64)FMath::RoundToDouble(V));
		FString S = FString::Printf(TEXT("%.3f"), V);
		while (S.EndsWith(TEXT("0"))) S.LeftChopInline(1);
		if (S.EndsWith(TEXT("."))) S.LeftChopInline(1);
		return S;
	}

	FString Vec(const FVector& V) { return FString::Printf(TEXT("[%s, %s, %s]"), *Num(V.X), *Num(V.Y), *Num(V.Z)); }
	FString Rot(const FRotator& R) { return FString::Printf(TEXT("[%s, %s, %s]"), *Num(R.Pitch), *Num(R.Yaw), *Num(R.Roll)); }
	FString Quoted(const FString& S) { return TEXT("\"") + S + TEXT("\""); }

	FString JoinQuoted(const TArray<FString>& Items, int32 Max)
	{
		TArray<FString> Out;
		for (int32 i = 0; i < Items.Num() && i < Max; ++i) Out.Add(Quoted(Items[i]));
		FString S = FString::Join(Out, TEXT(", "));
		if (Items.Num() > Max) S += FString::Printf(TEXT(" and %d more"), Items.Num() - Max);
		return S;
	}

	FZettaToolPreview Preview(const FString& Title, const FString& Body)
	{
		FZettaToolPreview P;
		P.Title = Title;
		P.Body = Body;
		return P;
	}

	FZettaToolPreview PreviewError(const FString& Title, const FString& Error)
	{
		FZettaToolPreview P;
		P.Title = Title;
		P.Error = Error;
		return P;
	}

	// ─── Editor ──────────────────────────────────────────────────────────────

	UWorld* EditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	UWorld* PlayWorld()
	{
		return GEditor ? GEditor->PlayWorld.Get() : nullptr;
	}

	bool IsPlaying() { return GEditor && (GEditor->PlayWorld != nullptr || GEditor->IsPlaySessionInProgress()); }

	FString TransactionTitle(const FString& What) { return FString(TransactionPrefix) + What; }

	// ─── Files ───────────────────────────────────────────────────────────────

	static FString Normalized(const FString& Path)
	{
		FString P = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeFilename(P);
		FPaths::CollapseRelativeDirectories(P);
		return P;
	}

	static FString AsFolder(FString Path)
	{
		Path = Normalized(Path);
		if (!Path.EndsWith(TEXT("/"))) Path += TEXT("/");
		return Path;
	}

	FString ProjectDir() { return AsFolder(FPaths::ProjectDir()); }

	FString PluginDir()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Zetta3D"));
		return Plugin.IsValid() ? AsFolder(Plugin->GetBaseDir()) : FString();
	}

	FString AttachedDir() { return AsFolder(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Zetta3D"), TEXT("Attached"))); }

	FString AttachedFile(const FString& Ref)
	{
		FString Key = Ref.TrimStartAndEnd();
		if (!Key.StartsWith(TEXT("attached:"))) Key = TEXT("attached:") + Key;
		return ZettaAgentSession::AttachedFile(Key);
	}

	bool IsUnder(const FString& Absolute, const FString& Folder)
	{
		if (Folder.IsEmpty()) return false;
		const FString F = AsFolder(Folder);
		const FString A = Normalized(Absolute);
		return A.StartsWith(F, ESearchCase::IgnoreCase) || (A + TEXT("/")).Equals(F, ESearchCase::IgnoreCase);
	}

	FString ProjectRelative(const FString& Absolute)
	{
		FString A = Normalized(Absolute);
		const FString Root = ProjectDir();
		if (A.StartsWith(Root, ESearchCase::IgnoreCase)) return A.RightChop(Root.Len());
		return A;
	}

	static bool IsProjectPluginSource(const FString& Relative)
	{
		// Plugins/<Name>/Source/… (or nested Plugins/Group/<Name>/Source/…).
		if (!Relative.StartsWith(TEXT("Plugins/"), ESearchCase::IgnoreCase)) return false;
		return Relative.Contains(TEXT("/Source/"), ESearchCase::IgnoreCase);
	}

	bool ProjectFile(const FString& Path, bool bWrite, FString& OutAbsolute, FString& OutRelative, FString& OutError)
	{
		FString P = Path.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/"));
		if (P.IsEmpty())
		{
			OutError = TEXT("'path' is required (relative to the project folder, e.g. \"Source/MyGame/MyActor.cpp\").");
			return false;
		}
		const FString Root = ProjectDir();
		const bool bAbsolute = !FPaths::IsRelative(P);
		const FString Full = Normalized(bAbsolute ? P : Root + P);
		if (!IsUnder(Full, Root) || Full.Contains(TEXT("/../")))
		{
			OutError = FString::Printf(TEXT("'%s' is outside the project folder. Use a path relative to the project (Source/…, Config/…, Content/Python/…)."), *Path);
			return false;
		}
		OutAbsolute = Full;
		OutRelative = ProjectRelative(Full);

		const FString Plugin = PluginDir();
		if (bWrite)
		{
			if (!Plugin.IsEmpty() && IsUnder(Full, Plugin))
			{
				OutError = TEXT("The Zetta 3D plugin's own folder is never written. Put the file in the project's Source/, Config/ or Content/Python/.");
				return false;
			}
			const FString& R = OutRelative;
			const bool bAllowed = R.StartsWith(TEXT("Source/"), ESearchCase::IgnoreCase) || R.StartsWith(TEXT("Config/"), ESearchCase::IgnoreCase)
				|| R.StartsWith(TEXT("Content/Python/"), ESearchCase::IgnoreCase) || IsProjectPluginSource(R);
			if (!bAllowed)
			{
				OutError = FString::Printf(TEXT("'%s' can't be written: text files go under Source/, Config/, Content/Python/ or Plugins/<plugin>/Source/. Assets are changed with the asset tools."), *R);
				return false;
			}
			static const TCHAR* Binary[] = { TEXT(".uasset"), TEXT(".umap"), TEXT(".dll"), TEXT(".exe"), TEXT(".dylib"), TEXT(".so"), TEXT(".lib"), TEXT(".pdb"), TEXT(".png"), TEXT(".jpg"), TEXT(".fbx"), TEXT(".glb") };
			for (const TCHAR* Ext : Binary)
			{
				if (R.EndsWith(Ext, ESearchCase::IgnoreCase))
				{
					OutError = FString::Printf(TEXT("'%s' is not a text file; write_file only writes text (code, config, Python)."), *R);
					return false;
				}
			}
		}
		return true;
	}

	bool ReadableFile(const FString& Path, FString& OutAbsolute, FString& OutError)
	{
		const FString P = Path.TrimStartAndEnd();
		if (P.StartsWith(TEXT("attached:")))
		{
			OutAbsolute = AttachedFile(P);
			if (OutAbsolute.IsEmpty())
			{
				OutError = FString::Printf(TEXT("'%s' is no longer available. Ask the user to attach it again."), *P);
				return false;
			}
			return true;
		}
		// A file the user attached, kept outside the content folder.
		const FString Full = Normalized(FPaths::IsRelative(P) ? ProjectDir() + P : P);
		if (IsUnder(Full, AttachedDir()))
		{
			OutAbsolute = Full;
		}
		else
		{
			FString Relative;
			if (!ProjectFile(P, false, OutAbsolute, Relative, OutError)) return false;
		}
		if (!IFileManager::Get().FileExists(*OutAbsolute))
		{
			OutError = FString::Printf(TEXT("'%s' does not exist."), *P);
			return false;
		}
		return true;
	}

	// ─── Content paths ───────────────────────────────────────────────────────

	FString PackagePath(const FString& Path)
	{
		FString P = Path.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/"));
		// Class-style references ("Blueprint'/Game/A.A'") — keep what is inside the quotes.
		int32 Quote;
		if (P.FindChar(TEXT('\''), Quote) && P.EndsWith(TEXT("'"))) P = P.Mid(Quote + 1, P.Len() - Quote - 2);
		if (P.StartsWith(TEXT("Content/"))) P = TEXT("/Game/") + P.RightChop(8);
		int32 Colon;
		if (P.FindChar(TEXT(':'), Colon)) P = P.Left(Colon);
		int32 Dot;
		if (P.FindLastChar(TEXT('.'), Dot) && Dot > P.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd)) P = P.Left(Dot);
		while (P.EndsWith(TEXT("/"))) P.LeftChopInline(1);
		return P;
	}

	FString ObjectPath(const FString& Path)
	{
		const FString Package = PackagePath(Path);
		return Package + TEXT(".") + FPackageName::GetShortName(Package);
	}

	bool WritableContentPath(const FString& Path, FString& OutPackage, FString& OutError)
	{
		OutPackage = PackagePath(Path);
		if (!OutPackage.StartsWith(TEXT("/")))
		{
			OutError = FString::Printf(TEXT("'%s' is not a content path. Write it as \"/Game/Folder/Name\"."), *Path);
			return false;
		}
		if (OutPackage.StartsWith(TEXT("/Engine/")) || OutPackage.StartsWith(TEXT("/Script/")) || OutPackage.StartsWith(TEXT("/Zetta3D/")) || OutPackage.StartsWith(TEXT("/Temp/")))
		{
			OutError = FString::Printf(TEXT("'%s' is not in the project's content. Use a path under /Game/."), *OutPackage);
			return false;
		}
		FText Reason;
		if (!FPackageName::IsValidLongPackageName(OutPackage, false, &Reason))
		{
			OutError = FString::Printf(TEXT("'%s' is not a valid content path: %s"), *OutPackage, *Reason.ToString());
			return false;
		}
		return true;
	}
}
