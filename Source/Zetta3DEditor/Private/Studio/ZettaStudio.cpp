// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaStudio.h"

#include "AssetRegistry/AssetData.h"
#include "ContentBrowserMenuContexts.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/Paths.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Zetta3DEditorModule.h"
#include "ZettaAuth.h"
#include "ZettaStudioAssets.h"
#include "ZettaStudioLink.h"
#include "ZettaStyle.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace ZettaStudio
{
	struct FPendingImage
	{
		TArray<uint8> Bytes;
		FString Name;
	};

	static TOptional<FPendingImage> PendingImage;

	FSimpleMulticastDelegate& OnImageSent()
	{
		static FSimpleMulticastDelegate Delegate;
		return Delegate;
	}

	void SendImageToGenerate(const TArray<uint8>& Bytes, const FString& Name)
	{
		PendingImage = FPendingImage{ Bytes, Name };
		FZetta3DEditorModule::Get().OpenWindow(TEXT("Studio"));
		OnImageSent().Broadcast();
	}

	bool TakePendingImage(TArray<uint8>& OutBytes, FString& OutName)
	{
		if (!PendingImage.IsSet()) return false;
		OutBytes = MoveTemp(PendingImage->Bytes);
		OutName = PendingImage->Name;
		PendingImage.Reset();
		return true;
	}

	static void Notify(const FText& Text, bool bFailed)
	{
		FNotificationInfo Info(Text);
		Info.ExpireDuration = 5.f;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bFailed ? SNotificationItem::CS_Fail : SNotificationItem::CS_None);
		}
	}

	static void MakeModelFrom(const FAssetData& Asset)
	{
		UTexture* Texture = Cast<UTexture>(Asset.GetAsset());
		TArray<uint8> Bytes;
		FString Type, Name;
		if (!Texture || !ZettaStudioAssets::TextureBytes(Texture, Bytes, Type, Name))
		{
			Notify(LOCTEXT("TextureUnreadable", "Zetta 3D: that texture's pixels could not be read."), true);
			return;
		}
		SendImageToGenerate(Bytes, FPaths::GetBaseFilename(Name) + ZettaStudioAssets::DotExtension(Type));
	}

	static void SendModelTo(const FAssetData& Asset, FName Tool)
	{
		if (!FZettaAuth::Get().IsSignedIn())
		{
			FZetta3DEditorModule::Get().OpenWindow(TEXT("Studio"));
			Notify(LOCTEXT("SignInFirst", "Sign in to Zetta 3D first."), true);
			return;
		}
		Notify(LOCTEXT("FindingModel", "Zetta 3D: getting the model ready…"), false);
		ZettaStudioAssets::SourceFromAsset(Asset, [Tool](const FString& Error, const FZettaGenerationPtr& Generation)
		{
			if (!Error.IsEmpty() || !Generation.IsValid())
			{
				Notify(FText::Format(LOCTEXT("SendFailed", "Zetta 3D: {0}"), FText::FromString(Error.IsEmpty() ? TEXT("the model could not be used.") : Error)), true);
				return;
			}
			ZettaStudioLink::OpenTool(Tool, Generation);
		});
	}

	static FToolMenuSection& ZettaSection(UToolMenu* Menu)
	{
		return Menu->FindOrAddSection("Zetta3D", LOCTEXT("Zetta3DSection", "Zetta 3D"));
	}

	void RegisterMenus()
	{
		UToolMenus* Menus = UToolMenus::Get();
		if (!Menus) return;

		// Textures: build a 3D model from the picture.
		if (UToolMenu* Menu = Menus->ExtendMenu("ContentBrowser.AssetContextMenu.Texture2D"))
		{
			ZettaSection(Menu).AddDynamicEntry("Zetta3DTexture", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& Section)
			{
				const UContentBrowserAssetContextMenuContext* Context = Section.FindContext<UContentBrowserAssetContextMenuContext>();
				if (!Context || Context->SelectedAssets.Num() != 1) return;
				const FAssetData Asset = Context->SelectedAssets[0];
				Section.AddSubMenu("Zetta3DTextureMenu", LOCTEXT("Zetta3DMenu", "Zetta 3D"), LOCTEXT("Zetta3DTextureMenuTip", "Use this picture in the Zetta 3D Studio"),
					FNewToolMenuDelegate::CreateLambda([Asset](UToolMenu* SubMenu)
					{
						SubMenu->AddSection("Zetta3DActions").AddMenuEntry("Zetta3DMake3D",
							LOCTEXT("Make3D", "Make a 3D model from this picture"),
							LOCTEXT("Make3DTip", "Opens Generate 3D with this picture as its input. Nothing runs until you press Generate 3D."),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([Asset]() { MakeModelFrom(Asset); })));
					}),
					false, FSlateIcon(FZettaStyle::GetStyleSetName(), "Zetta.Icon"));
			}));
		}

		// Meshes: send one to a finishing tool.
		for (const TCHAR* MenuName : { TEXT("ContentBrowser.AssetContextMenu.StaticMesh"), TEXT("ContentBrowser.AssetContextMenu.SkeletalMesh") })
		{
			UToolMenu* Menu = Menus->ExtendMenu(MenuName);
			if (!Menu) continue;
			ZettaSection(Menu).AddDynamicEntry("Zetta3DMesh", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& Section)
			{
				const UContentBrowserAssetContextMenuContext* Context = Section.FindContext<UContentBrowserAssetContextMenuContext>();
				if (!Context || Context->SelectedAssets.Num() != 1) return;
				const FAssetData Asset = Context->SelectedAssets[0];
				Section.AddSubMenu("Zetta3DMeshMenu", LOCTEXT("Zetta3DMenu", "Zetta 3D"),
					LOCTEXT("Zetta3DMeshMenuTip", "Open this model in a Zetta 3D finishing tool. Models not made with Zetta 3D are uploaded to your library first (free)."),
					FNewToolMenuDelegate::CreateLambda([Asset](UToolMenu* SubMenu)
					{
						FToolMenuSection& Tools = SubMenu->AddSection("Zetta3DTools", LOCTEXT("SendToTool", "Send to a tool"));
						const TPair<FName, FText> Entries[] = {
							{ TEXT("Retopology"), LOCTEXT("SendRetopology", "Retopology") },
							{ TEXT("Segment"), LOCTEXT("SendSegment", "Segment") },
							{ TEXT("SmartUv"), LOCTEXT("SendSmartUv", "Smart UV") },
							{ TEXT("Texture"), LOCTEXT("SendTexture", "Texture") },
							{ TEXT("AutoRig"), LOCTEXT("SendAutoRig", "Auto Rig") },
						};
						for (const TPair<FName, FText>& Entry : Entries)
						{
							const FName Tool = Entry.Key;
							Tools.AddMenuEntry(Tool, Entry.Value, FText(), FSlateIcon(),
								FUIAction(FExecuteAction::CreateLambda([Asset, Tool]() { SendModelTo(Asset, Tool); })));
						}
					}),
					false, FSlateIcon(FZettaStyle::GetStyleSetName(), "Zetta.Icon"));
			}));
		}
	}
}

#undef LOCTEXT_NAMESPACE
