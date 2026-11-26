// Copyright Zetta 3D. See LICENSE.md.

#include "SZettaImageStudioTool.h"

#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "SZettaImageList.h"
#include "Zetta3DEditorModule.h"
#include "ZettaApi.h"
#include "ZettaAuth.h"
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
	constexpr int32 ImageMaxPrompt = 5000;
	const TCHAR* ImageEditLock = TEXT("Editing and composing are available on a paid plan. Generating stays free.");
	const TCHAR* ImageFourKLock = TEXT("4K is available on a paid plan. 1K and 2K stay free.");

	TArray<ZettaStudioUI::FOption> RatioOptions()
	{
		static const TCHAR* Keys[] = { TEXT("9:16"), TEXT("2:3"), TEXT("3:4"), TEXT("4:5"), TEXT("1:1"), TEXT("5:4"), TEXT("4:3"), TEXT("3:2"), TEXT("16:9"), TEXT("21:9") };
		static const TCHAR* Labels[] = { TEXT("9:16 · Story"), TEXT("2:3"), TEXT("3:4"), TEXT("4:5 · Post"), TEXT("1:1 · Square"), TEXT("5:4"), TEXT("4:3"), TEXT("3:2"), TEXT("16:9 · Wide"), TEXT("21:9 · Cinema") };
		TArray<ZettaStudioUI::FOption> Out;
		for (int32 i = 0; i < UE_ARRAY_COUNT(Keys); ++i) Out.Add({ Keys[i], FText::FromString(Labels[i]), FText() });
		return Out;
	}

	FString ImageSizeText(int64 Bytes)
	{
		return Bytes >= 1024 * 1024 ? FString::Printf(TEXT("%.1f MB"), Bytes / 1024.0 / 1024.0) : FString::Printf(TEXT("%lld KB"), FMath::Max<int64>(1, Bytes / 1024));
	}
}

