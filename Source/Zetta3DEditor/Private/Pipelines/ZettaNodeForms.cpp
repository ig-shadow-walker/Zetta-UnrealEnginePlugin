// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaNodeForms.h"

#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "ZettaCatalog.h"
#include "ZettaPipelineJson.h"
#include "ZettaPipelineServices.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

using namespace ZettaPJson;

// ─── Context ────────────────────────────────────────────────────────────────────────────────────────────────────────

void FZettaFormContext::Refresh(const FZettaPipelineNode& InNode, const FZettaPipelineGraph& InGraph)
{
	Node = InNode;
	Graph = InGraph;
	Settings = InNode.Settings.IsValid() ? JClone(InNode.Settings) : ZettaNodeSettings::Defaults(InNode.Kind);
	Incoming = ZettaGraphRules::IncomingType(InGraph, InNode.Id);
}

FString FZettaFormContext::Str(const FString& Key, const FString& Fallback) const { return JTextOr(Settings, Key, Fallback); }

bool FZettaFormContext::Bool(const FString& Key, bool bFallback) const { return JBoolOr(Settings, Key, bFallback); }

void FZettaFormContext::Set(const FString& Key, const TSharedPtr<FJsonValue>& Value) const
{
	TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
	P->SetField(Key, Value.IsValid() ? Value : JNull());
	if (Patch) Patch(P);
}

// ─── Forms ──────────────────────────────────────────────────────────────────────────────────────────────────────────

namespace ZettaNodeForms
{
	using FCtx = TSharedRef<FZettaFormContext>;

	static TSharedRef<SWidget> Para(const FText& Words, bool bMuted = true)
	{
		return SNew(STextBlock).Text(Words).AutoWrapText(true).ColorAndOpacity(bMuted ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground());
	}

	static TSharedRef<SWidget> Hint(TFunction<FString()> Words)
	{
		return SNew(STextBlock).AutoWrapText(true).Font(FZettaStyle::Font(8)).ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text_Lambda([Words]() { return FText::FromString(Words()); })
			.Visibility_Lambda([Words]() { return Words().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; });
	}

	static TSharedRef<SWidget> Shown(TFunction<bool()> Visible, const TSharedRef<SWidget>& Content)
	{
		return SNew(SBox).Visibility_Lambda([Visible]() { return Visible() ? EVisibility::Visible : EVisibility::Collapsed; })[Content];
	}

	static TSharedRef<SWidget> Gap(const TSharedRef<SWidget>& Content, float Top = 4.f) { return SNew(SBox).Padding(FMargin(0, Top, 0, 0))[Content]; }

