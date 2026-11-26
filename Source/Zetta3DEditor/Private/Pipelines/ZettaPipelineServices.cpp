// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaPipelineServices.h"

#include "AssetRegistry/AssetData.h"
#include "ContentBrowserModule.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "IContentBrowserSingleton.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Zetta3DEditorModule.h"
#include "ZettaAuth.h"
#include "ZettaCatalog.h"
#include "ZettaHttp.h"
#include "ZettaImageCache.h"
#include "ZettaPipelineJson.h"

using namespace ZettaPJson;

// ─── Pictures ───────────────────────────────────────────────────────────────────────────────────────────────────────

/** A box-filtered copy no larger than MaxEdge, on the CPU. Returns false when the picture cannot be decoded. */
static bool PlShrink(const TArray<uint8>& Bytes, int32 MaxEdge, bool bOnlyWhenLarger, TArray<FColor>& Out, int32& OutW, int32& OutH, bool& bResized)
{
	TArray<uint8> Bgra;
	int32 W = 0, H = 0;
	if (Bytes.Num() == 0 || !FZettaImageCache::Decode(Bytes, Bgra, W, H) || W <= 0 || H <= 0) return false;
	bResized = false;
	const int32 Longest = FMath::Max(W, H);
	const double Scale = FMath::Min(1.0, MaxEdge / (double)Longest);
	if (bOnlyWhenLarger && Longest <= MaxEdge) return true;
	OutW = FMath::Max(1, FMath::RoundToInt(W * Scale));
	OutH = FMath::Max(1, FMath::RoundToInt(H * Scale));
	bResized = true;
	Out.SetNumUninitialized(OutW * OutH);
	const double SX = W / (double)OutW, SY = H / (double)OutH;
	for (int32 Y = 0; Y < OutH; ++Y)
	{
		const int32 Y0 = (int32)(Y * SY);
		const int32 Y1 = FMath::Max(Y0 + 1, FMath::Min(H, FMath::CeilToInt((Y + 1) * SY)));
		for (int32 X = 0; X < OutW; ++X)
		{
			const int32 X0 = (int32)(X * SX);
			const int32 X1 = FMath::Max(X0 + 1, FMath::Min(W, FMath::CeilToInt((X + 1) * SX)));
			int64 B = 0, G = 0, R = 0, A = 0, N = 0;
			for (int32 YY = Y0; YY < Y1; ++YY)
			{
				for (int32 XX = X0; XX < X1; ++XX)
				{
					const uint8* P = &Bgra[(YY * W + XX) * 4];
					B += P[0]; G += P[1]; R += P[2]; A += P[3]; ++N;
				}
			}
			N = FMath::Max<int64>(1, N);
			Out[Y * OutW + X] = FColor((uint8)(R / N), (uint8)(G / N), (uint8)(B / N), (uint8)(A / N));
		}
	}
	return true;
}

static TArray<uint8> PlJpeg(const TArray<FColor>& Pixels, int32 W, int32 H, int32 Quality)
{
	TArray64<uint8> Out;
	if (!FImageUtils::CompressImage(Out, TEXT("jpg"), FImageView(Pixels.GetData(), W, H), Quality)) return {};
	return TArray<uint8>(Out.GetData(), (int32)Out.Num());
}

FString FZettaPipelineSeed::DataUrl(const TArray<uint8>& Bytes)
{
	return Bytes.Num() == 0 ? FString() : TEXT("data:image/jpeg;base64,") + FBase64::Encode(Bytes);
}

TArray<uint8> FZettaPipelineSeed::FromDataUrl(const FString& Url)
{
	TArray<uint8> Out;
	int32 Comma = INDEX_NONE;
	if (!Url.StartsWith(TEXT("data:")) || !Url.FindChar(TEXT(','), Comma) || Comma <= 0) return Out;
	if (!FBase64::Decode(Url.Mid(Comma + 1), Out)) Out.Reset();
	return Out;
}

FString FZettaPipelineSeed::ThumbnailDataUrl() const { return DataUrl(Thumbnail); }

TOptional<int64> FZettaPipelineSeed::ImagePostId() const
{
	const TOptional<double> N = JNumber(Request, TEXT("gen2dPostId"));
	return N.IsSet() ? TOptional<int64>((int64)N.GetValue()) : TOptional<int64>();
}

