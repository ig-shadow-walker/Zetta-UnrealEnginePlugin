// Copyright Zetta 3D. See LICENSE.md.

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ContentBrowserModule.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IContentBrowserSingleton.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "SZettaPipelineCanvas.h"
#include "SZettaPipelineEditor.h"
#include "Zetta3DEditorModule.h"
#include "ZettaCatalog.h"
#include "ZettaHttp.h"
#include "ZettaImporter.h"
#include "ZettaNodeForms.h"
#include "ZettaPipelineJson.h"
#include "ZettaPipelineServices.h"
#include "ZettaPipelineText.h"
#include "ZettaSettings.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

using namespace ZettaPJson;

// ─── What a step made ───────────────────────────────────────────────────────────────────────────────────────────────

/** What a step's results can ask the run to do. */
struct FZettaStepActions
{
	bool bBusy = false;
	int64 PipelineId = 0;
	TFunction<void(int64 /*StepId*/)> Run;
	/** (step, with the settings shown in the panel) */
	TFunction<void(int64, bool)> Retry;
	TFunction<void(int64, const TArray<FString>&, const TArray<FString>&)> ConfirmParts;
};

/** The boxes the identify pass drew on the picture, in fractions of it. */
class SZettaReviewBoxes : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaReviewBoxes) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&) {}
	TArray<FZettaIdentifiedPart> Parts;
	TFunction<bool(const FString&)> IsLit;
	FString Hover;

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
	{
		const FVector2D Size = Geometry.GetLocalSize();
		for (const FZettaIdentifiedPart& P : Parts)
		{
			if (P.Box.Num() != 4) continue;
			const bool bLit = Hover == P.Key || (IsLit && IsLit(P.Key));
			const FVector2D Min(P.Box[0] * Size.X, P.Box[1] * Size.Y);
			const FVector2D Max = Min + FVector2D(P.Box[2] * Size.X, P.Box[3] * Size.Y);
			const FLinearColor Colour = bLit ? FZettaStyle::Accent() : FLinearColor(1, 1, 1, 0.35f);
			const TArray<FVector2f> Lines = { FVector2f(Min), FVector2f(Max.X, Min.Y), FVector2f(Max), FVector2f(Min.X, Max.Y), FVector2f(Min) };
			FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), Lines, ESlateDrawEffect::None, Colour, true, Hover == P.Key ? 2.5f : 1.5f);
			if (bLit)
			{
				FSlateDrawElement::MakeText(Out, Layer + 1, Geometry.ToPaintGeometry(FVector2f(120, 14), FSlateLayoutTransform(FVector2f(Min + FVector2D(3, 2)))),
					P.Label, FZettaStyle::Font(8, true), ESlateDrawEffect::None, Colour);
			}
		}
		return Layer + 2;
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
};

namespace ZettaStepResults
{
	static TSharedRef<SWidget> Hint(const FText& Text) { return ZettaUI::Hint(Text); }

