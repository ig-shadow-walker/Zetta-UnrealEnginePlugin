// Copyright Zetta 3D. See LICENSE.md.
// Blueprints: create_blueprint, edit_blueprint, and the Blueprint half of inspect_asset. Graph logic stays out of
// reach on purpose: the agent writes it in C++ (a parent class or a component) or with run_python.

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "K2Node_Event.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompilerModule.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "ZettaAgentTools.h"
#include "ZettaAssetUtil.h"
#include "ZettaProperties.h"
#include "ZettaTargets.h"
#include "ZettaToolCommon.h"

namespace ZettaAssets
{
	bool Save(UObject* Asset, FString& OutError)
	{
		if (!Asset)
		{
			OutError = TEXT("Nothing to save.");
			return false;
		}
		if (!UEditorAssetLibrary::SaveLoadedAsset(Asset, false))
		{
			OutError = FString::Printf(TEXT("Unreal could not save %s (is the file read-only or checked in?)."), *ZettaTool::PackagePath(Asset->GetPathName()));
			return false;
		}
		return true;
	}

	FString Compile(UBlueprint* Blueprint, bool& bOutOk)
	{
		FCompilerResultsLog Results;
		Results.SetSourcePath(Blueprint->GetPathName());
		Results.bSilentMode = true;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		TArray<FString> Errors, Warnings;
		for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
		{
			const FString Line = Message->ToText().ToString();
			if (Message->GetSeverity() == EMessageSeverity::Error) Errors.Add(Line);
			else if (Message->GetSeverity() == EMessageSeverity::Warning || Message->GetSeverity() == EMessageSeverity::PerformanceWarning) Warnings.Add(Line);
		}
		bOutOk = Blueprint->Status != BS_Error && Errors.Num() == 0;
		FString Text;
		if (bOutOk) Text = TEXT("Compiled OK.");
		else Text = TEXT("Blueprint errors:\n") + FString::Join(Errors.Num() > 0 ? Errors : TArray<FString>{ TEXT("(the compiler reported an error; open the Blueprint to see it)") }, TEXT("\n")).Left(8000);
		if (Warnings.Num() > 0) Text += TEXT("\nWarnings:\n") + FString::Join(Warnings, TEXT("\n")).Left(4000);
		return Text;
	}

	FString CompileAndSave(UBlueprint* Blueprint, bool& bOutOk)
	{
		FString Text = Compile(Blueprint, bOutOk);
		FString Error;
		Text += Save(Blueprint, Error) ? TEXT(" Saved.") : TEXT(" Not saved: ") + Error;
		return Text;
	}
}

namespace ZettaBlueprintImpl
{
	using ZettaTool::Fail;
	using ZettaTool::Ok;

	// ─── Variable types ──────────────────────────────────────────────────────