	/** A segmented choice bound to one key, with an optional hint under it that follows the value. */
	static TSharedRef<SWidget> Choice(const FCtx& Ctx, const FText& Label, const FString& Key, const TArray<FZettaSettingOption>& Options,
		bool bHint = true, const FString& Fallback = FString(), TFunction<FString(const FString&)> DisabledReason = nullptr)
	{
		const FString First = Fallback.IsEmpty() && Options.Num() > 0 ? Options[0].Value : Fallback;
		auto Value = [Ctx, Key, First, Options]()
		{
			const FString V = Ctx->Str(Key, First);
			return ZettaToolSettings::Find(Options, V) ? V : First;
		};
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				ZettaPlUI::Field(Label, ZettaPlUI::Segmented(ZettaPlUI::Options(Options), Value, [Ctx, Key](const FString& V) { Ctx->Set(Key, JStr(V)); }, DisabledReason))
			];
		if (bHint)
		{
			Box->AddSlot().AutoHeight().Padding(0, 2)
			[
				Hint([Value, Options]() { const FZettaSettingOption* O = ZettaToolSettings::Find(Options, Value()); return O ? O->Hint : FString(); })
			];
		}
		return Box;
	}

	static TSharedRef<SWidget> Check(const FCtx& Ctx, const TAttribute<FText>& Label, const FString& Key, bool bFallback, const FText& Tooltip = FText(),
		const TAttribute<bool>& Enabled = true)
	{
		return ZettaPlUI::Check(Label, [Ctx, Key, bFallback]() { return Ctx->Bool(Key, bFallback); }, [Ctx, Key](bool bOn) { Ctx->Set(Key, JBool(bOn)); }, Tooltip, Enabled);
	}

	static TSharedRef<SWidget> Words(const FCtx& Ctx, const FText& Label, const FString& Key, const FText& Placeholder, int32 Max, bool bMultiLine = true)
	{
		TSharedRef<SWidget> Box = SNew(SBox).MinDesiredHeight(bMultiLine ? 54.f : 0.f)
			[
				SNew(SZettaPlText, [Ctx, Key]() { return Ctx->Str(Key); }, [Ctx, Key](const FString& V) { Ctx->Set(Key, JStr(V)); })
				.MultiLine(bMultiLine).MaxLength(Max).Hint(Placeholder)
			];
		return Label.IsEmpty() ? Box : ZettaPlUI::Field(Label, Box);
	}

	/** The pipeline leaves this step's input to each run. */
	static TSharedRef<SWidget> GivenInEachRun(const FString& What)
	{
		return ZettaPlUI::Note(FText::FromString(FString::Printf(
			TEXT("↻ The %s is given in each run. New run opens the run without starting it, with this step marked Needs input — fill it in there, then press Start."), *What)), TEXT("dashed"));
	}

	/** The Image Studio's engines, the locked ones greyed out and saying why. */
	static TSharedRef<SWidget> ImageEngine(const FCtx& Ctx)
	{
		auto Models = []() { return FZettaPipelinePlan::Get().ImageModels(); };
		TArray<FZettaPlOption> Options;
		if (const FZettaImageModels* M = Models()) for (const FZettaImageModel& Model : M->Models) Options.Add({ Model.Id, Model.Label, FString() });
		if (Options.Num() == 0) return SNullWidget::NullWidget;
		auto Value = [Ctx, Models]()
		{
			const FString Own = Ctx->Str(TEXT("model"));
			if (!Own.IsEmpty()) return Own;
			const FZettaImageModels* M = Models();
			return M ? (M->Default.IsEmpty() && M->Models.Num() > 0 ? M->Models[0].Id : M->Default) : FString();
		};
		return ZettaPlUI::Field(LOCTEXT("PlImageEngine", "Image engine"), ZettaPlUI::Segmented(Options, Value, [Ctx](const FString& V) { Ctx->Set(TEXT("model"), JStr(V)); },
			[Models](const FString& Id)
			{
				if (const FZettaImageModels* M = Models())
					for (const FZettaImageModel& Model : M->Models) if (Model.Id == Id && Model.bLocked) return Model.LockedReason.IsEmpty() ? FString(TEXT("Available on a paid plan.")) : Model.LockedReason;
				return FString();
			}));
	}

	static void AiEdit(const TSharedRef<SVerticalBox>& Host, const FCtx& Ctx)
	{
		auto PerRun = [Ctx]() { return !Ctx->bInRun && Ctx->Bool(TEXT("askAtRun")); };
		if (!Ctx->bInRun)
		{
			Host->AddSlot().AutoHeight()[Check(Ctx, LOCTEXT("PlSetEachRun", "Set on each run"), TEXT("askAtRun"), false)];
			Host->AddSlot().AutoHeight()[Hint([]() { return FString(TEXT("Every run asks for its own instruction before it starts.")); })];
		}
		Host->AddSlot().AutoHeight().Padding(0, 4)[Shown(PerRun, GivenInEachRun(TEXT("instruction")))];
		Host->AddSlot().AutoHeight()[Shown([PerRun]() { return !PerRun(); }, Words(Ctx, LOCTEXT("PlHowChange", "How to change it"), TEXT("prompt"), LOCTEXT("PlHowChangeHint", "Make the armour gold, keep everything else"), 2000))];
		Host->AddSlot().AutoHeight()[ImageEngine(Ctx)];
		Host->AddSlot().AutoHeight().Padding(0, 4)[Check(Ctx, LOCTEXT("PlFor3D", "Frame it for 3D — one object, centred, plain background"), TEXT("for3D"), true)];
		Host->AddSlot().AutoHeight().Padding(0, 4)[ZettaPlUI::Note(LOCTEXT("PlEditPlan", "Image editing needs a plan that includes it, as in the Image Studio."))];
	}

	static void DrawViews(const TSharedRef<SVerticalBox>& Host, const FCtx& Ctx)
	{
		Host->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[Para(LOCTEXT("PlDrawViewsLead", "The picture coming in is the front. These angles are drawn from it, as the Studio’s Generate views does — connect this to Multi-view - Generate 3D for a multi-view model."))];
		for (const FZettaSettingOption& O : ZettaToolSettings::DrawViewsChoices())
		{
			const FString View = O.Value;
			auto Current = [Ctx]() { return JStringsOf(ZettaPJson::JGet(Ctx->Settings, TEXT("views"))); };
			Host->AddSlot().AutoHeight()
			[
				ZettaPlUI::Check(FText::FromString(O.Label), [Current, View]() { return Current().Contains(View); }, [Ctx, Current, View](bool bOn)
				{
					TArray<FString> Views = Current();
					if (bOn) Views.AddUnique(View);
					else Views.Remove(View);
					Ctx->Set(TEXT("views"), JStrings(Views));
				})
			];
		}
	}

	static void Isolate(const TSharedRef<SVerticalBox>& Host, const FCtx& Ctx)
	{
		auto Identify = [Ctx]() { return Ctx->Bool(TEXT("identify"), true); };
		auto Parts = [Ctx]() { return JStringsOf(ZettaPJson::JGet(Ctx->Settings, TEXT("extraParts"))); };
		Host->AddSlot().AutoHeight()
		[
			ZettaPlUI::Field(LOCTEXT("PlFindParts", "Find the parts"), ZettaPlUI::Segmented({ { TEXT("identify"), TEXT("Identify"), FString() }, { TEXT("typed"), TEXT("Only my list"), FString() } },
				[Identify]() { return Identify() ? FString(TEXT("identify")) : FString(TEXT("typed")); },
				[Ctx](const FString& V) { Ctx->Set(TEXT("identify"), JBool(V == TEXT("identify"))); }))
		];
		Host->AddSlot().AutoHeight()[Shown(Identify, Choice(Ctx, LOCTEXT("PlSensitivity", "Sensitivity"), TEXT("sensitivity"), ZettaToolSettings::IsolateSensitivities(), true, TEXT("low")))];

		// The parts typed in: up to 16, each up to 80 characters, one of each (case aside).
		TSharedRef<SZettaPlText> Typing = SNew(SZettaPlText, nullptr, nullptr).MaxLength(80).Hint(LOCTEXT("PlBeltPouch", "belt pouch"));
		auto AddPart = [Ctx, Parts, Typing]()
		{
			const FString Label = Typing->GetText().TrimStartAndEnd();
			TArray<FString> List = Parts();
			if (Label.IsEmpty() || List.ContainsByPredicate([&Label](const FString& P) { return P.Equals(Label, ESearchCase::IgnoreCase); })) return;
			List.Add(Label);
			Typing->SetText(FString());
			if (List.Num() > ZettaToolSettings::IsolateMaxPartsLimit) List.SetNum(ZettaToolSettings::IsolateMaxPartsLimit);
			Ctx->Set(TEXT("extraParts"), JStrings(List));
		};
		Typing->OnEnter = AddPart;
		TSharedRef<SWrapBox> Chips = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4, 4));
		TWeakPtr<SWrapBox> WeakChips = Chips;
		Host->AddSlot().AutoHeight().Padding(0, 8, 0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1)
			[
				SNew(STextBlock).Font(FZettaStyle::Font(9, true)).Text_Lambda([Identify]() { return Identify() ? LOCTEXT("PlAlsoIsolate", "Also isolate") : LOCTEXT("PlPartsToIsolate", "Parts to isolate"); })
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).Font(FZettaStyle::Font(8)).ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text_Lambda([Parts]() { return FText::FromString(FString::Printf(TEXT("%d/%d"), Parts().Num(), ZettaToolSettings::IsolateMaxPartsLimit)); })
			]
		];
		Host->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1)[Typing]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)[ZettaPlUI::GhostButton(LOCTEXT("PlAdd", "+ Add"), FSimpleDelegate::CreateLambda(AddPart))]
		];
		// The chips follow the list as it changes.
		TSharedRef<FString> ChipsFor = MakeShared<FString>(TEXT("\x1"));
		Host->AddSlot().AutoHeight().Padding(0, 4)
		[
			SNew(SZettaPlTicker, [Ctx, Parts, WeakChips, ChipsFor]()
			{
				const TArray<FString> List = Parts();
				const FString Signature = FString::Join(List, TEXT("\n"));
				const TSharedPtr<SWrapBox> Box = WeakChips.Pin();
				if (!Box.IsValid() || Signature == *ChipsFor) return;
				*ChipsFor = Signature;
				Box->ClearChildren();
				for (const FString& Part : List)
				{
					Box->AddSlot()
					[
						SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Chip")).Padding(FMargin(8, 2, 2, 2))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Part))]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								ZettaPlUI::GhostButton(FText::FromString(TEXT("✕")), FSimpleDelegate::CreateLambda([Ctx, Parts, Part]()
								{
									TArray<FString> Next = Parts();
									Next.Remove(Part);
									Ctx->Set(TEXT("extraParts"), JStrings(Next));
								}))
							]
						]
					];
				}
			})
			[
				Chips
			]
		];
		Host->AddSlot().AutoHeight()
		[
			ZettaPlUI::Field(LOCTEXT("PlMaxParts", "Max parts"),
				SNew(SSpinBox<int32>).MinValue(1).MaxValue(ZettaToolSettings::IsolateMaxPartsLimit).MinSliderValue(1).MaxSliderValue(ZettaToolSettings::IsolateMaxPartsLimit)
				.Value_Lambda([Ctx]()
				{
					const TOptional<double> N = JNumber(Ctx->Settings, TEXT("maxParts"));
					return FMath::Clamp(N.IsSet() ? (int32)N.GetValue() : 6, 1, ZettaToolSettings::IsolateMaxPartsLimit);
				})
				.OnValueCommitted_Lambda([Ctx](int32 V, ETextCommit::Type) { Ctx->Set(TEXT("maxParts"), JNum(FMath::Clamp(V, 1, 16))); }),
				LOCTEXT("PlMaxPartsAside", "Caps what one run isolates"))
		];
		Host->AddSlot().AutoHeight()[Choice(Ctx, LOCTEXT("PlPose", "Character pose"), TEXT("pose"), ZettaToolSettings::IsolatePoses(), false, TEXT("t"))];
		Host->AddSlot().AutoHeight()[Hint([]() { return FString(TEXT("Humanoids only, with their hands emptied. Anything else keeps its pose and loses what was attached.")); })];
		Host->AddSlot().AutoHeight()[ImageEngine(Ctx)];
		Host->AddSlot().AutoHeight().Padding(0, 6)[ZettaPlUI::Note(LOCTEXT("PlIsolateNote", "Wire Character on to its own chain, and Each part to a chain every part runs through. After a run, each part it found gets its own output too."))];
	}

	static bool Denied(const TOptional<bool>& Gate) { return Gate.IsSet() && !Gate.GetValue(); }

	static void Retopology(const TSharedRef<SVerticalBox>& Host, const FCtx& Ctx)
	{
		auto Quads = [Ctx]() { return Ctx->Str(TEXT("polygonType"), TEXT("triangle")) == TEXT("quadrilateral"); };
		auto R1 = [Ctx]() { return Ctx->Str(TEXT("engine"), TEXT("r1")) == TEXT("r1"); };
		auto QuadsAllowed = []() { const FZettaEntitlements3D* G = FZettaPipelinePlan::Get().Gates(); return !(G && Denied(G->AllowsQuadMesh)); };
		auto Bounds = [Quads]() { return ZettaToolSettings::RetopologyBounds(Quads() ? TEXT("quadrilateral") : TEXT("triangle")); };
		Host->AddSlot().AutoHeight()[Choice(Ctx, LOCTEXT("PlEngine", "Engine"), TEXT("engine"), ZettaToolSettings::RetopologyEngines(), true, TEXT("r1"))];
		Host->AddSlot().AutoHeight()[Choice(Ctx, LOCTEXT("PlPolygons", "Polygons"), TEXT("polygonType"), ZettaToolSettings::PolygonTypes(), false, TEXT("triangle"),
			[QuadsAllowed](const FString& V) { return V == TEXT("quadrilateral") && !QuadsAllowed() ? FString(TEXT("Quad meshes are available on paid plans.")) : FString(); })];
		Host->AddSlot().AutoHeight()[Hint([QuadsAllowed]() { return QuadsAllowed() ? FString() : FString(TEXT("Quad meshes are available on paid plans.")); })];

		TSharedRef<SVerticalBox> R1Box = SNew(SVerticalBox);
		R1Box->AddSlot().AutoHeight().Padding(0, 6, 0, 3)
		[
			SNew(STextBlock).Font(FZettaStyle::Font(9, true)).Text_Lambda([Bounds]()
			{
				const FInt32Range B = Bounds();
				return FText::FromString(FString::Printf(TEXT("Polygon count (%s–%s)"), *JCount(B.GetLowerBoundValue()), *JCount(B.GetUpperBoundValue() - 1)));
			})
		];
		R1Box->AddSlot().AutoHeight()
		[
			SNew(SZettaPlText, [Ctx]()
			{
				const FVal Faces = ZettaPJson::JGet(Ctx->Settings, TEXT("faces"));
				return JIsNumber(Faces) ? FString::Printf(TEXT("%lld"), (int64)Faces->AsNumber()) : JAsText(Faces);
			}, [Ctx](const FString& Typed)
			{
				// A whole number is stored as one; anything else as typed — and named out of range.
				const FString T = Typed.TrimStartAndEnd();
				if (!T.IsEmpty() && T.IsNumeric() && !T.Contains(TEXT("."))) Ctx->Set(TEXT("faces"), JNum((double)FCString::Atoi64(*T)));
				else Ctx->Set(TEXT("faces"), JStr(Typed));
			}).MaxLength(12).Hint(LOCTEXT("PlPolygonCount", "Polygon count"))
		];
		R1Box->AddSlot().AutoHeight()
		[
			ZettaUI::ErrorText(MakeAttributeLambda([Ctx, Bounds, Quads]()
			{
				if (!ZettaToolSettings::RetopologyOutOfRange(Ctx->Settings)) return FText();
				const FInt32Range B = Bounds();
				return FText::FromString(FString::Printf(TEXT("Out of range — %s to %s%s."), *JCount(B.GetLowerBoundValue()), *JCount(B.GetUpperBoundValue() - 1),
					Quads() ? TEXT(", as quads allow half as many") : TEXT("")));
			}))
		];
		R1Box->AddSlot().AutoHeight()[Hint([Quads]()
		{
			return Quads() ? FString(TEXT("Quads halve the ceiling. Zetta R1 aims for your count, then settles on the cleanest topology near it."))
				: FString(TEXT("Zetta R1 aims for this count, then settles on the cleanest topology near it."));
		})];
		R1Box->AddSlot().AutoHeight().Padding(0, 4)[Check(Ctx, LOCTEXT("PlBakeRetopo", "Bake textures onto the rebuilt mesh"), TEXT("bake"), true,
			LOCTEXT("PlBakeRetopoTip", "Carries the original colours over to the new mesh. Off returns bare geometry."))];
		Host->AddSlot().AutoHeight()[Shown(R1, R1Box)];
		Host->AddSlot().AutoHeight()[Shown([R1]() { return !R1(); }, Choice(Ctx, LOCTEXT("PlDetail", "Level of detail"), TEXT("faceLevel"), ZettaToolSettings::FaceLevels(), true, TEXT("medium")))];
	}

	static void Segmentation(const TSharedRef<SVerticalBox>& Host, const FCtx& Ctx)
	{
		auto Smart = [Ctx]() { return Ctx->Str(TEXT("mode"), TEXT("segment")) == TEXT("smart"); };
		Host->AddSlot().AutoHeight()[Choice(Ctx, LOCTEXT("PlSegType", "Segmentation type"), TEXT("mode"), ZettaToolSettings::SegmentModes(), true, TEXT("segment"))];
		Host->AddSlot().AutoHeight()[Choice(Ctx, LOCTEXT("PlSegHowMany", "How many parts"), TEXT("granularity"), ZettaToolSettings::SegmentGranularities(), true, TEXT("balanced"))];
		Host->AddSlot().AutoHeight()[Shown([Smart]() { return !Smart(); }, Check(Ctx, LOCTEXT("PlSplitConn", "Also split pieces that are not joined"), TEXT("splitByConnectivity"), true,
			LOCTEXT("PlSplitConnTip", "Pieces that do not touch each other become separate parts.")))];
		TSharedRef<SVerticalBox> SmartBox = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Words(Ctx, LOCTEXT("PlWhatIsThis", "What is this? (Optional)"), TEXT("hint"), LOCTEXT("PlWhatIsThisHint", "e.g. game character with sword and armor"), 1024)]
			+ SVerticalBox::Slot().AutoHeight()[Hint([]() { return FString(TEXT("Naming the parts helps it find them.")); })];
		Host->AddSlot().AutoHeight()[Shown(Smart, SmartBox)];
	}

	/** A run's own reference picture (uploaded or from the user's images), or its description. */
	static void TextureInput(const TSharedRef<SVerticalBox>& Host, const FCtx& Ctx)
	{
		auto Reference = [Ctx]() { return JObject(Ctx->Settings, TEXT("reference")); };
		TSharedRef<FString> Status = MakeShared<FString>();
		TSharedRef<FString> Error = MakeShared<FString>();
		auto Busy = [Status]() { return !Status->IsEmpty(); };
		TSharedRef<SWidget> Row = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SZettaPlThumb).Size(FVector2D(64, 64)).Url_Lambda([Reference]() { return JTextOr(Reference(), TEXT("thumbnail")); }).Placeholder(LOCTEXT("PlRef", "Ref"))
			]
			+ SHorizontalBox::Slot().FillWidth(1).Padding(8, 0, 0, 0)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground()).Text_Lambda([Reference]()
					{
						const FObj R = Reference();
						return R.IsValid() ? FText::FromString(JTextOr(R, TEXT("name"), TEXT("Your picture"))) : LOCTEXT("PlRefOptional", "Optional — or describe the texture below.");
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						ZettaPlUI::SmallButton(LOCTEXT("PlUploadRef", "Upload"), FSimpleDelegate::CreateLambda([Ctx, Status, Error]()
						{
							const FString Path = ZettaPlUI::PickPicture(TEXT("Choose a reference picture"));
							if (Path.IsEmpty()) return;
							TArray<uint8> Bytes;
							FString Type;
							*Error = ZettaUI::ReadImageFile(Path, Bytes, Type);
							if (!Error->IsEmpty()) return;
							*Status = TEXT("Uploading…");
							FZettaPipelineSeed::FromUpload(Bytes, Type, FPaths::GetCleanFilename(Path), [Ctx, Status, Error](const FZettaResponse& R, const FZettaPipelineSeed& Seed)
							{
								Status->Reset();
								if (!R.bOk)
								{
									*Error = ZettaCatalog::Explain(R);
									return;
								}
								TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
								Ref->SetStringField(TEXT("uploadKey"), JTextOr(Seed.Request, TEXT("uploadKey")));
								Ref->SetStringField(TEXT("name"), Seed.Name);
								const FString Thumb = Seed.ThumbnailDataUrl();
								if (!Thumb.IsEmpty()) Ref->SetStringField(TEXT("thumbnail"), Thumb);
								Ctx->Set(TEXT("reference"), JObj(Ref));
							});
						}), MakeAttributeLambda([Busy]() { return !Busy(); }))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
					[
						ZettaPlUI::SmallButton(LOCTEXT("PlFromImages", "From your images"), FSimpleDelegate::CreateLambda([Ctx]()
						{
							ZettaPlPickers::PickImages(Ctx->Overlay, false, nullptr, [Ctx](const FZettaPickedImage& Picked)
							{
								FZettaPipelineSeed::ThumbnailFromUrl(Picked.Url, [Ctx, Picked](const TArray<uint8>& Preview)
								{
									TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
									Ref->SetNumberField(TEXT("gen2dPostId"), (double)Picked.Id);
									Ref->SetStringField(TEXT("name"), Picked.Name);
									if (Preview.Num() > 0) Ref->SetStringField(TEXT("thumbnail"), FZettaPipelineSeed::DataUrl(Preview));
									else Ref->SetField(TEXT("thumbnail"), JNull());
									Ctx->Set(TEXT("reference"), JObj(Ref));
								});
								return true;
							});
						}))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
					[
						Shown([Reference]() { return Reference().IsValid(); }, ZettaPlUI::GhostButton(FText::FromString(TEXT("✕")),
							FSimpleDelegate::CreateLambda([Ctx]() { Ctx->Set(TEXT("reference"), JNull()); }), true, LOCTEXT("PlRemoveRef", "Remove the reference picture")))
					]
				]
			];
		Host->AddSlot().AutoHeight()[ZettaPlUI::Field(LOCTEXT("PlRefPicture", "Reference picture"), Row)];
		Host->AddSlot().AutoHeight()[Hint([Status]() { return *Status; })];
		Host->AddSlot().AutoHeight()[ZettaUI::ErrorText(MakeAttributeLambda([Error]() { return FText::FromString(*Error); }))];
		Host->AddSlot().AutoHeight()[Shown([Reference]() { return !Reference().IsValid(); },
			Words(Ctx, LOCTEXT("PlDescribeTexture", "Describe the texture"), TEXT("prompt"), LOCTEXT("PlDescribeTextureHint", "Weathered bronze with green patina"), 1000))];
	}

	static void Texture(const TSharedRef<SVerticalBox>& Host, const FCtx& Ctx, bool bMultiView)
	{
		if (!bMultiView)
		{
			// Always given in each run: the pipeline itself never holds the description or the picture.
			if (!Ctx->bInRun) Host->AddSlot().AutoHeight()[GivenInEachRun(TEXT("texture’s description or reference picture"))];
			else TextureInput(Host, Ctx);
		}
		else
		{
			auto Wired = [Ctx]() { const FZettaPipelineNode* S = ZettaGraphRules::SourceOf(Ctx->Graph, Ctx->Node.Id, TEXT("views")); return S ? S->Kind : FString(); };
			Host->AddSlot().AutoHeight()[ZettaPlUI::Note(MakeAttributeLambda([Wired]()
			{
				const FString W = Wired();
				if (W.IsEmpty()) return FText();
				return W == ZettaNodeKinds::SeedViews
					? LOCTEXT("PlMvTexSeed", "Textured from the pictures of each side the Multi-view seed gives in each run — the same ones the model was made from, when it was.")
					: LOCTEXT("PlMvTexDraw", "Textured from the front and the back and sides Draw views draws.");
			}))];
			if (!Ctx->bInRun) Host->AddSlot().AutoHeight().Padding(0, 4)[Shown([Wired]() { return Wired().IsEmpty(); }, GivenInEachRun(TEXT("set of pictures of each side")))];
			else
			{
				Host->AddSlot().AutoHeight()
				[
					Shown([Wired]() { return Wired().IsEmpty(); },
						SNew(SZettaViewSlots, Ctx->Overlay, [Ctx]() { return JObject(Ctx->Settings, TEXT("referenceViews")); },
							[Ctx](SZettaViewSlots::FUpdate Update)
							{
								const FObj Current = JObject(Ctx->Settings, TEXT("referenceViews"));
								Ctx->Set(TEXT("referenceViews"), JObj(Update(Current.IsValid() ? Current.ToSharedRef() : MakeShared<FJsonObject>())));
							}))
				];
			}
		}

		Host->AddSlot().AutoHeight()
		[
			Choice(Ctx, LOCTEXT("PlTexQuality", "Texture quality"), TEXT("tier"), ZettaToolSettings::TextureTiers(), false, ZettaToolSettings::Retexture)
		];
		Host->AddSlot().AutoHeight()[Hint([Ctx]()
		{
			const FString Tier = Ctx->Str(TEXT("tier"), ZettaToolSettings::Retexture);
			const FZettaSettingOption* O = ZettaToolSettings::Find(ZettaToolSettings::TextureTiers(), Tier);
			const FZettaPriceList* Prices = FZettaPipelinePlan::Get().Prices();
			const TOptional<int32> Price = Prices ? Prices->Price(Tier, FString()) : TOptional<int32>();
			return FString::Printf(TEXT("Up to %s resolution%s"), O ? *O->Hint : TEXT("2K"), Price.IsSet() ? *FString::Printf(TEXT(" (%d credits)"), Price.GetValue()) : TEXT(""));
		})];
		Host->AddSlot().AutoHeight()[Shown([Ctx]() { return Ctx->Str(TEXT("tier"), ZettaToolSettings::Retexture) == ZettaToolSettings::Retexture; },
			Check(Ctx, LOCTEXT("PlFast", "Faster, at some detail — same price"), TEXT("fast"), false))];
		const TArray<FZettaSettingOption>& Fits = ZettaToolSettings::TextureAlignments();
		Host->AddSlot().AutoHeight()[Choice(Ctx, LOCTEXT("PlTexFit", "Texture fit"), TEXT("alignment"), { Fits[1], Fits[0] }, false, TEXT("original_image"))];
		Host->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[Check(Ctx, LOCTEXT("PlPbrMaterials", "PBR materials"), TEXT("pbr"), true, LOCTEXT("PlPbrTip", "Roughness, metalness and normal maps as well as colour."))];
		Host->AddSlot().AutoHeight()[Check(Ctx, LOCTEXT("PlDelightRef", "Remove the reference's baked-in lighting"), TEXT("delight"), true, LOCTEXT("PlDelightRefTip", "So the texture lights correctly in your own scene."))];
		Host->AddSlot().AutoHeight()[Check(Ctx, LOCTEXT("PlBakeEffects", "Bake effects into the textures"), TEXT("bake"), true)];
		Host->AddSlot().AutoHeight()[Check(Ctx, LOCTEXT("PlRemoveBgRef", "Remove background before applying"), TEXT("removeBg"), true)];
	}

	static void SmartUv(const TSharedRef<SVerticalBox>& Host, const FCtx& Ctx)
	{
		Host->AddSlot().AutoHeight()[Choice(Ctx, LOCTEXT("PlOrientation", "Orientation"), TEXT("orientation"), ZettaToolSettings::SmartUvOrientations(), false, TEXT("as-is"))];
		Host->AddSlot().AutoHeight()[Hint([]() { return FString(TEXT("Which way the model faces in the exported file.")); })];
		Host->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[Check(Ctx, LOCTEXT("PlBakeTextures", "Bake textures"), TEXT("bake"), true)];
		Host->AddSlot().AutoHeight()[Hint([Ctx]()
		{
			return Ctx->Bool(TEXT("bake"), true) ? FString(TEXT("Keeps the textures, baked onto the new layout.")) : FString(TEXT("Returns the geometry with its new UVs only — no textures."));
		})];
		Host->AddSlot().AutoHeight()[Check(Ctx, LOCTEXT("PlCompress", "Compress geometry"), TEXT("compress"), false)];
		Host->AddSlot().AutoHeight()[Hint([]() { return FString(TEXT("A smaller file, through mesh compression.")); })];
		Host->AddSlot().AutoHeight().Padding(0, 6)[Hint([]() { return FString::Printf(TEXT("Takes models up to %s triangles."), *JCount(ZettaToolSettings::SmartUvMaxTriangles)); })];
	}

	static void Rigging(const TSharedRef<SVerticalBox>& Host, const FCtx& Ctx)
	{
		auto Rig = [Ctx]() { return Ctx->Str(TEXT("rigType"), TEXT("biped")); };
		auto Spec = [Ctx]() { return Ctx->Str(TEXT("spec"), TEXT("standard")); };
		Host->AddSlot().AutoHeight()
		[
			ZettaPlUI::Field(LOCTEXT("PlBody", "Body"), ZettaPlUI::Dropdown(ZettaPlUI::Options(ZettaToolSettings::RigTypes()), Rig, [Ctx](const FString& V) { Ctx->Set(TEXT("rigType"), JStr(V)); }))
		];
		Host->AddSlot().AutoHeight()[Hint([Rig]() { const FZettaSettingOption* O = ZettaToolSettings::Find(ZettaToolSettings::RigTypes(), Rig()); return O ? O->Hint : FString(); })];
		// A drop-down: six skeletons do not fit a segmented row. Held, with a hint, on any body but a humanoid.
		Host->AddSlot().AutoHeight()
		[
			ZettaPlUI::Field(LOCTEXT("PlBoneNaming", "Bone naming"), ZettaPlUI::Dropdown(ZettaPlUI::Options(ZettaToolSettings::RigSpecs()), Spec,
				[Ctx](const FString& V) { Ctx->Set(TEXT("spec"), JStr(V)); }, MakeAttributeLambda([Rig]() { return Rig() == TEXT("biped"); })))
		];
		Host->AddSlot().AutoHeight()[Hint([Rig, Spec]()
		{
			if (Rig() != TEXT("biped")) return FString(TEXT("Skeleton presets apply to humanoids."));
			const FZettaSettingOption* O = ZettaToolSettings::Find(ZettaToolSettings::RigSpecs(), Spec());
			return O ? O->Hint : FString();
		})];
		Host->AddSlot().AutoHeight()[Choice(Ctx, LOCTEXT("PlRiggedFile", "Rigged file"), TEXT("outFormat"), ZettaToolSettings::RigOutFormats(), false, TEXT("glb"))];
		Host->AddSlot().AutoHeight()[Hint([Ctx]()
		{
			return Ctx->Str(TEXT("outFormat"), TEXT("glb")) == TEXT("fbx")
				? FString(TEXT("FBX imports into Unreal with its skeleton — the usual choice for retargeting onto the UE5 Mannequin."))
				: FString(TEXT("GLB imports with Unreal's glTF importer, skeleton included. For retargeting, FBX is the usual choice."));
		})];
	}

	/** The engines' blurbs as the Studio's Generate form writes them. */
	static FString EngineBlurb(const FString& Model)
	{
		if (Model == TEXT("n1")) return TEXT("Base Model");
		if (Model == TEXT("n25")) return TEXT("Highest quality of mesh and texture");
		if (Model == TEXT("np1")) return TEXT("Clean topology, Balanced quality");
		if (Model == TEXT("np15")) return TEXT("Clean topology – Quad Native");
		return ZettaCatalog::Model(Model).Blurb;
	}

	/**
	 * Generate 3D and Generate Smart 3D as a node: the Studio's Generate form whole — the tab's engines, every parameter
	 * with the plan's locks, the title, and Remove background — without Control Mode. Every change goes through the
	 * form's applyFormPatch, so a coupled option settles exactly as the Studio's does.
	 */
	static void Generate(const TSharedRef<SVerticalBox>& Host, const FCtx& Ctx)
	{
		auto Model = [Ctx]() { return Ctx->Str(TEXT("model"), ZettaGenerateForm::ModelsFor(Ctx->Node.Kind)[0]); };
		auto Caps = [Model]() -> const ZettaCatalog::FModelInfo& { return ZettaCatalog::Model(Model()); };
		auto Gates = []() { return FZettaPipelinePlan::Get().Gates(); };
		auto Price = [Model](const FString& Code)
		{
			const FZettaPriceList* Prices = FZettaPipelinePlan::Get().Prices();
			const TOptional<int32> P = Prices ? Prices->Price(Code, Model()) : TOptional<int32>();
			return !P.IsSet() ? FString() : P.GetValue() == 0 ? FString(TEXT("Free")) : FString::Printf(TEXT("+%d"), P.GetValue());
		};
		auto Quality = [Ctx]() { return Ctx->Str(TEXT("quality"), TEXT("high-res")); };
		auto Textured = [Ctx]() { return Ctx->Str(TEXT("output"), TEXT("textured")) == TEXT("textured"); };
		auto Segment = [Ctx]() { return Ctx->Bool(TEXT("segmentParts")); };
		auto Rungs = [Model]() { return Model() == TEXT("n1") || Model() == TEXT("n25"); };
		auto MayChoose = [Gates]() { const FZettaEntitlements3D* G = Gates(); return !(G && Denied(G->AllowsCustomFaceCount)); };

		// A multi-view step says where its pictures come from — or what to connect.
		if (ZettaNodeCatalog::IsMultiViewGenerate(Ctx->Node.Kind))
		{
			Host->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[ZettaPlUI::Note(MakeAttributeLambda([Ctx]()
			{
				const FZettaPipelineNode* S = ZettaGraphRules::SourceOf(Ctx->Graph, Ctx->Node.Id);
				const FString W = S ? S->Kind : FString();
				return W == ZettaNodeKinds::SeedViews ? LOCTEXT("PlMvFromSeed", "Made from the pictures of each side the Multi-view seed gives in each run — the front, and the back, left and right as given.")
					: W == ZettaNodeKinds::DrawViews ? LOCTEXT("PlMvFromDraw", "Made from the front and the back and sides Draw views draws from it.")
					: LOCTEXT("PlMvConnect", "Connect a Multi-view seed (pictures of each side) or Draw views.");
			}))];
		}
		Host->AddSlot().AutoHeight()[Words(Ctx, LOCTEXT("PlTitle", "Title"), TEXT("title"), LOCTEXT("PlTitleHint", "Name this generation"), 255, false)];
		Host->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[Check(Ctx, LOCTEXT("PlRemoveBg", "Remove background"), TEXT("removeBackground"), true,
			LOCTEXT("PlRemoveBgTip", "The background is removed from the picture coming in before the model is made from it."))];
		Host->AddSlot().AutoHeight()[Shown([Caps]() { return Caps().bImageTuning; }, Check(Ctx, LOCTEXT("PlAutofix", "Enhance input image  · Free"), TEXT("autofixImage"), false,
			LOCTEXT("PlAutofixTip", "Cleans up the picture before the model is built from it.")))];

		// Generation engine.
		TArray<FZettaPlOption> Engines;
		for (const FString& Id : ZettaGenerateForm::ModelsFor(Ctx->Node.Kind))
			Engines.Add({ Id, ZettaCatalog::Model(Id).Label + (Id == TEXT("np15") ? TEXT(" · New") : TEXT("")), EngineBlurb(Id) });
		Host->AddSlot().AutoHeight()
		[
			ZettaPlUI::Field(LOCTEXT("PlGenEngine", "Generation Engine"), ZettaPlUI::Segmented(Engines, Model, [Ctx](const FString& V) { Ctx->Set(TEXT("model"), JStr(V)); },
				[Gates](const FString& Id)
				{
					const FZettaEntitlements3D* G = Gates();
					if (!G) return FString();
					if (Id == TEXT("n25") && Denied(G->AllowsZettaN25)) return FString(TEXT("Zetta N2.5 is available on a paid plan."));
					if ((Id == TEXT("np1") || Id == TEXT("np15")) && Denied(G->AllowsZettaNp1)) return FString(TEXT("Available on a paid plan."));
					return FString();
				}))
		];
		Host->AddSlot().AutoHeight()[Hint([Model]() { return EngineBlurb(Model()); })];

		Host->AddSlot().AutoHeight()
		[
			ZettaPlUI::Field(LOCTEXT("PlOutput", "Output"), ZettaPlUI::Segmented({ { TEXT("geometry"), TEXT("Geometry"), FString() }, { TEXT("textured"), TEXT("Textured"), FString() } },
				[Ctx]() { return Ctx->Str(TEXT("output"), TEXT("textured")); }, [Ctx](const FString& V) { Ctx->Set(TEXT("output"), JStr(V)); },
				[Quality, Rungs, Model, Segment](const FString& V)
				{
					if (V == TEXT("geometry") && Quality() == TEXT("low-poly") && Rungs() && Model() != TEXT("n25")) return FString(TEXT("Not available at this mesh quality."));
					if (V == TEXT("textured") && Segment()) return FString(TEXT("A segmented mesh comes out untextured. Turn off Segment first."));
					return FString();
				}))
		];
		Host->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
		[
			Shown([Caps, Quality]() { return Caps().bParts && Quality() != TEXT("low-poly"); },
				Check(Ctx, MakeAttributeLambda([Price]() { const FString P = Price(TEXT("generate_parts")); return FText::FromString(TEXT("Segmented mesh") + (P.IsEmpty() ? FString() : TEXT("  · ") + P)); }),
					TEXT("segmentParts"), false, LOCTEXT("PlSegmentTip", "Split mesh to separate parts")))
		];

		// Quality: the rungs (N1, N2.5), with the plan's locks.
		auto QualityReason = [Gates, Caps](const FString& Q)
		{
			const FZettaEntitlements3D* G = Gates();
			if (G && Denied(G->AllowsCustomFaceCount)) return FString(TEXT("Your plan generates at the default quality — 500,000 polygons. Choosing a quality is available on a paid plan."));
			if (Q == TEXT("ultra") && G && G->MaxFaceCount.IsSet() && G->MaxFaceCount.GetValue() < 1000000) return FString(TEXT("Ultra detail is available on paid plans."));
			if (Q == TEXT("low-poly") && G && Denied(G->AllowsLowPoly)) return FString(TEXT("Low-poly generation is available on a paid plan."));
			if (Q == TEXT("low-poly") && !Caps().bLowPoly) return FString(TEXT("Low-poly generation needs Zetta N1."));
			return FString();
		};
		TArray<FZettaPlOption> QualitiesN1, QualitiesN25;
		for (const ZettaCatalog::FChoice& C : ZettaCatalog::Qualities()) QualitiesN1.Add({ C.Key, C.Label, C.Hint });
		QualitiesN25 = { { TEXT("low-poly"), TEXT("Low-poly"), FString() }, { TEXT("high-res"), TEXT("High"), FString() }, { TEXT("ultra"), TEXT("Ultra"), FString() } };
		auto SetQuality = [Ctx](const FString& V) { Ctx->Set(TEXT("quality"), JStr(V)); };
		Host->AddSlot().AutoHeight()
		[
			Shown(Rungs, SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 3)
				[
					SNew(STextBlock).Font(FZettaStyle::Font(9, true)).Text_Lambda([Model]() { return Model() == TEXT("n25") ? LOCTEXT("PlMeshQuality", "Mesh quality") : LOCTEXT("PlQuality", "Quality"); })
				]
				+ SVerticalBox::Slot().AutoHeight()[Shown([Model]() { return Model() != TEXT("n25"); }, ZettaPlUI::Segmented(QualitiesN1, Quality, SetQuality, QualityReason))]
				+ SVerticalBox::Slot().AutoHeight()[Shown([Model]() { return Model() == TEXT("n25"); }, ZettaPlUI::Segmented(QualitiesN25, Quality, SetQuality, QualityReason))]
				+ SVerticalBox::Slot().AutoHeight()[Hint([QualityReason, Quality]()
				{
					FString Shown = QualityReason(Quality());
					if (Shown.IsEmpty()) for (const TCHAR* Q : { TEXT("low-poly"), TEXT("high-res"), TEXT("ultra") }) if (Shown.IsEmpty()) Shown = QualityReason(Q);
					return Shown;
				})])
		];

		// Polygon control.
		TSharedRef<SVerticalBox> Polygons = SNew(SVerticalBox);
		Polygons->AddSlot().AutoHeight().Padding(0, 4, 0, 3)
		[
			SNew(STextBlock).Font(FZettaStyle::Font(9, true)).Text_Lambda([Ctx, MayChoose, Price]()
			{
				const FString P = ZettaGenerateForm::HasCustomPolygonCount(Ctx->Settings, MayChoose()) ? Price(TEXT("custom_face_count")) : FString();
				return FText::FromString(TEXT("Polygon count") + (P.IsEmpty() ? FString() : TEXT("  · ") + P));
			})
		];
		Polygons->AddSlot().AutoHeight()
		[
			Shown(MayChoose,
				SNew(SZettaPlText, [Ctx]() { return ZettaGenerateForm::FieldText(Ctx->Settings, TEXT("polygonCount")); }, [Ctx](const FString& Typed)
				{
					// Free text: a number the engine refuses stays as typed and is named out of range, never clamped.
					TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
					P->SetStringField(TEXT("polygonCount"), Typed);
					P->SetBoolField(TEXT("polygonCountTyped"), true);
					if (Ctx->Patch) Ctx->Patch(P);
				}).MaxLength(12).Hint(LOCTEXT("PlEngineDefault", "The engine's default")))
		];
		Polygons->AddSlot().AutoHeight()
		[
			ZettaUI::ErrorText(MakeAttributeLambda([Ctx, MayChoose]()
			{
				if (!MayChoose()) return FText();
				const TArray<FString> Problems = ZettaGenerateForm::ParameterProblems(Ctx->Settings, true);
				if (Problems.Num() == 0) return FText();
				if (Problems[0].Contains(TEXT("whole number"))) return LOCTEXT("PlWhole", "Out of range — a whole number, please.");
				const FInt32Range B = ZettaGenerateForm::PolygonBounds(Ctx->Settings);
				return FText::FromString(FString::Printf(TEXT("Out of range — %s to %s."), *JCount(B.GetLowerBoundValue()), *JCount(B.GetUpperBoundValue() - 1)));
			}))
		];
		Polygons->AddSlot().AutoHeight()[Hint([MayChoose]() { return MayChoose() ? FString() : FString(TEXT("Model's default face count (choosing a count is available on a paid plan)")); })];
		Polygons->AddSlot().AutoHeight()[Hint([Ctx, MayChoose, Model]()
		{
			if (!MayChoose() || Model() == TEXT("n1")) return FString();
			const FInt32Range B = ZettaGenerateForm::PolygonBounds(Ctx->Settings);
			return FString::Printf(TEXT("Minimum %s, up to %s"), *JCount(B.GetLowerBoundValue()), *JCount(B.GetUpperBoundValue() - 1));
		})];
		auto PolygonsReason = [Quality, Caps, Segment, Gates](const FString& V)
		{
			if (Quality() != TEXT("low-poly") && !Caps().bQuadAnyBase) return FString(TEXT("Not used at this mesh quality."));
			if (V == TEXT("quadrilateral"))
			{
				if (Segment()) return FString(TEXT("Not available while segmenting."));
				const FZettaEntitlements3D* G = Gates();
				if (G && Denied(G->AllowsQuadMesh)) return FString(TEXT("Quad meshes are available on paid plans."));
			}
			return FString();
		};
		Polygons->AddSlot().AutoHeight()
		[
			Shown([Caps]() { return Caps().bQuadAnyBase || Caps().bLowPoly; }, SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					ZettaPlUI::Field(LOCTEXT("PlPolygonsField", "Polygons"), ZettaPlUI::Segmented({ { TEXT("triangle"), TEXT("Triangles"), FString() }, { TEXT("quadrilateral"), TEXT("Quads"), FString() } },
						[Ctx]() { return Ctx->Str(TEXT("polygonType"), TEXT("triangle")); }, [Ctx](const FString& V) { Ctx->Set(TEXT("polygonType"), JStr(V)); }, PolygonsReason))
				]
				+ SVerticalBox::Slot().AutoHeight()[Hint([PolygonsReason]() { return PolygonsReason(TEXT("quadrilateral")); })])
		];
		Host->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(SExpandableArea).InitiallyCollapsed(true).AreaTitle(LOCTEXT("PlPolygonControl", "Polygon control")).BodyContent()[Polygons]
		];

		Host->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
		[
			Shown(Textured, Check(Ctx, MakeAttributeLambda([Ctx, Price]()
			{
				const FString P = Ctx->Bool(TEXT("pbrMaterials"), true) ? Price(TEXT("pbr")) : FString();
				return FText::FromString(TEXT("PBR") + (P.IsEmpty() ? FString() : TEXT("  · ") + P));
			}), TEXT("pbrMaterials"), true, LOCTEXT("PlPbrGenTip", "Physically based materials: roughness, metalness and normal maps.")))
		];

		// Texture quality.
		const TArray<FZettaPlOption> TexStd = { { TEXT("standard"), TEXT("Standard"), FString() }, { TEXT("detailed"), TEXT("Detailed"), FString() } };
		const TArray<FZettaPlOption> TexExt = { { TEXT("standard"), TEXT("Standard"), FString() }, { TEXT("detailed"), TEXT("Detailed"), FString() }, { TEXT("extreme"), TEXT("8K"), FString() } };
		auto TexValue = [Ctx]() { return Ctx->Str(TEXT("textureQuality"), TEXT("standard")); };
		auto TexSet = [Ctx](const FString& V) { Ctx->Set(TEXT("textureQuality"), JStr(V)); };
		Host->AddSlot().AutoHeight()
		[
			Shown([Caps, Textured]() { return Caps().bDetailedTexture && Textured(); }, SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 3)[SNew(STextBlock).Text(LOCTEXT("PlTexQualityGen", "Texture quality")).Font(FZettaStyle::Font(9, true))]
				+ SVerticalBox::Slot().AutoHeight()[Shown([Caps]() { return !Caps().bExtremeTexture; }, ZettaPlUI::Segmented(TexStd, TexValue, TexSet))]
				+ SVerticalBox::Slot().AutoHeight()[Shown([Caps]() { return Caps().bExtremeTexture; }, ZettaPlUI::Segmented(TexExt, TexValue, TexSet))]
				+ SVerticalBox::Slot().AutoHeight()[Hint([TexValue, Caps, Price]()
				{
					const FString V = TexValue();
					if (V == TEXT("detailed")) return FString::Printf(TEXT("Up to 4K resolution (%s)"), *Price(TEXT("detailed_texture")));
					if (V == TEXT("extreme") && Caps().bExtremeTexture) return FString::Printf(TEXT("Up to 8K resolution (%s)"), *Price(TEXT("extreme_texture")));
					return FString(TEXT("Up to 2K resolution (free)"));
				})])
		];

		// Visibility.
		auto PrivateAllowed = [Gates]() { const FZettaEntitlements3D* G = Gates(); return !(G && Denied(G->AllowsPrivateGenerations)); };
		Host->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
		[
			ZettaPlUI::Field(LOCTEXT("PlVisibility", "Visibility"), ZettaPlUI::Check(
				MakeAttributeLambda([PrivateAllowed]() { return PrivateAllowed() ? LOCTEXT("PlPrivate", "Keep this model private") : LOCTEXT("PlPrivatePaid", "Keep this model private (paid plan)"); }),
				[Ctx, Gates, PrivateAllowed]()
				{
					const FString A = Ctx->Str(TEXT("availability"));
					const FZettaEntitlements3D* G = Gates();
					const bool bOn = A.IsEmpty() ? (G && G->AllowsPrivateGenerations.IsSet() && G->AllowsPrivateGenerations.GetValue()) : A == TEXT("private");
					return PrivateAllowed() && bOn;
				},
				[Ctx](bool bOn) { Ctx->Set(TEXT("availability"), JStr(bOn ? TEXT("private") : TEXT("public"))); },
				LOCTEXT("PlPrivateTip", "Private models are yours alone. Public ones can appear in the community gallery."), MakeAttributeLambda(PrivateAllowed)))
		];

		// Advanced controls (the engines that take image tuning).
		TSharedRef<SVerticalBox> Advanced = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Check(Ctx, LOCTEXT("PlScale", "Real-world scale  · Free"), TEXT("realWorldScale"), false, LOCTEXT("PlScaleTip", "Sizes the model in metres, as the object would be."), MakeAttributeLambda(Textured))]
			+ SVerticalBox::Slot().AutoHeight()[Check(Ctx, LOCTEXT("PlSmaller", "Smaller file  · Free"), TEXT("compressGeometry"), false, LOCTEXT("PlSmallerTip", "Compresses the geometry for a lighter download."))]
			+ SVerticalBox::Slot().AutoHeight()[Shown(Textured, Check(Ctx, LOCTEXT("PlExportUv", "Export UV map  · Free"), TEXT("exportUv"), true, LOCTEXT("PlExportUvTip", "Off is faster and produces a smaller file, but the mesh cannot be re-textured.")))]
			+ SVerticalBox::Slot().AutoHeight()[Shown(Textured, Check(Ctx, LOCTEXT("PlDelight", "Remove baked-in lighting  · Free"), TEXT("delight"), false,
				LOCTEXT("PlDelightTip", "Removes the lighting baked into the reference image, so the texture lights correctly in your own scene. Uses our newest texture engine.")))]
			+ SVerticalBox::Slot().AutoHeight()[Shown(Textured, ZettaPlUI::Field(LOCTEXT("PlTexFitGen", "Texture fit"),
				ZettaPlUI::Segmented({ { TEXT("geometry"), TEXT("Fit texture to mesh"), FString() }, { TEXT("original_image"), TEXT("Fit texture to picture"), FString() } },
					[Ctx]() { return Ctx->Str(TEXT("textureAlignment"), TEXT("geometry")); }, [Ctx](const FString& V) { Ctx->Set(TEXT("textureAlignment"), JStr(V)); })))];
		Host->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
		[
			Shown([Caps]() { return Caps().bImageTuning; }, SNew(SExpandableArea).InitiallyCollapsed(true).AreaTitle(LOCTEXT("PlAdvanced", "Advanced controls")).BodyContent()[Advanced])
		];
	}

	TSharedRef<SWidget> Build(const FCtx& Ctx)
	{
		namespace K = ZettaNodeKinds;
		TSharedRef<SVerticalBox> Host = SNew(SVerticalBox);
		const FString& Kind = Ctx->Node.Kind;
		if (Kind == K::SeedViews)
		{
			Host->AddSlot().AutoHeight()[Para(LOCTEXT("PlSeedViewsText", "Each run gives its own pictures of each side — a front at least, and the back, left and right as you have them. Connect it to Multi-view - Generate 3D or Multi-view - Smart 3D and the model is made from all of them. Press New run to give the pictures; several sets make a run each."))];
		}
		else if (Kind == K::SeedPicture || Kind == K::SeedModel)
		{
			Host->AddSlot().AutoHeight()[Para(FText::FromString(FString::Printf(TEXT("%s is chosen each time you start a run — press New run and pick one, or several for a run each. Every run keeps its own seed and results in Runs."),
				Kind == K::SeedModel ? TEXT("The model") : TEXT("The picture"))))];
		}
		else if (Kind == K::RemoveBackground)
		{
			Host->AddSlot().AutoHeight()[Para(LOCTEXT("PlRemoveBgText", "Nothing to set. The subject is cut out of the picture coming in, as the Studio’s Remove background does."))];
		}
		else if (Kind == K::AiEdit) ZettaNodeForms::AiEdit(Host, Ctx);
		else if (Kind == K::DrawViews) ZettaNodeForms::DrawViews(Host, Ctx);
		else if (Kind == K::IsolateParts) Isolate(Host, Ctx);
		else if (ZettaNodeCatalog::IsGenerate(Kind)) Generate(Host, Ctx);
		else if (Kind == K::Retopology) ZettaNodeForms::Retopology(Host, Ctx);
		else if (Kind == K::Segmentation) ZettaNodeForms::Segmentation(Host, Ctx);
		else if (Kind == K::Texture) ZettaNodeForms::Texture(Host, Ctx, false);
		else if (Kind == K::TextureMultiview) ZettaNodeForms::Texture(Host, Ctx, true);
		else if (Kind == K::SmartUv) ZettaNodeForms::SmartUv(Host, Ctx);
		else if (Kind == K::Rigging) ZettaNodeForms::Rigging(Host, Ctx);
		return Host;
	}
}

