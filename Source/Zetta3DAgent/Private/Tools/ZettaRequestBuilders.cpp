// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaRequestBuilders.h"

#include "ZettaCatalog.h"
#include "ZettaImporter.h"
#include "ZettaModels.h"
#include "ZettaRequests.h"
#include "ZettaToolCommon.h"

namespace ZettaRequestBuildersImpl
{
	const TMap<FString, TArray<FString>>& AllowedOptions()
	{
		static const TMap<FString, TArray<FString>> Allowed = {
			{ TEXT("retopologize"), { TEXT("engine"), TEXT("model"), TEXT("polygonType"), TEXT("faceCount"), TEXT("detail"), TEXT("faceLevel"), TEXT("bake") } },
			{ TEXT("smart_uv"), { TEXT("bake"), TEXT("exportOrientation"), TEXT("compressGeometry") } },
			{ TEXT("texture"), { TEXT("prompt"), TEXT("imageId"), TEXT("quality"), TEXT("pbr"), TEXT("fast"), TEXT("textureAlignment"), TEXT("delight") } },
			{ TEXT("rig"), { TEXT("rigType"), TEXT("rigSpec"), TEXT("rigOutFormat") } },
			{ TEXT("segment"), { TEXT("smart"), TEXT("granularity"), TEXT("hint"), TEXT("splitByConnectivity") } },
		};
		return Allowed;
	}

	/** A choice from a fixed list, lower-cased, after an optional alias step; Fallback when absent. */
	bool Pick(const TSharedPtr<FJsonObject>& O, const FString& Key, const TArray<FString>& Allowed, const FString& Fallback, FString& Out, FString& OutError,
		TFunction<FString(const FString&)> Alias = nullptr)
	{
		FString V = ZettaTool::Str(O, Key).TrimStartAndEnd().ToLower();
		if (V.IsEmpty())
		{
			Out = Fallback;
			return true;
		}
		if (Alias) V = Alias(V);
		if (!Allowed.Contains(V))
		{
			OutError = FString::Printf(TEXT("'%s' must be one of %s."), *Key, *FString::Join(Allowed, TEXT(", ")));
			return false;
		}
		Out = V;
		return true;
	}

	FString PolygonAlias(const FString& V)
	{
		if (V == TEXT("quad") || V == TEXT("quads")) return TEXT("quadrilateral");
		if (V == TEXT("tri") || V == TEXT("tris") || V == TEXT("triangles")) return TEXT("triangle");
		return V;
	}

	/** A whole number field: unset when absent; an error when present but not whole. */
	bool WholeNumber(const TSharedPtr<FJsonObject>& O, const FString& Key, TOptional<int32>& Out, FString& OutError)
	{
		if (!ZettaTool::Has(O, Key)) return true;
		const TOptional<double> D = ZettaTool::Num(O, Key);
		if (!D.IsSet() || !FMath::IsNearlyEqual(D.GetValue(), FMath::RoundToDouble(D.GetValue())))
		{
			OutError = FString::Printf(TEXT("'%s' must be a whole number."), *Key);
			return false;
		}
		Out = (int32)D.GetValue();
		return true;
	}

