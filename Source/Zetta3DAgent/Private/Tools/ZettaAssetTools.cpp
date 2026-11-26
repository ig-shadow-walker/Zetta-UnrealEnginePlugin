// Copyright Zetta 3D. See LICENSE.md.
// Assets: find_assets, inspect_asset, asset_operation, import_file, delete_asset.

#include "AssetImportTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "IAssetTools.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/Skeleton.h"
#include "UObject/ObjectRedirector.h"
#include "ZettaAgentTools.h"
#include "ZettaAssetUtil.h"
#include "ZettaProperties.h"
#include "ZettaTargets.h"
#include "ZettaToolCommon.h"

namespace ZettaAssetImpl
{
	using ZettaTool::Fail;
	using ZettaTool::Ok;

	IAssetRegistry& Registry() { return FAssetRegistryModule::GetRegistry(); }

	FString ClassName(const FAssetData& A) { return A.AssetClassPath.GetAssetName().ToString(); }

	/** Packages that depend on this one (hard or soft), itself excluded. */
	TArray<FString> Referencers(const FString& Package)
	{
		TArray<FName> Names;
		Registry().GetReferencers(FName(*Package), Names, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::NoRequirements);
		TArray<FString> Out;
		for (const FName& N : Names)
		{
			if (N.ToString() != Package) Out.Add(N.ToString());
		}
		Out.Sort();
		return Out;
	}

	// ─── find_assets ─────────────────────────────────────────────────────────

