// Copyright Zetta 3D. See LICENSE.md.

#include "SZettaImageLibrary.h"
#include "Studio/ZettaStudio.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "ContentBrowserModule.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IContentBrowserSingleton.h"
#include "Misc/MessageDialog.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "SZettaLibraryTiles.h"
#include "ZettaApi.h"
#include "ZettaAuth.h"
#include "ZettaCatalog.h"
#include "ZettaHttp.h"
#include "ZettaImporter.h"
#include "ZettaLibrary.h"
#include "ZettaSettings.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace ZettaImageLibrary
{
	static constexpr int32 PageSize = 24;
	static constexpr double SearchDelay = 0.4;

	static FText FeatureLabel(const FString& Feature)
	{
		if (Feature == TEXT("genImg")) return LOCTEXT("Library.ImgGenerated", "Generated");
		if (Feature == TEXT("editImg")) return LOCTEXT("Library.ImgEdited", "Edited");
		if (Feature == TEXT("composeImg")) return LOCTEXT("Library.ImgComposed", "Composed");
		return LOCTEXT("Library.ImgPicture", "Picture");
	}

	static FString Caption(const FZettaImagePost& Post)
	{
		const FString Prompt = Post.Prompt.TrimStartAndEnd();
		return Prompt.IsEmpty() ? FString(TEXT("Untitled")) : Prompt;
	}
}

void SZettaImageLibrary::Construct(const FArguments&)
{
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("Library.SearchImages", "Search by prompt"))
				.OnTextChanged_Lambda([this](const FText& Text)
				{
					Search = Text.ToString();
					LastTyped = FPlatformTime::Seconds();
					if (bSearchPending) return;
					bSearchPending = true;
					// Typing pauses before the library is asked again, so each key doesn't make a request.
					RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
					{
						if (FPlatformTime::Seconds() - LastTyped < ZettaImageLibrary::SearchDelay) return EActiveTimerReturnType::Continue;
						bSearchPending = false;
						Reload();
						return EActiveTimerReturnType::Stop;
					}));
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				ZettaUI::Button(LOCTEXT("Library.Refresh", "Refresh"), FSimpleDelegate::CreateSP(this, &SZettaImageLibrary::Reload))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 4)
		[
			ZettaUI::ErrorText(MakeAttributeLambda([this]() { return FText::FromString(Error); }))
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot().Padding(8, 4)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(Grid, SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(10, 10))
				]
				// Loading / empty states.
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 24)
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda([this]() { return bLoading && Posts.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[SNew(SThrobber)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ZettaUI::Hint(LOCTEXT("Library.LoadingImages", "Loading your pictures…"))]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 24)
				[
					SNew(SBox)
					.Visibility_Lambda([this]() { return bLoaded && !bLoading && Error.IsEmpty() && Posts.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						ZettaUI::Hint(MakeAttributeLambda([this]()
						{
							return Search.TrimStartAndEnd().IsEmpty()
								? LOCTEXT("Library.NoImages", "Pictures you make in the Image Studio appear here.")
								: LOCTEXT("Library.NoImagesMatch", "No pictures match that search.");
						}))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 12)
				[
					SNew(SHorizontalBox)
					.Visibility_Lambda([this]() { return Total > Posts.Num() ? EVisibility::Visible : EVisibility::Collapsed; })
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
					[
						ZettaUI::Hint(MakeAttributeLambda([this]()
						{
							return FText::Format(LOCTEXT("Library.ShowingImages", "Showing {0} of {1}."), FText::AsNumber(Posts.Num()), FText::AsNumber(Total));
						}))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						ZettaUI::Button(LOCTEXT("Library.LoadMore", "Load more"), FSimpleDelegate::CreateLambda([this]() { Load(PageNumber + 1); }),
							MakeAttributeLambda([this]() { return !bLoading; }))
					]
				]
			]
		]
	];
}

void SZettaImageLibrary::EnsureLoaded()
{
	if (!bLoaded) Reload();
}

void SZettaImageLibrary::Reload()
{
	Load(1);
}

void SZettaImageLibrary::Reset()
{
	++Request;
	Posts.Reset();
	Total = 0;
	bLoaded = false;
	bLoading = false;
	Error.Reset();
	Redraw();
}