	bool Retopologize(const TSharedPtr<FJsonObject>& O, ZettaRequestBuilders::FProcessPlan& Plan, FString& OutError)
	{
		if (ZettaTool::Has(O, TEXT("engine")) && ZettaTool::Has(O, TEXT("model")) && !ZettaTool::Str(O, TEXT("engine")).TrimStartAndEnd().Equals(ZettaTool::Str(O, TEXT("model")).TrimStartAndEnd(), ESearchCase::IgnoreCase))
		{
			OutError = TEXT("Give the engine once ('engine': r1 or n1).");
			return false;
		}
		FString Engine, PolygonType, Level;
		if (!Pick(O, ZettaTool::Has(O, TEXT("engine")) ? TEXT("engine") : TEXT("model"), { TEXT("r1"), TEXT("n1") }, TEXT("r1"), Engine, OutError)) return false;
		if (!Pick(O, TEXT("polygonType"), { TEXT("triangle"), TEXT("quadrilateral") }, TEXT("triangle"), PolygonType, OutError, &PolygonAlias)) return false;
		const bool bQuads = PolygonType == TEXT("quadrilateral");
		if (!Pick(O, ZettaTool::Has(O, TEXT("detail")) ? TEXT("detail") : TEXT("faceLevel"), { TEXT("low"), TEXT("medium"), TEXT("high") }, FString(), Level, OutError)) return false;
		TOptional<int32> Count;
		if (!WholeNumber(O, TEXT("faceCount"), Count, OutError)) return false;
		const TOptional<bool> Bake = ZettaTool::Bool(O, TEXT("bake"));

		Plan.Code = TEXT("smart_topology");
		Plan.PriceModel = Engine;
		// The Studio's order: model, polygonType, then the density field, then bake.
		Plan.Options->SetStringField(TEXT("model"), Engine);
		Plan.Options->SetStringField(TEXT("polygonType"), PolygonType);
		if (Engine == TEXT("r1"))
		{
			if (!Level.IsEmpty())
			{
				OutError = TEXT("'detail' (low/medium/high) is a Zetta N1 option. Zetta R1 takes 'faceCount' (500–20,000) — send one or the other, or set engine \"n1\".");
				return false;
			}
			const int32 Max = bQuads ? ZettaRequestBuilders::RetopologyMaxQuads : ZettaRequestBuilders::RetopologyMaxTriangles;
			const int32 Faces = Count.Get(ZettaRequestBuilders::RetopologyDefaultCount);
			if (Faces < ZettaRequestBuilders::RetopologyMin || Faces > Max)
			{
				OutError = FString::Printf(TEXT("'faceCount' must be between %s and %s for Zetta R1%s.%s"), *FString::FormatAsNumber(ZettaRequestBuilders::RetopologyMin), *FString::FormatAsNumber(Max),
					bQuads ? TEXT(" in quads (quads halve the ceiling)") : TEXT(""), Faces > Max ? TEXT(" For a denser mesh use engine \"n1\" with 'detail'.") : TEXT(""));
				return false;
			}
			Plan.Options->SetNumberField(TEXT("faceCount"), Faces);
			// True by default at the engine: only "off" travels.
			if (Bake.IsSet() && !Bake.GetValue()) Plan.Options->SetBoolField(TEXT("bake"), false);
			Plan.Summary.Add(FString::Printf(TEXT("Zetta R1 · %s %s%s"), *FString::FormatAsNumber(Faces), bQuads ? TEXT("quads") : TEXT("triangles"), Count.IsSet() ? TEXT("") : TEXT(" (default)")));
			Plan.Summary.Add(Bake.IsSet() && !Bake.GetValue() ? TEXT("Bare geometry (textures not baked onto the new mesh)") : TEXT("The original textures are baked onto the rebuilt mesh"));
		}
		else
		{
			if (Count.IsSet())
			{
				OutError = TEXT("'faceCount' is a Zetta R1 option — Zetta N1 takes 'detail' (low, medium or high) instead.");
				return false;
			}
			if (Bake.IsSet())
			{
				OutError = TEXT("'bake' is a Zetta R1 option — Zetta N1 rebuilds topology without one.");
				return false;
			}
			const FString FaceLevel = Level.IsEmpty() ? TEXT("medium") : Level;
			Plan.Options->SetStringField(TEXT("faceLevel"), FaceLevel);
			Plan.Summary.Add(FString::Printf(TEXT("Zetta N1 · %s detail · %s"), *FaceLevel, bQuads ? TEXT("quads") : TEXT("triangles")));
		}
		return true;
	}