	bool PinType(const FString& InType, FEdGraphPinType& Out, FString& OutError)
	{
		FString Type = InType.TrimStartAndEnd();
		Out = FEdGraphPinType();
		if (Type.EndsWith(TEXT("[]")))
		{
			Out.ContainerType = EPinContainerType::Array;
			Type.LeftChopInline(2);
		}
		else if (Type.StartsWith(TEXT("array:"), ESearchCase::IgnoreCase))
		{
			Out.ContainerType = EPinContainerType::Array;
			Type.RightChopInline(6);
		}
		FString Kind = Type, Arg;
		int32 Colon;
		if (Type.FindChar(TEXT(':'), Colon))
		{
			Kind = Type.Left(Colon);
			Arg = Type.RightChop(Colon + 1).TrimStartAndEnd();
		}
		Kind = Kind.TrimStartAndEnd().ToLower();
		auto Struct = [&](UScriptStruct* S)
		{
			Out.PinCategory = UEdGraphSchema_K2::PC_Struct;
			Out.PinSubCategoryObject = S;
		};
		if (Kind == TEXT("bool") || Kind == TEXT("boolean")) Out.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		else if (Kind == TEXT("int") || Kind == TEXT("integer") || Kind == TEXT("int32")) Out.PinCategory = UEdGraphSchema_K2::PC_Int;
		else if (Kind == TEXT("int64")) Out.PinCategory = UEdGraphSchema_K2::PC_Int64;
		else if (Kind == TEXT("byte")) Out.PinCategory = UEdGraphSchema_K2::PC_Byte;
		else if (Kind == TEXT("float") || Kind == TEXT("double") || Kind == TEXT("real") || Kind == TEXT("number"))
		{
			Out.PinCategory = UEdGraphSchema_K2::PC_Real;
			Out.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		}
		else if (Kind == TEXT("string")) Out.PinCategory = UEdGraphSchema_K2::PC_String;
		else if (Kind == TEXT("name")) Out.PinCategory = UEdGraphSchema_K2::PC_Name;
		else if (Kind == TEXT("text")) Out.PinCategory = UEdGraphSchema_K2::PC_Text;
		else if (Kind == TEXT("vector")) Struct(TBaseStructure<FVector>::Get());
		else if (Kind == TEXT("vector2d")) Struct(TBaseStructure<FVector2D>::Get());
		else if (Kind == TEXT("rotator")) Struct(TBaseStructure<FRotator>::Get());
		else if (Kind == TEXT("transform")) Struct(TBaseStructure<FTransform>::Get());
		else if (Kind == TEXT("color") || Kind == TEXT("linearcolor") || Kind == TEXT("colour")) Struct(TBaseStructure<FLinearColor>::Get());
		else if (Kind == TEXT("object") || Kind == TEXT("class") || Kind == TEXT("softobject") || Kind == TEXT("softclass"))
		{
			UClass* Class = ZettaTargets::FindClass(Arg.IsEmpty() ? TEXT("Object") : Arg, nullptr, OutError);
			if (!Class) return false;
			Out.PinCategory = Kind == TEXT("object") ? UEdGraphSchema_K2::PC_Object : Kind == TEXT("class") ? UEdGraphSchema_K2::PC_Class
				: Kind == TEXT("softobject") ? UEdGraphSchema_K2::PC_SoftObject : UEdGraphSchema_K2::PC_SoftClass;
			Out.PinSubCategoryObject = Class;
		}
		else if (Kind == TEXT("struct"))
		{
			UScriptStruct* S = FindFirstObject<UScriptStruct>(*Arg, EFindFirstObjectOptions::NativeFirst);
			if (!S && Arg.StartsWith(TEXT("/"))) S = LoadObject<UScriptStruct>(nullptr, *Arg);
			if (!S)
			{
				OutError = FString::Printf(TEXT("No struct \"%s\"."), *Arg);
				return false;
			}
			Struct(S);
		}
		else if (Kind == TEXT("enum"))
		{
			UEnum* E = FindFirstObject<UEnum>(*Arg, EFindFirstObjectOptions::NativeFirst);
			if (!E && Arg.StartsWith(TEXT("/"))) E = LoadObject<UEnum>(nullptr, *Arg);
			if (!E)
			{
				OutError = FString::Printf(TEXT("No enum \"%s\"."), *Arg);
				return false;
			}
			Out.PinCategory = UEdGraphSchema_K2::PC_Byte;
			Out.PinSubCategoryObject = E;
		}
		else
		{
			OutError = FString::Printf(TEXT("Unknown variable type \"%s\". Use bool, int, float, string, name, text, vector, rotator, transform, color, object:<Class>, class:<Class> (add [] for an array)."), *InType);
			return false;
		}
		return true;
	}

	// ─── The edits ───────────────────────────────────────────────────────────

	/** One Blueprint being changed: what was done, what failed, and the class-default writes kept for after the compile. */
	struct FEdit
	{
		UBlueprint* BP = nullptr;
		TArray<FString> Done;
		TArray<FString> Errors;
		bool bStructural = false;
		/** Class-default writes wait for the compile, which makes the class's properties (new variables) real. */
		TSharedRef<FJsonObject> Defaults = MakeShared<FJsonObject>();
	};

	bool IsActorBlueprint(const UBlueprint* BP) { return BP->ParentClass && BP->ParentClass->IsChildOf(AActor::StaticClass()) && BP->SimpleConstructionScript; }

