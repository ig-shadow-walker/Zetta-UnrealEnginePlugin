// Copyright Zetta 3D. See LICENSE.md.
// The 2D Studio for the agent: generate_image_with_references, edit_image, compose_images, list_images, and
// save_image_asset (a library picture as a texture). A picture source is { imageId } (the user's 2D library) or
// { path }: a texture asset ("/Game/Textures/T_Hero", exported to PNG), a file in the project, or "attached:<name>".

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ZettaAgentTools.h"
#include "ZettaApi.h"
#include "ZettaAssetUtil.h"
#include "ZettaCloud.h"
#include "ZettaImporter.h"
#include "ZettaSettings.h"
#include "ZettaTargets.h"
#include "ZettaToolCommon.h"

namespace ZettaImageImpl
{
	using ZettaTool::Fail;
	using ZettaTool::Ok;

	constexpr int32 MinSide = 128;
	constexpr int32 MaxSide = 2048;
	constexpr int64 MaxBytes = 8 * 1024 * 1024;

	/** A picture to send: a library id, or bytes read from the project. */
	struct FSource
	{
		TOptional<int64> Id;
		TOptional<FZettaInlineImage> Inline;
		FString Label;
	};

	/** PNG bytes of a decoded image, scaled into the API's 128–2,048 px range. */
	bool EncodeScaled(const FImage& Image, TArray<uint8>& Out, FString& OutError, const FString& Label)
	{
		if (Image.SizeX <= 0 || Image.SizeY <= 0)
		{
			OutError = Label + TEXT(" is empty.");
			return false;
		}
		double Scale = FMath::Min(1.0, (double)MaxSide / FMath::Max(Image.SizeX, Image.SizeY));
		Scale = FMath::Max(Scale, (double)MinSide / FMath::Min(Image.SizeX, Image.SizeY));
		const int32 W = FMath::Clamp(FMath::RoundToInt(Image.SizeX * Scale), MinSide, MaxSide);
		const int32 H = FMath::Clamp(FMath::RoundToInt(Image.SizeY * Scale), MinSide, MaxSide);
		FImage Scaled;
		if (W != Image.SizeX || H != Image.SizeY) Image.ResizeTo(Scaled, W, H, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		else Image.CopyTo(Scaled, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		TArray64<uint8> Png;
		if (!FImageUtils::CompressImage(Png, TEXT("png"), Scaled))
		{
			OutError = Label + TEXT(" could not be encoded as PNG.");
			return false;
		}
		if (Png.Num() > MaxBytes)
		{
			OutError = FString::Printf(TEXT("%s is too large to send, even scaled to %d×%d."), *Label, W, H);
			return false;
		}
		Out = TArray<uint8>(Png.GetData(), (int32)Png.Num());
		return true;
	}

	/** The bytes as the API takes them: PNG, JPEG or WebP within range as they are; anything else converted. */
	bool Acceptable(const TArray<uint8>& Bytes, const FString& Label, FZettaInlineImage& Out, FString& OutError)
	{
		const FString Type = ZettaImages::SniffContentType(Bytes);
		FImage Image;
		const bool bDecoded = FImageUtils::DecompressImage(Bytes.GetData(), Bytes.Num(), Image);
		if (!bDecoded)
		{
			// WebP may not decode here; the API reads it.
			if (Type == TEXT("image/webp") && Bytes.Num() <= MaxBytes)
			{
				Out = FZettaInlineImage::From(Bytes, Type);
				return true;
			}
			OutError = Label + TEXT(" could not be read as an image (use a PNG, JPEG or a texture).");
			return false;
		}
		const bool bFits = Image.SizeX >= MinSide && Image.SizeY >= MinSide && FMath::Max(Image.SizeX, Image.SizeY) <= MaxSide && Bytes.Num() <= MaxBytes;
		if (!Type.IsEmpty() && bFits)
		{
			Out = FZettaInlineImage::From(Bytes, Type);
			return true;
		}
		TArray<uint8> Png;
		if (!EncodeScaled(Image, Png, OutError, Label)) return false;
		Out = FZettaInlineImage::From(Png, TEXT("image/png"));
		return true;
	}

	bool ResolveSource(const TSharedPtr<FJsonValue>& Value, FSource& Out, FString& OutError)
	{
		const TSharedPtr<FJsonObject> O = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
		if (!O.IsValid())
		{
			OutError = TEXT("A picture is { \"imageId\": … } or { \"path\": … }.");
			return false;
		}
		const TOptional<int64> Id = ZettaTool::Int(O, TEXT("imageId"));
		const FString Path = ZettaTool::Str(O, TEXT("path")).TrimStartAndEnd();
		if (Id.IsSet() && !Path.IsEmpty())
		{
			OutError = TEXT("Give a picture's imageId or its path, not both.");
			return false;
		}
		if (Id.IsSet())
		{
			if (Id.GetValue() <= 0)
			{
				OutError = TEXT("'imageId' must be positive.");
				return false;
			}
			Out.Id = Id;
			Out.Label = FString::Printf(TEXT("image #%lld"), Id.GetValue());
			return true;
		}
		if (Path.IsEmpty())
		{
			OutError = TEXT("A picture needs an imageId (from the 2D library) or a path (a texture asset, a project file, or attached:<name>).");
			return false;
		}
		Out.Label = Path;
		TArray<uint8> Bytes;
		if (Path.StartsWith(TEXT("/")) && !IFileManager::Get().FileExists(*Path))
		{
			// A texture asset: its source picture, as the artist imported it.
			UTexture* Texture = Cast<UTexture>(ZettaTargets::LoadAsset(Path, OutError, UTexture::StaticClass()));
			if (!Texture) return false;
			FImage Image;
			const bool bRead = Cast<UTexture2D>(Texture) ? FImageUtils::GetTexture2DSourceImage(Cast<UTexture2D>(Texture), Image) : Texture->Source.GetMipImage(Image, 0);
			if (!bRead)
			{
				OutError = FString::Printf(TEXT("%s has no source picture to read (it may be generated or cooked)."), *Path);
				return false;
			}
			TArray<uint8> Png;
			if (!EncodeScaled(Image, Png, OutError, Path)) return false;
			Out.Inline = FZettaInlineImage::From(Png, TEXT("image/png"));
			return true;
		}
		FString File;
		if (!ZettaTool::ReadableFile(Path, File, OutError)) return false;
		if (!FFileHelper::LoadFileToArray(Bytes, *File))
		{
			OutError = FString::Printf(TEXT("%s could not be read."), *Path);
			return false;
		}
		FZettaInlineImage Inline;
		if (!Acceptable(Bytes, Path, Inline, OutError)) return false;
		Out.Inline = Inline;
		return true;
	}

	bool ResolveList(const TSharedPtr<FJsonObject>& In, const FString& Key, int32 Min, int32 Max, TArray<FSource>& Out, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>> List = ZettaTool::Arr(In, Key);
		if (List.Num() < Min || List.Num() > Max)
		{
			OutError = FString::Printf(TEXT("'%s' takes %d to %d pictures ({ imageId } or { path }); got %d."), *Key, Min, Max, List.Num());
			return false;
		}
		for (const TSharedPtr<FJsonValue>& V : List)
		{
			FSource S;
			if (!ResolveSource(V, S, OutError)) return false;
			Out.Add(S);
		}
		return true;
	}

	/** Every source as inline bytes (library pictures downloaded), in order. */
	void InlineAll(const TArray<FSource>& Sources, TFunction<void(const TArray<FZettaInlineImage>&, const FString& Error)> Done)
	{
		TSharedRef<TArray<FZettaInlineImage>> Out = MakeShared<TArray<FZettaInlineImage>>();
		Out->SetNum(Sources.Num());
		TSharedRef<int32> Left = MakeShared<int32>(Sources.Num());
		TSharedRef<FString> Error = MakeShared<FString>();
		if (Sources.Num() == 0)
		{
			Done(*Out, FString());
			return;
		}
		for (int32 i = 0; i < Sources.Num(); ++i)
		{
			auto Finish = [Out, Left, Error, Done]()
			{
				if (--(*Left) == 0) Done(*Out, *Error);
			};
			if (Sources[i].Inline.IsSet())
			{
				(*Out)[i] = Sources[i].Inline.GetValue();
				Finish();
				continue;
			}
			const FString Label = Sources[i].Label;
			ZettaCloud::DownloadImage(Sources[i].Id.GetValue(), [Out, Error, Finish, i, Label](const TArray<uint8>& Bytes, const FString& DownloadError)
			{
				FZettaInlineImage Inline;
				FString AcceptError;
				if (!DownloadError.IsEmpty()) *Error = DownloadError;
				else if (Acceptable(Bytes, Label, Inline, AcceptError)) (*Out)[i] = Inline;
				else *Error = AcceptError;
				Finish();
			});
		}
	}

	bool ReadFrame(const TSharedPtr<FJsonObject>& In, ZettaApi::FImageOptions& Options, FString& Prompt, FString& OutError)
	{
		Prompt = ZettaTool::Str(In, TEXT("prompt")).TrimStartAndEnd();
		if (Prompt.IsEmpty())
		{
			OutError = TEXT("'prompt' is required.");
			return false;
		}
		if (Prompt.Len() > 5000)
		{
			OutError = TEXT("The prompt is longer than 5,000 characters.");
			return false;
		}
		Options.AspectRatio = ZettaTool::Str(In, TEXT("aspectRatio"), TEXT("1:1")).TrimStartAndEnd();
		Options.Resolution = ZettaTool::Str(In, TEXT("resolution"), TEXT("1K")).TrimStartAndEnd().ToUpper();
		if (Options.Resolution != TEXT("1K") && Options.Resolution != TEXT("2K"))
		{
			OutError = TEXT("'resolution' must be 1K or 2K.");
			return false;
		}
		return true;
	}

	/** A new library picture: its id told to the model, its link handed to the chat to draw. */
	void Made(const FZettaResponse& Response, const FZettaImageResult& Result, const ZettaApi::FImageOptions& Options, const FString& What, FZettaToolDone Done)
	{
		if (!Response.bOk)
		{
			Done(Fail(ZettaCloud::Explain(Response)));
			return;
		}
		if (Result.Images.Num() == 0)
		{
			Done(Fail(Result.Text.TrimStartAndEnd().IsEmpty() ? FString(TEXT("No picture came back. Try describing it differently.")) : Result.Text.TrimStartAndEnd()));
			return;
		}
		if (!Result.PostId.IsSet())
		{
			Done(Fail(TEXT("The picture was made but could not be saved to the library, so it cannot be used. Try again.")));
			return;
		}
		const int64 Id = Result.PostId.GetValue();
		const FString Text = FString::Printf(TEXT("Made image #%lld (%s, %s), %s. It is in the user's 2D library and shown to them. Use imageId %lld with save_image_asset to put it in the project "
			"(replace: true to overwrite a texture), with edit_image or compose_images to change it further, or with generate_3d_model or process_model."),
			Id, *Options.AspectRatio, *Options.Resolution, *What, Id);
		ZettaCloud::ImageUrl(Id, [Done, Text, Id](const FString& Url, const FString&)
		{
			FZettaToolResult R = Ok(Text, FString::Printf(TEXT("Made image #%lld"), Id));
			R.ImageId = Id;
			R.ImageUrl = Url;
			Done(R);
		});
	}

	// ─── The tools ───────────────────────────────────────────────────────────

	void GenerateWithReferences(const TSharedPtr<FJsonObject>& In, FZettaToolDone Done)
	{
		FString Error, Prompt;
		ZettaApi::FImageOptions Options;
		TArray<FSource> Sources;
		if (!ZettaCloud::RequireSignIn(Error) || !ReadFrame(In, Options, Prompt, Error) || !ResolveList(In, TEXT("references"), 1, 3, Sources, Error))
		{
			Done(Fail(Error));
			return;
		}
		const int32 Count = Sources.Num();
		InlineAll(Sources, [Done, Prompt, Options, Count](const TArray<FZettaInlineImage>& References, const FString& InlineError)
		{
			if (!InlineError.IsEmpty())
			{
				Done(Fail(InlineError));
				return;
			}
			ZettaApi::GenerateImage(Prompt, Options, References, [Done, Options, Count](const FZettaResponse& Response, const FZettaImageResult& Result)
			{
				Made(Response, Result, Options, FString::Printf(TEXT("in the look of %s"), *ZettaTool::Plural(Count, TEXT("reference"), TEXT("references"))), Done);
			});
		});
	}

	void EditImage(const TSharedPtr<FJsonObject>& In, FZettaToolDone Done)
	{
		FString Error, Prompt;
		ZettaApi::FImageOptions Options;
		FSource Source;
		if (!ZettaCloud::RequireSignIn(Error) || !ReadFrame(In, Options, Prompt, Error)) { Done(Fail(Error)); return; }
		if (!ZettaTool::Has(In, TEXT("image"))) { Done(Fail(TEXT("'image' is required: { imageId } or { path }."))); return; }
		if (!ResolveSource(ZettaTool::Get(In, TEXT("image")), Source, Error)) { Done(Fail(Error)); return; }
		const FString Label = Source.Label;
		ZettaApi::EditImage(Prompt, Options, Source.Inline, Source.Id, [Done, Options, Label](const FZettaResponse& Response, const FZettaImageResult& Result)
		{
			Made(Response, Result, Options, TEXT("an edit of ") + Label, Done);
		});
	}

	void ComposeImages(const TSharedPtr<FJsonObject>& In, FZettaToolDone Done)
	{
		FString Error, Prompt;
		ZettaApi::FImageOptions Options;
		TArray<FSource> Sources;
		if (!ZettaCloud::RequireSignIn(Error) || !ReadFrame(In, Options, Prompt, Error) || !ResolveList(In, TEXT("images"), 2, 14, Sources, Error))
		{
			Done(Fail(Error));
			return;
		}
		TArray<FString> Labels;
		for (const FSource& S : Sources) Labels.Add(S.Label);
		const FString What = TEXT("composed from ") + FString::Join(Labels, TEXT(", "));
		auto Compose = [Done, Prompt, Options, What](const TArray<FZettaInlineImage>& Inline, const TArray<int64>& Ids)
		{
			ZettaApi::ComposeImages(Prompt, Options, Inline, Ids, [Done, Options, What](const FZettaResponse& Response, const FZettaImageResult& Result)
			{
				Made(Response, Result, Options, What, Done);
			});
		};
		// The server puts library pictures before uploaded ones, so a mix is all sent inline to keep the prompt's order true.
		if (!Sources.ContainsByPredicate([](const FSource& S) { return !S.Id.IsSet(); }))
		{
			TArray<int64> Ids;
			for (const FSource& S : Sources) Ids.Add(S.Id.GetValue());
			Compose({}, Ids);
			return;
		}
		InlineAll(Sources, [Done, Compose](const TArray<FZettaInlineImage>& Inline, const FString& InlineError)
		{
			if (!InlineError.IsEmpty()) Done(Fail(InlineError));
			else Compose(Inline, {});
		});
	}

	void ListImages(const TSharedPtr<FJsonObject>& In, FZettaToolDone Done)
	{
		FString Error;
		if (!ZettaCloud::RequireSignIn(Error)) { Done(Fail(Error)); return; }
		const FString Search = ZettaTool::Str(In, TEXT("search")).TrimStartAndEnd();
		ZettaApi::ListImages(1, ZettaTool::IntIn(In, TEXT("limit"), 20, 1, 50), Search, [Done, Search](const FZettaResponse& Response, const TArray<FZettaImagePost>& Posts, int32 Total)
		{
			if (!Response.bOk) { Done(Fail(ZettaCloud::Explain(Response))); return; }
			TArray<FString> Lines;
			for (const FZettaImagePost& P : Posts)
			{
				if (!P.ErrorMessage.IsEmpty() || P.ImageUrls.Num() == 0) continue;
				const TCHAR* How = P.Feature == TEXT("editImg") ? TEXT("edit") : P.Feature == TEXT("composeImg") ? TEXT("composite") : TEXT("generated");
				Lines.Add(FString::Printf(TEXT("#%lld · %s · %s %s · %s · \"%s\""), P.Id, How, P.AspectRatio.IsEmpty() ? TEXT("?") : *P.AspectRatio, *P.Resolution,
					*P.CreatedAt.ToString(TEXT("%Y-%m-%d")), *ZettaTool::Short(P.Prompt, 140)));
			}
			if (Lines.Num() == 0)
			{
				Done(Ok(Search.IsEmpty() ? FString(TEXT("The user's 2D library is empty.")) : FString::Printf(TEXT("No pictures match \"%s\"."), *Search)));
				return;
			}
			FString Text = FString::Printf(TEXT("%d picture(s) in the user's 2D library, newest first (imageId · how it was made · frame · date · prompt):\n%s"), Total, *FString::Join(Lines, TEXT("\n")));
			if (Total > Lines.Num()) Text += FString::Printf(TEXT("\n…%d more — search to narrow it."), Total - Lines.Num());
			Done(Ok(Text));
		});
	}

	void ApplyTextureType(UTexture* Texture, const FString& Type)
	{
		Texture->Modify();
		if (Type == TEXT("normal"))
		{
			Texture->CompressionSettings = TC_Normalmap;
			Texture->SRGB = false;
			Texture->LODGroup = TEXTUREGROUP_WorldNormalMap;
		}
		else if (Type == TEXT("masks"))
		{
			Texture->CompressionSettings = TC_Masks;
			Texture->SRGB = false;
		}
		else if (Type == TEXT("ui"))
		{
			Texture->CompressionSettings = TC_EditorIcon;
			Texture->LODGroup = TEXTUREGROUP_UI;
			Texture->MipGenSettings = TMGS_NoMipmaps;
			Texture->NeverStream = true;
		}
		Texture->PostEditChange();
	}

	void SaveImageAsset(const TSharedPtr<FJsonObject>& In, FZettaToolDone Done)
	{
		FString Error, Package;
		const TOptional<int64> Id = ZettaTool::Int(In, TEXT("imageId"));
		if (!ZettaCloud::RequireSignIn(Error)) { Done(Fail(Error)); return; }
		if (!Id.IsSet()) { Done(Fail(TEXT("'imageId' is required."))); return; }
		FString Path = ZettaTool::Str(In, TEXT("path")).TrimStartAndEnd();
		if (Path.IsEmpty()) Path = UZettaProjectSettings::Get()->GetImagesFolder() + FString::Printf(TEXT("/T_Image_%lld"), Id.GetValue());
		if (!ZettaTool::WritableContentPath(Path, Package, Error)) { Done(Fail(Error)); return; }
		const FString Type = ZettaTool::Str(In, TEXT("textureType"), TEXT("default")).TrimStartAndEnd().ToLower();
		if (Type != TEXT("default") && Type != TEXT("normal") && Type != TEXT("ui") && Type != TEXT("masks")) { Done(Fail(TEXT("'textureType' must be default, normal, ui or masks."))); return; }
		const bool bReplace = ZettaTool::BoolOr(In, TEXT("replace"), false);
		const bool bExists = ZettaTargets::AssetExists(Package);
		if (bReplace && !bExists) { Done(Fail(FString::Printf(TEXT("%s does not exist, so there is nothing to replace. Leave replace out to save a new texture."), *Package))); return; }
		if (!bReplace && bExists) { Done(Fail(FString::Printf(TEXT("%s already exists. Pass replace: true to put the new picture in it (its settings are kept), or pick another path."), *Package))); return; }
		const bool bTypeGiven = ZettaTool::Has(In, TEXT("textureType"));
		const int64 ImageId = Id.GetValue();
		ZettaCloud::DownloadImage(ImageId, [Done, Package, Type, bReplace, bTypeGiven, ImageId](const TArray<uint8>& Bytes, const FString& DownloadError)
		{
			if (!DownloadError.IsEmpty()) { Done(Fail(DownloadError)); return; }
			if (bReplace) ZettaAgentChanges::WillChangeAsset(Package, TEXT("changed (picture replaced)"));
			FString ObjectPath, SaveError;
			if (!ZettaImporter::SaveTexture(Bytes, Package, ObjectPath, SaveError, bReplace)) { Done(Fail(SaveError)); return; }
			if (!bReplace) ZettaAgentChanges::CreatedAsset(Package);
			UTexture* Texture = LoadObject<UTexture>(nullptr, *ObjectPath);
			if (Texture && (!bReplace || bTypeGiven) && Type != TEXT("default"))
			{
				ApplyTextureType(Texture, Type);
				FString Ignored;
				ZettaAssets::Save(Texture, Ignored);
			}
			FString Size;
			if (const UTexture2D* T2 = Cast<UTexture2D>(Texture)) Size = FString::Printf(TEXT(", %d×%d"), (int32)T2->Source.GetSizeX(), (int32)T2->Source.GetSizeY());
			Done(Ok(bReplace ? FString::Printf(TEXT("Replaced the picture in %s with image #%lld%s; its texture settings are kept, so every material using it changes with it."), *Package, ImageId, *Size)
							 : FString::Printf(TEXT("Saved image #%lld as %s (%s texture%s). Use it in create_material textures or set_properties."), ImageId, *Package, *Type, *Size),
				FString::Printf(TEXT("Saved %s"), *FPackageName::GetShortName(Package))));
		});
	}

	FString SourceLabel(const TSharedPtr<FJsonValue>& V)
	{
		const TSharedPtr<FJsonObject> O = V.IsValid() && V->Type == EJson::Object ? V->AsObject() : nullptr;
		if (const TOptional<int64> Id = ZettaTool::Int(O, TEXT("imageId"))) return FString::Printf(TEXT("image #%lld"), Id.GetValue());
		const FString Path = ZettaTool::Str(O, TEXT("path"));
		return Path.IsEmpty() ? FString(TEXT("a picture")) : FPaths::GetCleanFilename(Path);
	}
}

void ZettaToolAreas::RegisterImageTools(FZettaAgentTools& Tools)
{
	using namespace ZettaImageImpl;
	auto Sync = [](FZettaToolPreview P) { return [P](const TSharedPtr<FJsonObject>&, TFunction<void(const FZettaToolPreview&)> Done) { Done(P); }; };
	Tools.RegisterAsync(TEXT("generate_image_with_references"), &GenerateWithReferences, [](const TSharedPtr<FJsonObject>& In, TFunction<void(const FZettaToolPreview&)> Done)
	{
		Done(ZettaTool::Preview(FString::Printf(TEXT("Draw a picture from %s"), *ZettaTool::Plural(ZettaTool::Arr(In, TEXT("references")).Num(), TEXT("reference"), TEXT("references"))),
			ZettaTool::Short(ZettaTool::Str(In, TEXT("prompt")), 400)));
	});
	Tools.RegisterAsync(TEXT("edit_image"), &EditImage, [](const TSharedPtr<FJsonObject>& In, TFunction<void(const FZettaToolPreview&)> Done)
	{
		Done(ZettaTool::Preview(TEXT("Edit ") + SourceLabel(ZettaTool::Get(In, TEXT("image"))), ZettaTool::Short(ZettaTool::Str(In, TEXT("prompt")), 400)));
	});
	Tools.RegisterAsync(TEXT("compose_images"), &ComposeImages, [](const TSharedPtr<FJsonObject>& In, TFunction<void(const FZettaToolPreview&)> Done)
	{
		Done(ZettaTool::Preview(FString::Printf(TEXT("Compose %d pictures"), ZettaTool::Arr(In, TEXT("images")).Num()), ZettaTool::Short(ZettaTool::Str(In, TEXT("prompt")), 400)));
	});
	Tools.RegisterAsync(TEXT("list_images"), &ListImages, Sync(ZettaTool::Preview(TEXT("Browse your pictures"))));
	Tools.RegisterAsync(TEXT("save_image_asset"), &SaveImageAsset, [](const TSharedPtr<FJsonObject>& In, TFunction<void(const FZettaToolPreview&)> Done)
	{
		const bool bReplace = ZettaTool::BoolOr(In, TEXT("replace"), false);
		Done(ZettaTool::Preview(FString::Printf(TEXT("%s image #%s"), bReplace ? TEXT("Replace a texture with") : TEXT("Save"), *ZettaTool::Str(In, TEXT("imageId"))),
			FString::Printf(TEXT("%s %s"), bReplace ? TEXT("Into") : TEXT("As"), *ZettaTool::Str(In, TEXT("path"), TEXT("a new texture")))));
	});
}