	bool SmartUv(const TSharedPtr<FJsonObject>& O, ZettaRequestBuilders::FProcessPlan& Plan, FString& OutError)
	{
		const TOptional<bool> Bake = ZettaTool::Bool(O, TEXT("bake"));
		const TOptional<bool> Compress = ZettaTool::Bool(O, TEXT("compressGeometry"));
		FString Orientation = ZettaTool::Str(O, TEXT("exportOrientation")).TrimStartAndEnd().ToLower();
		Plan.Code = TEXT("smart_uv");
		if (!Orientation.IsEmpty())
		{
			if (Orientation == TEXT("x") || Orientation == TEXT("y")) Orientation = TEXT("+") + Orientation;
			if (!ZettaCatalog::Orientations().Contains(Orientation))
			{
				OutError = FString::Printf(TEXT("'exportOrientation' must be one of %s (leave it out to keep the model's own facing)."), *FString::Join(ZettaCatalog::Orientations(), TEXT(", ")));
				return false;
			}
			Plan.Options->SetStringField(TEXT("exportOrientation"), Orientation);
		}
		// The Studio panel's own rule: defaults travel as silence (bake only as false, compression only when on).
		if (Bake.IsSet() && !Bake.GetValue()) Plan.Options->SetBoolField(TEXT("bake"), false);
		if (Compress.IsSet() && Compress.GetValue()) Plan.Options->SetBoolField(TEXT("compressGeometry"), true);
		Plan.Summary.Add(Bake.IsSet() && !Bake.GetValue() ? TEXT("A clean new UV layout, bare (textures not re-baked)") : TEXT("A clean new UV layout, with the textures, rig and animation carried across"));
		if (!Orientation.IsEmpty()) Plan.Summary.Add(TEXT("Facing ") + Orientation);
		if (Compress.IsSet() && Compress.GetValue()) Plan.Summary.Add(TEXT("Geometry compression (smaller file)"));
		return true;
	}

