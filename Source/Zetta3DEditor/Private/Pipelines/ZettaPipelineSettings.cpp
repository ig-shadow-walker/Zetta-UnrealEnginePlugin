// Copyright Zetta 3D. See LICENSE.md.

// The Studio tools' settings as data (the web's *-settings-value modules and postProcessRequestBody), the Generate
// form's rules (the web's gen3d-request), and what each node holds in settings (node-config). JavaScript truthiness
// is used on purpose: a graph means exactly what it means on the web.

#include "ZettaPipelineRecipe.h"

#include "ZettaCatalog.h"
#include "ZettaPipelineJson.h"

using namespace ZettaPJson;

// ─── Tool settings ──────────────────────────────────────────────────────────────────────────────────────────────────

namespace ZettaToolSettings
{
	const TCHAR* SmartTopology = TEXT("smart_topology");
	const TCHAR* AiSegmentation = TEXT("ai_segmentation");
	const TCHAR* SmartSegmentationCode = TEXT("smart_segmentation");
	const TCHAR* Retexture = TEXT("retexture");
	const TCHAR* RetextureHd = TEXT("retexture_hd");
	const TCHAR* Retexture8K = TEXT("retexture_8k");
	const TCHAR* AutoRigging = TEXT("auto_rigging");
	const TCHAR* SmartUvCode = TEXT("smart_uv");
	const TCHAR* SplatBase = TEXT("gaussian_splat");
	const TCHAR* SplatModel = TEXT("n25");

	const TArray<FZettaSettingOption>& RetopologyEngines()
	{
		static const TArray<FZettaSettingOption> L = {
			// Zetta R1 alone: Zetta N1 was retired on 2026-11-18, as on zetta3d.ai.
			{ TEXT("r1"), TEXT("Zetta R1"), TEXT("Up to 20,000 polygons, as triangles or quads.") },
		};
		return L;
	}

	const TArray<FZettaSettingOption>& FaceLevels()
	{
		static const TArray<FZettaSettingOption> L = {
			{ TEXT("low"), TEXT("Low"), TEXT("Fewest polygons — game-ready.") },
			{ TEXT("medium"), TEXT("Medium"), TEXT("A balance of the two.") },
			{ TEXT("high"), TEXT("High"), TEXT("Keeps the most detail.") },
		};
		return L;
	}

	const TArray<FZettaSettingOption>& PolygonTypes()
	{
		static const TArray<FZettaSettingOption> L = { { TEXT("triangle"), TEXT("Triangles"), FString() }, { TEXT("quadrilateral"), TEXT("Quads"), FString() } };
		return L;
	}

	const TArray<FZettaSettingOption>& SegmentModes()
	{
		static const TArray<FZettaSettingOption> L = {
			{ TEXT("segment"), TEXT("Segment"), TEXT("Splits the model you have into named parts.") },
			{ TEXT("smart"), TEXT("Smart Segment"), TEXT("Rebuilds as it splits, for cleaner parts on a rough or incomplete mesh.") },
		};
		return L;
	}

	const TArray<FZettaSettingOption>& SegmentGranularities()
	{
		static const TArray<FZettaSettingOption> L = {
			{ TEXT("simple"), TEXT("Few parts"), TEXT("Broad, obvious divisions.") },
			{ TEXT("balanced"), TEXT("Balanced"), TEXT("The usual answer.") },
			{ TEXT("detailed"), TEXT("Max parts"), TEXT("Finer, smaller pieces.") },
		};
		return L;
	}

	const TArray<FZettaSettingOption>& TextureTiers()
	{
		// The value IS the price code.
		static const TArray<FZettaSettingOption> L = {
			{ Retexture, TEXT("Standard"), TEXT("2K") }, { RetextureHd, TEXT("Detailed"), TEXT("4K") }, { Retexture8K, TEXT("8K"), TEXT("8K") },
		};
		return L;
	}

	const TArray<FZettaSettingOption>& TextureAlignments()
	{
		static const TArray<FZettaSettingOption> L = {
			{ TEXT("original_image"), TEXT("Fit texture to picture"), FString() }, { TEXT("geometry"), TEXT("Fit texture to mesh"), FString() },
		};
		return L;
	}

	const TArray<FZettaSettingOption>& SmartUvOrientations()
	{
		static const TArray<FZettaSettingOption> L = {
			{ TEXT("as-is"), TEXT("As is"), FString() }, { TEXT("+x"), TEXT("+X"), FString() }, { TEXT("-x"), TEXT("−X"), FString() },
			{ TEXT("+y"), TEXT("+Y"), FString() }, { TEXT("-y"), TEXT("−Y"), FString() },
		};
		return L;
	}

	const TArray<FZettaSettingOption>& RigTypes()
	{
		static const TArray<FZettaSettingOption> L = {
			{ TEXT("biped"), TEXT("Humanoid"), TEXT("Two legs, two arms. Character presets.") },
			{ TEXT("quadruped"), TEXT("Four legs"), TEXT("Dogs, horses, most mammals.") },
			{ TEXT("avian"), TEXT("Bird"), TEXT("Wings and a tail.") },
			{ TEXT("serpentine"), TEXT("Snake"), TEXT("A long spine, no limbs.") },
			{ TEXT("aquatic"), TEXT("Fish"), TEXT("Fins and a tail.") },
			{ TEXT("hexapod"), TEXT("Six legs"), TEXT("Insects.") },
			{ TEXT("octopod"), TEXT("Eight legs"), TEXT("Spiders and octopods.") },
			{ TEXT("others"), TEXT("Other creature"), TEXT("A body none of these describe.") },
		};
		return L;
	}

	const TArray<FZettaSettingOption>& RigSpecs()
	{
		static const TArray<FZettaSettingOption> L = {
			{ TEXT("standard"), TEXT("Standard"), TEXT("The rigger’s own skeleton, finger bones included.") },
			{ TEXT("mixamo"), TEXT("Mixamo"), TEXT("For Mixamo and libraries that follow it.") },
			{ TEXT("unity"), TEXT("Unity Humanoid"), TEXT("Maps straight onto a Unity Humanoid avatar.") },
			{ TEXT("unreal"), TEXT("UE5 Mannequin"), TEXT("Unreal Engine 5’s Mannequin skeleton.") },
			{ TEXT("vrm"), TEXT("VRM 1.0"), TEXT("For VRM avatars and VTuber tools.") },
			{ TEXT("actorcore"), TEXT("ActorCore"), TEXT("For ActorCore motion libraries.") },
		};
		return L;
	}

