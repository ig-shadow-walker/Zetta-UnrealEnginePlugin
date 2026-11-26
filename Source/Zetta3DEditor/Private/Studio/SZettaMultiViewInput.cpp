// Copyright Zetta 3D. See LICENSE.md.

#include "SZettaMultiViewInput.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "ZettaAiEdit.h"
#include "ZettaApi.h"
#include "ZettaCatalog.h"
#include "ZettaStudioAssets.h"
#include "ZettaStudioUI.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace
{
	const TCHAR* ViewEditPreamble =
		TEXT("The attached image is a reference photograph of a single object. Re-render that SAME object from a different camera angle. ")
		TEXT("Keep its shape, proportions, colours, materials and surface details identical — this is one object photographed again, not a new design, ")
		TEXT("so do not restyle it and do not add or remove any part of it. Place the camera ");

	FString ViewAngleFor(const FString& Slot)
	{
		if (Slot == TEXT("front")) return TEXT("directly in front of the object at its own eye level, looking straight at it head-on.");
		if (Slot == TEXT("back")) return TEXT("directly behind the object at its own eye level. The face that pointed at the camera in the reference is now hidden, and what was hidden is now facing us.");
		if (Slot == TEXT("left"))
		{
			return TEXT("directly beside the object's left flank at its own eye level, looking horizontally at it. A flat side-on profile: the front and the back are both hidden, ")
				TEXT("and the opposite flank is entirely behind the object. In the frame the object's FRONT points to the LEFT edge and its BACK to the RIGHT edge.");
		}
		return TEXT("directly beside the object's right flank at its own eye level, looking horizontally at it. A flat side-on profile: the front and the back are both hidden, ")
			TEXT("and the opposite flank is entirely behind the object. In the frame the object's FRONT points to the RIGHT edge and its BACK to the LEFT edge — ")
			TEXT("this is the mirror image of the left-hand view, not a repeat of it.");
	}

	FText ViewSlotLabel(const FString& Slot)
	{
		if (Slot == TEXT("front")) return LOCTEXT("ViewFront", "Front");
		if (Slot == TEXT("back")) return LOCTEXT("ViewBack", "Back");
		if (Slot == TEXT("left")) return LOCTEXT("ViewLeft", "Left");
		return LOCTEXT("ViewRight", "Right");
	}
}

const TArray<FString>& SZettaMultiViewInput::Slots()
{
	static const TArray<FString> L = { TEXT("front"), TEXT("back"), TEXT("left"), TEXT("right") };
	return L;
}

const TArray<FString>& SZettaMultiViewInput::SubmitOrder()
{
	static const TArray<FString> L = { TEXT("left"), TEXT("right"), TEXT("back"), TEXT("front") };
	return L;
}

void SZettaMultiViewInput::Construct(const FArguments& InArgs)
{
	OnChanged = InArgs._OnChanged;
	TSharedRef<SWrapBox> Grid = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.f, 8.f));
	for (const FString& Slot : Slots())
	{
		TSharedPtr<SZettaImageInput> Picker;
		Grid->AddSlot()
		[
			SNew(SBox).WidthOverride(128)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ZettaStudioUI::FieldLabel(ViewSlotLabel(Slot))]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
					[
						SNew(SBox).Visibility_Lambda([this, Slot]() { return ZettaStudioUI::Shown(ReferenceSlot() == Slot); })
						[
							ZettaStudioUI::Chip(LOCTEXT("ReferenceChip", "Reference"))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).Visibility_Lambda([this, Slot]() { return ZettaStudioUI::Shown(HasView(Slot)); })
						[
							ZettaStudioUI::SmallButton(LOCTEXT("AiEdit", "AI edit"), [this, Slot]() { EditView(Slot); },
								MakeAttributeLambda([this]() { return !bGenerating; }), LOCTEXT("AiEditViewTip", "Change this view with a sentence"))
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox)
					.IsEnabled_Lambda([this, Slot]() { return !bGenerating && !SlotLocked(Slot); })
					.ToolTipText_Lambda([this, Slot]() { return SlotLocked(Slot) ? FText::FromString(BudgetReason) : FText(); })
					[
						SAssignNew(Picker, SZettaImageInput)
						.Size(FVector2D(120, 120))
						.Label(LOCTEXT("DropOrClick", "Drop or click"))
						.OnChanged(FSimpleDelegate::CreateSP(this, &SZettaMultiViewInput::OnPicked, Slot))
					]
				]
			]
		];
		Pickers.Add(Slot, Picker);
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Grid]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0).HAlign(HAlign_Left)
		[
			ZettaStudioUI::SmallButton(
				MakeAttributeLambda([this]() { return bGenerating ? LOCTEXT("GeneratingViews", "Generating views…") : LOCTEXT("GenerateMissing", "Generate missing views"); }),
				[this]() { GenerateViews(); },
				MakeAttributeLambda([this]() { return !ReferenceSlot().IsEmpty() && Filled() < Slots().Num() && !bGenerating && Filled() < Budget; }),
				LOCTEXT("GenerateMissingTip", "Draws the empty angles from your reference view, keeping the object the same."))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[ZettaStudioUI::Hint(MakeAttributeLambda([this]() { return HelperText(); }))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[ZettaStudioUI::Error(MakeAttributeLambda([this]() { return FText::FromString(Error); }))]
	];
}

