// Copyright Zetta 3D. See LICENSE.md.

#include "SZettaPostProcessTool.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "SZettaMultiViewInput.h"
#include "ZettaApi.h"
#include "ZettaCatalog.h"
#include "ZettaImageCache.h"
#include "ZettaImporter.h"
#include "ZettaStudioAssets.h"
#include "ZettaStudioContext.h"
#include "ZettaStudioUI.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace
{
	struct FTextureTier
	{
		const TCHAR* Code;
		const TCHAR* Resolution;
	};

	const FTextureTier TextureTiers[] = { { TEXT("retexture"), TEXT("2K") }, { TEXT("retexture_hd"), TEXT("4K") }, { TEXT("retexture_8k"), TEXT("8K") } };
}

FText SZettaTextureTool::Intro() const
{
	return LOCTEXT("TextureIntro", "Paint a new texture onto a model from a picture, or from a description of the surface.");
}

FText SZettaTextureTool::ButtonText() const { return LOCTEXT("ApplyTexture", "Apply texture"); }

TSharedRef<SWidget> SZettaTextureTool::BuildOptions()
{
	TSharedRef<SVerticalBox> Single = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
		[
			SAssignNew(Picture, SZettaImageInput)
			.Size(FVector2D(160, 160))
			.Label(LOCTEXT("OwnPicture", "Use my own picture —\ndrop an image or click"))
			.OnChanged_Lambda([this]()
			{
				if (bSettingPicture) return;
				if (Picture->HasImage())
				{
					const FString Problem = ZettaStudioAssets::CheckImage(Picture->GetBytes(), Picture->GetContentType());
					if (!Problem.IsEmpty())
					{
						PreviewError = Problem;
						TGuardValue<bool> Guard(bSettingPicture, true);
						Picture->Clear();
						return;
					}
				}
				ClearPreview();
				Refresh();
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[ZettaStudioUI::Hint(MakeAttributeLambda([this]() { return ReferenceHint(); }))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
		[
			ZettaStudioUI::Field(LOCTEXT("DescribeSurface", "Describe the surface"),
				SNew(SBox).MinDesiredHeight(44)
				[
					SNew(SMultiLineEditableTextBox)
					.AutoWrapText(true)
					.HintText(LOCTEXT("DescribeSurfaceHint", "e.g. worn brass with green patina settling in the recesses"))
					.Text_Lambda([this]() { return FText::FromString(DescribeText); })
					.OnTextChanged_Lambda([this](const FText& T)
					{
						if (T.ToString() == DescribeText) return;
						DescribeText = T.ToString().Left(1000);
						Refresh();
					})
				])
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
		[
			ZettaStudioUI::Hint(LOCTEXT("DescribeSurfaceTip", "Optional. A description is turned into a new reference picture you can check before applying."))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0).HAlign(HAlign_Left)
		[
			ZettaStudioUI::SmallButton(MakeAttributeLambda([this]()
			{
				return bPreviewing ? LOCTEXT("Drawing", "Drawing…") : Approved.Num() > 0 ? LOCTEXT("TryAgain", "Try again") : LOCTEXT("Preview", "Preview");
			}), [this]() { Preview(); }, MakeAttributeLambda([this]() { return !bPreviewing && !Description().IsEmpty() && Source().IsValid(); }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
		[
			ZettaStudioUI::ShownWhen([this]() { return Approved.Num() > 0; },
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
				[
					SNew(SBox).WidthOverride(220).HeightOverride(220)
					[
						SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Thumb")).Padding(4)
						[
							SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
							[
								SNew(SImage).Image_Lambda([this]() { return ApprovedBrush.IsValid() ? ApprovedBrush.Get() : FAppStyle::GetNoBrush(); })
							]
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
				[
					ZettaStudioUI::Hint(LOCTEXT("PreviewApplied", "This picture is applied as the new surface. Press Preview again for a different one."))
				])
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[ZettaStudioUI::Error(MakeAttributeLambda([this]() { return FText::FromString(PreviewError); }))];

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[ZettaUI::Section(LOCTEXT("Reference", "Reference"))]
		+ SVerticalBox::Slot().AutoHeight()
		[
			ZettaStudioUI::Segmented([this]() { return Mode; }, [this](const FString& V) { Mode = V; Refresh(); },
				{ { TEXT("single"), LOCTEXT("SingleImage", "Single image"), FText() }, { TEXT("multi"), LOCTEXT("MultiViewTex", "Multi-view"), FText() } })
		]
		+ SVerticalBox::Slot().AutoHeight()[ZettaStudioUI::ShownWhen([this]() { return Mode == TEXT("single"); }, Single)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
		[
			ZettaStudioUI::ShownWhen([this]() { return Mode == TEXT("multi"); },
				SAssignNew(Multi, SZettaMultiViewInput).OnChanged_Lambda([this]() { Refresh(); }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
		[
			ZettaStudioUI::Check(LOCTEXT("RemoveBgTexture", "Remove background before applying"), [this]() { return bRemoveBackground; }, [this](bool b) { bRemoveBackground = b; })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 6)[ZettaUI::Section(LOCTEXT("TextureSettings", "Settings"))]
		+ SVerticalBox::Slot().AutoHeight()
		[
			ZettaStudioUI::Field(LOCTEXT("TextureQuality", "Texture quality"), ZettaStudioUI::Segmented([this]() { return Tier; }, [this](const FString& V) { Tier = V; Refresh(); },
				{ { TEXT("retexture"), LOCTEXT("Standard", "Standard"), FText() }, { TEXT("retexture_hd"), LOCTEXT("Detailed", "Detailed"), FText() },
				  { TEXT("retexture_8k"), LOCTEXT("Tex8K", "8K"), FText() } }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
		[
			ZettaStudioUI::Hint(MakeAttributeLambda([this]()
			{
				const TCHAR* Resolution = TEXT("2K");
				for (const FTextureTier& T : TextureTiers) if (Tier == T.Code) Resolution = T.Resolution;
				const TOptional<int32> Price = FZettaStudioContext::Get().Price(Tier);
				return FText::FromString(FString::Printf(TEXT("Up to %s resolution"), Resolution) + (Price.IsSet() ? FString::Printf(TEXT(" (%d credits)"), Price.GetValue()) : FString()));
			}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[
			ZettaStudioUI::ShownWhen([this]() { return Tier == TEXT("retexture"); },
				ZettaStudioUI::Check(LOCTEXT("Fast", "Faster, at some detail — same price"), [this]() { return bFast; }, [this](bool b) { bFast = b; }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
		[
			ZettaStudioUI::Field(LOCTEXT("TextureFit", "Texture fit"), ZettaStudioUI::Segmented([this]() { return Alignment; }, [this](const FString& V) { Alignment = V; },
				{ { TEXT("geometry"), LOCTEXT("FitMesh", "Fit texture to mesh"), FText() }, { TEXT("original_image"), LOCTEXT("FitPicture", "Fit texture to picture"), FText() } }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
		[
			ZettaStudioUI::Check(LOCTEXT("PbrMaterials", "PBR materials"), [this]() { return bPbr; }, [this](bool b) { bPbr = b; },
				LOCTEXT("PbrMaterialsTip", "Roughness, metalness and normal maps as well as colour."))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
		[
			ZettaStudioUI::Check(LOCTEXT("DelightTexture", "Remove the reference's baked-in lighting"), [this]() { return bDelight; }, [this](bool b) { bDelight = b; },
				LOCTEXT("DelightTextureTip", "So the texture lights correctly in your own level."))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
		[
			ZettaStudioUI::Check(LOCTEXT("BakeEffects", "Bake effects into the textures"), [this]() { return bBake; }, [this](bool b) { bBake = b; })
		];
	return Box;
}

void SZettaTextureTool::OnSourceChanged()
{
	ImportedFor = -1;
	ClearPreview();
}

void SZettaTextureTool::ClearPreview()
{
	Approved.Reset();
	ApprovedType.Reset();
	ApprovedFor.Reset();
	ApprovedBrush.Reset();
	PreviewError.Reset();
}

FString SZettaTextureTool::SourceImageUrl() const
{
	const FZettaGenerationPtr From = Source();
	if (!From.IsValid()) return FString();
	for (const FString& Url : From->InputImageUrls) if (!Url.IsEmpty()) return Url;
	return FString();
}

FString SZettaTextureTool::ImportedAsset() const
{
	const FZettaGenerationPtr From = Source();
	if (!From.IsValid()) return FString();
	if (ImportedFor != From->Id)
	{
		ImportedFor = From->Id;
		ImportedPath = ZettaProjectIndex::MainAssetOf(From->Id);
	}
	return ImportedPath;
}

FText SZettaTextureTool::ReferenceHint() const
{
	if (Picture.IsValid() && Picture->HasImage()) return LOCTEXT("RefOwn", "Your picture is the reference.");
	if (!SourceImageUrl().IsEmpty()) return LOCTEXT("RefInput", "No picture chosen: the model's own input picture is the reference.");
	if (!ImportedAsset().IsEmpty()) return LOCTEXT("RefRender", "No picture chosen: a render of the imported model is the reference.");
	return LOCTEXT("RefNone", "Choose a reference picture, or describe the surface.");
}

FString SZettaTextureTool::Problem() const
{
	if (Mode == TEXT("multi"))
	{
		if (!Multi->HasView(TEXT("front"))) return TEXT("Add a front view — it is required.");
		return Multi->IsBusy() ? FString(TEXT("Generating views…")) : FString();
	}
	if (bPreviewing) return TEXT("Drawing the preview…");
	if (!Description().IsEmpty() && (Approved.Num() == 0 || ApprovedFor != Description())) return TEXT("Press Preview to see the new surface first.");
	if (Description().IsEmpty() && !Picture->HasImage() && SourceImageUrl().IsEmpty() && ImportedAsset().IsEmpty())
	{
		return TEXT("Choose a reference picture, or describe the surface.");
	}
	return FString();
}

void SZettaTextureTool::Reference(bool bRequired, TFunction<void(const FString&, const TArray<uint8>&, const FString&)> Done)
{
	if (Picture->HasImage())
	{
		Done(FString(), Picture->GetBytes(), Picture->GetContentType());
		return;
	}
	const FString Url = SourceImageUrl();
	if (!Url.IsEmpty())
	{
		ZettaStudioAssets::DownloadPicture(Url, Done);
		return;
	}
	ImportedFor = -1;
	const FString Asset = ImportedAsset();
	TArray<uint8> Jpeg;
	if (!Asset.IsEmpty() && ZettaStudioAssets::RenderAssetJpeg(Asset, 768, Jpeg))
	{
		Done(FString(), Jpeg, TEXT("image/jpeg"));
		return;
	}
	Done(bRequired ? FString(TEXT("Choose a reference picture, or describe the surface.")) : FString(), TArray<uint8>(), FString());
}

void SZettaTextureTool::Preview()
{
	const FString Said = Description();
	if (Said.IsEmpty() || bPreviewing) return;
	bPreviewing = true;
	PreviewError.Reset();
	Refresh();

	TWeakPtr<SZettaTextureTool> Weak = StaticCastSharedRef<SZettaTextureTool>(AsShared());
	auto Arrived = [Weak, Said](const FZettaResponse& R, const FZettaImageResult& Result)
	{
		const TSharedPtr<SZettaTextureTool> Self = Weak.Pin();
		if (!Self) return;
		Self->bPreviewing = false;
		if (!R.bOk) Self->PreviewError = ZettaCatalog::Explain(R);
		else if (Result.Images.Num() == 0 || Result.Images[0].Num() == 0)
		{
			Self->PreviewError = Result.Text.TrimStartAndEnd().IsEmpty() ? FString(TEXT("No picture came back. Try again.")) : Result.Text.TrimStartAndEnd();
		}
		else
		{
			Self->Approved = Result.Images[0];
			Self->ApprovedType = Result.ContentTypes.Num() > 0 ? Result.ContentTypes[0] : FString(TEXT("image/png"));
			Self->ApprovedFor = Said;
			Self->ApprovedBrush = FZettaImageCache::Get().FromBytes(
				FString::Printf(TEXT("texture-preview:%u:%d"), FCrc::MemCrc32(Self->Approved.GetData(), Self->Approved.Num()), Self->Approved.Num()), Self->Approved);
			FZettaStudioContext::RefreshCredits();
		}
		Self->Refresh();
	};

	Reference(false, [Weak, Said, Arrived](const FString& Error, const TArray<uint8>& Bytes, const FString& Type)
	{
		const TSharedPtr<SZettaTextureTool> Self = Weak.Pin();
		if (!Self) return;
		if (!Error.IsEmpty())
		{
			Self->bPreviewing = false;
			Self->PreviewError = Error;
			Self->Refresh();
			return;
		}
		ZettaApi::FImageOptions Options;
		Options.bFor3D = true;
		// With a reference the description changes it; without one, the surface is drawn from the words alone.
		if (Bytes.Num() > 0) ZettaApi::EditImage(Said, Options, FZettaInlineImage::From(Bytes, Type), TOptional<int64>(), Arrived);
		else ZettaApi::GenerateImage(Said, Options, {}, Arrived);
	});
}

void SZettaTextureTool::MakeOptions(FOptionsDone Done)
{
	TSharedRef<FJsonObject> Options = MakeShared<FJsonObject>();
	Options->SetStringField(TEXT("textureAlignment"), Alignment);
	Options->SetBoolField(TEXT("enablePBR"), bPbr);
	Options->SetBoolField(TEXT("delight"), bDelight);
	if (!bBake) Options->SetBoolField(TEXT("bake"), false);
	if (bFast && Tier == TEXT("retexture")) Options->SetBoolField(TEXT("textureFast"), true);

	TWeakPtr<SZettaTextureTool> Weak = StaticCastSharedRef<SZettaTextureTool>(AsShared());
	const bool bRemoveBg = bRemoveBackground;

	if (Mode == TEXT("multi"))
	{
		const TArray<SZettaMultiViewInput::FView> Views = Multi->Views();
		if (!Views.ContainsByPredicate([](const SZettaMultiViewInput::FView& V) { return V.Slot == TEXT("front"); }))
		{
			Done(TEXT("Add a front view — it is required."), nullptr);
			return;
		}
		struct FUploads
		{
			TArray<TSharedPtr<FJsonValue>> Items;
			TArray<float> Shares;
			int32 Left = 0;
			bool bFailed = false;
		};
		TSharedRef<FUploads> U = MakeShared<FUploads>();
		U->Items.SetNum(Views.Num());
		U->Shares.Init(0.f, Views.Num());
		U->Left = Views.Num();
		Stage(ZettaStudioAssets::UploadingText(0.f, TEXT("Uploading views")));
		for (int32 i = 0; i < Views.Num(); ++i)
		{
			const FString Slot = Views[i].Slot;
			ZettaStudioAssets::UploadImage(Views[i].Bytes, Views[i].ContentType, bRemoveBg, [Done, Options, U, i, Slot](const FString& Error, const FString& Url)
			{
				if (U->bFailed) return;
				if (!Error.IsEmpty())
				{
					U->bFailed = true;
					Done(Error, nullptr);
					return;
				}
				TSharedRef<FJsonObject> View = MakeShared<FJsonObject>();
				View->SetStringField(TEXT("viewType"), Slot);
				View->SetStringField(TEXT("imageUrl"), Url);
				U->Items[i] = MakeShared<FJsonValueObject>(View);
				if (--U->Left > 0) return;
				Options->SetArrayField(TEXT("multiViewImages"), U->Items);
				Done(FString(), Options);
			}, [Weak, U, i](float P)
			{
				// One percentage for the batch: each view's share of the bytes going up.
				U->Shares[i] = P;
				float Sum = 0.f;
				for (const float S : U->Shares) Sum += S;
				if (const TSharedPtr<SZettaTextureTool> Self = Weak.Pin()) Self->Stage(ZettaStudioAssets::UploadingText(Sum / U->Shares.Num(), TEXT("Uploading views")));
			});
		}
		return;
	}

	auto Upload = [Weak, Done, Options, bRemoveBg](const TArray<uint8>& Bytes, const FString& Type)
	{
		ZettaStudioAssets::UploadImage(Bytes, Type, bRemoveBg, [Done, Options](const FString& Error, const FString& Url)
		{
			if (!Error.IsEmpty())
			{
				Done(Error, nullptr);
				return;
			}
			Options->SetStringField(TEXT("imageUrl"), Url);
			Done(FString(), Options);
		}, [Weak](float P)
		{
			if (const TSharedPtr<SZettaTextureTool> Self = Weak.Pin()) Self->Stage(ZettaStudioAssets::UploadingText(P));
		});
	};

	if (!Description().IsEmpty())
	{
		if (Approved.Num() == 0 || ApprovedFor != Description())
		{
			Done(TEXT("Press Preview to see the new surface first."), nullptr);
			return;
		}
		Upload(Approved, ApprovedType);
		return;
	}
	Reference(true, [Done, Upload](const FString& Error, const TArray<uint8>& Bytes, const FString& Type)
	{
		if (!Error.IsEmpty()) Done(Error, nullptr);
		else Upload(Bytes, Type);
	});
}

#undef LOCTEXT_NAMESPACE