	const TArray<FZettaSettingOption>& RigOutFormats()
	{
		static const TArray<FZettaSettingOption> L = { { TEXT("glb"), TEXT("GLB"), FString() }, { TEXT("fbx"), TEXT("FBX"), FString() } };
		return L;
	}

	const TArray<FZettaSettingOption>& IsolateSensitivities()
	{
		static const TArray<FZettaSettingOption> L = {
			{ TEXT("low"), TEXT("Low"), TEXT("The whole character, and each thing it holds — weapon, shield. Anything not humanoid: its main body and what is attached to it.") },
			{ TEXT("medium"), TEXT("Medium"), TEXT("Also the head and the hands. Anything else: also its major parts — wheels, doors, wings, a tail.") },
			{ TEXT("high"), TEXT("High"), TEXT("Everything it can tell apart — helmet, armour pieces, belt, cape, boots…") },
		};
		return L;
	}

	const TArray<FZettaSettingOption>& IsolatePoses()
	{
		static const TArray<FZettaSettingOption> L = { { TEXT("t"), TEXT("T-pose"), FString() }, { TEXT("a"), TEXT("A-pose"), FString() } };
		return L;
	}

	const TArray<FZettaSettingOption>& DrawViewsChoices()
	{
		static const TArray<FZettaSettingOption> L = {
			{ TEXT("back"), TEXT("Back"), FString() }, { TEXT("left"), TEXT("Left"), FString() }, { TEXT("right"), TEXT("Right"), FString() },
		};
		return L;
	}

	const FZettaSettingOption* Find(const TArray<FZettaSettingOption>& Options, const FString& Value)
	{
		for (const FZettaSettingOption& O : Options) if (O.Value == Value) return &O;
		return nullptr;
	}

	FString LabelOf(const TArray<FZettaSettingOption>& Options, const FString& Value)
	{
		const FZettaSettingOption* O = Find(Options, Value);
		return O ? O->Label : Value;
	}

	// Defaults