void SZettaImageStudioTool::Construct(const FArguments& InArgs, FUseFor3D InUseFor3D)
{
	UseFor3D = MoveTemp(InUseFor3D);

	TArray<ZettaStudioUI::FOption> Sizes;
	for (const FString& Size : ZettaCatalog::Resolutions()) Sizes.Add({ Size, FText::FromString(Size), FText() });

	TSharedRef<SWidget> FormBody = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			ZettaStudioUI::Segmented([this]() { return Mode; }, [this](const FString& V) { Mode = V; Error.Reset(); },
				{ { TEXT("generate"), LOCTEXT("ModeGenerate", "Generate"), FText() }, { TEXT("edit"), LOCTEXT("ModeEdit", "Edit"), FText() },
				  { TEXT("compose"), LOCTEXT("ModeCompose", "Compose"), FText() } })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[
			ZettaStudioUI::Locked(MakeAttributeLambda([this]() { return Mode != TEXT("generate") && EditLocked() ? FText::FromString(ImageEditLock) : FText(); }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
		[
			ZettaStudioUI::Hint(MakeAttributeLambda([this]()
			{
				if (Mode == TEXT("edit")) return LOCTEXT("EditHint", "The picture is changed. Describe what should be different.");
				if (Mode == TEXT("compose")) return LOCTEXT("ComposeHint", "Several pictures are combined into one new picture.");
				return LOCTEXT("GenerateHint", "A new picture from your prompt.");
			}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
		[
			ZettaStudioUI::ShownWhen([this]() { return Mode == TEXT("edit"); },
				SAssignNew(EditSource, SZettaImageList).Label(LOCTEXT("ImageToEdit", "Image to edit")).Max(1))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
		[
			ZettaStudioUI::ShownWhen([this]() { return Mode == TEXT("compose"); },
				SAssignNew(ComposeSources, SZettaImageList).Label(LOCTEXT("ImagesToCombine", "Images to combine")).Max(14).Numbered(true)
				.Hint(LOCTEXT("ComposeListHint", "At least two. They are built into one new picture, in this order.")))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 3)[ZettaStudioUI::FieldLabel(LOCTEXT("Prompt", "Prompt"))]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).MinDesiredHeight(64)
			[
				SNew(SMultiLineEditableTextBox)
				.AutoWrapText(true)
				.HintText_Lambda([this]()
				{
					if (Mode == TEXT("edit")) return LOCTEXT("EditPromptHint", "Make the sky stormy and add rain on the stone");
					if (Mode == TEXT("compose")) return LOCTEXT("ComposePromptHint", "Put the cat from the first image onto the throne in the second");
					return LOCTEXT("GeneratePromptHint", "A brushed-brass compass on weathered oak, morning light");
				})
				.Text_Lambda([this]() { return FText::FromString(PromptText); })
				.OnTextChanged_Lambda([this](const FText& T) { PromptText = T.ToString().Left(ImageMaxPrompt); })
				.OnKeyDownHandler_Lambda([this](const FGeometry&, const FKeyEvent& Key)
				{
					// Ctrl/Cmd + Enter sends.
					if (Key.GetKey() == EKeys::Enter && (Key.IsControlDown() || Key.IsCommandDown()))
					{
						Submit();
						return FReply::Handled();
					}
					return FReply::Unhandled();
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
		[
			ZettaStudioUI::Hint(MakeAttributeLambda([this]()
			{
				return PromptText.Len() > ImageMaxPrompt * 0.8 ? FText::FromString(FString::Printf(TEXT("%s/%s"), *ZettaStudioUI::Number(PromptText.Len()), *ZettaStudioUI::Number(ImageMaxPrompt))) : FText();
			}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
		[
			ZettaStudioUI::ShownWhen([this]() { return Mode == TEXT("generate"); },
				SAssignNew(Styles, SZettaImageList).Label(LOCTEXT("StyleReferences", "Style references")).Max(3)
				.Hint(LOCTEXT("StyleHint", "Used for look and feel only. The picture is still made from your prompt.")))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
		[
			ZettaStudioUI::Field(LOCTEXT("AspectRatio", "Aspect ratio"),
				ZettaStudioUI::Dropdown([this]() { return Ratio; }, [this](const FString& V) { Ratio = V; }, []() { return RatioOptions(); }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
		[
			ZettaStudioUI::Field(LOCTEXT("Resolution", "Resolution"),
				ZettaStudioUI::Segmented([this]() { return Resolution; }, [this](const FString& V) { Resolution = V; }, Sizes,
					[this](const FString& K) { return ResolutionLock(K); }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
		[
			ZettaStudioUI::Locked(MakeAttributeLambda([this]() { return FText::FromString(ResolutionLock(TEXT("4K"))); }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
		[
			ZettaStudioUI::ShownWhen([]() { const FZettaStudioContext& C = FZettaStudioContext::Get(); return C.ImageModels.IsSet() && C.ImageModels->Models.Num() > 0; },
				ZettaStudioUI::Field(LOCTEXT("ImageModel", "Model"), ZettaStudioUI::Dropdown(
					[this]() { const FZettaImageModel* M = ActiveModel(); return M ? M->Id : FString(); },
					[this](const FString& V) { Model = V; Error.Reset(); FitResolution(); },
					[]()
					{
						TArray<ZettaStudioUI::FOption> Out;
						const FZettaStudioContext& C = FZettaStudioContext::Get();
						if (C.ImageModels.IsSet())
						{
							for (const FZettaImageModel& M : C.ImageModels->Models) Out.Add({ M.Id, FText::FromString(M.bLocked ? M.Label + TEXT(" (locked)") : M.Label), FText() });
						}
						return Out;
					},
					[](const FString& Key)
					{
						const FZettaStudioContext& C = FZettaStudioContext::Get();
						if (!C.ImageModels.IsSet()) return FString();
						for (const FZettaImageModel& M : C.ImageModels->Models)
						{
							if (M.Id == Key && M.bLocked) return M.LockedReason.IsEmpty() ? M.Label + TEXT(" is available on a paid plan.") : M.LockedReason;
						}
						return FString();
					})))
		];

	TSharedRef<SWidget> ResultsBody = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[SAssignNew(ResultsBox, SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(10.f, 10.f))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[ZettaStudioUI::Success(MakeAttributeLambda([this]() { return FText::FromString(Notice); }))];

	TSharedRef<SWidget> HistoryBody = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
			[
				ZettaStudioUI::SmallButton(LOCTEXT("AllImages", "All images"), []() { FZetta3DEditorModule::Get().OpenWindow(TEXT("Library")); }, true,
					LOCTEXT("AllImagesTip", "Open My Generations"))
			]
			+ SHorizontalBox::Slot().AutoWidth()[ZettaStudioUI::SmallButton(LOCTEXT("Refresh", "Refresh"), [this]() { LoadHistory(); })]
		]
		+ SVerticalBox::Slot().AutoHeight()[SAssignNew(HistoryBox, SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.f, 6.f))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[ZettaStudioUI::Hint(MakeAttributeLambda([this]() { return FText::FromString(HistoryNote); }))];

	TSharedRef<SWidget> Form = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[ZettaStudioUI::Group(FText(), FormBody)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[ZettaStudioUI::Group(LOCTEXT("Result", "Result"), ResultsBody)]
		+ SVerticalBox::Slot().AutoHeight()[ZettaStudioUI::Group(LOCTEXT("Recent", "Recent"), HistoryBody)];

	TSharedRef<SWidget> Footer = SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
		.Padding(FMargin(12, 8))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)[ZettaStudioUI::Error(MakeAttributeLambda([this]() { return FText::FromString(Error); }))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)[ZettaStudioUI::Warn(MakeAttributeLambda([this]() { return bBusy ? FText() : FText::FromString(NotReady()); }))]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
				.HAlign(HAlign_Center)
				.ContentPadding(FMargin(14, 6))
				.IsEnabled_Lambda([this]() { return !bBusy && NotReady().IsEmpty(); })
				.OnClicked_Lambda([this]()
				{
					Submit();
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Font(FZettaStyle::Font(10, true)).Text_Lambda([this]() { return SubmitText(); })
				]
			]
		];

	ChildSlot
	[
		Frame(LOCTEXT("ImageIntro", "Make pictures for your game — textures, concept art, decals — or a reference to build a 3D model from."), Form, Footer)
	];
	RenderResults();
}

// ─── Model, resolution, readiness ─────────────────────────────────────────────

const FZettaImageModel* SZettaImageStudioTool::ActiveModel() const
{
	const FZettaStudioContext& C = FZettaStudioContext::Get();
	if (!C.ImageModels.IsSet() || C.ImageModels->Models.Num() == 0) return nullptr;
	const FString Wanted = Model.IsEmpty() ? C.ImageModels->Default : Model;
	for (const FZettaImageModel& M : C.ImageModels->Models) if (M.Id == Wanted) return &M;
	return &C.ImageModels->Models[0];
}

bool SZettaImageStudioTool::EditLocked() const
{
	const FZettaStudioContext& C = FZettaStudioContext::Get();
	return C.Gates2D.IsSet() && FZettaStudioContext::No(C.Gates2D->AllowsImageEditing);
}

FString SZettaImageStudioTool::ResolutionLock(const FString& Size) const
{
	const FZettaImageModel* M = ActiveModel();
	if (M && M->Resolutions.Num() > 0 && !M->Resolutions.Contains(Size)) return FString::Printf(TEXT("Not available on %s."), *M->Label);
	const FZettaStudioContext& C = FZettaStudioContext::Get();
	if (Size == TEXT("4K") && C.Gates2D.IsSet() && FZettaStudioContext::No(C.Gates2D->Allows4kImages)) return ImageFourKLock;
	return FString();
}

void SZettaImageStudioTool::FitResolution()
{
	// A size the model or plan cannot make moves to the largest one it can.
	if (ResolutionLock(Resolution).IsEmpty()) return;
	for (int32 i = ZettaCatalog::Resolutions().Num() - 1; i >= 0; --i)
	{
		if (ResolutionLock(ZettaCatalog::Resolutions()[i]).IsEmpty())
		{
			Resolution = ZettaCatalog::Resolutions()[i];
			return;
		}
	}
}

void SZettaImageStudioTool::OnContextChanged()
{
	FitResolution();
	if (!FZettaAuth::Get().IsSignedIn())
	{
		// Signed out: nothing of the last account's stays on show.
		bHistoryLoaded = false;
		Results.Reset();
		RenderResults();
		if (HistoryBox.IsValid()) HistoryBox->ClearChildren();
		HistoryNote.Reset();
		return;
	}
	if (!bHistoryLoaded) LoadHistory();
}

void SZettaImageStudioTool::OnShown()
{
	if (!bHistoryLoaded && FZettaAuth::Get().IsSignedIn()) LoadHistory();
}

FString SZettaImageStudioTool::NotReady() const
{
	const bool bEditing = Mode != TEXT("generate");
	if (bEditing && EditLocked()) return ImageEditLock;
	if (Prompt().IsEmpty()) return TEXT("Write a prompt first.");
	if (Mode == TEXT("edit") && EditSource->Items().Num() != 1) return TEXT("Add the image to edit.");
	if (Mode == TEXT("compose") && ComposeSources->Items().Num() < 2) return TEXT("Add at least two images.");
	return FString();
}

FText SZettaImageStudioTool::SubmitText() const
{
	if (bBusy) return Mode == TEXT("generate") ? LOCTEXT("Generating", "Generating…") : LOCTEXT("Working", "Working…");
	if (Mode == TEXT("edit")) return LOCTEXT("EditImage", "Edit image");
	if (Mode == TEXT("compose")) return LOCTEXT("ComposeImage", "Compose image");
	return LOCTEXT("GenerateImage", "Generate image");
}

void SZettaImageStudioTool::StartEdit(const TArray<uint8>& Bytes, const FString& ContentType, const TOptional<int64>& PostId, const FString& Label)
{
	Mode = TEXT("edit");
	EditSource->ClearImages();
	EditSource->Add({ Bytes, ContentType, Label, PostId });
}

// ─── Submit ───────────────────────────────────────────────────────────────────

void SZettaImageStudioTool::Submit()
{
	if (bBusy || !NotReady().IsEmpty()) return;
	const FString Said = Prompt();
	const FString Size = ResolutionLock(Resolution).IsEmpty() ? Resolution : FString(TEXT("2K"));
	const FString AspectRatio = Ratio;
	bBusy = true;
	Error.Reset();
	Notice.Reset();

	ZettaApi::FImageOptions Options;
	Options.AspectRatio = AspectRatio;
	Options.Resolution = Size;
	Options.Model = Model;

	TWeakPtr<SZettaImageStudioTool> Weak = SharedThis(this);
	auto Done = [Weak, Said, AspectRatio, Size](const FZettaResponse& R, const FZettaImageResult& Result)
	{
		const TSharedPtr<SZettaImageStudioTool> Self = Weak.Pin();
		if (!Self) return;
		Self->bBusy = false;
		if (!R.bOk)
		{
			Self->Error = ZettaCatalog::Explain(R);
			return;
		}
		TArray<TArray<uint8>> Images;
		TArray<FString> Types;
		for (int32 i = 0; i < Result.Images.Num(); ++i)
		{
			if (Result.Images[i].Num() == 0) continue;
			Images.Add(Result.Images[i]);
			Types.Add(Result.ContentTypes.IsValidIndex(i) ? Result.ContentTypes[i] : FString(TEXT("image/png")));
		}
		if (Images.Num() == 0)
		{
			Self->Error = Result.Text.TrimStartAndEnd().IsEmpty() ? FString(TEXT("No picture came back. Try a different prompt.")) : Result.Text.TrimStartAndEnd();
			return;
		}
		Self->ShowResults(Images, Types, Said, AspectRatio, Size, Result.PostId);
		FZettaStudioContext::RefreshCredits();
		Self->LoadHistory();
	};

	if (Mode == TEXT("edit"))
	{
		const SZettaImageList::FItem& Item = EditSource->Items()[0];
		if (Item.PostId.IsSet()) ZettaApi::EditImage(Said, Options, TOptional<FZettaInlineImage>(), Item.PostId, Done);
		else ZettaApi::EditImage(Said, Options, FZettaInlineImage::From(Item.Bytes, Item.ContentType), TOptional<int64>(), Done);
	}
	else if (Mode == TEXT("compose"))
	{
		TArray<FZettaInlineImage> Inline;
		TArray<int64> Posts;
		for (const SZettaImageList::FItem& Item : ComposeSources->Items())
		{
			if (Item.PostId.IsSet()) Posts.Add(Item.PostId.GetValue());
			else Inline.Add(FZettaInlineImage::From(Item.Bytes, Item.ContentType));
		}
		ZettaApi::ComposeImages(Said, Options, Inline, Posts, Done);
	}
	else
	{
		TArray<FZettaInlineImage> References;
		for (const SZettaImageList::FItem& Item : Styles->Items()) References.Add(FZettaInlineImage::From(Item.Bytes, Item.ContentType));
		ZettaApi::GenerateImage(Said, Options, References, Done);
	}
}

// ─── Results ──────────────────────────────────────────────────────────────────

void SZettaImageStudioTool::ShowResults(const TArray<TArray<uint8>>& Images, const TArray<FString>& Types, const FString& ForPrompt, const FString& AspectRatio,
	const FString& InResolution, const TOptional<int64>& PostId)
{
	Results.Reset();
	for (int32 i = 0; i < Images.Num(); ++i)
	{
		FResult R;
		R.Bytes = Images[i];
		R.ContentType = Types[i];
		R.Prompt = ForPrompt;
		R.AspectRatio = AspectRatio;
		R.Resolution = InResolution;
		R.PostId = PostId;
		R.Brush = FZettaImageCache::Get().FromBytes(FString::Printf(TEXT("image-studio:%u:%d"), FCrc::MemCrc32(R.Bytes.GetData(), R.Bytes.Num()), R.Bytes.Num()), R.Bytes);
		Results.Add(MoveTemp(R));
	}
	RenderResults();
}

void SZettaImageStudioTool::RenderResults()
{
	if (!ResultsBox.IsValid()) return;
	ResultsBox->ClearChildren();
	if (Results.Num() == 0)
	{
		ResultsBox->AddSlot()[ZettaUI::Hint(LOCTEXT("ResultEmpty", "Your generated image will appear here."))];
		return;
	}
	for (int32 i = 0; i < Results.Num(); ++i) ResultsBox->AddSlot()[ResultCard(i)];
}

TSharedRef<SWidget> SZettaImageStudioTool::ResultCard(int32 Index)
{
	const FResult& R = Results[Index];
	TArray<FString> Facts;
	for (const FString& S : { R.AspectRatio, R.Resolution, ImageSizeText(R.Bytes.Num()) }) if (!S.IsEmpty()) Facts.Add(S);
	const FSlateBrush* Brush = R.Brush.IsValid() ? R.Brush.Get() : FAppStyle::GetNoBrush();
	const bool bWebp = R.ContentType == TEXT("image/webp");

	return SNew(SBox).WidthOverride(240)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).HeightOverride(240)
				[
					SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Thumb")).Padding(4)
					[
						SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
						[
							SNew(SImage).Image(Brush)
						]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[ZettaUI::Hint(FText::FromString(FString::Join(Facts, TEXT(" · "))))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
			[
				SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.f, 4.f))
				+ SWrapBox::Slot()
				[
					ZettaStudioUI::SmallButton(LOCTEXT("SaveToProject", "Save to project"), [this, Index]() { Save(Index); }, !bWebp,
						bWebp ? LOCTEXT("SaveWebp", "This picture is WebP, which Unreal does not import as a texture.") : LOCTEXT("SaveTip", "Save as a texture in the project's images folder"))
				]
				+ SWrapBox::Slot()
				[
					ZettaStudioUI::SmallButton(LOCTEXT("UseFor3D", "Use for 3D"), [this, Index]()
					{
						if (!Results.IsValidIndex(Index) || !UseFor3D) return;
						const FResult& Item = Results[Index];
						UseFor3D(Item.Bytes, ZettaImporter::Slug(Item.Prompt.Left(60)) + ZettaStudioAssets::DotExtension(Item.ContentType));
					}, true, LOCTEXT("UseFor3DTip", "Send this picture to Generate 3D"))
				]
				+ SWrapBox::Slot()
				[
					ZettaStudioUI::SmallButton(LOCTEXT("EditThis", "Edit this"), [this, Index]()
					{
						if (!Results.IsValidIndex(Index)) return;
						const FResult Item = Results[Index];
						StartEdit(Item.Bytes, Item.ContentType, Item.PostId, Item.Prompt);
					}, MakeAttributeLambda([this]() { return !EditLocked(); }), MakeAttributeLambda([this]() { return EditLocked() ? FText::FromString(ImageEditLock) : FText(); }))
				]
				+ SWrapBox::Slot()
				[
					ZettaStudioUI::SmallButton(LOCTEXT("Compose", "Compose"), [this, Index]()
					{
						if (!Results.IsValidIndex(Index)) return;
						const FResult& Item = Results[Index];
						Mode = TEXT("compose");
						ComposeSources->Add({ Item.Bytes, Item.ContentType, Item.Prompt, Item.PostId });
					}, MakeAttributeLambda([this]() { return !EditLocked(); }), MakeAttributeLambda([this]() { return EditLocked() ? FText::FromString(ImageEditLock) : FText(); }))
				]
			]
		];
}

void SZettaImageStudioTool::Save(int32 Index)
{
	if (!Results.IsValidIndex(Index)) return;
	const FResult& R = Results[Index];
	FString Problem;
	const FString Path = ZettaStudioAssets::SaveImage(R.Bytes, R.Prompt.TrimStartAndEnd().IsEmpty() ? FString(TEXT("image")) : R.Prompt, Problem);
	if (Path.IsEmpty())
	{
		Error = TEXT("Could not save the image: ") + Problem;
		return;
	}
	Error.Reset();
	Notice = TEXT("Saved to ") + Path;
	ZettaStudioAssets::ShowInContentBrowser({ Path });
}

// ─── History ──────────────────────────────────────────────────────────────────

void SZettaImageStudioTool::LoadHistory()
{
	bHistoryLoaded = true;
	TWeakPtr<SZettaImageStudioTool> Weak = SharedThis(this);
	ZettaApi::ListImages(1, 12, FString(), [Weak](const FZettaResponse& R, const TArray<FZettaImagePost>& Posts, int32)
	{
		const TSharedPtr<SZettaImageStudioTool> Self = Weak.Pin();
		if (!Self || !Self->HistoryBox.IsValid()) return;
		if (!R.bOk)
		{
			Self->HistoryNote = TEXT("Your library could not be loaded. ") + ZettaCatalog::Explain(R);
			return;
		}
		Self->HistoryBox->ClearChildren();
		for (const FZettaImagePost& Post : Posts)
		{
			const FText Tip = FText::FromString(Post.Prompt.TrimStartAndEnd().IsEmpty() ? FString(TEXT("Untitled")) : Post.Prompt);
			if (Post.ImageUrls.Num() == 0 || Post.ImageUrls[0].IsEmpty())
			{
				Self->HistoryBox->AddSlot()
				[
					SNew(SBox).WidthOverride(72).HeightOverride(72).ToolTipText(Tip)
					[
						SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Thumb")).HAlign(HAlign_Center).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("Failed", "Failed")).ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
					]
				];
				continue;
			}
			Self->HistoryBox->AddSlot()
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
				.ContentPadding(0)
				.ToolTipText(Tip)
				.OnClicked_Lambda([Weak, Post]()
				{
					if (const TSharedPtr<SZettaImageStudioTool> S = Weak.Pin()) S->Open(Post);
					return FReply::Handled();
				})
				[
					SNew(SZettaRemoteImage).Url(Post.ImageUrls[0]).Size(FVector2D(72, 72))
				]
			];
		}
		Self->HistoryNote = Posts.Num() == 0 ? TEXT("Images you generate appear here.") : TEXT("Click a picture to open it above.");
	});
}

void SZettaImageStudioTool::Open(const FZettaImagePost& Post)
{
	TWeakPtr<SZettaImageStudioTool> Weak = SharedThis(this);
	ZettaStudioAssets::DownloadPicture(Post.ImageUrls[0], [Weak, Post](const FString& Problem, const TArray<uint8>& Bytes, const FString& Type)
	{
		const TSharedPtr<SZettaImageStudioTool> Self = Weak.Pin();
		if (!Self) return;
		if (!Problem.IsEmpty())
		{
			Self->Error = TEXT("That image could not be opened. Refresh and try again.");
			return;
		}
		Self->Notice.Reset();
		Self->ShowResults({ Bytes }, { Type }, Post.Prompt, Post.AspectRatio, Post.Resolution, Post.Id);
	});
}

#undef LOCTEXT_NAMESPACE