bool SZettaMultiViewInput::HasView(const FString& Slot) const
{
	const TSharedPtr<SZettaImageInput>* P = Pickers.Find(Slot);
	return P && P->IsValid() && (*P)->HasImage();
}

int32 SZettaMultiViewInput::Filled() const
{
	int32 N = 0;
	for (const FString& Slot : Slots()) if (HasView(Slot)) ++N;
	return N;
}

FString SZettaMultiViewInput::ReferenceSlot() const
{
	for (const FString& Slot : Slots()) if (HasView(Slot)) return Slot;
	return FString();
}

bool SZettaMultiViewInput::SlotLocked(const FString& Slot) const
{
	return !HasView(Slot) && Filled() >= Budget;
}

void SZettaMultiViewInput::SetBudget(const TOptional<int64>& MaxViews)
{
	Budget = (int32)FMath::Clamp<int64>(MaxViews.Get(4), 0, 4);
	BudgetReason = Budget < 4 ? FString::Printf(TEXT("Your plan uses up to %d views. All 4 are available on paid plans."), Budget) : FString();
}

TArray<SZettaMultiViewInput::FView> SZettaMultiViewInput::Views() const
{
	TArray<FView> Out;
	for (const FString& Slot : SubmitOrder())
	{
		if (!HasView(Slot)) continue;
		const TSharedPtr<SZettaImageInput>& P = Pickers[Slot];
		Out.Add({ Slot, P->GetBytes(), P->GetContentType() });
	}
	return Out;
}

TArray<FString> SZettaMultiViewInput::FilledSlots() const
{
	TArray<FString> Out;
	for (const FString& Slot : SubmitOrder()) if (HasView(Slot)) Out.Add(Slot);
	return Out;
}

FString SZettaMultiViewInput::ReferenceName() const
{
	const FString Slot = ReferenceSlot();
	return Slot.IsEmpty() ? FString() : Pickers[Slot]->GetFileName();
}

void SZettaMultiViewInput::SetView(const FString& Slot, const TArray<uint8>& Bytes, const FString& Name)
{
	const TSharedPtr<SZettaImageInput>* P = Pickers.Find(Slot);
	if (!P || !P->IsValid()) return;
	TGuardValue<bool> Guard(bSetting, true);
	(*P)->SetImage(Bytes, Name);
}

void SZettaMultiViewInput::ClearViews()
{
	{
		TGuardValue<bool> Guard(bSetting, true);
		for (const TPair<FString, TSharedPtr<SZettaImageInput>>& Pair : Pickers) if (Pair.Value->HasImage()) Pair.Value->Clear();
	}
	Error.Reset();
	OnChanged.ExecuteIfBound();
}

void SZettaMultiViewInput::OnPicked(FString Slot)
{
	const TSharedPtr<SZettaImageInput> Picker = Pickers.FindRef(Slot);
	if (!bSetting && Picker.IsValid() && Picker->HasImage())
	{
		const FString Problem = ZettaStudioAssets::CheckImage(Picker->GetBytes(), Picker->GetContentType());
		if (!Problem.IsEmpty())
		{
			Error = Problem;
			{
				TGuardValue<bool> Guard(bSetting, true);
				Picker->Clear();
			}
			OnChanged.ExecuteIfBound();
			return;
		}
	}
	if (!bSetting) Error.Reset();
	OnChanged.ExecuteIfBound();
}

void SZettaMultiViewInput::EditView(FString Slot)
{
	const TSharedPtr<SZettaImageInput> Picker = Pickers.FindRef(Slot);
	if (!Picker.IsValid() || !Picker->HasImage() || bGenerating) return;
	const FString Name = Picker->GetFileName().IsEmpty() ? Slot : Picker->GetFileName();
	TWeakPtr<SZettaMultiViewInput> Weak = SharedThis(this);
	ZettaAiEdit::Open(FText::Format(LOCTEXT("ViewEditTitle", "{0} view"), ViewSlotLabel(Slot)), Picker->GetBytes(), Picker->GetContentType(),
		[Weak, Slot, Name](const TArray<uint8>& Bytes, const FString& Type)
		{
			const TSharedPtr<SZettaMultiViewInput> Self = Weak.Pin();
			if (!Self) return;
			Self->Error.Reset();
			Self->SetView(Slot, Bytes, ZettaStudioAssets::Renamed(Name, Type));
			Self->OnChanged.ExecuteIfBound();
		}, LOCTEXT("UseThisView", "Use this view"));
}