	TSharedRef<FJsonObject> RetopologyDefaults()
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("engine"), TEXT("r1"));
		O->SetStringField(TEXT("faceLevel"), TEXT("medium"));
		O->SetStringField(TEXT("polygonType"), TEXT("triangle"));
		O->SetNumberField(TEXT("faces"), DefaultRetopologyFaces);
		O->SetBoolField(TEXT("bake"), true);
		return O;
	}

	TSharedRef<FJsonObject> SegmentDefaults()
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("mode"), TEXT("segment"));
		O->SetStringField(TEXT("granularity"), TEXT("balanced"));
		O->SetBoolField(TEXT("splitByConnectivity"), true);
		O->SetStringField(TEXT("hint"), TEXT(""));
		return O;
	}

	TSharedRef<FJsonObject> TextureDefaults()
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("tier"), Retexture);
		O->SetStringField(TEXT("alignment"), TEXT("original_image"));
		O->SetBoolField(TEXT("bake"), true);
		O->SetBoolField(TEXT("pbr"), true);
		O->SetBoolField(TEXT("fast"), false);
		O->SetBoolField(TEXT("delight"), true);
		O->SetBoolField(TEXT("removeBg"), true);
		return O;
	}

	TSharedRef<FJsonObject> SmartUvDefaults()
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("bake"), true);
		O->SetStringField(TEXT("orientation"), TEXT("as-is"));
		O->SetBoolField(TEXT("compress"), false);
		return O;
	}

	TSharedRef<FJsonObject> AutoRigDefaults()
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("rigType"), TEXT("biped"));
		O->SetStringField(TEXT("spec"), TEXT("standard"));
		O->SetStringField(TEXT("outFormat"), TEXT("glb"));
		return O;
	}

	TSharedRef<FJsonObject> SplatDefaults()
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("subject"), TEXT("object"));
		O->SetStringField(TEXT("engine"), SplatModel);
		O->SetBoolField(TEXT("removeBackground"), true);
		O->SetStringField(TEXT("ratio"), TEXT("16:9"));
		O->SetStringField(TEXT("resolution"), TEXT("1K"));
		return O;
	}

	TSharedRef<FJsonObject> IsolateDefaults()
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("identify"), true);
		O->SetStringField(TEXT("sensitivity"), TEXT("low"));
		O->SetNumberField(TEXT("maxParts"), 6);
		O->SetStringField(TEXT("pose"), TEXT("t"));
		O->SetArrayField(TEXT("extraParts"), {});
		O->SetField(TEXT("model"), JNull());
		return O;
	}

	static TSharedRef<FJsonObject> MergeOver(TSharedRef<FJsonObject> Defaults, const TSharedPtr<FJsonObject>& Value)
	{
		JMerge(Defaults, Value);
		return Defaults;
	}

	/** postProcessRequestBody's opening: the spec with its one operation code. */
	static TSharedRef<FJsonObject> Body(const FString& Code)
	{
		TSharedRef<FJsonObject> Spec = MakeShared<FJsonObject>();
		Spec->SetField(TEXT("postProcess"), JStrings({ Code }));
		TSharedRef<FJsonObject> B = MakeShared<FJsonObject>();
		B->SetObjectField(TEXT("spec"), Spec);
		return B;
	}

	// Retopology

	FInt32Range RetopologyBounds(const FString& PolygonType)
	{
		return PolygonType == TEXT("quadrilateral") ? FInt32Range(500, 10000 + 1) : FInt32Range(500, 20000 + 1);
	}

	bool RetopologyOutOfRange(const TSharedPtr<FJsonObject>& Value)
	{
		const TSharedRef<FJsonObject> V = MergeOver(RetopologyDefaults(), Value);
		if (JTextOr(V, TEXT("engine")) != TEXT("r1")) return false;
		const FVal Faces = JGet(V, TEXT("faces"));
		if (!JIsNumber(Faces)) return true;
		const double Count = Faces->AsNumber();
		const FInt32Range Bounds = RetopologyBounds(JTextOr(V, TEXT("polygonType")));
		return !FMath::IsFinite(Count) || Count < Bounds.GetLowerBoundValue() || Count > Bounds.GetUpperBoundValue() - 1;
	}

	FString RetopologyRangeProblem(const TSharedPtr<FJsonObject>& Value)
	{
		if (!RetopologyOutOfRange(Value)) return FString();
		const FInt32Range Bounds = RetopologyBounds(JTextOr(MergeOver(RetopologyDefaults(), Value), TEXT("polygonType")));
		return FString::Printf(TEXT("The polygon count must be between %s and %s."), *JCount(Bounds.GetLowerBoundValue()), *JCount(Bounds.GetUpperBoundValue() - 1));
	}

	TSharedRef<FJsonObject> RetopologyRequest(const TSharedPtr<FJsonObject>& Value)
	{
		const TSharedRef<FJsonObject> V = MergeOver(RetopologyDefaults(), Value);
		TSharedRef<FJsonObject> B = Body(SmartTopology);
		const FVal Engine = JGet(V, TEXT("engine"));
		if (JTruthy(Engine)) JSet(B, TEXT("model"), Engine);
		const bool bR1 = JTextOr(V, TEXT("engine")) == TEXT("r1");
		if (bR1)
		{
			if (JIsNumber(JGet(V, TEXT("faces")))) JSet(B, TEXT("faceCount"), JGet(V, TEXT("faces")));
		}
		else if (JTruthy(JGet(V, TEXT("faceLevel")))) JSet(B, TEXT("faceLevel"), JGet(V, TEXT("faceLevel")));
		if (JTruthy(JGet(V, TEXT("polygonType")))) JSet(B, TEXT("polygonType"), JGet(V, TEXT("polygonType")));
		const FVal Bake = JGet(V, TEXT("bake"));
		if (bR1 && JIsBool(Bake) && !Bake->AsBool()) B->SetBoolField(TEXT("bake"), false);
		return B;
	}

	TOptional<int32> RetopologyPrice(const FZettaPriceList* Prices, const TSharedPtr<FJsonObject>& Value)
	{
		if (!Prices) return {};
		return Prices->Price(SmartTopology, JTextOr(MergeOver(RetopologyDefaults(), Value), TEXT("engine")));
	}

	// Segmentation

	FString SegmentCode(const TSharedPtr<FJsonObject>& Value)
	{
		return JTextOr(MergeOver(SegmentDefaults(), Value), TEXT("mode")) == TEXT("smart") ? SmartSegmentationCode : AiSegmentation;
	}

	TSharedRef<FJsonObject> SegmentRequest(const TSharedPtr<FJsonObject>& Value)
	{
		const TSharedRef<FJsonObject> V = MergeOver(SegmentDefaults(), Value);
		TSharedRef<FJsonObject> B = Body(SegmentCode(V));
		const bool bSmart = JTextOr(V, TEXT("mode")) == TEXT("smart");
		if (bSmart)
		{
			const FString Hint = JTextOr(V, TEXT("hint")).TrimStartAndEnd();
			if (!Hint.IsEmpty()) B->SetStringField(TEXT("prompt"), Hint);
		}
		if (JTruthy(JGet(V, TEXT("granularity")))) JSet(B, TEXT("segmentGranularity"), JGet(V, TEXT("granularity")));
		if (!bSmart && !JTruthy(JGet(V, TEXT("splitByConnectivity")))) B->SetBoolField(TEXT("splitByConnectivity"), false);
		return B;
	}

	TOptional<int32> SegmentPrice(const FZettaPriceList* Prices, const TSharedPtr<FJsonObject>& Value)
	{
		if (!Prices) return {};
		return Prices->Price(SegmentCode(Value), FString());
	}

	// AI texturing

	TSharedRef<FJsonObject> TextureRequest(const TSharedPtr<FJsonObject>& Value, const TOptional<FString>& Prompt)
	{
		const TSharedRef<FJsonObject> V = MergeOver(TextureDefaults(), Value);
		const FString Tier = JAsText(JGet(V, TEXT("tier")));
		TSharedRef<FJsonObject> B = Body(Tier);
		const FString Words = Prompt.Get(FString()).TrimStartAndEnd();
		if (!Words.IsEmpty()) B->SetStringField(TEXT("prompt"), Words);
		if (JTruthy(JGet(V, TEXT("alignment")))) JSet(B, TEXT("textureAlignment"), JGet(V, TEXT("alignment")));
		if (!JTruthy(JGet(V, TEXT("bake")))) B->SetBoolField(TEXT("bake"), false);
		if (JHas(V, TEXT("pbr"))) JSet(B, TEXT("enablePBR"), JGet(V, TEXT("pbr")));
		if (JTruthy(JGet(V, TEXT("fast"))) && Tier == Retexture) B->SetBoolField(TEXT("textureFast"), true);
		if (JHas(V, TEXT("delight"))) JSet(B, TEXT("delight"), JGet(V, TEXT("delight")));
		return B;
	}

	TOptional<int32> TexturePrice(const FZettaPriceList* Prices, const TSharedPtr<FJsonObject>& Value)
	{
		if (!Prices) return {};
		return Prices->Price(JTextOr(MergeOver(TextureDefaults(), Value), TEXT("tier")), FString());
	}

	FString TextureTierLabel(const FString& Tier)
	{
		const FZettaSettingOption* O = Find(TextureTiers(), Tier);
		return O ? O->Label : FString(TEXT("Standard"));
	}

	// Smart UV: defaults travel as silence.

	TSharedRef<FJsonObject> SmartUvRequest(const TSharedPtr<FJsonObject>& Value)
	{
		const TSharedRef<FJsonObject> V = MergeOver(SmartUvDefaults(), Value);
		TSharedRef<FJsonObject> B = Body(SmartUvCode);
		const TOptional<FString> Orientation = JText(V, TEXT("orientation"));
		if (Orientation.Get(FString()) != TEXT("as-is") && JTruthy(JGet(V, TEXT("orientation")))) JSet(B, TEXT("exportOrientation"), JGet(V, TEXT("orientation")));
		if (JTruthy(JGet(V, TEXT("compress")))) B->SetBoolField(TEXT("compressGeometry"), true);
		if (!JTruthy(JGet(V, TEXT("bake")))) B->SetBoolField(TEXT("bake"), false);
		return B;
	}

	TOptional<int32> SmartUvPrice(const FZettaPriceList* Prices)
	{
		if (!Prices) return {};
		return Prices->Price(SmartUvCode, FString());
	}

	// Auto rig: each choice sent only when it is not the default.

	TSharedRef<FJsonObject> AutoRigRequest(const TSharedPtr<FJsonObject>& Value)
	{
		const TSharedRef<FJsonObject> V = MergeOver(AutoRigDefaults(), Value);
		TSharedRef<FJsonObject> B = Body(AutoRigging);
		const FString RigType = JTextOr(V, TEXT("rigType"));
		if (RigType != TEXT("biped") && JTruthy(JGet(V, TEXT("rigType")))) JSet(B, TEXT("rigType"), JGet(V, TEXT("rigType")));
		// Skeleton presets are humanoid skeletons: sent only for a humanoid, and only a preset offered.
		const FString Spec = JTextOr(V, TEXT("spec"));
		if (RigType == TEXT("biped") && Spec != TEXT("standard") && Find(RigSpecs(), Spec)) B->SetStringField(TEXT("rigSpec"), Spec);
		if (JTextOr(V, TEXT("outFormat")) != TEXT("glb") && JTruthy(JGet(V, TEXT("outFormat")))) JSet(B, TEXT("rigOutFormat"), JGet(V, TEXT("outFormat")));
		return B;
	}

	TOptional<int32> AutoRigPrice(const FZettaPriceList* Prices)
	{
		if (!Prices) return {};
		return Prices->Price(AutoRigging, FString());
	}

	// Splat (web-only; priced and rebuilt only so a graph that has one stays whole)

	TSharedRef<FJsonObject> SplatRequest(const TSharedPtr<FJsonObject>& Value)
	{
		const TSharedRef<FJsonObject> V = MergeOver(SplatDefaults(), Value);
		const FString Engine = JTextOr(V, TEXT("engine"));
		const bool bCutOut = JTextOr(V, TEXT("subject")) == TEXT("object") && Engine == SplatModel;
		TSharedRef<FJsonObject> Generation = MakeShared<FJsonObject>();
		Generation->SetStringField(TEXT("base"), SplatBase);
		Generation->SetArrayField(TEXT("modifiers"), {});
		TSharedRef<FJsonObject> Spec = MakeShared<FJsonObject>();
		Spec->SetObjectField(TEXT("generation"), Generation);
		TSharedRef<FJsonObject> B = MakeShared<FJsonObject>();
		B->SetObjectField(TEXT("spec"), Spec);
		JSet(B, TEXT("model"), JGet(V, TEXT("engine")));
		B->SetBoolField(TEXT("removeBackground"), bCutOut && JTruthy(JGet(V, TEXT("removeBackground"))));
		const FString Title = JTextOr(V, TEXT("title")).TrimStartAndEnd();
		if (!Title.IsEmpty()) B->SetStringField(TEXT("title"), Title);
		return B;
	}

	TOptional<int32> SplatPrice(const FZettaPriceList* Prices, const TSharedPtr<FJsonObject>& Value)
	{
		if (!Prices) return {};
		return Prices->Total(JTextOr(MergeOver(SplatDefaults(), Value), TEXT("engine")), { SplatBase });
	}
}

