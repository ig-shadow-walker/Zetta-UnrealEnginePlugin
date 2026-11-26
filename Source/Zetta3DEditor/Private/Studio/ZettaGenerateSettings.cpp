// Copyright Zetta 3D. See LICENSE.md.

// The Generate form's settings: the web Studio's defaults, visibility and lock rules per model capability and plan.

#include "SZettaGenerateTool.h"

#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "ZettaCatalog.h"
#include "ZettaStudioAssets.h"
#include "ZettaStudioContext.h"
#include "ZettaStudioUI.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace
{
	/** Zetta N1 reads counts up to this; past it the engine's own ceiling gets in the way. */
	constexpr int32 GenN1Max = 1480000;

	const FZettaEntitlements3D* GenGates() { return FZettaStudioContext::Get().G3(); }

	bool GenGateNo(TOptional<bool> FZettaEntitlements3D::*Field)
	{
		const FZettaEntitlements3D* G = GenGates();
		return G && FZettaStudioContext::No(G->*Field);
	}

	bool GenGateYes(TOptional<bool> FZettaEntitlements3D::*Field)
	{
		const FZettaEntitlements3D* G = GenGates();
		return G && FZettaStudioContext::Yes(G->*Field);
	}

	TOptional<int64> GenMaxFaces()
	{
		const FZettaEntitlements3D* G = GenGates();
		return G ? G->MaxFaceCount : TOptional<int64>();
	}

	FString GenBlurb(const FString& Model)
	{
		if (Model == TEXT("n1")) return TEXT("Base model");
		if (Model == TEXT("n25")) return TEXT("Highest quality of mesh and texture");
		if (Model == TEXT("np1")) return TEXT("Clean topology, balanced quality");
		if (Model == TEXT("np15")) return TEXT("Clean topology — quad native");
		return ZettaCatalog::Model(Model).Blurb;
	}

	/** A whole number as typed (an optional sign, then digits), or unset. */
	TOptional<int64> GenParseCount(const FString& Text)
	{
		const FString T = Text.TrimStartAndEnd();
		if (T.IsEmpty() || T.Len() > 12) return {};
		int32 i = (T[0] == TEXT('-') || T[0] == TEXT('+')) ? 1 : 0;
		if (i >= T.Len()) return {};
		for (int32 j = i; j < T.Len(); ++j) if (!FChar::IsDigit(T[j])) return {};
		return FCString::Atoi64(*T);
	}
}

// ─── Build ────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SZettaGenerateTool::BuildEngineChoice()
{
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	const TArray<FString>& Models = bSmart ? ZettaCatalog::SmartModels() : ZettaCatalog::GenerateModels();
	for (const FString& Model : Models)
	{
		const FString Label = ZettaCatalog::Model(Model).Label + (Model == TEXT("np15") ? TEXT(" · New") : TEXT(""));
		Rows->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
		[
			SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
			.ContentPadding(0)
			.ToolTipText_Lambda([this, Model]() { return FText::FromString(EngineLock(Model)); })
			.OnClicked_Lambda([this, Model]()
			{
				if (EngineLock(Model).IsEmpty() && O.Model != Model) SetModel(Model, true);
				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage_Lambda([this, Model]() { return FZettaStyle::Get().GetBrush(O.Model == Model ? "Zetta.CardSelected" : "Zetta.Card"); })
				.Padding(FMargin(10, 6))
				.ColorAndOpacity_Lambda([this, Model]() { return FLinearColor(1.f, 1.f, 1.f, EngineLock(Model).IsEmpty() ? 1.f : 0.55f); })
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(FText::FromString(Label)).Font(FZettaStyle::Font(10, true))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Font(FZettaStyle::Font(9))
						.Text_Lambda([this, Model]()
						{
							const FString Lock = EngineLock(Model);
							return FText::FromString(Lock.IsEmpty() ? GenBlurb(Model) : Lock);
						})
						.ColorAndOpacity_Lambda([this, Model]()
						{
							return FSlateColor(EngineLock(Model).IsEmpty() ? FZettaStyle::Muted() : FZettaStyle::Warning());
						})
					]
				]
			]
		];
	}
	return ZettaStudioUI::Field(LOCTEXT("Engine", "Generation Engine"), Rows);
}

