// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaStudioAssets.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/Texture.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "IContentBrowserSingleton.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/ObjectThumbnail.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "ZettaApi.h"
#include "ZettaCatalog.h"
#include "ZettaHttp.h"
#include "ZettaImageCache.h"
#include "ZettaImporter.h"
#include "ZettaJson.h"
#include "ZettaSettings.h"

namespace ZettaStudioAssets
{
	// ─── Pictures ─────────────────────────────────────────────────────────────────

	FString CheckImage(const TArray<uint8>& Bytes, const FString& ContentType)
	{
		if (Bytes.Num() == 0) return TEXT("That image is empty.");
		if (ContentType != TEXT("image/png") && ContentType != TEXT("image/jpeg") && ContentType != TEXT("image/webp")) return TEXT("Use a PNG, JPEG or WebP image.");
		if (Bytes.Num() > MaxImageBytes) return FString::Printf(TEXT("That image is %.1f MB — the limit is 8 MB."), Bytes.Num() / 1024.0 / 1024.0);
		int32 W = 0, H = 0;
		if (!ImageSize(Bytes, W, H)) return TEXT("That image could not be read.");
		if (W < MinSide || H < MinSide) return FString::Printf(TEXT("That image is %d×%d px — each side needs at least %d px."), W, H, MinSide);
		if (W > MaxSide || H > MaxSide) return FString::Printf(TEXT("That image is %d×%d px — each side can be at most %s px."), W, H, *FText::AsNumber(MaxSide).ToString());
		return FString();
	}

	static int32 BigEndian(const TArray<uint8>& B, int32 At) { return (B[At] << 24) | (B[At + 1] << 16) | (B[At + 2] << 8) | B[At + 3]; }

	bool ImageSize(const TArray<uint8>& B, int32& OutW, int32& OutH)
	{
		const int32 N = B.Num();
		if (N > 24 && B[0] == 0x89 && B[1] == 'P' && B[2] == 'N' && B[3] == 'G')
		{
			OutW = BigEndian(B, 16);
			OutH = BigEndian(B, 20);
			return true;
		}
		if (N > 4 && B[0] == 0xFF && B[1] == 0xD8)
		{
			int32 i = 2;
			while (i + 9 < N)
			{
				if (B[i] != 0xFF)
				{
					++i;
					continue;
				}
				const uint8 Marker = B[i + 1];
				if (Marker == 0xD8 || Marker == 0x01 || (Marker >= 0xD0 && Marker <= 0xD7))
				{
					i += 2;
					continue;
				}
				const int32 Length = (B[i + 2] << 8) | B[i + 3];
				const bool bFrame = Marker >= 0xC0 && Marker <= 0xCF && Marker != 0xC4 && Marker != 0xC8 && Marker != 0xCC;
				if (bFrame)
				{
					OutW = (B[i + 7] << 8) | B[i + 8];
					OutH = (B[i + 5] << 8) | B[i + 6];
					return true;
				}
				i += 2 + Length;
			}
			return false;
		}
		if (N > 30 && B[0] == 'R' && B[1] == 'I' && B[2] == 'F' && B[3] == 'F' && B[8] == 'W' && B[9] == 'E' && B[10] == 'B' && B[11] == 'P')
		{
			if (B[12] == 'V' && B[13] == 'P' && B[14] == '8' && B[15] == ' ')
			{
				OutW = ((B[27] << 8) | B[26]) & 0x3FFF;
				OutH = ((B[29] << 8) | B[28]) & 0x3FFF;
				return true;
			}
			if (B[12] == 'V' && B[13] == 'P' && B[14] == '8' && B[15] == 'L')
			{
				const uint32 Bits = B[21] | (B[22] << 8) | (B[23] << 16) | ((uint32)B[24] << 24);
				OutW = (int32)(Bits & 0x3FFF) + 1;
				OutH = (int32)((Bits >> 14) & 0x3FFF) + 1;
				return true;
			}
			if (B[12] == 'V' && B[13] == 'P' && B[14] == '8' && B[15] == 'X')
			{
				OutW = 1 + (B[24] | (B[25] << 8) | (B[26] << 16));
				OutH = 1 + (B[27] | (B[28] << 8) | (B[29] << 16));
				return true;
			}
		}
		return false;
	}