	void AddComponent(FEdit& E, const TSharedPtr<FJsonObject>& C)
	{
		UBlueprint* BP = E.BP;
		const FString Name = ZettaTool::Str(C, TEXT("name")).TrimStartAndEnd();
		const FString Type = ZettaTool::Str(C, TEXT("type")).TrimStartAndEnd();
		if (!IsActorBlueprint(BP))
		{
			E.Errors.Add(FString::Printf(TEXT("add_component %s: only actor Blueprints have components."), *Name));
			return;
		}
		if (Name.IsEmpty() || Type.IsEmpty())
		{
			E.Errors.Add(TEXT("add_component needs a 'type' and a 'name'."));
			return;
		}
		FString Error;
		UClass* Class = ZettaTargets::FindClass(Type, UActorComponent::StaticClass(), Error);
		if (!Class && !Type.EndsWith(TEXT("Component")) && !Type.StartsWith(TEXT("/")))
		{
			FString Second;
			Class = ZettaTargets::FindClass(Type + TEXT("Component"), UActorComponent::StaticClass(), Second);
		}
		if (!Class)
		{
			E.Errors.Add(FString::Printf(TEXT("add_component %s: %s"), *Name, *Error));
			return;
		}
		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
		{
			E.Errors.Add(FString::Printf(TEXT("add_component %s: %s is abstract."), *Name, *Class->GetName()));
			return;
		}
		if (ZettaTargets::BlueprintComponentNames(BP).ContainsByPredicate([&](const FString& N) { return N.Equals(Name, ESearchCase::IgnoreCase); })
			|| FBlueprintEditorUtils::FindNewVariableIndex(BP, FName(*Name)) != INDEX_NONE)
		{
			E.Errors.Add(FString::Printf(TEXT("add_component: the Blueprint already has a component or variable named \"%s\"."), *Name));
			return;
		}

		USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
		SCS->Modify();
		USCS_Node* Node = SCS->CreateNode(Class, FName(*Name));
		const bool bScene = Class->IsChildOf(USceneComponent::StaticClass());
		const FString ParentName = ZettaTool::Str(C, TEXT("parent")).TrimStartAndEnd();
		FString Where;
		if (bScene && !ParentName.IsEmpty())
		{
			ZettaTargets::FBlueprintComponent Parent;
			if (!ZettaTargets::FindBlueprintComponent(BP, ParentName, false, Parent, Error) || !Cast<USceneComponent>(Parent.Template))
			{
				E.Errors.Add(FString::Printf(TEXT("add_component %s: no scene component \"%s\" to attach to. %s"), *Name, *ParentName, *Error));
				return;
			}
			if (Parent.Node && !Parent.bInherited)
			{
				Parent.Node->AddChildNode(Node);
			}
			else
			{
				if (Parent.Node) Node->SetParent(Parent.Node);
				else Node->SetParent(Cast<USceneComponent>(Parent.Template));
				SCS->AddNode(Node);
			}
			Where = TEXT(" under ") + ParentName;
		}
		else if (bScene)
		{
			// Under the Blueprint's own scene root when it has one; otherwise it becomes a root, which attaches to the
			// root a parent class provides.
			USCS_Node* Root = nullptr;
			for (USCS_Node* R : SCS->GetRootNodes())
			{
				if (R && R->ComponentTemplate && R->ComponentTemplate->IsA<USceneComponent>())
				{
					Root = R;
					break;
				}
			}
			if (Root)
			{
				Root->AddChildNode(Node);
				Where = TEXT(" under ") + Root->GetVariableName().ToString();
			}
			else
			{
				SCS->AddNode(Node);
				Where = TEXT(" as a root");
			}
		}
		else
		{
			SCS->AddNode(Node);
		}
		TArray<FString> Lines, Errors;
		if (const TSharedPtr<FJsonObject> Props = ZettaTool::Obj(C, TEXT("properties"))) ZettaProps::WriteAll(Node->ComponentTemplate, Props, Lines, Errors);
		E.bStructural = true;
		FString Line = FString::Printf(TEXT("added component %s (%s)%s"), *Name, *Class->GetName(), *Where);
		if (Lines.Num() > 0) Line += TEXT(": ") + FString::Join(Lines, TEXT("; "));
		E.Done.Add(Line);
		for (const FString& Err : Errors) E.Errors.Add(FString::Printf(TEXT("%s.%s"), *Name, *Err));
	}

	void RemoveComponent(FEdit& E, const TSharedPtr<FJsonObject>& C)
	{
		const FString Name = ZettaTool::Str(C, TEXT("name")).TrimStartAndEnd();
		ZettaTargets::FBlueprintComponent Found;
		FString Error;
		if (!ZettaTargets::FindBlueprintComponent(E.BP, Name, false, Found, Error))
		{
			E.Errors.Add(TEXT("remove_component: ") + Error);
			return;
		}
		if (Found.bNative || Found.bInherited || !Found.Node)
		{
			E.Errors.Add(FString::Printf(TEXT("remove_component %s: it comes from the parent class, so this Blueprint can't remove it (set its properties instead)."), *Name));
			return;
		}
		E.BP->SimpleConstructionScript->Modify();
		E.BP->SimpleConstructionScript->RemoveNodeAndPromoteChildren(Found.Node);
		E.bStructural = true;
		E.Done.Add(FString::Printf(TEXT("removed component %s"), *Name));
	}