TOptional<int64> FZettaPipelineSeed::ModelPostId() const
{
	const TOptional<double> N = JNumber(Request, TEXT("gen3dPostId"));
	return N.IsSet() ? TOptional<int64>((int64)N.GetValue()) : TOptional<int64>();
}

TArray<uint8> FZettaPipelineSeed::DownscaleForUpload(const TArray<uint8>& Bytes, const FString& ContentType, FString& OutType)
{
	OutType = ContentType.IsEmpty() ? FString(TEXT("image/png")) : ContentType;
	TArray<FColor> Pixels;
	int32 W = 0, H = 0;
	bool bResized = false;
	// One the engine cannot decode (WebP) is left for the server to judge; one small enough is sent as it is.
	if (!PlShrink(Bytes, MaxEdge, true, Pixels, W, H, bResized) || !bResized) return Bytes;
	TArray<uint8> Out = PlJpeg(Pixels, W, H, 90);
	if (Out.Num() == 0) return Bytes;
	OutType = TEXT("image/jpeg");
	return Out;
}

TArray<uint8> FZettaPipelineSeed::ThumbnailOf(const TArray<uint8>& Bytes)
{
	TArray<FColor> Pixels;
	int32 W = 0, H = 0;
	bool bResized = false;
	if (!PlShrink(Bytes, ThumbnailSize, false, Pixels, W, H, bResized)) return {};
	return PlJpeg(Pixels, W, H, 80);
}

void FZettaPipelineSeed::FromUpload(const TArray<uint8>& Bytes, const FString& ContentType, const FString& Name,
	TFunction<void(const FZettaResponse&, const FZettaPipelineSeed&)> Done, FZettaProgress Progress)
{
	if (Bytes.Num() == 0)
	{
		FZettaResponse R;
		R.Message = TEXT("The picture is empty.");
		Done(R, FZettaPipelineSeed());
		return;
	}
	FString Type;
	const TArray<uint8> Ready = DownscaleForUpload(Bytes, ContentType, Type);
	const FString FileName = Name.TrimStartAndEnd().IsEmpty() ? FString(TEXT("picture")) : Name;
	TArray<uint8> Thumb = ThumbnailOf(Ready);
	ZettaApi::Upload(Ready, Type, [Done, FileName, Thumb](const FZettaResponse& R, const FZettaUploadTicket& Ticket)
	{
		FZettaPipelineSeed Seed;
		if (R.bOk)
		{
			Seed.Request = MakeShared<FJsonObject>();
			Seed.Request->SetStringField(TEXT("uploadKey"), Ticket.Key);
			Seed.Request->SetStringField(TEXT("name"), FileName);
			Seed.Name = FileName;
			Seed.Thumbnail = Thumb;
		}
		Done(R, Seed);
	}, Progress);
}

FZettaPipelineSeed FZettaPipelineSeed::FromImagePost(int64 Gen2dPostId, const FString& Name, const FString& ThumbnailUrl)
{
	FZettaPipelineSeed S;
	S.Request = MakeShared<FJsonObject>();
	S.Request->SetNumberField(TEXT("gen2dPostId"), (double)Gen2dPostId);
	S.Name = Name.IsEmpty() ? FString::Printf(TEXT("Picture #%lld"), Gen2dPostId) : Name;
	S.ThumbnailUrl = ThumbnailUrl;
	return S;
}

FZettaPipelineSeed FZettaPipelineSeed::FromModel(int64 Gen3dPostId, const FString& Name, const FString& ThumbnailUrl)
{
	FZettaPipelineSeed S;
	S.Request = MakeShared<FJsonObject>();
	S.Request->SetNumberField(TEXT("gen3dPostId"), (double)Gen3dPostId);
	S.Name = Name.IsEmpty() ? FString::Printf(TEXT("Model #%lld"), Gen3dPostId) : Name;
	S.Type = TEXT("model");
	S.ThumbnailUrl = ThumbnailUrl;
	return S;
}

FZettaPipelineSeed FZettaPipelineSeed::FromModel(const FZettaGeneration& Generation)
{
	return FromModel(Generation.Id, Generation.DisplayTitle(), Generation.ThumbnailUrl);
}