void SZettaImageLibrary::Load(int32 Page)
{
	if (!FZettaAuth::Get().IsSignedIn()) return;
	const int32 This = ++Request;
	bLoaded = true;
	bLoading = true;
	Error.Reset();
	if (Page == 1)
	{
		Posts.Reset();
		Total = 0;
		Redraw();
	}
	TWeakPtr<SZettaImageLibrary> Weak = SharedThis(this);
	ZettaApi::ListImages(Page, ZettaImageLibrary::PageSize, Search, [Weak, This, Page](const FZettaResponse& R, const TArray<FZettaImagePost>& Found, int32 Count)
	{
		const TSharedPtr<SZettaImageLibrary> Self = Weak.Pin();
		if (!Self.IsValid() || This != Self->Request) return;
		Self->bLoading = false;
		if (!R.bOk)
		{
			Self->Error = TEXT("Your pictures could not be loaded. ") + ZettaCatalog::Explain(R);
			return;
		}
		Self->PageNumber = Page;
		Self->Total = Count;
		for (const FZettaImagePost& Post : Found)
		{
			if (!Self->Posts.ContainsByPredicate([&Post](const FZettaImagePost& P) { return P.Id == Post.Id; })) Self->Posts.Add(Post);
		}
		Self->Redraw();
	});
}

void SZettaImageLibrary::Redraw()
{
	if (!Grid.IsValid()) return;
	Grid->ClearChildren();
	for (const FZettaImagePost& Post : Posts) Grid->AddSlot()[MakeTile(Post)];
}

TSharedRef<SWidget> SZettaImageLibrary::MakeTile(const FZettaImagePost& Post)
{
	using namespace ZettaLibraryUI;
	const FString Url = Post.ImageUrls.Num() > 0 ? Post.ImageUrls[0] : FString();
	const FString Caption = ZettaImageLibrary::Caption(Post);
	const int64 Id = Post.Id;
	const FZettaImagePost Copy = Post;

	TSharedRef<SHorizontalBox> Actions = SNew(SHorizontalBox);
	if (!Url.IsEmpty())
	{
		Actions->AddSlot().AutoWidth()
		[
			ZettaUI::Button(
				MakeAttributeLambda([this, Id]() { return Saving.Contains(Id) ? LOCTEXT("Library.SavingImage", "Saving…") : LOCTEXT("Library.SaveImage", "Save to project"); }),
				FSimpleDelegate::CreateLambda([this, Copy]() { Save(Copy); }),
				MakeAttributeLambda([this, Id]() { return !Saving.Contains(Id); }),
				LOCTEXT("Library.SaveImageTip", "Save it as a texture in the Images folder (Project Settings › Zetta 3D)"))
		];
	}
	Actions->AddSlot().FillWidth(1.f)[SNew(SSpacer)];
	Actions->AddSlot().AutoWidth().VAlign(VAlign_Center)
	[
		SNew(SComboButton)
		.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
		.HasDownArrow(true)
		.ToolTipText(LOCTEXT("Library.ImageMoreTip", "More: open, delete"))
		.OnGetMenuContent_Lambda([this, Copy, Url]()
		{
			FMenuBuilder Menu(true, nullptr);
			if (!Url.IsEmpty())
			{
				const FString Name = FString::Printf(TEXT("picture-%lld"), Copy.Id);
				Menu.AddMenuEntry(LOCTEXT("Library.UseFor3D", "Use for 3D"), LOCTEXT("Library.UseFor3DTip", "Open Generate 3D with this picture"), FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([Url, Name]()
					{
						FZettaHttp::Download(Url, [Name](const FZettaResponse& R)
						{
							if (R.bOk) ZettaStudio::SendImageToGenerate(R.Bytes, Name);
						});
					})));
				Menu.AddMenuEntry(LOCTEXT("Library.OpenImage", "Open the picture in the browser"), FText(), FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([Url]() { FPlatformProcess::LaunchURL(*Url, nullptr, nullptr); })));
				Menu.AddMenuSeparator();
			}
			Menu.AddMenuEntry(LOCTEXT("Library.DeleteImage", "Delete…"), FText(), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
				FUIAction(FExecuteAction::CreateLambda([this, Copy]() { Delete(Copy); })));
			return Menu.MakeWidget();
		})
	];

	return SNew(SBox)
		.WidthOverride(TileWidth)
		[
			SNew(SBorder)
			.BorderImage(FZettaStyle::Get().GetBrush("Zetta.Card"))
			.Padding(8)
			.ToolTipText(FText::FromString(Url.IsEmpty() && !Post.ErrorMessage.IsEmpty() ? Post.ErrorMessage : Caption))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SZettaRemoteImage).Url(Url).Size(FVector2D(ThumbSize, ThumbSize))
					.Placeholder(Url.IsEmpty() ? LOCTEXT("Library.NoPicture", "No picture") : LOCTEXT("Library.PicturePlaceholder", "Picture"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					SNew(STextBlock).Text(FText::FromString(Caption)).Font(FZettaStyle::Font(10, true)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
				[
					ZettaUI::Hint(ZettaLibrary::DateText(Post.CreatedAt))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 3, 0)
					[
						Url.IsEmpty()
							? Chip(LOCTEXT("Library.ImgFailed", "Failed"), FSlateColor(FZettaStyle::Error()), FText::FromString(Post.ErrorMessage))
							: Chip(ZettaImageLibrary::FeatureLabel(Post.Feature))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						Post.AspectRatio.IsEmpty() ? SNullWidget::NullWidget : Chip(FText::FromString(Post.AspectRatio))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					Actions
				]
			]
		];
}

