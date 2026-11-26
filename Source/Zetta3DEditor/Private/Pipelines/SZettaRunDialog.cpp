// Copyright Zetta 3D. See LICENSE.md.

// RUN ON…: the seeds to run on — pictures or models chosen now, each its own run (a batch of up to 20), or, for a
// Multi-view seed, sets of pictures of each side, a run per set — and Auto or Manual. It MAKES the runs; it does not
// start them: each opens in Runs not started (a draft), with any step set on each run marked Needs input.

#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "SZettaPipelineEditor.h"
#include "ZettaCatalog.h"
#include "ZettaPipelineJson.h"
#include "ZettaPipelineServices.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

using namespace ZettaPJson;

namespace ZettaRunDialog
{
	/** The most runs one press makes — the server's own cap. */
	static constexpr int32 MaxBatch = 20;

	struct FState
	{
		TArray<FZettaPipelineSeed> Seeds;
		TArray<TSharedRef<FJsonObject>> Sets;
		int32 Uploading = 0;
		FString Mode = ZettaRunStatus::ModeManual;
		bool bImport = true;
		bool bBusy = false;
		FString Error;
		bool bPictures = true;
		bool bViews = false;
		TFunction<void()> Close;
		TFunction<void()> Render;
		TSharedPtr<SWrapBox> Grid;
		TSharedPtr<SVerticalBox> SetsBox;

		bool Full() const { return Seeds.Num() + Uploading >= MaxBatch; }

		/** The runs the dialog would make: one per seed — or per set with its front. */
		TArray<FZettaPipelineSeed> Runs() const
		{
			if (!bViews) return Seeds;
			TArray<FZettaPipelineSeed> Out;
			for (const TSharedRef<FJsonObject>& Set : Sets) if (SZettaViewSlots::HasFront(Set)) Out.Add(FZettaPipelineSeed::FromViews(Set));
			return Out;
		}

		/** A set started without its front holds the button back. */
		bool Unfinished() const
		{
			if (!bViews) return false;
			for (const TSharedRef<FJsonObject>& Set : Sets) if (!SZettaViewSlots::HasFront(Set) && Set->Values.Num() > 0) return true;
			return false;
		}
	};

	static void AddSeed(const TSharedRef<FState>& S, const FZettaPipelineSeed& Seed)
	{
		if (S->Seeds.Num() >= MaxBatch) return;
		S->Seeds.Add(Seed);
		if (S->Render) S->Render();
	}

	static void UploadPicture(const TSharedRef<FState>& S, const TArray<uint8>& Bytes, const FString& ContentType, const FString& Name)
	{
		if (S->Full()) return;
		++S->Uploading;
		S->Render();
		TWeakPtr<FState> Weak = S;
		FZettaPipelineSeed::FromUpload(Bytes, ContentType, Name, [Weak](const FZettaResponse& R, const FZettaPipelineSeed& Seed)
		{
			const TSharedPtr<FState> St = Weak.Pin();
			if (!St.IsValid()) return;
			--St->Uploading;
			if (R.bOk) AddSeed(St.ToSharedRef(), Seed);
			else St->Error = ZettaCatalog::Explain(R);
			St->Render();
		});
	}

	static void UploadFile(const TSharedRef<FState>& S, const FString& Path)
	{
		TArray<uint8> Bytes;
		FString Type;
		const FString Problem = ZettaUI::ReadImageFile(Path, Bytes, Type);
		if (!Problem.IsEmpty())
		{
			S->Error = Problem;
			return;
		}
		UploadPicture(S, Bytes, Type, FPaths::GetCleanFilename(Path));
	}

	static void UseTexture(const TSharedRef<FState>& S, const FString& ObjectPath)
	{
		TArray<uint8> Bytes;
		FString Type, Name, Error;
		if (!ZettaPipelineAssets::PictureOfTexture(ObjectPath, Bytes, Type, Name, Error))
		{
			S->Error = Error;
			return;
		}
		UploadPicture(S, Bytes, Type, Name);
	}