	/** Pictures a step made, each with Save to project. */
	static TSharedRef<SWidget> Pictures(const TArray<TPair<FString, FString>>& Items, const FString& NodeName)
	{
		TSharedRef<SWrapBox> Grid = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6, 6));
		for (const TPair<FString, FString>& Item : Items)
		{
			if (Item.Value.IsEmpty()) continue;
			const FString Url = Item.Value;
			const FString Name = FString::Printf(TEXT("%s %s"), *NodeName, *Item.Key);
			TSharedRef<bool> bSaving = MakeShared<bool>(false);
			Grid->AddSlot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SNew(SZettaPlThumb).Url(Url).Size(FVector2D(120, 120))]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[SNew(STextBlock).Text(FText::FromString(Item.Key)).Font(FZettaStyle::Font(8))]
				+ SVerticalBox::Slot().AutoHeight()
				[
					ZettaPlUI::GhostButton(LOCTEXT("PlSaveToProject", "Save to project"), FSimpleDelegate::CreateLambda([Url, Name, bSaving]()
					{
						*bSaving = true;
						FZettaHttp::Download(Url, [Name, bSaving](const FZettaResponse& R)
						{
							*bSaving = false;
							if (!R.bOk)
							{
								ZettaPlUI::Toast(TEXT("The picture could not be saved: ") + ZettaCatalog::Explain(R), true);
								return;
							}
							const FString Package = UZettaProjectSettings::Get()->GetImagesFolder() + TEXT("/") + ZettaImporter::Slug(Name);
							FString ObjectPath, Error;
							if (!ZettaImporter::SaveTexture(R.Bytes, Package, ObjectPath, Error))
							{
								ZettaPlUI::Toast(TEXT("The picture could not be saved: ") + Error, true);
								return;
							}
							FContentBrowserModule& Browser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
							if (IAssetRegistry* Registry = IAssetRegistry::Get())
							{
								const FAssetData Asset = Registry->GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
								if (Asset.IsValid()) Browser.Get().SyncBrowserToAssets({ Asset });
							}
							ZettaPlUI::Toast(TEXT("Saved to ") + ObjectPath);
						});
					}), MakeAttributeLambda([bSaving]() { return !*bSaving; }))
				]
			];
		}
		return Grid;
	}

	/** A model's properties, read from its imported asset. */
	static TSharedRef<SWidget> Properties(const FZettaGenerationPtr& Generation)
	{
		const FString Main = ZettaProjectIndex::MainAssetOf(Generation->Id);
		if (Main.IsEmpty()) return Hint(LOCTEXT("PlImportToRead", "Import the model to read its polygons and vertices."));
		TArray<TPair<FString, FString>> Rows;
		if (const UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Main))
		{
			Rows.Add({ TEXT("Polygons"), FString::Printf(TEXT("%s · %s"), *JCount(Mesh->GetNumTriangles(0)), Generation->PolygonType == TEXT("quadrilateral") ? TEXT("Quads") : TEXT("Triangles")) });
			Rows.Add({ TEXT("Vertices"), JCount(Mesh->GetNumVertices(0)) });
			Rows.Add({ TEXT("Materials"), JCount(Mesh->GetStaticMaterials().Num()) });
			Rows.Add({ TEXT("Rigged"), TEXT("No") });
		}
		else if (const USkeletalMesh* Skeletal = LoadObject<USkeletalMesh>(nullptr, *Main))
		{
			Rows.Add({ TEXT("Materials"), JCount(Skeletal->GetMaterials().Num()) });
			Rows.Add({ TEXT("Rigged"), TEXT("Yes") });
		}
		Rows.Add({ TEXT("Made by"), ZettaCatalog::FeatureLabel(Generation->Feature) });
		TSharedRef<SVerticalBox> Table = SNew(SVerticalBox);
		for (const TPair<FString, FString>& R : Rows)
		{
			Table->AddSlot().AutoHeight().Padding(0, 1)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(90)[SNew(STextBlock).Text(FText::FromString(R.Key)).ColorAndOpacity(FSlateColor::UseSubduedForeground())]]
				+ SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(FText::FromString(R.Value))]
			];
		}
		return Table;
	}

	static void ShowInBrowser(const FString& ObjectPath)
	{
		if (IAssetRegistry* Registry = IAssetRegistry::Get())
		{
			const FAssetData Asset = Registry->GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
			if (!Asset.IsValid()) return;
			FContentBrowserModule& Browser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
			Browser.Get().SyncBrowserToAssets({ Asset });
		}
	}

	/** The model a step made: its library row, and the Unreal way to use it — import, import and place. */
	static TSharedRef<SWidget> Model(const FZettaGenerationPtr& Generation, int64 PostId)
	{
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		Box->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SZettaPlThumb).Url(Generation.IsValid() ? Generation->ThumbnailUrl : FString()).Size(FVector2D(64, 64)).Placeholder(LOCTEXT("PlModelThumb", "Model"))]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(FZettaStyle::Font(9, true)).Text(FText::FromString(Generation.IsValid() ? Generation->DisplayTitle() : FString::Printf(TEXT("#%lld"), PostId)))]
				+ SVerticalBox::Slot().AutoHeight()[Hint(LOCTEXT("PlInLibrary", "Model · in your library"))]
			]
		];
		if (!Generation.IsValid() || !Generation->IsSuccess())
		{
			Box->AddSlot().AutoHeight().Padding(0, 4)[Hint(LOCTEXT("PlPreparing", "The model is still being prepared."))];
			return Box;
		}
		if (Generation->IsSplat())
		{
			Box->AddSlot().AutoHeight().Padding(0, 4)[Hint(FText::FromString(ZettaImporter::SplatUnsupported))];
			return Box;
		}
		struct FImportState
		{
			bool bImporting = false;
			float Progress = 0.f;
			FString Status;
		};
		TSharedRef<FImportState> State = MakeShared<FImportState>();
		const FZettaGenerationPtr Gen = Generation;
		auto InProject = [Gen]() { return ZettaProjectIndex::Contains(Gen->Id) && !ZettaProjectIndex::MainAssetOf(Gen->Id).IsEmpty(); };
		Box->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4, 4))
			+ SWrapBox::Slot()
			[
				SNew(SBox).Visibility_Lambda([InProject]() { return InProject() ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
					[
						SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Chip")).Padding(FMargin(8, 2))
						[
							SNew(STextBlock).Text(LOCTEXT("PlInProject", "In project")).ColorAndOpacity(FSlateColor(FZettaStyle::Success()))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
					[
						ZettaPlUI::SmallButton(LOCTEXT("PlShowInBrowser", "Show in Content Browser"), FSimpleDelegate::CreateLambda([Gen]() { ShowInBrowser(ZettaProjectIndex::MainAssetOf(Gen->Id)); }), true, FText(), true)
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						ZettaPlUI::SmallButton(LOCTEXT("PlPlaceInLevel", "Place in level"), FSimpleDelegate::CreateLambda([Gen]()
						{
							const FString Main = ZettaProjectIndex::MainAssetOf(Gen->Id);
							FZettaImportOptions Options;
							const TArray<AActor*> Placed = ZettaImporter::Place({ Main }, Gen->DisplayTitle(), Options);
							ZettaPlUI::Toast(Placed.Num() > 0 ? TEXT("Placed in the level.") : TEXT("It could not be placed."), Placed.Num() == 0);
						}))
					]
				]
			]
			+ SWrapBox::Slot()
			[
				SNew(SBox).Visibility_Lambda([InProject]() { return InProject() ? EVisibility::Collapsed : EVisibility::Visible; })
				[
					ZettaPlUI::SmallButton(LOCTEXT("PlImportIntoProject", "Import into project"), FSimpleDelegate::CreateLambda([Gen, State]()
					{
						State->bImporting = true;
						State->Progress = 0;
						FZettaImportOptions Options;
						Options.bPlace = UZettaProjectSettings::Get()->bPlaceInLevel;
						ZettaImporter::Import(Gen, Options, [State](const FZettaImportResult& Result)
						{
							State->bImporting = false;
							if (!Result.bOk)
							{
								ZettaPlUI::Toast(TEXT("The import failed: ") + Result.Error, true);
								return;
							}
							ZettaProjectIndex::Invalidate();
							if (Result.PlacedActors.Num() == 0) ShowInBrowser(Result.MainAssetPath());
							ZettaPlUI::Toast(Result.PlacedActors.Num() > 0 ? TEXT("Imported and placed in the level.") : TEXT("Imported to ") + Result.Folder);
						}, [State](const FString& Status, float Progress)
						{
							State->Status = Status;
							State->Progress = Progress;
						});
					}), MakeAttributeLambda([State]() { return !State->bImporting; }),
						LOCTEXT("PlImportTip", "Imports into your models folder — and places it in the level when Project Settings › Zetta 3D › Place in level is on."), true)
				]
			]
			+ SWrapBox::Slot()
			[
				ZettaPlUI::GhostButton(LOCTEXT("PlShowLibrary", "Show in My Generations"), FSimpleDelegate::CreateLambda([]() { FZetta3DEditorModule::Get().OpenWindow(TEXT("Library")); }))
			]
		];
		Box->AddSlot().AutoHeight().Padding(0, 4)
		[
			SNew(SVerticalBox)
			.Visibility_Lambda([State]() { return State->bImporting ? EVisibility::Visible : EVisibility::Collapsed; })
			+ SVerticalBox::Slot().AutoHeight()[SNew(SProgressBar).Percent_Lambda([State]() { return State->Progress; })]
			+ SVerticalBox::Slot().AutoHeight()[ZettaUI::Hint(MakeAttributeLambda([State]() { return FText::FromString(State->Status); }))]
		];
		Box->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Properties(Generation)];
		return Box;
	}

	/** ISOLATE PARTS, PAUSED FOR THE USER: what the identify pass found, boxed on the picture, the proposal ticked. */
	static TSharedRef<SWidget> IsolateReview(const FZettaPipelineRunStep& Step, const FZettaStepActions& Actions)
	{
		struct FState
		{
			TSet<FString> Chosen;
			TArray<FString> Typed;
		};
		TSharedRef<FState> State = MakeShared<FState>();
		const FZettaStepReview Review = Step.Review.Get(FZettaStepReview());
		for (const FString& K : Review.ProposedKeys) State->Chosen.Add(K);
		const int64 StepId = Step.Id;
		const bool bBusy = Actions.bBusy;

		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		Box->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(STextBlock).AutoWrapText(true).Text(FText::FromString((Review.Subject.IsEmpty() ? FString(TEXT("Choose the parts to isolate.")) : FString::Printf(TEXT("Found in %s:"), *Review.Subject))
				+ TEXT(" The character is always made, re-posed with its hands empty.")))
		];
		TSharedPtr<SZettaReviewBoxes> Boxes;
		if (!Review.Source.Url.IsEmpty())
		{
			Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, 0, 0, 6)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[SNew(SZettaPlThumb).Url(Review.Source.Url).Size(FVector2D(280, 280))]
				+ SOverlay::Slot()[SAssignNew(Boxes, SZettaReviewBoxes).Visibility(EVisibility::HitTestInvisible)]
			];
			Boxes->Parts = Review.Parts;
			Boxes->IsLit = [State](const FString& Key) { return State->Chosen.Contains(Key); };
		}
		if (Review.Parts.Num() > 0)
		{
			static const TMap<FString, FString> KindLabel = { { TEXT("held"), TEXT("Held") }, { TEXT("attached"), TEXT("Attached") }, { TEXT("detail"), TEXT("Part") } };
			for (const FZettaIdentifiedPart& Part : Review.Parts)
			{
				const FString Key = Part.Key;
				const FString* Kind = KindLabel.Find(Part.Kind);
				TWeakPtr<SZettaReviewBoxes> WeakBoxes = Boxes;
				Box->AddSlot().AutoHeight()
				[
					SNew(SBorder).BorderImage(FAppStyle::GetNoBrush()).Padding(0)
					.OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Unhandled(); })
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1)
						[
							ZettaPlUI::Check(FText::FromString(Part.Label), [State, Key]() { return State->Chosen.Contains(Key); },
								[State, Key](bool bOn) { if (bOn) State->Chosen.Add(Key); else State->Chosen.Remove(Key); }, FText(), !bBusy)
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock).Font(FZettaStyle::Font(8)).ColorAndOpacity(FSlateColor::UseSubduedForeground())
							.Text(FText::FromString(FString::Printf(TEXT("%s · %s"), Kind ? **Kind : TEXT("Part"), *Part.Level)))
						]
					]
				];
			}
		}
		else Box->AddSlot().AutoHeight()[Hint(LOCTEXT("PlNothingFound", "Nothing was found to isolate — type the parts you want."))];

		TSharedRef<SZettaPlText> Field = SNew(SZettaPlText, nullptr, nullptr).MaxLength(80).Hint(LOCTEXT("PlAnotherPart", "Another part")).IsEnabled(!bBusy);
		TSharedRef<SWrapBox> Chips = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4, 4));
		TWeakPtr<SWrapBox> WeakChips = Chips;
		TSharedRef<TFunction<void()>> RenderChips = MakeShared<TFunction<void()>>();
		TWeakPtr<TFunction<void()>> WeakRender = RenderChips;
		*RenderChips = [State, WeakChips, WeakRender]()
		{
			const TSharedPtr<SWrapBox> C = WeakChips.Pin();
			if (!C.IsValid()) return;
			C->ClearChildren();
			for (const FString& Label : State->Typed)
			{
				C->AddSlot()
				[
					SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Chip")).Padding(FMargin(8, 2, 2, 2))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Label))]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							ZettaPlUI::GhostButton(FText::FromString(TEXT("✕")), FSimpleDelegate::CreateLambda([State, Label, WeakRender]()
							{
								State->Typed.Remove(Label);
								if (const TSharedPtr<TFunction<void()>> R = WeakRender.Pin()) (*R)();
							}))
						]
					]
				];
			}
		};
		auto Add = [State, Field, RenderChips]()
		{
			const FString Label = Field->GetText().TrimStartAndEnd();
			if (Label.IsEmpty() || State->Typed.Contains(Label)) return;
			State->Typed.Add(Label);
			Field->SetText(FString());
			(*RenderChips)();
		};
		Field->OnEnter = Add;
		Box->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1)[Field]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)[ZettaPlUI::GhostButton(LOCTEXT("PlAddPart", "+ Add"), FSimpleDelegate::CreateLambda(Add))]
		];
		Box->AddSlot().AutoHeight().Padding(0, 4)[Chips];
		const TFunction<void(int64, const TArray<FString>&, const TArray<FString>&)> Confirm = Actions.ConfirmParts;
		Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, 6, 0, 0)
		[
			SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
			.IsEnabled_Lambda([State, bBusy]() { return !bBusy && State->Chosen.Num() + State->Typed.Num() > 0; })
			.OnClicked_Lambda([State, Confirm, StepId, RenderChips]()
			{
				if (Confirm) Confirm(StepId, State->Chosen.Array(), State->Typed);
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text_Lambda([State]()
				{
					const int32 N = State->Chosen.Num() + State->Typed.Num();
					return N == 0 ? LOCTEXT("PlChooseAPart", "Choose a part") : FText::FromString(FString::Printf(TEXT("Isolate %d part%s and the character"), N, N == 1 ? TEXT("") : TEXT("s")));
				})
			]
		];
		return Box;
	}

	static TSharedRef<SWidget> Body(const FZettaPipelineNode& Node, const FZettaPipelineRunStep& Step, const FZettaPipelineRun& Run, const FZettaStepActions& Actions)
	{
		const int64 StepId = Step.Id;
		if (Step.Status == ZettaStepStatus::Waiting)
		{
			const TFunction<void(int64)> RunStep = Actions.Run;
			return SNew(SBox).HAlign(HAlign_Left)[ZettaPlUI::SmallButton(LOCTEXT("PlRunThisStep", "▶ Run this step"), FSimpleDelegate::CreateLambda([RunStep, StepId]() { if (RunStep) RunStep(StepId); }), !Actions.bBusy, FText(), true)];
		}
		if (Step.Status == ZettaStepStatus::Review) return IsolateReview(Step, Actions);
		if (Step.Status == ZettaStepStatus::Failed)
		{
			const TFunction<void(int64, bool)> Retry = Actions.Retry;
			return SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ZettaPlUI::Note(FText::FromString(Step.ErrorMessage.IsEmpty() ? FString(TEXT("This step failed.")) : Step.ErrorMessage), TEXT("error"))]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[ZettaPlUI::SmallButton(LOCTEXT("PlRetry", "↻ Retry"), FSimpleDelegate::CreateLambda([Retry, StepId]() { if (Retry) Retry(StepId, false); }), !Actions.bBusy)]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
					[
						ZettaPlUI::GhostButton(LOCTEXT("PlRetryWith", "Retry with the settings below"), FSimpleDelegate::CreateLambda([Retry, StepId]() { if (Retry) Retry(StepId, true); }), !Actions.bBusy)
					]
				];
		}
		if (Step.Status == ZettaStepStatus::Running || Step.Status == ZettaStepStatus::Ready || Step.Status == ZettaStepStatus::Submitted)
		{
			const FString Job = Step.Generation.IsValid() ? Step.Generation->Status : FString();
			return Hint(FText::FromString(Step.Status == ZettaStepStatus::Submitted && !Job.IsEmpty() ? FString::Printf(TEXT("The 3D job is %s."), *Job) : FString(TEXT("Working…"))));
		}
		if (!Step.Output.IsSet()) return SNullWidget::NullWidget;
		const FZettaStepOutput& O = Step.Output.GetValue();
		if (O.IsPicture()) return Pictures({ { TEXT("Result"), O.Picture.Url } }, Node.DisplayName());
		if (O.IsViews())
		{
			TArray<TPair<FString, FString>> Items;
			for (const TPair<FString, FZettaSignedPicture>& V : O.Views) Items.Add({ V.Key, V.Value.Url });
			return Pictures(Items, Node.DisplayName());
		}
		if (O.IsIsolated())
		{
			TArray<TPair<FString, FString>> Items = { { TEXT("Character"), O.Character.Url } };
			for (const FZettaIsolatedPart& P : O.Parts) Items.Add({ P.Label, P.Picture.Url });
			return Pictures(Items, Node.DisplayName());
		}
		if (O.IsMesh()) return Model(Step.Generation, O.Gen3dPostId);
		if (O.IsSplat() || O.IsFile())
		{
			const int64 PipelineId = Actions.PipelineId;
			return SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Hint(O.IsSplat() ? LOCTEXT("PlSplatNotHere", "A splat — not available in Unreal.") : LOCTEXT("PlFileNotHere", "A file export — not available in Unreal."))]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
				[
					ZettaPlUI::GhostButton(LOCTEXT("PlOpenOnWeb", "Open on web"), FSimpleDelegate::CreateLambda([PipelineId]() { ZettaUI::OpenSite(FString::Printf(TEXT("/pipelines?pipeline=%lld&view=runs"), PipelineId)); }))
				];
		}
		return SNullWidget::NullWidget;
	}

	/** WHAT ONE NODE DID IN THE RUN: each of its steps (one per part downstream of "each") with its result. */
	static TSharedRef<SWidget> Build(const FZettaPipelineNode& Node, const FZettaPipelineRun& Run, const FZettaStepActions& Actions)
	{
		const TArray<const FZettaPipelineRunStep*> Steps = Run.StepsOf(Node.Id);
		if (Steps.Num() == 0)
		{
			const bool bOver = Run.Status == ZettaRunStatus::Done || Run.Status == ZettaRunStatus::Failed || Run.Status == ZettaRunStatus::Cancelled;
			return Hint(Run.Status == ZettaRunStatus::Draft ? LOCTEXT("PlNotStartedStep", "This run hasn’t started. Check this step’s settings for this run below, then press Start.")
				: bOver ? LOCTEXT("PlDidNotRun", "This step did not run — the step before it did not reach it.")
				: LOCTEXT("PlNotReached", "Not reached yet. It starts when the step before it finishes."));
		}
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		for (const FZettaPipelineRunStep* Step : Steps)
		{
			const FString Title = Step->PartKey.IsEmpty() ? ZettaNodeCatalog::Label(Node.Kind) : Run.PartLabel(Step->PartKey);
			const FString Status = FZettaNodeRunState::StatusLabel(Step->Status) + (Step->Credits > 0 ? FString::Printf(TEXT(" · %d cr"), Step->Credits) : FString());
			Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.CardRaised")).Padding(8)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(FText::FromString(Title)).Font(FZettaStyle::Font(9, true))]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(STextBlock).Text(FText::FromString(Status)).Font(FZettaStyle::Font(8))
							.ColorAndOpacity(Step->Status == ZettaStepStatus::Failed ? FSlateColor(FZettaStyle::Error()) : FSlateColor::UseSubduedForeground())
						]
					]
					+ SVerticalBox::Slot().AutoHeight()[Body(Node, *Step, Run, Actions)]
				]
			];
		}
		return Box;
	}
}

