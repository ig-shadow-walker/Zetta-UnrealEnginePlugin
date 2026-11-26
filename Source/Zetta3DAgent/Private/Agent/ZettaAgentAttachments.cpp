// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaAgentAttachments.h"

#include "HAL/FileManager.h"
#include "IAssetViewport.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "LevelEditor.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "ZettaImageCache.h"
#include "ZettaModels.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace
{
	const TCHAR* const AttachKeyPrefix = TEXT("attached:");

	FString AttachProjectRoot()
	{
		FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		FPaths::NormalizeDirectoryName(Root);
		return Root + TEXT("/");
	}

	FString AttachStoreFolder()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Zetta3D/Attached"));
	}

	bool AttachIsImage(const FString& Ext)
	{
		return Ext == TEXT("png") || Ext == TEXT("jpg") || Ext == TEXT("jpeg") || Ext == TEXT("webp") || Ext == TEXT("gif");
	}

	bool AttachIsText(const FString& Ext)
	{
		static const TSet<FString> Text = {
			TEXT("h"), TEXT("hpp"), TEXT("cpp"), TEXT("c"), TEXT("cc"), TEXT("inl"), TEXT("cs"), TEXT("py"), TEXT("txt"), TEXT("md"),
			TEXT("json"), TEXT("xml"), TEXT("yaml"), TEXT("yml"), TEXT("csv"), TEXT("tsv"), TEXT("ini"), TEXT("cfg"), TEXT("toml"),
			TEXT("log"), TEXT("js"), TEXT("ts"), TEXT("lua"), TEXT("html"), TEXT("css"), TEXT("sql"), TEXT("usf"), TEXT("ush"),
			TEXT("hlsl"), TEXT("glsl"), TEXT("uproject"), TEXT("uplugin"), TEXT("bat"), TEXT("sh"), TEXT("rtf"), TEXT("gitignore"),
		};
		return Text.Contains(Ext);
	}

	/** No NUL bytes in the first 8 KB, and valid UTF-8: treat it as text. */
	bool AttachLooksLikeText(const TArray<uint8>& Bytes)
	{
		const int32 N = FMath::Min(Bytes.Num(), 8192);
		if (N == 0) return false;
		for (int32 i = 0; i < N; ++i)
		{
			if (Bytes[i] == 0) return false;
		}
		// A rough UTF-8 check: continuation bytes only where a lead byte promised them.
		int32 Expect = 0;
		for (int32 i = 0; i < N; ++i)
		{
			const uint8 B = Bytes[i];
			if (Expect > 0)
			{
				if ((B & 0xC0) != 0x80) return false;
				--Expect;
			}
			else if (B >= 0xF0) Expect = 3;
			else if (B >= 0xE0) Expect = 2;
			else if (B >= 0xC0) Expect = 1;
			else if (B >= 0x80) return false;
		}
		return true;
	}

	/** Letters, digits, dot, dash and underscore only: no folders, so a name can't reach outside the store. */
	FString AttachSafeName(const FString& Name)
	{
		FString Clean;
		for (const TCHAR C : FPaths::GetCleanFilename(Name))
		{
			Clean.AppendChar(FChar::IsAlnum(C) || C == TEXT('.') || C == TEXT('-') || C == TEXT('_') ? C : TEXT('-'));
		}
		while (Clean.StartsWith(TEXT(".")) || Clean.StartsWith(TEXT("-"))) Clean.RightChopInline(1);
		if (Clean.IsEmpty()) Clean = TEXT("picture");
		if (Clean.Len() > 80) Clean = Clean.Right(80);
		return FPaths::GetExtension(Clean).IsEmpty() ? Clean + TEXT(".png") : Clean;
	}

	FString AttachEncodeJpeg(const TArray<FColor>& Pixels, int32 Width, int32 Height)
	{
		TArray64<uint8> Jpeg;
		const FImageView View(Pixels.GetData(), Width, Height);
		if (!FImageUtils::CompressImage(Jpeg, TEXT("jpg"), View, 88)) return FString();
		return FBase64::Encode(Jpeg.GetData(), (uint32)Jpeg.Num());
	}

	/** Scales BGRA pixels so the longer side is at most MaxSide. */
	void AttachFit(TArray<FColor>& Pixels, int32& Width, int32& Height, int32 MaxSide)
	{
		const float Scale = FMath::Min(1.f, (float)MaxSide / FMath::Max(Width, Height));
		if (Scale >= 1.f) return;
		const int32 W = FMath::Max(1, FMath::RoundToInt(Width * Scale));
		const int32 H = FMath::Max(1, FMath::RoundToInt(Height * Scale));
		TArray<FColor> Scaled;
		Scaled.SetNumUninitialized(W * H);
		FImageUtils::ImageResize(Width, Height, Pixels, W, H, Scaled, false, true);
		Pixels = MoveTemp(Scaled);
		Width = W;
		Height = H;
	}
}