	bool Texture(const TSharedPtr<FJsonObject>& O, ZettaRequestBuilders::FProcessPlan& Plan, FString& OutError)
	{
		const FString Prompt = ZettaTool::Str(O, TEXT("prompt")).TrimStartAndEnd();
		if (Prompt.Len() > 1024)
		{
			OutError = TEXT("'prompt' is longer than 1,024 characters.");
			return false;
		}
		const TOptional<int64> ImageId = ZettaTool::Int(O, TEXT("imageId"));
		if (ZettaTool::Has(O, TEXT("imageId")) && !ImageId.IsSet())
		{
			OutError = TEXT("'imageId' must be the number a picture tool returned.");
			return false;
		}
		if (Prompt.IsEmpty() && !ImageId.IsSet())
		{
			OutError = TEXT("texture needs a 'prompt' describing the surface, or an 'imageId' from generate_image (or both: the picture, changed as described).");
			return false;
		}
		FString Quality;
		if (!Pick(O, TEXT("quality"), { TEXT("standard"), TEXT("hd"), TEXT("8k") }, TEXT("standard"), Quality, OutError, [](const FString& V) -> FString
		{
			if (V == TEXT("detailed") || V == TEXT("4k")) return TEXT("hd");
			if (V == TEXT("extreme")) return TEXT("8k");
			if (V == TEXT("2k") || V == TEXT("normal")) return TEXT("standard");
			return V;
		})) return false;
		const bool bPbr = ZettaTool::BoolOr(O, TEXT("pbr"), true);
		const TOptional<bool> Fast = ZettaTool::Bool(O, TEXT("fast"));
		if (Fast.IsSet() && Fast.GetValue() && Quality != TEXT("standard"))
		{
			OutError = TEXT("'fast' only applies to standard quality (it is the quicker standard tier).");
			return false;
		}
		FString Alignment = ZettaTool::Str(O, TEXT("textureAlignment")).TrimStartAndEnd().ToLower();
		const TOptional<bool> Delight = ZettaTool::Bool(O, TEXT("delight"));

		Plan.Code = Quality == TEXT("hd") ? TEXT("retexture_hd") : Quality == TEXT("8k") ? TEXT("retexture_8k") : TEXT("retexture");
		Plan.Prompt = Prompt;
		Plan.ImageId = ImageId;
		if (!Alignment.IsEmpty())
		{
			if (Alignment == TEXT("image") || Alignment == TEXT("picture")) Alignment = TEXT("original_image");
			if (Alignment == TEXT("mesh")) Alignment = TEXT("geometry");
			if (Alignment != TEXT("original_image") && Alignment != TEXT("geometry"))
			{
				OutError = TEXT("'textureAlignment' must be original_image (fit to the picture) or geometry (fit to the mesh).");
				return false;
			}
			Plan.Options->SetStringField(TEXT("textureAlignment"), Alignment);
		}
		Plan.Options->SetBoolField(TEXT("enablePBR"), bPbr);
		// On by default at the engine; the Studio sends it whichever way it is set, so an explicit "off" is honoured.
		if (Delight.IsSet()) Plan.Options->SetBoolField(TEXT("delight"), Delight.GetValue());
		if (Fast.IsSet() && Fast.GetValue()) Plan.Options->SetBoolField(TEXT("textureFast"), true);

		const TCHAR* Resolution = Quality == TEXT("hd") ? TEXT("4K") : Quality == TEXT("8k") ? TEXT("8K") : TEXT("2K");
		FString Line = FString::Printf(TEXT("%s texture (up to %s)"), Quality == TEXT("standard") ? TEXT("Standard") : Quality == TEXT("hd") ? TEXT("Detailed") : TEXT("8K"), Resolution);
		if (Fast.IsSet() && Fast.GetValue()) Line += TEXT(", faster");
		Line += bPbr ? TEXT(" · PBR maps") : TEXT(" · colour only");
		if (!Alignment.IsEmpty()) Line += Alignment == TEXT("geometry") ? TEXT(" · fit to the mesh") : TEXT(" · fit to the picture");
		if (Delight.IsSet()) Line += Delight.GetValue() ? TEXT(" · baked-in lighting removed") : TEXT(" · keeps the picture's lighting");
		Plan.Summary.Add(Line);
		if (ImageId.IsSet()) Plan.Summary.Add(FString::Printf(TEXT("Reference: picture #%lld"), ImageId.GetValue()));
		if (!Prompt.IsEmpty())
		{
			Plan.Summary.Add(TEXT("Surface: ") + ZettaTool::Short(Prompt, 300));
			Plan.Summary.Add(ImageId.IsSet() ? TEXT("The picture is changed as described first, then applied.") : TEXT("A reference picture is drawn from the description first, then applied."));
		}
		return true;
	}

