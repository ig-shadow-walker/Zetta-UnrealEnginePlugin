// Copyright Zetta 3D. See LICENSE.md.

#include "SZettaPostProcessTool.h"

#include "Widgets/SBoxPanel.h"
#include "SZettaSourcePicker.h"
#include "ZettaApi.h"
#include "ZettaAuth.h"
#include "ZettaCatalog.h"
#include "ZettaJobs.h"
#include "ZettaSettings.h"
#include "ZettaRequests.h"
#include "ZettaStudioContext.h"
#include "ZettaStudioUI.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

void SZettaPostProcessTool::ConstructTool()
{
	TSharedRef<SWidget> Form = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
		[
			ZettaStudioUI::Locked(MakeAttributeLambda([]()
			{
				const FZettaEntitlements3D* G = FZettaStudioContext::Get().G3();
				return G && FZettaStudioContext::No(G->AllowsPostProcess)
					? LOCTEXT("FinishingLocked", "The finishing tools are available on a paid plan. Generate 3D works on every plan.")
					: FText();
			}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
		[
			SAssignNew(Picker, SZettaSourcePicker, [this](const FZettaGeneration& Row) { return Refuse(Row); })
			.OnChanged_Lambda([this]()
			{
				OnSourceChanged();
				Refresh();
			})
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			ZettaStudioUI::Group(LOCTEXT("Options", "Options"), BuildOptions())
		];

	ChildSlot
	[
		Frame(Intro(), Form, SAssignNew(Bar, SZettaSubmitBar).ButtonText_Lambda([this]() { return ButtonText(); }))
	];

	TWeakPtr<SZettaPostProcessTool> Weak = StaticCastSharedRef<SZettaPostProcessTool>(AsShared());
	Bar->SetAction([Weak](SZettaSubmitBar::FFinish Finish)
	{
		if (const TSharedPtr<SZettaPostProcessTool> Self = Weak.Pin()) Self->Submit(Finish);
		else Finish(FString());
	});
	Refresh();
}

void SZettaPostProcessTool::OnContextChanged()
{
	if (!FZettaAuth::Get().IsSignedIn() && Picker.IsValid()) Picker->Forget();
	Refresh();
}

FZettaGenerationPtr SZettaPostProcessTool::Source() const
{
	return Picker.IsValid() ? Picker->GetSource() : nullptr;
}

void SZettaPostProcessTool::SetSource(const FZettaGenerationPtr& Generation)
{
	if (Picker.IsValid()) Picker->Set(Generation);
}

FString SZettaPostProcessTool::Refuse(const FZettaGeneration& Row) const
{
	if (!Row.IsSuccess()) return TEXT("That model is not finished yet.");
	if (!ZettaCatalog::IsProcessable(Row.Feature))
	{
		return FString::Printf(TEXT("%s results cannot be used here. Pick a generated or uploaded model."), *ZettaCatalog::FeatureLabel(Row.Feature));
	}
	return FString();
}

void SZettaPostProcessTool::Refresh()
{
	if (!Bar.IsValid()) return;
	const FZettaStudioContext& Ctx = FZettaStudioContext::Get();
	const bool bLocked = Ctx.G3() && FZettaStudioContext::No(Ctx.G3()->AllowsPostProcess);
	Bar->SetPrice(Ctx.Price(Code(), PriceModel()));
	Bar->SetProblem(bLocked ? FString(TEXT("Upgrade to use the finishing tools."))
		: !Source().IsValid() ? FString(TEXT("Choose a source model."))
		: Problem());
}

void SZettaPostProcessTool::Stage(const FString& Text)
{
	if (Bar.IsValid()) Bar->SetStage(Text);
}

void SZettaPostProcessTool::Submit(SZettaSubmitBar::FFinish Finish)
{
	const FZettaGenerationPtr From = Source();
	if (!From.IsValid())
	{
		Finish(TEXT("Choose a source model."));
		return;
	}
	const FString Operation = Code();
	TWeakPtr<SZettaPostProcessTool> Weak = StaticCastSharedRef<SZettaPostProcessTool>(AsShared());
	MakeOptions([Weak, Finish, From, Operation](const FString& Error, const TSharedPtr<FJsonObject>& Options)
	{
		if (!Error.IsEmpty())
		{
			Finish(Error);
			return;
		}
		if (const TSharedPtr<SZettaPostProcessTool> Self = Weak.Pin()) Self->Stage(TEXT("Starting…"));
		ZettaApi::CreateGeneration(ZettaRequests::PostProcess(Operation, From->Id, Options), [Weak, Finish](const FZettaResponse& R, const FZettaGenerationPtr& G)
		{
			if (!R.bOk || !G.IsValid())
			{
				Finish(R.bOk ? FString(TEXT("The job did not start. Try again.")) : ZettaCatalog::Explain(R));
				return;
			}
			if (const TSharedPtr<SZettaPostProcessTool> Self = Weak.Pin()) Self->Bar->Track(G);
			else
			{
				// The Studio closed while the job was being created: it still runs, so it is still followed.
				FZettaImportOptions Import;
				Import.bPlace = UZettaProjectSettings::Get()->bPlaceInLevel;
				FZettaJobs::Get().Track(G, true, Import, TEXT("studio"));
			}
			Finish(FString());
		});
	});
}

#undef LOCTEXT_NAMESPACE