	static void UseModel(const TSharedRef<FState>& S, const FString& Path)
	{
		if (S->Full()) return;
		++S->Uploading;
		S->Render();
		TWeakPtr<FState> Weak = S;
		ZettaPipelineAssets::ModelFromAsset(Path, [Weak](const FZettaGenerationPtr& Row, const FString& Error)
		{
			const TSharedPtr<FState> St = Weak.Pin();
			if (!St.IsValid()) return;
			--St->Uploading;
			if (!Row.IsValid()) St->Error = Error;
			else if (!St->Seeds.ContainsByPredicate([&Row](const FZettaPipelineSeed& Seed) { return Seed.ModelPostId() == Row->Id; })) AddSeed(St.ToSharedRef(), FZettaPipelineSeed::FromModel(*Row));
			St->Render();
		});
	}

	static TSharedRef<SWidget> Tile(const TSharedRef<FState>& S, int32 Index)
	{
		const FZettaPipelineSeed& Seed = S->Seeds[Index];
		TSharedRef<SZettaPlThumb> Thumb = SNew(SZettaPlThumb).Size(FVector2D(96, 96)).Url(Seed.ThumbnailUrl).Placeholder(FText::FromString(Seed.Type == TEXT("model") ? TEXT("Model") : TEXT("Picture")));
		if (Seed.Thumbnail.Num() > 0) Thumb->SetBytes(Seed.Thumbnail);
		TWeakPtr<FState> Weak = S;
		return SNew(SBox).WidthOverride(100)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[Thumb]
					+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(4)
					[
						SNew(SButton)
						.ButtonStyle(&FZettaStyle::Get().GetWidgetStyle<FButtonStyle>("Zetta.Button.Close"))
				.ContentPadding(FMargin(4))
						.ToolTipText(FText::FromString(TEXT("Remove ") + Seed.Name))
						.OnClicked_Lambda([Weak, Index]()
						{
							if (const TSharedPtr<FState> St = Weak.Pin())
							{
								if (St->Seeds.IsValidIndex(Index)) St->Seeds.RemoveAt(Index);
								St->Render();
							}
							return FReply::Handled();
						})
						[
							SNew(SBox).WidthOverride(10).HeightOverride(10)[SNew(SImage).Image(FAppStyle::GetBrush("Icons.X")).ColorAndOpacity(FLinearColor::White)]
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
				[
					SNew(STextBlock).Text(FText::FromString(Seed.Name)).Font(FZettaStyle::Font(8)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
			];
	}

	static void RenderSets(const TSharedRef<FState>& S, const FZettaPlOverlayRef& Overlay)
	{
		if (!S->SetsBox.IsValid()) return;
		S->SetsBox->ClearChildren();
		TWeakPtr<FState> Weak = S;
		for (int32 I = 0; I < S->Sets.Num(); ++I)
		{
			S->SetsBox->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
			[
				ZettaUI::Card(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("Run %d"), I + 1))).Font(FZettaStyle::Font(9, true))]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SBox).Visibility(S->Sets.Num() > 1 ? EVisibility::Visible : EVisibility::Collapsed)
							[
								ZettaPlUI::GhostButton(LOCTEXT("PlRemoveSet", "Remove this set"), FSimpleDelegate::CreateLambda([Weak, I, Overlay]()
								{
									const TSharedPtr<FState> St = Weak.Pin();
									if (!St.IsValid() || !St->Sets.IsValidIndex(I)) return;
									St->Sets.RemoveAt(I);
									RenderSets(St.ToSharedRef(), Overlay);
								}))
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SZettaViewSlots, Overlay, [Weak, I]() -> TSharedPtr<FJsonObject>
						{
							const TSharedPtr<FState> St = Weak.Pin();
							return St.IsValid() && St->Sets.IsValidIndex(I) ? TSharedPtr<FJsonObject>(St->Sets[I]) : nullptr;
						}, [Weak, I](SZettaViewSlots::FUpdate Update)
						{
							const TSharedPtr<FState> St = Weak.Pin();
							if (St.IsValid() && St->Sets.IsValidIndex(I)) St->Sets[I] = Update(St->Sets[I]);
						})
					], 8)
			];
		}
	}

	void Show(const TSharedRef<SZettaPipelineEditor>& Editor, TFunction<void(const TArray<FZettaRunSummary>&, bool)> Created)
	{
		const FZettaPipelineGraph Graph = Editor->GetGraph();
		const FZettaPipelineNode* SeedNode = Graph.Seed();
		TSharedRef<FState> S = MakeShared<FState>();
		S->bViews = SeedNode && SeedNode->Kind == ZettaNodeKinds::SeedViews;
		S->bPictures = !(SeedNode && SeedNode->Kind == ZettaNodeKinds::SeedModel) && !S->bViews;
		S->Sets.Add(MakeShared<FJsonObject>());
		const TOptional<int32> PerRun = Editor->CreditsPerRun();
		const FZettaPlOverlayRef Overlay = Editor->GetOverlay();
		TWeakPtr<FState> Weak = S;
		TWeakPtr<SZettaPipelineEditor> WeakEditor = Editor;

		S->Render = [Weak]()
		{
			const TSharedPtr<FState> St = Weak.Pin();
			if (!St.IsValid() || !St->Grid.IsValid()) return;
			St->Grid->ClearChildren();
			for (int32 I = 0; I < St->Seeds.Num(); ++I) St->Grid->AddSlot()[Tile(St.ToSharedRef(), I)];
			if (St->Uploading > 0)
			{
				St->Grid->AddSlot()
				[
					SNew(SBox).WidthOverride(96).HeightOverride(96).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("PlUploadingSeeds", "Uploading…")).ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
				];
			}
		};

		// The ways in: files, the user's library, the Content Browser selection — or, for a Multi-view seed, sets.
		TSharedRef<SWrapBox> Buttons = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4, 4));
		auto NotFull = MakeAttributeLambda([Weak]() { const TSharedPtr<FState> St = Weak.Pin(); return St.IsValid() && !St->Full() && !St->bBusy; });
		if (S->bViews)
		{
			Buttons->AddSlot()
			[
				ZettaPlUI::SmallButton(LOCTEXT("PlAnotherSet", "Add another set"), FSimpleDelegate::CreateLambda([Weak, Overlay]()
				{
					const TSharedPtr<FState> St = Weak.Pin();
					if (!St.IsValid() || St->Sets.Num() >= MaxBatch) return;
					St->Sets.Add(MakeShared<FJsonObject>());
					RenderSets(St.ToSharedRef(), Overlay);
				}), MakeAttributeLambda([Weak]() { const TSharedPtr<FState> St = Weak.Pin(); return St.IsValid() && St->Sets.Num() < MaxBatch && !St->bBusy; }))
			];
		}
		else if (S->bPictures)
		{
			Buttons->AddSlot()
			[
				ZettaPlUI::SmallButton(LOCTEXT("PlUploadPictures", "Upload pictures"), FSimpleDelegate::CreateLambda([Weak]()
				{
					const TSharedPtr<FState> St = Weak.Pin();
					if (!St.IsValid()) return;
					St->Error.Reset();
					for (const FString& Path : ZettaUI::PickFiles(TEXT("Choose pictures"), ZettaUI::ImageFilter())) UploadFile(St.ToSharedRef(), Path);
				}), NotFull)
			];
			Buttons->AddSlot()
			[
				ZettaPlUI::SmallButton(LOCTEXT("PlFromYourImages", "From your images"), FSimpleDelegate::CreateLambda([Weak, Overlay]()
				{
					const TSharedPtr<FState> St = Weak.Pin();
					if (!St.IsValid()) return;
					St->Error.Reset();
					ZettaPlPickers::PickImages(Overlay, true, [Weak]()
					{
						TArray<int64> Ids;
						if (const TSharedPtr<FState> S2 = Weak.Pin()) for (const FZettaPipelineSeed& Seed : S2->Seeds) if (Seed.ImagePostId().IsSet()) Ids.Add(Seed.ImagePostId().GetValue());
						return Ids;
					}, [Weak](const FZettaPickedImage& Picked)
					{
						const TSharedPtr<FState> S2 = Weak.Pin();
						if (!S2.IsValid()) return false;
						const int32 Existing = S2->Seeds.IndexOfByPredicate([&Picked](const FZettaPipelineSeed& Seed) { return Seed.ImagePostId() == Picked.Id; });
						if (Existing != INDEX_NONE)
						{
							S2->Seeds.RemoveAt(Existing);
							S2->Render();
							return false;
						}
						if (S2->Seeds.Num() >= MaxBatch) return false;
						AddSeed(S2.ToSharedRef(), FZettaPipelineSeed::FromImagePost(Picked.Id, Picked.Name, Picked.Url));
						return true;
					});
				}), NotFull)
			];
			Buttons->AddSlot()
			[
				ZettaPlUI::SmallButton(LOCTEXT("PlFromProject", "From the project"), FSimpleDelegate::CreateLambda([Weak]()
				{
					const TSharedPtr<FState> St = Weak.Pin();
					if (!St.IsValid()) return;
					St->Error.Reset();
					const TArray<FString> Selected = ZettaPipelineAssets::SelectedAssets();
					if (Selected.Num() == 0)
					{
						St->Error = TEXT("Select one or more pictures (textures) in the Content Browser first.");
						return;
					}
					for (const FString& Path : Selected) if (!St->Full()) UseTexture(St.ToSharedRef(), Path);
				}), NotFull, LOCTEXT("PlFromProjectTip", "Uses the textures selected in the Content Browser."))
			];
		}
		else
		{
			Buttons->AddSlot()
			[
				ZettaPlUI::SmallButton(LOCTEXT("PlFromLibrary", "From your library"), FSimpleDelegate::CreateLambda([Weak, Overlay]()
				{
					const TSharedPtr<FState> St = Weak.Pin();
					if (!St.IsValid()) return;
					St->Error.Reset();
					ZettaPlPickers::PickModels(Overlay, [Weak]()
					{
						TArray<int64> Ids;
						if (const TSharedPtr<FState> S2 = Weak.Pin()) for (const FZettaPipelineSeed& Seed : S2->Seeds) if (Seed.ModelPostId().IsSet()) Ids.Add(Seed.ModelPostId().GetValue());
						return Ids;
					}, [Weak](const FZettaGenerationPtr& Generation)
					{
						const TSharedPtr<FState> S2 = Weak.Pin();
						if (!S2.IsValid()) return false;
						const int32 Existing = S2->Seeds.IndexOfByPredicate([&Generation](const FZettaPipelineSeed& Seed) { return Seed.ModelPostId() == Generation->Id; });
						if (Existing != INDEX_NONE)
						{
							S2->Seeds.RemoveAt(Existing);
							S2->Render();
							return false;
						}
						if (S2->Seeds.Num() >= MaxBatch) return false;
						AddSeed(S2.ToSharedRef(), FZettaPipelineSeed::FromModel(*Generation));
						return true;
					});
				}), NotFull)
			];
			Buttons->AddSlot()
			[
				ZettaPlUI::SmallButton(LOCTEXT("PlModelFromProject", "From the project"), FSimpleDelegate::CreateLambda([Weak]()
				{
					const TSharedPtr<FState> St = Weak.Pin();
					if (!St.IsValid()) return;
					St->Error.Reset();
					TArray<FString> Models;
					for (const FString& Path : ZettaPipelineAssets::SelectedAssets()) if (ZettaPipelineAssets::IsModelSource(Path)) Models.Add(Path);
					if (Models.Num() == 0)
					{
						St->Error = TEXT("Select a model imported from Zetta 3D in the Content Browser first — or upload a .glb.");
						return;
					}
					for (const FString& Path : Models) UseModel(St.ToSharedRef(), Path);
				}), NotFull, LOCTEXT("PlModelFromProjectTip", "Uses the models selected in the Content Browser that were imported from Zetta 3D."))
			];
			Buttons->AddSlot()
			[
				ZettaPlUI::SmallButton(LOCTEXT("PlUploadGlb", "Upload a .glb"), FSimpleDelegate::CreateLambda([Weak]()
				{
					const TSharedPtr<FState> St = Weak.Pin();
					if (!St.IsValid()) return;
					St->Error.Reset();
					for (const FString& Path : ZettaUI::PickFiles(TEXT("Choose models"), TEXT("glTF binary (*.glb)|*.glb"))) UseModel(St.ToSharedRef(), Path);
				}), NotFull, LOCTEXT("PlUploadGlbTip", "Uploads a .glb to your library (free) and runs on it."))
			];
		}

		auto ModeCard = [Weak](const FString& Mode)
		{
			return SNew(SCheckBox)
				.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox"))
				.IsChecked_Lambda([Weak, Mode]() { const TSharedPtr<FState> St = Weak.Pin(); return St.IsValid() && St->Mode == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([Weak, Mode](ECheckBoxState) { if (const TSharedPtr<FState> St = Weak.Pin()) St->Mode = Mode; })
				[
					SNew(SBox).WidthOverride(240).Padding(FMargin(10, 6))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(ZettaRunStatus::ModeLabel(Mode))).Font(FZettaStyle::Font(10, true))]
						+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(ZettaRunStatus::ModeHint(Mode))).AutoWrapText(true).Font(FZettaStyle::Font(8))]
					]
				];
		};

		TSharedRef<SZettaPlDropTarget> Drop = SNew(SZettaPlDropTarget)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).Visibility(S->bViews ? EVisibility::Collapsed : EVisibility::Visible)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SAssignNew(S->Grid, SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6, 6))]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
					[
						SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Thumb")).Padding(12).HAlign(HAlign_Center)
						[
							SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())
							.Text(S->bPictures ? LOCTEXT("PlDropPictures", "Drop pictures here — from your desktop or the Content Browser.")
								: LOCTEXT("PlDropModels", "Drop a .glb (or a model imported from Zetta 3D) here."))
						]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SVerticalBox)
				.Visibility(S->bViews ? EVisibility::Visible : EVisibility::Collapsed)
				+ SVerticalBox::Slot().AutoHeight()[SAssignNew(S->SetsBox, SVerticalBox)]
				+ SVerticalBox::Slot().AutoHeight()
				[
					ZettaUI::Hint(MakeAttributeLambda([Weak]()
					{
						const TSharedPtr<FState> St = Weak.Pin();
						return St.IsValid() && St->Unfinished() ? LOCTEXT("PlNeedsFront", "Each set needs a front picture before it can be a run.") : FText();
					}))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)[Buttons]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 4)[SNew(STextBlock).Text(LOCTEXT("PlHowItRuns", "How it runs")).Font(FZettaStyle::Font(9, true))]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[ModeCard(ZettaRunStatus::ModeManual)]
				+ SHorizontalBox::Slot().AutoWidth()[ModeCard(ZettaRunStatus::ModeAuto)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
			[
				ZettaPlUI::Check(LOCTEXT("PlImportWhenDone", "Import results into the project when done"),
					[Weak]() { const TSharedPtr<FState> St = Weak.Pin(); return St.IsValid() && St->bImport; },
					[Weak](bool bOn) { if (const TSharedPtr<FState> St = Weak.Pin()) St->bImport = bOn; },
					LOCTEXT("PlImportWhenDoneTip", "Each run's final models are imported into your models folder as they finish (never a splat)."))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[ZettaUI::ErrorText(MakeAttributeLambda([Weak]() { const TSharedPtr<FState> St = Weak.Pin(); return FText::FromString(St.IsValid() ? St->Error : FString()); }))]
		];
		Drop->Accepts = MakeAttributeLambda([Weak]() { const TSharedPtr<FState> St = Weak.Pin(); return St.IsValid() && !St->bViews && !St->Full() && !St->bBusy; });
		Drop->OnDropped = [Weak](const TArray<FString>& Files, const TArray<FString>& Assets)
		{
			const TSharedPtr<FState> St = Weak.Pin();
			if (!St.IsValid()) return;
			St->Error.Reset();
			for (const FString& Path : Assets)
			{
				if (St->bPictures) UseTexture(St.ToSharedRef(), Path);
				else if (ZettaPipelineAssets::IsModelSource(Path)) UseModel(St.ToSharedRef(), Path);
				else St->Error = TEXT("That is not a model imported from Zetta 3D. Drop a .glb file, or a model imported from Zetta 3D.");
			}
			for (const FString& File : Files)
			{
				if (St->bPictures) UploadFile(St.ToSharedRef(), File);
				else UseModel(St.ToSharedRef(), File);
			}
		};

		TSharedRef<SWidget> Footer = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(FZettaStyle::Font(9, true)).Text_Lambda([Weak, PerRun]()
				{
					const TSharedPtr<FState> St = Weak.Pin();
					const int32 N = St.IsValid() ? St->Runs().Num() : 0;
					return !PerRun.IsSet() || N == 0 ? FText() : FText::FromString(FString::Printf(TEXT("Up to %d credits"), PerRun.GetValue() * N));
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
			[
				ZettaPlUI::GhostButton(LOCTEXT("PlCancel", "Cancel"), FSimpleDelegate::CreateLambda([Weak]() { if (const TSharedPtr<FState> St = Weak.Pin()) if (St->Close) St->Close(); }))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
				.IsEnabled_Lambda([Weak]()
				{
					const TSharedPtr<FState> St = Weak.Pin();
					return St.IsValid() && !St->bBusy && St->Uploading == 0 && St->Runs().Num() > 0 && !St->Unfinished();
				})
				.OnClicked_Lambda([Weak, WeakEditor, Created]()
				{
					const TSharedPtr<FState> St = Weak.Pin();
					const TSharedPtr<SZettaPipelineEditor> E = WeakEditor.Pin();
					if (!St.IsValid() || !E.IsValid() || St->bBusy) return FReply::Handled();
					const TArray<FZettaPipelineSeed> Runs = St->Runs();
					if (Runs.Num() == 0) return FReply::Handled();
					St->bBusy = true;
					St->Error.Reset();
					TArray<TSharedPtr<FJsonObject>> Seeds;
					for (const FZettaPipelineSeed& Seed : Runs) Seeds.Add(Seed.Request);
					const FZettaPipelineGraph RunGraph = ZettaRequestBuilder::WithRequests(ZettaRequestBuilder::ForNewRun(E->GetGraph()), E->Gates());
					const TWeakPtr<FState> WeakState = St;
					ZettaPipelinesApi::Batch(E->GetPipelineId(), St->Mode, Seeds, &RunGraph, true, [WeakState, Created](const FZettaResponse& R, const TArray<FZettaRunSummary>& Made)
					{
						const TSharedPtr<FState> S2 = WeakState.Pin();
						if (!S2.IsValid()) return;
						S2->bBusy = false;
						if (!R.bOk)
						{
							S2->Error = ZettaCatalog::Explain(R);
							return;
						}
						const bool bImport = S2->bImport;
						if (S2->Close) S2->Close();
						if (Created) Created(Made, bImport);
					});
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Font(FZettaStyle::Font(10, true)).Text_Lambda([Weak]()
					{
						const TSharedPtr<FState> St = Weak.Pin();
						if (!St.IsValid()) return FText();
						if (St->bBusy) return LOCTEXT("PlCreating", "Creating…");
						const int32 N = St->Runs().Num();
						return N > 1 ? FText::FromString(FString::Printf(TEXT("Create %d runs"), N)) : LOCTEXT("PlCreateRun", "Create the run");
					})
				]
			];

		const FString What = S->bViews ? TEXT("set of pictures") : S->bPictures ? TEXT("picture") : TEXT("model");
		const FText Lead = FText::FromString(FString::Printf(TEXT("Each %s gets its own run of this pipeline, up to %d at once. The runs open ready to check — each starts when you press Start."), *What, MaxBatch));
		// The dialog keeps its own state alive for as long as it is open.
		TSharedRef<SWidget> Card = SNew(SZettaPlTicker, [S]() {})[ZettaPlUI::Dialog(LOCTEXT("PlRunOn", "Run on…"), Lead, Drop, Footer, 720.f)];
		S->Close = Overlay->Show(Card);
		RenderSets(S, Overlay);
		S->Render();
	}
}

#undef LOCTEXT_NAMESPACE