	bool Rig(const TSharedPtr<FJsonObject>& O, ZettaRequestBuilders::FProcessPlan& Plan, FString& OutError)
	{
		TArray<FString> RigTypes, RigSpecs;
		for (const ZettaCatalog::FChoice& C : ZettaCatalog::RigTypes()) RigTypes.Add(C.Key);
		for (const ZettaCatalog::FChoice& C : ZettaCatalog::RigSpecs()) RigSpecs.Add(C.Key);
		FString RigType, RigSpec, Format;
		if (!Pick(O, TEXT("rigType"), RigTypes, TEXT("biped"), RigType, OutError, [](const FString& V) -> FString
		{
			if (V == TEXT("humanoid") || V == TEXT("human")) return TEXT("biped");
			if (V == TEXT("bird")) return TEXT("avian");
			if (V == TEXT("snake")) return TEXT("serpentine");
			if (V == TEXT("fish")) return TEXT("aquatic");
			return V;
		})) return false;
		// In Unreal a humanoid gets the UE5 Mannequin skeleton unless asked otherwise.
		if (!Pick(O, TEXT("rigSpec"), RigSpecs, TEXT("unreal"), RigSpec, OutError, [](const FString& V) -> FString
		{
			if (V == TEXT("ue5") || V == TEXT("unreal engine") || V == TEXT("mannequin") || V == TEXT("ue5 mannequin")) return TEXT("unreal");
			if (V == TEXT("unity humanoid") || V == TEXT("mecanim")) return TEXT("unity");
			return V;
		})) return false;
		// A preset is a humanoid skeleton — on any other body it is not sent.
		if (RigType != ZettaCatalog::HumanoidRigType) RigSpec = TEXT("standard");
		if (!Pick(O, TEXT("rigOutFormat"), { TEXT("glb"), TEXT("fbx") }, TEXT("fbx"), Format, OutError)) return false;

		Plan.Code = TEXT("auto_rigging");
		// Each sent only when it differs from the web default (biped, the rigger's own naming, GLB).
		if (RigType != TEXT("biped")) Plan.Options->SetStringField(TEXT("rigType"), RigType);
		if (RigSpec != TEXT("standard")) Plan.Options->SetStringField(TEXT("rigSpec"), RigSpec);
		if (Format != TEXT("glb")) Plan.Options->SetStringField(TEXT("rigOutFormat"), Format);

		const ZettaCatalog::FChoice* Body = ZettaCatalog::RigTypes().FindByPredicate([&](const ZettaCatalog::FChoice& C) { return C.Key == RigType; });
		const ZettaCatalog::FChoice* Spec = ZettaCatalog::RigSpecs().FindByPredicate([&](const ZettaCatalog::FChoice& C) { return C.Key == RigSpec; });
		Plan.Summary.Add(FString::Printf(TEXT("%s skeleton · %s bone naming · %s file"), Body ? *Body->Label : *RigType, RigSpec == TEXT("standard") ? TEXT("standard") : (Spec ? *Spec->Label : *RigSpec), *Format.ToUpper()));
		if (Format == TEXT("fbx")) Plan.Summary.Add(RigSpec == TEXT("unreal") ? TEXT("The FBX imports as a Skeletal Mesh on the UE5 Mannequin's bone names, ready to retarget UE5 animations.") : TEXT("The FBX imports as a Skeletal Mesh with its skeleton."));
		return true;
	}

	bool Segment(const TSharedPtr<FJsonObject>& O, ZettaRequestBuilders::FProcessPlan& Plan, FString& OutError)
	{
		const bool bSmart = ZettaTool::BoolOr(O, TEXT("smart"), false);
		FString Granularity;
		if (!Pick(O, TEXT("granularity"), { TEXT("simple"), TEXT("balanced"), TEXT("detailed") }, TEXT("balanced"), Granularity, OutError, [](const FString& V) -> FString
		{
			if (V == TEXT("few") || V == TEXT("coarse")) return TEXT("simple");
			if (V == TEXT("max") || V == TEXT("fine") || V == TEXT("many")) return TEXT("detailed");
			if (V == TEXT("medium")) return TEXT("balanced");
			return V;
		})) return false;
		const TOptional<bool> Split = ZettaTool::Bool(O, TEXT("splitByConnectivity"));
		const FString Hint = ZettaTool::Str(O, TEXT("hint")).TrimStartAndEnd();
		if (bSmart && Split.IsSet())
		{
			OutError = TEXT("'splitByConnectivity' applies to plain segmentation, not smart: true.");
			return false;
		}
		if (!bSmart && !Hint.IsEmpty())
		{
			OutError = TEXT("'hint' applies to smart segmentation — set smart: true, or leave the hint out.");
			return false;
		}
		if (Hint.Len() > 1024)
		{
			OutError = TEXT("'hint' is longer than 1,024 characters.");
			return false;
		}
		Plan.Code = bSmart ? TEXT("smart_segmentation") : TEXT("ai_segmentation");
		Plan.Options->SetStringField(TEXT("segmentGranularity"), Granularity);
		if (!bSmart)
		{
			if (Split.IsSet() && !Split.GetValue()) Plan.Options->SetBoolField(TEXT("splitByConnectivity"), false);
		}
		else if (!Hint.IsEmpty())
		{
			Plan.Options->SetStringField(TEXT("prompt"), Hint);
		}
		const TCHAR* Parts = Granularity == TEXT("simple") ? TEXT("few parts") : Granularity == TEXT("detailed") ? TEXT("as many parts as it finds") : TEXT("a balanced number of parts");
		Plan.Summary.Add(FString::Printf(TEXT("%s · %s"), bSmart ? TEXT("Smart Segment (rebuilds as it splits)") : TEXT("Segment"), Parts));
		if (!bSmart) Plan.Summary.Add(Split.IsSet() && !Split.GetValue() ? TEXT("Pieces that are not joined stay together") : TEXT("Pieces that are not joined become separate parts"));
		if (!Hint.IsEmpty()) Plan.Summary.Add(TEXT("What it is: ") + ZettaTool::Short(Hint, 200));
		return true;
	}
}

