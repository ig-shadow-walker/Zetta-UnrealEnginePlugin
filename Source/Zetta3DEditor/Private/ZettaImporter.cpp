// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaImporter.h"

#include "AssetImportTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/Selection.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "Internationalization/Regex.h"
#include "LevelEditorViewport.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "UObject/SavePackage.h"
#include "ZettaApi.h"
#include "ZettaHttp.h"
#include "ZettaJson.h"
#include "ZettaSettings.h"
#include "Zetta3DEditorModule.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace ZettaImporter
{
	const TCHAR* SplatUnsupported = TEXT("Gaussian splats are not supported in Unreal by Zetta 3D yet — open it on zetta3d.ai.");

	FString Slug(const FString& Text)
	{
		FString Out;
		bool bDash = false;
		for (const TCHAR C : (Text.IsEmpty() ? FString(TEXT("model")) : Text).ToLower())
		{
			if (FChar::IsAlnum(C) && C < 128)
			{
				Out.AppendChar(C);
				bDash = false;
			}
			else if (!bDash && Out.Len() > 0)
			{
				Out.AppendChar(TEXT('-'));
				bDash = true;
			}
		}
		while (Out.EndsWith(TEXT("-"))) Out.LeftChopInline(1);
		if (Out.Len() > 40) Out = Out.Left(40);
		while (Out.EndsWith(TEXT("-"))) Out.LeftChopInline(1);
		return Out.IsEmpty() ? TEXT("model") : Out;
	}

	static int32 PartIndex(const FString& FileName)
	{
		const FRegexPattern Pattern(TEXT("-(\\d+)\\.glb$"));
		FRegexMatcher Matcher(Pattern, FileName);
		return Matcher.FindNext() ? FCString::Atoi(*Matcher.GetCaptureGroup(1)) : MAX_int32;
	}

	TArray<FZettaResultFile> PickFiles(const FZettaGeneration& G)
	{
		const TArray<FZettaResultFile> Meshes = G.MeshFiles();
		TArray<FZettaResultFile> Glbs = Meshes.FilterByPredicate([](const FZettaResultFile& F) { return F.Extension() == TEXT("glb"); });
		if ((G.Feature == TEXT("segmentation") || G.Feature == TEXT("smartSegmentation")) && Glbs.Num() > 1)
		{
			Glbs.Sort([](const FZettaResultFile& A, const FZettaResultFile& B) { return PartIndex(A.FileName()) < PartIndex(B.FileName()); });
			return Glbs;
		}
		if (G.Feature == TEXT("worldMeshExport") && Glbs.Num() > 0) return { Glbs[0] };

		const bool bFbxFirst = UZettaProjectSettings::Get()->PreferredFormat == EZettaMeshFormat::FBX;
		const TArray<FString> Order = bFbxFirst
			? TArray<FString>{ TEXT("fbx"), TEXT("glb"), TEXT("gltf"), TEXT("obj") }
			: TArray<FString>{ TEXT("glb"), TEXT("fbx"), TEXT("gltf"), TEXT("obj") };
		for (const FString& Ext : Order)
		{
			if (const FZettaResultFile* F = Meshes.FindByPredicate([&Ext](const FZettaResultFile& M) { return M.Extension() == Ext; })) return { *F };
		}
		return {};
	}

	static FString DiskFolder(const FString& ContentFolder)
	{
		return FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(ContentFolder + TEXT("/")));
	}

	static void WriteManifest(const FString& ContentFolder, const FZettaGeneration& G, const TArray<FString>& Assets)
	{
		TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetNumberField(TEXT("generationId"), (double)G.Id);
		M->SetStringField(TEXT("title"), G.DisplayTitle());
		M->SetStringField(TEXT("feature"), G.Feature);
		M->SetStringField(TEXT("model"), G.Model);
		M->SetStringField(TEXT("importedAt"), FDateTime::UtcNow().ToIso8601());
		M->SetField(TEXT("assets"), ZettaJson::StringArray(Assets));
		M->SetStringField(TEXT("source"), FString::Printf(TEXT("https://zetta3d.ai/generations/%lld"), G.Id));
		const FString Path = FPaths::Combine(DiskFolder(ContentFolder), TEXT("zetta.json"));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveStringToFile(ZettaJson::Write(M, true), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		ZettaProjectIndex::Invalidate();
	}

	/** Imports files from disk into a content folder; returns the object paths made, meshes first. */
	static TArray<FString> ImportFiles(const TArray<FString>& Files, const FString& ContentFolder, const TArray<FString>& Names)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		TArray<UAssetImportTask*> Tasks;
		for (int32 i = 0; i < Files.Num(); ++i)
		{
			UAssetImportTask* Task = NewObject<UAssetImportTask>();
			Task->AddToRoot();
			Task->Filename = Files[i];
			Task->DestinationPath = ContentFolder;
			Task->DestinationName = Names.IsValidIndex(i) ? Names[i] : FString();
			Task->bAutomated = true;
			Task->bReplaceExisting = true;
			Task->bReplaceExistingSettings = true;
			Task->bSave = true;
			Task->bAsync = false;
			Tasks.Add(Task);
		}
		AssetTools.ImportAssetTasks(Tasks);

		TArray<FString> Meshes, Others;
		for (UAssetImportTask* Task : Tasks)
		{
			for (UObject* Object : Task->GetObjects())
			{
				if (!Object) continue;
				const FString Path = Object->GetPathName();
				if (Object->IsA<UStaticMesh>() || Object->IsA<USkeletalMesh>()) Meshes.AddUnique(Path);
				else Others.AddUnique(Path);
			}
			for (const FString& Path : Task->ImportedObjectPaths)
			{
				if (!Meshes.Contains(Path) && !Others.Contains(Path)) Others.AddUnique(Path);
			}
			Task->RemoveFromRoot();
		}
		// Imports that report nothing (some importers) are found by what landed in the folder.
		if (Meshes.Num() == 0)
		{
			IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
			Registry.ScanPathsSynchronous({ ContentFolder }, true);
			TArray<FAssetData> Found;
			Registry.GetAssetsByPath(FName(*ContentFolder), Found, true);
			for (const FAssetData& A : Found)
			{
				const FString Path = A.GetObjectPathString();
				if (A.AssetClassPath == UStaticMesh::StaticClass()->GetClassPathName() || A.AssetClassPath == USkeletalMesh::StaticClass()->GetClassPathName())
					Meshes.AddUnique(Path);
				else Others.AddUnique(Path);
			}
		}
		Meshes.Append(Others);
		return Meshes;
	}

	FVector DropPoint()
	{
		FVector Start = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		if (GCurrentLevelEditingViewportClient)
		{
			Start = GCurrentLevelEditingViewportClient->GetViewLocation();
			Forward = GCurrentLevelEditingViewportClient->GetViewRotation().Vector();
		}
		else if (GEditor)
		{
			for (FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
			{
				if (Client && Client->IsPerspective())
				{
					Start = Client->GetViewLocation();
					Forward = Client->GetViewRotation().Vector();
					break;
				}
			}
		}
		FVector Flat = FVector(Forward.X, Forward.Y, 0.f).GetSafeNormal();
		if (Flat.IsNearlyZero()) Flat = FVector::ForwardVector;
		FVector Point = Start + Flat * 500.f;

		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (World)
		{
			FHitResult Hit;
			const FVector From = Point + FVector(0, 0, 10000.f);
			const FVector To = Point - FVector(0, 0, 100000.f);
			FCollisionQueryParams Params(TEXT("ZettaDrop"), true);
			if (World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params)) return Hit.ImpactPoint;
		}
		Point.Z = 0.f;
		return Point;
	}

	TArray<AActor*> Place(const TArray<FString>& AssetPaths, const FString& Label, const FZettaImportOptions& Options)
	{
		TArray<AActor*> Out;
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World) return Out;

		const FScopedTransaction Transaction(FText::Format(LOCTEXT("PlaceModel", "Zetta 3D: place {0}"), FText::FromString(Label)));
		const FVector Location = Options.Location.IsSet() ? Options.Location.GetValue() : DropPoint();
		const FName Folder(*FString::Printf(TEXT("Zetta 3D/%s"), *Label));

		for (const FString& Path : AssetPaths)
		{
			UObject* Asset = LoadObject<UObject>(nullptr, *Path);
			AActor* Actor = nullptr;
			if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset))
			{
				AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
				if (!A) continue;
				A->GetStaticMeshComponent()->SetStaticMesh(Mesh);
				Actor = A;
			}
			else if (USkeletalMesh* Skeletal = Cast<USkeletalMesh>(Asset))
			{
				ASkeletalMeshActor* A = World->SpawnActor<ASkeletalMeshActor>(Location, FRotator::ZeroRotator);
				if (!A) continue;
				A->GetSkeletalMeshComponent()->SetSkeletalMesh(Skeletal);
				Actor = A;
			}
			if (!Actor) continue;

			Actor->Modify();
			Actor->SetActorLabel(AssetPaths.Num() > 1 ? FString::Printf(TEXT("%s_%d"), *Label, Out.Num() + 1) : Label);
			Actor->SetFolderPath(Folder);
			// Rest it on the ground: the bounds' bottom at the drop point.
			FVector Origin, Extent;
			Actor->GetActorBounds(false, Origin, Extent);
			const float Bottom = Origin.Z - Extent.Z;
			Actor->AddActorWorldOffset(FVector(0, 0, Location.Z - Bottom));
			if (Options.Parent.IsValid()) Actor->AttachToActor(Options.Parent.Get(), FAttachmentTransformRules::KeepWorldTransform);
			Out.Add(Actor);
		}

		if (Options.bSelect && Out.Num() > 0 && GEditor)
		{
			GEditor->SelectNone(false, true);
			for (AActor* A : Out) GEditor->SelectActor(A, true, false);
			GEditor->NoteSelectionChange();
		}
		return Out;
	}

	bool SaveTexture(const TArray<uint8>& Bytes, const FString& PackagePath, FString& OutObjectPath, FString& OutError, bool bReplace)
	{
		const FString ContentType = ZettaImages::SniffContentType(Bytes);
		if (ContentType == TEXT("image/webp"))
		{
			OutError = TEXT("The picture is WebP, which Unreal does not import as a texture.");
			return false;
		}
		const FString Ext = ZettaImages::ExtensionFor(ContentType.IsEmpty() ? TEXT("image/png") : ContentType);
		const FString Name = FPackageName::GetShortName(PackagePath);
		const FString Folder = FPackageName::GetLongPackagePath(PackagePath);
		const FString Temp = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Zetta3D"), TEXT("Pictures"), Name + TEXT(".") + Ext);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Temp), true);
		if (!FFileHelper::SaveArrayToFile(Bytes, *Temp))
		{
			OutError = TEXT("Could not write the picture to disk.");
			return false;
		}
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->AddToRoot();
		Task->Filename = Temp;
		Task->DestinationPath = Folder;
		Task->DestinationName = Name;
		Task->bAutomated = true;
		Task->bReplaceExisting = bReplace;
		Task->bReplaceExistingSettings = false;
		Task->bSave = true;
		AssetTools.ImportAssetTasks({ Task });
		for (UObject* O : Task->GetObjects())
		{
			if (O && O->IsA<UTexture>())
			{
				OutObjectPath = O->GetPathName();
				break;
			}
		}
		Task->RemoveFromRoot();
		if (OutObjectPath.IsEmpty())
		{
			OutError = TEXT("Unreal could not import the picture.");
			return false;
		}
		return true;
	}

	void Import(const FZettaGenerationPtr& Generation, const FZettaImportOptions& Options, TFunction<void(const FZettaImportResult&)> Done, FZettaImportStatus Status)
	{
		FZettaImportResult Fail;
		if (!Generation.IsValid()) { Fail.Error = TEXT("Nothing to import."); Done(Fail); return; }
		if (Generation->IsSplat()) { Fail.Error = SplatUnsupported; Done(Fail); return; }
		if (!Generation->IsSuccess()) { Fail.Error = TEXT("That model is not ready yet."); Done(Fail); return; }
		const TArray<FZettaResultFile> Files = PickFiles(*Generation);
		if (Files.Num() == 0) { Fail.Error = TEXT("This generation has no mesh file Unreal can import."); Done(Fail); return; }

		const FString Base = Options.Folder.IsEmpty() ? UZettaProjectSettings::Get()->GetModelsFolder() : Options.Folder;
		const FString Slugged = Slug(Generation->DisplayTitle());
		const FString ContentFolder = FString::Printf(TEXT("%s/%lld-%s"), *Base, Generation->Id, *Slugged);
		const FString DownloadDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Zetta3D"), TEXT("Downloads"), FString::Printf(TEXT("%lld"), Generation->Id)));
		IFileManager::Get().MakeDirectory(*DownloadDir, true);

		struct FState
		{
			TArray<FString> Disk;
			TArray<FString> Names;
			int32 Index = 0;
		};
		TSharedRef<FState> State = MakeShared<FState>();
		TSharedRef<TFunction<void()>> Next = MakeShared<TFunction<void()>>();

		auto Finish = [=]()
		{
			if (Status) Status(TEXT("Importing…"), 0.96f);
			FZettaImportResult Result;
			Result.Folder = ContentFolder;
			Result.AssetPaths = ImportFiles(State->Disk, ContentFolder, State->Names);
			if (Result.AssetPaths.Num() == 0)
			{
				Result.Error = TEXT("Unreal could not import the model. Check the Output Log for the importer's message.");
				Done(Result);
				return;
			}
			WriteManifest(ContentFolder, *Generation, Result.AssetPaths);
			const bool bCollider = Files.ContainsByPredicate([](const FZettaResultFile& F) { return F.Label.TrimStartAndEnd().Equals(TEXT("Collider"), ESearchCase::IgnoreCase); });
			if (bCollider) Result.Note = TEXT("This is a collider — a physics proxy for a world. Use it as a collision mesh rather than something to see.");
			if (Options.bPlace)
			{
				TArray<FString> MeshPaths;
				for (const FString& P : Result.AssetPaths)
				{
					UObject* O = LoadObject<UObject>(nullptr, *P);
					if (O && (O->IsA<UStaticMesh>() || O->IsA<USkeletalMesh>())) MeshPaths.Add(P);
				}
				for (AActor* A : Place(MeshPaths, FString::Printf(TEXT("%lld-%s"), Generation->Id, *Slugged), Options))
				{
					Result.Placed.Add(A);
					Result.PlacedActors.Add(A->GetActorLabel());
				}
			}
			Result.bOk = true;
			if (Status) Status(TEXT("Imported"), 1.f);
			Done(Result);
		};

		*Next = [=]()
		{
			const int32 i = State->Index;
			if (i >= Files.Num())
			{
				// Clearing *Next destroys this very lambda and everything it captured (Finish, the folder, Done…),
				// so take a copy of Finish onto the stack first and only then break the cycle.
				const auto Last = Finish;
				*Next = nullptr;
				Last();
				return;
			}
			const FZettaResultFile File = Files[i];
			if (Status) Status(Files.Num() > 1 ? FString::Printf(TEXT("Downloading part %d of %d…"), i + 1, Files.Num()) : FString(TEXT("Downloading…")), 0.95f * i / Files.Num());
			// The manifest's links expire after ~15 minutes — ask for a fresh one by key.
			ZettaApi::DownloadUrl(Generation->Id, File.Key, [=](const FZettaResponse& R, const FString& Fresh)
			{
				const FString Url = R.bOk && !Fresh.IsEmpty() ? Fresh : File.Url;
				FZettaHttp::Download(Url, [=](const FZettaResponse& D)
				{
					if (!D.bOk)
					{
						FZettaImportResult Failed;
						Failed.Error = D.Message;
						*Next = nullptr;
						Done(Failed);
						return;
					}
					const FString Name = Files.Num() > 1 ? FString::Printf(TEXT("%s_part%d"), *Slugged.Replace(TEXT("-"), TEXT("_")), i + 1) : Slugged.Replace(TEXT("-"), TEXT("_"));
					const FString Path = FPaths::Combine(DownloadDir, Name + TEXT(".") + File.Extension());
					FFileHelper::SaveArrayToFile(D.Bytes, *Path);
					State->Disk.Add(Path);
					State->Names.Add(Name);
					State->Index++;
					(*Next)();
				}, [=](float P)
				{
					if (Status) Status(TEXT("Downloading…"), 0.95f * (i + P) / Files.Num());
				});
			});
		};
		(*Next)();
	}
}