	void SetComponent(FEdit& E, const TSharedPtr<FJsonObject>& C)
	{
		const FString Name = ZettaTool::Str(C, TEXT("name")).TrimStartAndEnd();
		ZettaTargets::FBlueprintComponent Found;
		FString Error;
		if (!ZettaTargets::FindBlueprintComponent(E.BP, Name, true, Found, Error))
		{
			E.Errors.Add(TEXT("set_component: ") + Error);
			return;
		}
		const TSharedPtr<FJsonObject> Props = ZettaTool::Obj(C, TEXT("properties"));
		if (!Props.IsValid())
		{
			E.Errors.Add(FString::Printf(TEXT("set_component %s: 'properties' is required."), *Name));
			return;
		}
		TArray<FString> Lines, Errors;
		ZettaProps::WriteArchetype(Found.Template, Props, Lines, Errors);
		if (Lines.Num() > 0) E.Done.Add(FString::Printf(TEXT("%s: %s"), *Name, *FString::Join(Lines, TEXT("; "))));
		for (const FString& Err : Errors) E.Errors.Add(FString::Printf(TEXT("%s.%s"), *Name, *Err));
	}

	void AddVariable(FEdit& E, const TSharedPtr<FJsonObject>& V)
	{
		const FString Name = ZettaTool::Str(V, TEXT("name")).TrimStartAndEnd();
		if (Name.IsEmpty())
		{
			E.Errors.Add(TEXT("add_variable needs a 'name'."));
			return;
		}
		if (FBlueprintEditorUtils::FindNewVariableIndex(E.BP, FName(*Name)) != INDEX_NONE)
		{
			E.Errors.Add(FString::Printf(TEXT("add_variable: %s already exists (set its default with set_defaults)."), *Name));
			return;
		}
		FEdGraphPinType Type;
		FString Error;
		if (!PinType(ZettaTool::Str(V, TEXT("type")), Type, Error))
		{
			E.Errors.Add(FString::Printf(TEXT("add_variable %s: %s"), *Name, *Error));
			return;
		}
		if (!FBlueprintEditorUtils::AddMemberVariable(E.BP, FName(*Name), Type))
		{
			E.Errors.Add(FString::Printf(TEXT("add_variable %s: Unreal refused the name (it may clash with a parent's property or function)."), *Name));
			return;
		}
		const bool bEditable = ZettaTool::BoolOr(V, TEXT("editable"), true);
		FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(E.BP, FName(*Name), !bEditable);
		const FString Category = ZettaTool::Str(V, TEXT("category")).TrimStartAndEnd();
		if (!Category.IsEmpty()) FBlueprintEditorUtils::SetBlueprintVariableCategory(E.BP, FName(*Name), nullptr, FText::FromString(Category), true);
		if (const TSharedPtr<FJsonValue> Default = ZettaTool::Get(V, TEXT("default"))) E.Defaults->SetField(Name, Default);
		E.bStructural = true;
		E.Done.Add(FString::Printf(TEXT("added variable %s: %s%s%s"), *Name, *UEdGraphSchema_K2::TypeToText(Type).ToString(), bEditable ? TEXT(" (instance editable)") : TEXT(""),
			Category.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" in \"%s\""), *Category)));
	}

	void RemoveVariable(FEdit& E, const TSharedPtr<FJsonObject>& V)
	{
		const FString Name = ZettaTool::Str(V, TEXT("name")).TrimStartAndEnd();
		if (FBlueprintEditorUtils::FindNewVariableIndex(E.BP, FName(*Name)) == INDEX_NONE)
		{
			TArray<FString> Names;
			for (const FBPVariableDescription& D : E.BP->NewVariables) Names.Add(D.VarName.ToString());
			E.Errors.Add(FString::Printf(TEXT("remove_variable: no variable \"%s\". Its variables: %s."), *Name, *ZettaTool::JoinQuoted(Names, 40)));
			return;
		}
		FBlueprintEditorUtils::RemoveMemberVariable(E.BP, FName(*Name));
		E.bStructural = true;
		E.Done.Add(FString::Printf(TEXT("removed variable %s (nodes that used it now show errors until removed)"), *Name));
	}

	void Reparent(FEdit& E, const TSharedPtr<FJsonObject>& C)
	{
		FString Error;
		const bool bActor = E.BP->ParentClass && E.BP->ParentClass->IsChildOf(AActor::StaticClass());
		UClass* Parent = ZettaTargets::FindClass(ZettaTool::Str(C, TEXT("parent")), bActor ? AActor::StaticClass() : UObject::StaticClass(), Error);
		if (!Parent)
		{
			E.Errors.Add(TEXT("reparent: ") + Error);
			return;
		}
		if (!FKismetEditorUtilities::CanCreateBlueprintOfClass(Parent) || Parent == E.BP->GeneratedClass || Parent->IsChildOf(E.BP->GeneratedClass))
		{
			E.Errors.Add(FString::Printf(TEXT("reparent: %s can't be this Blueprint's parent."), *Parent->GetName()));
			return;
		}
		E.BP->Modify();
		E.BP->ParentClass = Parent;
		FBlueprintEditorUtils::RefreshAllNodes(E.BP);
		E.bStructural = true;
		E.Done.Add(FString::Printf(TEXT("reparented to %s"), *Parent->GetName()));
	}

	void Apply(FEdit& E, const TSharedPtr<FJsonObject>& Change)
	{
		const FString Op = ZettaTool::Str(Change, TEXT("op")).TrimStartAndEnd().ToLower();
		if (Op == TEXT("add_component")) AddComponent(E, Change);
		else if (Op == TEXT("remove_component")) RemoveComponent(E, Change);
		else if (Op == TEXT("set_component")) SetComponent(E, Change);
		else if (Op == TEXT("add_variable")) AddVariable(E, Change);
		else if (Op == TEXT("remove_variable")) RemoveVariable(E, Change);
		else if (Op == TEXT("reparent")) Reparent(E, Change);
		else if (Op == TEXT("set_defaults"))
		{
			const TSharedPtr<FJsonObject> Props = ZettaTool::Obj(Change, TEXT("properties"));
			if (!Props.IsValid()) E.Errors.Add(TEXT("set_defaults: 'properties' is required."));
			else for (const auto& Pair : Props->Values) E.Defaults->SetField(FString(Pair.Key), Pair.Value);
		}
		else E.Errors.Add(FString::Printf(TEXT("Unknown op \"%s\" (add_component, remove_component, set_component, add_variable, remove_variable, set_defaults, reparent)."), *Op));
	}

	/** Compiles, writes the class defaults on the fresh class, saves; the summary for the model. */
	FString Finish(FEdit& E, bool& bOutCompiled)
	{
		if (E.bStructural) FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(E.BP);
		else FBlueprintEditorUtils::MarkBlueprintAsModified(E.BP);
		FString Status = ZettaAssets::Compile(E.BP, bOutCompiled);
		if (E.Defaults->Values.Num() > 0)
		{
			UObject* CDO = E.BP->GeneratedClass ? E.BP->GeneratedClass->GetDefaultObject() : nullptr;
			if (!CDO)
			{
				E.Errors.Add(TEXT("The class defaults could not be set: the Blueprint did not compile."));
			}
			else
			{
				TArray<FString> Lines, Errors;
				ZettaProps::WriteArchetype(CDO, E.Defaults, Lines, Errors);
				if (Lines.Num() > 0) E.Done.Add(TEXT("defaults: ") + FString::Join(Lines, TEXT("; ")));
				for (const FString& Err : Errors) E.Errors.Add(TEXT("default ") + Err);
			}
		}
		FString SaveError;
		const bool bSaved = ZettaAssets::Save(E.BP, SaveError);
		FString Text;
		for (const FString& D : E.Done) Text += TEXT("- ") + D + TEXT("\n");
		if (E.Errors.Num() > 0)
		{
			Text += TEXT("Not done:\n");
			for (const FString& Err : E.Errors) Text += TEXT("- ") + Err + TEXT("\n");
		}
		Text += Status;
		Text += bSaved ? TEXT(" Saved.") : TEXT(" Not saved: ") + SaveError;
		return Text;
	}

	// ─── create_blueprint ────────────────────────────────────────────────────

	FZettaToolResult CreateBlueprint(const TSharedPtr<FJsonObject>& In)
	{
		FString Package, Error;
		if (!ZettaTool::WritableContentPath(ZettaTool::Str(In, TEXT("path")), Package, Error)) return Fail(Error);
		if (ZettaTargets::AssetExists(Package)) return Fail(FString::Printf(TEXT("%s already exists. Change it with edit_blueprint, or pick another path."), *Package));
		const FString ParentName = ZettaTool::Str(In, TEXT("parent"), TEXT("Actor"));
		UClass* Parent = ZettaTargets::FindClass(ParentName, UObject::StaticClass(), Error);
		if (!Parent) return Fail(Error);
		if (!FKismetEditorUtilities::CanCreateBlueprintOfClass(Parent)) return Fail(FString::Printf(TEXT("%s can't be a Blueprint's parent (it is not Blueprintable)."), *Parent->GetName()));
		const TArray<TSharedPtr<FJsonValue>> Components = ZettaTool::Arr(In, TEXT("components"));
		const TArray<TSharedPtr<FJsonValue>> Variables = ZettaTool::Arr(In, TEXT("variables"));
		if (Components.Num() > 30 || Variables.Num() > 30) return Fail(TEXT("At most 30 components and 30 variables per call."));

		UClass* BlueprintClass = UBlueprint::StaticClass();
		UClass* GeneratedClass = UBlueprintGeneratedClass::StaticClass();
		IKismetCompilerInterface& Compiler = FModuleManager::LoadModuleChecked<IKismetCompilerInterface>("KismetCompiler");
		Compiler.GetBlueprintTypesForClass(Parent, BlueprintClass, GeneratedClass);

		const FString Name = FPackageName::GetShortName(Package);
		UPackage* Pkg = CreatePackage(*Package);
		UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(Parent, Pkg, FName(*Name), BPTYPE_Normal, BlueprintClass, GeneratedClass, FName(TEXT("ZettaAgent")));
		if (!BP) return Fail(FString::Printf(TEXT("Unreal could not create a Blueprint of %s."), *Parent->GetName()));
		FAssetRegistryModule::AssetCreated(BP);
		Pkg->MarkPackageDirty();
		ZettaAgentChanges::CreatedAsset(Package);

		FEdit E;
		E.BP = BP;
		for (const TSharedPtr<FJsonValue>& C : Components)
		{
			if (C.IsValid() && C->Type == EJson::Object) AddComponent(E, C->AsObject());
		}
		for (const TSharedPtr<FJsonValue>& V : Variables)
		{
			if (V.IsValid() && V->Type == EJson::Object) AddVariable(E, V->AsObject());
		}
		if (const TSharedPtr<FJsonObject> Defaults = ZettaTool::Obj(In, TEXT("defaults")))
		{
			for (const auto& Pair : Defaults->Values) E.Defaults->SetField(FString(Pair.Key), Pair.Value);
		}
		bool bCompiled = false;
		const FString Summary = Finish(E, bCompiled);
		FZettaToolResult R = Ok(FString::Printf(TEXT("Created %s (a %s Blueprint).\n%sPlace it with spawn_actor class \"%s\"."), *Package, *Parent->GetName(), *(Summary + TEXT("\n")),
			*Package), FString::Printf(TEXT("Created %s"), *Name));
		return R;
	}

	FZettaToolResult EditBlueprint(const TSharedPtr<FJsonObject>& In)
	{
		FString Error;
		const FString Path = ZettaTool::Str(In, TEXT("path"));
		UBlueprint* BP = Cast<UBlueprint>(ZettaTargets::LoadAsset(Path, Error, UBlueprint::StaticClass()));
		if (!BP) return Fail(Error);
		const TArray<TSharedPtr<FJsonValue>> Changes = ZettaTool::Arr(In, TEXT("changes"));
		if (Changes.Num() == 0 || Changes.Num() > 30) return Fail(TEXT("'changes' takes 1 to 30 changes."));
		const FString Package = ZettaTool::PackagePath(BP->GetPathName());
		ZettaAgentChanges::WillChangeAsset(Package, TEXT("changed (Blueprint edited)"));

		const FString Title = ZettaTool::TransactionTitle(FString::Printf(TEXT("Edit %s"), *BP->GetName()));
		FScopedTransaction Transaction(FText::FromString(Title));
		BP->Modify();
		FEdit E;
		E.BP = BP;
		for (const TSharedPtr<FJsonValue>& C : Changes)
		{
			if (C.IsValid() && C->Type == EJson::Object) Apply(E, C->AsObject());
			else E.Errors.Add(TEXT("Each change is an object with an 'op'."));
		}
		if (E.Done.Num() == 0 && E.Defaults->Values.Num() == 0)
		{
			Transaction.Cancel();
			return Fail(FString::Printf(TEXT("Nothing changed in %s. %s"), *Package, *FString::Join(E.Errors, TEXT(" "))));
		}
		bool bCompiled = false;
		const FString Summary = Finish(E, bCompiled);
		return Ok(FString::Printf(TEXT("%s:\n%s"), *Package, *Summary), FString::Printf(TEXT("Edited %s"), *BP->GetName()));
	}

	FZettaToolPreview DescribeEdit(const TSharedPtr<FJsonObject>& In)
	{
		TArray<FString> Lines;
		for (const TSharedPtr<FJsonValue>& C : ZettaTool::Arr(In, TEXT("changes")))
		{
			if (!C.IsValid() || C->Type != EJson::Object) continue;
			const TSharedPtr<FJsonObject> O = C->AsObject();
			const FString Op = ZettaTool::Str(O, TEXT("op"));
			FString What = ZettaTool::Str(O, TEXT("name"), ZettaTool::Str(O, TEXT("parent")));
			if (Op == TEXT("add_component") || Op == TEXT("add_variable")) What += TEXT(" (") + ZettaTool::Str(O, TEXT("type")) + TEXT(")");
			Lines.Add(FString::Printf(TEXT("%s %s"), *Op.Replace(TEXT("_"), TEXT(" ")), *What).TrimEnd());
		}
		return ZettaTool::Preview(FString::Printf(TEXT("Edit %s"), *FPackageName::GetShortName(ZettaTool::PackagePath(ZettaTool::Str(In, TEXT("path"))))), FString::Join(Lines, TEXT("\n")));
	}

	// ─── Describe (inspect_asset) ────────────────────────────────────────────

	void DescribeNode(const USCS_Node* Node, int32 Level, bool bAll, TArray<FString>& Out)
	{
		if (!Node || !Node->ComponentTemplate) return;
		FString Line = FString::ChrN(Level * 2, TEXT(' ')) + FString::Printf(TEXT("- %s (%s)"), *Node->GetVariableName().ToString(), *Node->ComponentTemplate->GetClass()->GetName());
		if (!Node->ParentComponentOrVariableName.IsNone()) Line += FString::Printf(TEXT(" attached to %s"), *Node->ParentComponentOrVariableName.ToString());
		Out.Add(Line);
		for (const FString& L : ZettaProps::Lines(Node->ComponentTemplate, bAll, bAll ? 150 : 40)) Out.Add(FString::ChrN(Level * 2 + 4, TEXT(' ')) + L);
		for (const USCS_Node* Child : Node->GetChildNodes()) DescribeNode(Child, Level + 1, bAll, Out);
	}
}