namespace ZettaRequestBuilders
{
	bool NormalizeOperation(const FString& Operation, FString& Out, FString& OutError)
	{
		const FString Op = Operation.TrimStartAndEnd().ToLower().Replace(TEXT("-"), TEXT("_")).Replace(TEXT(" "), TEXT("_"));
		static const TMap<FString, FString> Aliases = {
			{ TEXT("retopologize"), TEXT("retopologize") }, { TEXT("retopology"), TEXT("retopologize") }, { TEXT("retopo"), TEXT("retopologize") },
			{ TEXT("smart_topology"), TEXT("retopologize") }, { TEXT("remesh"), TEXT("retopologize") },
			{ TEXT("smart_uv"), TEXT("smart_uv") }, { TEXT("uv"), TEXT("smart_uv") }, { TEXT("uvs"), TEXT("smart_uv") }, { TEXT("uv_unwrap"), TEXT("smart_uv") }, { TEXT("unwrap"), TEXT("smart_uv") },
			{ TEXT("texture"), TEXT("texture") }, { TEXT("retexture"), TEXT("texture") }, { TEXT("texturing"), TEXT("texture") }, { TEXT("paint"), TEXT("texture") },
			{ TEXT("rig"), TEXT("rig") }, { TEXT("auto_rig"), TEXT("rig") }, { TEXT("autorig"), TEXT("rig") }, { TEXT("auto_rigging"), TEXT("rig") }, { TEXT("rigging"), TEXT("rig") },
			{ TEXT("segment"), TEXT("segment") }, { TEXT("segmentation"), TEXT("segment") }, { TEXT("ai_segmentation"), TEXT("segment") }, { TEXT("smart_segmentation"), TEXT("segment") },
			{ TEXT("split"), TEXT("segment") },
		};
		if (const FString* Found = Aliases.Find(Op))
		{
			Out = *Found;
			return true;
		}
		OutError = TEXT("'operation' must be one of retopologize, smart_uv, texture, rig, segment.");
		return false;
	}

	bool Process(const FString& Operation, const TSharedPtr<FJsonObject>& InOptions, FProcessPlan& Out, FString& OutError)
	{
		using namespace ZettaRequestBuildersImpl;
		FString Op;
		if (!NormalizeOperation(Operation, Op, OutError)) return false;
		const TSharedPtr<FJsonObject> Options = InOptions.IsValid() ? InOptions : MakeShared<FJsonObject>();
		const TArray<FString>& Allowed = AllowedOptions()[Op];
		TArray<FString> Unknown;
		for (const auto& Pair : Options->Values)
		{
			const FString Key(Pair.Key);
			if (!Allowed.Contains(Key) && Pair.Value.IsValid() && !Pair.Value->IsNull()) Unknown.Add(TEXT("'") + Key + TEXT("'"));
		}
		if (Unknown.Num() > 0)
		{
			OutError = FString::Printf(TEXT("Unknown option%s %s for %s. It takes: %s."), Unknown.Num() > 1 ? TEXT("s") : TEXT(""), *FString::Join(Unknown, TEXT(", ")), *Op, *FString::Join(Allowed, TEXT(", ")));
			return false;
		}
		Out = FProcessPlan();
		Out.Operation = Op;
		if (Op == TEXT("retopologize")) return Retopologize(Options, Out, OutError);
		if (Op == TEXT("smart_uv")) return SmartUv(Options, Out, OutError);
		if (Op == TEXT("texture")) return Texture(Options, Out, OutError);
		if (Op == TEXT("rig")) return Rig(Options, Out, OutError);
		return Segment(Options, Out, OutError);
	}

