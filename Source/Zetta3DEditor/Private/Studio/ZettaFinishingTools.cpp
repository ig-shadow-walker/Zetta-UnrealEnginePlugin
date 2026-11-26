// Copyright Zetta 3D. See LICENSE.md.

// Segment, Retopology, Smart UV and Auto Rig. Texture has its own file (it has the most to it).

#include "SZettaPostProcessTool.h"

#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "ZettaApi.h"
#include "ZettaCatalog.h"
#include "ZettaImporter.h"
#include "ZettaStudioAssets.h"
#include "ZettaStudioContext.h"
#include "ZettaStudioUI.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace
{
	TSharedRef<SWidget> FinishRows(std::initializer_list<TSharedRef<SWidget>> Widgets)
	{
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		bool bFirst = true;
		for (const TSharedRef<SWidget>& W : Widgets)
		{
			Box->AddSlot().AutoHeight().Padding(0, bFirst ? 0.f : 6.f, 0, 0)[W];
			bFirst = false;
		}
		return Box;
	}
}

// ─── Segment ──────────────────────────────────────────────────────────────────

FText SZettaSegmentTool::Intro() const
{
	return LOCTEXT("SegmentIntro", "Split a model into separate, named parts — each one imports as its own object.");
}

FString SZettaSegmentTool::Code() const
{
	return Mode == TEXT("smart") ? TEXT("smart_segmentation") : TEXT("ai_segmentation");
}

FText SZettaSegmentTool::ButtonText() const
{
	return Mode == TEXT("smart") ? LOCTEXT("SmartSegment", "Smart Segment") : LOCTEXT("Segment", "Segment");
}

TSharedRef<SWidget> SZettaSegmentTool::BuildOptions()
{
	return FinishRows({
		ZettaStudioUI::Field(LOCTEXT("SegmentationType", "Segmentation type"), ZettaStudioUI::Segmented([this]() { return Mode; }, [this](const FString& V) { Mode = V; Refresh(); },
			{ { TEXT("segment"), LOCTEXT("Segment", "Segment"), FText() }, { TEXT("smart"), LOCTEXT("SmartSegment", "Smart Segment"), FText() } })),
		ZettaStudioUI::Hint(MakeAttributeLambda([this]()
		{
			return Mode == TEXT("smart")
				? LOCTEXT("SmartSegmentHint", "Rebuilds as it splits, for cleaner parts on a rough or incomplete mesh.")
				: LOCTEXT("SegmentHint", "Splits the model you have into named parts.");
		})),
		ZettaStudioUI::Field(LOCTEXT("HowManyParts", "How many parts"), ZettaStudioUI::Segmented([this]() { return Granularity; }, [this](const FString& V) { Granularity = V; Refresh(); },
			{ { TEXT("simple"), LOCTEXT("FewParts", "Few parts"), FText() }, { TEXT("balanced"), LOCTEXT("Balanced", "Balanced"), FText() },
			  { TEXT("detailed"), LOCTEXT("MaxParts", "Max parts"), FText() } })),
		ZettaStudioUI::ShownWhen([this]() { return Mode == TEXT("segment"); },
			ZettaStudioUI::Check(LOCTEXT("SplitConnectivity", "Also split pieces that are not joined"), [this]() { return bSplitConnectivity; }, [this](bool b) { bSplitConnectivity = b; },
				LOCTEXT("SplitConnectivityTip", "Pieces that do not touch each other become separate parts."))),
		ZettaStudioUI::ShownWhen([this]() { return Mode == TEXT("smart"); },
			FinishRows({
				ZettaStudioUI::Field(LOCTEXT("WhatIsThis", "What is this? (Optional)"),
					SNew(SBox).MinDesiredHeight(44)
					[
						SNew(SMultiLineEditableTextBox)
						.AutoWrapText(true)
						.HintText(LOCTEXT("WhatIsThisHint", "e.g. game character with sword and armor"))
						.Text_Lambda([this]() { return FText::FromString(PromptText); })
						.OnTextChanged_Lambda([this](const FText& T) { PromptText = T.ToString().Left(1024); })
					]),
				ZettaStudioUI::Hint(LOCTEXT("WhatIsThisTip", "Naming the parts helps it find them.")),
			})),
	});
}

