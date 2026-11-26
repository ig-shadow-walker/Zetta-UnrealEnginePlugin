// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaAiEdit.h"

#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "ZettaApi.h"
#include "ZettaCatalog.h"
#include "ZettaImageCache.h"
#include "ZettaStudioContext.h"
#include "ZettaStudioUI.h"
#include "ZettaStyle.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace
{
	class SZettaAiEditBody : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SZettaAiEditBody) {}
			SLATE_ARGUMENT(FText, Title)
			SLATE_ARGUMENT(FText, AcceptLabel)
		SLATE_END_ARGS()

		TArray<uint8> Original;
		FString OriginalType;
		TArray<uint8> Result;
		FString ResultType;
		ZettaAiEdit::FOnAccept OnAccept;
		TWeakPtr<SWindow> Window;

		void Construct(const FArguments& InArgs, const TArray<uint8>& Bytes, const FString& Type)
		{
			Original = Bytes;
			OriginalType = Type.IsEmpty() ? TEXT("image/png") : Type;
			Shown = FZettaImageCache::Get().FromBytes(FString::Printf(TEXT("aiedit:%u:%d"), FCrc::MemCrc32(Bytes.GetData(), Bytes.Num()), Bytes.Num()), Bytes);
			const bool bLocked = Locked();

			TSharedRef<SVerticalBox> Body = SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)[ZettaUI::Title(InArgs._Title)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
				[
					ZettaUI::Hint(bLocked ? LOCTEXT("AiEditLookOnly", "A closer look at the picture.") : LOCTEXT("AiEditIntro", "Describe a change to the picture. Each edit builds on the last."))
				]
				+ SVerticalBox::Slot().FillHeight(1.f).Padding(0, 0, 0, 10)
				[
					SNew(SBorder).BorderImage(FZettaStyle::Get().GetBrush("Zetta.Thumb")).Padding(6)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()
						[
							SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
							[
								SNew(SImage).Image_Lambda([this]() { return Shown.IsValid() ? Shown.Get() : FAppStyle::GetNoBrush(); })
							]
						]
						+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(6)
						[
							SNew(SBox).Visibility_Lambda([this]() { return ZettaStudioUI::Shown(Result.Num() > 0 && !bBusy); })
							[
								ZettaStudioUI::Chip(LOCTEXT("Edited", "Edited"))
							]
						]
						+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
						[
							SNew(SVerticalBox).Visibility_Lambda([this]() { return ZettaStudioUI::Shown(bBusy); })
							+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(SThrobber)]
							+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Text(LOCTEXT("Editing", "Editing…"))]
						]
					]
				];

			if (bLocked)
			{
				Body->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[ZettaStudioUI::Locked(LOCTEXT("AiEditLocked", "Change this picture with a sentence on a paid plan."))];
			}
			else
			{
				Body->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(SBox).MinDesiredHeight(48)
						[
							SAssignNew(Instruction, SMultiLineEditableTextBox)
							.AutoWrapText(true)
							.IsEnabled_Lambda([this]() { return !bBusy; })
							.HintText_Lambda([this]() { return Result.Num() > 0 ? LOCTEXT("AnotherChange", "Another change?") : LOCTEXT("AiEditHint", "Make the stripes blue, add a flag on top…"); })
							.OnKeyDownHandler_Lambda([this](const FGeometry&, const FKeyEvent& Key)
							{
								// Enter sends, Shift+Enter is a new line.
								if (Key.GetKey() == EKeys::Enter && !Key.IsShiftDown())
								{
									Submit();
									return FReply::Handled();
								}
								return FReply::Unhandled();
							})
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(6, 0, 0, 0)
					[
						ZettaUI::Button(MakeAttributeLambda([this]() { return bBusy ? LOCTEXT("Submitting", "Submitting…") : LOCTEXT("Submit", "Submit"); }),
							FSimpleDelegate::CreateLambda([this]() { Submit(); }),
							MakeAttributeLambda([this]() { return !bBusy && !Text().IsEmpty(); }))
					]
				];
			}

			Body->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[ZettaStudioUI::Error(MakeAttributeLambda([this]() { return FText::FromString(Error); }))];

			TSharedRef<SHorizontalBox> Foot = SNew(SHorizontalBox);
			if (!bLocked)
			{
				Foot->AddSlot().FillWidth(1.f).Padding(0, 0, 8, 0)
				[
					ZettaUI::PrimaryButton(InArgs._AcceptLabel.IsEmpty() ? LOCTEXT("UseToGenerate", "Use this to generate 3D") : InArgs._AcceptLabel,
						FSimpleDelegate::CreateLambda([this]() { Accept(); }),
						MakeAttributeLambda([this]() { return Result.Num() > 0 && !bBusy; }))
				];
			}
			Foot->AddSlot().AutoWidth()
			[
				ZettaUI::Button(bLocked ? LOCTEXT("Close", "Close") : LOCTEXT("Cancel", "Cancel"), FSimpleDelegate::CreateLambda([this]() { Close(); }),
					true, LOCTEXT("KeepOriginal", "Keep the original picture"))
			];
			Body->AddSlot().AutoHeight()[Foot];

			ChildSlot[SNew(SBorder).BorderImage(FAppStyle::GetBrush("Brushes.Panel")).Padding(14)[Body]];
		}

	private:
		static bool Locked() { return FZettaStudioContext::Get().Gates2D.IsSet() && FZettaStudioContext::No(FZettaStudioContext::Get().Gates2D->AllowsImageEditing); }

		FString Text() const { return Instruction.IsValid() ? Instruction->GetText().ToString().TrimStartAndEnd() : FString(); }

		void Submit()
		{
			const FString Said = Text();
			if (bBusy || Said.IsEmpty() || Locked()) return;
			bBusy = true;
			Error.Reset();
			// Each edit starts from the last result, the way a picture is refined.
			const bool bFromResult = Result.Num() > 0;
			const FZettaInlineImage Image = FZettaInlineImage::From(bFromResult ? Result : Original, bFromResult ? ResultType : OriginalType);
			ZettaApi::FImageOptions Options;
			Options.bFor3D = true;
			TWeakPtr<SZettaAiEditBody> Weak = StaticCastSharedRef<SZettaAiEditBody>(AsShared());
			ZettaApi::EditImage(Said, Options, Image, TOptional<int64>(), [Weak](const FZettaResponse& R, const FZettaImageResult& Out)
			{
				const TSharedPtr<SZettaAiEditBody> Self = Weak.Pin();
				if (!Self) return;
				Self->bBusy = false;
				if (!R.bOk)
				{
					Self->Error = ZettaCatalog::Explain(R);
					return;
				}
				const int32 Index = Out.Images.IndexOfByPredicate([](const TArray<uint8>& B) { return B.Num() > 0; });
				if (Index == INDEX_NONE)
				{
					Self->Error = Out.Text.TrimStartAndEnd().IsEmpty() ? FString(TEXT("The image service returned no image for that edit.")) : Out.Text.TrimStartAndEnd();
					return;
				}
				Self->Result = Out.Images[Index];
				Self->ResultType = Out.ContentTypes.IsValidIndex(Index) ? Out.ContentTypes[Index] : FString(TEXT("image/png"));
				Self->Shown = FZettaImageCache::Get().FromBytes(FString::Printf(TEXT("aiedit:%u:%d"), FCrc::MemCrc32(Self->Result.GetData(), Self->Result.Num()), Self->Result.Num()), Self->Result);
				if (Self->Instruction.IsValid()) Self->Instruction->SetText(FText::GetEmpty());
				FZettaStudioContext::RefreshCredits();
			});
		}

		void Accept()
		{
			if (Result.Num() == 0 || bBusy) return;
			const ZettaAiEdit::FOnAccept Callback = OnAccept;
			const TArray<uint8> Bytes = Result;
			const FString Type = ResultType;
			OnAccept = nullptr;
			Close();
			if (Callback) Callback(Bytes, Type);
		}

		void Close()
		{
			if (const TSharedPtr<SWindow> W = Window.Pin()) W->RequestDestroyWindow();
		}

		TSharedPtr<FSlateBrush> Shown;
		TSharedPtr<SMultiLineEditableTextBox> Instruction;
		FString Error;
		bool bBusy = false;
	};
}

namespace ZettaAiEdit
{
	void Open(const FText& Title, const TArray<uint8>& Bytes, const FString& ContentType, FOnAccept OnAccept, const FText& AcceptLabel)
	{
		if (Bytes.Num() == 0) return;
		const FText Name = Title.IsEmpty() ? LOCTEXT("AiEditTitle", "AI edit") : Title;
		TSharedRef<SZettaAiEditBody> Body = SNew(SZettaAiEditBody, Bytes, ContentType).Title(Name).AcceptLabel(AcceptLabel);
		Body->OnAccept = MoveTemp(OnAccept);
		TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(Name)
			.ClientSize(FVector2D(540, 660))
			.SupportsMinimize(false)
			.SupportsMaximize(false)
			[
				Body
			];
		Body->Window = Window;
		const TSharedPtr<SWindow> Parent = FSlateApplication::Get().GetActiveTopLevelWindow();
		if (Parent.IsValid()) FSlateApplication::Get().AddWindowAsNativeChild(Window, Parent.ToSharedRef());
		else FSlateApplication::Get().AddWindow(Window);
	}
}

#undef LOCTEXT_NAMESPACE
