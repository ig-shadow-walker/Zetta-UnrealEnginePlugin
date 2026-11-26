// Copyright Zetta 3D. See LICENSE.md.

#include "SZettaJobsStrip.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Misc/ConfigCacheIni.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "ZettaImporter.h"
#include "ZettaSettings.h"
#include "ZettaStudioAssets.h"
#include "ZettaStudioUI.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace
{
	const TCHAR* JobsConfigSection = TEXT("Zetta3D.Studio");

	FLinearColor JobColor(const FString& Status)
	{
		if (Status == TEXT("success")) return FZettaStyle::Success();
		if (Status == TEXT("failed") || Status == TEXT("cancelled")) return FZettaStyle::Error();
		if (Status == TEXT("pending") || Status == TEXT("queued")) return FZettaStyle::Warning();
		return FZettaStyle::Accent();
	}
}

void SZettaJobsStrip::Construct(const FArguments& InArgs)
{
	GConfig->GetBool(JobsConfigSection, TEXT("JobsCollapsed"), bCollapsed, GEditorPerProjectIni);

	auto Counts = []()
	{
		int32 Running = 0, Finished = 0;
		for (const FZettaJobPtr& J : FZettaJobs::Get().GetJobs()) (J->IsFinished() ? Finished : Running)++;
		return TPair<int32, int32>(Running, Finished);
	};

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(FMargin(10, 6))
		.Visibility_Lambda([]() { return ZettaStudioUI::Shown(FZettaJobs::Get().GetJobs().Num() > 0); })
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(FZettaStyle::Font(10, true)).Text_Lambda([Counts]()
					{
						const TPair<int32, int32> N = Counts();
						return N.Key > 0
							? FText::Format(LOCTEXT("JobsRunningTitle", "Jobs · {0} running"), N.Key)
							: FText::Format(LOCTEXT("JobsTitle", "Jobs · {0}"), N.Key + N.Value);
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
				[
					SNew(SBox).Visibility_Lambda([this, Counts]() { return ZettaStudioUI::Shown(!bCollapsed && Counts().Value > 0); })
					[
						ZettaStudioUI::SmallButton(LOCTEXT("ClearFinished", "Clear finished"), []() { FZettaJobs::Get().DismissFinished(); })
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					ZettaStudioUI::SmallButton(MakeAttributeLambda([this]() { return bCollapsed ? LOCTEXT("ShowJobs", "Show") : LOCTEXT("HideJobs", "Hide"); }), [this]()
					{
						bCollapsed = !bCollapsed;
						GConfig->SetBool(JobsConfigSection, TEXT("JobsCollapsed"), bCollapsed, GEditorPerProjectIni);
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
			[
				SNew(SBox)
				.MaxDesiredHeight(190)
				.Visibility_Lambda([this]() { return ZettaStudioUI::Shown(!bCollapsed); })
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()[SAssignNew(List, SVerticalBox)]
				]
			]
		]
	];

	ChangedHandle = FZettaJobs::Get().OnChanged.AddSP(this, &SZettaJobsStrip::Render);
	Render();
}

SZettaJobsStrip::~SZettaJobsStrip()
{
	FZettaJobs::Get().OnChanged.Remove(ChangedHandle);
}

void SZettaJobsStrip::Render()
{
	// Drawn on the next tick: a job can change while one of its own buttons is being clicked.
	bDirty = true;
}

void SZettaJobsStrip::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (!bDirty || !List.IsValid()) return;
	bDirty = false;

	// Rows read their text live; they are only rebuilt when the set of jobs or what can be done with one changes.
	FString Shape;
	for (const FZettaJobPtr& J : FZettaJobs::Get().GetJobs())
	{
		Shape += FString::Printf(TEXT("%lld:%s:%d:%d:%d|"), J->Id(), *J->Generation->Status, J->bImporting ? 1 : 0,
			J->Import.IsSet() ? (J->Import->bOk ? 2 : 1) : 0, J->Generation->IsSuccess() && ZettaProjectIndex::Contains(J->Id()) ? 1 : 0);
	}
	if (Shape == Drawn) return;
	Drawn = Shape;

	List->ClearChildren();
	for (const FZettaJobPtr& J : FZettaJobs::Get().GetJobs()) List->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[Row(J)];
}

FString SZettaJobsStrip::Describe(const FZettaJob& Job)
{
	const FString Who = Job.Source == TEXT("agent") ? TEXT("Agent · ") : TEXT("");
	const FZettaGenerationPtr& G = Job.Generation;
	const FString& Status = G->Status;
	if (Status == TEXT("pending")) return Who + TEXT("Waiting to start");
	if (Status == TEXT("queued"))
	{
		return Who + (Job.QueuePosition.IsSet() && Job.QueuePosition.GetValue() > 0 ? FString::Printf(TEXT("Queued · #%d in line"), Job.QueuePosition.GetValue()) : FString(TEXT("Queued")));
	}
	if (Status == TEXT("processing")) return Who + TEXT("Generating…");
	if (Status == TEXT("saving")) return Who + TEXT("Saving the result…");
	if (Status == TEXT("success"))
	{
		if (Job.bImporting)
		{
			const FString Line = Job.ImportStatus.IsEmpty() ? FString(TEXT("Importing…")) : Job.ImportStatus;
			return Who + FString::Printf(TEXT("%s %d%%"), *Line, FMath::RoundToInt(FMath::Clamp(Job.ImportProgress, 0.f, 1.f) * 100.f));
		}
		if (!Job.Error.IsEmpty()) return Who + Job.Error;
		if (G->IsSplat()) return Who + TEXT("Ready · ") + ZettaImporter::SplatUnsupported;
		if (Job.Import.IsSet() && Job.Import->bOk)
		{
			FString Line = Who + TEXT("Ready · imported to ") + Job.Import->Folder;
			if (!Job.Import->UvLayoutPath.IsEmpty()) Line += TEXT(" · UV layout picture saved");
			if (!Job.Import->Note.IsEmpty()) Line += TEXT(" · ") + Job.Import->Note;
			return Line;
		}
		const FString Uv = G->UvLayout() ? TEXT(" · UV layout picture ready") : TEXT("");
		return Who + (Job.bAutoImport ? TEXT("Ready · importing…") : TEXT("Ready")) + Uv;
	}
	if (Status == TEXT("failed")) return Who + (Job.Error.IsEmpty() ? FString(TEXT("Failed. Your credits were refunded.")) : Job.Error);
	if (Status == TEXT("cancelled")) return Who + TEXT("Cancelled");
	return Who + G->StatusLabel();
}

TSharedRef<SWidget> SZettaJobsStrip::Row(const FZettaJobPtr& Job)
{
	const FZettaGenerationPtr G = Job->Generation;
	const FString Status = G->Status;
	const int64 Id = Job->Id();

	TSharedRef<SHorizontalBox> Actions = SNew(SHorizontalBox);
	auto Action = [&Actions](const FText& Text, TFunction<void()> OnClick, const FText& Tip = FText())
	{
		Actions->AddSlot().AutoWidth().Padding(4, 0, 0, 0).VAlign(VAlign_Center)[ZettaStudioUI::SmallButton(Text, OnClick, true, Tip)];
	};

	if (Status == TEXT("pending") || Status == TEXT("queued"))
	{
		Action(LOCTEXT("CancelJob", "Cancel"), [Id]() { FZettaJobs::Get().Cancel(Id); }, LOCTEXT("CancelJobTip", "Stop this job before it starts. Nothing is charged."));
	}
	if (G->IsSuccess() && !Job->bImporting)
	{
		if (G->IsSplat())
		{
			Action(LOCTEXT("OpenOnWeb", "Open on zetta3d.ai"), [Id]() { ZettaUI::OpenSite(FString::Printf(TEXT("/3d-studio?generation=%lld"), Id)); });
		}
		else if (ImportedAssets(Job).Num() > 0)
		{
			Action(LOCTEXT("ShowInBrowser", "Show"), [this, Job]() { ZettaStudioAssets::ShowInContentBrowser(ImportedAssets(Job)); }, LOCTEXT("ShowInBrowserTip", "Show the model in the Content Browser"));
			Action(LOCTEXT("PlaceInLevel", "Place in level"), [this, Job]() { Place(Job); }, LOCTEXT("PlaceInLevelTip", "Place the model in the open level, on the ground in front of the camera"));
		}
		else
		{
			Action(LOCTEXT("ImportJob", "Import"), [this, Job]() { Import(Job); },
				UZettaProjectSettings::Get()->bPlaceInLevel
					? LOCTEXT("ImportPlaceTip", "Download into the project and place it in the open level (Place in Level is on in Project Settings › Zetta 3D)")
					: LOCTEXT("ImportTip", "Download into the project"));
		}
	}
	if (Job->IsFinished())
	{
		Action(FText::FromString(TEXT("×")), [Id]() { FZettaJobs::Get().Dismiss(Id); }, LOCTEXT("DismissJob", "Remove from the list"));
	}

	const TWeakPtr<FZettaJob> Weak = Job;
	return SNew(SBorder)
		.BorderImage(FZettaStyle::Get().GetBrush("Zetta.Card"))
		.Padding(FMargin(8, 5))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SNew(SBox).WidthOverride(8).HeightOverride(8)
				[
					SNew(SBorder)
					.BorderImage(FZettaStyle::Get().GetBrush("Zetta.AccentFill"))
					.BorderBackgroundColor(FSlateColor(JobColor(Status)))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(FText::FromString(G->DisplayTitle())).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Font(FZettaStyle::Font(9))
					.AutoWrapText(true)
					.ColorAndOpacity_Lambda([Weak]()
					{
						const TSharedPtr<FZettaJob> J = Weak.Pin();
						return FSlateColor(J && !J->Error.IsEmpty() ? FZettaStyle::Error() : FZettaStyle::Muted());
					})
					.Text_Lambda([Weak]()
					{
						const TSharedPtr<FZettaJob> J = Weak.Pin();
						return J ? FText::FromString(Describe(*J)) : FText();
					})
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Actions]
		];
}

TArray<FString> SZettaJobsStrip::ImportedAssets(const FZettaJobPtr& Job) const
{
	if (Job->Import.IsSet() && Job->Import->bOk && Job->Import->AssetPaths.Num() > 0) return Job->Import->AssetPaths;
	const FString Main = ZettaProjectIndex::MainAssetOf(Job->Id());
	return Main.IsEmpty() ? TArray<FString>() : TArray<FString>{ Main };
}

void SZettaJobsStrip::Import(const FZettaJobPtr& Job)
{
	FZettaImportOptions Options;
	Options.bPlace = UZettaProjectSettings::Get()->bPlaceInLevel;
	FZettaJobs::Get().ImportNow(Job->Id(), Options);
}

void SZettaJobsStrip::Place(const FZettaJobPtr& Job)
{
	// Every mesh in the model's folder: a segmented result is several parts, placed together.
	FString Folder = Job->Import.IsSet() && Job->Import->bOk ? Job->Import->Folder : ZettaProjectIndex::FolderOf(Job->Id());
	TArray<FString> Meshes;
	if (!Folder.IsEmpty())
	{
		TArray<FAssetData> Assets;
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().GetAssetsByPath(FName(*Folder), Assets, false);
		for (const FAssetData& A : Assets)
		{
			if (A.AssetClassPath == UStaticMesh::StaticClass()->GetClassPathName() || A.AssetClassPath == USkeletalMesh::StaticClass()->GetClassPathName())
			{
				Meshes.Add(A.GetObjectPathString());
			}
		}
		Meshes.Sort();
	}
	if (Meshes.Num() == 0) Meshes = ImportedAssets(Job);
	const FString Label = FString::Printf(TEXT("%lld-%s"), Job->Id(), *ZettaImporter::Slug(Job->Generation->DisplayTitle()));
	ZettaImporter::Place(Meshes, Label, FZettaImportOptions());
}

#undef LOCTEXT_NAMESPACE