void SZettaSegmentTool::MakeOptions(FOptionsDone Done)
{
	TSharedRef<FJsonObject> Options = MakeShared<FJsonObject>();
	Options->SetStringField(TEXT("segmentGranularity"), Granularity);
	if (Mode == TEXT("segment"))
	{
		if (!bSplitConnectivity) Options->SetBoolField(TEXT("splitByConnectivity"), false);
	}
	else if (!PromptText.TrimStartAndEnd().IsEmpty())
	{
		Options->SetStringField(TEXT("prompt"), PromptText.TrimStartAndEnd());
	}
	Done(FString(), Options);
}

// ─── Retopology ───────────────────────────────────────────────────────────────

namespace
{
	constexpr int32 RetopoMin = 500;
	constexpr int32 RetopoMaxTriangles = 20000;
	constexpr int32 RetopoMaxQuads = 10000;
}

FText SZettaRetopologyTool::Intro() const
{
	return LOCTEXT("RetopoIntro", "Rebuild the mesh with clean, even topology — lighter for games, easier to rig and UV.");
}

FText SZettaRetopologyTool::ButtonText() const { return LOCTEXT("Retopologize", "Retopologize"); }

FString SZettaRetopologyTool::Refuse(const FZettaGeneration& Row) const
{
	if (Row.Feature == TEXT("retopology")) return TEXT("This model is already a retopology result.");
	return SZettaPostProcessTool::Refuse(Row);
}

int32 SZettaRetopologyTool::Max() const { return PolygonType == TEXT("quadrilateral") ? RetopoMaxQuads : RetopoMaxTriangles; }

TOptional<int32> SZettaRetopologyTool::Count() const
{
	const FString T = CountText.TrimStartAndEnd();
	if (T.IsEmpty() || T.Len() > 10) return {};
	for (int32 i = 0; i < T.Len(); ++i) if (!FChar::IsDigit(T[i]) && !(i == 0 && T[i] == TEXT('-'))) return {};
	return FCString::Atoi(*T);
}

bool SZettaRetopologyTool::OutOfRange() const
{
	// An empty field is not zero: it is out of range too. Only R1 takes a count.
	const TOptional<int32> N = Count();
	return Engine == TEXT("r1") && (!N.IsSet() || N.GetValue() < RetopoMin || N.GetValue() > Max());
}

bool SZettaRetopologyTool::QuadsLocked() const
{
	const FZettaEntitlements3D* G = FZettaStudioContext::Get().G3();
	return G && FZettaStudioContext::No(G->AllowsQuadMesh);
}

void SZettaRetopologyTool::OnContextChanged()
{
	if (QuadsLocked() && PolygonType == TEXT("quadrilateral")) PolygonType = TEXT("triangle");
	SZettaPostProcessTool::OnContextChanged();
}