FZettaPipelineSeed FZettaPipelineSeed::FromViews(const TSharedPtr<FJsonObject>& Set)
{
	const FObj Front = JObject(Set, TEXT("front"));
	const FString Name = JTextOr(Front, TEXT("name"));
	FZettaPipelineSeed S;
	S.Request = MakeShared<FJsonObject>();
	S.Request->SetArrayField(TEXT("views"), ZettaNodeSettings::ViewsRequest(Set));
	if (!Name.IsEmpty()) S.Request->SetStringField(TEXT("name"), Name);
	S.Name = Name.IsEmpty() ? FString(TEXT("Pictures of each side")) : Name;
	S.Type = TEXT("views");
	S.Thumbnail = FromDataUrl(JTextOr(Front, TEXT("thumbnail")));
	return S;
}

void FZettaPipelineSeed::ThumbnailFromUrl(const FString& Url, TFunction<void(const TArray<uint8>&)> Done)
{
	if (Url.IsEmpty())
	{
		Done({});
		return;
	}
	FZettaHttp::Download(Url, [Done](const FZettaResponse& R) { Done(R.bOk ? ThumbnailOf(R.Bytes) : TArray<uint8>()); });
}

// ─── Project assets ─────────────────────────────────────────────────────────────────────────────────────────────────

namespace ZettaPipelineAssets
{
	static FString PackageOf(const FString& Path)
	{
		FString P = Path;
		int32 Dot = INDEX_NONE;
		if (P.FindLastChar(TEXT('.'), Dot) && P.StartsWith(TEXT("/"))) P = P.Left(Dot);
		return P;
	}

	bool PictureOfTexture(const FString& ObjectPath, TArray<uint8>& OutBytes, FString& OutContentType, FString& OutName, FString& OutError)
	{
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *ObjectPath);
		if (!Texture)
		{
			OutError = TEXT("That is not a picture (a texture asset).");
			return false;
		}
		OutName = Texture->GetName();
#if WITH_EDITORONLY_DATA
		// The file it was imported from, when it is still there and is one the engines read.
		if (Texture->AssetImportData)
		{
			const FString Source = Texture->AssetImportData->GetFirstFilename();
			const FString Ext = FPaths::GetExtension(Source).ToLower();
			if (!Source.IsEmpty() && FPaths::FileExists(Source) && (Ext == TEXT("png") || Ext == TEXT("jpg") || Ext == TEXT("jpeg"))
				&& FFileHelper::LoadFileToArray(OutBytes, *Source))
			{
				OutContentType = Ext == TEXT("png") ? TEXT("image/png") : TEXT("image/jpeg");
				OutName = FPaths::GetCleanFilename(Source);
				return true;
			}
		}
		FImage Image;
		if (Texture->Source.IsValid() && Texture->Source.GetMipImage(Image, 0))
		{
			FImage Bgra;
			Image.CopyTo(Bgra, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
			TArray64<uint8> Png;
			if (FImageUtils::CompressImage(Png, TEXT("png"), Bgra))
			{
				OutBytes = TArray<uint8>(Png.GetData(), (int32)Png.Num());
				OutContentType = TEXT("image/png");
				OutName += TEXT(".png");
				return true;
			}
		}
#endif
		OutError = FString::Printf(TEXT("%s could not be read as a picture."), *OutName);
		return false;
	}

	/** The library model a content path belongs to (a folder the importer made), or 0. */
	static int64 GenerationOf(const FString& Path)
	{
		const FString Package = PackageOf(Path);
		for (const TPair<int64, FString>& Pair : ZettaProjectIndex::Models())
		{
			const FString Folder = Pair.Value;
			if (!Folder.IsEmpty() && (Package == Folder || Package.StartsWith(Folder + TEXT("/")))) return Pair.Key;
		}
		return 0;
	}

	bool IsModelSource(const FString& Path)
	{
		if (Path.StartsWith(TEXT("/"))) return GenerationOf(Path) != 0 || FPaths::GetExtension(Path).ToLower() == TEXT("glb");
		return FPaths::GetExtension(Path).ToLower() == TEXT("glb");
	}