// ─── Inspector ──────────────────────────────────────────────────────────────────────────────────────────────────────

void SZettaNodeInspector::Construct(const FArguments&, const FZettaPlOverlayRef& InOverlay)
{
	Overlay = InOverlay;
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(12, 10, 12, 6)
		[
			SNew(STextBlock).Font(FZettaStyle::Font(10, true)).Text_Lambda([this]() { return Heading; })
		]
		+ SVerticalBox::Slot().FillHeight(1)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot().Padding(12, 0, 12, 12)[SAssignNew(Body, SVerticalBox)]
		]
	];
	BuildOverview();
}

void SZettaNodeInspector::Show(const FZettaPipelineGraph& Graph, const FString& NodeId, const TArray<FZettaPipelineProblem>& Problems, int32 RunCount)
{
	const FZettaPipelineNode* Node = Graph.Node(NodeId);
	Runs = RunCount;
	if (!Node)
	{
		if (!bOverview) BuildOverview();
		Lead = Graph.Nodes.Num() == 0 ? TEXT("Start with a seed.") : TEXT("Select a step to set it up.");
		return;
	}
	TArray<FString> Own;
	for (const FZettaPipelineProblem& P : Problems) if (P.NodeId == Node->Id) Own.AddUnique(P.Message);
	Notes = FString::Join(Own, TEXT("\n"));
	NoteWarnings = ZettaGraphRules::Warnings(Graph, Node->Id);
	if (bOverview || ShownId != Node->Id || ShownKind != Node->Kind) BuildStep(*Node, Graph);
	else if (Form.IsValid()) Form->Refresh(*Node, Graph);
	ShownId = Node->Id;
	ShownKind = Node->Kind;
}