void SZettaImageLibrary::Save(const FZettaImagePost& Post)
{
	if (Post.ImageUrls.Num() == 0 || Saving.Contains(Post.Id)) return;
	Saving.Add(Post.Id);
	Error.Reset();
	const int64 Id = Post.Id;
	// T_<prompt>_<id>: the id keeps two pictures with the same prompt apart, and saving one again replaces its copy.
	const FString Name = FString::Printf(TEXT("T_%s_%lld"), *ZettaImporter::Slug(ZettaImageLibrary::Caption(Post)).Replace(TEXT("-"), TEXT("_")), Id);
	const FString PackagePath = UZettaProjectSettings::Get()->GetImagesFolder() / Name;
	TWeakPtr<SZettaImageLibrary> Weak = SharedThis(this);
	FZettaHttp::Download(Post.ImageUrls[0], [Weak, Id, PackagePath](const FZettaResponse& R)
	{
		const TSharedPtr<SZettaImageLibrary> Self = Weak.Pin();
		if (Self.IsValid()) Self->Saving.Remove(Id);
		if (!R.bOk)
		{
			const FString Message = TEXT("The picture could not be downloaded. ") + ZettaCatalog::Explain(R);
			if (Self.IsValid()) Self->Error = Message;
			else ZettaLibrary::Toast(FText::FromString(Message), true);
			return;
		}
		FString ObjectPath, Problem;
		if (!ZettaImporter::SaveTexture(R.Bytes, PackagePath, ObjectPath, Problem, true))
		{
			if (Self.IsValid()) Self->Error = Problem;
			else ZettaLibrary::Toast(FText::FromString(Problem), true);
			return;
		}
		const FAssetData Asset = IAssetRegistry::GetChecked().GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
		if (Asset.IsValid())
		{
			FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().SyncBrowserToAssets({ Asset });
		}
		ZettaLibrary::Toast(FText::Format(LOCTEXT("Library.SavedImage", "Saved to {0}"), FText::FromString(PackagePath)));
	});
}

void SZettaImageLibrary::Delete(const FZettaImagePost& Post)
{
	const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNo,
		LOCTEXT("Library.DeleteImageBody", "Delete this picture from your Zetta 3D library? Copies saved into this project are kept."),
		LOCTEXT("Library.DeleteImageTitle", "Delete picture"));
	if (Answer != EAppReturnType::Yes) return;
	TWeakPtr<SZettaImageLibrary> Weak = SharedThis(this);
	ZettaApi::DeleteImage(Post.Id, [Weak](const FZettaResponse& R)
	{
		const TSharedPtr<SZettaImageLibrary> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		if (!R.bOk)
		{
			Self->Error = ZettaCatalog::Explain(R);
			return;
		}
		Self->Reload();
	});
}

#undef LOCTEXT_NAMESPACE
