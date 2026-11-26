// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaModels.h"

#include "Misc/Base64.h"
#include "ZettaCatalog.h"
#include "ZettaJson.h"

using namespace ZettaJson;

FZettaCreditPool FZettaCreditPool::From(const TSharedPtr<FJsonObject>& O)
{
	FZettaCreditPool P;
	P.Monthly = Int(O, TEXT("monthly"));
	P.Purchased = Int(O, TEXT("purchased"));
	P.BonusMonthly = Int(O, TEXT("bonusMonthly"));
	P.Bonus = Int(O, TEXT("bonus"));
	return P;
}

FZettaUser FZettaUser::From(const TSharedPtr<FJsonObject>& O)
{
	FZettaUser U;
	U.Id = Int(O, TEXT("id"));
	U.Email = Str(O, TEXT("email"));
	U.DisplayName = Str(O, TEXT("displayName"));
	U.ProfilePicture = Str(O, TEXT("profilePicture"));
	U.PaymentPlan = Str(O, TEXT("paymentPlan"));
	U.SubscriptionStatus = Str(O, TEXT("subscriptionStatus"));
	const TSharedPtr<FJsonObject> Credits = Obj(O, TEXT("credits"));
	U.Credits3D = FZettaCreditPool::From(Obj(Credits, TEXT("threeD")));
	U.CreditsAi = FZettaCreditPool::From(Obj(Credits, TEXT("ai")));
	return U;
}

FString FZettaUser::PlanLabel() const
{
	if (PaymentPlan == TEXT("creator_pro")) return TEXT("Creator Pro");
	if (IsFree()) return TEXT("Free");
	FString Label = PaymentPlan.Replace(TEXT("_"), TEXT(" "));
	if (Label.Len() > 0) Label[0] = FChar::ToUpper(Label[0]);
	return Label;
}

bool FZettaUser::IsFree() const
{
	return PaymentPlan.IsEmpty() || PaymentPlan == TEXT("none") || PaymentPlan == TEXT("free");
}

FZettaEntitlements3D FZettaEntitlements3D::From(const TSharedPtr<FJsonObject>& O)
{
	FZettaEntitlements3D E;
	E.MaxMultiViewImages = OptInt(O, TEXT("maxMultiViewImages"));
	E.MaxFaceCount = OptInt(O, TEXT("maxFaceCount"));
	E.MaxConcurrentJobs = OptInt(O, TEXT("maxConcurrentJobs"));
	E.AllowsLowPoly = OptBool(O, TEXT("allowsLowPoly"));
	E.AllowsQuadMesh = OptBool(O, TEXT("allowsQuadMesh"));
	E.AllowsCustomFaceCount = OptBool(O, TEXT("allowsCustomFaceCount"));
	E.AllowsZettaN25 = OptBool(O, TEXT("allowsZettaN25"));
	E.AllowsZettaNp1 = OptBool(O, TEXT("allowsZettaNp1"));
	E.AllowsPostProcess = OptBool(O, TEXT("allowsPostProcess"));
	E.AllowsPrivateGenerations = OptBool(O, TEXT("allowsPrivateGenerations"));
	return E;
}

FZettaEntitlements2D FZettaEntitlements2D::From(const TSharedPtr<FJsonObject>& O)
{
	FZettaEntitlements2D E;
	E.AllowsImageEditing = OptBool(O, TEXT("allowsImageEditing"));
	E.Allows4kImages = OptBool(O, TEXT("allows4kImages"));
	E.AllowsProImageModel = OptBool(O, TEXT("allowsProImageModel"));
	return E;
}

TOptional<int32> FZettaPriceList::Price(const FString& Code, const FString& Model) const
{
	for (const FZettaPriceItem& I : Items) if (I.Code == Code && I.Model == Model) return I.Credits;
	for (const FZettaPriceItem& I : Items) if (I.Code == Code && I.Model.IsEmpty()) return I.Credits;
	return {};
}