	FString DotExtension(const FString& ContentType)
	{
		return TEXT(".") + ZettaImages::ExtensionFor(ContentType);
	}

	FString Renamed(const FString& Original, const FString& ContentType)
	{
		FString Name = FPaths::GetBaseFilename(Original);
		if (Name.IsEmpty()) Name = TEXT("picture");
		return Name + DotExtension(ContentType);
	}

	FString TitleFromFile(const FString& FileName)
	{
		FString Name = FPaths::GetBaseFilename(FileName).Replace(TEXT("_"), TEXT(" "));
		while (Name.Contains(TEXT("  "))) Name = Name.Replace(TEXT("  "), TEXT(" "));
		return Name.TrimStartAndEnd().Left(255);
	}

	FString UploadingText(float Progress, const FString& What)
	{
		return FString::Printf(TEXT("%s… %d%%"), *What, FMath::RoundToInt(FMath::Clamp(Progress, 0.f, 1.f) * 100.f));
	}

	void UploadImage(const TArray<uint8>& Bytes, const FString& ContentType, bool bRemoveBackground,
		TFunction<void(const FString&, const FString&)> Done, TFunction<void(float)> Progress)
	{
		const FString Problem = CheckImage(Bytes, ContentType);
		if (!Problem.IsEmpty())
		{
			Done(Problem, FString());
			return;
		}
		auto Send = [Done, Progress](const TArray<uint8>& Data, const FString& Type)
		{
			if (Progress) Progress(0.f);
			ZettaApi::Upload(Data, Type, [Done](const FZettaResponse& R, const FZettaUploadTicket& Ticket)
			{
				if (!R.bOk) Done(ZettaCatalog::Explain(R), FString());
				else if (Ticket.FileUrl.IsEmpty()) Done(TEXT("The upload did not return an address. Try again."), FString());
				else Done(FString(), Ticket.FileUrl);
			}, Progress);
		};
		if (!bRemoveBackground)
		{
			Send(Bytes, ContentType);
			return;
		}
		ZettaApi::RemoveBackground(Bytes, [Done, Send](const FZettaResponse& R, const TArray<uint8>& Png)
		{
			if (!R.bOk)
			{
				Done(ZettaCatalog::Explain(R), FString());
				return;
			}
			if (Png.Num() == 0)
			{
				Done(TEXT("Could not remove the background. Try again, or turn Remove background off."), FString());
				return;
			}
			Send(Png, TEXT("image/png"));
		});
	}

	// ─── Models ───────────────────────────────────────────────────────────────────

	void UploadModelFile(const FString& Path, TFunction<void(const FString&, const FZettaGenerationPtr&)> Done, TFunction<void(const FString&)> Status)
	{
		const FString Ext = FPaths::GetExtension(Path).ToLower();
		if (Ext != TEXT("glb") && Ext != TEXT("fbx"))
		{
			Done(TEXT("Choose a .glb or .fbx model."), nullptr);
			return;
		}
		const int64 Size = IFileManager::Get().FileSize(*Path);
		if (Size < 0)
		{
			Done(TEXT("That file is not on disk."), nullptr);
			return;
		}
		if (Size > MaxModelBytes)
		{
			Done(TEXT("That model is larger than 100 MB, the most the library accepts. Retopologize or compress it first."), nullptr);
			return;
		}
		TSharedRef<TArray<uint8>> Bytes = MakeShared<TArray<uint8>>();
		if (!FFileHelper::LoadFileToArray(*Bytes, *Path))
		{
			Done(TEXT("Could not read the file."), nullptr);
			return;
		}
		const bool bGlb = Ext == TEXT("glb");
		if (bGlb && (Bytes->Num() < 12 || (*Bytes)[0] != 'g' || (*Bytes)[1] != 'l' || (*Bytes)[2] != 'T' || (*Bytes)[3] != 'F'))
		{
			Done(TEXT("That file is not a valid binary glTF (.glb)."), nullptr);
			return;
		}

		const FString Title = FPaths::GetBaseFilename(Path);
		if (Status) Status(UploadingText(0.f, TEXT("Uploading the model")));

		// A GLB goes up as itself; any other model format as bytes with its extension, stored as it is.
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("contentType"), bGlb ? TEXT("model/gltf-binary") : TEXT("application/octet-stream"));
		if (!bGlb) Body->SetStringField(TEXT("extension"), Ext);
		const FString ContentType = bGlb ? TEXT("model/gltf-binary") : TEXT("application/octet-stream");