	FZettaToolResult FindAssets(const TSharedPtr<FJsonObject>& In)
	{
		const FString Query = ZettaTool::Str(In, TEXT("query")).TrimStartAndEnd();
		const FString ClassName = ZettaTool::Str(In, TEXT("class")).TrimStartAndEnd();
		FString Path = ZettaTool::PackagePath(ZettaTool::Str(In, TEXT("path"), TEXT("/Game")));
		if (Path.IsEmpty() || !Path.StartsWith(TEXT("/"))) Path = TEXT("/Game");
		const int32 Limit = ZettaTool::IntIn(In, TEXT("limit"), 50, 1, 200);

		FARFilter Filter;
		Filter.bRecursivePaths = true;
		Filter.PackagePaths.Add(FName(*Path));
		if (!ClassName.IsEmpty())
		{
			FString Error;
			UClass* Class = ZettaTargets::FindClass(ClassName, UObject::StaticClass(), Error);
			if (!Class) return Fail(Error);
			Filter.ClassPaths.Add(Class->GetClassPathName());
			Filter.bRecursiveClasses = true;
		}
		TArray<FAssetData> Assets;
		Registry().GetAssets(Filter, Assets);
		// A path that is itself an asset ("/Game/Props/SM_Chair") lists just that one.
		if (Assets.Num() == 0 && ClassName.IsEmpty()) Registry().GetAssetsByPackageName(FName(*Path), Assets);
		if (!Query.IsEmpty())
		{
			Assets.RemoveAll([&](const FAssetData& A) { return !A.AssetName.ToString().Contains(Query) && !A.PackageName.ToString().Contains(Query); });
		}
		Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });
		if (Assets.Num() == 0)
		{
			return Ok(FString::Printf(TEXT("No assets match under %s%s%s."), *Path, Query.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" named like \"%s\""), *Query),
				ClassName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" of class %s"), *ClassName)));
		}
		TArray<FString> Lines;
		for (int32 i = 0; i < Assets.Num() && i < Limit; ++i)
		{
			const FAssetData& A = Assets[i];
			FString Line = FString::Printf(TEXT("%s (%s"), *A.PackageName.ToString(), *ZettaAssetImpl::ClassName(A));
			FString Parent;
			if (A.GetTagValue(FName(TEXT("ParentClass")), Parent) && !Parent.IsEmpty())
			{
				Parent = FPackageName::ObjectPathToObjectName(Parent);
				Parent.RemoveFromEnd(TEXT("'"));
				Parent.RemoveFromEnd(TEXT("_C"));
				Line += TEXT(", parent ") + Parent;
			}
			Lines.Add(Line + TEXT(")"));
		}
		FString Text = FString::Printf(TEXT("%d asset(s)%s:\n%s"), Assets.Num(), Assets.Num() > Limit ? *FString::Printf(TEXT(", first %d"), Limit) : TEXT(""), *FString::Join(Lines, TEXT("\n")));
		return Ok(Text);
	}

	// ─── inspect_asset ───────────────────────────────────────────────────────

	void MaterialParameters(const UMaterialInterface* M, TArray<FString>& Out)
	{
		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Ids;
		M->GetAllScalarParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& I : Infos)
		{
			float V = 0;
			if (M->GetScalarParameterValue(I, V)) Out.Add(FString::Printf(TEXT("  scalar %s = %s"), *I.Name.ToString(), *ZettaTool::Num(V)));
		}
		M->GetAllVectorParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& I : Infos)
		{
			FLinearColor V;
			if (M->GetVectorParameterValue(I, V)) Out.Add(FString::Printf(TEXT("  vector %s = \"#%s\" (linear %s, %s, %s, %s)"), *I.Name.ToString(), *V.ToFColor(true).ToHex(),
				*ZettaTool::Num(V.R), *ZettaTool::Num(V.G), *ZettaTool::Num(V.B), *ZettaTool::Num(V.A)));
		}
		M->GetAllTextureParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& I : Infos)
		{
			UTexture* T = nullptr;
			if (M->GetTextureParameterValue(I, T)) Out.Add(FString::Printf(TEXT("  texture %s = %s"), *I.Name.ToString(), T ? *ZettaTool::PackagePath(T->GetPathName()) : TEXT("None")));
		}
		M->GetAllStaticSwitchParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& I : Infos)
		{
			bool V = false;
			FGuid Guid;
			if (M->GetStaticSwitchParameterValue(I, V, Guid)) Out.Add(FString::Printf(TEXT("  switch %s = %s"), *I.Name.ToString(), V ? TEXT("true") : TEXT("false")));
		}
	}

	FString DescribeMaterial(UMaterialInterface* MI, bool bAll)
	{
		TArray<FString> Out;
		if (UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(MI))
		{
			Out.Add(FString::Printf(TEXT("%s — Material Instance of %s"), *ZettaTool::PackagePath(MI->GetPathName()), Instance->Parent ? *ZettaTool::PackagePath(Instance->Parent->GetPathName()) : TEXT("nothing")));
			TArray<FString> Overrides;
			for (const FScalarParameterValue& P : Instance->ScalarParameterValues) Overrides.Add(P.ParameterInfo.Name.ToString());
			for (const FVectorParameterValue& P : Instance->VectorParameterValues) Overrides.Add(P.ParameterInfo.Name.ToString());
			for (const FTextureParameterValue& P : Instance->TextureParameterValues) Overrides.Add(P.ParameterInfo.Name.ToString());
			Out.Add(Overrides.Num() > 0 ? TEXT("Overrides: ") + FString::Join(Overrides, TEXT(", ")) : TEXT("Overrides nothing (it looks exactly like its parent)."));
		}
		else if (UMaterial* M = Cast<UMaterial>(MI))
		{
			Out.Add(FString::Printf(TEXT("%s — Material"), *ZettaTool::PackagePath(MI->GetPathName())));
			Out.Add(FString::Printf(TEXT("Domain %s · blend %s · shading %s · %s · %d expression node(s)"),
				*StaticEnum<EMaterialDomain>()->GetNameStringByValue(M->MaterialDomain), *StaticEnum<EBlendMode>()->GetNameStringByValue(M->BlendMode),
				*StaticEnum<EMaterialShadingModel>()->GetNameStringByValue(M->GetShadingModels().GetFirstShadingModel()), M->IsTwoSided() ? TEXT("two-sided") : TEXT("one-sided"),
				M->GetExpressions().Num()));
			TArray<UTexture*> Textures;
			M->GetUsedTextures(Textures);
			TArray<FString> Names;
			for (const UTexture* T : Textures)
			{
				if (T) Names.Add(ZettaTool::PackagePath(T->GetPathName()));
			}
			if (Names.Num() > 0) Out.Add(TEXT("Textures: ") + FString::Join(Names, TEXT(", ")));
		}
		else
		{
			Out.Add(FString::Printf(TEXT("%s — %s"), *ZettaTool::PackagePath(MI->GetPathName()), *MI->GetClass()->GetName()));
		}
		TArray<FString> Params;
		MaterialParameters(MI, Params);
		Out.Add(Params.Num() > 0 ? TEXT("Parameters:") : TEXT("No parameters (a Material Instance of it can't change anything)."));
		Out.Append(Params);
		if (bAll)
		{
			Out.Add(TEXT("Properties:"));
			for (const FString& L : ZettaProps::Lines(MI, true, 120)) Out.Add(TEXT("  ") + L);
		}
		return FString::Join(Out, TEXT("\n"));
	}

	FString DescribeStaticMesh(UStaticMesh* Mesh, bool bAll)
	{
		TArray<FString> Out;
		Out.Add(FString::Printf(TEXT("%s — Static Mesh"), *ZettaTool::PackagePath(Mesh->GetPathName())));
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		Out.Add(FString::Printf(TEXT("%s triangles, %s vertices (LOD 0) · %d LOD(s) · Nanite %s · size %s cm"), *FString::FormatAsNumber(Mesh->GetNumTriangles(0)),
			*FString::FormatAsNumber(Mesh->GetNumVertices(0)), Mesh->GetNumLODs(), Mesh->IsNaniteEnabled() ? TEXT("on") : TEXT("off"), *ZettaTool::Vec(Bounds.BoxExtent * 2)));
		TArray<FString> Slots;
		const TArray<FStaticMaterial>& Materials = Mesh->GetStaticMaterials();
		for (int32 i = 0; i < Materials.Num(); ++i)
		{
			Slots.Add(FString::Printf(TEXT("[%d] %s = %s"), i, *Materials[i].MaterialSlotName.ToString(),
				Materials[i].MaterialInterface ? *ZettaTool::PackagePath(Materials[i].MaterialInterface->GetPathName()) : TEXT("None")));
		}
		Out.Add(TEXT("Material slots: ") + (Slots.Num() > 0 ? FString::Join(Slots, TEXT("; ")) : FString(TEXT("none"))));
		if (const UBodySetup* Body = Mesh->GetBodySetup())
		{
			const FKAggregateGeom& G = Body->AggGeom;
			Out.Add(FString::Printf(TEXT("Collision: %d box, %d sphere, %d capsule, %d convex%s"), G.BoxElems.Num(), G.SphereElems.Num(), G.SphylElems.Num(), G.ConvexElems.Num(),
				Body->CollisionTraceFlag == CTF_UseComplexAsSimple ? TEXT(" (uses the mesh itself as simple collision)") : G.GetElementCount() == 0 ? TEXT(" (none: add simple collision for physics)") : TEXT("")));
		}
		else
		{
			Out.Add(TEXT("Collision: none"));
		}
		if (Mesh->Sockets.Num() > 0)
		{
			TArray<FString> Sockets;
			for (const UStaticMeshSocket* S : Mesh->Sockets)
			{
				if (S) Sockets.Add(S->SocketName.ToString());
			}
			Out.Add(TEXT("Sockets: ") + FString::Join(Sockets, TEXT(", ")));
		}
		if (bAll)
		{
			Out.Add(TEXT("Properties:"));
			for (const FString& L : ZettaProps::Lines(Mesh, true, 120)) Out.Add(TEXT("  ") + L);
		}
		return FString::Join(Out, TEXT("\n"));
	}

	FString DescribeSkeletalMesh(USkeletalMesh* Mesh, bool bAll)
	{
		TArray<FString> Out;
		Out.Add(FString::Printf(TEXT("%s — Skeletal Mesh"), *ZettaTool::PackagePath(Mesh->GetPathName())));
		int32 Triangles = 0;
		if (const FSkeletalMeshRenderData* Render = Mesh->GetResourceForRendering())
		{
			if (Render->LODRenderData.Num() > 0) Triangles = (int32)Render->LODRenderData[0].GetTotalFaces();
		}
		Out.Add(FString::Printf(TEXT("%s triangles (LOD 0) · %d LOD(s) · %d bones · size %s cm"), *FString::FormatAsNumber(Triangles), Mesh->GetLODNum(), Mesh->GetRefSkeleton().GetNum(),
			*ZettaTool::Vec(Mesh->GetBounds().BoxExtent * 2)));
		Out.Add(FString::Printf(TEXT("Skeleton: %s · Physics asset: %s"), Mesh->GetSkeleton() ? *ZettaTool::PackagePath(Mesh->GetSkeleton()->GetPathName()) : TEXT("none"),
			Mesh->GetPhysicsAsset() ? *ZettaTool::PackagePath(Mesh->GetPhysicsAsset()->GetPathName()) : TEXT("none")));
		TArray<FString> Slots;
		const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
		for (int32 i = 0; i < Materials.Num(); ++i)
		{
			Slots.Add(FString::Printf(TEXT("[%d] %s = %s"), i, *Materials[i].MaterialSlotName.ToString(),
				Materials[i].MaterialInterface ? *ZettaTool::PackagePath(Materials[i].MaterialInterface->GetPathName()) : TEXT("None")));
		}
		Out.Add(TEXT("Material slots: ") + (Slots.Num() > 0 ? FString::Join(Slots, TEXT("; ")) : FString(TEXT("none"))));
		TArray<FString> Sockets;
		for (int32 i = 0; i < Mesh->NumSockets(); ++i)
		{
			if (const USkeletalMeshSocket* S = Mesh->GetSocketByIndex(i)) Sockets.Add(FString::Printf(TEXT("%s (on %s)"), *S->SocketName.ToString(), *S->BoneName.ToString()));
		}
		if (Sockets.Num() > 0) Out.Add(TEXT("Sockets: ") + FString::Join(Sockets, TEXT(", ")));
		if (bAll)
		{
			TArray<FString> Bones;
			const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
			for (int32 i = 0; i < Ref.GetNum() && i < 300; ++i) Bones.Add(Ref.GetBoneName(i).ToString());
			Out.Add(TEXT("Bones: ") + FString::Join(Bones, TEXT(", ")));
		}
		return FString::Join(Out, TEXT("\n"));
	}

	FString DescribeTexture(UTexture* Texture, bool bAll)
	{
		TArray<FString> Out;
		Out.Add(FString::Printf(TEXT("%s — %s"), *ZettaTool::PackagePath(Texture->GetPathName()), *Texture->GetClass()->GetName()));
		if (UTexture2D* T2 = Cast<UTexture2D>(Texture))
		{
			Out.Add(FString::Printf(TEXT("%d × %d (source %d × %d) · %d mip(s)"), T2->GetSizeX(), T2->GetSizeY(), (int32)T2->Source.GetSizeX(), (int32)T2->Source.GetSizeY(), T2->GetNumMips()));
		}
		Out.Add(FString::Printf(TEXT("Compression %s · sRGB %s · group %s%s"), *StaticEnum<TextureCompressionSettings>()->GetNameStringByValue(Texture->CompressionSettings),
			Texture->SRGB ? TEXT("on") : TEXT("off"), *StaticEnum<TextureGroup>()->GetNameStringByValue(Texture->LODGroup), Texture->VirtualTextureStreaming ? TEXT(" · virtual texture") : TEXT("")));
		if (bAll)
		{
			for (const FString& L : ZettaProps::Lines(Texture, true, 120)) Out.Add(TEXT("  ") + L);
		}
		return FString::Join(Out, TEXT("\n"));
	}

	FZettaToolResult InspectAsset(const TSharedPtr<FJsonObject>& In)
	{
		FString Error;
		UObject* Asset = ZettaTargets::LoadAsset(ZettaTool::Str(In, TEXT("path")), Error);
		if (!Asset) return Fail(Error);
		const bool bAll = ZettaTool::BoolOr(In, TEXT("all"), false);
		FString Text;
		if (UBlueprint* BP = Cast<UBlueprint>(Asset)) Text = ZettaAssets::DescribeBlueprint(BP, bAll);
		else if (UMaterialInterface* M = Cast<UMaterialInterface>(Asset)) Text = DescribeMaterial(M, bAll);
		else if (UStaticMesh* SM = Cast<UStaticMesh>(Asset)) Text = DescribeStaticMesh(SM, bAll);
		else if (USkeletalMesh* SK = Cast<USkeletalMesh>(Asset)) Text = DescribeSkeletalMesh(SK, bAll);
		else if (UTexture* T = Cast<UTexture>(Asset)) Text = DescribeTexture(T, bAll);
		else if (UWorld* W = Cast<UWorld>(Asset)) Text = FString::Printf(TEXT("%s — Level. Open it with level_operation open to see what is in it."), *ZettaTool::PackagePath(W->GetPathName()));
		else
		{
			const TArray<FString> Lines = ZettaProps::Lines(Asset, bAll, 150);
			Text = FString::Printf(TEXT("%s — %s (%s)\n%s\n%s"), *ZettaTool::PackagePath(Asset->GetPathName()), *Asset->GetClass()->GetName(), *Asset->GetClass()->GetPathName(),
				bAll ? TEXT("Properties:") : Lines.Num() > 0 ? TEXT("Properties changed from the defaults:") : TEXT("All properties are at their defaults (all: true lists them)."),
				*FString::Join(Lines, TEXT("\n")));
		}
		const TArray<FString> Users = Referencers(ZettaTool::PackagePath(Asset->GetPathName()));
		if (Users.Num() > 0) Text += FString::Printf(TEXT("\nUsed by %d asset(s)/level(s): %s"), Users.Num(), *ZettaTool::JoinQuoted(Users, 10));
		return Ok(Text);
	}

	// ─── asset_operation ─────────────────────────────────────────────────────

	FZettaToolResult MoveOrRename(const FString& Action, const TSharedPtr<FJsonObject>& In)
	{
		FString Error;
		UObject* Asset = ZettaTargets::LoadAsset(ZettaTool::Str(In, TEXT("from"), ZettaTool::Str(In, TEXT("path"))), Error);
		if (!Asset) return Fail(Error);
		const FString From = ZettaTool::PackagePath(Asset->GetPathName());
		FString To = ZettaTool::Str(In, TEXT("to")).TrimStartAndEnd();
		if (To.IsEmpty()) return Fail(FString::Printf(TEXT("%s needs 'to': the new content path (or, for a rename, just the new name)."), *Action));
		if (!To.StartsWith(TEXT("/"))) To = FPackageName::GetLongPackagePath(From) + TEXT("/") + To;
		else if (To.EndsWith(TEXT("/")) || UEditorAssetLibrary::DoesDirectoryExist(To))
		{
			// A folder: the asset keeps its name.
			while (To.EndsWith(TEXT("/"))) To.LeftChopInline(1);
			To += TEXT("/") + FPackageName::GetShortName(From);
		}
		FString Package;
		if (!ZettaTool::WritableContentPath(To, Package, Error)) return Fail(Error);
		if (ZettaTargets::AssetExists(Package)) return Fail(FString::Printf(TEXT("%s already exists."), *Package));

		ZettaAgentChanges::WillChangeAsset(From, TEXT("moved to ") + Package);
		IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		TArray<FAssetRenameData> Renames;
		Renames.Emplace(Asset, FPackageName::GetLongPackagePath(Package), FPackageName::GetShortName(Package));
		if (!Tools.RenameAssets(Renames)) return Fail(FString::Printf(TEXT("Unreal could not %s %s (is it read-only, or open in an editor that blocks it?)."), *Action, *From));
		// Point everything that used the old path at the new one and remove the redirector left behind.
		int32 Fixed = 0;
		if (UObjectRedirector* Redirector = FindObject<UObjectRedirector>(nullptr, *ZettaTool::ObjectPath(From)))
		{
			Tools.FixupReferencers({ Redirector }, false);
			Fixed = 1;
		}
		ZettaAgentChanges::CreatedAsset(Package);
		return Ok(FString::Printf(TEXT("%s %s → %s.%s"), Action == TEXT("rename") ? TEXT("Renamed") : TEXT("Moved"), *From, *Package,
			Fixed ? TEXT(" References were updated and the redirector removed.") : TEXT("")), FString::Printf(TEXT("%s %s"), Action == TEXT("rename") ? TEXT("Renamed") : TEXT("Moved"), *FPackageName::GetShortName(From)));
	}

	FZettaToolResult AssetOperation(const TSharedPtr<FJsonObject>& In)
	{
		const FString Action = ZettaTool::Str(In, TEXT("action")).TrimStartAndEnd().ToLower();
		FString Error, Package;
		if (Action == TEXT("move") || Action == TEXT("rename")) return MoveOrRename(Action, In);
		if (Action == TEXT("duplicate"))
		{
			UObject* Asset = ZettaTargets::LoadAsset(ZettaTool::Str(In, TEXT("from"), ZettaTool::Str(In, TEXT("path"))), Error);
			if (!Asset) return Fail(Error);
			const FString From = ZettaTool::PackagePath(Asset->GetPathName());
			FString To = ZettaTool::Str(In, TEXT("to")).TrimStartAndEnd();
			if (To.IsEmpty()) return Fail(TEXT("duplicate needs 'to': the copy's content path."));
			if (!To.StartsWith(TEXT("/"))) To = FPackageName::GetLongPackagePath(From) + TEXT("/") + To;
			if (!ZettaTool::WritableContentPath(To, Package, Error)) return Fail(Error);
			if (ZettaTargets::AssetExists(Package)) return Fail(FString::Printf(TEXT("%s already exists."), *Package));
			if (!UEditorAssetLibrary::DuplicateAsset(From, Package)) return Fail(FString::Printf(TEXT("Unreal could not duplicate %s."), *From));
			UEditorAssetLibrary::SaveAsset(Package, false);
			ZettaAgentChanges::CreatedAsset(Package);
			return Ok(FString::Printf(TEXT("Duplicated %s as %s."), *From, *Package), FString::Printf(TEXT("Duplicated %s"), *FPackageName::GetShortName(From)));
		}
		if (Action == TEXT("create_folder"))
		{
			if (!ZettaTool::WritableContentPath(ZettaTool::Str(In, TEXT("path"), ZettaTool::Str(In, TEXT("to"))), Package, Error)) return Fail(Error);
			if (UEditorAssetLibrary::DoesDirectoryExist(Package)) return Ok(FString::Printf(TEXT("%s already exists."), *Package));
			if (!UEditorAssetLibrary::MakeDirectory(Package)) return Fail(FString::Printf(TEXT("Unreal could not make the folder %s."), *Package));
			return Ok(FString::Printf(TEXT("Made the folder %s."), *Package), TEXT("New folder"));
		}
		if (Action == TEXT("save"))
		{
			const FString Path = ZettaTool::PackagePath(ZettaTool::Str(In, TEXT("path"), ZettaTool::Str(In, TEXT("from"))));
			if (Path.IsEmpty()) return Fail(TEXT("save needs a 'path': an asset or a folder."));
			if (ZettaTargets::AssetExists(Path))
			{
				if (!UEditorAssetLibrary::SaveAsset(Path, false)) return Fail(FString::Printf(TEXT("Unreal could not save %s."), *Path));
				return Ok(FString::Printf(TEXT("Saved %s."), *Path), TEXT("Saved"));
			}
			if (UEditorAssetLibrary::DoesDirectoryExist(Path))
			{
				if (!UEditorAssetLibrary::SaveDirectory(Path, true, true)) return Fail(FString::Printf(TEXT("Some assets under %s could not be saved."), *Path));
				return Ok(FString::Printf(TEXT("Saved the unsaved assets under %s."), *Path), TEXT("Saved"));
			}
			return Fail(FString::Printf(TEXT("No asset or folder at %s."), *Path));
		}
		return Fail(TEXT("'action' must be move, rename, duplicate, create_folder or save."));
	}

	// ─── import_file ─────────────────────────────────────────────────────────

	FZettaToolResult ImportFile(const TSharedPtr<FJsonObject>& In)
	{
		FString File, Error, Folder;
		if (!ZettaTool::ReadableFile(ZettaTool::Str(In, TEXT("path")), File, Error)) return Fail(Error);
		if (!ZettaTool::WritableContentPath(ZettaTool::Str(In, TEXT("destination"), TEXT("/Game/Imported")), Folder, Error)) return Fail(Error);
		const FString Name = ZettaTool::Str(In, TEXT("name")).TrimStartAndEnd();
		const FString Planned = Folder + TEXT("/") + (Name.IsEmpty() ? ObjectTools::SanitizeObjectName(FPaths::GetBaseFilename(File)) : Name);
		if (ZettaTargets::AssetExists(Planned)) return Fail(FString::Printf(TEXT("%s already exists. Give another 'name' or 'destination'."), *Planned));

		IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->AddToRoot();
		Task->Filename = File;
		Task->DestinationPath = Folder;
		if (!Name.IsEmpty()) Task->DestinationName = Name;
		Task->bAutomated = true;
		Task->bReplaceExisting = false;
		Task->bSave = true;
		Tools.ImportAssetTasks({ Task });
		TArray<FString> Made;
		for (UObject* O : Task->GetObjects())
		{
			if (!O) continue;
			const FString P = ZettaTool::PackagePath(O->GetPathName());
			Made.AddUnique(FString::Printf(TEXT("%s (%s)"), *P, *O->GetClass()->GetName()));
			ZettaAgentChanges::CreatedAsset(P);
		}
		Task->RemoveFromRoot();
		if (Made.Num() == 0) return Fail(FString::Printf(TEXT("Unreal could not import %s (no importer for .%s, or the file is damaged). The Output Log says why."), *FPaths::GetCleanFilename(File), *FPaths::GetExtension(File)));
		return Ok(FString::Printf(TEXT("Imported %s:\n%s"), *FPaths::GetCleanFilename(File), *FString::Join(Made, TEXT("\n"))), FString::Printf(TEXT("Imported %s"), *FPaths::GetCleanFilename(File)));
	}

	// ─── delete_asset ────────────────────────────────────────────────────────

	FZettaToolResult DeleteAsset(const TSharedPtr<FJsonObject>& In)
	{
		FString Error;
		UObject* Asset = ZettaTargets::LoadAsset(ZettaTool::Str(In, TEXT("path")), Error);
		if (!Asset) return Fail(Error);
		const FString Package = ZettaTool::PackagePath(Asset->GetPathName());
		FString Writable;
		if (!ZettaTool::WritableContentPath(Package, Writable, Error)) return Fail(Error);
		const bool bForce = ZettaTool::BoolOr(In, TEXT("force"), false);
		const TArray<FString> Users = Referencers(Package);
		bool bInMemory = false, bByUndo = false;
		ObjectTools::GatherObjectReferencersForDeletion(Asset, bInMemory, bByUndo);
		if (!bForce && (Users.Num() > 0 || bInMemory))
		{
			FString Who = Users.Num() > 0 ? FString::Printf(TEXT("%s use it: %s"), *ZettaTool::Plural(Users.Num(), TEXT("asset"), TEXT("assets")), *ZettaTool::JoinQuoted(Users, 20))
				: FString(TEXT("something in the open level (or another loaded asset) uses it"));
			return Fail(FString::Printf(TEXT("%s was not deleted: %s. Point them at something else first, or pass force: true to delete it anyway (they lose the reference)."), *Package, *Who));
		}
		ZettaAgentChanges::WillChangeAsset(Package, TEXT("deleted"));
		const int32 Deleted = ObjectTools::ForceDeleteObjects({ Asset }, false);
		if (Deleted == 0) return Fail(FString::Printf(TEXT("Unreal could not delete %s (read-only, or still in use by an open editor)."), *Package));
		return Ok(FString::Printf(TEXT("Deleted %s%s."), *Package, Users.Num() > 0 ? *FString::Printf(TEXT("; %s lost their reference to it"), *ZettaTool::JoinQuoted(Users, 20)) : TEXT("")),
			FString::Printf(TEXT("Deleted %s"), *FPackageName::GetShortName(Package)));
	}

	FZettaToolPreview DescribeDeleteAsset(const TSharedPtr<FJsonObject>& In)
	{
		const FString Package = ZettaTool::PackagePath(ZettaTool::Str(In, TEXT("path")));
		const TArray<FString> Users = Referencers(Package);
		FString Body = Package;
		if (Users.Num() > 0) Body += FString::Printf(TEXT("\nUsed by %s%s"), *ZettaTool::JoinQuoted(Users, 10), ZettaTool::BoolOr(In, TEXT("force"), false) ? TEXT(" — they will lose the reference") : TEXT(""));
		return ZettaTool::Preview(FString::Printf(TEXT("Delete %s"), *FPackageName::GetShortName(Package)), Body);
	}
}