TOptional<int32> FZettaPriceList::Total(const FString& Model, const TArray<FString>& Codes) const
{
	int32 Sum = 0;
	for (const FString& Code : Codes)
	{
		const TOptional<int32> P = Price(Code, Model);
		if (!P.IsSet()) return {};
		Sum += P.GetValue();
	}
	return Sum;
}

FZettaPriceList FZettaPriceList::From(const TSharedPtr<FJsonObject>& O)
{
	FZettaPriceList L;
	for (const TSharedPtr<FJsonObject>& I : Objects(O, TEXT("items")))
	{
		FZettaPriceItem Item;
		Item.Code = Str(I, TEXT("code"));
		Item.Kind = Str(I, TEXT("kind"));
		Item.Label = Str(I, TEXT("label"));
		Item.Model = Str(I, TEXT("model"));
		Item.Credits = (int32)Int(I, TEXT("credits"));
		L.Items.Add(Item);
	}
	return L;
}

FZettaResultFile FZettaResultFile::From(const TSharedPtr<FJsonObject>& O)
{
	FZettaResultFile F;
	F.Type = Str(O, TEXT("type"));
	F.Folder = Str(O, TEXT("folder"));
	F.Key = Str(O, TEXT("key"));
	F.Url = Str(O, TEXT("url"));
	F.Label = Str(O, TEXT("label"));
	F.Bytes = Int(O, TEXT("bytes"));
	return F;
}

FString FZettaResultFile::Extension() const
{
	FString Name = Key.IsEmpty() ? Url : Key;
	int32 Q = INDEX_NONE;
	if (Name.FindChar(TEXT('?'), Q)) Name = Name.Left(Q);
	int32 Dot = INDEX_NONE;
	if (Name.FindLastChar(TEXT('.'), Dot)) return Name.Mid(Dot + 1).ToLower();
	return Folder.ToLower();
}

FString FZettaResultFile::FileName() const
{
	int32 Slash = INDEX_NONE;
	return Key.FindLastChar(TEXT('/'), Slash) ? Key.Mid(Slash + 1) : Key;
}

bool FZettaResultFile::IsPicture() const
{
	static const TSet<FString> Pictures = { TEXT("IMAGE"), TEXT("WEBP"), TEXT("PNG"), TEXT("JPG"), TEXT("JPEG"), TEXT("UV_LAYOUT") };
	return Folder == TEXT("thumbnail") || Folder == TEXT("gifs") || Folder == TEXT("uv") || Label == TEXT("UV layout") || Pictures.Contains(Type.ToUpper());
}

bool FZettaResultFile::IsSplat() const
{
	static const TSet<FString> Splats = { TEXT("SPLAT"), TEXT("SPZ"), TEXT("PLY") };
	const FString Ext = Extension();
	return Folder == TEXT("splat") || Splats.Contains(Type.ToUpper()) || Ext == TEXT("splat") || Ext == TEXT("spz");
}