// ─── One run, open ──────────────────────────────────────────────────────────────────────────────────────────────────

/** One run, open: its steps on a read-only canvas, its controls, and the selected step's results and settings. */
class SZettaRunPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaRunPanel) {}
	SLATE_END_ARGS()

	int64 RunId = 0;

	void Construct(const FArguments&, const TSharedRef<SZettaPipelineEditor>& InEditor, int64 InRunId, const TOptional<FZettaRunSummary>& InSummary, TFunction<void()> InClose)
	{
		Editor = InEditor;
		RunId = InRunId;
		Summary = InSummary;
		Close = MoveTemp(InClose);
		ChildSlot
		[
			SNew(SSplitter)
			+ SSplitter::Slot().Value(0.68f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(8, 6)[SAssignNew(Header, SBox)]
				+ SVerticalBox::Slot().AutoHeight().Padding(8, 0)[ZettaUI::ErrorText(MakeAttributeLambda([this]() { return FText::FromString(Error); }))]
				+ SVerticalBox::Slot().FillHeight(1)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[SAssignNew(Canvas, SZettaPipelineCanvas).ReadOnly(true)]
					+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(LoadingText); })
						.Visibility_Lambda([this]() { return Run.IsValid() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)[SAssignNew(Alert, SBox)]
				+ SVerticalBox::Slot().AutoHeight().Padding(8, 6)[SAssignNew(Toolbar, SBox)]
			]
			+ SSplitter::Slot().Value(0.32f)
			[
				SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Card")).Padding(0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(12, 10, 12, 6)
					[
						SNew(STextBlock).Font(FZettaStyle::Font(10, true)).Text_Lambda([this]() { return Selected.IsEmpty() ? LOCTEXT("PlRunTitle", "Run") : LOCTEXT("PlStepTitle", "Step"); })
					]
					+ SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox) + SScrollBox::Slot().Padding(12, 0, 12, 12)[SAssignNew(Side, SBox)]]
				]
			]
		];
		Canvas->OnSelectionChanged = [this](const FString& Id)
		{
			Selected = Id;
			RenderSide();
		};
		TWeakPtr<SZettaRunPanel> Weak = SharedThis(this);
		UpdatedHandle = FZettaRunFollower::Get().OnRunUpdated.AddLambda([Weak](const FZettaPipelineRunPtr& Updated)
		{
			const TSharedPtr<SZettaRunPanel> Self = Weak.Pin();
			if (Self.IsValid() && Updated.IsValid() && Updated->Id == Self->RunId) Self->Apply(Updated);
		});
		Load();
	}

	virtual ~SZettaRunPanel() override
	{
		FZettaRunFollower::Get().OnRunUpdated.Remove(UpdatedHandle);
		// What was typed for this run is sent, not dropped.
		for (const TPair<FString, double>& Timer : Timers) Send(Timer.Key, false);
	}

	void SetSummary(const TOptional<FZettaRunSummary>& InSummary)
	{
		Summary = InSummary;
		if (Run.IsValid()) RenderHeader();
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
		const double Now = FPlatformTime::Seconds();
		TArray<FString> Due;
		for (const TPair<FString, double>& Timer : Timers) if (Now >= Timer.Value) Due.Add(Timer.Key);
		for (const FString& NodeId : Due)
		{
			Timers.Remove(NodeId);
			Send(NodeId, true);
		}
		if (ConfirmDeleteUntil > 0 && Now >= ConfirmDeleteUntil)
		{
			ConfirmDeleteUntil = 0;
			RenderToolbar();
		}
	}