void ZettaToolAreas::RegisterAssetTools(FZettaAgentTools& Tools)
{
	using namespace ZettaAssetImpl;
	using ZettaTool::Preview;
	Tools.Register(TEXT("find_assets"), &FindAssets, [](const TSharedPtr<FJsonObject>&) { return Preview(TEXT("Find assets")); });
	Tools.Register(TEXT("inspect_asset"), &InspectAsset, [](const TSharedPtr<FJsonObject>& In) { return Preview(FString::Printf(TEXT("Inspect %s"), *ZettaTool::Str(In, TEXT("path")))); });
	Tools.Register(TEXT("asset_operation"), &AssetOperation, [](const TSharedPtr<FJsonObject>& In)
	{
		const FString Action = ZettaTool::Str(In, TEXT("action")).ToLower();
		const FString From = ZettaTool::Str(In, TEXT("from"), ZettaTool::Str(In, TEXT("path")));
		const FString To = ZettaTool::Str(In, TEXT("to"));
		if (Action == TEXT("move")) return Preview(FString::Printf(TEXT("Move %s"), *FPaths::GetBaseFilename(From)), FString::Printf(TEXT("%s → %s"), *From, *To));
		if (Action == TEXT("rename")) return Preview(FString::Printf(TEXT("Rename %s"), *FPaths::GetBaseFilename(From)), FString::Printf(TEXT("%s → %s"), *From, *To));
		if (Action == TEXT("duplicate")) return Preview(FString::Printf(TEXT("Duplicate %s"), *FPaths::GetBaseFilename(From)), FString::Printf(TEXT("%s → %s"), *From, *To));
		if (Action == TEXT("create_folder")) return Preview(TEXT("New folder"), ZettaTool::Str(In, TEXT("path"), To));
		return Preview(TEXT("Save"), From);
	});
	Tools.Register(TEXT("import_file"), &ImportFile, [](const TSharedPtr<FJsonObject>& In)
	{
		return Preview(FString::Printf(TEXT("Import %s"), *FPaths::GetCleanFilename(ZettaTool::Str(In, TEXT("path")))), TEXT("Into ") + ZettaTool::Str(In, TEXT("destination"), TEXT("/Game/Imported")));
	});
	Tools.Register(TEXT("delete_asset"), &DeleteAsset, &DescribeDeleteAsset);
}