TSharedRef<SWidget> SZettaGenerateTool::BuildSettings()
{
	auto Caps = [this]() -> const ZettaCatalog::FModelInfo& { return ZettaCatalog::Model(O.Model); };
	auto Textured = [this]() { return O.Output == TEXT("textured"); };

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	// Title — shown once a picture file is attached, suggested from its name until edited.
	Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
	[
		ZettaStudioUI::ShownWhen([this]() { return bHasFile; }, ZettaStudioUI::Field(LOCTEXT("TitleLabel", "Title"),
			SNew(SEditableTextBox)
			.HintText(LOCTEXT("TitleHint", "Name this generation"))
			.Text_Lambda([this]() { return FText::FromString(O.Title); })
			.OnTextChanged_Lambda([this](const FText& Text)
			{
				if (Text.ToString() == O.Title) return;
				bTitleTouched = true;
				O.Title = Text.ToString().Left(255);
			})))
	];

	Box->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
	[
		ZettaStudioUI::Check(LOCTEXT("RemoveBg", "Remove background"), [this]() { return bRemoveBackground; }, [this](bool b) { bRemoveBackground = b; Changed(); },
			LOCTEXT("RemoveBgTip", "The background is removed from the picture before it is uploaded."))
	];
	Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
	[
		ZettaStudioUI::ShownWhen([Caps]() { return Caps().bImageTuning; },
			ZettaStudioUI::Check(LOCTEXT("Autofix", "Enhance input image  · Free"), [this]() { return O.bAutofixImage; }, [this](bool b) { O.bAutofixImage = b; Changed(); },
				LOCTEXT("AutofixTip", "Cleans up the picture before the model is built from it.")))
	];

	Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[BuildEngineChoice()];

	// Output
	Box->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
	[
		ZettaStudioUI::Field(LOCTEXT("Output", "Output"), ZettaStudioUI::Segmented([this]() { return O.Output; }, [this](const FString& V)
		{
			// A deliberate pick: the plan's default must not argue with it.
			bOutputPicked = true;
			O.Output = V;
			Changed();
		}, { { TEXT("geometry"), LOCTEXT("Geometry", "Geometry"), FText() }, { TEXT("textured"), LOCTEXT("Textured", "Textured"), FText() } },
		[this](const FString& K) { return OutputLock(K); }))
	];

	// Segmented mesh
	Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
	[
		ZettaStudioUI::ShownWhen([this, Caps]() { return Caps().bParts && O.Quality != TEXT("low-poly"); },
			ZettaStudioUI::Check(MakeAttributeLambda([this]()
			{
				const FString Tag = PriceTag(TEXT("generate_parts"));
				return FText::FromString(Tag.IsEmpty() ? FString(TEXT("Segmented mesh")) : FString::Printf(TEXT("Segmented mesh  · %s"), *Tag));
			}), [this]() { return O.bSegmentParts; }, [this](bool b)
			{
				O.bSegmentParts = b;
				Invariants();
				Changed();
			}, LOCTEXT("PartsTip", "Split the mesh into separate parts")))
	];

	// Quality rungs
	const TArray<ZettaStudioUI::FOption> RungsN1 = {
		{ TEXT("low-poly"), LOCTEXT("LowPoly", "Low-Poly"), LOCTEXT("LowPolyTip", "Game-ready, light meshes") },
		{ TEXT("high-res"), LOCTEXT("HighRes", "High-Res"), LOCTEXT("HighResTip", "Detailed meshes") },
		{ TEXT("ultra"), LOCTEXT("Ultra", "Ultra"), LOCTEXT("UltraTip", "The most detail the model makes") },
	};
	const TArray<ZettaStudioUI::FOption> RungsN25 = {
		{ TEXT("low-poly"), LOCTEXT("LowPoly25", "Low-poly"), RungsN1[0].Tooltip },
		{ TEXT("high-res"), LOCTEXT("High25", "High"), RungsN1[1].Tooltip },
		{ TEXT("ultra"), LOCTEXT("Ultra25", "Ultra"), RungsN1[2].Tooltip },
	};
	auto GetQuality = [this]() { return O.Quality; };
	auto PutQuality = [this](const FString& V) { SetQuality(V); };
	auto LockRung = [this](const FString& K) { return QualityLock(K); };
	Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
	[
		ZettaStudioUI::ShownWhen([this]() { return QualityRungs(); },
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
			[
				ZettaStudioUI::FieldLabel(MakeAttributeLambda([this]() { return O.Model == TEXT("n25") ? LOCTEXT("MeshQuality", "Mesh quality") : LOCTEXT("Quality", "Quality"); }))
			]
			+ SVerticalBox::Slot().AutoHeight()[ZettaStudioUI::ShownWhen([this]() { return O.Model != TEXT("n25"); }, ZettaStudioUI::Segmented(GetQuality, PutQuality, RungsN1, LockRung))]
			+ SVerticalBox::Slot().AutoHeight()[ZettaStudioUI::ShownWhen([this]() { return O.Model == TEXT("n25"); }, ZettaStudioUI::Segmented(GetQuality, PutQuality, RungsN25, LockRung))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)[ZettaStudioUI::Locked(MakeAttributeLambda([this]() { return FText::FromString(QualityLockLine()); }))])
	];

	// Polygon control
	auto MayChoose = []() { return !GenGateNo(&FZettaEntitlements3D::AllowsCustomFaceCount); };
	TSharedRef<SVerticalBox> Polygon = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
		[
			ZettaStudioUI::FieldLabel(MakeAttributeLambda([this, MayChoose]()
			{
				const bool bCustom = MayChoose() && O.FaceCount.IsSet() && O.FaceCount.GetValue() != DefaultCount();
				const FString Tag = bCustom ? PriceTag(TEXT("custom_face_count")) : FString();
				return FText::FromString(Tag.IsEmpty() ? FString(TEXT("Polygon count")) : FString::Printf(TEXT("Polygon count  · %s"), *Tag));
			}))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			ZettaStudioUI::ShownWhen(MayChoose,
				// Free text, not a spin box: a number the engine refuses must stay as typed and be named
				// out of range under the field, never silently clamped.
				SNew(SEditableTextBox)
				.HintText(LOCTEXT("CountHint", "The engine's default"))
				.Text_Lambda([this]() { return FText::FromString(CountText); })
				.ToolTipText_Lambda([this]()
				{
					const FInt32Range B = Bounds();
					return FText::FromString(FString::Printf(TEXT("%s–%s"), *ZettaStudioUI::Number(B.GetLowerBoundValue()), *ZettaStudioUI::Number(B.GetUpperBoundValue())));
				})
				.OnTextChanged_Lambda([this](const FText& Text)
				{
					if (Text.ToString() == CountText) return;
					TypeCount(Text.ToString());
					Changed();
				}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)[ZettaStudioUI::Error(MakeAttributeLambda([this]() { return FText::FromString(CountFieldError()); }))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
		[
			ZettaStudioUI::Locked(MakeAttributeLambda([MayChoose]()
			{
				return MayChoose() ? FText() : LOCTEXT("CountLocked", "The model's default face count. Choosing a count is available on a paid plan.");
			}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
		[
			ZettaStudioUI::Hint(MakeAttributeLambda([this, MayChoose]()
			{
				if (!MayChoose() || O.Model == TEXT("n1")) return FText();
				const FInt32Range B = Bounds();
				return FText::FromString(FString::Printf(TEXT("Minimum %s, up to %s"), *ZettaStudioUI::Number(B.GetLowerBoundValue()), *ZettaStudioUI::Number(B.GetUpperBoundValue())));
			}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
		[
			ZettaStudioUI::Hint(MakeAttributeLambda([Caps, MayChoose]()
			{
				return !Caps().bDetailedGeometry && !MayChoose() ? LOCTEXT("PlanLine", "Your plan generates at the default quality of 500,000 polygons.") : FText();
			}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
		[
			ZettaStudioUI::ShownWhen([Caps]() { return Caps().bQuadAnyBase || Caps().bLowPoly; },
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					ZettaStudioUI::Field(LOCTEXT("Polygons", "Polygons"), ZettaStudioUI::Segmented([this]() { return O.PolygonType; }, [this](const FString& V)
					{
						O.PolygonType = V;
						// A typed count survives a topology switch exactly as typed (and is reported out of range if the
						// new topology refuses it); a count nobody typed follows the new topology's preset.
						if (!bCountTyped) SetCount(DefaultCount(), false);
						Changed();
					}, { { TEXT("triangle"), LOCTEXT("Triangles", "Triangles"), FText() }, { TEXT("quadrilateral"), LOCTEXT("Quads", "Quads"), FText() } },
					[this](const FString& K) { return PolygonLock(K); }))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)[ZettaStudioUI::Locked(MakeAttributeLambda([this]() { return FText::FromString(PolygonLockLine()); }))])
		];

	Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
	[
		SNew(SExpandableArea)
		.AreaTitle(LOCTEXT("PolygonControl", "Polygon control"))
		.InitiallyCollapsed(true)
		.Padding(FMargin(8, 6))
		.BodyContent()[Polygon]
	];

	// PBR and texture quality
	Box->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
	[
		ZettaStudioUI::ShownWhen(Textured, ZettaStudioUI::Check(MakeAttributeLambda([this]()
		{
			const FString Tag = O.bPbr ? PriceTag(TEXT("pbr")) : FString();
			return FText::FromString(Tag.IsEmpty() ? FString(TEXT("PBR")) : FString::Printf(TEXT("PBR  · %s"), *Tag));
		}), [this]() { return O.bPbr; }, [this](bool b) { O.bPbr = b; Changed(); },
			LOCTEXT("PbrTip", "Physically based materials: roughness, metalness and normal maps.")))
	];
	auto GetTexture = [this]() { return O.TextureQuality; };
	auto PutTexture = [this](const FString& V) { O.TextureQuality = V; Changed(); };
	Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
	[
		ZettaStudioUI::ShownWhen([Caps, Textured]() { return Caps().bDetailedTexture && Textured(); },
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)[ZettaStudioUI::FieldLabel(LOCTEXT("TextureQuality", "Texture quality"))]
			+ SVerticalBox::Slot().AutoHeight()
			[
				ZettaStudioUI::ShownWhen([Caps]() { return !Caps().bExtremeTexture; }, ZettaStudioUI::Segmented(GetTexture, PutTexture,
					{ { TEXT("standard"), LOCTEXT("Standard", "Standard"), FText() }, { TEXT("detailed"), LOCTEXT("Detailed", "Detailed"), FText() } }))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				ZettaStudioUI::ShownWhen([Caps]() { return Caps().bExtremeTexture; }, ZettaStudioUI::Segmented(GetTexture, PutTexture,
					{ { TEXT("standard"), LOCTEXT("Standard", "Standard"), FText() }, { TEXT("detailed"), LOCTEXT("Detailed", "Detailed"), FText() },
					  { TEXT("extreme"), LOCTEXT("Tex8K", "8K"), FText() } }))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
			[
				ZettaStudioUI::Hint(MakeAttributeLambda([this, Caps]()
				{
					if (O.TextureQuality == TEXT("detailed")) return FText::FromString(FString::Printf(TEXT("Up to 4K resolution (%s)"), *PriceTag(TEXT("detailed_texture"))));
					if (O.TextureQuality == TEXT("extreme") && Caps().bExtremeTexture) return FText::FromString(FString::Printf(TEXT("Up to 8K resolution (%s)"), *PriceTag(TEXT("extreme_texture"))));
					return LOCTEXT("Up2K", "Up to 2K resolution (free)");
				}))
			])
	];

	// Visibility
	Box->AddSlot().AutoHeight().Padding(0, 0, 0, 3)[ZettaStudioUI::FieldLabel(LOCTEXT("Visibility", "Visibility"))];
	Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			ZettaStudioUI::Check(MakeAttributeLambda([this]() { return PrivateAllowed() ? LOCTEXT("Private", "Keep this model private") : LOCTEXT("PrivatePaid", "Keep this model private (paid plan)"); }),
				[this]() { return PrivateAllowed() && IsPrivate(); },
				[this](bool b) { O.Availability = b ? TEXT("private") : TEXT("public"); Changed(); },
				MakeAttributeLambda([this]()
				{
					return PrivateAllowed()
						? LOCTEXT("PrivateTip", "Private models are yours alone. Public ones can appear in the community gallery.")
						: LOCTEXT("PrivateLockedTip", "Keeping a model private is available on a paid plan. Models made on your plan are public.");
				}),
				MakeAttributeLambda([this]() { return PrivateAllowed(); }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
		[
			ZettaStudioUI::Locked(MakeAttributeLambda([this]() { return PrivateAllowed() ? FText() : LOCTEXT("PrivateLocked", "Keeping a model private is available on a paid plan."); }))
		]
	];

	// Advanced
	TSharedRef<SVerticalBox> Advanced = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			ZettaStudioUI::ShownWhen([Caps]() { return Caps().bImageTuning; },
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
				[
					ZettaStudioUI::Check(LOCTEXT("Scale", "Real-world scale  · Free"), [this, Textured]() { return Textured() && O.bRealWorldScale; },
						[this](bool b) { O.bRealWorldScale = b; Changed(); },
						MakeAttributeLambda([Textured]() { return Textured() ? LOCTEXT("ScaleTip", "Sizes the model in metres, as the object would be.") : LOCTEXT("ScaleOff", "Only applies to a textured output."); }),
						MakeAttributeLambda([Textured]() { return Textured(); }))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
				[
					ZettaStudioUI::Check(LOCTEXT("Compress", "Smaller file  · Free"), [this]() { return O.bCompressGeometry; }, [this](bool b) { O.bCompressGeometry = b; Changed(); },
						LOCTEXT("CompressTip", "Compresses the geometry for a lighter download."))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
				[
					ZettaStudioUI::ShownWhen(Textured, ZettaStudioUI::Check(LOCTEXT("ExportUv", "Export UV map  · Free"), [this]() { return O.bExportUv; }, [this](bool b) { O.bExportUv = b; Changed(); },
						LOCTEXT("ExportUvTip", "Off is faster and produces a smaller file, but the mesh cannot be re-textured.")))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
				[
					ZettaStudioUI::ShownWhen(Textured, ZettaStudioUI::Check(LOCTEXT("Delight", "Remove baked-in lighting  · Free"), [this]() { return O.bDelight; }, [this](bool b) { O.bDelight = b; Changed(); },
						LOCTEXT("DelightTip", "Removes the lighting baked into the reference image, so the texture lights correctly in your own level.")))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
				[
					ZettaStudioUI::ShownWhen(Textured, ZettaStudioUI::Field(LOCTEXT("TextureFit", "Texture fit"), ZettaStudioUI::Segmented([this]() { return O.TextureAlignment; },
						[this](const FString& V) { O.TextureAlignment = V; Changed(); },
						{ { TEXT("geometry"), LOCTEXT("FitMesh", "Fit texture to mesh"), FText() }, { TEXT("original_image"), LOCTEXT("FitPicture", "Fit texture to picture"), FText() } })))
				])
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			ZettaStudioUI::ShownWhen([this]() { return !bMultiViewSettings; },
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					ZettaStudioUI::Check(LOCTEXT("ControlMode", "Control Mode"), [this]() { return O.bControlMode; }, [this](bool b) { O.bControlMode = b; Changed(); },
						LOCTEXT("ControlModeTip", "Displays the reference before generating 3D: a picture drawn from your prompt is shown first, so you can redraw it or go ahead."))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)[ZettaStudioUI::Hint(LOCTEXT("ControlModeHint", "Control Mode displays the reference before generating 3D."))])
		];

	Box->AddSlot().AutoHeight()
	[
		ZettaStudioUI::ShownWhen([this, Caps]() { return Caps().bImageTuning || !bMultiViewSettings; },
			SNew(SExpandableArea)
			.AreaTitle(LOCTEXT("AdvancedControls", "Advanced controls"))
			.InitiallyCollapsed(true)
			.Padding(FMargin(8, 6))
			.BodyContent()[Advanced])
	];

	return ZettaStudioUI::Group(LOCTEXT("Settings", "Settings"), Box);
}

// ─── Rules ────────────────────────────────────────────────────────────────────

int32 SZettaGenerateTool::DefaultCount() const
{
	return ZettaCatalog::FacePreset(O.Model, O.Quality, Quads());
}

FInt32Range SZettaGenerateTool::Bounds() const
{
	const FInt32Range R = ZettaCatalog::FaceRange(O.Model, O.Quality, Quads());
	const int32 Max = O.Model == TEXT("n1") ? FMath::Min(R.GetUpperBoundValue(), GenN1Max) : R.GetUpperBoundValue();
	return FInt32Range(FInt32Range::BoundsType::Inclusive(R.GetLowerBoundValue()), FInt32Range::BoundsType::Inclusive(Max));
}

void SZettaGenerateTool::SetCount(const TOptional<int32>& Count, bool bTyped)
{
	O.FaceCount = Count;
	CountText = Count.IsSet() ? FString::FromInt(Count.GetValue()) : FString();
	bCountTyped = bTyped;
}

void SZettaGenerateTool::TypeCount(const FString& Text)
{
	CountText = Text;
	bCountTyped = true;
	const TOptional<int64> Parsed = GenParseCount(Text);
	// Empty (or not a number) means "whatever the quality implies" — no custom count is sent.
	if (!Parsed.IsSet() || Parsed.GetValue() < MIN_int32 || Parsed.GetValue() > MAX_int32)
	{
		O.FaceCount.Reset();
		return;
	}
	const int32 Value = (int32)Parsed.GetValue();
	O.FaceCount = Value;
	// On Zetta N1 the rung IS a count, so typing a count moves the rung with it.
	if (O.Model == TEXT("n1") && Value >= 3100 && Value <= GenN1Max)
	{
		O.Quality = Value >= 1000000 ? TEXT("ultra") : Value > 15000 ? TEXT("high-res") : TEXT("low-poly");
	}
}

FString SZettaGenerateTool::CountProblem() const
{
	if (GenGateNo(&FZettaEntitlements3D::AllowsCustomFaceCount)) return FString();
	const FString T = CountText.TrimStartAndEnd();
	if (T.IsEmpty()) return FString();
	const TOptional<int64> Count = GenParseCount(T);
	if (!Count.IsSet()) return TEXT("The polygon count must be a whole number.");
	const FInt32Range B = Bounds();
	if (Count.GetValue() < B.GetLowerBoundValue() || Count.GetValue() > B.GetUpperBoundValue())
	{
		return FString::Printf(TEXT("The polygon count must be between %s and %s."), *ZettaStudioUI::Number(B.GetLowerBoundValue()), *ZettaStudioUI::Number(B.GetUpperBoundValue()));
	}
	return FString();
}

FString SZettaGenerateTool::CountFieldError() const
{
	const FString Problem = CountProblem();
	if (Problem.IsEmpty()) return FString();
	if (Problem.Contains(TEXT("whole number"))) return TEXT("Out of range — a whole number, please.");
	const FInt32Range B = Bounds();
	return FString::Printf(TEXT("Out of range — %s to %s."), *ZettaStudioUI::Number(B.GetLowerBoundValue()), *ZettaStudioUI::Number(B.GetUpperBoundValue()));
}

void SZettaGenerateTool::SetModel(const FString& Model, bool bPicked)
{
	if (bPicked) bModelPicked = true;
	ApplyModel(Model);
	Changed();
}

void SZettaGenerateTool::ApplyModel(const FString& Model)
{
	// An untouched count follows the model (so a switch never turns on the paid custom count by itself);
	// a count that no longer fits the new engine is refilled too.
	const bool bUntouched = !O.FaceCount.IsSet() || O.FaceCount.GetValue() == DefaultCount();
	O.Model = Model;
	const ZettaCatalog::FModelInfo& Caps = ZettaCatalog::Model(Model);
	if (O.Quality == TEXT("low-poly") && !Caps.bLowPoly) O.Quality = TEXT("high-res");
	if (O.TextureQuality == TEXT("extreme") && !Caps.bExtremeTexture) O.TextureQuality = TEXT("detailed");
	if (Quads() && !Caps.bQuadAnyBase && !Caps.bLowPoly) O.PolygonType = TEXT("triangle");
	Invariants();
	const FInt32Range B = Bounds();
	if (bUntouched || !CountProblem().IsEmpty() || (O.FaceCount.IsSet() && !B.Contains(O.FaceCount.GetValue()))) SetCount(DefaultCount(), false);
}

void SZettaGenerateTool::SetQuality(const FString& Quality)
{
	O.Quality = Quality;
	Invariants();
	// A rung change is choosing a new density: the preset is what was asked for.
	SetCount(DefaultCount(), false);
	Changed();
}

void SZettaGenerateTool::Invariants()
{
	if (O.Quality == TEXT("low-poly") && QualityRungs())
	{
		// Only Zetta N2.5 makes a bare low-poly mesh; N1's low-poly is always textured.
		if (O.Model != TEXT("n25")) O.Output = TEXT("textured");
		O.bSegmentParts = false;
	}
	else if (O.bSegmentParts)
	{
		O.Output = TEXT("geometry");
		O.PolygonType = TEXT("triangle");
	}
}

void SZettaGenerateTool::ApplySetUp(const ZettaStudioFork::FSettings& S)
{
	bModelPicked = true;
	bOutputPicked = true;
	const TArray<FString>& Models = bSmart ? ZettaCatalog::SmartModels() : ZettaCatalog::GenerateModels();
	ApplyModel(Models.Contains(S.Model) ? S.Model : Models[0]);
	const ZettaCatalog::FModelInfo& Caps = ZettaCatalog::Model(O.Model);
	if (S.Quality == TEXT("low-poly") || S.Quality == TEXT("high-res") || S.Quality == TEXT("ultra"))
	{
		O.Quality = S.Quality == TEXT("low-poly") && !Caps.bLowPoly ? TEXT("high-res") : S.Quality;
	}
	if (S.Output == TEXT("geometry") || S.Output == TEXT("textured")) O.Output = S.Output;
	if (S.PolygonType == TEXT("triangle") || S.PolygonType == TEXT("quadrilateral")) O.PolygonType = S.PolygonType;
	if (S.TextureQuality == TEXT("standard") || S.TextureQuality == TEXT("detailed") || S.TextureQuality == TEXT("extreme"))
	{
		O.TextureQuality = S.TextureQuality == TEXT("extreme") && !Caps.bExtremeTexture ? TEXT("detailed") : S.TextureQuality;
	}
	O.bPbr = S.bPbr;
	O.bSegmentParts = S.bSegmentParts;
	Invariants();
	SetCount(DefaultCount(), false);
	if (S.FaceCount.IsSet())
	{
		TypeCount(FString::FromInt(S.FaceCount.GetValue()));
		bCountTyped = false;
	}
	Changed();
}

FString SZettaGenerateTool::PriceTag(const FString& Code) const
{
	return ZettaStudioUI::PriceTag(FZettaStudioContext::Get().Price(Code, O.Model));
}

void SZettaGenerateTool::ApplyContextDefaults()
{
	const FZettaEntitlements3D* G = GenGates();
	if (!G) return;
	// The output the form opens on, by plan: Geometry without a paid plan, Textured otherwise — never over the user's pick.
	if (!bOutputPicked)
	{
		const FString Wanted = FZettaStudioContext::No(G->AllowsPrivateGenerations) ? TEXT("geometry") : TEXT("textured");
		if (Wanted != O.Output && !(O.bSegmentParts && Wanted == TEXT("textured")))
		{
			O.Output = Wanted;
			Invariants();
		}
	}
	// Generate 3D opens on N1 and moves to N2.5 once the plan is known to include it — unless someone chose.
	if (!bSmart && !bModelPicked && FZettaStudioContext::Yes(G->AllowsZettaN25) && O.Model == TEXT("n1")) ApplyModel(TEXT("n25"));
}

void SZettaGenerateTool::SetMultiViewSettings(bool bMulti)
{
	bMultiViewSettings = bMulti;
}

void SZettaGenerateTool::SetFile(const FString& FileName)
{
	bHasFile = !FileName.IsEmpty();
	if (bHasFile && !bTitleTouched) O.Title = ZettaStudioAssets::TitleFromFile(FileName);
	if (!bHasFile && !bTitleTouched) O.Title.Reset();
}

void SZettaGenerateTool::ResetInputs()
{
	bTitleTouched = false;
	O.Title.Reset();
}

FString SZettaGenerateTool::EngineLock(const FString& Model) const
{
	if (Model == TEXT("n25") && GenGateNo(&FZettaEntitlements3D::AllowsZettaN25)) return TEXT("Zetta N2.5 is available on a paid plan.");
	if ((Model == TEXT("np1") || Model == TEXT("np15")) && GenGateNo(&FZettaEntitlements3D::AllowsZettaNp1)) return TEXT("Available on a paid plan.");
	return FString();
}

FString SZettaGenerateTool::QualityLock(const FString& Rung) const
{
	if (GenGateNo(&FZettaEntitlements3D::AllowsCustomFaceCount)) return TEXT("Your plan generates at the default quality — 500,000 polygons. Choosing a quality is available on a paid plan.");
	const TOptional<int64> Max = GenMaxFaces();
	if (Rung == TEXT("ultra") && Max.IsSet() && Max.GetValue() < 1000000) return TEXT("Ultra detail is available on paid plans.");
	if (Rung == TEXT("low-poly") && GenGateNo(&FZettaEntitlements3D::AllowsLowPoly)) return TEXT("Low-poly generation is available on a paid plan.");
	if (Rung == TEXT("low-poly") && !ZettaCatalog::Model(O.Model).bLowPoly) return TEXT("Low-poly generation needs Zetta N2.5.");
	return FString();
}

FString SZettaGenerateTool::QualityLockLine() const
{
	// The plan-wide lock first, then the rung in use, then any rung's reason.
	FString Line;
	for (const FString Rung : { FString(TEXT("low-poly")), FString(TEXT("high-res")), FString(TEXT("ultra")) })
	{
		const FString Reason = QualityLock(Rung);
		if (Reason.IsEmpty()) continue;
		if (GenGateNo(&FZettaEntitlements3D::AllowsCustomFaceCount) || Rung == O.Quality || Line.IsEmpty()) Line = Reason;
	}
	return Line;
}

FString SZettaGenerateTool::OutputLock(const FString& Output) const
{
	if (Output == TEXT("geometry") && O.Quality == TEXT("low-poly") && QualityRungs() && O.Model != TEXT("n25")) return TEXT("Not available at this mesh quality.");
	if (Output == TEXT("textured") && O.bSegmentParts) return TEXT("A segmented mesh comes out untextured. Turn off Segmented mesh first.");
	return FString();
}

FString SZettaGenerateTool::PolygonLock(const FString& Type) const
{
	const ZettaCatalog::FModelInfo& Caps = ZettaCatalog::Model(O.Model);
	if (O.Quality != TEXT("low-poly") && !Caps.bQuadAnyBase) return TEXT("Not used at this mesh quality.");
	if (Type == TEXT("quadrilateral"))
	{
		if (O.bSegmentParts) return TEXT("Not available while segmenting.");
		if (GenGateNo(&FZettaEntitlements3D::AllowsQuadMesh)) return TEXT("Quad meshes are available on paid plans.");
	}
	return FString();
}

FString SZettaGenerateTool::PolygonLockLine() const
{
	const FString Off = PolygonLock(TEXT("triangle"));
	return Off.IsEmpty() ? PolygonLock(TEXT("quadrilateral")) : Off;
}

bool SZettaGenerateTool::PrivateAllowed() const
{
	return !GenGateNo(&FZettaEntitlements3D::AllowsPrivateGenerations);
}

bool SZettaGenerateTool::IsPrivate() const
{
	return O.Availability.IsEmpty() ? GenGateYes(&FZettaEntitlements3D::AllowsPrivateGenerations) : O.Availability == TEXT("private");
}

FZettaGenerateOptions SZettaGenerateTool::Snapshot() const
{
	const bool bTextured = O.Output == TEXT("textured");
	FZettaGenerateOptions S;
	S.Model = O.Model;
	S.Quality = QualityRungs() ? O.Quality : TEXT("high-res");
	S.Output = O.Output;
	S.bPbr = O.bPbr;
	S.TextureQuality = O.TextureQuality;
	S.FaceCount = O.FaceCount;
	S.PolygonType = O.PolygonType;
	S.Title = bHasFile ? O.Title : FString();
	S.bSegmentParts = O.bSegmentParts && ZettaCatalog::Model(O.Model).bParts && O.Quality != TEXT("low-poly");
	S.bRealWorldScale = bTextured && O.bRealWorldScale;
	S.bDelight = bTextured && O.bDelight;
	S.bAutofixImage = O.bAutofixImage;
	S.bCompressGeometry = O.bCompressGeometry;
	// Texture-only knobs are dropped on a geometry output.
	S.TextureAlignment = bTextured ? O.TextureAlignment : TEXT("original_image");
	S.bExportUv = !bTextured || O.bExportUv;
	S.bControlMode = O.bControlMode && !bMultiViewSettings;
	S.Availability = PrivateAllowed() ? O.Availability : FString();
	return S;
}

TArray<FString> SZettaGenerateTool::PlanProblems(const FZettaBuiltRequest& Built, int32 Views) const
{
	TArray<FString> Out;
	const FZettaEntitlements3D* G = GenGates();
	if (!G) return Out;
	if (O.Model == TEXT("n25") && FZettaStudioContext::No(G->AllowsZettaN25)) Out.Add(TEXT("Zetta N2.5 is available on a paid plan."));
	if ((O.Model == TEXT("np1") || O.Model == TEXT("np15")) && FZettaStudioContext::No(G->AllowsZettaNp1)) Out.Add(TEXT("Generate Smart 3D is available on a paid plan."));
	if (Built.Base == TEXT("lowpoly") && FZettaStudioContext::No(G->AllowsLowPoly)) Out.Add(TEXT("Low-poly generation is available on a paid plan."));
	FString Polygon;
	if (Built.Body.IsValid() && Built.Body->TryGetStringField(TEXT("polygonType"), Polygon) && Polygon == TEXT("quadrilateral") && FZettaStudioContext::No(G->AllowsQuadMesh))
	{
		Out.Add(TEXT("Quad meshes are available on paid plans."));
	}
	if (G->MaxFaceCount.IsSet() && G->MaxFaceCount.GetValue() < 1000000)
	{
		const int64 MaxFacesValue = G->MaxFaceCount.GetValue();
		double Sent = 0;
		if (Built.Modifiers.Contains(TEXT("detailed_geometry"))) Out.Add(TEXT("Ultra detail is available on paid plans."));
		else if (Built.Body.IsValid() && Built.Body->TryGetNumberField(TEXT("faceCount"), Sent) && Sent >= FMath::Min<double>(1000000, MaxFacesValue + 1))
		{
			Out.Add(FString::Printf(TEXT("Your plan generates up to %s polygons. Lower the polygon count, or upgrade."), *ZettaStudioUI::Number(MaxFacesValue)));
		}
	}
	if (Views > 0 && G->MaxMultiViewImages.IsSet() && Views > G->MaxMultiViewImages.GetValue())
	{
		Out.Add(FString::Printf(TEXT("You have %d views and your plan uses %lld."), Views, G->MaxMultiViewImages.GetValue()));
	}
	return Out;
}

#undef LOCTEXT_NAMESPACE