private:
	FString Name() const
	{
		return Summary.IsSet() && !Summary->SeedName.IsEmpty() ? Summary->SeedName : FString::Printf(TEXT("Run #%lld"), RunId);
	}

	void Load()
	{
		if (const FZettaPipelineRunPtr Latest = FZettaRunFollower::Get().LatestRun(RunId)) Apply(Latest);
		TWeakPtr<SZettaRunPanel> Weak = SharedThis(this);
		ZettaPipelinesApi::GetRun(RunId, [Weak](const FZettaResponse& R, const FZettaPipelineRunPtr& Loaded)
		{
			const TSharedPtr<SZettaRunPanel> Self = Weak.Pin();
			if (!Self.IsValid()) return;
			if (!R.bOk || !Loaded.IsValid())
			{
				Self->LoadingText = R.Status == 404 ? TEXT("This run is gone.") : ZettaCatalog::Explain(R);
				return;
			}
			Self->Apply(Loaded);
			FZettaRunFollower& Follower = FZettaRunFollower::Get();
			if ((Loaded->IsLive() || Loaded->Status == ZettaRunStatus::Draft) && !Follower.IsFollowing(Self->RunId)) Follower.Follow(Self->RunId, Loaded->PipelineId);
		});
	}

	void Apply(const FZettaPipelineRunPtr& Next)
	{
		if (!Next.IsValid()) return;
		const bool bWasLive = Run.IsValid() && Run->IsLive();
		Run = Next;
		if (const TSharedPtr<SZettaPipelineEditor> E = Editor.Pin()) E->LearnParts(*Run);
		// A run that becomes live (Start, Resume) is followed until it is over.
		if (Run->IsLive() && !bWasLive && !FZettaRunFollower::Get().IsFollowing(RunId)) FZettaRunFollower::Get().Follow(RunId, Run->PipelineId);
		Render();
	}

	/** Hands an action's answer to the view (and the follower), then re-reads the runs list. */
	void Act(TFunction<void(ZettaPipelinesApi::FRunDone)> Action)
	{
		if (bBusy) return;
		bBusy = true;
		Error.Reset();
		RenderToolbar();
		TWeakPtr<SZettaRunPanel> Weak = SharedThis(this);
		Action([Weak](const FZettaResponse& R, const FZettaPipelineRunPtr& Answer)
		{
			const TSharedPtr<SZettaRunPanel> Self = Weak.Pin();
			if (!Self.IsValid()) return;
			Self->bBusy = false;
			if (R.bOk)
			{
				if (Answer.IsValid())
				{
					FZettaRunFollower::Get().Report(Answer);
					Self->Apply(Answer);
				}
				if (const TSharedPtr<SZettaPipelineEditor> E = Self->Editor.Pin()) E->RefreshRuns();
			}
			else if (R.Code == TEXT("RUN_NEEDS_INPUT"))
			{
				Self->ServerNeeds = ZettaPipelinesApi::ProblemsOf(R);
				Self->bStartTried = true;
				for (const FZettaPipelineProblem& P : Self->ServerNeeds)
				{
					if (P.NodeId.IsEmpty()) continue;
					Self->SelectStep(P.NodeId);
					break;
				}
				Self->Error = ZettaCatalog::Explain(R);
			}
			else Self->Error = ZettaCatalog::Explain(R);
			Self->Render();
		});
	}

	void SelectStep(const FString& NodeId)
	{
		Selected = NodeId;
		Canvas->Select(NodeId);
		RenderSide();
	}

	/** The run's own graph, with this run's typed changes ahead of the server's copy. */
	FZettaPipelineGraph BuildGraph() const
	{
		FZettaPipelineGraph G = Run->Graph;
		for (FZettaPipelineNode& Node : G.Nodes)
		{
			if (const FZettaPipelineNode* Draft = Drafts.Find(Node.Id)) Node = *Draft;
			else if (!ZettaNodeCatalog::NotAvailableHere(Node.Kind)) Node.Settings = ZettaNodeSettings::WithDefaults(Node.Kind, Node.Settings);
		}
		return G;
	}

	void ChangeNode(const FString& NodeId, const TSharedRef<FJsonObject>& Patch)
	{
		const TSharedPtr<SZettaPipelineEditor> E = Editor.Pin();
		TSharedPtr<FJsonObject> Settings, Request;
		FString Why;
		if (!ZettaRequestBuilder::RunNodeChange(Graph, NodeId, Patch, E.IsValid() ? E->Gates() : nullptr, Settings, Request, Why))
		{
			Error = Why;
			return;
		}
		FZettaPipelineNode Node = *Graph.Node(NodeId);
		Node.Settings = Settings;
		Node.Request = Request;
		Drafts.Add(NodeId, Node);
		Timers.Add(NodeId, FPlatformTime::Seconds() + 0.7);
		Render();
	}

	void Send(const FString& NodeId, bool bApplyAnswer)
	{
		const FZettaPipelineNode* Draft = Drafts.Find(NodeId);
		if (!Draft) return;
		const FZettaPipelineNode Sent = *Draft;
		TWeakPtr<SZettaRunPanel> Weak = bApplyAnswer ? TWeakPtr<SZettaRunPanel>(SharedThis(this)) : nullptr;
		ZettaPipelinesApi::PatchRunNode(RunId, NodeId, Sent.Request, Sent.Settings, [Weak, NodeId, Sent](const FZettaResponse& R, const FZettaPipelineRunPtr& Answer)
		{
			const TSharedPtr<SZettaRunPanel> Self = Weak.Pin();
			if (!Self.IsValid()) return;
			if (!R.bOk)
			{
				Self->Error = ZettaCatalog::Explain(R);
				return;
			}
			// Sent: the server's copy is the run's own again — unless more was typed meanwhile.
			const FZettaPipelineNode* Now = Self->Drafts.Find(NodeId);
			if (Now && JEquals(Now->Settings, Sent.Settings) && !Self->Timers.Contains(NodeId)) Self->Drafts.Remove(NodeId);
			if (Answer.IsValid()) Self->Apply(Answer);
		});
	}

	void FlushDrafts(TFunction<void()> Then)
	{
		// What is still typed goes first, one after another, then the action.
		TArray<FString> Pending;
		Timers.GetKeys(Pending);
		Timers.Reset();
		if (Pending.Num() == 0)
		{
			Then();
			return;
		}
		TSharedRef<int32> Left = MakeShared<int32>(Pending.Num());
		for (const FString& NodeId : Pending)
		{
			const FZettaPipelineNode* Draft = Drafts.Find(NodeId);
			if (!Draft)
			{
				if (--*Left == 0) Then();
				continue;
			}
			ZettaPipelinesApi::PatchRunNode(RunId, NodeId, Draft->Request, Draft->Settings, [Left, Then](const FZettaResponse&, const FZettaPipelineRunPtr&)
			{
				if (--*Left == 0) Then();
			});
		}
	}

	// ─── Drawing ───────────────────────────────────────────────────────────────────────────────────────────────────

	void Render()
	{
		if (!Run.IsValid()) return;
		Graph = BuildGraph();
		const bool bDraft = Run->Status == ZettaRunStatus::Draft;
		Needs = bDraft ? ZettaGraphRules::RunInputProblems(Graph) : TArray<FZettaPipelineProblem>();
		if (Needs.Num() == 0) bStartTried = false;
		TSet<FString> Running;
		for (const FZettaPipelineNode& N : Graph.Nodes) if (Run->NodeState(N.Id).Status == ZettaStepStatus::Running) Running.Add(N.Id);
		Canvas->SetRunning(Running);
		const TSharedPtr<SZettaPipelineEditor> E = Editor.Pin();
		const FZettaPriceList* Prices = E.IsValid() ? E->Prices() : nullptr;
		const FZettaEntitlements3D* Gates = E.IsValid() ? E->Gates() : nullptr;
		const FZettaPipelineRunPtr R = Run;
		const TArray<FZettaPipelineProblem> NeedsNow = Needs, ServerNow = ServerNeeds;
		const bool bTried = bStartTried;
		const TOptional<FZettaRunSummary> S = Summary;
		const FZettaPipelineGraph& G = Graph;
		Canvas->Show(Graph, [&G, R, Prices, Gates, NeedsNow, ServerNow, bTried, S](const FZettaPipelineNode& Node)
		{
			FZettaStepCardData D;
			const bool bSeed = ZettaNodeCatalog::IsSeed(Node.Kind);
			const TArray<const FZettaPipelineRunStep*> Steps = R->StepsOf(Node.Id);
			D.Summary = bSeed ? FString(TEXT("This run’s seed")) : ZettaNodeSettings::Summary(Node);
			D.Price = ZettaRequestBuilder::PriceFor(Node, Prices, { ZettaGraphRules::IncomingType(G, Node.Id), Gates });
			D.Run = FZettaNodeRunState::Of(Steps);
			if (R->Status == ZettaRunStatus::Draft)
			{
				for (const FZettaPipelineProblem& P : NeedsNow) if (P.NodeId == Node.Id && D.NeedsInput.IsEmpty()) D.NeedsInput = P.Message;
				if (D.NeedsInput.IsEmpty() && bTried) for (const FZettaPipelineProblem& P : ServerNow) if (P.NodeId == Node.Id && D.NeedsInput.IsEmpty()) D.NeedsInput = P.Message;
			}
			D.bFlash = bTried;
			auto OutputsOf = [](const FZettaPipelineRunStep& Step, TArray<TPair<FString, FString>>& Out)
			{
				if (!Step.Output.IsSet()) return;
				const FZettaStepOutput& O = Step.Output.GetValue();
				if (O.IsPicture()) Out.Add({ O.Picture.Url, TEXT("Picture") });
				else if (O.IsViews()) for (const TPair<FString, FZettaSignedPicture>& V : O.Views) Out.Add({ V.Value.Url, V.Key.IsEmpty() ? FString(TEXT("View")) : V.Key.Left(1).ToUpper() + V.Key.Mid(1) });
				else if (O.IsIsolated())
				{
					Out.Add({ O.Character.Url, TEXT("Character") });
					for (const FZettaIsolatedPart& P : O.Parts) Out.Add({ P.Picture.Url, P.Label });
				}
				else Out.Add({ Step.Generation.IsValid() ? Step.Generation->ThumbnailUrl : FString(), Step.Generation.IsValid() ? Step.Generation->Title : FString(TEXT("Model")) });
			};
			if (bSeed)
			{
				const FZettaPipelineRunStep* First = Steps.Num() > 0 ? Steps[0] : nullptr;
				FString Url;
				if (First && First->Output.IsSet() && First->Output->IsPicture()) Url = First->Output->Picture.Url;
				else if (First && First->Output.IsSet() && First->Output->IsViews())
				{
					// A Multi-view seed shows its front; its other sides are listed below it.
					for (const TPair<FString, FZettaSignedPicture>& V : First->Output->Views) if (V.Key == TEXT("front")) Url = V.Value.Url;
				}
				else if (First && First->Generation.IsValid()) Url = First->Generation->ThumbnailUrl;
				if (Url.IsEmpty() && S.IsSet()) Url = S->SeedThumbnailUrl;
				D.bHasSeed = true;
				D.SeedUrl = Url;
				D.SeedName = S.IsSet() && !S->SeedName.IsEmpty() ? S->SeedName : First && First->Generation.IsValid() ? First->Generation->Title : FString();
				if (Node.Kind == ZettaNodeKinds::SeedViews) for (const FZettaPipelineRunStep* Step : Steps) OutputsOf(*Step, D.Outputs);
			}
			else for (const FZettaPipelineRunStep* Step : Steps) OutputsOf(*Step, D.Outputs);
			return D;
		}, false);
		if (!Selected.IsEmpty() && !Graph.Node(Selected)) Selected.Reset();
		Canvas->Select(Selected);
		RenderHeader();
		RenderToolbar();
		RenderSide();
	}

	void RenderHeader()
	{
		const FZettaPipelineRunPtr R = Run;
		const bool bImporting = FZettaRunFollower::Get().IsFollowing(RunId) && FZettaRunFollower::Get().Find(RunId)->Import != EZettaRunImport::None;
		Header->SetContent(
			SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8, 4))
			+ SWrapBox::Slot()[ZettaPlUI::GhostButton(LOCTEXT("PlAllRuns", "‹ All runs"), FSimpleDelegate::CreateLambda([this]() { if (Close) Close(); }))]
			+ SWrapBox::Slot().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Name())).Font(FZettaStyle::Font(11, true))]
			+ SWrapBox::Slot().VAlign(VAlign_Center)[ZettaUI::Hint(FText::FromString(FString::Printf(TEXT("#%lld"), RunId)))]
			+ SWrapBox::Slot().VAlign(VAlign_Center)[ZettaPlUI::RunPill(R->Status)]
			+ SWrapBox::Slot().VAlign(VAlign_Center)[ZettaUI::Hint(FText::FromString(FString::Printf(TEXT("%s · %d cr spent"), *ZettaRunStatus::ModeLabel(R->Mode), R->SpentCredits)))]
			+ SWrapBox::Slot().VAlign(VAlign_Center)
			[
				SNew(SBox).Visibility(bImporting ? EVisibility::Visible : EVisibility::Collapsed)
				[
					SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Chip")).Padding(FMargin(8, 2))
					[
						SNew(STextBlock).Text(LOCTEXT("PlImportingResults", "Importing results")).ColorAndOpacity(FSlateColor(FZettaStyle::Accent()))
					]
				]
			]);
	}

	TSharedRef<SWidget> ModeSwitch()
	{
		const FString Mode = Run->Mode == ZettaRunStatus::ModeAuto ? ZettaRunStatus::ModeAuto : ZettaRunStatus::ModeManual;
		return SNew(SBox).IsEnabled(!bBusy)
			[
				ZettaPlUI::Segmented({ { ZettaRunStatus::ModeAuto, TEXT("Auto"), ZettaRunStatus::ModeHint(ZettaRunStatus::ModeAuto) }, { ZettaRunStatus::ModeManual, TEXT("Manual"), ZettaRunStatus::ModeHint(ZettaRunStatus::ModeManual) } },
					[Mode]() { return Mode; }, [this](const FString& V)
					{
						const int64 Id = RunId;
						Act([Id, V](ZettaPipelinesApi::FRunDone Done) { ZettaPipelinesApi::SetMode(Id, V, Done); });
					})
			];
	}

	void RenderToolbar()
	{
		if (!Run.IsValid())
		{
			Toolbar->SetContent(SNullWidget::NullWidget);
			Alert->SetContent(SNullWidget::NullWidget);
			return;
		}
		const int64 Id = RunId;
		TSharedRef<SHorizontalBox> Bar = SNew(SHorizontalBox);
		Alert->SetContent(SNullWidget::NullWidget);
		const FString& Status = Run->Status;
		if (Status == ZettaRunStatus::Draft)
		{
			if (bStartTried && Needs.Num() > 0)
			{
				TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(FZettaStyle::Font(9, true)).ColorAndOpacity(FSlateColor(FZettaStyle::Warning()))
						.Text(FText::FromString(FString::Printf(TEXT("This run can’t start yet — %s something from you."), Needs.Num() == 1 ? TEXT("one step needs") : *FString::Printf(TEXT("%d steps need"), Needs.Num()))))
					];
				for (const FZettaPipelineProblem& Need : Needs)
				{
					const FString NodeId = Need.NodeId;
					Box->AddSlot().AutoHeight().HAlign(HAlign_Left)[ZettaPlUI::GhostButton(FText::FromString(Need.Message), FSimpleDelegate::CreateLambda([this, NodeId]() { SelectStep(NodeId); }))];
				}
				Box->AddSlot().AutoHeight()[ZettaUI::Hint(LOCTEXT("PlFillAndStart", "Select the step marked Needs input, fill it in on the right, then press Start again."))];
				Alert->SetContent(ZettaUI::Card(Box, 8));
			}
			Bar->AddSlot().AutoWidth().Padding(0, 0, 8, 0)[ModeSwitch()];
			Bar->AddSlot().AutoWidth().Padding(0, 0, 4, 0)[ZettaPlUI::SmallButton(FText::FromString(bBusy ? TEXT("Starting…") : TEXT("▶ Start")), FSimpleDelegate::CreateLambda([this]() { Start(); }), !bBusy, FText(), true)];
			Bar->AddSlot().AutoWidth()[ZettaPlUI::GhostButton(LOCTEXT("PlDiscard", "Discard"), FSimpleDelegate::CreateLambda([this]()
			{
				Timers.Reset();
				Drafts.Reset();
				Delete();
			}), !bBusy, LOCTEXT("PlDiscardTip", "A run that never started has nothing to keep: Discard deletes it."))];
		}
		else if (Run->IsLive())
		{
			Bar->AddSlot().AutoWidth().Padding(0, 0, 8, 0)[ModeSwitch()];
			const int32 Waiting = Run->WaitingCount();
			if (Waiting > 0)
			{
				Bar->AddSlot().AutoWidth().Padding(0, 0, 4, 0)
				[
					ZettaPlUI::SmallButton(FText::FromString(Waiting == 1 ? FString(TEXT("▶ Run the next step")) : FString::Printf(TEXT("▶ Run %d waiting"), Waiting)),
						FSimpleDelegate::CreateLambda([this]() { RunWaiting(); }), !bBusy, FText(), true)
				];
			}
			if (Run->bPaused)
				Bar->AddSlot().AutoWidth().Padding(0, 0, 4, 0)[ZettaPlUI::GhostButton(LOCTEXT("PlResume", "▶ Resume"), FSimpleDelegate::CreateLambda([this, Id]() { Act([Id](ZettaPipelinesApi::FRunDone Done) { ZettaPipelinesApi::Resume(Id, Done); }); }), !bBusy)];
			else
				Bar->AddSlot().AutoWidth().Padding(0, 0, 4, 0)[ZettaPlUI::GhostButton(LOCTEXT("PlPause", "❚❚ Pause"), FSimpleDelegate::CreateLambda([this, Id]() { Act([Id](ZettaPipelinesApi::FRunDone Done) { ZettaPipelinesApi::Pause(Id, Done); }); }), !bBusy)];
			Bar->AddSlot().AutoWidth()
			[
				ZettaPlUI::GhostButton(LOCTEXT("PlCancelRun", "■ Cancel"), FSimpleDelegate::CreateLambda([this, Id]()
				{
					if (ZettaPlUI::Confirm(TEXT("Cancel this run?"), TEXT("Steps not started are cancelled, and a 3D job still in the queue is cancelled and refunded. What it has made stays in your library.")))
						Act([Id](ZettaPipelinesApi::FRunDone Done) { ZettaPipelinesApi::Cancel(Id, Done); });
				}), !bBusy)
			];
		}
		else
		{
			// A run that is over can be deleted — never one still going. Its models stay in the library.
			const bool bConfirm = ConfirmDeleteUntil > 0;
			Bar->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				ZettaUI::Hint(bConfirm ? LOCTEXT("PlModelsStay", "Its models stay in your library.") : LOCTEXT("PlRunOver", "This run is over."))
			];
			Bar->AddSlot().AutoWidth()
			[
				ZettaPlUI::GhostButton(bConfirm ? LOCTEXT("PlPressAgain", "Press again to delete") : LOCTEXT("PlDeleteRun", "Delete run"), FSimpleDelegate::CreateLambda([this]()
				{
					if (ConfirmDeleteUntil > 0)
					{
						Delete();
						return;
					}
					ConfirmDeleteUntil = FPlatformTime::Seconds() + 4;
					RenderToolbar();
				}), !bBusy)
			];
		}
		Toolbar->SetContent(Bar);
	}

	/** Start: what was just typed is sent first; a step still needing its input stops it and is pointed at. */
	void Start()
	{
		if (Needs.Num() > 0)
		{
			bStartTried = true;
			SelectStep(Needs[0].NodeId);
			Render();
			return;
		}
		bBusy = true;
		RenderToolbar();
		TWeakPtr<SZettaRunPanel> Weak = SharedThis(this);
		FlushDrafts([Weak]()
		{
			const TSharedPtr<SZettaRunPanel> Self = Weak.Pin();
			if (!Self.IsValid()) return;
			Self->bBusy = false;
			const int64 Id = Self->RunId;
			Self->Act([Id](ZettaPipelinesApi::FRunDone Done) { ZettaPipelinesApi::Start(Id, Done); });
		});
	}

	void RunWaiting()
	{
		const TSharedPtr<SZettaPipelineEditor> E = Editor.Pin();
		TArray<FString> Lines;
		TOptional<int32> Sum = 0;
		for (const FZettaPipelineRunStep& Step : Run->Steps)
		{
			if (Step.Status != ZettaStepStatus::Waiting) continue;
			const FZettaPipelineNode* Node = Graph.Node(Step.NodeId);
			FString StepName = Node ? Node->DisplayName() : Step.NodeId;
			if (!Step.PartKey.IsEmpty()) StepName += FString::Printf(TEXT(" (%s)"), *Run->PartLabel(Step.PartKey));
			const TOptional<int32> Price = Node && E.IsValid() ? ZettaRequestBuilder::PriceFor(*Node, E->Prices(), { ZettaGraphRules::IncomingType(Graph, Node->Id), E->Gates() }) : TOptional<int32>();
			Lines.Add(FString::Printf(TEXT("• %s — %s"), *StepName, *ZettaRequestBuilder::PriceLabel(Price, false)));
			if (!Price.IsSet()) Sum.Reset();
			else if (Sum.IsSet()) Sum = Sum.GetValue() + Price.GetValue();
		}
		const FString Message = FString::Join(Lines, TEXT("\n")) + (Sum.IsSet() ? FString::Printf(TEXT("\n\nUp to %d credits."), Sum.GetValue()) : FString());
		const FString Title = Lines.Num() == 1 ? FString(TEXT("Run the next step?")) : FString::Printf(TEXT("Run %d waiting steps?"), Lines.Num());
		if (!ZettaPlUI::Confirm(Title, Message)) return;
		const int64 Id = RunId;
		Act([Id](ZettaPipelinesApi::FRunDone Done) { ZettaPipelinesApi::RunWaiting(Id, Done); });
	}

	void Delete()
	{
		bBusy = true;
		RenderToolbar();
		TWeakPtr<SZettaRunPanel> Weak = SharedThis(this);
		const int64 Id = RunId;
		ZettaPipelinesApi::DeleteRun(Id, [Weak, Id](const FZettaResponse& R)
		{
			if (R.bOk) FZettaRunFollower::Get().Unfollow(Id);
			const TSharedPtr<SZettaRunPanel> Self = Weak.Pin();
			if (!Self.IsValid()) return;
			Self->bBusy = false;
			if (!R.bOk)
			{
				Self->Error = ZettaCatalog::Explain(R);
				Self->RenderToolbar();
				return;
			}
			if (const TSharedPtr<SZettaPipelineEditor> E = Self->Editor.Pin()) E->RefreshRuns();
			if (Self->Close) Self->Close();
		});
	}

	// ─── The side panel ──────────────────────────────────────────────────────────────────────────────────────────

	FZettaStepActions Actions(const FString& NodeId)
	{
		FZettaStepActions A;
		A.bBusy = bBusy;
		const TSharedPtr<SZettaPipelineEditor> E = Editor.Pin();
		A.PipelineId = E.IsValid() ? E->GetPipelineId() : 0;
		const int64 Id = RunId;
		TWeakPtr<SZettaRunPanel> Weak = SharedThis(this);
		A.Run = [Weak, Id](int64 StepId)
		{
			if (const TSharedPtr<SZettaRunPanel> Self = Weak.Pin()) Self->Act([Id, StepId](ZettaPipelinesApi::FRunDone Done) { ZettaPipelinesApi::RunStep(Id, StepId, Done); });
		};
		A.Retry = [Weak, Id, NodeId](int64 StepId, bool bWithCurrent)
		{
			const TSharedPtr<SZettaRunPanel> Self = Weak.Pin();
			if (!Self.IsValid()) return;
			TSharedPtr<FJsonObject> Settings, Request;
			if (bWithCurrent)
			{
				if (const FZettaPipelineNode* Current = Self->Graph.Node(NodeId))
				{
					const TSharedPtr<SZettaPipelineEditor> Ed = Self->Editor.Pin();
					Settings = ZettaNodeSettings::Of(*Current);
					Request = ZettaRequestBuilder::RequestFor(Current->Kind, Settings, { ZettaGraphRules::IncomingType(Self->Graph, Current->Id), Ed.IsValid() ? Ed->Gates() : nullptr });
				}
			}
			Self->Act([Id, StepId, Request, Settings](ZettaPipelinesApi::FRunDone Done) { ZettaPipelinesApi::RetryStep(Id, StepId, Request, Settings, Done); });
		};
		A.ConfirmParts = [Weak, Id](int64 StepId, const TArray<FString>& Keys, const TArray<FString>& Extra)
		{
			if (const TSharedPtr<SZettaRunPanel> Self = Weak.Pin()) Self->Act([Id, StepId, Keys, Extra](ZettaPipelinesApi::FRunDone Done) { ZettaPipelinesApi::ChooseParts(Id, StepId, Keys, Extra, Done); });
		};
		return A;
	}

	FString ResultsSignature(const FString& NodeId) const
	{
		FString S = FString::Printf(TEXT("%d|%s|"), bBusy ? 1 : 0, *Run->Status);
		for (const FZettaPipelineRunStep* Step : Run->StepsOf(NodeId))
		{
			S += FString::Printf(TEXT("%lld:%s:%d:%d:%lld:%s:%s;"), Step->Id, *Step->Status, Step->Attempt, Step->Credits, Step->UpdatedAt.GetTicks(),
				Step->Generation.IsValid() ? *Step->Generation->Status : TEXT(""), Step->Output.IsSet() ? *JWrite(Step->Output->Raw) : TEXT(""));
		}
		return S;
	}

	TSharedRef<SWidget> Overview()
	{
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SZettaPlThumb).Url(Summary.IsSet() ? Summary->SeedThumbnailUrl : FString()).Size(FVector2D(64, 64))]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Name())).Font(FZettaStyle::Font(10, true))]
				+ SVerticalBox::Slot().AutoHeight()[ZettaUI::Hint(FText::FromString(FString::Printf(TEXT("%s %s"), Run->Status == ZettaRunStatus::Draft ? TEXT("Made") : TEXT("Started"), *ZettaPipelineText::WhenRun(Run->CreatedAt))))]
			]
		];
		if (Run->Status == ZettaRunStatus::Draft)
		{
			if (Needs.Num() > 0)
			{
				TSharedRef<SVerticalBox> Note = SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(FString::Printf(TEXT("This run hasn’t started. %s its own input first:"),
							Needs.Num() == 1 ? TEXT("One step needs") : *FString::Printf(TEXT("%d steps need"), Needs.Num()))))
					];
				for (const FZettaPipelineProblem& Need : Needs)
				{
					const FString NodeId = Need.NodeId;
					Note->AddSlot().AutoHeight().HAlign(HAlign_Left)[ZettaPlUI::GhostButton(FText::FromString(Need.Message), FSimpleDelegate::CreateLambda([this, NodeId]() { if (!NodeId.IsEmpty()) SelectStep(NodeId); }))];
				}
				Note->AddSlot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("PlFillThenStart", "Fill each in, then press Start below."))];
				Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[ZettaUI::Card(Note, 8)];
			}
			else Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[ZettaPlUI::Note(LOCTEXT("PlReadyToStart", "This run hasn’t started. Everything it needs is set — check the steps, then press Start below."))];
		}
		else if (Run->Status == ZettaRunStatus::Awaiting)
		{
			Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[ZettaPlUI::Note(LOCTEXT("PlWaitingForYou", "This run is waiting for you. Select the step marked Waiting for you to run it, or press Run the next step below."), TEXT("amber"))];
		}
		TArray<TPair<FString, FString>> Rows = {
			{ TEXT("State"), ZettaRunStatus::Label(Run->Status) },
			{ TEXT("Mode"), ZettaRunStatus::ModeLabel(Run->Mode) },
		};
		if (Summary.IsSet() && Summary->bHasProgress) Rows.Add({ TEXT("Steps done"), FString::Printf(TEXT("%d of %d"), Summary->Done, Summary->Total) });
		Rows.Add({ TEXT("Spent"), FString::Printf(TEXT("%d cr"), Run->SpentCredits) });
		Rows.Add({ TEXT("Estimated"), FString::Printf(TEXT("up to %d cr"), Run->EstimatedCredits) });
		for (const TPair<FString, FString>& R : Rows)
		{
			Box->AddSlot().AutoHeight().Padding(0, 1)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(100)[SNew(STextBlock).Text(FText::FromString(R.Key)).ColorAndOpacity(FSlateColor::UseSubduedForeground())]]
				+ SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(FText::FromString(R.Value))]
			];
		}
		Box->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[ZettaUI::Hint(LOCTEXT("PlSelectStep", "Select a step to see what it made."))];
		return Box;
	}

	void RenderSide()
	{
		if (!Run.IsValid()) return;
		const FZettaPipelineNode* Node = Graph.Node(Selected);
		if (!Node)
		{
			FormFor.Reset();
			Form.Reset();
			Side->SetContent(Overview());
			return;
		}
		const bool bEditable = Run->IsNodeEditable(Node->Id) && !ZettaNodeCatalog::NotAvailableHere(Node->Kind);
		const FString Key = Node->Id + (bEditable ? TEXT("|edit") : TEXT("|view"));
		const FString Signature = ResultsSignature(Node->Id);
		// A form being typed into is refreshed in place; results are drawn again only when the steps changed.
		if (FormFor == Key)
		{
			if (Form.IsValid()) Form->Refresh(*Node, Graph);
			if (Signature != ResultsFor && Results.IsValid())
			{
				ResultsFor = Signature;
				Results->SetContent(ZettaStepResults::Build(*Node, *Run, Actions(Node->Id)));
			}
			return;
		}
		FormFor = Key;
		ResultsFor = Signature;
		Form.Reset();
		const FZettaNodeSpec* Spec = ZettaNodeCatalog::Spec(Node->Kind);
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		Box->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 2, 8, 0)[ZettaPlUI::HueIcon(Node->Kind, 28)]
			+ SHorizontalBox::Slot().FillWidth(1)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Node->DisplayName())).Font(FZettaStyle::Font(10, true))]
				+ SVerticalBox::Slot().AutoHeight()[ZettaUI::Hint(FText::FromString(Spec ? Spec->Blurb : FString()))]
			]
		];
		Box->AddSlot().AutoHeight()[SAssignNew(Results, SBox)[ZettaStepResults::Build(*Node, *Run, Actions(Node->Id))]];
		if (!ZettaNodeCatalog::IsSeed(Node->Kind))
		{
			Box->AddSlot().AutoHeight().Padding(0, 10, 0, 4)[ZettaPlUI::Heading(bEditable ? LOCTEXT("PlSettingsForRun", "Settings for this run") : LOCTEXT("PlSettingsRanWith", "Settings it ran with"))];
			const FString NodeId = Node->Id;
			Box->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
			[
				ZettaPlUI::Note(MakeAttributeLambda([this, NodeId]()
				{
					for (const FZettaPipelineProblem& P : Needs)
					{
						if (P.NodeId != NodeId) continue;
						const int32 Colon = P.Message.Find(TEXT(": "));
						FString What = Colon >= 0 ? P.Message.Mid(Colon + 2) : P.Message;
						if (!What.IsEmpty()) What = What.Left(1).ToUpper() + What.Mid(1);
						return FText::FromString(FString::Printf(TEXT("Needs input. %s It is set on each run, so this run needs its own before it can start."), *What));
					}
					return FText();
				}), bStartTried ? TEXT("error") : TEXT("amber"))
			];
			if (bEditable)
			{
				TSharedRef<FZettaFormContext> Ctx = MakeShared<FZettaFormContext>();
				const TSharedPtr<SZettaPipelineEditor> E = Editor.Pin();
				if (E.IsValid()) Ctx->Overlay = E->GetOverlay();
				Ctx->bInRun = true;
				Ctx->Refresh(*Node, Graph);
				TWeakPtr<SZettaRunPanel> Weak = SharedThis(this);
				Ctx->Patch = [Weak, NodeId](const TSharedRef<FJsonObject>& Patch) { if (const TSharedPtr<SZettaRunPanel> Self = Weak.Pin()) Self->ChangeNode(NodeId, Patch); };
				Form = Ctx;
				Box->AddSlot().AutoHeight()[ZettaNodeForms::Build(Ctx)];
			}
			else
			{
				const FString Lead = ZettaNodeCatalog::NotAvailableHere(Node->Kind) ? FString(TEXT("This step is not available in Unreal. ")) : ZettaNodeSettings::Summary(*Node) + TEXT(". ");
				Box->AddSlot().AutoHeight()
				[
					ZettaUI::Hint(FText::FromString(Lead + TEXT("A step that has run keeps the settings it ran with — change the pipeline for the next run, or retry a failed step with new settings.")))
				];
			}
		}
		Side->SetContent(Box);
	}

	TWeakPtr<SZettaPipelineEditor> Editor;
	TOptional<FZettaRunSummary> Summary;
	TFunction<void()> Close;
	FZettaPipelineRunPtr Run;
	FZettaPipelineGraph Graph;
	TMap<FString, FZettaPipelineNode> Drafts;
	TMap<FString, double> Timers;
	bool bBusy = false;
	bool bStartTried = false;
	double ConfirmDeleteUntil = 0;
	FString Selected;
	FString Error;
	FString LoadingText = TEXT("Loading the run…");
	TArray<FZettaPipelineProblem> Needs;
	TArray<FZettaPipelineProblem> ServerNeeds;
	FString FormFor;
	FString ResultsFor;
	TSharedPtr<FZettaFormContext> Form;
	TSharedPtr<SBox> Header, Alert, Toolbar, Side, Results;
	TSharedPtr<SZettaPipelineCanvas> Canvas;
	FDelegateHandle UpdatedHandle;
};

