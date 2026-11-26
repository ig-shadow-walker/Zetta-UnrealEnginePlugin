// Copyright Zetta 3D. See LICENSE.md.
// What the project is: unreal_project_info, api_lookup (the running engine's reflection), and the editor snapshot
// sent with every chat request.

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/InputSettings.h"
#include "GameMapsSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "IContentBrowserSingleton.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "ProjectDescriptor.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "ZettaAgentTools.h"
#include "ZettaCompile.h"
#include "ZettaConfig.h"
#include "ZettaLevelOutline.h"
#include "ZettaLogBuffer.h"
#include "ZettaProperties.h"
#include "ZettaSettings.h"
#include "ZettaTargets.h"
#include "ZettaToolCommon.h"

namespace ZettaProjectImpl
{
	using ZettaTool::Fail;
	using ZettaTool::Ok;

	struct FRendering
	{
		bool bLumen = false, bNanite = false, bVirtualShadowMaps = false, bRayTracing = false, bForwardShading = false;
	};

	int32 CVar(const TCHAR* Name, int32 Default = 0)
	{
		const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name);
		return V ? V->GetInt() : Default;
	}

	FRendering Rendering()
	{
		FRendering R;
		R.bLumen = CVar(TEXT("r.DynamicGlobalIlluminationMethod")) == 1 || CVar(TEXT("r.ReflectionMethod")) == 1;
		R.bNanite = CVar(TEXT("r.Nanite.ProjectEnabled"), 1) != 0;
		R.bVirtualShadowMaps = CVar(TEXT("r.Shadow.Virtual.Enable")) != 0;
		R.bRayTracing = CVar(TEXT("r.RayTracing")) != 0;
		R.bForwardShading = CVar(TEXT("r.ForwardShading")) != 0;
		return R;
	}

	bool EnhancedInput()
	{
		const UClass* C = UInputSettings::GetDefaultInputComponentClass();
		return C && C->GetName().Contains(TEXT("EnhancedInput"));
	}

	FString ShortPath(const FString& ObjectPath)
	{
		FString P = ZettaTool::PackagePath(ObjectPath);
		return P;
	}

	FString EditorStartupMap()
	{
#if WITH_EDITORONLY_DATA
		return GetDefault<UGameMapsSettings>()->EditorStartupMap.GetLongPackageName();
#else
		return FString();
#endif
	}

	/** Plugins worth naming: the project's own, the ones its .uproject turns on, and well-known gameplay ones. */
	TArray<FString> NotablePlugins(int32 Max)
	{
		static const TCHAR* Notable[] = { TEXT("EnhancedInput"), TEXT("PythonScriptPlugin"), TEXT("CommonUI"), TEXT("GameplayAbilities"), TEXT("Niagara"), TEXT("PCG"),
			TEXT("StateTree"), TEXT("GameplayStateTree"), TEXT("MassEntity"), TEXT("Water"), TEXT("ChaosVehiclesPlugin"), TEXT("ModularGameplay"), TEXT("GameFeatures"),
			TEXT("OnlineSubsystem"), TEXT("Paper2D"), TEXT("MetaHuman"), TEXT("ControlRig"), TEXT("IKRig"), TEXT("MotionWarping"), TEXT("SmartObjects"), TEXT("Landmass"),
			TEXT("Interchange"), TEXT("GLTFImporter"), TEXT("Text3D"), TEXT("OpenXR"), TEXT("ModelingToolsEditorMode"), TEXT("Zetta3D") };
		TArray<FString> Out;
		TSet<FString> FromProject;
		if (const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject())
		{
			for (const FPluginReferenceDescriptor& P : Project->Plugins)
			{
				if (P.bEnabled) FromProject.Add(P.Name);
			}
		}
		for (const TSharedRef<IPlugin>& P : IPluginManager::Get().GetEnabledPlugins())
		{
			const FString Name = P->GetName();
			bool bNamed = P->GetLoadedFrom() == EPluginLoadedFrom::Project || FromProject.Contains(Name);
			for (const TCHAR* N : Notable) bNamed |= Name == N;
			if (bNamed) Out.AddUnique(Name);
		}
		Out.Sort();
		if (Out.Num() > Max) Out.SetNum(Max);
		return Out;
	}

	TArray<FString> ContentFolders()
	{
		TArray<FString> Out;
		FAssetRegistryModule::GetRegistry().GetSubPaths(TEXT("/Game"), Out, false);
		Out.Sort();
		if (Out.Num() > 60) Out.SetNum(60);
		return Out;
	}

	TArray<FString> SourceFolders()
	{
		TArray<FString> Out;
		const FString Root = ZettaTool::ProjectDir() + TEXT("Source");
		if (!IFileManager::Get().DirectoryExists(*Root)) return Out;
		TArray<FString> Level1;
		IFileManager::Get().FindFiles(Level1, *(Root / TEXT("*")), false, true);
		Level1.Sort();
		for (const FString& A : Level1)
		{
			Out.Add(TEXT("Source/") + A);
			TArray<FString> Level2;
			IFileManager::Get().FindFiles(Level2, *(Root / A / TEXT("*")), false, true);
			Level2.Sort();
			for (const FString& B : Level2)
			{
				if (Out.Num() < 60) Out.Add(TEXT("Source/") + A + TEXT("/") + B);
			}
		}
		return Out;
	}

	// ─── unreal_project_info ─────────────────────────────────────────────────

	FZettaToolResult ProjectInfo(const TSharedPtr<FJsonObject>& In)
	{
		TArray<FString> Out;
		Out.Add(FString::Printf(TEXT("Unreal Engine %s · project %s (%s)"), *ZettaConfig::EngineVersion(), FApp::GetProjectName(), *FPaths::GetCleanFilename(FPaths::GetProjectFilePath())));
		const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject();
		if (Project && Project->Modules.Num() > 0)
		{
			TArray<FString> Modules;
			for (const FModuleDescriptor& M : Project->Modules) Modules.Add(FString::Printf(TEXT("%s (%s)"), *M.Name.ToString(), EHostType::ToString(M.Type)));
			Out.Add(TEXT("C++ project; modules: ") + FString::Join(Modules, TEXT(", ")));
		}
		else
		{
			Out.Add(TEXT("Blueprint-only project (no C++ modules yet; Tools › New C++ Class adds them)."));
		}
		Out.Add(ZettaCompile::LiveCodingAvailable() ? TEXT("compile uses Live Coding.")
			: PLATFORM_WINDOWS ? TEXT("Live Coding is off; compile uses hot reload.") : TEXT("Live Coding is Windows-only; compile uses the editor's hot reload (new classes need an IDE build and a restart)."));
		TArray<FString> Targets;
		if (Project)
		{
			for (const FName& P : Project->TargetPlatforms) Targets.Add(P.ToString());
		}
		Out.Add(FString::Printf(TEXT("Editor platform %s · target platforms: %s"), ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName()), Targets.Num() > 0 ? *FString::Join(Targets, TEXT(", ")) : TEXT("all")));
		const FRendering R = Rendering();
		Out.Add(FString::Printf(TEXT("Rendering: Lumen %s · Nanite %s · virtual shadow maps %s · ray tracing %s · %s"), R.bLumen ? TEXT("on") : TEXT("off"), R.bNanite ? TEXT("on") : TEXT("off"),
			R.bVirtualShadowMaps ? TEXT("on") : TEXT("off"), R.bRayTracing ? TEXT("on") : TEXT("off"), R.bForwardShading ? TEXT("forward shading") : TEXT("deferred shading")));
		const UClass* InputComponent = UInputSettings::GetDefaultInputComponentClass();
		const UClass* PlayerInput = UInputSettings::GetDefaultPlayerInputClass();
		Out.Add(FString::Printf(TEXT("Input: %s (default input component %s, player input %s)"), EnhancedInput() ? TEXT("Enhanced Input") : TEXT("legacy input"),
			InputComponent ? *InputComponent->GetName() : TEXT("?"), PlayerInput ? *PlayerInput->GetName() : TEXT("?")));
		Out.Add(FString::Printf(TEXT("Maps: editor startup %s · game default %s · default GameMode %s"), *EditorStartupMap(), *ShortPath(UGameMapsSettings::GetGameDefaultMap()),
			*UGameMapsSettings::GetGlobalDefaultGameMode()));
		if (UWorld* World = ZettaTool::EditorWorld())
		{
			Out.Add(FString::Printf(TEXT("Open level: %s%s%s"), *World->GetOutermost()->GetName(), World->GetOutermost()->IsDirty() ? TEXT(" (unsaved changes)") : TEXT(""),
				World->GetWorldPartition() ? TEXT(" · World Partition") : TEXT("")));
		}
		Out.Add(TEXT("Plugins: ") + FString::Join(NotablePlugins(120), TEXT(", ")));
		Out.Add(TEXT("Content folders: ") + FString::Join(ContentFolders(), TEXT(", ")));
		const TArray<FString> Sources = SourceFolders();
		if (Sources.Num() > 0) Out.Add(TEXT("Source folders: ") + FString::Join(Sources, TEXT(", ")));
		Out.Add(FString::Printf(TEXT("Models from Zetta 3D import into %s."), *UZettaProjectSettings::Get()->GetModelsFolder()));
		return Ok(FString::Join(Out, TEXT("\n")));
	}

	// ─── api_lookup ──────────────────────────────────────────────────────────

	/** The Python name the editor gives a C++ name: SetStaticMesh → set_static_mesh, bSimulatePhysics → simulate_physics. */
	FString Pythonize(FString Name, bool bBool = false)
	{
		if (bBool && Name.Len() > 1 && Name[0] == TEXT('b') && FChar::IsUpper(Name[1])) Name.RightChopInline(1);
		FString Out;
		for (int32 i = 0; i < Name.Len(); ++i)
		{
			const TCHAR C = Name[i];
			if (FChar::IsUpper(C) && i > 0)
			{
				const TCHAR Prev = Name[i - 1];
				const bool bNextLower = i + 1 < Name.Len() && FChar::IsLower(Name[i + 1]);
				if ((FChar::IsLower(Prev) || FChar::IsDigit(Prev)) || (FChar::IsUpper(Prev) && bNextLower))
				{
					if (!Out.EndsWith(TEXT("_"))) Out += TEXT("_");
				}
			}
			Out.AppendChar(FChar::ToLower(C));
		}
		return Out;
	}

	FString PythonClass(const UClass* C)
	{
		FString N = C->GetName();
		N.RemoveFromEnd(TEXT("_C"));
		return TEXT("unreal.") + N;
	}

	bool Usable(const UClass* C)
	{
		const FString N = C->GetName();
		return !C->HasAnyClassFlags(CLASS_NewerVersionExists) && !N.StartsWith(TEXT("SKEL_")) && !N.StartsWith(TEXT("REINST_")) && !N.StartsWith(TEXT("TRASHCLASS_"))
			&& !N.StartsWith(TEXT("PLACEHOLDER-")) && !N.StartsWith(TEXT("HOTRELOADED_"));
	}

	/** 0 = exact, 1 = starts with, 2 = contains, -1 = no match. */
	int32 Score(const FString& Name, const FString& Query)
	{
		if (Name.Equals(Query, ESearchCase::IgnoreCase)) return 0;
		if (Name.StartsWith(Query, ESearchCase::IgnoreCase)) return 1;
		return Name.Contains(Query) ? 2 : -1;
	}

	FString ClassLine(const UClass* C)
	{
		TArray<FString> Flags;
		if (C->HasAnyClassFlags(CLASS_Abstract)) Flags.Add(TEXT("abstract"));
		if (C->HasAnyClassFlags(CLASS_Deprecated)) Flags.Add(TEXT("deprecated"));
		if (C->GetBoolMetaDataHierarchical(TEXT("BlueprintSpawnableComponent"))) Flags.Add(TEXT("spawnable component"));
		if (C->GetBoolMetaDataHierarchical(TEXT("IsBlueprintBase"))) Flags.Add(TEXT("Blueprintable"));
		if (C->HasAnyClassFlags(CLASS_Native) == false) Flags.Add(TEXT("Blueprint class"));
		return FString::Printf(TEXT("class %s : %s (%s)%s — Python %s"), *C->GetName(), C->GetSuperClass() ? *C->GetSuperClass()->GetName() : TEXT("—"), *C->GetOuterUPackage()->GetName(),
			Flags.Num() > 0 ? *(TEXT(" [") + FString::Join(Flags, TEXT(", ")) + TEXT("]")) : TEXT(""), *PythonClass(C));
	}

	FString FunctionLine(const UClass* C, const UFunction* F)
	{
		TArray<FString> Params;
		FString Return;
		for (TFieldIterator<FProperty> It(F); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			const FString Piece = ZettaProps::TypeName(*It) + TEXT(" ") + It->GetName();
			if (It->HasAnyPropertyFlags(CPF_ReturnParm)) Return = ZettaProps::TypeName(*It);
			else if (It->HasAnyPropertyFlags(CPF_OutParm) && !It->HasAnyPropertyFlags(CPF_ConstParm | CPF_ReferenceParm)) Params.Add(TEXT("out ") + Piece);
			else Params.Add(Piece);
		}
		TArray<FString> Flags;
		if (F->HasAnyFunctionFlags(FUNC_BlueprintCallable)) Flags.Add(TEXT("BlueprintCallable"));
		if (F->HasAnyFunctionFlags(FUNC_BlueprintPure)) Flags.Add(TEXT("pure"));
		if (F->HasAnyFunctionFlags(FUNC_BlueprintEvent)) Flags.Add(TEXT("event"));
		if (F->HasAnyFunctionFlags(FUNC_Static)) Flags.Add(TEXT("static"));
		const FString Script = F->GetMetaData(TEXT("ScriptName"));
		const FString PyName = Pythonize(Script.IsEmpty() ? F->GetName() : Script);
		return FString::Printf(TEXT("function %s.%s(%s)%s%s — Python %s.%s"), *C->GetName(), *F->GetName(), *FString::Join(Params, TEXT(", ")),
			Return.IsEmpty() ? TEXT("") : *(TEXT(" → ") + Return), Flags.Num() > 0 ? *(TEXT(" [") + FString::Join(Flags, TEXT(", ")) + TEXT("]")) : TEXT(""), *PythonClass(C), *PyName);
	}

	FString PropertyLine(const UClass* C, const FProperty* P)
	{
		TArray<FString> Flags;
		if (P->HasAnyPropertyFlags(CPF_Edit)) Flags.Add(P->HasAnyPropertyFlags(CPF_DisableEditOnInstance) ? TEXT("EditDefaultsOnly") : P->HasAnyPropertyFlags(CPF_DisableEditOnTemplate) ? TEXT("EditInstanceOnly") : TEXT("EditAnywhere"));
		if (P->HasAnyPropertyFlags(CPF_EditConst)) Flags.Add(TEXT("read-only in the editor"));
		if (P->HasAnyPropertyFlags(CPF_BlueprintVisible)) Flags.Add(P->HasAnyPropertyFlags(CPF_BlueprintReadOnly) ? TEXT("BlueprintReadOnly") : TEXT("BlueprintReadWrite"));
		if (P->HasAnyPropertyFlags(CPF_Net)) Flags.Add(TEXT("replicated"));
		return FString::Printf(TEXT("property %s.%s: %s%s — Python get_editor_property('%s')"), *C->GetName(), *P->GetName(), *ZettaProps::TypeName(P),
			Flags.Num() > 0 ? *(TEXT(" [") + FString::Join(Flags, TEXT(", ")) + TEXT("]")) : TEXT(""), *Pythonize(P->GetName(), P->IsA<FBoolProperty>()));
	}

	FZettaToolResult ApiLookup(const TSharedPtr<FJsonObject>& In)
	{
		FString Query = ZettaTool::Str(In, TEXT("query")).TrimStartAndEnd();
		if (Query.IsEmpty()) return Fail(TEXT("'query' is required: a class, function or property name (or part of one), or \"Class.Member\"."));
		const FString Kind = ZettaTool::Str(In, TEXT("kind"), TEXT("any")).ToLower();
		const int32 Limit = ZettaTool::IntIn(In, TEXT("limit"), 20, 1, 50);
		const bool bClasses = Kind == TEXT("any") || Kind == TEXT("class");
		const bool bFunctions = Kind == TEXT("any") || Kind == TEXT("function");
		const bool bProperties = Kind == TEXT("any") || Kind == TEXT("property");
		// "StaticMeshComponent.SetStaticMesh" limits members to one class.
		FString ClassPart, MemberPart = Query;
		Query.Split(TEXT("."), &ClassPart, &MemberPart);
		if (Query.StartsWith(TEXT("unreal."))) { ClassPart.Reset(); MemberPart = Query.RightChop(7); }
		if (ClassPart.Len() > 1 && (ClassPart[0] == TEXT('U') || ClassPart[0] == TEXT('A')) && FChar::IsUpper(ClassPart[1]) && !FindFirstObject<UClass>(*ClassPart, EFindFirstObjectOptions::NativeFirst)) ClassPart.RightChopInline(1);
		const FString PyQuery = MemberPart.Replace(TEXT("_"), TEXT(""));

		struct FHit
		{
			int32 Score;
			FString Line;
		};
		TArray<FHit> Hits;
		const UClass* Exact = nullptr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			const UClass* C = *It;
			if (!Usable(C)) continue;
			if (!ClassPart.IsEmpty() && !C->GetName().Equals(ClassPart, ESearchCase::IgnoreCase)) continue;
			if (ClassPart.IsEmpty() && bClasses)
			{
				const int32 S = Score(C->GetName(), MemberPart);
				if (S >= 0) Hits.Add({ S, ClassLine(C) });
				if (S == 0) Exact = C;
			}
			if (bFunctions)
			{
				for (TFieldIterator<UFunction> F(C, EFieldIteratorFlags::ExcludeSuper); F; ++F)
				{
					int32 S = Score(F->GetName(), MemberPart);
					if (S < 0) S = Score(F->GetName(), PyQuery);
					if (S < 0 && ClassPart.IsEmpty()) continue;
					if (S < 0) S = 3;
					Hits.Add({ S + 1, FunctionLine(C, *F) });
				}
			}
			if (bProperties)
			{
				for (TFieldIterator<FProperty> P(C, EFieldIteratorFlags::ExcludeSuper); P; ++P)
				{
					int32 S = Score(P->GetName(), MemberPart);
					if (S < 0) S = Score(P->GetName(), PyQuery);
					if (S < 0 && ClassPart.IsEmpty()) continue;
					if (S < 0) S = 3;
					Hits.Add({ S + 1, PropertyLine(C, *P) });
				}
			}
		}
		if (Hits.Num() == 0) return Ok(FString::Printf(TEXT("Nothing in the engine's reflection matches \"%s\". Try part of the name, or kind \"any\"."), *Query));
		Hits.StableSort([](const FHit& A, const FHit& B) { return A.Score != B.Score ? A.Score < B.Score : A.Line.Len() < B.Line.Len(); });
		TArray<FString> Lines;
		for (int32 i = 0; i < Hits.Num() && i < Limit; ++i) Lines.Add(Hits[i].Line);
		FString Text = FString::Join(Lines, TEXT("\n"));
		if (Hits.Num() > Limit) Text += FString::Printf(TEXT("\n… %d more. Narrow the query or use \"Class.Member\"."), Hits.Num() - Limit);
		// An exact class: show its own editable properties and callable functions too.
		if (Exact && Kind != TEXT("function") && Kind != TEXT("property"))
		{
			TArray<FString> Props, Funcs;
			for (TFieldIterator<FProperty> P(Exact, EFieldIteratorFlags::ExcludeSuper); P && Props.Num() < 40; ++P)
			{
				if (P->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible)) Props.Add(FString::Printf(TEXT("%s: %s"), *P->GetName(), *ZettaProps::TypeName(*P)));
			}
			for (TFieldIterator<UFunction> F(Exact, EFieldIteratorFlags::ExcludeSuper); F && Funcs.Num() < 40; ++F)
			{
				if (F->HasAnyFunctionFlags(FUNC_BlueprintCallable)) Funcs.Add(F->GetName());
			}
			if (Props.Num() > 0) Text += FString::Printf(TEXT("\n%s's own properties: %s"), *Exact->GetName(), *FString::Join(Props, TEXT("; ")));
			if (Funcs.Num() > 0) Text += FString::Printf(TEXT("\n%s's own callable functions: %s (ask \"%s.<Name>\" for a signature)"), *Exact->GetName(), *FString::Join(Funcs, TEXT(", ")), *Exact->GetName());
		}
		return Ok(Text);
	}

	// ─── The snapshot ────────────────────────────────────────────────────────

	TSharedRef<FJsonValue> Strings(const TArray<FString>& Items)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& S : Items) Out.Add(MakeShared<FJsonValueString>(S));
		return MakeShared<FJsonValueArray>(Out);
	}

	int32 Length(const TSharedRef<FJsonObject>& O)
	{
		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(O, W);
		return Text.Len();
	}
}