	void ModelFromAsset(const FString& Path, TFunction<void(const FZettaGenerationPtr&, const FString&)> Done, TFunction<void(const FString&)> Status)
	{
		const int64 Id = Path.StartsWith(TEXT("/")) ? GenerationOf(Path) : 0;
		if (Id != 0)
		{
			ZettaApi::GetGeneration(Id, [Done, Id](const FZettaResponse& R, const FZettaGenerationPtr& Generation, TOptional<int32>)
			{
				if (!R.bOk || !Generation.IsValid()) Done(nullptr, R.Status == 404 ? FString::Printf(TEXT("Model #%lld is no longer in your library."), Id) : ZettaCatalog::Explain(R));
				else Done(Generation, FString());
			});
			return;
		}
		FString File = Path;
		if (FPaths::IsRelative(File)) File = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), File);
		if (FPaths::GetExtension(File).ToLower() != TEXT("glb") || !FPaths::FileExists(File))
		{
			Done(nullptr, TEXT("That is not a .glb model. Choose a .glb file, or a model imported from Zetta 3D."));
			return;
		}
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *File))
		{
			Done(nullptr, TEXT("Could not read the file."));
			return;
		}
		const FString Title = FPaths::GetBaseFilename(File);
		const int64 Size = Bytes.Num();
		if (Status) Status(TEXT("Uploading the model to your library…"));
		ZettaApi::Upload(Bytes, TEXT("model/gltf-binary"), [Done, Title, Size](const FZettaResponse& R, const FZettaUploadTicket& Ticket)
		{
			if (!R.bOk)
			{
				Done(nullptr, ZettaCatalog::Explain(R));
				return;
			}
			ZettaApi::CreateUploadPost(Ticket.Key, Size, Title, [Done](const FZettaResponse& R2, const FZettaGenerationPtr& Row)
			{
				if (!R2.bOk || !Row.IsValid()) Done(nullptr, ZettaCatalog::Explain(R2));
				else Done(Row, FString());
			});
		});
	}

	TArray<FString> SelectedAssets()
	{
		TArray<FString> Out;
		FContentBrowserModule& Browser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		TArray<FAssetData> Selected;
		Browser.Get().GetSelectedAssets(Selected);
		for (const FAssetData& A : Selected) Out.Add(A.GetObjectPathString());
		return Out;
	}
}

// ─── Plan ───────────────────────────────────────────────────────────────────────────────────────────────────────────

FZettaPipelinePlan& FZettaPipelinePlan::Get()
{
	static FZettaPipelinePlan Instance;
	if (!Instance.AuthHandle.IsValid())
	{
		// A new account (or a sign-out) changes the plan: read it again.
		Instance.AuthHandle = FZettaAuth::Get().OnChanged.AddLambda([]()
		{
			FZettaPipelinePlan& Plan = FZettaPipelinePlan::Get();
			if (FZettaAuth::Get().IsSignedIn()) Plan.EnsureLoaded(true);
		});
	}
	return Instance;
}

void FZettaPipelinePlan::EnsureLoaded(bool bForce)
{
	if (Pending > 0 || !FZettaAuth::Get().IsSignedIn()) return;
	const double Now = FPlatformTime::Seconds();
	const bool bMissing = !PriceList.IsSet() || !Gates3D.IsSet() || !Images.IsSet();
	if (!bForce && (!bMissing || Now - LastTry < 20)) return;
	LastTry = Now;
	Pending = 3;
	ZettaApi::Pricing([](const FZettaResponse& R, const FZettaPriceList& Prices)
	{
		FZettaPipelinePlan& Plan = Get();
		if (R.bOk) Plan.PriceList = Prices;
		Plan.Finish();
	});
	ZettaApi::Entitlements3D([](const FZettaResponse& R, const FZettaEntitlements3D& Gates)
	{
		FZettaPipelinePlan& Plan = Get();
		if (R.bOk) Plan.Gates3D = Gates;
		Plan.Finish();
	});
	ZettaApi::ImageModels([](const FZettaResponse& R, const FZettaImageModels& Models)
	{
		FZettaPipelinePlan& Plan = Get();
		if (R.bOk) Plan.Images = Models;
		Plan.Finish();
	});
}