// ─── Runs list ──────────────────────────────────────────────────────────────────────────────────────────────────────

struct FZettaRunFilter
{
	FString Key;
	FString Label;
	TArray<FString> Statuses;
	EZettaRunTone Tone;
};

static const TArray<FZettaRunFilter>& RunFilters()
{
	static const TArray<FZettaRunFilter> List = {
		{ TEXT("all"), TEXT("All"), {}, EZettaRunTone::Idle },
		{ TEXT("running"), TEXT("Running"), { ZettaRunStatus::Running, ZettaRunStatus::Paused }, EZettaRunTone::Running },
		{ TEXT("attention"), TEXT("Needs you"), { ZettaRunStatus::Draft, ZettaRunStatus::Awaiting }, EZettaRunTone::Attention },
		{ TEXT("done"), TEXT("Done"), { ZettaRunStatus::Done }, EZettaRunTone::Done },
		{ TEXT("failed"), TEXT("Stopped"), { ZettaRunStatus::Failed, ZettaRunStatus::Cancelled }, EZettaRunTone::Failed },
	};
	return List;
}

void SZettaRunsView::Construct(const FArguments&, const TSharedRef<SZettaPipelineEditor>& InEditor)
{
	Editor = InEditor;
	ChildSlot
	[
		SNew(SSplitter)
		+ SSplitter::Slot().Value(0.26f)
		[
			SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Card")).Padding(0)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(10, 10, 10, 4)[SNew(STextBlock).Text(LOCTEXT("PlRunsTitle", "Runs")).Font(FZettaStyle::Font(10, true))]
				+ SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 6)[SAssignNew(Filters, SHorizontalBox)]
				+ SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox) + SScrollBox::Slot().Padding(6, 0)[SAssignNew(List, SVerticalBox)]]
			]
		]
		+ SSplitter::Slot().Value(0.74f)[SAssignNew(Right, SBox)]
	];
	RenderList();
	OpenRun({});
}