TSharedRef<FZettaGeneration> FZettaGeneration::From(const TSharedPtr<FJsonObject>& O)
{
	TSharedRef<FZettaGeneration> G = MakeShared<FZettaGeneration>();
	G->Raw = O;
	G->Id = Int(O, TEXT("id"));
	G->Status = Str(O, TEXT("status"));
	G->Feature = Str(O, TEXT("selectedFeature"));
	G->Model = Str(O, TEXT("selectedModel"));
	G->Title = Str(O, TEXT("title"));
	G->Availability = Str(O, TEXT("availability"));
	G->InputPrompt = Str(O, TEXT("inputPrompt"));
	G->ThumbnailUrl = Str(O, TEXT("thumbnailURL"));
	G->PolygonType = Str(O, TEXT("polygonType"));
	G->ErrorMessage = Str(O, TEXT("errorMessage"));
	G->Tags = Strings(O, TEXT("tags"));
	for (const TSharedPtr<FJsonObject>& I : Objects(O, TEXT("inputImages"))) G->InputImageUrls.Add(Str(I, TEXT("url")));
	G->FaceCount = OptInt(O, TEXT("faceCount"));
	G->PipelineRunId = OptInt(O, TEXT("pipelineRunId"));
	for (const TSharedPtr<FJsonObject>& F : Objects(Obj(O, TEXT("result")), TEXT("files"))) G->Files.Add(FZettaResultFile::From(F));
	FDateTime::ParseIso8601(*Str(O, TEXT("createdAt")), G->CreatedAt);
	FDateTime::ParseIso8601(*Str(O, TEXT("updatedAt")), G->UpdatedAt);
	G->JobSpec = Obj(O, TEXT("jobSpec"));

	if (const TSharedPtr<FJsonObject> Group = Obj(O, TEXT("runGroup")))
	{
		G->RunGroup = MakeShared<FZettaRunGroup>();
		G->RunGroup->RunId = Int(Group, TEXT("runId"));
		G->RunGroup->PipelineId = OptInt(Group, TEXT("pipelineId"));
		G->RunGroup->PipelineTitle = Str(Group, TEXT("pipelineTitle"));
		G->RunGroup->Count = (int32)Int(Group, TEXT("count"));
		for (const TSharedPtr<FJsonObject>& M : Objects(Group, TEXT("members"))) G->RunGroup->Members.Add(FZettaGeneration::From(M));
	}
	if (const TSharedPtr<FJsonObject> Step = Obj(O, TEXT("runStep")))
	{
		FZettaRunStep S;
		S.Position = (int32)Int(Step, TEXT("position"));
		S.Label = Str(Step, TEXT("label"));
		S.Part = Str(Step, TEXT("part"));
		G->RunStep = S;
	}
	return G;
}

TArray<FZettaResultFile> FZettaGeneration::MeshFiles() const
{
	return Files.FilterByPredicate([](const FZettaResultFile& F) { return !F.IsPicture() && !F.IsSplat(); });
}

const FZettaResultFile* FZettaGeneration::UvLayout() const
{
	return Files.FindByPredicate([](const FZettaResultFile& F) { return F.Label == TEXT("UV layout") || F.Folder == TEXT("uv"); });
}

bool FZettaGeneration::IsSplat() const
{
	if (ZettaCatalog::IsSplatFeature(Feature)) return true;
	return Files.Num() > 0 && MeshFiles().Num() == 0 && Files.ContainsByPredicate([](const FZettaResultFile& F) { return F.IsSplat(); });
}

FString FZettaGeneration::DisplayTitle() const
{
	if (!Title.TrimStartAndEnd().IsEmpty()) return Title;
	if (!InputPrompt.TrimStartAndEnd().IsEmpty()) return InputPrompt.Len() <= 48 ? InputPrompt : InputPrompt.Left(47) + TEXT("…");
	return FString::Printf(TEXT("%s #%lld"), *ZettaCatalog::FeatureLabel(Feature), Id);
}

FString FZettaGeneration::StatusLabel() const
{
	if (Status == TEXT("pending")) return TEXT("Waiting");
	if (Status == TEXT("queued")) return TEXT("Queued");
	if (Status == TEXT("processing")) return TEXT("Generating");
	if (Status == TEXT("saving")) return TEXT("Saving");
	if (Status == TEXT("success")) return TEXT("Ready");
	if (Status == TEXT("failed")) return TEXT("Failed");
	if (Status == TEXT("cancelled")) return TEXT("Cancelled");
	return Status;
}

FString FZettaGeneration::SpecBase() const
{
	return Str(Obj(JobSpec, TEXT("generation")), TEXT("base"));
}

TSet<FString> FZettaGeneration::SpecModifiers() const
{
	return TSet<FString>(Strings(Obj(JobSpec, TEXT("generation")), TEXT("modifiers")));
}