FText SZettaMultiViewInput::HelperText() const
{
	const FString Reference = ReferenceSlot();
	const int32 N = Filled();
	const int32 Empty = Slots().Num() - N;
	if (Reference.IsEmpty()) return LOCTEXT("UploadOneView", "Upload one view — the rest can be generated from it.");
	if (N >= Budget && !BudgetReason.IsEmpty()) return FText::Format(LOCTEXT("ViewsReadyBudget", "{0} views ready. {1}"), N, FText::FromString(BudgetReason));
	if (Empty == 0) return FText::Format(LOCTEXT("ViewsReady", "{0} views ready."), N);
	return FText::Format(LOCTEXT("ViewsMore", "{0} views ready. {1} more can be generated from your {2} view."), N, FMath::Min(Empty, Budget - N), ViewSlotLabel(Reference));
}

void SZettaMultiViewInput::GenerateViews()
{
	const FString Reference = ReferenceSlot();
	if (Reference.IsEmpty() || bGenerating) return;
	TArray<FString> Targets;
	for (const FString& Slot : Slots())
	{
		if (!HasView(Slot) && Targets.Num() < FMath::Max(0, Budget - Filled())) Targets.Add(Slot);
	}
	if (Targets.Num() == 0) return;

	bGenerating = true;
	Error.Reset();
	OnChanged.ExecuteIfBound();

	TArray<uint8> Bytes = Pickers[Reference]->GetBytes();
	FString Type = Pickers[Reference]->GetContentType();
	ZettaStudioAssets::Downscale(Bytes, Type);
	const FZettaInlineImage Image = FZettaInlineImage::From(Bytes, Type);

	struct FRun
	{
		TArray<FString> Targets;
		int32 Index = 0;
		int32 Produced = 0;
		FString LastError;
	};
	TSharedRef<FRun> Run = MakeShared<FRun>();
	Run->Targets = Targets;
	TWeakPtr<SZettaMultiViewInput> Weak = SharedThis(this);
	TSharedRef<TFunction<void()>> Next = MakeShared<TFunction<void()>>();

	// One angle at a time: each is its own edit of the same reference.
	*Next = [Weak, Run, Next, Image]()
	{
		const TSharedPtr<SZettaMultiViewInput> Self = Weak.Pin();
		if (!Self)
		{
			*Next = nullptr;
			return;
		}
		if (Run->Index >= Run->Targets.Num())
		{
			Self->bGenerating = false;
			if (Run->Produced == 0) Self->Error = Run->LastError.IsEmpty() ? FString(TEXT("The views could not be generated. Try again.")) : Run->LastError;
			else if (Run->Produced < Run->Targets.Num())
			{
				Self->Error = FString::Printf(TEXT("%d view(s) could not be generated. %s"), Run->Targets.Num() - Run->Produced, *Run->LastError).TrimEnd();
			}
			Self->OnChanged.ExecuteIfBound();
			*Next = nullptr;
			return;
		}
		const FString Slot = Run->Targets[Run->Index++];
		ZettaApi::FImageOptions Options;
		Options.bFor3D = true;
		ZettaApi::EditImage(FString(ViewEditPreamble) + ViewAngleFor(Slot), Options, Image, TOptional<int64>(), [Weak, Run, Next, Slot](const FZettaResponse& R, const FZettaImageResult& Out)
		{
			const TSharedPtr<SZettaMultiViewInput> Self = Weak.Pin();
			if (!Self)
			{
				*Next = nullptr;
				return;
			}
			if (!R.bOk)
			{
				Run->LastError = ZettaCatalog::Explain(R);
				// A limit or plan problem will not change for the next angle.
				if (R.Status == 402 || R.Status == 403 || R.Status == 429) Run->Index = Run->Targets.Num();
			}
			else if (Out.Images.Num() == 0 || Out.Images[0].Num() == 0)
			{
				Run->LastError = Out.Text.TrimStartAndEnd().IsEmpty() ? FString(TEXT("The image service returned no picture for this view.")) : Out.Text.TrimStartAndEnd();
			}
			else
			{
				const FString Type = Out.ContentTypes.Num() > 0 ? Out.ContentTypes[0] : FString(TEXT("image/png"));
				Self->SetView(Slot, Out.Images[0], Slot + ZettaStudioAssets::DotExtension(Type));
				Run->Produced++;
				Self->OnChanged.ExecuteIfBound();
			}
			(*Next)();
		});
	};
	(*Next)();
}

#undef LOCTEXT_NAMESPACE