TOptional<int64> SZettaRunsView::OpenRunId() const
{
	return Panel.IsValid() ? TOptional<int64>(Panel->RunId) : TOptional<int64>();
}

void SZettaRunsView::SetRuns(const TArray<FZettaRunSummary>& InRuns)
{
	Runs = InRuns;
	RenderList();
	if (Panel.IsValid())
	{
		TOptional<FZettaRunSummary> Found;
		for (const FZettaRunSummary& R : InRuns) if (R.Id == Panel->RunId) Found = R;
		Panel->SetSummary(Found);
	}
	else OpenRun({});
}

void SZettaRunsView::OpenRun(TOptional<int64> RunId)
{
	if (RunId.IsSet() && Panel.IsValid() && Panel->RunId == RunId.GetValue()) return;
	const TSharedPtr<SZettaPipelineEditor> E = Editor.Pin();
	Panel.Reset();
	if (!RunId.IsSet() || !E.IsValid())
	{
		const bool bAny = Runs.IsSet() && Runs->Num() > 0;
		Right->SetContent(
			SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center).MaxDesiredWidth(420)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ZettaUI::Title(bAny ? LOCTEXT("PlOpenARun", "Open a run to see what it made") : LOCTEXT("PlNoRuns", "No runs yet"))]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
				[
					SNew(STextBlock).AutoWrapText(true).Justification(ETextJustify::Center).ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(bAny ? LOCTEXT("PlOpenARunLead", "Each run started from its own seed and keeps its own results. One that needs you waits here until you run its next step.")
						: LOCTEXT("PlNoRunsLead", "Press New run and pick a picture or a model. Every run of this pipeline is listed here — running, waiting for you, or done."))
				]
			]);
		RenderList();
		return;
	}
	TOptional<FZettaRunSummary> Found;
	if (Runs.IsSet()) for (const FZettaRunSummary& R : Runs.GetValue()) if (R.Id == RunId.GetValue()) Found = R;
	TWeakPtr<SZettaRunsView> Weak = SharedThis(this);
	Panel = SNew(SZettaRunPanel, E.ToSharedRef(), RunId.GetValue(), Found, [Weak]() { if (const TSharedPtr<SZettaRunsView> Self = Weak.Pin()) Self->OpenRun({}); });
	Right->SetContent(Panel.ToSharedRef());
	RenderList();
}