TSharedRef<SWidget> SZettaRetopologyTool::BuildOptions()
{
	return FinishRows({
		ZettaStudioUI::Field(LOCTEXT("Engine", "Engine"), ZettaStudioUI::Segmented([this]() { return Engine; }, [this](const FString& V) { Engine = V; Refresh(); },
			{ { TEXT("r1"), LOCTEXT("ZettaR1", "Zetta R1"), FText() }, { TEXT("n1"), LOCTEXT("ZettaN1", "Zetta N1"), FText() } })),
		ZettaStudioUI::Hint(MakeAttributeLambda([this]()
		{
			return Engine == TEXT("r1")
				? LOCTEXT("R1Hint", "Up to 20,000 polygons. Faster, and less than half the price.")
				: LOCTEXT("N1Hint", "No polygon ceiling — the option for a dense mesh.");
		})),
		ZettaStudioUI::Field(LOCTEXT("Polygons", "Polygons"), ZettaStudioUI::Segmented([this]() { return PolygonType; }, [this](const FString& V)
		{
			// The number does not jump: a count over the new ceiling stays as typed and is reported out of range.
			PolygonType = V;
			Refresh();
		}, { { TEXT("triangle"), LOCTEXT("Triangles", "Triangles"), FText() }, { TEXT("quadrilateral"), LOCTEXT("Quads", "Quads"), FText() } },
		[this](const FString& K) { return K == TEXT("quadrilateral") && QuadsLocked() ? FString(TEXT("Quad meshes are available on paid plans.")) : FString(); })),
		ZettaStudioUI::Locked(MakeAttributeLambda([this]() { return QuadsLocked() ? LOCTEXT("QuadLocked", "Quad meshes are available on paid plans.") : FText(); })),
		ZettaStudioUI::ShownWhen([this]() { return Engine == TEXT("r1"); },
			FinishRows({
				ZettaStudioUI::Field(MakeAttributeLambda([this]() { return FText::FromString(FString::Printf(TEXT("Polygon count (%s–%s)"), *ZettaStudioUI::Number(RetopoMin), *ZettaStudioUI::Number(Max()))); }),
					SNew(SEditableTextBox)
					.HintText(LOCTEXT("PolygonCount", "Polygon count"))
					.Text_Lambda([this]() { return FText::FromString(CountText); })
					.OnTextChanged_Lambda([this](const FText& T)
					{
						CountText = T.ToString();
						Refresh();
					})),
				ZettaStudioUI::Error(MakeAttributeLambda([this]()
				{
					if (!OutOfRange()) return FText();
					return FText::FromString(FString::Printf(TEXT("Out of range — %s to %s%s."), *ZettaStudioUI::Number(RetopoMin), *ZettaStudioUI::Number(Max()),
						PolygonType == TEXT("quadrilateral") ? TEXT(", as quads allow half as many") : TEXT("")));
				})),
				ZettaStudioUI::Hint(MakeAttributeLambda([this]()
				{
					return PolygonType == TEXT("quadrilateral")
						? LOCTEXT("QuadCountHint", "Quads halve the ceiling. Zetta R1 aims for your count, then settles on the cleanest topology near it.")
						: LOCTEXT("CountHintR1", "Zetta R1 aims for this count, then settles on the cleanest topology near it.");
				})),
				ZettaStudioUI::Check(LOCTEXT("BakeRetopo", "Bake textures onto the rebuilt mesh"), [this]() { return bBake; }, [this](bool b) { bBake = b; },
					LOCTEXT("BakeRetopoTip", "Carries the original colours over to the new mesh. Off returns bare geometry.")),
			})),
		ZettaStudioUI::ShownWhen([this]() { return Engine == TEXT("n1"); },
			FinishRows({
				ZettaStudioUI::Field(LOCTEXT("LevelOfDetail", "Level of detail"), ZettaStudioUI::Segmented([this]() { return Level; }, [this](const FString& V) { Level = V; },
					{ { TEXT("low"), LOCTEXT("Low", "Low"), FText() }, { TEXT("medium"), LOCTEXT("Medium", "Medium"), FText() }, { TEXT("high"), LOCTEXT("High", "High"), FText() } })),
				ZettaStudioUI::Hint(MakeAttributeLambda([this]()
				{
					return Level == TEXT("low") ? LOCTEXT("LevelLow", "Fewest polygons — game-ready.")
						: Level == TEXT("high") ? LOCTEXT("LevelHigh", "Keeps the most detail.")
						: LOCTEXT("LevelMedium", "A balance of the two.");
				})),
			})),
	});
}

FString SZettaRetopologyTool::Problem() const
{
	if (OutOfRange()) return FString::Printf(TEXT("Choose a polygon count between %s and %s."), *ZettaStudioUI::Number(RetopoMin), *ZettaStudioUI::Number(Max()));
	return FString();
}

void SZettaRetopologyTool::MakeOptions(FOptionsDone Done)
{
	TSharedRef<FJsonObject> Options = MakeShared<FJsonObject>();
	Options->SetStringField(TEXT("model"), Engine);
	Options->SetStringField(TEXT("polygonType"), PolygonType);
	if (Engine == TEXT("r1"))
	{
		if (OutOfRange())
		{
			Done(Problem(), nullptr);
			return;
		}
		Options->SetNumberField(TEXT("faceCount"), Count().GetValue());
		if (!bBake) Options->SetBoolField(TEXT("bake"), false);
	}
	else Options->SetStringField(TEXT("faceLevel"), Level);
	Done(FString(), Options);
}

// ─── Smart UV ─────────────────────────────────────────────────────────────────