namespace ZettaAssets
{
	FString DescribeBlueprint(UBlueprint* BP, bool bAll)
	{
		using namespace ZettaBlueprintImpl;
		TArray<FString> Out;
		Out.Add(FString::Printf(TEXT("%s — Blueprint, parent %s (%s)"), *ZettaTool::PackagePath(BP->GetPathName()), BP->ParentClass ? *BP->ParentClass->GetName() : TEXT("none"),
			BP->ParentClass ? *BP->ParentClass->GetPathName() : TEXT("")));
		const TCHAR* Status = BP->Status == BS_Error ? TEXT("has compile errors") : BP->Status == BS_UpToDate || BP->Status == BS_UpToDateWithWarnings ? TEXT("compiled") : TEXT("not compiled since its last change");
		Out.Add(FString::Printf(TEXT("Status: %s%s"), Status, BP->Status == BS_UpToDateWithWarnings ? TEXT(" (with warnings)") : TEXT("")));
		for (const FBPInterfaceDescription& I : BP->ImplementedInterfaces)
		{
			if (I.Interface) Out.Add(TEXT("Implements ") + I.Interface->GetName());
		}

		if (BP->GeneratedClass && BP->GeneratedClass->IsChildOf(AActor::StaticClass()))
		{
			Out.Add(TEXT("Components:"));
			if (AActor* CDO = Cast<AActor>(BP->GeneratedClass->GetDefaultObject()))
			{
				TInlineComponentArray<UActorComponent*> Native(CDO);
				for (UActorComponent* C : Native)
				{
					if (!C) continue;
					FString Line = FString::Printf(TEXT("- %s (%s, from the parent class%s)"), *C->GetName(), *C->GetClass()->GetName(), CDO->GetRootComponent() == C ? TEXT(", root") : TEXT(""));
					Out.Add(Line);
					for (const FString& L : ZettaProps::Lines(C, bAll, bAll ? 150 : 40)) Out.Add(TEXT("    ") + L);
				}
			}
			// Components a parent Blueprint added.
			for (UClass* C = BP->ParentClass; C; C = C->GetSuperClass())
			{
				UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(C);
				if (!BPGC || !BPGC->SimpleConstructionScript) continue;
				for (const USCS_Node* Node : BPGC->SimpleConstructionScript->GetAllNodes())
				{
					if (Node && Node->ComponentTemplate) Out.Add(FString::Printf(TEXT("- %s (%s, from %s)"), *Node->GetVariableName().ToString(), *Node->ComponentTemplate->GetClass()->GetName(), *BPGC->GetName()));
				}
			}
			if (BP->SimpleConstructionScript)
			{
				for (const USCS_Node* Root : BP->SimpleConstructionScript->GetRootNodes()) DescribeNode(Root, 0, bAll, Out);
			}
		}

		if (BP->NewVariables.Num() > 0)
		{
			Out.Add(TEXT("Variables:"));
			UObject* CDO = BP->GeneratedClass ? BP->GeneratedClass->GetDefaultObject() : nullptr;
			for (const FBPVariableDescription& V : BP->NewVariables)
			{
				FString Line = FString::Printf(TEXT("- %s: %s"), *V.VarName.ToString(), *UEdGraphSchema_K2::TypeToText(V.VarType).ToString());
				if (CDO)
				{
					if (const FProperty* P = FindFProperty<FProperty>(BP->GeneratedClass, V.VarName)) Line += TEXT(" = ") + ZettaProps::Text(P, P->ContainerPtrToValuePtr<void>(CDO), 160);
				}
				TArray<FString> Flags;
				if (!(V.PropertyFlags & CPF_DisableEditOnInstance)) Flags.Add(TEXT("instance editable"));
				if (V.PropertyFlags & CPF_BlueprintReadOnly) Flags.Add(TEXT("read-only"));
				if (V.PropertyFlags & CPF_Net) Flags.Add(TEXT("replicated"));
				if (!V.Category.IsEmpty() && !V.Category.EqualTo(UEdGraphSchema_K2::VR_DefaultCategory)) Flags.Add(TEXT("category \"") + V.Category.ToString() + TEXT("\""));
				if (Flags.Num() > 0) Line += TEXT(" (") + FString::Join(Flags, TEXT(", ")) + TEXT(")");
				Out.Add(Line);
			}
		}

		if (BP->FunctionGraphs.Num() > 0)
		{
			TArray<FString> Functions;
			for (const UEdGraph* G : BP->FunctionGraphs)
			{
				if (!G || G->GetFName() == UEdGraphSchema_K2::FN_UserConstructionScript) continue;
				FString Signature = G->GetName();
				if (BP->GeneratedClass)
				{
					if (const UFunction* F = BP->GeneratedClass->FindFunctionByName(G->GetFName()))
					{
						TArray<FString> Params;
						FString Return;
						for (TFieldIterator<FProperty> It(F); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
						{
							if (It->HasAnyPropertyFlags(CPF_ReturnParm) || (It->HasAnyPropertyFlags(CPF_OutParm) && !It->HasAnyPropertyFlags(CPF_ReferenceParm))) Return += (Return.IsEmpty() ? TEXT("") : TEXT(", ")) + ZettaProps::TypeName(*It) + TEXT(" ") + It->GetName();
							else Params.Add(ZettaProps::TypeName(*It) + TEXT(" ") + It->GetName());
						}
						Signature = FString::Printf(TEXT("%s(%s)%s"), *G->GetName(), *FString::Join(Params, TEXT(", ")), Return.IsEmpty() ? TEXT("") : *(TEXT(" → ") + Return));
					}
				}
				Functions.Add(FString::Printf(TEXT("%s [%d nodes]"), *Signature, G->Nodes.Num()));
			}
			if (Functions.Num() > 0) Out.Add(TEXT("Functions: ") + FString::Join(Functions, TEXT("; ")));
		}
		int32 Nodes = 0;
		TArray<FString> Events;
		for (const UEdGraph* G : BP->UbergraphPages)
		{
			if (!G) continue;
			Nodes += G->Nodes.Num();
			for (const UEdGraphNode* N : G->Nodes)
			{
				if (const UK2Node_Event* Ev = Cast<UK2Node_Event>(N)) Events.Add(Ev->GetNodeTitle(ENodeTitleType::ListView).ToString());
			}
		}
		Out.Add(FString::Printf(TEXT("Event graph: %d node(s)%s"), Nodes, Events.Num() > 0 ? *(TEXT(", events: ") + FString::Join(Events, TEXT(", "))) : TEXT("")));
		if (BP->MacroGraphs.Num() > 0) Out.Add(FString::Printf(TEXT("Macros: %d"), BP->MacroGraphs.Num()));
		if (!bAll) Out.Add(TEXT("(Component properties are those changed from their defaults; all: true lists every one.)"));
		return FString::Join(Out, TEXT("\n"));
	}
}

void ZettaToolAreas::RegisterBlueprintTools(FZettaAgentTools& Tools)
{
	using namespace ZettaBlueprintImpl;
	Tools.Register(TEXT("create_blueprint"), &CreateBlueprint, [](const TSharedPtr<FJsonObject>& In)
	{
		TArray<FString> Lines;
		Lines.Add(TEXT("Parent: ") + ZettaTool::Str(In, TEXT("parent"), TEXT("Actor")));
		const int32 Components = ZettaTool::Arr(In, TEXT("components")).Num();
		const int32 Variables = ZettaTool::Arr(In, TEXT("variables")).Num();
		if (Components > 0) Lines.Add(ZettaTool::Plural(Components, TEXT("component"), TEXT("components")));
		if (Variables > 0) Lines.Add(ZettaTool::Plural(Variables, TEXT("variable"), TEXT("variables")));
		return ZettaTool::Preview(FString::Printf(TEXT("Create %s"), *FPackageName::GetShortName(ZettaTool::PackagePath(ZettaTool::Str(In, TEXT("path"))))), FString::Join(Lines, TEXT("\n")));
	});
	Tools.Register(TEXT("edit_blueprint"), &EditBlueprint, &DescribeEdit);
}