// ─── The Generate form ──────────────────────────────────────────────────────────────────────────────────────────────

namespace ZettaGenerateForm
{
	static constexpr int32 LowPolyCeiling = 15000;
	static constexpr int32 UltraFloor = 1000000;

	TSharedRef<FJsonObject> Defaults()
	{
		// DEFAULT_FORM, field for field.
		TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
		F->SetStringField(TEXT("quality"), TEXT("high-res"));
		F->SetStringField(TEXT("output"), TEXT("textured"));
		F->SetBoolField(TEXT("pbrMaterials"), true);
		F->SetStringField(TEXT("polygonCount"), TEXT("200000"));
		F->SetStringField(TEXT("polygonType"), TEXT("triangle"));
		F->SetStringField(TEXT("textureQuality"), TEXT("standard"));
		F->SetBoolField(TEXT("segmentParts"), false);
		F->SetBoolField(TEXT("realWorldScale"), false);
		F->SetStringField(TEXT("textureAlignment"), TEXT("geometry"));
		F->SetBoolField(TEXT("delight"), false);
		F->SetField(TEXT("availability"), JNull());
		F->SetBoolField(TEXT("autofixImage"), false);
		F->SetBoolField(TEXT("compressGeometry"), false);
		F->SetBoolField(TEXT("exportUv"), true);
		F->SetBoolField(TEXT("imageFromPrompt"), false);
		F->SetBoolField(TEXT("removeBackground"), true);
		F->SetBoolField(TEXT("controlMode"), true);
		F->SetStringField(TEXT("model"), TEXT("n25"));
		F->SetStringField(TEXT("imageUrl"), TEXT(""));
		F->SetStringField(TEXT("prompt"), TEXT(""));
		F->SetStringField(TEXT("title"), TEXT(""));
		return F;
	}

	static const TSharedRef<FJsonObject>& SharedDefaults()
	{
		static const TSharedRef<FJsonObject> D = Defaults();
		return D;
	}

	/** The web's MODEL_CAPABILITIES, for the form's rules. */
	struct FCaps
	{
		bool bLowPoly = false, bBareLowPoly = false, bQuadAnyBase = false, bExtremeTexture = false, bCustomFaceCount = true;
		TOptional<FIntPoint> FaceRange, QuadFaceRange;
		TOptional<TMap<FString, int32>> Ceiling, Floor, QuadCeiling, QuadFloor;
	};