FText SZettaSmartUvTool::Intro() const
{
	return LOCTEXT("SmartUvIntro", "Lays the model's UVs out again as a clean new layout. Its textures, rig and animation are carried across onto it.");
}

FText SZettaSmartUvTool::ButtonText() const { return LOCTEXT("SmartUv", "Smart UV"); }

TOptional<int64> SZettaSmartUvTool::Triangles(const FZettaGenerationPtr& Row)
{
	if (!Row.IsValid() || !Row->FaceCount.IsSet() || Row->FaceCount.GetValue() <= 0) return {};
	return Row->PolygonType == TEXT("quadrilateral") ? Row->FaceCount.GetValue() * 2 : Row->FaceCount.GetValue();
}

FString SZettaSmartUvTool::Refuse(const FZettaGeneration& Row) const
{
	if (Row.Feature == TEXT("uvUnwrap")) return TEXT("This model already has a Smart UV layout.");
	return SZettaPostProcessTool::Refuse(Row);
}

TSharedRef<SWidget> SZettaSmartUvTool::BuildOptions()
{
	return FinishRows({
		ZettaStudioUI::Hint(MakeAttributeLambda([this]()
		{
			const TOptional<int64> N = Triangles(Source());
			if (!N.IsSet()) return FText::FromString(FString::Printf(TEXT("Takes models up to %s triangles."), *ZettaStudioUI::Number(ZettaCatalog::SmartUvMaxTriangles)));
			if (N.GetValue() > ZettaCatalog::SmartUvMaxTriangles) return FText();
			return FText::FromString(FString::Printf(TEXT("%s / %s triangles"), *ZettaStudioUI::Number(N.GetValue()), *ZettaStudioUI::Number(ZettaCatalog::SmartUvMaxTriangles)));
		})),
		ZettaStudioUI::Field(LOCTEXT("Orientation", "Orientation"), ZettaStudioUI::Segmented([this]() { return Orientation; }, [this](const FString& V) { Orientation = V; },
			{ { TEXT("as-is"), LOCTEXT("AsIs", "As is"), FText() }, { TEXT("+x"), FText::FromString(TEXT("+X")), FText() }, { TEXT("-x"), FText::FromString(TEXT("−X")), FText() },
			  { TEXT("+y"), FText::FromString(TEXT("+Y")), FText() }, { TEXT("-y"), FText::FromString(TEXT("−Y")), FText() } })),
		ZettaStudioUI::Hint(LOCTEXT("OrientationHint", "Which way the model faces in the exported file.")),
		ZettaStudioUI::Check(LOCTEXT("BakeTextures", "Bake textures"), [this]() { return bBake; }, [this](bool b) { bBake = b; }),
		ZettaStudioUI::Hint(MakeAttributeLambda([this]()
		{
			return bBake ? LOCTEXT("BakeOn", "Keeps the textures, baked onto the new layout.") : LOCTEXT("BakeOff", "Returns the geometry with its new UVs only — no textures.");
		})),
		ZettaStudioUI::Check(LOCTEXT("CompressGeometry", "Compress geometry"), [this]() { return bCompress; }, [this](bool b) { bCompress = b; }),
		ZettaStudioUI::Hint(LOCTEXT("CompressHint", "A smaller file, through mesh compression.")),
		ZettaStudioUI::Hint(LOCTEXT("UvLayoutHint", "The result comes with a picture of its UV layout, saved beside the model when it is imported.")),
	});
}

FString SZettaSmartUvTool::Problem() const
{
	const TOptional<int64> N = Triangles(Source());
	if (N.IsSet() && N.GetValue() > ZettaCatalog::SmartUvMaxTriangles)
	{
		return FString::Printf(TEXT("Smart UV takes models up to %s triangles — this one has %s. Run Retopology first to bring it down."),
			*ZettaStudioUI::Number(ZettaCatalog::SmartUvMaxTriangles), *ZettaStudioUI::Number(N.GetValue()));
	}
	return FString();
}

void SZettaSmartUvTool::MakeOptions(FOptionsDone Done)
{
	const FString Why = Problem();
	if (!Why.IsEmpty())
	{
		Done(Why, nullptr);
		return;
	}
	// Defaults travel as silence: only an untick, a turn or a tick is sent.
	TSharedRef<FJsonObject> Options = MakeShared<FJsonObject>();
	if (!bBake) Options->SetBoolField(TEXT("bake"), false);
	if (Orientation != TEXT("as-is")) Options->SetStringField(TEXT("exportOrientation"), Orientation);
	if (bCompress) Options->SetBoolField(TEXT("compressGeometry"), true);
	Done(FString(), Options);
}