void FZettaPipelinePlan::Finish()
{
	if (--Pending > 0) return;
	OnChanged.Broadcast();
	TArray<TFunction<void()>> Calls = MoveTemp(Waiting);
	Waiting.Reset();
	for (TFunction<void()>& Call : Calls) Call();
}

void FZettaPipelinePlan::WhenLoaded(TFunction<void()> Done)
{
	if (PriceList.IsSet() && Gates3D.IsSet() && Pending == 0)
	{
		Done();
		return;
	}
	Waiting.Add(MoveTemp(Done));
	EnsureLoaded(true);
	// Signed out (or already failing): answer with what there is rather than wait forever.
	if (Pending == 0)
	{
		TArray<TFunction<void()>> Calls = MoveTemp(Waiting);
		Waiting.Reset();
		for (TFunction<void()>& Call : Calls) Call();
	}
}

// ─── Run follower ───────────────────────────────────────────────────────────────────────────────────────────────────

FZettaRunFollower& FZettaRunFollower::Get()
{
	static FZettaRunFollower Instance;
	return Instance;
}

void FZettaRunFollower::Shutdown()
{
	if (TickHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	TickHandle.Reset();
	Runs.Reset();
	Pipelines.Reset();
	Latest.Reset();
}

void FZettaRunFollower::EnsureTicking()
{
	if (TickHandle.IsValid()) return;
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FZettaRunFollower::Tick), 0.25f);
}

TSharedPtr<FZettaRunFollower::FFollowed> FZettaRunFollower::Find(int64 RunId) const
{
	for (const TSharedPtr<FFollowed>& E : Runs) if (E->RunId == RunId) return E;
	return nullptr;
}

bool FZettaRunFollower::IsFollowing(int64 RunId) const { return Find(RunId).IsValid(); }

void FZettaRunFollower::Follow(int64 RunId, int64 PipelineId, const FString& AgentSessionId, EZettaRunImport Import, const FString& Folder)
{
	TSharedPtr<FFollowed> Entry = Find(RunId);
	if (!Entry.IsValid())
	{
		Entry = MakeShared<FFollowed>();
		Entry->RunId = RunId;
		Runs.Add(Entry);
	}
	if (PipelineId != 0) Entry->PipelineId = PipelineId;
	if (!AgentSessionId.IsEmpty()) Entry->AgentSessionId = AgentSessionId;
	Entry->Import = Import;
	Entry->Folder = Folder;
	Entry->LastPolledAt = -1000;
	EnsureTicking();
}

void FZettaRunFollower::Unfollow(int64 RunId)
{
	Runs.RemoveAll([RunId](const TSharedPtr<FFollowed>& E) { return E->RunId == RunId; });
	Latest.Remove(RunId);
}

void FZettaRunFollower::FollowRuns(int64 PipelineId)
{
	Pipelines.AddUnique(PipelineId);
	EnsureTicking();
}

void FZettaRunFollower::UnfollowRuns(int64 PipelineId) { Pipelines.Remove(PipelineId); }

bool FZettaRunFollower::Tick(float)
{
	if (Runs.Num() == 0 && Pipelines.Num() == 0)
	{
		TickHandle.Reset();
		return false;
	}
	if (!FZettaAuth::Get().IsSignedIn()) return true;
	const double Now = FPlatformTime::Seconds();
	for (const TSharedPtr<FFollowed>& Entry : TArray<TSharedPtr<FFollowed>>(Runs))
	{
		const double Due = Entry->Status == ZettaRunStatus::Draft ? DraftPollSeconds : PollSeconds - 0.25;
		if (Entry->bInFlight || Entry->ImportsRunning > 0 || Now - Entry->LastPolledAt < Due) continue;
		Poll(Entry);
	}
	if (Now - LastListPoll >= PollSeconds)
	{
		LastListPoll = Now;
		for (const int64 PipelineId : TArray<int64>(Pipelines)) PollList(PipelineId);
	}
	return true;
}