	static FCaps CapsOf(const FString& Model)
	{
		FCaps C;
		if (Model == TEXT("n25"))
		{
			C.bLowPoly = C.bBareLowPoly = C.bQuadAnyBase = true;
			C.Ceiling = TMap<FString, int32>{ { TEXT("low-poly"), 20000 }, { TEXT("high-res"), 1000000 }, { TEXT("ultra"), 2000000 } };
			C.Floor = TMap<FString, int32>{ { TEXT("low-poly"), 1000 }, { TEXT("high-res"), 1000 }, { TEXT("ultra"), 1000 } };
			C.QuadCeiling = TMap<FString, int32>{ { TEXT("low-poly"), 10000 }, { TEXT("high-res"), 150000 }, { TEXT("ultra"), 150000 } };
			C.QuadFloor = TMap<FString, int32>{ { TEXT("low-poly"), 500 }, { TEXT("high-res"), 1000 }, { TEXT("ultra"), 1000 } };
		}
		else if (Model == TEXT("np1"))
		{
			C.bExtremeTexture = true;
			C.FaceRange = FIntPoint(48, 20000);
		}
		else if (Model == TEXT("np15"))
		{
			C.bExtremeTexture = C.bQuadAnyBase = true;
			C.FaceRange = FIntPoint(48, 50000);
			C.QuadFaceRange = FIntPoint(48, 25000);
		}
		else if (Model == TEXT("n1"))
		{
			C.bLowPoly = true;
		}
		else
		{
			// r1 / splat engines and anything unknown: no generation options at all.
			C.bCustomFaceCount = false;
		}
		return C;
	}

	/** A string field, else the default form's, else empty. */
	static FString JStr(const TSharedPtr<FJsonObject>& Form, const FString& Key)
	{
		const TOptional<FString> Own = JText(Form, Key);
		if (Own.IsSet()) return Own.GetValue();
		return JTextOr(SharedDefaults(), Key);
	}

	FString FieldText(const TSharedPtr<FJsonObject>& Form, const FString& Key)
	{
		const FVal V = JGet(Form, Key);
		if (JIsNull(V)) return JStr(nullptr, Key);
		return JAsText(V);
	}

	static bool Flag(const TSharedPtr<FJsonObject>& Form, const FString& Key)
	{
		const FVal V = JGet(Form, Key);
		if (JIsNull(V))
		{
			const FVal D = JGet(SharedDefaults(), Key);
			return JIsBool(D) && D->AsBool();
		}
		return JIsBool(V) ? V->AsBool() : JTruthy(V);
	}

	/** JavaScript's JNumber(raw) of a count field: unset when it is not a number at all; "" is 0. */
	static TOptional<double> JsNumber(const FString& Raw)
	{
		const FString T = Raw.TrimStartAndEnd();
		if (T.IsEmpty()) return 0.0;
		// A plain decimal, optionally signed, with an optional exponent — nothing else is a number.
		int32 I = 0;
		const int32 N = T.Len();
		if (I < N && (T[I] == TEXT('+') || T[I] == TEXT('-'))) ++I;
		int32 Digits = 0;
		while (I < N && FChar::IsDigit(T[I])) { ++I; ++Digits; }
		if (I < N && T[I] == TEXT('.'))
		{
			++I;
			while (I < N && FChar::IsDigit(T[I])) { ++I; ++Digits; }
		}
		if (Digits == 0) return {};
		if (I < N && (T[I] == TEXT('e') || T[I] == TEXT('E')))
		{
			++I;
			if (I < N && (T[I] == TEXT('+') || T[I] == TEXT('-'))) ++I;
			int32 Exp = 0;
			while (I < N && FChar::IsDigit(T[I])) { ++I; ++Exp; }
			if (Exp == 0) return {};
		}
		if (I != N) return {};
		return FCString::Atod(*T);
	}

	static bool IsInteger(const TOptional<double>& V)
	{
		return V.IsSet() && FMath::IsFinite(V.GetValue()) && FMath::FloorToDouble(V.GetValue()) == V.GetValue();
	}

	FInt32Range PolygonBounds(const TSharedPtr<FJsonObject>& Form)
	{
		const FCaps C = CapsOf(JStr(Form, TEXT("model")));
		const bool bQuads = JStr(Form, TEXT("polygonType")) == TEXT("quadrilateral");
		if (C.FaceRange.IsSet())
		{
			const FIntPoint R = bQuads && C.QuadFaceRange.IsSet() ? C.QuadFaceRange.GetValue() : C.FaceRange.GetValue();
			return FInt32Range(R.X, R.Y + 1);
		}
		const FString Quality = JStr(Form, TEXT("quality"));
		const TOptional<TMap<FString, int32>>& Ceilings = bQuads ? C.QuadCeiling : C.Ceiling;
		const TOptional<TMap<FString, int32>>& Floors = bQuads ? C.QuadFloor : C.Floor;
		const int32* Min = Floors.IsSet() ? Floors->Find(Quality) : nullptr;
		const int32* Max = Ceilings.IsSet() ? Ceilings->Find(Quality) : nullptr;
		return FInt32Range(Min ? *Min : PolygonMin, (Max ? *Max : PolygonMax) + 1);
	}

	static int32 QualityPolygons(const FString& Quality)
	{
		if (Quality == TEXT("low-poly")) return 9000;
		if (Quality == TEXT("ultra")) return 1000000;
		return 200000;
	}

	int32 DefaultPolygonCount(const TSharedPtr<FJsonObject>& Form)
	{
		const FCaps C = CapsOf(JStr(Form, TEXT("model")));
		if (C.FaceRange.IsSet() || C.Ceiling.IsSet()) return PolygonBounds(Form).GetUpperBoundValue() - 1;
		return QualityPolygons(JStr(Form, TEXT("quality")));
	}

	/** The rung a typed count implies, or empty. */
	static FString QualityForPolygons(const FString& Raw)
	{
		const TOptional<double> Parsed = JsNumber(Raw);
		if (Raw.TrimStartAndEnd().IsEmpty() || !IsInteger(Parsed)) return FString();
		const double V = Parsed.GetValue();
		if (V < PolygonMin || V > PolygonMax) return FString();
		if (V >= UltraFloor) return TEXT("ultra");
		if (V > LowPolyCeiling) return TEXT("high-res");
		return TEXT("low-poly");
	}

	bool HasCustomPolygonCount(const TSharedPtr<FJsonObject>& Form, bool bMayChoose)
	{
		if (!bMayChoose || !CapsOf(JStr(Form, TEXT("model"))).bCustomFaceCount) return false;
		const FString Raw = FieldText(Form, TEXT("polygonCount")).TrimStartAndEnd();
		if (Raw.IsEmpty()) return false;
		const TOptional<double> Parsed = JsNumber(Raw);
		return !(IsInteger(Parsed) && Parsed.GetValue() == DefaultPolygonCount(Form));
	}