// ─── Auto Rig ─────────────────────────────────────────────────────────────────

namespace
{
	FString RigBodyHint(const FString& Body)
	{
		if (Body == TEXT("biped")) return TEXT("Two legs, two arms. Character presets.");
		if (Body == TEXT("quadruped")) return TEXT("Dogs, horses, most mammals.");
		if (Body == TEXT("avian")) return TEXT("Wings and a tail.");
		if (Body == TEXT("serpentine")) return TEXT("A long spine, no limbs.");
		if (Body == TEXT("aquatic")) return TEXT("Fins and a tail.");
		if (Body == TEXT("hexapod")) return TEXT("Insects.");
		if (Body == TEXT("octopod")) return TEXT("Spiders and octopods.");
		return TEXT("A body none of these describe.");
	}

	TArray<ZettaStudioUI::FOption> RigOptions(const TArray<ZettaCatalog::FChoice>& Choices)
	{
		TArray<ZettaStudioUI::FOption> Out;
		for (const ZettaCatalog::FChoice& C : Choices) Out.Add({ C.Key, FText::FromString(C.Label), FText::FromString(C.Hint) });
		return Out;
	}

	FString RigChoiceLabel(const TArray<ZettaCatalog::FChoice>& Choices, const FString& Key)
	{
		for (const ZettaCatalog::FChoice& C : Choices) if (C.Key == Key) return C.Label;
		return Key;
	}
}

FText SZettaAutoRigTool::Intro() const
{
	return LOCTEXT("RigIntro", "Add a skeleton and skin weights so the model is ready to animate.");
}

FText SZettaAutoRigTool::ButtonText() const { return LOCTEXT("AutoRig", "Auto Rig"); }