void FZettaRunFollower::Poll(const TSharedPtr<FFollowed>& Entry)
{
	Entry->bInFlight = true;
	Entry->LastPolledAt = FPlatformTime::Seconds();
	TWeakPtr<FFollowed> Weak = Entry;
	ZettaPipelinesApi::GetRun(Entry->RunId, [this, Weak](const FZettaResponse& R, const FZettaPipelineRunPtr& Run)
	{
		const TSharedPtr<FFollowed> E = Weak.Pin();
		if (!E.IsValid()) return;
		E->bInFlight = false;
		if (R.Status == 404) Unfollow(E->RunId);
		else if (R.bOk && Run.IsValid()) Handle(E, Run);
		// Anything else is transient (offline, 5xx): next round.
	});
}

void FZettaRunFollower::PollList(int64 PipelineId)
{
	if (ListsInFlight.Contains(PipelineId)) return;
	ListsInFlight.Add(PipelineId);
	ZettaPipelinesApi::Runs(PipelineId, [this, PipelineId](const FZettaResponse& R, const TArray<FZettaRunSummary>& List)
	{
		ListsInFlight.Remove(PipelineId);
		if (R.Status == 404)
		{
			UnfollowRuns(PipelineId);
			return;
		}
		if (!R.bOk) return;
		OnRunsUpdated.Broadcast(PipelineId, List);
		// The list is polled only while something in it moves.
		bool bLive = false;
		for (const FZettaRunSummary& S : List) bLive |= S.IsLive();
		if (!bLive) UnfollowRuns(PipelineId);
	});
}

void FZettaRunFollower::Refresh(int64 RunId, ZettaPipelinesApi::FRunDone Done)
{
	ZettaPipelinesApi::GetRun(RunId, [this, Done](const FZettaResponse& R, const FZettaPipelineRunPtr& Run)
	{
		if (R.bOk && Run.IsValid())
		{
			if (const TSharedPtr<FFollowed> Entry = Find(Run->Id)) Handle(Entry, Run);
			else
			{
				Latest.Add(Run->Id, Run);
				OnRunUpdated.Broadcast(Run);
			}
		}
		if (Done) Done(R, Run);
	});
}

void FZettaRunFollower::Report(const FZettaPipelineRunPtr& Run)
{
	if (!Run.IsValid()) return;
	if (const TSharedPtr<FFollowed> Entry = Find(Run->Id))
	{
		EnsureTicking();
		Handle(Entry, Run);
	}
}

void FZettaRunFollower::Handle(const TSharedPtr<FFollowed>& Entry, const FZettaPipelineRunPtr& Run)
{
	if (Entry->bFinished) return;
	const bool bNowAwaiting = Run->Status == ZettaRunStatus::Awaiting && Entry->Status != ZettaRunStatus::Awaiting;
	Entry->Status = Run->Status;
	Entry->LastPolledAt = FPlatformTime::Seconds();
	if (Entry->PipelineId == 0) Entry->PipelineId = Run->PipelineId;
	Latest.Add(Run->Id, Run);
	OnRunUpdated.Broadcast(Run);

	TArray<int64> Queue;
	if (Entry->Import != EZettaRunImport::None)
	{
		for (const FZettaPipelineRunStep* Step : Run->ImportableSteps(Entry->Import))
		{
			if (Entry->ImportedSteps.Contains(Step->Id)) continue;
			Entry->ImportedSteps.Add(Step->Id);
			Queue.Add(Step->Id);
		}
	}
	if (Queue.Num() == 0)
	{
		AfterImports(Entry, Run, bNowAwaiting);
		return;
	}
	// The finish is reported once its imports are in, so the paths go with it.
	++Entry->ImportsRunning;
	ImportNext(Entry, Run, MoveTemp(Queue), bNowAwaiting);
}

void FZettaRunFollower::ImportNext(const TSharedPtr<FFollowed>& Entry, const FZettaPipelineRunPtr& Run, TArray<int64> Queue, bool bNowAwaiting)
{
	if (Queue.Num() == 0)
	{
		--Entry->ImportsRunning;
		AfterImports(Entry, Run, bNowAwaiting);
		return;
	}
	const int64 StepId = Queue[0];
	Queue.RemoveAt(0);
	const FZettaPipelineRunStep* Step = Run->Step(StepId);
	if (!Step || !Step->Generation.IsValid())
	{
		ImportNext(Entry, Run, MoveTemp(Queue), bNowAwaiting);
		return;
	}
	FZettaImportOptions Options;
	Options.Folder = Entry->Folder;
	Options.bPlace = false;
	Options.bSelect = false;
	ZettaImporter::Import(Step->Generation, Options, [this, Entry, Run, Queue, bNowAwaiting](const FZettaImportResult& Result)
	{
		if (Result.bOk) Entry->ImportedPaths.Append(Result.AssetPaths);
		else UE_LOG(LogZetta, Warning, TEXT("A pipeline step finished, but its import failed: %s"), *Result.Error);
		ImportNext(Entry, Run, Queue, bNowAwaiting);
	});
}