	TArray<FString> ParameterProblems(const TSharedPtr<FJsonObject>& Form, bool bMayChoose)
	{
		TArray<FString> Problems;
		if (!HasCustomPolygonCount(Form, bMayChoose)) return Problems;
		const TOptional<double> Parsed = JsNumber(FieldText(Form, TEXT("polygonCount")));
		if (!IsInteger(Parsed)) Problems.Add(TEXT("The polygon count must be a whole number."));
		else
		{
			const FInt32Range B = PolygonBounds(Form);
			const int32 Min = B.GetLowerBoundValue(), Max = B.GetUpperBoundValue() - 1;
			if (Parsed.GetValue() < Min || Parsed.GetValue() > Max)
				Problems.Add(FString::Printf(TEXT("The polygon count must be between %s and %s."), *JCount(Min), *JCount(Max)));
		}
		return Problems;
	}

	TSharedRef<FJsonObject> ApplyPatch(const TSharedPtr<FJsonObject>& InCurrent, const TSharedPtr<FJsonObject>& InPatch)
	{
		const TSharedRef<FJsonObject> Current = InCurrent.IsValid() ? JClone(InCurrent) : Defaults();
		const TSharedRef<FJsonObject> Patch = JClone(InPatch);
		TSharedRef<FJsonObject> Next = JClone(Current);
		JMerge(Next, Patch);

		const FCaps C = CapsOf(JStr(Next, TEXT("model")));
		if (JStr(Next, TEXT("quality")) == TEXT("low-poly") && !C.bLowPoly) Next->SetStringField(TEXT("quality"), TEXT("high-res"));
		if (JStr(Next, TEXT("textureQuality")) == TEXT("extreme") && !C.bExtremeTexture) Next->SetStringField(TEXT("textureQuality"), TEXT("detailed"));
		if (JStr(Next, TEXT("polygonType")) == TEXT("quadrilateral") && !C.bQuadAnyBase && !C.bLowPoly) Next->SetStringField(TEXT("polygonType"), TEXT("triangle"));
		if (JStr(Next, TEXT("quality")) == TEXT("low-poly"))
		{
			if (!C.bBareLowPoly) Next->SetStringField(TEXT("output"), TEXT("textured"));
			Next->SetBoolField(TEXT("segmentParts"), false);
		}
		else if (Flag(Next, TEXT("segmentParts")))
		{
			Next->SetStringField(TEXT("output"), TEXT("geometry"));
			Next->SetStringField(TEXT("polygonType"), TEXT("triangle"));
		}

		auto Changed = [&Patch, &Current](const TCHAR* Key)
		{
			return JHas(Patch, Key) && !JEquals(JGet(Patch, Key), JGet(Current, Key));
		};
		auto DefaultCountText = [&Next]() { return FString::Printf(TEXT("%d"), DefaultPolygonCount(Next)); };

		if (Changed(TEXT("quality")))
		{
			Next->SetStringField(TEXT("polygonCount"), DefaultCountText());
			Next->SetBoolField(TEXT("polygonCountTyped"), false);
			return Next;
		}
		if (Changed(TEXT("polygonType")) && !Flag(Next, TEXT("polygonCountTyped")))
		{
			Next->SetStringField(TEXT("polygonCount"), DefaultCountText());
			return Next;
		}
		if (Changed(TEXT("model")))
		{
			const FInt32Range B = PolygonBounds(Next);
			const TOptional<double> N = JsNumber(FieldText(Next, TEXT("polygonCount")));
			if (!N.IsSet() || !FMath::IsFinite(N.GetValue()) || N.GetValue() < B.GetLowerBoundValue() || N.GetValue() > B.GetUpperBoundValue() - 1)
			{
				Next->SetStringField(TEXT("polygonCount"), DefaultCountText());
				return Next;
			}
		}
		if (JHas(Patch, TEXT("polygonCount")) && !C.Ceiling.IsSet())
		{
			const FString Implied = QualityForPolygons(FieldText(Patch, TEXT("polygonCount")));
			if (!Implied.IsEmpty()) Next->SetStringField(TEXT("quality"), Implied);
		}
		return Next;
	}

	FZettaGenerateOptions ToOptions(const TSharedPtr<FJsonObject>& Form, bool bMayChooseFaceCount)
	{
		const FString Model = JStr(Form, TEXT("model"));
		const FString Raw = FieldText(Form, TEXT("polygonCount")).TrimStartAndEnd();
		const TOptional<double> Parsed = JsNumber(Raw);
		FZettaGenerateOptions O;
		// The web sends the typed count whenever it is a whole number and the plan and engine allow a count.
		if (!Raw.IsEmpty() && IsInteger(Parsed) && bMayChooseFaceCount && CapsOf(Model).bCustomFaceCount
			&& Parsed.GetValue() <= (double)MAX_int32 && Parsed.GetValue() >= (double)MIN_int32)
		{
			O.FaceCount = (int32)Parsed.GetValue();
		}
		O.Prompt = FieldText(Form, TEXT("prompt"));
		O.ImageUrl = FieldText(Form, TEXT("imageUrl"));
		O.bImageFromPrompt = Flag(Form, TEXT("imageFromPrompt"));
		O.Model = Model;
		O.Quality = JStr(Form, TEXT("quality"));
		O.Output = JStr(Form, TEXT("output"));
		O.bPbr = Flag(Form, TEXT("pbrMaterials"));
		O.TextureQuality = JStr(Form, TEXT("textureQuality"));
		O.PolygonType = JStr(Form, TEXT("polygonType"));
		O.Title = FieldText(Form, TEXT("title"));
		O.bSegmentParts = Flag(Form, TEXT("segmentParts"));
		O.bRealWorldScale = Flag(Form, TEXT("realWorldScale"));
		O.bDelight = Flag(Form, TEXT("delight"));
		O.bAutofixImage = Flag(Form, TEXT("autofixImage"));
		O.bCompressGeometry = Flag(Form, TEXT("compressGeometry"));
		O.TextureAlignment = JStr(Form, TEXT("textureAlignment"));
		O.bExportUv = Flag(Form, TEXT("exportUv"));
		O.bControlMode = Flag(Form, TEXT("controlMode"));
		O.Availability = JText(Form, TEXT("availability")).Get(FString());
		return O;
	}

