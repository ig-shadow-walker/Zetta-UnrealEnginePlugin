// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaCatalog.h"

#include "ZettaHttp.h"
#include "ZettaJson.h"

namespace ZettaCatalog
{
	static FModelInfo MakeN1()
	{
		FModelInfo M;
		M.Id = TEXT("n1"); M.Label = TEXT("Zetta N1"); M.Blurb = TEXT("Base model");
		M.bLowPoly = true; M.bBarePrompt = true;
		return M;
	}

	static FModelInfo MakeN25()
	{
		FModelInfo M;
		M.Id = TEXT("n25"); M.Label = TEXT("Zetta N2.5"); M.Blurb = TEXT("Highest quality of mesh and texture");
		M.bPaid = true; M.bLowPoly = true; M.bQuadAnyBase = true; M.bDetailedGeometry = true; M.bDetailedTexture = true;
		M.bImageTuning = true; M.bParts = true;
		return M;
	}

	static FModelInfo MakeNp1()
	{
		FModelInfo M;
		M.Id = TEXT("np1"); M.Label = TEXT("Zetta Np1"); M.Blurb = TEXT("Clean topology, balanced quality");
		M.bPaid = true; M.bDetailedTexture = true; M.bExtremeTexture = true; M.bImageTuning = true;
		return M;
	}

	static FModelInfo MakeNp15()
	{
		FModelInfo M;
		M.Id = TEXT("np15"); M.Label = TEXT("Zetta Np1.5"); M.Blurb = TEXT("Clean topology — quad native");
		M.bPaid = true; M.bQuadAnyBase = true; M.bDetailedTexture = true; M.bExtremeTexture = true; M.bImageTuning = true;
		return M;
	}

	const FModelInfo& Model(const FString& Id)
	{
		static const FModelInfo N1 = MakeN1(), N25 = MakeN25(), Np1 = MakeNp1(), Np15 = MakeNp15();
		if (Id == TEXT("n25")) return N25;
		if (Id == TEXT("np1")) return Np1;
		if (Id == TEXT("np15")) return Np15;
		return N1;
	}

	const TArray<FString>& GenerateModels()
	{
		// Zetta N1 was retired on 2026-11-18 (as on zetta3d.ai): never offered or sent; old library rows still name it.
		static const TArray<FString> L = { TEXT("n25") };
		return L;
	}

	const TArray<FString>& SmartModels()
	{
		static const TArray<FString> L = { TEXT("np1"), TEXT("np15") };
		return L;
	}

	bool IsSplatFeature(const FString& Feature)
	{
		return Feature == TEXT("imageToSplat") || Feature == TEXT("worldSplatExport");
	}

	FString ModelLabel(const FString& M)
	{
		if (M == TEXT("n1")) return TEXT("Zetta N1");
		if (M == TEXT("n25")) return TEXT("Zetta N2.5");
		if (M == TEXT("np1")) return TEXT("Zetta Np1");
		if (M == TEXT("np15")) return TEXT("Zetta Np1.5");
		if (M == TEXT("r1")) return TEXT("Zetta R1");
		if (M == TEXT("zs2")) return TEXT("Zetta Splat 2");
		if (M == TEXT("zs21")) return TEXT("Zetta Splat 2.1");
		return M;
	}

	FString FeatureLabel(const FString& F)
	{
		static const TMap<FString, FString> Labels = {
			{ TEXT("img23d"), TEXT("Image to 3D") }, { TEXT("txt23d"), TEXT("Text to 3D") },
			{ TEXT("multiView23d"), TEXT("Multi-view to 3D") }, { TEXT("retopology"), TEXT("Retopology") },
			{ TEXT("segmentation"), TEXT("Segmentation") }, { TEXT("smartSegmentation"), TEXT("Smart Segmentation") },
			{ TEXT("uvUnwrap"), TEXT("Smart UV") }, { TEXT("imageToSplat"), TEXT("Gaussian Splat") },
			{ TEXT("worldSplatExport"), TEXT("Splat export (PLY)") }, { TEXT("worldMeshExport"), TEXT("World mesh") },
			{ TEXT("retexture"), TEXT("AI Texturing") }, { TEXT("autoRigging"), TEXT("AI Rigging") },
			{ TEXT("convertFormat"), TEXT("Converted") }, { TEXT("upload"), TEXT("Uploaded model") },
		};
		const FString* Label = Labels.Find(F);
		return Label ? *Label : TEXT("Model");
	}

	FInt32Range FaceRange(const FString& Model, const FString& Quality, bool bQuads)
	{
		auto R = [](int32 Min, int32 Max) { return FInt32Range(FInt32Range::BoundsType::Inclusive(Min), FInt32Range::BoundsType::Inclusive(Max)); };
		if (Model == TEXT("n25"))
		{
			if (Quality == TEXT("low-poly")) return bQuads ? R(500, 10000) : R(1000, 20000);
			if (Quality == TEXT("ultra")) return bQuads ? R(1000, 150000) : R(1000, 2000000);
			return bQuads ? R(1000, 150000) : R(1000, 1000000);
		}
		if (Model == TEXT("np1")) return R(48, 20000);
		if (Model == TEXT("np15")) return bQuads ? R(48, 25000) : R(48, 50000);
		return R(3100, 1500000);
	}