void SZettaNodeInspector::BuildOverview()
{
	bOverview = true;
	ShownId.Reset();
	ShownKind.Reset();
	Form.Reset();
	Heading = LOCTEXT("PlRecipe", "Recipe");
	Body->ClearChildren();
	auto Line = [this](const FText& Text, bool bMuted)
	{
		Body->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[ZettaNodeForms::Para(Text, bMuted)];
	};
	Body->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]() { return FText::FromString(Lead); })];
	Line(LOCTEXT("PlOverview1", "This is the recipe. The seed says what it starts from — a picture or a model; you choose which one each time you press New run, and can start several at once."), true);
	Line(LOCTEXT("PlOverview2", "Auto submits every step for you to the end, Manual waits for you before each one. Runs keep going on the server — close Unreal whenever you like."), true);
	Line(LOCTEXT("PlOverview3", "Every step makes a real post in your library, charged as the Studio charges it."), true);
	Body->AddSlot().AutoHeight().HAlign(HAlign_Left)
	[
		SNew(SBox).Visibility_Lambda([this]() { return Runs > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			ZettaPlUI::GhostButton(MakeAttributeLambda([this]() { return FText::FromString(FString::Printf(TEXT("See its %d run%s"), Runs, Runs == 1 ? TEXT("") : TEXT("s"))); }),
				FSimpleDelegate::CreateLambda([this]() { if (OnShowRuns) OnShowRuns(); }))
		]
	];
}