	TOptional<FString> SentPolygonType(const TSharedPtr<FJsonObject>& Form, const FString& Base, const TArray<FString>& Modifiers)
	{
		const FCaps C = CapsOf(JStr(Form, TEXT("model")));
		const bool bParts = Modifiers.Contains(TEXT("generate_parts"));
		const FString Type = JStr(Form, TEXT("polygonType"));
		if ((Base == TEXT("lowpoly") || C.bQuadAnyBase) && !(bParts && Type == TEXT("quadrilateral"))) return Type;
		return {};
	}

	FString ModelLabel(const FString& Model) { return ZettaCatalog::ModelLabel(Model); }

	TArray<FString> ModelsFor(const FString& Kind)
	{
		if (ZettaNodeCatalog::IsSmartGenerate(Kind)) return { TEXT("np1"), TEXT("np15") };
		// Zetta N1 was retired on 2026-11-18 (as on zetta3d.ai): never offered or sent; old library rows still name it.
		return { TEXT("n25") };
	}

	FString ModelOf(const TSharedPtr<FJsonObject>& Form) { return JStr(Form, TEXT("model")); }
}

// ─── Node settings ──────────────────────────────────────────────────────────────────────────────────────────────────

namespace ZettaNodeSettings
{
	TSharedRef<FJsonObject> Defaults(const FString& Kind)
	{
		using namespace ZettaNodeKinds;
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		if (Kind == SeedPicture) { O->SetField(TEXT("source"), JNull()); return O; }
		if (Kind == SeedModel || Kind == SeedViews) return O;
		if (Kind == AiEdit)
		{
			O->SetStringField(TEXT("prompt"), TEXT(""));
			O->SetField(TEXT("model"), JNull());
			O->SetBoolField(TEXT("for3D"), true);
			return O;
		}
		if (Kind == DrawViews) { O->SetField(TEXT("views"), JStrings({ TEXT("back"), TEXT("left"), TEXT("right") })); return O; }
		if (Kind == IsolateParts) return ZettaToolSettings::IsolateDefaults();
		if (Kind == Generate3D || Kind == Generate3DMultiview)
		{
			// Spread, not applyFormPatch — so the count stays the default form's 200,000 (on N2.5 that reads as a
			// custom count, exactly as on the web).
			TSharedRef<FJsonObject> Form = ZettaGenerateForm::Defaults();
			Form->SetStringField(TEXT("model"), TEXT("n25"));
			return Form;
		}
		if (Kind == GenerateSmart3D || Kind == GenerateSmart3DMultiview)
		{
			TSharedRef<FJsonObject> Patch = MakeShared<FJsonObject>();
			Patch->SetStringField(TEXT("model"), TEXT("np1"));
			return ZettaGenerateForm::ApplyPatch(ZettaGenerateForm::Defaults(), Patch);
		}
		if (Kind == GenerateSplat)
		{
			TSharedRef<FJsonObject> Splat = ZettaToolSettings::SplatDefaults();
			Splat->SetStringField(TEXT("title"), TEXT(""));
			return Splat;
		}
		if (Kind == Retopology) return ZettaToolSettings::RetopologyDefaults();
		if (Kind == Segmentation) return ZettaToolSettings::SegmentDefaults();
		if (Kind == SmartSegmentation)
		{
			TSharedRef<FJsonObject> Smart = ZettaToolSettings::SegmentDefaults();
			Smart->SetStringField(TEXT("mode"), TEXT("smart"));
			return Smart;
		}
		if (Kind == Texture)
		{
			TSharedRef<FJsonObject> T = ZettaToolSettings::TextureDefaults();
			T->SetStringField(TEXT("prompt"), TEXT(""));
			return T;
		}
		if (Kind == TextureMultiview)
		{
			TSharedRef<FJsonObject> T = ZettaToolSettings::TextureDefaults();
			T->SetStringField(TEXT("prompt"), TEXT(""));
			T->SetObjectField(TEXT("referenceViews"), MakeShared<FJsonObject>());
			return T;
		}
		if (Kind == SmartUv) return ZettaToolSettings::SmartUvDefaults();
		if (Kind == Rigging) return ZettaToolSettings::AutoRigDefaults();
		if (Kind == Export) { O->SetStringField(TEXT("format"), TEXT("glb")); return O; }
		return O;
	}

	TSharedRef<FJsonObject> WithDefaults(const FString& Kind, const TSharedPtr<FJsonObject>& Settings)
	{
		TSharedRef<FJsonObject> D = Defaults(Kind);
		if (!Settings.IsValid()) return D;
		if (ZettaNodeCatalog::IsGenerate(Kind)) return ZettaGenerateForm::ApplyPatch(D, Settings);
		JMerge(D, Settings);
		return D;
	}

	TSharedRef<FJsonObject> Of(const FZettaPipelineNode& Node)
	{
		return Node.Settings.IsValid() ? Node.Settings.ToSharedRef() : Defaults(Node.Kind);
	}

	bool SetOnEachRun(const FZettaPipelineNode& Node)
	{
		if (Node.Kind == ZettaNodeKinds::Texture || Node.Kind == ZettaNodeKinds::TextureMultiview) return true;
		const FVal Ask = JGet(Node.Settings, TEXT("askAtRun"));
		return Node.Kind == ZettaNodeKinds::AiEdit && JIsBool(Ask) && Ask->AsBool();
	}

	bool ViewsWired(const FZettaPipelineGraph& Graph, const FString& NodeId)
	{
		for (const FZettaPipelineEdge& E : Graph.Edges) if (E.Target == NodeId && E.TargetHandle == TEXT("views")) return true;
		return false;
	}

	bool PerRunIn(const FZettaPipelineGraph& Graph, const FZettaPipelineNode& Node)
	{
		return !(Node.Kind == ZettaNodeKinds::TextureMultiview && ViewsWired(Graph, Node.Id)) && SetOnEachRun(Node);
	}

	static FString Cut(const FString& Text, int32 Length) { return Text.Len() <= Length ? Text : Text.Left(Length); }

	static FString Capitalise(const FString& S) { return S.IsEmpty() ? S : S.Left(1).ToUpper() + S.Mid(1); }