	int32 FacePreset(const FString& Model, const FString& Quality, bool bQuads)
	{
		if (Model == TEXT("n25")) return FaceRange(Model, Quality, bQuads).GetUpperBoundValue();
		if (Model == TEXT("np1")) return 20000;
		if (Model == TEXT("np15")) return bQuads ? 25000 : 50000;
		if (Quality == TEXT("low-poly")) return 9000;
		if (Quality == TEXT("ultra")) return 1000000;
		return 200000;
	}

	const TArray<FChoice>& Qualities()
	{
		static const TArray<FChoice> L = {
			{ TEXT("low-poly"), TEXT("Low-Poly"), TEXT("Game-ready, light meshes") },
			{ TEXT("high-res"), TEXT("High-Res"), TEXT("Detailed meshes") },
			{ TEXT("ultra"), TEXT("Ultra"), TEXT("The most detail the model makes") },
		};
		return L;
	}

	const TArray<FChoice>& RigTypes()
	{
		static const TArray<FChoice> L = {
			{ TEXT("biped"), TEXT("Humanoid") }, { TEXT("quadruped"), TEXT("Four legs") }, { TEXT("avian"), TEXT("Bird") },
			{ TEXT("serpentine"), TEXT("Snake") }, { TEXT("aquatic"), TEXT("Fish") }, { TEXT("hexapod"), TEXT("Six legs") },
			{ TEXT("octopod"), TEXT("Eight legs") }, { TEXT("others"), TEXT("Other creature") },
		};
		return L;
	}

	const TArray<FChoice>& RigSpecs()
	{
		static const TArray<FChoice> L = {
			{ TEXT("unreal"), TEXT("UE5 Mannequin"), TEXT("Unreal Engine 5's Mannequin skeleton — retargets straight onto UE5 animations.") },
			{ TEXT("standard"), TEXT("Standard"), TEXT("The rigger's own skeleton, finger bones included.") },
			{ TEXT("mixamo"), TEXT("Mixamo"), TEXT("For Mixamo and libraries that follow it.") },
			{ TEXT("unity"), TEXT("Unity Humanoid"), TEXT("Maps straight onto a Unity Humanoid avatar.") },
			{ TEXT("vrm"), TEXT("VRM 1.0"), TEXT("For VRM avatars and VTuber tools.") },
			{ TEXT("actorcore"), TEXT("ActorCore"), TEXT("For ActorCore motion libraries.") },
		};
		return L;
	}

	const TArray<FString>& Orientations()
	{
		static const TArray<FString> L = { TEXT("+x"), TEXT("-x"), TEXT("+y"), TEXT("-y") };
		return L;
	}

	const TArray<FString>& AspectRatios()
	{
		static const TArray<FString> L = { TEXT("1:1"), TEXT("16:9"), TEXT("9:16"), TEXT("4:3"), TEXT("3:4"), TEXT("3:2"), TEXT("2:3"), TEXT("4:5"), TEXT("5:4"), TEXT("21:9") };
		return L;
	}

	const TArray<FString>& Resolutions()
	{
		static const TArray<FString> L = { TEXT("1K"), TEXT("2K"), TEXT("4K") };
		return L;
	}

	bool IsProcessable(const FString& F)
	{
		static const TSet<FString> S = { TEXT("txt23d"), TEXT("img23d"), TEXT("multiView23d"), TEXT("retopology"), TEXT("upload"), TEXT("uvUnwrap"), TEXT("worldMeshExport") };
		return S.Contains(F);
	}

	FString Explain(const FZettaResponse& E)
	{
		const TSharedPtr<FJsonObject> Body = E.Object();
		if (E.Code == TEXT("INSUFFICIENT_CREDITS"))
		{
			const FString Required = ZettaJson::Str(Body, TEXT("required"));
			const FString Available = ZettaJson::Str(Body, TEXT("available"));
			return Required.IsEmpty()
				? TEXT("Not enough credits. Top up or upgrade at zetta3d.ai/pricing.")
				: FString::Printf(TEXT("This needs %s credits and you have %s. Top up or upgrade at zetta3d.ai/pricing."), *Required, *Available);
		}
		if (E.Code == TEXT("JOB_LIMIT_REACHED"))
		{
			return FString::Printf(TEXT("You already have %s job(s) running — the most your plan runs at once. Wait for one to finish, or upgrade."),
				*ZettaJson::Str(Body, TEXT("limit"), TEXT("the maximum")));
		}
		if (E.Code == TEXT("PLAN_CAPABILITY_REQUIRED"))
		{
			const TArray<TSharedPtr<FJsonObject>> Violations = ZettaJson::Objects(Body, TEXT("violations"));
			const FString First = Violations.Num() > 0 ? ZettaJson::Str(Violations[0], TEXT("message")) : FString();
			return (First.IsEmpty() ? TEXT("Your plan does not include this option.") : First) + TEXT(" Upgrade at zetta3d.ai/pricing.");
		}
		if (E.Code == TEXT("GEN3D_NOT_READY")) return TEXT("That model is not ready yet.");
		if (E.Code == TEXT("EARLY_ACCESS_REQUIRED")) return TEXT("This feature is in early access. Request access and we'll let you know the moment you're in. Nothing was charged.");
		if (E.Code == TEXT("FEATURE_UNAVAILABLE")) return TEXT("This kind of generation is temporarily unavailable while we work on it. Nothing was charged — please try again later.");
		return E.Message;
	}
}