		FZettaHttp::Post(TEXT("/3d/uploads"), Body, [Bytes, Done, Status, Title, ContentType](const FZettaResponse& R)
		{
			if (!R.bOk)
			{
				Done(ZettaCatalog::Explain(R), nullptr);
				return;
			}
			const FZettaUploadTicket Ticket = FZettaUploadTicket::From(R.Object());
			TMap<FString, FString> Headers = Ticket.Headers;
			if (!Headers.Contains(TEXT("Content-Type"))) Headers.Add(TEXT("Content-Type"), ContentType);
			FZettaHttp::PutBytes(Ticket.UploadUrl, *Bytes, Headers, [Bytes, Done, Status, Title, Ticket](const FZettaResponse& Put)
			{
				if (!Put.bOk)
				{
					Done(Put.Code == TEXT("UPLOAD_TICKET_EXPIRED") ? FString(TEXT("The upload took too long and expired. Try again.")) : ZettaCatalog::Explain(Put), nullptr);
					return;
				}
				if (Status) Status(TEXT("Adding it to your library…"));
				ZettaApi::CreateUploadPost(Ticket.Key, Bytes->Num(), Title, [Done](const FZettaResponse& Post, const FZettaGenerationPtr& G)
				{
					if (!Post.bOk || !G.IsValid()) Done(Post.bOk ? FString(TEXT("The library did not accept the model.")) : ZettaCatalog::Explain(Post), nullptr);
					else Done(FString(), G);
				});
			}, [Status](float P)
			{
				if (Status) Status(UploadingText(P, TEXT("Uploading the model")));
			});
		});
	}

	TOptional<int64> GenerationIdOf(const FString& ObjectPath)
	{
		if (ObjectPath.IsEmpty()) return {};
		const FString Package = FPackageName::ObjectPathToPackageName(ObjectPath);
		const FString Folder = FPackageName::GetLongPackagePath(Package);
		FString Disk;
		if (!FPackageName::TryConvertLongPackageNameToFilename(Folder + TEXT("/"), Disk)) return {};
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ConvertRelativePathToFull(Disk), TEXT("zetta.json")))) return {};
		const int64 Id = ZettaJson::Int(ZettaJson::Parse(Text), TEXT("generationId"));
		return Id > 0 ? TOptional<int64>(Id) : TOptional<int64>();
	}

	FString SourceFileOf(const UObject* Asset)
	{
		if (!Asset) return FString();
		// Meshes and textures keep their import data under the same property name; reflection reads any of them.
		const FObjectProperty* Property = FindFProperty<FObjectProperty>(Asset->GetClass(), TEXT("AssetImportData"));
		if (!Property) return FString();
		const UAssetImportData* Data = Cast<UAssetImportData>(Property->GetObjectPropertyValue_InContainer(Asset));
		return Data ? Data->GetFirstFilename() : FString();
	}

	void SourceFromSelectedAsset(TFunction<void(const FString&, const FZettaGenerationPtr&)> Done, TFunction<void(const FString&)> Status)
	{
		TArray<FAssetData> Selected;
		FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().GetSelectedAssets(Selected);
		if (Selected.Num() == 0)
		{
			Done(TEXT("Select a model in the Content Browser first."), nullptr);
			return;
		}
		SourceFromAsset(Selected[0], Done, Status);
	}

	void SourceFromAsset(const FAssetData& Asset, TFunction<void(const FString&, const FZettaGenerationPtr&)> Done, TFunction<void(const FString&)> Status)
	{
		const FString ObjectPath = Asset.GetObjectPathString();

		auto Upload = [Asset, Done, Status]()
		{
			const FString File = SourceFileOf(Asset.GetAsset());
			const FString Ext = FPaths::GetExtension(File).ToLower();
			if (File.IsEmpty() || !FPaths::FileExists(File) || (Ext != TEXT("glb") && Ext != TEXT("fbx")))
			{
				Done(TEXT("That asset was not imported from a .glb or .fbx file on this computer. Use Upload a model file… instead."), nullptr);
				return;
			}
			UploadModelFile(File, Done, Status);
		};

		const TOptional<int64> Id = GenerationIdOf(ObjectPath);
		if (!Id.IsSet())
		{
			Upload();
			return;
		}
		if (Status) Status(TEXT("Finding it in your library…"));
		ZettaApi::GetGeneration(Id.GetValue(), [Done, Upload](const FZettaResponse& R, const FZettaGenerationPtr& G, TOptional<int32>)
		{
			if (R.bOk && G.IsValid() && G->IsSuccess()) Done(FString(), G);
			// Imported from another account, or deleted: the file it came from goes up instead.
			else if (R.bOk || R.Status == 404) Upload();
			else Done(ZettaCatalog::Explain(R), nullptr);
		});
	}

	// ─── Turning assets into pictures ────────────────────────────────────────────

	static bool EncodeJpeg(const FImageView& Image, TArray<uint8>& Out)
	{
		TArray64<uint8> Compressed;
		if (!FImageUtils::CompressImage(Compressed, TEXT("jpg"), Image, 90) || Compressed.Num() == 0) return false;
		Out = TArray<uint8>(Compressed.GetData(), (int32)Compressed.Num());
		return true;
	}

	bool RenderAssetJpeg(const FString& ObjectPath, int32 Size, TArray<uint8>& OutJpeg)
	{
		UObject* Asset = ObjectPath.IsEmpty() ? nullptr : LoadObject<UObject>(nullptr, *ObjectPath);
		if (!Asset) return false;
		FObjectThumbnail Thumbnail;
		ThumbnailTools::RenderThumbnail(Asset, Size, Size, ThumbnailTools::EThumbnailTextureFlushMode::AlwaysFlush, nullptr, &Thumbnail);
		const int32 W = Thumbnail.GetImageWidth(), H = Thumbnail.GetImageHeight();
		const TArray<uint8>& Bgra = Thumbnail.GetUncompressedImageData();
		if (W <= 0 || H <= 0 || Bgra.Num() < W * H * 4) return false;
		FImage Image(W, H, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		FMemory::Memcpy(Image.RawData.GetData(), Bgra.GetData(), (SIZE_T)W * H * 4);
		return EncodeJpeg(Image, OutJpeg);
	}

	bool ToJpeg(const TArray<uint8>& Bytes, int32 MaxSize, TArray<uint8>& OutJpeg)
	{
		TArray<uint8> Bgra;
		int32 W = 0, H = 0;
		if (!FZettaImageCache::Decode(Bytes, Bgra, W, H) || W <= 0 || H <= 0) return false;
		FImage Image(W, H, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		FMemory::Memcpy(Image.RawData.GetData(), Bgra.GetData(), FMath::Min<int64>(Bgra.Num(), Image.RawData.Num()));
		if (FMath::Max(W, H) > MaxSize)
		{
			const float Scale = (float)MaxSize / FMath::Max(W, H);
			FImage Small;
			Image.ResizeTo(Small, FMath::Max(1, FMath::RoundToInt(W * Scale)), FMath::Max(1, FMath::RoundToInt(H * Scale)), ERawImageFormat::BGRA8, EGammaSpace::sRGB);
			return EncodeJpeg(Small, OutJpeg);
		}
		return EncodeJpeg(Image, OutJpeg);
	}

	void Downscale(TArray<uint8>& Bytes, FString& ContentType)
	{
		int32 W = 0, H = 0;
		if (ContentType == TEXT("image/webp") || !ImageSize(Bytes, W, H) || FMath::Max(W, H) <= 1536) return;
		TArray<uint8> Jpeg;
		if (ToJpeg(Bytes, 1536, Jpeg) && Jpeg.Num() < Bytes.Num())
		{
			Bytes = MoveTemp(Jpeg);
			ContentType = TEXT("image/jpeg");
		}
	}

	/** A texture too large to go up as it is becomes a JPEG of at most 4096 px. */
	static void FitTextureForUpload(TArray<uint8>& Bytes, FString& ContentType)
	{
		int32 W = 0, H = 0;
		const bool bTooLarge = Bytes.Num() > MaxImageBytes || (ImageSize(Bytes, W, H) && FMath::Max(W, H) > MaxSide);
		TArray<uint8> Jpeg;
		if (bTooLarge && ToJpeg(Bytes, 4096, Jpeg))
		{
			Bytes = MoveTemp(Jpeg);
			ContentType = TEXT("image/jpeg");
		}
	}

	static bool ReadTexture(UTexture* Texture, TArray<uint8>& OutBytes, FString& OutContentType, FString& OutName);

	bool TextureBytes(UTexture* Texture, TArray<uint8>& OutBytes, FString& OutContentType, FString& OutName)
	{
		if (!ReadTexture(Texture, OutBytes, OutContentType, OutName)) return false;
		FitTextureForUpload(OutBytes, OutContentType);
		OutName = FPaths::GetBaseFilename(OutName) + DotExtension(OutContentType);
		return true;
	}

	static bool ReadTexture(UTexture* Texture, TArray<uint8>& OutBytes, FString& OutContentType, FString& OutName)
	{
		if (!Texture) return false;
		const FString File = SourceFileOf(Texture);
		const FString Ext = FPaths::GetExtension(File).ToLower();
		if ((Ext == TEXT("png") || Ext == TEXT("jpg") || Ext == TEXT("jpeg")) && FFileHelper::LoadFileToArray(OutBytes, *File))
		{
			OutContentType = ZettaImages::SniffContentType(OutBytes);
			if (!OutContentType.IsEmpty())
			{
				OutName = FPaths::GetCleanFilename(File);
				return true;
			}
		}
		// No usable file on disk: the texture's own pixels, as a PNG.
		FImage Image;
		if (!Texture->Source.IsValid() || !Texture->Source.GetMipImage(Image, 0)) return false;
		FImage Bgra;
		Image.CopyTo(Bgra, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		TArray64<uint8> Png;
		if (!FImageUtils::CompressImage(Png, TEXT("png"), Bgra) || Png.Num() == 0) return false;
		OutBytes = TArray<uint8>(Png.GetData(), (int32)Png.Num());
		OutContentType = TEXT("image/png");
		OutName = Texture->GetName() + TEXT(".png");
		return true;
	}

	FString SaveImage(const TArray<uint8>& Bytes, const FString& Name, FString& OutError)
	{
		FString Base = ZettaImporter::Slug(Name.Left(60)).Replace(TEXT("-"), TEXT("_"));
		if (Base.IsEmpty()) Base = TEXT("image");
		const FString Folder = UZettaProjectSettings::Get()->GetImagesFolder();
		FString Package, AssetName;
		FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().CreateUniqueAssetName(Folder / (TEXT("T_") + Base), FString(), Package, AssetName);
		FString ObjectPath;
		if (!ZettaImporter::SaveTexture(Bytes, Package, ObjectPath, OutError)) return FString();
		return ObjectPath;
	}

	void ShowInContentBrowser(const TArray<FString>& ObjectPaths)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Assets;
		for (const FString& Path : ObjectPaths)
		{
			const FAssetData Data = Registry.GetAssetByObjectPath(FSoftObjectPath(Path));
			if (Data.IsValid()) Assets.Add(Data);
		}
		if (Assets.Num() > 0) FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().SyncBrowserToAssets(Assets);
	}

	void DownloadPicture(const FString& Url, TFunction<void(const FString&, const TArray<uint8>&, const FString&)> Done)
	{
		if (Url.IsEmpty())
		{
			Done(TEXT("There is no picture to load."), TArray<uint8>(), FString());
			return;
		}
		FZettaHttp::Download(Url, [Done](const FZettaResponse& R)
		{
			if (!R.bOk || R.Bytes.Num() == 0)
			{
				Done(R.Message.IsEmpty() ? FString(TEXT("The picture could not be loaded.")) : R.Message, TArray<uint8>(), FString());
				return;
			}
			const FString Type = ZettaImages::SniffContentType(R.Bytes);
			if (Type.IsEmpty()) Done(TEXT("That is not a picture the Studio can use."), TArray<uint8>(), FString());
			else Done(FString(), R.Bytes, Type);
		}, nullptr, 120.f);
	}
}