namespace ZettaAgentAttachments
{
	FString Picture(const TArray<uint8>& Bytes, const FString& Name, FZettaAgentFile& Out)
	{
		const FString Ext = FPaths::GetExtension(Name).ToLower();
		FString Type = ZettaImages::SniffContentType(Bytes);
		if (Type.IsEmpty() && Ext == TEXT("gif")) Type = TEXT("image/gif");
		Out.Name = FPaths::GetCleanFilename(Name);

		// Small pictures go as they are; a large photo is scaled to 1600 px: the model gains nothing from more.
		if (!Type.IsEmpty() && Bytes.Num() <= 2500000)
		{
			Out.Data = FBase64::Encode(Bytes);
			Out.MediaType = Type;
			return FString();
		}
		TArray<uint8> Bgra;
		int32 Width = 0, Height = 0;
		if (!FZettaImageCache::Decode(Bytes, Bgra, Width, Height) || Width <= 0 || Height <= 0)
		{
			if (!Type.IsEmpty() && Bytes.Num() * 4 / 3 + 4 <= MaxImageChars)
			{
				Out.Data = FBase64::Encode(Bytes);
				Out.MediaType = Type;
				return FString();
			}
			return LOCTEXT("AgentUI.PictureUnreadable", "That picture could not be read, or is too large. Use a PNG or JPEG under 3 MB.").ToString();
		}
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);
		FMemory::Memcpy(Pixels.GetData(), Bgra.GetData(), FMath::Min<int64>(Bgra.Num(), (int64)Pixels.Num() * 4));
		AttachFit(Pixels, Width, Height, 1600);
		Out.Data = AttachEncodeJpeg(Pixels, Width, Height);
		Out.MediaType = TEXT("image/jpeg");
		if (Out.Data.IsEmpty()) return LOCTEXT("AgentUI.PictureNotEncoded", "That picture could not be prepared for the agent.").ToString();
		return FString();
	}

	FString FromPath(const FString& InPath, FZettaAttachment& Out)
	{
		Out = FZettaAttachment();
		const FString Full = FPaths::ConvertRelativePathToFull(InPath);
		const FString Name = FPaths::GetCleanFilename(Full);
		if (!IFileManager::Get().FileExists(*Full))
		{
			return FText::Format(LOCTEXT("AgentUI.NotAFile", "“{0}” is not a file."), FText::FromString(Name)).ToString();
		}
		const FString Ext = FPaths::GetExtension(Full).ToLower();
		const FString Root = AttachProjectRoot();

		// Already in the project: tag it where it is — nothing to copy.
		if (Full.StartsWith(Root))
		{
			const FString Relative = Full.Mid(Root.Len());
			if (Ext == TEXT("uasset") || Ext == TEXT("umap"))
			{
				FString Package;
				if (FPackageName::TryConvertFilenameToLongPackageName(Full, Package))
				{
					Out.Kind = TEXT("mention");
					Out.Label = Package;
					Out.Mention.Kind = TEXT("asset");
					Out.Mention.Path = Package;
					Out.Mention.Class = Ext == TEXT("umap") ? TEXT("World") : FString();
					return FString();
				}
			}
			if (!AttachIsImage(Ext))
			{
				Out.Kind = TEXT("mention");
				Out.Label = Relative;
				Out.Mention = FZettaMention::FromFile(Relative);
				return FString();
			}
		}

		const int64 Size = IFileManager::Get().FileSize(*Full);
		if (Size > 64 * 1024 * 1024 && !Full.StartsWith(Root))
		{
			return FText::Format(LOCTEXT("AgentUI.TooBigToAttach", "“{0}” is larger than 64 MB — copy it into the project yourself, then tag it with @."), FText::FromString(Name)).ToString();
		}
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Full))
		{
			return FText::Format(LOCTEXT("AgentUI.CannotRead", "Could not read “{0}”."), FText::FromString(Name)).ToString();
		}

		if (AttachIsImage(Ext))
		{
			const FString Problem = Picture(Bytes, Name, Out.File);
			if (!Problem.IsEmpty()) return Problem;
			Out.Kind = TEXT("image");
			Out.Label = FText::Format(LOCTEXT("AgentUI.PictureLabel", "picture {0}"), FText::FromString(Name)).ToString();
			Out.Original = MoveTemp(Bytes);
			Out.OriginalName = Name;
			return FString();
		}

		if (Ext == TEXT("pdf"))
		{
			if (Bytes.Num() > MaxPdfBytes)
			{
				return FText::Format(LOCTEXT("AgentUI.PdfTooBig", "“{0}” is {1} MB — PDFs up to 6 MB can be attached."),
					FText::FromString(Name), FText::AsNumber(Bytes.Num() / (1024.0 * 1024.0))).ToString();
			}
			Out.Kind = TEXT("document");
			Out.Label = FText::Format(LOCTEXT("AgentUI.PdfLabel", "PDF {0}"), FText::FromString(Name)).ToString();
			Out.File.Data = FBase64::Encode(Bytes);
			Out.File.MediaType = TEXT("application/pdf");
			Out.File.Name = Name;
			return FString();
		}

		if ((AttachIsText(Ext) || AttachLooksLikeText(Bytes)) && Bytes.Num() <= MaxTextBytes)
		{
			FUTF8ToTCHAR Converted((const ANSICHAR*)Bytes.GetData(), Bytes.Num());
			Out.Kind = TEXT("text");
			Out.Label = Name;
			Out.Text = FString(Converted.Length(), Converted.Get());
			return FString();
		}

		// Anything else — a model, a sound, a font — goes into the project, where the agent can import it.
		const FString Folder = FPaths::ProjectContentDir() / TEXT("Zetta3D/Attachments");
		IFileManager::Get().MakeDirectory(*Folder, true);
		FString Target = Folder / Name;
		for (int32 n = 2; IFileManager::Get().FileExists(*Target); ++n)
		{
			Target = Folder / FString::Printf(TEXT("%s-%d.%s"), *FPaths::GetBaseFilename(Name), n, *FPaths::GetExtension(Name));
		}
		if (!FFileHelper::SaveArrayToFile(Bytes, *Target))
		{
			return FText::Format(LOCTEXT("AgentUI.CannotCopy", "Could not copy “{0}” into the project."), FText::FromString(Name)).ToString();
		}
		const FString Relative = FPaths::ConvertRelativePathToFull(Target).Mid(Root.Len());
		Out.Kind = TEXT("mention");
		Out.Label = Relative;
		Out.Mention = FZettaMention::FromFile(Relative, TEXT("the user attached this file; it was copied into the project folder (not imported yet)"));
		return FString();
	}

	FString TextBlock(const FZettaAttachment& A)
	{
		return FString::Printf(TEXT("\n\n<attached_file name=\"%s\">\n%s\n</attached_file>"), *A.Label, *A.Text);
	}

	FString CaptureViewport(TArray<uint8>& OutJpeg)
	{
		FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		const TSharedPtr<IAssetViewport> Active = LevelEditor.GetFirstActiveViewport();
		FViewport* Viewport = Active.IsValid() ? Active->GetActiveViewport() : nullptr;
		if (!Viewport || Viewport->GetSizeXY().X <= 0)
		{
			return LOCTEXT("AgentUI.NoViewport", "Open a level viewport first.").ToString();
		}
		TArray<FColor> Pixels;
		if (!Viewport->ReadPixels(Pixels) || Pixels.Num() == 0)
		{
			return LOCTEXT("AgentUI.ViewportUnreadable", "Could not read the viewport. Click in it once, then try again.").ToString();
		}
		int32 Width = Viewport->GetSizeXY().X;
		int32 Height = Viewport->GetSizeXY().Y;
		if (Pixels.Num() != Width * Height) return LOCTEXT("AgentUI.ViewportUnreadable2", "Could not read the viewport.").ToString();
		for (FColor& C : Pixels) C.A = 255;
		AttachFit(Pixels, Width, Height, 1024);
		TArray64<uint8> Jpeg;
		if (!FImageUtils::CompressImage(Jpeg, TEXT("jpg"), FImageView(Pixels.GetData(), Width, Height), 88))
		{
			return LOCTEXT("AgentUI.ViewportNotEncoded", "Could not make a picture of the viewport.").ToString();
		}
		OutJpeg = TArray<uint8>(Jpeg.GetData(), (int32)Jpeg.Num());
		return FString();
	}

	FString Keep(const TArray<uint8>& Bytes, const FString& Name)
	{
		const FString Folder = AttachStoreFolder();
		IFileManager& Files = IFileManager::Get();
		Files.MakeDirectory(*Folder, true);

		// Kept a week: older pictures are let go whenever a new one arrives.
		TArray<FString> Old;
		const FDateTime Cutoff = FDateTime::UtcNow() - FTimespan::FromDays(7);
		Files.IterateDirectory(*Folder, [&Old, &Cutoff, &Files](const TCHAR* Path, bool bDirectory)
		{
			if (!bDirectory && Files.GetTimeStamp(Path) < Cutoff) Old.Add(Path);
			return true;
		});
		for (const FString& File : Old) Files.Delete(*File, false, true, true);

		const FString Safe = AttachSafeName(Name);
		const FString Stem = FPaths::GetBaseFilename(Safe);
		const FString Ext = FPaths::GetExtension(Safe, true);
		for (int32 n = 1; n < 1000; ++n)
		{
			const FString Candidate = n == 1 ? Safe : FString::Printf(TEXT("%s-%d%s"), *Stem, n, *Ext);
			const FString File = Folder / Candidate;
			if (Files.FileExists(*File))
			{
				// The same picture under the same name comes back as it was.
				TArray<uint8> Existing;
				if (FFileHelper::LoadFileToArray(Existing, *File) && Existing == Bytes) return AttachKeyPrefix + Candidate;
				continue;
			}
			return FFileHelper::SaveArrayToFile(Bytes, *File) ? AttachKeyPrefix + Candidate : FString();
		}
		return FString();
	}

	FString FileOf(const FString& Key)
	{
		if (!Key.StartsWith(AttachKeyPrefix)) return FString();
		const FString Name = Key.Mid(FCString::Strlen(AttachKeyPrefix)).TrimStartAndEnd();
		if (Name.IsEmpty() || Name != AttachSafeName(Name)) return FString();
		const FString File = AttachStoreFolder() / Name;
		return IFileManager::Get().FileExists(*File) ? File : FString();
	}
}

namespace ZettaAgentSession
{
	FString AttachedFile(const FString& Key) { return ZettaAgentAttachments::FileOf(Key); }
}

#undef LOCTEXT_NAMESPACE