TSharedRef<FJsonObject> ZettaAgentSnapshot::Build()
{
	using namespace ZettaProjectImpl;
	TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
	S->SetStringField(TEXT("engineVersion"), ZettaConfig::EngineVersion());
	S->SetStringField(TEXT("projectName"), FApp::GetProjectName());
	S->SetStringField(TEXT("projectFile"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
	const TArray<FString> Modules = ZettaCompile::ProjectModules();
	S->SetBoolField(TEXT("cppProject"), Modules.Num() > 0);
	if (Modules.Num() > 0) S->SetField(TEXT("modules"), ZettaProjectImpl::Strings(Modules));
	S->SetBoolField(TEXT("liveCoding"), ZettaCompile::LiveCodingAvailable());
	S->SetStringField(TEXT("platform"), ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName()));
	if (const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject())
	{
		TArray<FString> Targets;
		for (const FName& P : Project->TargetPlatforms) Targets.Add(P.ToString());
		if (Targets.Num() > 0) S->SetField(TEXT("targetPlatforms"), ZettaProjectImpl::Strings(Targets));
	}
	{
		const FRendering R = Rendering();
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("lumen"), R.bLumen);
		O->SetBoolField(TEXT("nanite"), R.bNanite);
		O->SetBoolField(TEXT("virtualShadowMaps"), R.bVirtualShadowMaps);
		O->SetBoolField(TEXT("rayTracing"), R.bRayTracing);
		O->SetBoolField(TEXT("forwardShading"), R.bForwardShading);
		S->SetObjectField(TEXT("rendering"), O);
	}
	S->SetBoolField(TEXT("enhancedInput"), EnhancedInput());
	{
		FString GameMode = UGameMapsSettings::GetGlobalDefaultGameMode();
		if (!GameMode.IsEmpty()) S->SetStringField(TEXT("gameMode"), ShortPath(GameMode));
	}
	S->SetBoolField(TEXT("playing"), ZettaTool::IsPlaying());

	if (UWorld* World = ZettaTool::EditorWorld())
	{
		TSharedRef<FJsonObject> Map = MakeShared<FJsonObject>();
		Map->SetStringField(TEXT("path"), World->GetOutermost()->GetName());
		Map->SetStringField(TEXT("name"), World->GetMapName());
		Map->SetBoolField(TEXT("dirty"), World->GetOutermost()->IsDirty());
		Map->SetNumberField(TEXT("actorCount"), ZettaTargets::LevelActors().Num());
		Map->SetBoolField(TEXT("worldPartition"), World->GetWorldPartition() != nullptr);
		S->SetObjectField(TEXT("map"), Map);
	}

	if (GEditor)
	{
		TArray<TSharedPtr<FJsonValue>> Selection;
		for (FSelectionIterator It(*GEditor->GetSelectedActors()); It && Selection.Num() < 20; ++It)
		{
			if (const AActor* A = Cast<AActor>(*It))
			{
				TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("label"), A->GetActorLabel());
				O->SetStringField(TEXT("name"), A->GetName());
				O->SetStringField(TEXT("class"), A->GetClass()->GetName());
				Selection.Add(MakeShared<FJsonValueObject>(O));
			}
		}
		if (Selection.Num() > 0) S->SetArrayField(TEXT("selection"), Selection);
	}
	if (FModuleManager::Get().IsModuleLoaded(TEXT("ContentBrowser")))
	{
		TArray<FAssetData> Assets;
		FModuleManager::GetModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().GetSelectedAssets(Assets);
		TArray<TSharedPtr<FJsonValue>> Selected;
		for (int32 i = 0; i < Assets.Num() && i < 20; ++i)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("path"), Assets[i].PackageName.ToString());
			O->SetStringField(TEXT("class"), Assets[i].AssetClassPath.GetAssetName().ToString());
			Selected.Add(MakeShared<FJsonValueObject>(O));
		}
		if (Selected.Num() > 0) S->SetArrayField(TEXT("selectedAssets"), Selected);
	}

	{
		ZettaOutline::FOptions Options;
		Options.Depth = 3;
		Options.Limit = 2000;
		Options.MaxChars = 20000;
		int32 Total = 0;
		S->SetStringField(TEXT("outline"), ZettaOutline::Build(Options, Total));
	}
	const TArray<TSharedPtr<FJsonValue>> CompileErrors = ZettaCompile::LastErrorsJson();
	if (CompileErrors.Num() > 0) S->SetArrayField(TEXT("compileErrors"), CompileErrors);
	{
		TArray<TSharedPtr<FJsonValue>> Errors;
		for (const FZettaLogEntry& E : ZettaLogBuffer::Recent(true, false, false, FString(), 20))
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("category"), E.Category.ToString());
			O->SetStringField(TEXT("message"), E.Message.Left(500));
			Errors.Add(MakeShared<FJsonValueObject>(O));
		}
		if (Errors.Num() > 0) S->SetArrayField(TEXT("recentErrors"), Errors);
	}
	S->SetField(TEXT("plugins"), ZettaProjectImpl::Strings(NotablePlugins(80)));
	S->SetField(TEXT("contentFolders"), ZettaProjectImpl::Strings(ContentFolders()));
	const TArray<FString> Sources = SourceFolders();
	if (Sources.Num() > 0) S->SetField(TEXT("sourceFolders"), ZettaProjectImpl::Strings(Sources));

	// Stay under the protocol's 60,000 characters: the outline goes first, then the long lists.
	if (Length(S) > 60000)
	{
		S->SetStringField(TEXT("outline"), S->GetStringField(TEXT("outline")).Left(8000) + TEXT("\n… (cut: use level_outline)"));
	}
	if (Length(S) > 60000)
	{
		S->RemoveField(TEXT("plugins"));
		S->RemoveField(TEXT("sourceFolders"));
		S->RemoveField(TEXT("contentFolders"));
		S->RemoveField(TEXT("recentErrors"));
	}
	return S;
}

void ZettaToolAreas::RegisterProjectTools(FZettaAgentTools& Tools)
{
	Tools.Register(TEXT("unreal_project_info"), &ZettaProjectImpl::ProjectInfo, [](const TSharedPtr<FJsonObject>&) { return ZettaTool::Preview(TEXT("Read the project's setup")); });
	Tools.Register(TEXT("api_lookup"), &ZettaProjectImpl::ApiLookup, [](const TSharedPtr<FJsonObject>& In) { return ZettaTool::Preview(FString::Printf(TEXT("Look up %s"), *ZettaTool::Str(In, TEXT("query")))); });
}