void FZettaRunFollower::AfterImports(const TSharedPtr<FFollowed>& Entry, const FZettaPipelineRunPtr& Run, bool bNowAwaiting)
{
	if (Entry->bFinished) return;
	if (bNowAwaiting) OnRunNeedsUser.Broadcast(Run, *Entry);
	// Imports still going (from an earlier read): the next read reports the finish.
	if (Entry->ImportsRunning > 0) return;
	if (Run->Status == ZettaRunStatus::Done || Run->Status == ZettaRunStatus::Failed || Run->Status == ZettaRunStatus::Cancelled)
	{
		Entry->bFinished = true;
		const FFollowed Copy = *Entry;
		Unfollow(Run->Id);
		OnRunFinished.Broadcast(Run, Copy);
	}
}

namespace ZettaRunImportChain
{
	struct FState
	{
		TArray<FZettaGenerationPtr> Generations;
		TArray<FString> NodeIds;
		TArray<FString> Paths;
		TArray<FString> Failures;
		int32 Next = 0;
		FZettaImportOptions Options;
		TFunction<void(const TArray<FString>&, const TArray<FString>&)> Done;
	};

	/** One import after another: the importer works on the game thread and places nothing at the same time. */
	static void Continue(const TSharedRef<FState>& State)
	{
		if (State->Next >= State->Generations.Num())
		{
			if (State->Done) State->Done(State->Paths, State->Failures);
			return;
		}
		const int32 I = State->Next++;
		ZettaImporter::Import(State->Generations[I], State->Options, [State, I](const FZettaImportResult& Result)
		{
			if (Result.bOk) State->Paths.Append(Result.AssetPaths);
			else State->Failures.Add(FString::Printf(TEXT("%s: %s"), *State->NodeIds[I], *Result.Error));
			Continue(State);
		});
	}
}

void FZettaRunFollower::Import(const FZettaPipelineRunPtr& Run, const TArray<FString>& NodeIds, const FZettaImportOptions& Options,
	TFunction<void(const TArray<FString>&, const TArray<FString>&)> Done)
{
	TSharedRef<ZettaRunImportChain::FState> State = MakeShared<ZettaRunImportChain::FState>();
	State->Options = Options;
	State->Done = MoveTemp(Done);
	if (Run.IsValid())
	{
		for (const FZettaPipelineRunStep* S : Run->ImportableSteps(NodeIds.Num() == 0 ? EZettaRunImport::FinalSteps : EZettaRunImport::AllMeshSteps))
		{
			if (NodeIds.Num() > 0 && !NodeIds.Contains(S->NodeId)) continue;
			State->Generations.Add(S->Generation);
			State->NodeIds.Add(S->NodeId);
		}
	}
	ZettaRunImportChain::Continue(State);
}

// ─── Hooks ──────────────────────────────────────────────────────────────────────────────────────────────────────────

namespace ZettaPipelineHooks
{
	static TOptional<FOpenRequest>& Pending()
	{
		static TOptional<FOpenRequest> Request;
		return Request;
	}

	void RequestOpen(int64 PipelineId, TOptional<int64> RunId)
	{
		Pending() = FOpenRequest{ PipelineId, RunId };
		FZetta3DEditorModule::Get().OpenWindow(TEXT("Pipelines"));
		OnOpenRequested().Broadcast();
	}

	TOptional<FOpenRequest> TakePending()
	{
		TOptional<FOpenRequest> Out = Pending();
		Pending().Reset();
		return Out;
	}

	FSimpleMulticastDelegate& OnOpenRequested()
	{
		static FSimpleMulticastDelegate Delegate;
		return Delegate;
	}

	FOnSaved& OnSavedElsewhere()
	{
		static FOnSaved Delegate;
		return Delegate;
	}
}