FZettaQuote FZettaQuote::From(const TSharedPtr<FJsonObject>& O)
{
	FZettaQuote Q;
	Q.Credits = (int32)Int(O, TEXT("credits"));
	for (const TSharedPtr<FJsonObject>& L : Objects(O, TEXT("lines"))) Q.Lines.Emplace(Str(L, TEXT("label")), (int32)Int(L, TEXT("credits")));
	return Q;
}

FZettaUploadTicket FZettaUploadTicket::From(const TSharedPtr<FJsonObject>& O)
{
	FZettaUploadTicket T;
	T.UploadUrl = Str(O, TEXT("uploadUrl"));
	T.Method = Str(O, TEXT("method"), TEXT("PUT"));
	T.Key = Str(O, TEXT("key"));
	T.FileUrl = Str(O, TEXT("fileUrl"));
	if (const TSharedPtr<FJsonObject> H = Obj(O, TEXT("headers")))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : H->Values) T.Headers.Add(Pair.Key, AsText(Pair.Value));
	}
	return T;
}

FZettaImageResult FZettaImageResult::From(const TSharedPtr<FJsonObject>& O)
{
	FZettaImageResult R;
	R.PostId = OptInt(O, TEXT("postId"));
	R.Text = Str(O, TEXT("text"));
	R.Model = Str(O, TEXT("model"));
	for (const TSharedPtr<FJsonObject>& I : Objects(O, TEXT("images")))
	{
		TArray<uint8> Bytes;
		FBase64::Decode(Str(I, TEXT("base64")), Bytes);
		R.Images.Add(MoveTemp(Bytes));
		R.ContentTypes.Add(Str(I, TEXT("contentType"), TEXT("image/png")));
	}
	return R;
}

FZettaImagePost FZettaImagePost::From(const TSharedPtr<FJsonObject>& O)
{
	FZettaImagePost P;
	P.Id = Int(O, TEXT("id"));
	P.Feature = Str(O, TEXT("feature"));
	P.Prompt = Str(O, TEXT("prompt"));
	P.AspectRatio = Str(O, TEXT("aspectRatio"));
	P.Resolution = Str(O, TEXT("resolution"));
	P.ErrorMessage = Str(O, TEXT("errorMessage"));
	for (const TSharedPtr<FJsonObject>& I : Objects(O, TEXT("images"))) P.ImageUrls.Add(Str(I, TEXT("url")));
	FDateTime::ParseIso8601(*Str(O, TEXT("createdAt")), P.CreatedAt);
	return P;
}

FZettaInlineImage FZettaInlineImage::From(const TArray<uint8>& Bytes, const FString& ContentType)
{
	FZettaInlineImage I;
	I.Data = FBase64::Encode(Bytes);
	I.ContentType = ContentType.IsEmpty() ? ZettaImages::SniffContentType(Bytes) : ContentType;
	return I;
}

TSharedRef<FJsonObject> FZettaInlineImage::ToJson() const
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("data"), Data);
	O->SetStringField(TEXT("contentType"), ContentType);
	return O;
}

namespace ZettaImages
{
	FString SniffContentType(const TArray<uint8>& B)
	{
		if (B.Num() >= 4 && B[0] == 0x89 && B[1] == 'P' && B[2] == 'N' && B[3] == 'G') return TEXT("image/png");
		if (B.Num() >= 3 && B[0] == 0xFF && B[1] == 0xD8 && B[2] == 0xFF) return TEXT("image/jpeg");
		if (B.Num() >= 12 && B[0] == 'R' && B[1] == 'I' && B[2] == 'F' && B[3] == 'F' && B[8] == 'W' && B[9] == 'E') return TEXT("image/webp");
		return FString();
	}

	FString ExtensionFor(const FString& ContentType)
	{
		if (ContentType == TEXT("image/jpeg")) return TEXT("jpg");
		if (ContentType == TEXT("image/webp")) return TEXT("webp");
		return TEXT("png");
	}
}