	FString RefuseSource(const FString& Operation, const FZettaGeneration& Source)
	{
		const int64 Id = Source.Id;
		if (!Source.IsSuccess())
		{
			return FString::Printf(TEXT("#%lld is %s (%s); only finished models can be processed.%s"), Id, *Source.StatusLabel().ToLower(), *Source.Status, Source.IsRunning() ? TEXT(" Wait for it to finish.") : TEXT(""));
		}
		if (Source.IsSplat())
		{
			return FString::Printf(TEXT("#%lld is a Gaussian splat (%s), not a mesh — process_model works on meshes only. %s"), Id, *ZettaCatalog::FeatureLabel(Source.Feature), ZettaImporter::SplatUnsupported);
		}
		if (!ZettaCatalog::IsProcessable(Source.Feature))
		{
			return FString::Printf(TEXT("#%lld is a %s result, which cannot be processed further. Pick a generated, uploaded, retopologized or Smart UV model."), Id, *ZettaCatalog::FeatureLabel(Source.Feature));
		}
		FString Op, Ignored;
		NormalizeOperation(Operation, Op, Ignored);
		if (Op == TEXT("retopologize") && Source.Feature == TEXT("retopology")) return FString::Printf(TEXT("#%lld is already a retopology result."), Id);
		if (Op == TEXT("smart_uv") && Source.Feature == TEXT("uvUnwrap")) return FString::Printf(TEXT("#%lld is already a Smart UV result."), Id);
		return FString();
	}

	FString SmartUvProblem(const FZettaGeneration& Source)
	{
		if (!Source.FaceCount.IsSet() || Source.FaceCount.GetValue() <= 0) return FString();
		const bool bQuads = Source.PolygonType == TEXT("quadrilateral");
		const int64 Triangles = Source.FaceCount.GetValue() * (bQuads ? 2 : 1);
		if (Triangles <= ZettaCatalog::SmartUvMaxTriangles) return FString();
		return FString::Printf(TEXT("#%lld has %s triangles%s; Smart UV takes at most %s. Retopologize it first (process_model operation retopologize), then run smart_uv on the result."),
			Source.Id, *FString::FormatAsNumber(Triangles), bQuads ? *FString::Printf(TEXT(" (%s quads, each counting as two)"), *FString::FormatAsNumber(Source.FaceCount.GetValue())) : TEXT(""),
			*FString::FormatAsNumber(ZettaCatalog::SmartUvMaxTriangles));
	}

	TSharedRef<FJsonObject> ProcessBody(const FProcessPlan& Plan, int64 SourcePostId, const FString& ImageUrl)
	{
		TSharedRef<FJsonObject> Options = MakeShared<FJsonObject>();
		for (const auto& Pair : Plan.Options->Values) Options->SetField(FString(Pair.Key), Pair.Value);
		if (Plan.NeedsPicture() && !ImageUrl.IsEmpty()) Options->SetStringField(TEXT("imageUrl"), ImageUrl);
		return ZettaRequests::PostProcess(Plan.Code, SourcePostId, Options);
	}

	FString OperationLabel(const FString& Operation)
	{
		if (Operation == TEXT("retopologize")) return TEXT("Retopology");
		if (Operation == TEXT("smart_uv")) return TEXT("Smart UV");
		if (Operation == TEXT("texture")) return TEXT("AI Texturing");
		if (Operation == TEXT("rig")) return TEXT("Auto Rig");
		if (Operation == TEXT("segment")) return TEXT("Segmentation");
		return TEXT("Processing");
	}
}