static FString RunProgress(const FZettaRunSummary& Run)
{
	if (Run.Status == ZettaRunStatus::Draft)
		return Run.InputsNeeded > 0 ? FString::Printf(TEXT("%d step%s input"), Run.InputsNeeded, Run.InputsNeeded == 1 ? TEXT(" needs") : TEXT("s need")) : FString(TEXT("Ready to start"));
	return FString::Printf(TEXT("%d of %d step%s"), Run.Done, Run.Total, Run.Total == 1 ? TEXT("") : TEXT("s")) + (Run.Waiting > 0 ? FString::Printf(TEXT(" · %d waiting"), Run.Waiting) : FString());
}

TSharedRef<SWidget> SZettaRunsView::Row(const FZettaRunSummary& Run)
{
	const int64 Id = Run.Id;
	const FString Name = Run.SeedName.IsEmpty() ? FString::Printf(TEXT("Run #%lld"), Run.Id) : Run.SeedName;
	const bool bSelected = Panel.IsValid() && Panel->RunId == Id;
	const FZettaRunSummary Copy = Run;
	TWeakPtr<SZettaRunsView> Weak = SharedThis(this);
	return SNew(SButton)
		.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
		.ContentPadding(0)
		.OnClicked_Lambda([Weak, Id]() { if (const TSharedPtr<SZettaRunsView> Self = Weak.Pin()) Self->OpenRun(Id); return FReply::Handled(); })
		[
			SNew(SBorder)
			.BorderImage(bSelected ? FZettaStyle::Get().GetBrush("Zetta.CardSelected") : FZettaStyle::Get().GetBrush("Zetta.CardRaised"))
			.Padding(6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SZettaPlThumb).Url(Run.SeedThumbnailUrl).Size(FVector2D(40, 40))
					.Placeholder(FText::FromString(Run.SeedType == TEXT("model") ? TEXT("Mdl") : Run.SeedType == TEXT("views") ? TEXT("4V") : TEXT("Pic")))
				]
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(8, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Name)).Font(FZettaStyle::Font(9, true)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(FZettaStyle::Font(8)).ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text(FText::FromString(FString::Printf(TEXT("#%lld · %s · %s"), Run.Id, *RunProgress(Run), *ZettaPipelineText::WhenRun(Run.CreatedAt))))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ZettaPlUI::RunPill(Run.Status)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
				[
					SNew(SComboButton)
					.HasDownArrow(false)
					.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
					.ToolTipText(FText::FromString(TEXT("More for ") + Name))
					.ButtonContent()[SNew(SImage).Image(FAppStyle::GetBrush("Icons.EllipsisVerticalNarrow")).ColorAndOpacity(FSlateColor::UseForeground())]
					.OnGetMenuContent_Lambda([Weak, Copy]()
					{
						FMenuBuilder Menu(true, nullptr);
						const bool bDraft = Copy.Status == ZettaRunStatus::Draft;
						const FString Label = bDraft ? TEXT("Discard run") : TEXT("Delete run");
						if (ZettaRunStatus::IsDeletable(Copy.Status))
						{
							Menu.AddMenuEntry(FText::FromString(Label + TEXT("…")), FText(), FSlateIcon(),
								FUIAction(FExecuteAction::CreateLambda([Weak, Copy]() { if (const TSharedPtr<SZettaRunsView> Self = Weak.Pin()) Self->DeleteFromList(Copy); })));
						}
						else
						{
							Menu.AddMenuEntry(FText::FromString(Label + TEXT(" — still going, cancel it first")), FText(), FSlateIcon(),
								FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
						}
						return Menu.MakeWidget();
					})
				]
			]
		];
}

