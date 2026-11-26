// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaLibrary.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "ContentBrowserModule.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IContentBrowserSingleton.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "ZettaApi.h"
#include "ZettaCatalog.h"
#include "ZettaImporter.h"
#include "ZettaJson.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace ZettaLibrary
{
	/** How many deletes run at once. */
	static constexpr int32 DeletesAtOnce = 4;

	void RunName(const FZettaRunGroup& Group, FString& OutName, FString& OutDetail)
	{
		// The server sends members newest first; the oldest carries the plainest title ("racingSeat"; later ones add " - Retopology").
		const FZettaGenerationPtr First = Group.Members.Num() > 0 ? Group.Members.Last() : nullptr;
		FString Seed;
		if (First.IsValid())
		{
			Seed = First->Title;
			int32 Dash = INDEX_NONE;
			if ((Dash = Seed.Find(TEXT(" - "))) != INDEX_NONE) Seed = Seed.Left(Dash);
			Seed.TrimStartAndEndInline();
		}
		const FString Pipeline = Group.PipelineTitle.TrimStartAndEnd();
		OutName = !Seed.IsEmpty() ? Seed : !Pipeline.IsEmpty() ? Pipeline : FString::Printf(TEXT("Pipeline run #%lld"), Group.RunId);

		const int32 Count = RunCount(Group);
		const FString Models = FString::Printf(TEXT("%d model%s"), Count, Count == 1 ? TEXT("") : TEXT("s"));
		OutDetail = !Pipeline.IsEmpty()
			? FString::Printf(TEXT("%s · %s · run #%lld"), *Models, *Pipeline, Group.RunId)
			: FString::Printf(TEXT("%s · run #%lld"), *Models, Group.RunId);
	}

	int32 RunCount(const FZettaRunGroup& Group) { return Group.Count > 0 ? Group.Count : Group.Members.Num(); }

	TArray<FZettaGenerationPtr> InRunOrder(const TArray<FZettaGenerationPtr>& Members)
	{
		TArray<FZettaGenerationPtr> List = Members.FilterByPredicate([](const FZettaGenerationPtr& M) { return M.IsValid(); });
		List.StableSort([](const FZettaGenerationPtr& A, const FZettaGenerationPtr& B)
		{
			const bool bA = A->RunStep.IsSet(), bB = B->RunStep.IsSet();
			if (bA && bB && A->RunStep->Position != B->RunStep->Position) return A->RunStep->Position < B->RunStep->Position;
			if (bA != bB) return bA;
			if (A->CreatedAt != B->CreatedAt) return A->CreatedAt < B->CreatedAt;
			return A->Id < B->Id;
		});
		return List;
	}

	FString StepText(const FZettaGeneration& Member, int32 Index)
	{
		TArray<FString> Parts;
		if (Member.RunStep.IsSet())
		{
			Parts.Add(FString::Printf(TEXT("Step %d"), Member.RunStep->Position));
			Parts.Add(Member.RunStep->Label);
			Parts.Add(Member.RunStep->Part);
		}
		else
		{
			Parts.Add(FString::Printf(TEXT("Step %d"), Index + 1));
		}
		Parts.RemoveAll([](const FString& P) { return P.TrimStartAndEnd().IsEmpty(); });
		return FString::Join(Parts, TEXT(" · "));
	}

	bool IsRunEntry(const FZettaGenerationPtr& Row)
	{
		return Row.IsValid() && Row->RunGroup.IsValid() && Row->RunGroup->Members.Num() > 0;
	}

	TArray<FZettaGenerationPtr> Flatten(const TArray<FZettaGenerationPtr>& Entries)
	{
		TArray<FZettaGenerationPtr> Out;
		for (const FZettaGenerationPtr& E : Entries)
		{
			if (IsRunEntry(E))
			{
				for (const FZettaGenerationPtr& M : E->RunGroup->Members) if (M.IsValid()) Out.Add(M);
			}
			else if (E.IsValid())
			{
				Out.Add(E);
			}
		}
		return Out;
	}

	bool CanFork(const FZettaGeneration& Row)
	{
		static const TSet<FString> Ways = { TEXT("txt23d"), TEXT("img23d"), TEXT("multiView23d") };
		if (Row.Availability == TEXT("deleted") || !Ways.Contains(Row.Feature)) return false;
		return ZettaCatalog::GenerateModels().Contains(Row.Model) || ZettaCatalog::SmartModels().Contains(Row.Model);
	}

	FString ModelBadge(const FString& Model)
	{
		if (Model.IsEmpty()) return FString();
		const FString Label = ZettaCatalog::ModelLabel(Model);
		return Label == Model ? FString() : Label;
	}

	FText DateText(const FDateTime& When)
	{
		if (When.GetTicks() == 0) return FText::GetEmpty();
		return FText::AsDate(When, EDateTimeStyle::Medium);
	}

	void OpenOnSite(const FZettaGeneration& Row)
	{
		ZettaUI::OpenSite(FString::Printf(TEXT("/3d-studio?generation=%lld"), Row.Id));
	}

	TArray<FAssetData> ImportedAssets(int64 GenerationId)
	{
		TArray<FAssetData> Out;
		const FString Folder = ZettaProjectIndex::FolderOf(GenerationId);
		if (Folder.IsEmpty()) return Out;

		// The importer's zetta.json lists every asset it made, main mesh first.
		const FString Manifest = FPaths::Combine(FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(Folder + TEXT("/"))), TEXT("zetta.json"));
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Manifest)) return Out;

		// A folder emptied in the Content Browser keeps its zetta.json: only assets that still exist count.
		IAssetRegistry& Registry = IAssetRegistry::GetChecked();
		for (const FString& Path : ZettaJson::Strings(ZettaJson::Parse(Text), TEXT("assets")))
		{
			const FAssetData Asset = Registry.GetAssetByObjectPath(FSoftObjectPath(Path));
			if (Asset.IsValid()) Out.Add(Asset);
		}
		return Out;
	}

	TArray<FAssetData> ImportedMeshes(int64 GenerationId)
	{
		const FTopLevelAssetPath StaticMesh = UStaticMesh::StaticClass()->GetClassPathName();
		const FTopLevelAssetPath SkeletalMesh = USkeletalMesh::StaticClass()->GetClassPathName();
		return ImportedAssets(GenerationId).FilterByPredicate([&](const FAssetData& A)
		{
			return A.AssetClassPath == StaticMesh || A.AssetClassPath == SkeletalMesh;
		});
	}

	void ShowInContentBrowser(int64 GenerationId)
	{
		IContentBrowserSingleton& Browser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();
		TArray<FAssetData> Assets = ImportedMeshes(GenerationId);
		if (Assets.Num() == 0) Assets = ImportedAssets(GenerationId);
		if (Assets.Num() > 0)
		{
			Browser.SyncBrowserToAssets(Assets);
			return;
		}
		const FString Folder = ZettaProjectIndex::FolderOf(GenerationId);
		if (!Folder.IsEmpty()) Browser.SyncBrowserToFolders({ Folder });
	}

	void Toast(const FText& Message, bool bError)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = bError ? 6.f : 3.f;
		Info.bUseSuccessFailIcons = true;
		const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid()) Item->SetCompletionState(bError ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
	}

	TMap<int64, FImporting>& Importing()
	{
		static TMap<int64, FImporting> Map;
		return Map;
	}

	bool IsImporting(int64 GenerationId) { return Importing().Contains(GenerationId); }

	void BulkDeleteText(int32 Count, FText& OutTitle, FText& OutMessage)
	{
		const bool bOne = Count == 1;
		OutTitle = FText::Format(LOCTEXT("Library.BulkDeleteTitle", "Delete {0} {0}|plural(one=model,other=models)?"), Count);
		OutMessage = bOne
			? LOCTEXT("Library.BulkDeleteOne", "Its model files will be removed from storage and the record deleted. This cannot be undone, and the credits spent on it are not refunded. Copies already imported into this project are kept.")
			: LOCTEXT("Library.BulkDeleteMany", "Their model files will be removed from storage and the records deleted. This cannot be undone, and the credits spent on them are not refunded. Copies already imported into this project are kept.");
	}

	void RunDeletes(const TArray<int64>& Ids, TFunction<void(int32, int32)> Progress,
		TFunction<void(const TArray<int64>&, const TArray<int64>&, const FString&)> Done)
	{
		struct FState
		{
			TArray<int64> Queue, Deleted, Failed;
			FString LastError;
			int32 Total = 0;
			int32 InFlight = 0;
			TFunction<void(int32, int32)> Progress;
			TFunction<void(const TArray<int64>&, const TArray<int64>&, const FString&)> Done;
		};
		TSharedRef<FState> State = MakeShared<FState>();
		State->Queue = Ids;
		State->Total = Ids.Num();
		State->Progress = MoveTemp(Progress);
		State->Done = MoveTemp(Done);
		if (State->Progress) State->Progress(0, State->Total);
		if (Ids.Num() == 0)
		{
			if (State->Done) State->Done({}, {}, FString());
			return;
		}

		// Each worker takes the next id when its delete answers; the last one to finish reports.
		TSharedRef<TFunction<void()>> Worker = MakeShared<TFunction<void()>>();
		TWeakPtr<TFunction<void()>> WeakWorker = Worker;
		*Worker = [State, WeakWorker]()
		{
			if (State->Queue.Num() == 0) return;
			const int64 Id = State->Queue[0];
			State->Queue.RemoveAt(0);
			State->InFlight++;
			// Each request holds the worker alive until it answers.
			TSharedPtr<TFunction<void()>> Keep = WeakWorker.Pin();
			ZettaApi::DeleteGeneration(Id, [State, Id, Keep](const FZettaResponse& R)
			{
				State->InFlight--;
				if (R.bOk) State->Deleted.Add(Id);
				else
				{
					State->Failed.Add(Id);
					State->LastError = ZettaCatalog::Explain(R);
				}
				if (State->Progress) State->Progress(State->Deleted.Num() + State->Failed.Num(), State->Total);
				if (State->Queue.Num() > 0 && Keep.IsValid()) (*Keep)();
				else if (State->InFlight == 0 && State->Done)
				{
					auto Finish = MoveTemp(State->Done);
					State->Done = nullptr;
					Finish(State->Deleted, State->Failed, State->LastError);
				}
			});
		};
		for (int32 i = 0; i < FMath::Min(DeletesAtOnce, Ids.Num()); ++i) (*Worker)();
	}
}

#undef LOCTEXT_NAMESPACE