void SZettaNodeInspector::BuildStep(const FZettaPipelineNode& Node, const FZettaPipelineGraph& Graph)
{
	bOverview = false;
	Heading = LOCTEXT("PlStep", "Step");
	Body->ClearChildren();
	const FZettaNodeSpec* Spec = ZettaNodeCatalog::Spec(Node.Kind);
	const bool bNotHere = ZettaNodeCatalog::NotAvailableHere(Node.Kind);
	const FString Id = Node.Id;
	const FString KindLabel = Spec ? Spec->Label : Node.Kind;

	TSharedRef<FZettaFormContext> Ctx = MakeShared<FZettaFormContext>();
	Ctx->Overlay = Overlay;
	Ctx->Refresh(Node, Graph);
	TWeakPtr<SZettaNodeInspector> Weak = SharedThis(this);
	Ctx->Patch = [Weak, Id](const TSharedRef<FJsonObject>& Patch)
	{
		if (const TSharedPtr<SZettaNodeInspector> Self = Weak.Pin()) if (Self->OnPatch) Self->OnPatch(Id, Patch);
	};
	Form.Reset();
	if (!bNotHere) Form = Ctx;

	Body->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 2, 8, 0)[ZettaPlUI::HueIcon(Node.Kind, 28)]
		+ SHorizontalBox::Slot().FillWidth(1)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SZettaPlText, [Ctx]() { return Ctx->Node.Label.Get(FString()); }, [Weak, Id](const FString& Label)
				{
					if (const TSharedPtr<SZettaNodeInspector> Self = Weak.Pin()) if (Self->OnLabel) Self->OnLabel(Id, Label);
				}).MaxLength(80).Hint(FText::FromString(KindLabel)).IsEnabled(!bNotHere)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
			[
				ZettaUI::Hint(FText::FromString(Spec ? Spec->Blurb : FString(TEXT("This step is newer than this version of the plugin."))))
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(6, 0, 0, 0)
		[
			ZettaPlUI::GhostButton(LOCTEXT("PlRemove", "Remove"), FSimpleDelegate::CreateLambda([Weak, Id]()
			{
				if (const TSharedPtr<SZettaNodeInspector> Self = Weak.Pin()) if (Self->OnRemove) Self->OnRemove(Id);
			}), true, FText::FromString(TEXT("Remove ") + Node.DisplayName()))
		]
	];
	Body->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[ZettaPlUI::Note(MakeAttributeLambda([this]() { return FText::FromString(Notes); }), TEXT("error"))];
	Body->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[ZettaPlUI::Note(MakeAttributeLambda([this]() { return FText::FromString(FString::Join(NoteWarnings, TEXT("\n"))); }))];
	if (bNotHere) return;
	Body->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[ZettaNodeForms::Build(Ctx)];
}

#undef LOCTEXT_NAMESPACE