TSharedRef<SWidget> SZettaAutoRigTool::BuildOptions()
{
	return FinishRows({
		ZettaStudioUI::Field(LOCTEXT("Body", "Body"),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				ZettaStudioUI::Dropdown([this]() { return Body; }, [this](const FString& V) { Body = V; Note.Reset(); Refresh(); },
					[]() { return RigOptions(ZettaCatalog::RigTypes()); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0).VAlign(VAlign_Center)
			[
				ZettaStudioUI::SmallButton(MakeAttributeLambda([this]() { return bDetecting ? LOCTEXT("Detecting", "Detecting…") : LOCTEXT("Detect", "Detect"); }),
					[this]() { Detect(); }, MakeAttributeLambda([this]() { return !bDetecting && Source().IsValid(); }),
					LOCTEXT("DetectTip", "Looks at the model and picks the body type for you."))
			]),
		ZettaStudioUI::Hint(MakeAttributeLambda([this]() { return FText::FromString(RigBodyHint(Body)); })),
		ZettaStudioUI::Hint(MakeAttributeLambda([this]() { return FText::FromString(Note); })),
		ZettaStudioUI::Field(LOCTEXT("BoneNaming", "Bone naming"),
			SNew(SBox).IsEnabled_Lambda([this]() { return Body == ZettaCatalog::HumanoidRigType; })
			[
				ZettaStudioUI::Dropdown([this]() { return Spec; }, [this](const FString& V) { Spec = V; }, []() { return RigOptions(ZettaCatalog::RigSpecs()); })
			]),
		ZettaStudioUI::Hint(MakeAttributeLambda([this]()
		{
			if (Body != ZettaCatalog::HumanoidRigType) return LOCTEXT("SpecHumanoidOnly", "Skeleton presets apply to humanoids.");
			for (const ZettaCatalog::FChoice& C : ZettaCatalog::RigSpecs()) if (C.Key == Spec) return FText::FromString(C.Hint);
			return FText();
		})),
		ZettaStudioUI::Field(LOCTEXT("RiggedFile", "Rigged file"), ZettaStudioUI::Segmented([this]() { return Format; }, [this](const FString& V) { Format = V; },
			{ { TEXT("fbx"), FText::FromString(TEXT("FBX")), FText() }, { TEXT("glb"), FText::FromString(TEXT("GLB")), FText() } })),
		ZettaStudioUI::Hint(MakeAttributeLambda([this]()
		{
			return Format == TEXT("fbx")
				? LOCTEXT("FbxHint", "FBX imports as a Skeletal Mesh with its skeleton — Unreal's most dependable route for animation.")
				: LOCTEXT("GlbHint", "GLB imports through Unreal's glTF importer, skeleton included. Choose FBX if you plan to retarget animations.");
		})),
	});
}

FString SZettaAutoRigTool::Problem() const
{
	return bDetecting ? FString(TEXT("Detecting the body type…")) : FString();
}

void SZettaAutoRigTool::Detect()
{
	const FZettaGenerationPtr From = Source();
	if (!From.IsValid() || bDetecting) return;
	bDetecting = true;
	Note.Reset();
	Refresh();

	// The imported copy, seen the way the editor draws its thumbnail; without one, the library's own picture of it.
	TArray<uint8> Jpeg;
	const FString Main = ZettaProjectIndex::MainAssetOf(From->Id);
	if (!Main.IsEmpty() && ZettaStudioAssets::RenderAssetJpeg(Main, 512, Jpeg))
	{
		DetectFrom(Jpeg);
		return;
	}
	TWeakPtr<SZettaAutoRigTool> Weak = StaticCastSharedRef<SZettaAutoRigTool>(AsShared());
	ZettaStudioAssets::DownloadPicture(From->ThumbnailUrl, [Weak](const FString& Error, const TArray<uint8>& Bytes, const FString&)
	{
		const TSharedPtr<SZettaAutoRigTool> Self = Weak.Pin();
		if (!Self) return;
		TArray<uint8> Frame;
		if (Error.IsEmpty() && ZettaStudioAssets::ToJpeg(Bytes, 512, Frame))
		{
			Self->DetectFrom(Frame);
			return;
		}
		Self->bDetecting = false;
		Self->Note = TEXT("Import the model into the project first to detect its body — or choose the body yourself.");
		Self->Refresh();
	});
}

void SZettaAutoRigTool::DetectFrom(const TArray<uint8>& Jpeg)
{
	TWeakPtr<SZettaAutoRigTool> Weak = StaticCastSharedRef<SZettaAutoRigTool>(AsShared());
	ZettaApi::RiggingPrecheck({ Jpeg }, [Weak](const FZettaResponse& R, const FString& BodyType, const FString&, const FString& Reason)
	{
		const TSharedPtr<SZettaAutoRigTool> Self = Weak.Pin();
		if (!Self) return;
		Self->bDetecting = false;
		if (!R.bOk) Self->Note = TEXT("That could not be detected. Choose the body yourself.");
		else if (ZettaCatalog::RigTypes().ContainsByPredicate([&BodyType](const ZettaCatalog::FChoice& C) { return C.Key == BodyType; }))
		{
			Self->Body = BodyType;
			Self->Note = FString::Printf(TEXT("Detected: %s. %s"), *RigChoiceLabel(ZettaCatalog::RigTypes(), BodyType), *Reason).TrimEnd();
		}
		else Self->Note = FString::Printf(TEXT("Could not tell what this is. %s"), *Reason).TrimEnd();
		Self->Refresh();
	});
}

void SZettaAutoRigTool::MakeOptions(FOptionsDone Done)
{
	TSharedRef<FJsonObject> Options = MakeShared<FJsonObject>();
	if (Body != TEXT("biped")) Options->SetStringField(TEXT("rigType"), Body);
	// A preset is a humanoid skeleton; on any other body the naming is not sent at all.
	const bool bKnownSpec = ZettaCatalog::RigSpecs().ContainsByPredicate([this](const ZettaCatalog::FChoice& C) { return C.Key == Spec; });
	if (Body == ZettaCatalog::HumanoidRigType && Spec != TEXT("standard") && bKnownSpec) Options->SetStringField(TEXT("rigSpec"), Spec);
	if (Format == TEXT("fbx")) Options->SetStringField(TEXT("rigOutFormat"), TEXT("fbx"));
	Done(FString(), Options);
}

#undef LOCTEXT_NAMESPACE