void SZettaRunsView::DeleteFromList(const FZettaRunSummary& Run)
{
	const bool bDraft = Run.Status == ZettaRunStatus::Draft;
	const FString Name = Run.SeedName.IsEmpty() ? FString::Printf(TEXT("Run #%lld"), Run.Id) : Run.SeedName;
	if (!ZettaPlUI::Confirm(FString::Printf(TEXT("%s run #%lld?"), bDraft ? TEXT("Discard") : TEXT("Delete"), Run.Id),
		bDraft ? FString::Printf(TEXT("“%s” never started — it goes, and nothing was spent."), *Name) : FString::Printf(TEXT("“%s” and its steps go. The models it made stay in your library."), *Name)))
	{
		return;
	}
	TWeakPtr<SZettaRunsView> Weak = SharedThis(this);
	const int64 Id = Run.Id;
	ZettaPipelinesApi::DeleteRun(Id, [Weak, Id](const FZettaResponse& R)
	{
		if (!R.bOk)
		{
			ZettaPlUI::Toast(ZettaCatalog::Explain(R), true);
			return;
		}
		FZettaRunFollower::Get().Unfollow(Id);
		const TSharedPtr<SZettaRunsView> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		if (Self->Panel.IsValid() && Self->Panel->RunId == Id) Self->OpenRun({});
		if (const TSharedPtr<SZettaPipelineEditor> E = Self->Editor.Pin()) E->RefreshRuns();
	});
}

void SZettaRunsView::RenderList()
{
	if (!Filters.IsValid() || !List.IsValid()) return;
	Filters->ClearChildren();
	List->ClearChildren();
	if (!Runs.IsSet())
	{
		List->AddSlot().AutoHeight().Padding(4)[ZettaUI::Hint(LOCTEXT("PlLoadingRuns", "Loading the runs…"))];
		return;
	}
	if (Runs->Num() == 0)
	{
		List->AddSlot().AutoHeight().Padding(4)[ZettaUI::Hint(LOCTEXT("PlNoRunsYet", "No runs yet — start one with New run."))];
		return;
	}
	TWeakPtr<SZettaRunsView> Weak = SharedThis(this);
	const FZettaRunFilter* Active = nullptr;
	for (const FZettaRunFilter& F : RunFilters())
	{
		int32 N = 0;
		for (const FZettaRunSummary& R : Runs.GetValue()) if (F.Statuses.Num() == 0 || F.Statuses.Contains(R.Status)) ++N;
		if (F.Key == Filter) Active = &F;
		if (F.Key != TEXT("all") && N == 0) continue;
		const FString Key = F.Key;
		Filters->AddSlot().AutoWidth().Padding(0, 0, 4, 0)
		[
			SNew(SCheckBox)
			.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox"))
			.IsChecked(Filter == Key ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
			.OnCheckStateChanged_Lambda([Weak, Key](ECheckBoxState)
			{
				if (const TSharedPtr<SZettaRunsView> Self = Weak.Pin())
				{
					Self->Filter = Key;
					Self->RenderList();
				}
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 2, 4, 2)
				[
					SNew(SBox).Visibility(Key == TEXT("all") ? EVisibility::Collapsed : EVisibility::Visible)[ZettaPlUI::RunDot(F.Tone)]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 2, 6, 2)
				[
					SNew(STextBlock).Font(FZettaStyle::Font(8)).Text(FText::FromString(FString::Printf(TEXT("%s %d"), *F.Label, N)))
				]
			]
		];
	}
	for (const FZettaRunSummary& R : Runs.GetValue())
	{
		if (Active && Active->Statuses.Num() > 0 && !Active->Statuses.Contains(R.Status)) continue;
		List->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[Row(R)];
	}
}

#undef LOCTEXT_NAMESPACE
