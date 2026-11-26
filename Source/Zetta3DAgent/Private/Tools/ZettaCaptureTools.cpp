// Copyright Zetta 3D. See LICENSE.md.
// capture_view: what the level viewport (or the Play In Editor game) shows, as JPEGs the model can look at. "around"
// and "top" move the viewport camera for the shots and put it back exactly where the user had it.

#include "Camera/PlayerCameraManager.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
#include "LevelEditorViewport.h"
#include "Misc/Base64.h"
#include "RenderingThread.h"
#include "UnrealClient.h"
#include "ZettaAgentTools.h"
#include "ZettaTargets.h"
#include "ZettaToolCommon.h"

namespace ZettaCaptureImpl
{
	using ZettaTool::Fail;

	struct FShot
	{
		FString Label;
		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
	};

	FLevelEditorViewportClient* LevelViewport()
	{
		if (GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->Viewport) return GCurrentLevelEditingViewportClient;
		if (!GEditor) return nullptr;
		FLevelEditorViewportClient* Any = nullptr;
		for (FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
		{
			if (!Client || !Client->Viewport || Client->Viewport->GetSizeXY().X <= 0) continue;
			if (Client->IsPerspective()) return Client;
			if (!Any) Any = Client;
		}
		return Any;
	}

	/** Draws the viewport and reads it back as a JPEG at Width (aspect kept). */
	bool Grab(FViewport* Viewport, int32 Width, FZettaToolImage& Out, FString& OutError)
	{
		const FIntPoint Size = Viewport->GetSizeXY();
		if (Size.X <= 0 || Size.Y <= 0)
		{
			OutError = TEXT("The viewport is not visible (minimised or behind another tab). Ask the user to show it, then try again.");
			return false;
		}
		// A few frames let temporal effects (anti-aliasing, exposure) settle after a camera move.
		for (int32 i = 0; i < 3; ++i) Viewport->Draw(false);
		FlushRenderingCommands();
		TArray<FColor> Pixels;
		if (!Viewport->ReadPixels(Pixels) || Pixels.Num() != Size.X * Size.Y)
		{
			OutError = TEXT("Unreal could not read the viewport's pixels.");
			return false;
		}
		for (FColor& C : Pixels) C.A = 255;
		int32 W = Size.X, H = Size.Y;
		if (W != Width)
		{
			const int32 NewW = Width;
			const int32 NewH = FMath::Max(1, FMath::RoundToInt((float)H * Width / W));
			TArray<FColor> Scaled;
			Scaled.SetNumUninitialized(NewW * NewH);
			FImageUtils::ImageResize(W, H, Pixels, NewW, NewH, Scaled, false, true);
			Pixels = MoveTemp(Scaled);
			W = NewW;
			H = NewH;
		}
		IImageWrapperModule& Images = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Jpeg = Images.CreateImageWrapper(EImageFormat::JPEG);
		if (!Jpeg.IsValid() || !Jpeg->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), W, H, ERGBFormat::BGRA, 8))
		{
			OutError = TEXT("Unreal could not encode the picture.");
			return false;
		}
		const TArray64<uint8> Bytes = Jpeg->GetCompressed(85);
		Out.Data = FBase64::Encode(Bytes.GetData(), Bytes.Num());
		Out.MediaType = TEXT("image/jpeg");
		return true;
	}

	/** Actors roughly inside the camera's view cone, nearest first. */
	FString InView(UWorld* World, const FVector& Location, const FRotator& Rotation, float Fov)
	{
		struct FSeen
		{
			const AActor* Actor;
			double Distance;
		};
		TArray<FSeen> Seen;
		const FVector Forward = Rotation.Vector();
		const double Cone = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Fov * 0.5f * 1.1f, 5.f, 85.f)));
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			const AActor* A = *It;
			if (!IsValid(A) || !A->GetRootComponent() || A->IsHidden() || !A->IsListedInSceneOutliner()) continue;
			FVector Origin, Extent;
			A->GetActorBounds(true, Origin, Extent);
			if (Extent.IsNearlyZero()) continue;
			const FVector To = Origin - Location;
			const double Distance = To.Size();
			if (Distance < 1.0 || Distance > 200000.0) continue;
			// Inside the cone, or close enough that part of it surely is.
			if (FVector::DotProduct(Forward, To / Distance) < Cone && Distance > Extent.Size()) continue;
			Seen.Add({ A, Distance });
		}
		Seen.Sort([](const FSeen& A, const FSeen& B) { return A.Distance < B.Distance; });
		TArray<FString> Lines;
		for (int32 i = 0; i < Seen.Num() && i < 25; ++i)
		{
			Lines.Add(FString::Printf(TEXT("%s #%s [%s] %s m away"), *ZettaTargets::Label(Seen[i].Actor), *Seen[i].Actor->GetName(), *Seen[i].Actor->GetClass()->GetName(),
				*ZettaTool::Num(FMath::RoundToDouble(Seen[i].Distance / 10.0) / 10.0)));
		}
		if (Seen.Num() > 25) Lines.Add(FString::Printf(TEXT("… and %d more"), Seen.Num() - 25));
		return Lines.Num() > 0 ? TEXT("In view (nearest first): ") + FString::Join(Lines, TEXT("; ")) : FString(TEXT("Nothing from the level is in view."));
	}

	/** Shots that orbit a box: front, right, back, left at a raised angle, or one from straight above. */
	TArray<FShot> Orbit(const FVector& Center, const FVector& Extent, float BaseYaw, float Fov, bool bTop)
	{
		const double Radius = FMath::Max(Extent.Size(), 50.0);
		const double Distance = Radius / FMath::Sin(FMath::DegreesToRadians(FMath::Clamp(Fov, 20.f, 120.f) * 0.5)) * 1.15;
		TArray<FShot> Shots;
		if (bTop)
		{
			FShot S;
			S.Label = TEXT("from above");
			S.Location = Center + FVector(0, 0, Distance);
			S.Rotation = FRotator(-89.9, BaseYaw, 0);
			Shots.Add(S);
			return Shots;
		}
		static const TCHAR* Labels[] = { TEXT("front"), TEXT("right side"), TEXT("back"), TEXT("left side") };
		for (int32 i = 0; i < 4; ++i)
		{
			const FRotator Around(-25.0, BaseYaw + 180.0 + 90.0 * i, 0);
			FShot S;
			S.Label = Labels[i];
			S.Location = Center - Around.Vector() * Distance;
			S.Rotation = Around;
			Shots.Add(S);
		}
		return Shots;
	}

	FZettaToolResult CaptureGame(int32 Width)
	{
		UWorld* World = ZettaTool::PlayWorld();
		FViewport* Viewport = GEditor ? GEditor->GetPIEViewport() : nullptr;
		if (!World || !Viewport) return Fail(TEXT("Play In Editor is not running, so there is no game view. Start it with play_in_editor start, or capture view \"viewport\"."));
		FZettaToolImage Image;
		FString Error;
		if (!Grab(Viewport, Width, Image, Error)) return Fail(Error);
		FString Text = TEXT("The game (Play In Editor)");
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager)
			{
				const FVector L = PC->PlayerCameraManager->GetCameraLocation();
				const FRotator R = PC->PlayerCameraManager->GetCameraRotation();
				const float Fov = PC->PlayerCameraManager->GetFOVAngle();
				Text += FString::Printf(TEXT(", player camera at %s looking %s (FOV %s).\n%s"), *ZettaTool::Vec(L), *ZettaTool::Rot(R), *ZettaTool::Num(Fov), *InView(World, L, R, Fov));
			}
			if (const APawn* Pawn = PC->GetPawn()) Text += FString::Printf(TEXT("\nPlayer pawn: %s at %s."), *Pawn->GetName(), *ZettaTool::Vec(Pawn->GetActorLocation()));
		}
		FZettaToolResult R = ZettaTool::Ok(Text, TEXT("Captured the game view"));
		R.Images.Add(Image);
		return R;
	}

	FZettaToolResult CaptureView(const TSharedPtr<FJsonObject>& In)
	{
		const int32 Width = ZettaTool::IntIn(In, TEXT("width"), 768, 256, 1280);
		const FString View = ZettaTool::Str(In, TEXT("view"), TEXT("viewport")).ToLower();
		if (View == TEXT("game")) return CaptureGame(Width);
		if (View != TEXT("viewport") && View != TEXT("scene")) return Fail(TEXT("'view' must be viewport or game."));

		FLevelEditorViewportClient* Client = LevelViewport();
		if (!Client || !Client->Viewport) return Fail(TEXT("No level viewport is open. Ask the user to open one (Window › Viewports)."));
		UWorld* World = ZettaTool::EditorWorld();

		AActor* Target = nullptr;
		FString Error;
		if (ZettaTool::Has(In, TEXT("target")))
		{
			Target = ZettaTargets::FindActor(ZettaTool::Str(In, TEXT("target")), Error);
			if (!Target) return Fail(Error);
		}
		const FString Angles = ZettaTool::Str(In, TEXT("angles"), Target ? TEXT("around") : TEXT("current")).ToLower();
		if (Angles != TEXT("current") && Angles != TEXT("around") && Angles != TEXT("top")) return Fail(TEXT("'angles' must be current, around or top."));

		// The box to frame: the target, else the selection.
		FBox Box(ForceInit);
		float BaseYaw = 0.f;
		if (Target)
		{
			FVector Origin, Extent;
			Target->GetActorBounds(false, Origin, Extent, true);
			Box = FBox(Origin - Extent, Origin + Extent);
			BaseYaw = Target->GetActorRotation().Yaw;
		}
		else if (Angles != TEXT("current") && GEditor)
		{
			for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
			{
				if (const AActor* A = Cast<AActor>(*It))
				{
					FVector Origin, Extent;
					A->GetActorBounds(false, Origin, Extent, true);
					Box += FBox(Origin - Extent, Origin + Extent);
				}
			}
			if (!Box.IsValid) return Fail(TEXT("'around' and 'top' need a target (or a selection) to frame."));
		}

		TArray<FShot> Shots;
		const float Fov = Client->ViewFOV;
		if (Angles == TEXT("current"))
		{
			if (Target)
			{
				// "current" with a target: the viewport's own direction, moved to frame it.
				const FVector Center = Box.GetCenter();
				const double Radius = FMath::Max(Box.GetExtent().Size(), 50.0);
				const double Distance = Radius / FMath::Sin(FMath::DegreesToRadians(FMath::Clamp(Fov, 20.f, 120.f) * 0.5)) * 1.15;
				Shots.Add({ TEXT("current direction"), Center - Client->GetViewRotation().Vector() * Distance, Client->GetViewRotation() });
			}
			else
			{
				Shots.Add({ TEXT("as the user sees it"), Client->GetViewLocation(), Client->GetViewRotation() });
			}
		}
		else
		{
			Shots = Orbit(Box.GetCenter(), Box.GetExtent(), BaseYaw, Fov, Angles == TEXT("top"));
		}

		// Move the camera only for the shots, then put everything back.
		const FVector SavedLocation = Client->GetViewLocation();
		const FRotator SavedRotation = Client->GetViewRotation();
		const ELevelViewportType SavedType = Client->GetViewportType();
		const bool bMoves = !(Angles == TEXT("current") && !Target);
		if (bMoves && SavedType != LVT_Perspective) Client->SetViewportType(LVT_Perspective);

		FZettaToolResult Result;
		TArray<FString> Lines;
		for (int32 i = 0; i < Shots.Num(); ++i)
		{
			const FShot& S = Shots[i];
			if (bMoves)
			{
				Client->SetViewLocation(S.Location);
				Client->SetViewRotation(S.Rotation);
			}
			FZettaToolImage Image;
			if (!Grab(Client->Viewport, Width, Image, Error))
			{
				Result = Fail(Error);
				break;
			}
			Result.Images.Add(Image);
			Lines.Add(FString::Printf(TEXT("Image %d (%s): camera at %s, rotation %s."), i + 1, *S.Label, *ZettaTool::Vec(S.Location), *ZettaTool::Rot(S.Rotation)));
			if (i == 0 && World) Lines.Add(InView(World, S.Location, S.Rotation, Fov));
		}
		if (bMoves)
		{
			if (SavedType != LVT_Perspective) Client->SetViewportType(SavedType);
			Client->SetViewLocation(SavedLocation);
			Client->SetViewRotation(SavedRotation);
			Client->Invalidate();
		}
		if (Result.bIsError) return Result;

		FString Text = Target ? FString::Printf(TEXT("%s, size %s cm.\n"), *ZettaTargets::Describe(Target), *ZettaTool::Vec(Box.GetSize())) : FString();
		if (SavedType != LVT_Perspective && !bMoves) Text += TEXT("The viewport is in an orthographic view.\n");
		Text += FString::Join(Lines, TEXT("\n"));
		if (ZettaTool::IsPlaying()) Text += TEXT("\n(This is the editor level; view \"game\" shows the running game.)");
		Result.Content = ZettaTool::Clip(Text);
		Result.Summary = Shots.Num() > 1 ? FString::Printf(TEXT("Captured %d views"), Shots.Num()) : TEXT("Captured the viewport");
		return Result;
	}
}

void ZettaToolAreas::RegisterCaptureTools(FZettaAgentTools& Tools)
{
	Tools.Register(TEXT("capture_view"), &ZettaCaptureImpl::CaptureView, [](const TSharedPtr<FJsonObject>& In)
	{
		return ZettaTool::Preview(ZettaTool::Str(In, TEXT("view")).ToLower() == TEXT("game") ? TEXT("Look at the game") : TEXT("Look at the viewport"),
			ZettaTool::Has(In, TEXT("target")) ? TEXT("Framing ") + ZettaTool::Str(In, TEXT("target")) : FString());
	});
}
