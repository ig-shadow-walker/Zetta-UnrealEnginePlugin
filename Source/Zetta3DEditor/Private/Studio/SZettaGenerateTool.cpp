// Copyright Zetta 3D. See LICENSE.md.

#include "SZettaGenerateTool.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "SZettaMultiViewInput.h"
#include "ZettaAiEdit.h"
#include "ZettaApi.h"
#include "ZettaAuth.h"
#include "ZettaCatalog.h"
#include "ZettaJobs.h"
#include "ZettaSettings.h"
#include "ZettaStudioAssets.h"
#include "ZettaStudioContext.h"
#include "ZettaStudioUI.h"
#include "ZettaWidgets.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace
{
	/** Stands in for a picture not uploaded yet, so the request (and its price) can be checked before anything goes up. */
	const TCHAR* GenPlaceholderUrl = TEXT("https://zetta3d.ai/placeholder.png");
	const TCHAR* GenImageKey = TEXT("image");
}

void SZettaGenerateTool::Construct(const FArguments& InArgs)
{
	bSmart = InArgs._Smart;
	O.Model = bSmart ? TEXT("np1") : TEXT("n1");
	O.Quality = TEXT("high-res");
	O.Output = TEXT("textured");
	O.bPbr = true;
	O.TextureQuality = TEXT("standard");
	O.PolygonType = TEXT("triangle");
	O.TextureAlignment = TEXT("geometry");
	O.bExportUv = true;
	O.bControlMode = true;
	SetCount(DefaultCount(), false);

	const FText Intro = bSmart
		? LOCTEXT("SmartIntro", "Clean, production-ready topology from a picture or a prompt.")
		: LOCTEXT("GenerateIntro", "Turn a prompt or a picture into a textured 3D model.");

	TSharedRef<SWidget> Form = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
		[
			ZettaStudioUI::Locked(MakeAttributeLambda([this]()
			{
				const FZettaEntitlements3D* G = FZettaStudioContext::Get().G3();
				return bSmart && G && FZettaStudioContext::No(G->AllowsZettaNp1)
					? LOCTEXT("SmartLocked", "Generate Smart 3D is available on a paid plan. Zetta N1 generates from text and images on every plan.")
					: FText();
			}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[BuildInput()]
		+ SVerticalBox::Slot().AutoHeight()[BuildSettings()];

	ChildSlot
	[
		Frame(Intro, Form,
			SAssignNew(Bar, SZettaSubmitBar).ButtonText(LOCTEXT("Generate3D", "Generate 3D")),
			SNew(SBox).Padding(FMargin(12, 0, 12, 6))[ZettaStudioUI::Hint(MakeAttributeLambda([this]() { return FText::FromString(ForkNote); }))])
	];

	TWeakPtr<SZettaGenerateTool> Weak = SharedThis(this);
	Bar->SetAction([Weak](SZettaSubmitBar::FFinish Finish)
	{
		if (const TSharedPtr<SZettaGenerateTool> Self = Weak.Pin()) Self->Submit(Finish);
		else Finish(FString());
	});
	OnContextChanged();
}

SZettaGenerateTool::~SZettaGenerateTool()
{
	if (QuoteTimer.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(QuoteTimer);
}

TSharedRef<SWidget> SZettaGenerateTool::BuildInput()
{
	TSharedRef<SWidget> Single = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 3)[ZettaStudioUI::FieldLabel(LOCTEXT("Describe", "Describe the model"))]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).MinDesiredHeight(56)
			[
				SNew(SMultiLineEditableTextBox)
				.AutoWrapText(true)
				.HintText(LOCTEXT("PromptHint", "A weathered medieval helmet, battle-worn steel"))
				.Text_Lambda([this]() { return FText::FromString(PromptText); })
				.IsEnabled_Lambda([this]() { return !HasOwnImage() && !bBusy; })
				.OnTextChanged_Lambda([this](const FText& Text)
				{
					if (Text.ToString() == PromptText) return;
					PromptText = Text.ToString();
					Refresh();
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)[ZettaStudioUI::Hint(MakeAttributeLambda([this]() { return FText::FromString(PromptHintText); }))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 3)[ZettaStudioUI::FieldLabel(LOCTEXT("OrPicture", "…or use a picture"))]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(Picker, SZettaImageInput)
			.Size(FVector2D(176, 176))
			.Label(LOCTEXT("PickerLabel", "Drop a picture here\nor click to choose"))
			.OnChanged(FSimpleDelegate::CreateSP(this, &SZettaGenerateTool::OnPicked))
			.IsEnabled_Lambda([this]() { return !bBusy; })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[
			ZettaStudioUI::ShownWhen([this]() { return HasPicture() && !bBusy; },
				SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.f, 4.f))
				+ SWrapBox::Slot()[ZettaStudioUI::SmallButton(LOCTEXT("RemoveImage", "Remove image"), [this]() { ClearImage(); })]
				+ SWrapBox::Slot()
				[
					ZettaStudioUI::ShownWhen([this]() { return DrawnFor.IsSet(); },
						ZettaStudioUI::SmallButton(LOCTEXT("Redraw", "Redraw"), [this]() { Redraw(); }, true, LOCTEXT("RedrawTip", "Generate a different image from this prompt")))
				]
				+ SWrapBox::Slot()
				[
					ZettaStudioUI::SmallButton(LOCTEXT("AiEdit", "AI edit"), [this]() { OpenAiEdit(); }, true, MakeAttributeLambda([]()
					{
						const FZettaStudioContext& Ctx = FZettaStudioContext::Get();
						return Ctx.Gates2D.IsSet() && FZettaStudioContext::No(Ctx.Gates2D->AllowsImageEditing)
							? LOCTEXT("AiEditLockedTip", "Change this picture with a sentence on a paid plan.")
							: LOCTEXT("AiEditTip", "Change this picture with a sentence");
					}))
				])
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[ZettaStudioUI::Hint(MakeAttributeLambda([this]() { return FText::FromString(ImageStatus); }))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[ZettaStudioUI::Error(MakeAttributeLambda([this]() { return FText::FromString(InputError); }))];

	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			ZettaStudioUI::Segmented([this]() { return Source; }, [this](const FString& V) { SetSource(V); },
				{ { TEXT("single"), LOCTEXT("TextImage", "Text / Image"), FText() }, { TEXT("multi"), LOCTEXT("MultiView", "Multi-View"), LOCTEXT("MultiViewTip", "Front, back, left and right views of one object") } },
				[](const FString& Key)
				{
					const FZettaEntitlements3D* G = FZettaStudioContext::Get().G3();
					const bool bLocked = Key == TEXT("multi") && G && G->MaxMultiViewImages.IsSet() && G->MaxMultiViewImages.GetValue() <= 0;
					return bLocked ? FString(TEXT("Multi-view input is available on a paid plan.")) : FString();
				})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
		[
			ZettaStudioUI::Locked(MakeAttributeLambda([]()
			{
				const FZettaEntitlements3D* G = FZettaStudioContext::Get().G3();
				return G && G->MaxMultiViewImages.IsSet() && G->MaxMultiViewImages.GetValue() <= 0
					? LOCTEXT("MultiViewLocked", "Multi-view input is available on a paid plan.")
					: FText();
			}))
		]
		+ SVerticalBox::Slot().AutoHeight()[ZettaStudioUI::ShownWhen([this]() { return !IsMultiView(); }, Single)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
		[
			ZettaStudioUI::ShownWhen([this]() { return IsMultiView(); },
				SAssignNew(Multi, SZettaMultiViewInput).OnChanged(FSimpleDelegate::CreateSP(this, &SZettaGenerateTool::OnViewsChanged)))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[
			ZettaStudioUI::Hint(MakeAttributeLambda([this]()
			{
				if (LoadingPictures.Num() == 0) return FText();
				return IsMultiView() ? LOCTEXT("LoadingAngles", "Loading the reference angles…") : LOCTEXT("LoadingPicture", "Loading the reference picture…");
			}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[ZettaStudioUI::Error(MakeAttributeLambda([this]() { return FText::FromString(PictureProblem); }))];

	return ZettaStudioUI::Group(LOCTEXT("Input", "Input"), Body);
}

// ─── Context ──────────────────────────────────────────────────────────────────

void SZettaGenerateTool::OnContextChanged()
{
	ApplyContextDefaults();
	const FZettaEntitlements3D* G = FZettaStudioContext::Get().G3();
	if (Multi.IsValid()) Multi->SetBudget(G ? G->MaxMultiViewImages : TOptional<int64>());
	if (IsMultiView() && G && G->MaxMultiViewImages.IsSet() && G->MaxMultiViewImages.GetValue() <= 0) SetSource(TEXT("single"));
	Refresh();
}

void SZettaGenerateTool::Changed()
{
	Refresh();
}

// ─── Input ────────────────────────────────────────────────────────────────────

FString SZettaGenerateTool::Prompt() const { return PromptText.TrimStartAndEnd(); }
bool SZettaGenerateTool::HasPicture() const { return Picker.IsValid() && Picker->HasImage(); }
bool SZettaGenerateTool::HasOwnImage() const { return HasPicture() && !DrawnFor.IsSet(); }
bool SZettaGenerateTool::DrawnIsCurrent() const { return HasPicture() && DrawnFor.IsSet() && DrawnFor.GetValue() == Prompt(); }

bool SZettaGenerateTool::SendsBarePrompt() const
{
	// A prompt goes to the model as text, with no picture drawn: Zetta N1 without Control Mode.
	return ZettaCatalog::Model(O.Model).bBarePrompt && !O.bControlMode && !DrawnIsCurrent();
}

bool SZettaGenerateTool::NextIsDrawing() const
{
	return !IsMultiView() && !HasOwnImage() && !DrawnIsCurrent() && !Prompt().IsEmpty() && !SendsBarePrompt();
}

void SZettaGenerateTool::SetSource(const FString& Value)
{
	Source = Value == TEXT("multi") ? TEXT("multi") : TEXT("single");
	SetMultiViewSettings(IsMultiView());
	SetFile(IsMultiView() ? (Multi.IsValid() ? Multi->ReferenceName() : FString()) : (HasOwnImage() ? Picker->GetFileName() : FString()));
	Refresh();
}

void SZettaGenerateTool::SetImage(const TArray<uint8>& Bytes, const FString& Name)
{
	SetSource(TEXT("single"));
	LoadingPictures.Remove(GenImageKey);
	Picker->SetImage(Bytes, Name);
}

void SZettaGenerateTool::OnPicked()
{
	// The user's own choice (a picture attached or removed) wins over a fork's picture still loading.
	if (!bPlacingForkPicture) LoadingPictures.Remove(GenImageKey);
	if (bSettingImage) return;
	// The user's own picture: check it, and it replaces any drawn one.
	DrawnFor.Reset();
	InputError.Reset();
	if (HasPicture())
	{
		const FString Problem = ZettaStudioAssets::CheckImage(Picker->GetBytes(), Picker->GetContentType());
		if (!Problem.IsEmpty())
		{
			InputError = Problem;
			SetPicture(TArray<uint8>(), FString(), TOptional<FString>());
		}
	}
	SetFile(HasPicture() ? Picker->GetFileName() : FString());
	Refresh();
}

void SZettaGenerateTool::OnViewsChanged()
{
	// A view the user put in themselves wins over a fork's picture still loading for that slot.
	if (!bPlacingForkPicture && Multi.IsValid())
	{
		for (auto It = LoadingPictures.CreateIterator(); It; ++It)
		{
			if (*It != GenImageKey && Multi->HasView(*It)) It.RemoveCurrent();
		}
	}
	if (IsMultiView() && Multi.IsValid()) SetFile(Multi->ReferenceName());
	Refresh();
}

void SZettaGenerateTool::SetPicture(const TArray<uint8>& Bytes, const FString& Name, const TOptional<FString>& InDrawnFor)
{
	{
		TGuardValue<bool> Guard(bSettingImage, true);
		if (Bytes.Num() > 0) Picker->SetImage(Bytes, Name);
		else if (Picker->HasImage()) Picker->Clear();
	}
	DrawnFor = Bytes.Num() > 0 ? InDrawnFor : TOptional<FString>();
	SetFile(Bytes.Num() > 0 && !InDrawnFor.IsSet() ? Name : FString());
	Refresh();
}

void SZettaGenerateTool::ClearImage()
{
	LoadingPictures.Remove(GenImageKey);
	SetPicture(TArray<uint8>(), FString(), TOptional<FString>());
	ImageStatus.Reset();
	InputError.Reset();
}

void SZettaGenerateTool::OpenAiEdit()
{
	if (!HasPicture() || bBusy) return;
	// The result of an edit on a drawn picture still counts as drawn from that prompt.
	const TOptional<FString> Drawn = DrawnFor;
	const FString Name = Picker->GetFileName();
	TWeakPtr<SZettaGenerateTool> Weak = SharedThis(this);
	ZettaAiEdit::Open(LOCTEXT("AiEditTitle", "AI edit"), Picker->GetBytes(), Picker->GetContentType(), [Weak, Drawn, Name](const TArray<uint8>& Bytes, const FString& Type)
	{
		const TSharedPtr<SZettaGenerateTool> Self = Weak.Pin();
		if (!Self) return;
		Self->InputError.Reset();
		if (Drawn.IsSet()) Self->SetPicture(Bytes, TEXT("prompt") + ZettaStudioAssets::DotExtension(Type), Drawn);
		else Self->SetPicture(Bytes, ZettaStudioAssets::Renamed(Name, Type), TOptional<FString>());
	});
}

// ─── Fork ─────────────────────────────────────────────────────────────────────

void SZettaGenerateTool::ApplyFork(const ZettaStudioFork::FSetup& Fork)
{
	if (bBusy || Bar->IsBusy())
	{
		FNotificationInfo Info(LOCTEXT("ForkWait", "Wait for this job to start, then fork again."));
		Info.ExpireDuration = 4.f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}
	const bool bMulti = Fork.WayIn == ZettaStudioFork::EWayIn::MultiView;
	const bool bText = Fork.WayIn == ZettaStudioFork::EWayIn::Text;

	// What was here goes: the set-up is the input now.
	Bar->ClearMessages();
	InputError.Reset();
	PictureProblem.Reset();
	++PictureBatch;
	LoadingPictures.Reset();
	ResetInputs();
	SetPicture(TArray<uint8>(), FString(), TOptional<FString>());
	Multi->ClearViews();
	SetSource(bMulti ? TEXT("multi") : TEXT("single"));
	ApplySetUp(Fork.Settings);
	// A picture job carries no prompt.
	PromptText = bText ? Fork.Settings.Prompt : FString();

	TArray<ZettaStudioFork::FPicture> Wanted;
	if (!bText)
	{
		for (const ZettaStudioFork::FPicture& P : Fork.Pictures)
		{
			if (bMulti && P.View.IsEmpty()) continue;
			Wanted.Add(P);
			if (!bMulti) break;
		}
	}
	for (const ZettaStudioFork::FPicture& P : Wanted) LoadingPictures.Add(bMulti ? P.View : FString(GenImageKey));
	for (const ZettaStudioFork::FPicture& P : Wanted) LoadForkPicture(PictureBatch, P, bMulti);

	ForkNote = FString::Printf(TEXT("Forked from “%s”. Nothing has run yet — change anything you like, then press Generate 3D."), *Fork.FromTitle);
	Refresh();
}

void SZettaGenerateTool::LoadForkPicture(int32 Batch, const ZettaStudioFork::FPicture& Picture, bool bMulti)
{
	const FString Key = bMulti ? Picture.View : FString(GenImageKey);
	TWeakPtr<SZettaGenerateTool> Weak = SharedThis(this);
	ZettaStudioAssets::DownloadPicture(Picture.Url, [Weak, Batch, Picture, bMulti, Key](const FString& Error, const TArray<uint8>& Bytes, const FString&)
	{
		const TSharedPtr<SZettaGenerateTool> Self = Weak.Pin();
		// A newer fork, or the user's own picture in that slot: a late arrival is never laid over it.
		if (!Self || Batch != Self->PictureBatch || !Self->LoadingPictures.Contains(Key)) return;
		if (!Error.IsEmpty())
		{
			Self->PictureProblem = bMulti
				? TEXT("Some of the reference angles could not be loaded — add them again.")
				: TEXT("The reference picture could not be loaded — attach it again.");
		}
		else
		{
			TGuardValue<bool> Guard(Self->bPlacingForkPicture, true);
			if (bMulti) Self->Multi->SetView(Picture.View, Bytes, Picture.Name);
			else Self->Picker->SetImage(Bytes, Picture.Name);
		}
		Self->LoadingPictures.Remove(Key);
		Self->Refresh();
	});
}

// ─── Price and problems ───────────────────────────────────────────────────────

FZettaGenerateOptions SZettaGenerateTool::Preview() const
{
	FZettaGenerateOptions P = Snapshot();
	if (IsMultiView())
	{
		for (const FString& Slot : Multi->FilledSlots()) P.Views.Emplace(Slot, GenPlaceholderUrl);
		return P;
	}
	if (HasOwnImage()) P.ImageUrl = GenPlaceholderUrl;
	else if (!Prompt().IsEmpty())
	{
		P.Prompt = Prompt();
		if (!SendsBarePrompt())
		{
			P.ImageUrl = GenPlaceholderUrl;
			P.bImageFromPrompt = true;
		}
	}
	return P;
}

void SZettaGenerateTool::Refresh()
{
	if (!Bar.IsValid() || !Picker.IsValid() || !Multi.IsValid()) return;
	const FZettaGenerateOptions P = Preview();
	const FZettaStudioContext& Ctx = FZettaStudioContext::Get();
	const FZettaBuiltRequest Built = ZettaRequests::Build(P, Ctx.G3());

	PromptHintText = HasOwnImage() ? TEXT("The picture is used; the prompt is ignored. Remove the picture to generate from text.")
		: HasPicture() && DrawnFor.IsSet() && !DrawnIsCurrent() ? TEXT("The prompt changed — the picture will be drawn again.")
		: NextIsDrawing() && O.bControlMode ? TEXT("Control Mode: a picture is drawn from your prompt first, so you can check it before any 3D credits are spent.")
		: NextIsDrawing() ? TEXT("A picture is drawn from your prompt, then built into 3D.")
		: FString();

	FString Problem;
	if (LoadingPictures.Num() > 0) Problem = TEXT("Loading the reference picture…");
	else if (bBusy && !Bar->IsBusy()) Problem = TEXT("Drawing the picture…");
	else if (IsMultiView() && Multi->Filled() == 0) Problem = TEXT("Add at least one view to generate from.");
	else if (!IsMultiView() && Prompt().IsEmpty() && !HasPicture()) Problem = TEXT("Describe what to make, or attach an image.");
	else if (IsMultiView() && Multi->IsBusy()) Problem = TEXT("Generating views…");
	// The typed count, said in full here; the field itself says "Out of range — …".
	if (Problem.IsEmpty()) Problem = CountProblem();
	if (Problem.IsEmpty())
	{
		for (const FString& Each : Built.Problems)
		{
			if (!Each.Contains(TEXT("draw one from the prompt")))
			{
				Problem = Each;
				break;
			}
		}
	}
	if (Problem.IsEmpty())
	{
		const TArray<FString> Plan = PlanProblems(Built, IsMultiView() ? Multi->Filled() : 0);
		if (Plan.Num() > 0) Problem = FString::Join(Plan, TEXT(" ")) + TEXT(" Upgrade at zetta3d.ai/pricing.");
	}
	if (Problem.IsEmpty() && !Ctx.Prices.IsSet() && Ctx.bLoading) Problem = TEXT("Loading prices…");

	RequestQuote(Built, P.Model);
	TOptional<int32> Price = Quoted;
	if (!Price.IsSet() && Ctx.Prices.IsSet()) Price = ZettaRequests::Price(Ctx.Prices.GetValue(), Built, P.Model);
	Bar->SetPrice(Price);
	Bar->SetProblem(Problem);
}

void SZettaGenerateTool::RequestQuote(const FZettaBuiltRequest& Built, const FString& Model)
{
	if (!FZettaAuth::Get().IsSignedIn())
	{
		// Asked again once signed in (the account's prices arrive with a refresh).
		QuoteKey.Reset();
		Quoted.Reset();
		return;
	}
	const FString Key = Built.Base + TEXT("|") + FString::Join(Built.Modifiers, TEXT(",")) + TEXT("|") + Model;
	if (Key == QuoteKey) return;
	QuoteKey = Key;
	Quoted.Reset();
	if (QuoteTimer.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(QuoteTimer);
	QuoteTimer.Reset();
	if (Built.Base.IsEmpty()) return;

	// The server's quote is the price that will be charged; ask once the settings stop changing.
	TWeakPtr<SZettaGenerateTool> Weak = SharedThis(this);
	const FString Base = Built.Base;
	const TArray<FString> Modifiers = Built.Modifiers;
	QuoteTimer = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak, Key, Base, Modifiers, Model](float)
	{
		const TSharedPtr<SZettaGenerateTool> Self = Weak.Pin();
		if (!Self) return false;
		Self->QuoteTimer.Reset();
		ZettaApi::Quote(Base, Modifiers, Model, [Weak, Key](const FZettaResponse& R, const FZettaQuote& Quote)
		{
			const TSharedPtr<SZettaGenerateTool> Self = Weak.Pin();
			if (!Self || !R.bOk || Self->QuoteKey != Key) return;
			Self->Quoted = Quote.Credits;
			Self->Refresh();
		});
		return false;
	}), 0.5f);
}

// ─── Submit ───────────────────────────────────────────────────────────────────

void SZettaGenerateTool::Stage(const FString& Text)
{
	Bar->SetStage(Text);
}

void SZettaGenerateTool::Submit(SZettaSubmitBar::FFinish Finish)
{
	bBusy = true;
	InputError.Reset();
	Refresh();
	TWeakPtr<SZettaGenerateTool> Weak = SharedThis(this);
	SZettaSubmitBar::FFinish Done = [Weak, Finish](const FString& Error)
	{
		const TSharedPtr<SZettaGenerateTool> Self = Weak.Pin();
		if (Self)
		{
			Self->bBusy = false;
			Self->ImageStatus.Reset();
		}
		Finish(Error);
		if (Self) Self->Refresh();
	};
	if (IsMultiView()) SubmitMultiView(Done);
	else SubmitSingle(Done);
}

void SZettaGenerateTool::Draw(TFunction<void(const FString&)> Done)
{
	const FString Text = Prompt();
	Stage(TEXT("Generating image…"));
	ImageStatus = TEXT("Generating the image…");
	ZettaApi::FImageOptions Options;
	Options.bFor3D = true;
	TWeakPtr<SZettaGenerateTool> Weak = SharedThis(this);
	ZettaApi::GenerateImage(Text, Options, {}, [Weak, Done, Text](const FZettaResponse& R, const FZettaImageResult& Result)
	{
		const TSharedPtr<SZettaGenerateTool> Self = Weak.Pin();
		if (!Self) return;
		if (!R.bOk)
		{
			Done(ZettaCatalog::Explain(R));
			return;
		}
		if (Result.Images.Num() == 0 || Result.Images[0].Num() == 0)
		{
			Done(TEXT("The image service returned no image for that prompt."));
			return;
		}
		const FString Type = Result.ContentTypes.Num() > 0 ? Result.ContentTypes[0] : FString(TEXT("image/png"));
		Self->SetPicture(Result.Images[0], TEXT("prompt") + ZettaStudioAssets::DotExtension(Type), Text);
		Self->ImageStatus.Reset();
		Done(FString());
	});
}

void SZettaGenerateTool::Redraw()
{
	if (bBusy || Prompt().IsEmpty()) return;
	bBusy = true;
	InputError.Reset();
	Refresh();
	TWeakPtr<SZettaGenerateTool> Weak = SharedThis(this);
	Draw([Weak](const FString& Error)
	{
		const TSharedPtr<SZettaGenerateTool> Self = Weak.Pin();
		if (!Self) return;
		Self->bBusy = false;
		Self->ImageStatus.Reset();
		Self->Bar->SetStage(FString());
		if (!Error.IsEmpty()) Self->InputError = Error;
		Self->Refresh();
	});
}

void SZettaGenerateTool::SubmitSingle(SZettaSubmitBar::FFinish Done)
{
	const FZettaGenerateOptions Settings = Snapshot();
	const FString Text = Prompt();
	TWeakPtr<SZettaGenerateTool> Weak = SharedThis(this);

	auto Continue = [Weak, Done, Settings, Text]()
	{
		const TSharedPtr<SZettaGenerateTool> Self = Weak.Pin();
		if (!Self)
		{
			Done(FString());
			return;
		}
		FZettaGenerateOptions Options = Settings;
		auto Cleared = [Weak]()
		{
			if (const TSharedPtr<SZettaGenerateTool> S = Weak.Pin())
			{
				S->PromptText.Reset();
				S->SetPicture(TArray<uint8>(), FString(), TOptional<FString>());
			}
		};
		if (!Self->HasPicture())
		{
			Options.Prompt = Text;
			Self->Start(Options, Done, Cleared);
			return;
		}
		const TOptional<FString> From = Self->DrawnFor;
		const FString First = Self->bRemoveBackground ? TEXT("Removing background…") : TEXT("Uploading…");
		Self->Stage(First);
		Self->ImageStatus = Self->bRemoveBackground ? TEXT("Removing the background…") : TEXT("Uploading…");
		ZettaStudioAssets::UploadImage(Self->Picker->GetBytes(), Self->Picker->GetContentType(), Self->bRemoveBackground,
			[Weak, Done, Options, From, Cleared](const FString& Error, const FString& Url)
			{
				const TSharedPtr<SZettaGenerateTool> S = Weak.Pin();
				if (!S || !Error.IsEmpty())
				{
					Done(Error);
					return;
				}
				FZettaGenerateOptions Sent = Options;
				Sent.ImageUrl = Url;
				if (From.IsSet())
				{
					Sent.Prompt = From.GetValue();
					Sent.bImageFromPrompt = true;
				}
				S->Start(Sent, Done, Cleared);
			},
			[Weak](float Progress)
			{
				if (const TSharedPtr<SZettaGenerateTool> S = Weak.Pin())
				{
					const FString Line = ZettaStudioAssets::UploadingText(Progress);
					S->Stage(Line);
					S->ImageStatus = Line;
				}
			});
	};

	if (!HasOwnImage() && !DrawnIsCurrent() && !Text.IsEmpty() && !SendsBarePrompt())
	{
		const bool bControl = Settings.bControlMode;
		Draw([Weak, Done, Continue, bControl](const FString& Error)
		{
			const TSharedPtr<SZettaGenerateTool> Self = Weak.Pin();
			if (!Self || !Error.IsEmpty())
			{
				Done(Error);
				return;
			}
			if (bControl)
			{
				// Control Mode stops here: the picture is shown before any 3D credits are spent.
				Self->Bar->ShowDone(TEXT("Here is the picture. Press Generate 3D to build it, or Redraw for a different one."));
				Done(FString());
				return;
			}
			Continue();
		});
		return;
	}
	Continue();
}

void SZettaGenerateTool::SubmitMultiView(SZettaSubmitBar::FFinish Done)
{
	const FZettaGenerateOptions Settings = Snapshot();
	const TArray<SZettaMultiViewInput::FView> Views = Multi->Views();
	if (Views.Num() == 0)
	{
		Done(TEXT("Add at least one view to generate from."));
		return;
	}
	for (const SZettaMultiViewInput::FView& View : Views)
	{
		const FString Problem = ZettaStudioAssets::CheckImage(View.Bytes, View.ContentType);
		if (!Problem.IsEmpty())
		{
			Done(FString::Printf(TEXT("%s view: %s"), *(View.Slot.Left(1).ToUpper() + View.Slot.Mid(1)), *Problem));
			return;
		}
	}

	Stage(bRemoveBackground ? FString::Printf(TEXT("Removing backgrounds (0/%d)…"), Views.Num()) : ZettaStudioAssets::UploadingText(0.f, TEXT("Uploading views")));

	struct FUploads
	{
		TArray<FString> Slots;
		TArray<FString> Urls;
		TArray<float> Shares;
		int32 Left = 0;
		bool bFailed = false;
	};
	TSharedRef<FUploads> U = MakeShared<FUploads>();
	for (const SZettaMultiViewInput::FView& View : Views) U->Slots.Add(View.Slot);
	U->Urls.SetNum(Views.Num());
	U->Shares.Init(0.f, Views.Num());
	U->Left = Views.Num();

	TWeakPtr<SZettaGenerateTool> Weak = SharedThis(this);
	for (int32 i = 0; i < Views.Num(); ++i)
	{
		ZettaStudioAssets::UploadImage(Views[i].Bytes, Views[i].ContentType, bRemoveBackground, [Weak, Done, Settings, U, i](const FString& Error, const FString& Url)
		{
			if (U->bFailed) return;
			const TSharedPtr<SZettaGenerateTool> Self = Weak.Pin();
			if (!Self || !Error.IsEmpty())
			{
				U->bFailed = true;
				Done(Error.IsEmpty() ? FString() : FString::Printf(TEXT("%s view: %s"), *(U->Slots[i].Left(1).ToUpper() + U->Slots[i].Mid(1)), *Error));
				return;
			}
			U->Urls[i] = Url;
			if (--U->Left > 0) return;
			FZettaGenerateOptions Options = Settings;
			for (int32 j = 0; j < U->Slots.Num(); ++j) Options.Views.Emplace(U->Slots[j], U->Urls[j]);
			Self->Start(Options, Done, [Weak]()
			{
				if (const TSharedPtr<SZettaGenerateTool> S = Weak.Pin()) S->Multi->ClearViews();
			});
		}, [Weak, U, i](float Progress)
		{
			// One percentage for the batch: each view's share of the bytes going up.
			U->Shares[i] = Progress;
			float Sum = 0.f;
			for (const float S : U->Shares) Sum += S;
			if (const TSharedPtr<SZettaGenerateTool> Self = Weak.Pin()) Self->Stage(ZettaStudioAssets::UploadingText(Sum / U->Shares.Num(), TEXT("Uploading views")));
		});
	}
}

void SZettaGenerateTool::Start(FZettaGenerateOptions Options, SZettaSubmitBar::FFinish Done, TFunction<void()> OnStarted)
{
	const FZettaBuiltRequest Built = ZettaRequests::Build(Options, FZettaStudioContext::Get().G3());
	if (!Built.IsOk())
	{
		Done(Built.Problems[0]);
		return;
	}
	Stage(TEXT("Starting…"));
	TWeakPtr<SZettaGenerateTool> Weak = SharedThis(this);
	ZettaApi::CreateGeneration(Built.Body.ToSharedRef(), [Weak, Done, OnStarted](const FZettaResponse& R, const FZettaGenerationPtr& G)
	{
		if (!R.bOk || !G.IsValid())
		{
			Done(R.bOk ? FString(TEXT("The job did not start. Try again.")) : ZettaCatalog::Explain(R));
			return;
		}
		const TSharedPtr<SZettaGenerateTool> Self = Weak.Pin();
		if (!Self)
		{
			// The Studio closed while the job was being created: it still runs, so it is still followed.
			FZettaImportOptions Import;
			Import.bPlace = UZettaProjectSettings::Get()->bPlaceInLevel;
			FZettaJobs::Get().Track(G, true, Import, TEXT("studio"));
			Done(FString());
			return;
		}
		Self->Bar->Track(G);
		Self->ResetInputs();
		// It ran: the fork note has said what it had to.
		Self->ForkNote.Reset();
		if (OnStarted) OnStarted();
		Done(FString());
	});
}

#undef LOCTEXT_NAMESPACE