namespace ZettaProjectIndex
{
	static TMap<int64, FString> Index;
	static double ScannedAt = -1000.0;

	void Invalidate() { ScannedAt = -1000.0; }

	const TMap<int64, FString>& Models()
	{
		if (FPlatformTime::Seconds() - ScannedAt < 5.0) return Index;
		ScannedAt = FPlatformTime::Seconds();
		Index.Reset();
		const FString Root = UZettaProjectSettings::Get()->GetModelsFolder();
		const FString Disk = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(Root + TEXT("/")));
		TArray<FString> Manifests;
		IFileManager::Get().FindFilesRecursive(Manifests, *Disk, TEXT("zetta.json"), true, false);
		for (const FString& File : Manifests)
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *File)) continue;
			const int64 Id = ZettaJson::Int(ZettaJson::Parse(Text), TEXT("generationId"));
			if (Id <= 0) continue;
			FString Relative = FPaths::GetPath(File);
			FPaths::MakePathRelativeTo(Relative, *Disk);
			Index.Add(Id, Root + TEXT("/") + Relative.Replace(TEXT("\\"), TEXT("/")));
		}
		return Index;
	}

	bool Contains(int64 Id) { return Models().Contains(Id); }

	FString FolderOf(int64 Id)
	{
		const FString* F = Models().Find(Id);
		return F ? *F : FString();
	}

	FString MainAssetOf(int64 Id)
	{
		const FString Folder = FolderOf(Id);
		if (Folder.IsEmpty()) return FString();
		const FString Disk = FPaths::Combine(FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(Folder + TEXT("/"))), TEXT("zetta.json"));
		FString Text;
		FFileHelper::LoadFileToString(Text, *Disk);
		const TArray<FString> Assets = ZettaJson::Strings(ZettaJson::Parse(Text), TEXT("assets"));
		return Assets.Num() > 0 ? Assets[0] : FString();
	}
}

#undef LOCTEXT_NAMESPACE