	FString Summary(const FZettaPipelineNode& Node)
	{
		using namespace ZettaNodeKinds;
		const TSharedRef<FJsonObject> S = Of(Node);
		auto T = [&S](const TCHAR* Key) { return JTextOr(S, Key); };
		const FString& K = Node.Kind;
		if (K == SeedPicture) return TEXT("A picture, chosen for each run");
		if (K == SeedModel) return TEXT("A model, chosen for each run");
		if (K == SeedViews) return TEXT("Pictures of each side, chosen for each run");
		if (K == RemoveBackground) return TEXT("Cut-out");
		if (K == AiEdit)
		{
			const FVal Ask = JGet(S, TEXT("askAtRun"));
			if (JIsBool(Ask) && Ask->AsBool() && T(TEXT("prompt")).IsEmpty()) return TEXT("Set on each run");
			const FString Prompt = T(TEXT("prompt"));
			return Prompt.IsEmpty() ? FString(TEXT("Say how to change it")) : Cut(Prompt, 48);
		}
		if (K == DrawViews)
		{
			const FVal Views = JGet(S, TEXT("views"));
			const TArray<FString> List = JStringsOf(Views);
			return JIsArray(Views) && List.Num() > 0 ? TEXT("Front + ") + FString::Join(List, TEXT(", ")) : FString(TEXT("No views chosen"));
		}
		if (K == IsolateParts)
		{
			const TSharedRef<FJsonObject> V = WithDefaults(Node.Kind, Node.Settings);
			const FVal Identify = JGet(V, TEXT("identify"));
			const FString Sensitivity = JHas(V, TEXT("sensitivity")) ? JAsText(JGet(V, TEXT("sensitivity"))) : FString(TEXT("low"));
			const FString How = JIsBool(Identify) && !Identify->AsBool() ? FString(TEXT("Your list")) : Capitalise(Sensitivity);
			const FString Pose = JAsText(JGet(V, TEXT("pose"))) == TEXT("a") ? TEXT("A") : TEXT("T");
			return FString::Printf(TEXT("%s · up to %s · %s-pose"), *How, *JAsText(JGet(V, TEXT("maxParts"))), *Pose);
		}
		if (ZettaNodeCatalog::IsGenerate(K))
		{
			return FString::Printf(TEXT("%s · %s"), *ZettaGenerateForm::ModelLabel(T(TEXT("model"))),
				T(TEXT("output")) == TEXT("geometry") ? TEXT("Geometry") : TEXT("Textured"));
		}
		if (K == GenerateSplat)
		{
			const FString Engine = T(TEXT("engine"));
			const FString Label = Engine == TEXT("n25") ? TEXT("Zetta Splat") : Engine == TEXT("zs2") ? TEXT("Zetta Splat 2") : Engine == TEXT("zs21") ? TEXT("Zetta Splat 2.1") : Engine;
			return FString::Printf(TEXT("%s · %s"), *Label, T(TEXT("subject")) == TEXT("environment") ? TEXT("Environment") : TEXT("Object"));
		}
		if (K == Retopology)
		{
			const FString Engine = T(TEXT("engine"));
			const FString Label = ZettaToolSettings::LabelOf(ZettaToolSettings::RetopologyEngines(), Engine);
			if (Engine == TEXT("r1"))
			{
				const FVal Faces = JGet(S, TEXT("faces"));
				const FString Count = JIsNumber(Faces) ? ZettaPJson::JCount((int64)Faces->AsNumber()) : JAsText(Faces);
				return FString::Printf(TEXT("%s · %s faces"), *Label, *Count);
			}
			return FString::Printf(TEXT("%s · %s"), *Label, *T(TEXT("faceLevel")));
		}
		if (K == Segmentation || K == SmartSegmentation) return T(TEXT("mode")) == TEXT("smart") ? TEXT("Smart") : TEXT("AI segmentation");
		if (K == Texture)
		{
			const FString Tier = ZettaToolSettings::TextureTierLabel(T(TEXT("tier")));
			if (JTruthy(JGet(S, TEXT("reference")))) return Tier + TEXT(" · from a picture");
			const FString Prompt = T(TEXT("prompt")).TrimStartAndEnd();
			return Prompt.IsEmpty() ? Tier + TEXT(" · set on each run") : FString::Printf(TEXT("%s · %s"), *Tier, *Cut(Prompt, 32));
		}
		if (K == TextureMultiview)
		{
			const FString Tier = ZettaToolSettings::TextureTierLabel(T(TEXT("tier")));
			const int32 Given = ViewsRequest(JObject(S, TEXT("referenceViews"))).Num();
			return Given > 0 ? FString::Printf(TEXT("%s · %d view%s"), *Tier, Given, Given == 1 ? TEXT("") : TEXT("s")) : Tier + TEXT(" · pictures of each side");
		}
		if (K == SmartUv) return JTruthy(JGet(S, TEXT("bake"))) ? TEXT("Textures re-baked") : TEXT("Layout only");
		if (K == Rigging) return JAsText(JGet(S, TEXT("rigType"))) + TEXT(" rig");
		if (K == Export)
		{
			const FString Format = T(TEXT("format")).ToUpper();
			return Format.IsEmpty() ? FString(TEXT("GLB")) : Format;
		}
		return FString();
	}

	const TArray<FString>& ViewKeys()
	{
		static const TArray<FString> Keys = { TEXT("front"), TEXT("back"), TEXT("left"), TEXT("right") };
		return Keys;
	}

	TArray<TSharedPtr<FJsonValue>> ViewsRequest(const TSharedPtr<FJsonObject>& Set)
	{
		TArray<TSharedPtr<FJsonValue>> List;
		if (!Set.IsValid()) return List;
		for (const FString& Key : ViewKeys())
		{
			const FObj View = JObject(Set, Key);
			if (!View.IsValid()) continue;
			const TOptional<FString> Upload = JText(View, TEXT("uploadKey"));
			const FVal Post = JIsNumber(JGet(View, TEXT("gen2dPostId"))) ? JGet(View, TEXT("gen2dPostId")) : nullptr;
			if (!Upload.IsSet() && !Post.IsValid()) continue;
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("viewType"), Key);
			if (Upload.IsSet()) Entry->SetStringField(TEXT("uploadKey"), Upload.GetValue());
			else ZettaPJson::JSet(Entry, TEXT("gen2dPostId"), Post);
			const FString Name = JTextOr(View, TEXT("name"));
			if (!Name.IsEmpty()) Entry->SetStringField(TEXT("name"), Name);
			List.Add(MakeShared<FJsonValueObject>(Entry));
		}
		return List;
	}
}
