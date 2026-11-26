// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaApi.h"

#include "GenericPlatform/GenericPlatformHttp.h"
#include "Misc/Base64.h"
#include "Misc/Guid.h"
#include "ZettaConfig.h"
#include "ZettaJson.h"

using namespace ZettaJson;

namespace ZettaApi
{
	static TMap<FString, FString> Idempotent(const FString& Key = FString())
	{
		return { { TEXT("Idempotency-Key"), Key.IsEmpty() ? FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower) : Key } };
	}

	static FString Encode(const FString& S) { return FGenericPlatformHttp::UrlEncode(S); }

	void Me(TZettaDone<FZettaUser> Done)
	{
		FZettaHttp::Get(TEXT("/auth/me"), [Done](const FZettaResponse& R)
		{
			Done(R, R.bOk ? FZettaUser::From(Obj(R.Object(), TEXT("user"))) : FZettaUser());
		});
	}

	void Entitlements3D(TZettaDone<FZettaEntitlements3D> Done)
	{
		FZettaHttp::Get(TEXT("/3d/entitlements"), [Done](const FZettaResponse& R)
		{
			Done(R, FZettaEntitlements3D::From(Obj(R.Object(), TEXT("entitlements"))));
		});
	}

	void Pricing(TZettaDone<FZettaPriceList> Done)
	{
		FZettaHttp::Get(TEXT("/3d/pricing"), [Done](const FZettaResponse& R) { Done(R, FZettaPriceList::From(R.Object())); });
	}

	void Quote(const FString& Base, const TArray<FString>& Modifiers, const FString& Model, TZettaDone<FZettaQuote> Done)
	{
		TSharedRef<FJsonObject> Generation = MakeShared<FJsonObject>();
		Generation->SetStringField(TEXT("base"), Base);
		TArray<FString> Unique;
		for (const FString& M : Modifiers) Unique.AddUnique(M);
		Generation->SetField(TEXT("modifiers"), StringArray(Unique));
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetObjectField(TEXT("generation"), Generation);
		if (!Model.IsEmpty()) Body->SetStringField(TEXT("model"), Model);
		FZettaHttp::Post(TEXT("/3d/quote"), Body, [Done](const FZettaResponse& R) { Done(R, FZettaQuote::From(R.Object())); });
	}

	static void CreateUploadTicket(const FString& ContentType, TZettaDone<FZettaUploadTicket> Done)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("contentType"), ContentType);
		FZettaHttp::Post(TEXT("/3d/uploads"), Body, [Done](const FZettaResponse& R) { Done(R, FZettaUploadTicket::From(R.Object())); });
	}

	void Upload(const TArray<uint8>& Bytes, const FString& ContentType, TZettaDone<FZettaUploadTicket> Done, FZettaProgress Progress)
	{
		TSharedRef<TArray<uint8>> Data = MakeShared<TArray<uint8>>(Bytes);
		TSharedRef<TFunction<void(bool)>> Attempt = MakeShared<TFunction<void(bool)>>();
		*Attempt = [=](bool bRetried)
		{
			CreateUploadTicket(ContentType, [=](const FZettaResponse& R, const FZettaUploadTicket& Ticket)
			{
				if (!R.bOk)
				{
					Done(R, Ticket);
					*Attempt = nullptr;
					return;
				}
				TMap<FString, FString> Headers = Ticket.Headers;
				if (!Headers.Contains(TEXT("Content-Type"))) Headers.Add(TEXT("Content-Type"), ContentType);
				FZettaHttp::PutBytes(Ticket.UploadUrl, *Data, Headers, [=](const FZettaResponse& Put)
				{
					if (!Put.bOk && Put.Code == TEXT("UPLOAD_TICKET_EXPIRED") && !bRetried)
					{
						(*Attempt)(true);
						return;
					}
					Done(Put, Ticket);
					*Attempt = nullptr;
				}, Progress);
			});
		};
		(*Attempt)(false);
	}

	void CreateUploadPost(const FString& Key, int64 Bytes, const FString& Title, TZettaDone<FZettaGenerationPtr> Done)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("key"), Key);
		Body->SetNumberField(TEXT("bytes"), (double)Bytes);
		Body->SetStringField(TEXT("feature"), TEXT("upload"));
		if (!Title.TrimStartAndEnd().IsEmpty()) Body->SetStringField(TEXT("title"), Title.TrimStartAndEnd().Left(120));
		FZettaHttp::Post(TEXT("/3d/uploads/post"), Body, [Done](const FZettaResponse& R)
		{
			const TSharedPtr<FJsonObject> G = Obj(R.Object(), TEXT("generation"));
			Done(R, G.IsValid() ? FZettaGenerationPtr(FZettaGeneration::From(G)) : nullptr);
		});
	}

	void CreateGeneration(const TSharedRef<FJsonObject>& Body, TZettaDone<FZettaGenerationPtr> Done, const FString& IdempotencyKey)
	{
		Body->SetStringField(TEXT("origin"), ZettaConfig::ClientName);
		FZettaHttp::Post(TEXT("/3d/generations"), Body, [Done](const FZettaResponse& R)
		{
			const TSharedPtr<FJsonObject> G = Obj(R.Object(), TEXT("generation"));
			Done(R, G.IsValid() ? FZettaGenerationPtr(FZettaGeneration::From(G)) : nullptr);
		}, Idempotent(IdempotencyKey));
	}

	void GetGeneration(int64 Id, TFunction<void(const FZettaResponse&, const FZettaGenerationPtr&, TOptional<int32>)> Done)
	{
		FZettaHttp::Get(FString::Printf(TEXT("/3d/generations/%lld"), Id), [Done](const FZettaResponse& R)
		{
			const TSharedPtr<FJsonObject> G = Obj(R.Object(), TEXT("generation"));
			const TOptional<int64> Position = OptInt(R.Object(), TEXT("queuePosition"));
			Done(R, G.IsValid() ? FZettaGenerationPtr(FZettaGeneration::From(G)) : nullptr,
				Position.IsSet() ? TOptional<int32>((int32)Position.GetValue()) : TOptional<int32>());
		}, 30.f);
	}

	void ListGenerations(const FListQuery& Q, TZettaDone<FZettaGenerationPage> Done)
	{
		TArray<FString> Parts = { FString::Printf(TEXT("page=%d"), Q.Page), FString::Printf(TEXT("pageSize=%d"), Q.PageSize), TEXT("sortDir=DESC") };
		// Each pipeline run as one entry carrying its models, so a run is never split across pages.
		if (Q.bGroupRuns) Parts.Add(TEXT("groupRuns=true"));
		if (!Q.Search.TrimStartAndEnd().IsEmpty()) Parts.Add(TEXT("search=") + Encode(Q.Search.TrimStartAndEnd()));
		if (!Q.Feature.IsEmpty()) Parts.Add(TEXT("feature=") + Encode(Q.Feature));
		// Arrays repeat the key: the API rejects the status[]= form.
		for (const FString& S : Q.Statuses) Parts.Add(TEXT("status=") + Encode(S));
		FZettaHttp::Get(TEXT("/3d/generations?") + FString::Join(Parts, TEXT("&")), [Done](const FZettaResponse& R)
		{
			FZettaGenerationPage Page;
			const TSharedPtr<FJsonObject> O = R.Object();
			for (const TSharedPtr<FJsonObject>& Item : Objects(O, TEXT("items"))) Page.Items.Add(FZettaGeneration::From(Item));
			Page.Total = (int32)Int(O, TEXT("total"));
			Page.Page = (int32)Int(O, TEXT("page"), 1);
			Page.PageSize = (int32)Int(O, TEXT("pageSize"), 24);
			Done(R, Page);
		});
	}

	void DownloadUrl(int64 Id, const FString& Key, TZettaDone<FString> Done)
	{
		FString Path = FString::Printf(TEXT("/3d/generations/%lld/download"), Id);
		if (!Key.IsEmpty()) Path += TEXT("?key=") + Encode(Key);
		FZettaHttp::Get(Path, [Done](const FZettaResponse& R) { Done(R, Str(R.Object(), TEXT("url"))); });
	}

	void UpdateGeneration(int64 Id, const TOptional<FString>& Title, const TOptional<TArray<FString>>& Tags, const TOptional<FString>& Availability, TZettaDone<FZettaGenerationPtr> Done)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		if (Title.IsSet()) Body->SetStringField(TEXT("title"), Title.GetValue());
		if (Tags.IsSet()) Body->SetField(TEXT("tags"), StringArray(Tags.GetValue()));
		if (Availability.IsSet()) Body->SetStringField(TEXT("availability"), Availability.GetValue());
		FZettaHttp::Patch(FString::Printf(TEXT("/3d/generations/%lld"), Id), Body, [Done](const FZettaResponse& R)
		{
			const TSharedPtr<FJsonObject> G = Obj(R.Object(), TEXT("generation"));
			Done(R, G.IsValid() ? FZettaGenerationPtr(FZettaGeneration::From(G)) : nullptr);
		});
	}

	void CancelGeneration(int64 Id, TZettaDone<FZettaGenerationPtr> Done)
	{
		FZettaHttp::Post(FString::Printf(TEXT("/3d/generations/%lld/cancel"), Id), nullptr, [Done](const FZettaResponse& R)
		{
			const TSharedPtr<FJsonObject> G = Obj(R.Object(), TEXT("generation"));
			Done(R, G.IsValid() ? FZettaGenerationPtr(FZettaGeneration::From(G)) : nullptr);
		});
	}

	void DeleteGeneration(int64 Id, FZettaCallback Done)
	{
		FZettaHttp::Delete(FString::Printf(TEXT("/3d/generations/%lld"), Id), MoveTemp(Done));
	}

	void RemoveBackground(const TArray<uint8>& Image, TZettaDone<TArray<uint8>> Done)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("image"), FBase64::Encode(Image));
		FZettaHttp::Post(TEXT("/tools/rembg"), Body, [Done](const FZettaResponse& R)
		{
			TArray<uint8> Bytes;
			if (R.bOk) FBase64::Decode(Str(R.Object(), TEXT("image")), Bytes);
			Done(R, Bytes);
		}, {}, 120.f);
	}

	static TArray<TSharedPtr<FJsonValue>> Frames(const TArray<TArray<uint8>>& Jpegs)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (int32 i = 0; i < FMath::Min(4, Jpegs.Num()); ++i)
		{
			TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
			F->SetStringField(TEXT("data"), FBase64::Encode(Jpegs[i]));
			F->SetStringField(TEXT("mediaType"), TEXT("image/jpeg"));
			Out.Add(MakeShared<FJsonValueObject>(F));
		}
		return Out;
	}

	void SuggestTags(const TArray<TArray<uint8>>& JpegFrames, TFunction<void(const FZettaResponse&, const FString&, const TArray<FString>&)> Done)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetArrayField(TEXT("frames"), Frames(JpegFrames));
		FZettaHttp::Post(TEXT("/ai/tags"), Body, [Done](const FZettaResponse& R)
		{
			Done(R, Str(R.Object(), TEXT("category")), Strings(R.Object(), TEXT("tags")));
		}, {}, 90.f);
	}

	void RiggingPrecheck(const TArray<TArray<uint8>>& JpegFrames, TFunction<void(const FZettaResponse&, const FString&, const FString&, const FString&)> Done)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetArrayField(TEXT("frames"), Frames(JpegFrames));
		FZettaHttp::Post(TEXT("/ai/rigging-precheck"), Body, [Done](const FZettaResponse& R)
		{
			const TSharedPtr<FJsonObject> O = R.Object();
			Done(R, Str(O, TEXT("bodyType")), Str(O, TEXT("confidence")), Str(O, TEXT("reason")));
		}, {}, 90.f);
	}

	void Entitlements2D(TZettaDone<FZettaEntitlements2D> Done)
	{
		FZettaHttp::Get(TEXT("/2d/entitlements"), [Done](const FZettaResponse& R)
		{
			Done(R, FZettaEntitlements2D::From(Obj(R.Object(), TEXT("entitlements"))));
		});
	}

	void ImageModels(TZettaDone<FZettaImageModels> Done)
	{
		FZettaHttp::Get(TEXT("/2d/models"), [Done](const FZettaResponse& R)
		{
			FZettaImageModels Models;
			Models.Default = Str(R.Object(), TEXT("default"));
			for (const TSharedPtr<FJsonObject>& M : Objects(R.Object(), TEXT("models")))
			{
				FZettaImageModel Model;
				Model.Id = Str(M, TEXT("id"));
				Model.Label = Str(M, TEXT("label"));
				Model.Resolutions = Strings(M, TEXT("resolutions"));
				Model.bLocked = Bool(M, TEXT("locked"));
				Model.LockedReason = Str(M, TEXT("lockedReason"));
				Models.Models.Add(Model);
			}
			Done(R, Models);
		});
	}

	static TSharedRef<FJsonObject> ImageBody(const FString& Prompt, const FImageOptions& O)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("prompt"), Prompt);
		if (O.bFor3D) Body->SetBoolField(TEXT("for3D"), true);
		else
		{
			if (!O.AspectRatio.IsEmpty()) Body->SetStringField(TEXT("aspectRatio"), O.AspectRatio);
			if (!O.Resolution.IsEmpty()) Body->SetStringField(TEXT("resolution"), O.Resolution);
		}
		if (!O.Model.IsEmpty()) Body->SetStringField(TEXT("model"), O.Model);
		return Body;
	}

	static TArray<TSharedPtr<FJsonValue>> ImageArray(const TArray<FZettaInlineImage>& Images, int32 Max)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (int32 i = 0; i < FMath::Min(Max, Images.Num()); ++i) Out.Add(MakeShared<FJsonValueObject>(Images[i].ToJson()));
		return Out;
	}

	static FZettaCallback ImageDone(TZettaDone<FZettaImageResult> Done)
	{
		return [Done](const FZettaResponse& R) { Done(R, FZettaImageResult::From(R.Object())); };
	}

	void GenerateImage(const FString& Prompt, const FImageOptions& Options, const TArray<FZettaInlineImage>& StyleReferences, TZettaDone<FZettaImageResult> Done)
	{
		TSharedRef<FJsonObject> Body = ImageBody(Prompt, Options);
		if (StyleReferences.Num() > 0) Body->SetArrayField(TEXT("styleReferences"), ImageArray(StyleReferences, 3));
		FZettaHttp::Post(TEXT("/2d/generate"), Body, ImageDone(Done), Idempotent(), 180.f);
	}

	void EditImage(const FString& Prompt, const FImageOptions& Options, const TOptional<FZettaInlineImage>& Image, TOptional<int64> SourcePostId, TZettaDone<FZettaImageResult> Done)
	{
		TSharedRef<FJsonObject> Body = ImageBody(Prompt, Options);
		if (Image.IsSet()) Body->SetObjectField(TEXT("image"), Image.GetValue().ToJson());
		else if (SourcePostId.IsSet()) Body->SetNumberField(TEXT("sourcePostId"), (double)SourcePostId.GetValue());
		FZettaHttp::Post(TEXT("/2d/img-edit"), Body, ImageDone(Done), Idempotent(), 180.f);
	}

	void ComposeImages(const FString& Prompt, const FImageOptions& Options, const TArray<FZettaInlineImage>& Images, const TArray<int64>& SourcePostIds, TZettaDone<FZettaImageResult> Done)
	{
		TSharedRef<FJsonObject> Body = ImageBody(Prompt, Options);
		if (Images.Num() > 0) Body->SetArrayField(TEXT("images"), ImageArray(Images, 14));
		if (SourcePostIds.Num() > 0)
		{
			TArray<double> Ids;
			for (const int64 Id : SourcePostIds) Ids.Add((double)Id);
			Body->SetField(TEXT("sourcePostIds"), NumberArray(Ids));
		}
		FZettaHttp::Post(TEXT("/2d/img-compose"), Body, ImageDone(Done), Idempotent(), 240.f);
	}

	void ListImages(int32 Page, int32 PageSize, const FString& Search, TFunction<void(const FZettaResponse&, const TArray<FZettaImagePost>&, int32)> Done)
	{
		FString Path = FString::Printf(TEXT("/2d?page=%d&pageSize=%d"), Page, PageSize);
		if (!Search.TrimStartAndEnd().IsEmpty()) Path += TEXT("&search=") + Encode(Search.TrimStartAndEnd());
		FZettaHttp::Get(Path, [Done](const FZettaResponse& R)
		{
			TArray<FZettaImagePost> Posts;
			for (const TSharedPtr<FJsonObject>& Item : Objects(R.Object(), TEXT("items"))) Posts.Add(FZettaImagePost::From(Item));
			Done(R, Posts, (int32)Int(R.Object(), TEXT("total")));
		});
	}

	void DeleteImage(int64 Id, FZettaCallback Done)
	{
		FZettaHttp::Delete(FString::Printf(TEXT("/2d/%lld"), Id), MoveTemp(Done));
	}
}
