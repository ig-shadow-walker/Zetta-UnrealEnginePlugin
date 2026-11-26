// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaAgentAsk.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "ZettaAgentRuntime.h"
#include "ZettaAgentTools.h"
#include "ZettaJson.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace
{
	constexpr int32 AskMaxQuestions = 4;

	FString AskShort(const FString& Text, int32 Max)
	{
		const FString Clean = Text.TrimStartAndEnd();
		return Clean.Len() <= Max ? Clean : Clean.Left(Max - 1) + TEXT("…");
	}

	FString AskQuote(const FString& Text)
	{
		return TEXT("\"") + Text.Replace(TEXT("\""), TEXT("\\\"")) + TEXT("\"");
	}

	const UClass* AskFindClass(const FString& Name)
	{
		if (Name.IsEmpty()) return nullptr;
		if (Name.StartsWith(TEXT("/"))) return FindObject<UClass>(nullptr, *Name);
		return FindFirstObject<UClass>(*Name, EFindFirstObjectOptions::NativeFirst);
	}
}

namespace ZettaAgentAsk
{
	FString Parse(const TSharedPtr<FJsonObject>& Input, FZettaAskRequest& Out)
	{
		Out = FZettaAskRequest();
		Out.Title = AskShort(ZettaJson::Str(Input, TEXT("title")), 80);
		const TArray<TSharedPtr<FJsonValue>> List = ZettaJson::Arr(Input, TEXT("questions"));
		if (List.Num() == 0)
		{
			return TEXT("ask_user needs `questions`: one to four, each with `question` and `kind` (object, choice or text).");
		}
		if (List.Num() > AskMaxQuestions) return FString::Printf(TEXT("ask_user takes at most %d questions in one card."), AskMaxQuestions);

		TSet<FString> Ids;
		for (int32 n = 0; n < List.Num(); ++n)
		{
			const TSharedPtr<FJsonObject> Q = List[n].IsValid() && List[n]->Type == EJson::Object ? List[n]->AsObject() : nullptr;
			if (!Q.IsValid()) return FString::Printf(TEXT("Question %d must be an object."), n + 1);
			FZettaAskQuestion Question;
			Question.Id = ZettaJson::Str(Q, TEXT("id")).TrimStartAndEnd();
			Question.Question = AskShort(ZettaJson::Str(Q, TEXT("question")), 300);
			Question.Kind = ZettaJson::Str(Q, TEXT("kind")).ToLower();
			Question.Accept = ZettaJson::Str(Q, TEXT("accept"), TEXT("any")).ToLower();
			if (Question.Accept == TEXT("scene")) Question.Accept = TEXT("level");
			Question.AssetType = ZettaJson::Str(Q, TEXT("assetType")).TrimStartAndEnd();
			Question.bMultiple = ZettaJson::Bool(Q, TEXT("multiple"));
			Question.bAllowOther = ZettaJson::Bool(Q, TEXT("allowOther"), true);
			Question.Placeholder = AskShort(ZettaJson::Str(Q, TEXT("placeholder")), 120);
			Question.bOptional = ZettaJson::Bool(Q, TEXT("optional"));

			if (Question.Question.IsEmpty()) return FString::Printf(TEXT("Question %d has no `question` text."), n + 1);
			if (Question.Kind != TEXT("object") && Question.Kind != TEXT("choice") && Question.Kind != TEXT("text"))
			{
				return FString::Printf(TEXT("Question %d: `kind` must be object, choice or text."), n + 1);
			}
			if (Question.Accept != TEXT("any") && Question.Accept != TEXT("level") && Question.Accept != TEXT("asset"))
			{
				return FString::Printf(TEXT("Question %d: `accept` must be any, level or asset."), n + 1);
			}
			if (Question.Id.IsEmpty() || Ids.Contains(Question.Id)) Question.Id = FString::Printf(TEXT("q%d"), n + 1);
			Ids.Add(Question.Id);

			if (Question.Kind == TEXT("choice"))
			{
				for (const TSharedPtr<FJsonValue>& O : ZettaJson::Arr(Q, TEXT("options")))
				{
					FZettaAskOption Option;
					if (O.IsValid() && O->Type == EJson::String)
					{
						Option.Label = AskShort(O->AsString(), 80);
					}
					else if (O.IsValid() && O->Type == EJson::Object)
					{
						Option.Label = AskShort(ZettaJson::Str(O->AsObject(), TEXT("label")), 80);
						Option.Description = AskShort(ZettaJson::Str(O->AsObject(), TEXT("description")), 200);
					}
					if (!Option.Label.IsEmpty()) Question.Options.Add(Option);
				}
				if (Question.Options.Num() < 2 || Question.Options.Num() > 6)
				{
					return FString::Printf(TEXT("Question %d: a choice needs two to six `options`."), n + 1);
				}
			}
			Out.Questions.Add(MoveTemp(Question));
		}
		return FString();
	}

	FString Format(const FZettaAskRequest& Request, const TArray<FZettaAskAnswer>& Answers)
	{
		FString Out = TEXT("The user answered");
		if (!Request.Title.IsEmpty()) Out += FString::Printf(TEXT(" (card \"%s\")"), *Request.Title);
		Out += TEXT(":");
		for (const FZettaAskQuestion& Q : Request.Questions)
		{
			const FZettaAskAnswer* A = Answers.FindByPredicate([&Q](const FZettaAskAnswer& X) { return X.Id == Q.Id; });
			Out += FString::Printf(TEXT("\n- %s — %s: "), *Q.Id, *AskQuote(Q.Question));
			if (!A || A->IsEmpty())
			{
				Out += TEXT("left it empty");
				continue;
			}
			TArray<FString> Parts;
			if (A->Objects.Num() > 0)
			{
				TArray<FString> Objects;
				for (const FZettaMention& M : A->Objects) Objects.Add(M.Describe());
				Parts.Add(FString::Join(Objects, TEXT("; ")));
			}
			if (A->Choices.Num() > 0)
			{
				TArray<FString> Quoted;
				// In the order the agent listed them.
				for (const FZettaAskOption& O : Q.Options)
				{
					if (A->Choices.Contains(O.Label)) Quoted.Add(AskQuote(O.Label));
				}
				Parts.Add(TEXT("chose ") + FString::Join(Quoted, TEXT(", ")));
			}
			const FString Text = A->Text.TrimStartAndEnd();
			if (!Text.IsEmpty()) Parts.Add((Q.Kind == TEXT("text") ? TEXT("wrote: ") : TEXT("in their own words: ")) + AskQuote(Text));
			Out += FString::Join(Parts, TEXT(" — "));
		}
		return Out;
	}

	FString Skipped()
	{
		return TEXT("The user closed the card without answering (Skip). Carry on with your best judgment and say in a line what you assumed — ")
			TEXT("or, if you truly cannot go on without it, say in your reply what you need.");
	}

	FString Describe(const TSharedPtr<FJsonObject>& Input)
	{
		const FString Title = ZettaJson::Str(Input, TEXT("title")).TrimStartAndEnd();
		if (!Title.IsEmpty()) return FText::Format(LOCTEXT("AgentUI.AskingYouAbout", "Asking you: {0}"), FText::FromString(AskShort(Title, 60))).ToString();
		const TArray<TSharedPtr<FJsonObject>> Questions = ZettaJson::Objects(Input, TEXT("questions"));
		const FString First = Questions.Num() > 0 ? ZettaJson::Str(Questions[0], TEXT("question")).TrimStartAndEnd() : FString();
		return First.IsEmpty() ? LOCTEXT("AgentUI.AskingYou", "Asking you").ToString()
			: FText::Format(LOCTEXT("AgentUI.AskingYouAbout", "Asking you: {0}"), FText::FromString(AskShort(First, 60))).ToString();
	}

	bool Fits(const FZettaAskQuestion& Q, const FZettaMention& M, FString& OutWhy)
	{
		if (Q.Accept == TEXT("level") && !M.IsActor())
		{
			OutWhy = LOCTEXT("AgentUI.NeedsActor", "This needs an actor from the level (drag it from the Outliner).").ToString();
			return false;
		}
		if (Q.Accept == TEXT("asset") && !M.IsAsset())
		{
			OutWhy = LOCTEXT("AgentUI.NeedsAsset", "This needs an asset from the Content Browser, not an actor in the level.").ToString();
			return false;
		}
		const FString Type = Q.AssetType;
		if (Type.IsEmpty() || Type.Equals(TEXT("any"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("object"), ESearchCase::IgnoreCase)) return true;
		if (M.Class.Equals(Type, ESearchCase::IgnoreCase) || M.ParentClass.Equals(Type, ESearchCase::IgnoreCase)) return true;

		const UClass* Wanted = AskFindClass(Type);
		if (M.IsActor())
		{
			if (const AActor* Actor = M.FindActor(); Actor && Wanted)
			{
				if (Actor->IsA(Wanted)) return true;
				// A component class: an actor that has one fits.
				if (Wanted->IsChildOf(UActorComponent::StaticClass()) && Actor->FindComponentByClass(const_cast<UClass*>(Wanted))) return true;
			}
		}
		else if (Wanted)
		{
			const UClass* Have = AskFindClass(M.Class);
			if (Have && Have->IsChildOf(Wanted)) return true;
			const UClass* Parent = AskFindClass(M.ParentClass);
			if (Parent && Parent->IsChildOf(Wanted)) return true;
		}
		OutWhy = FText::Format(LOCTEXT("AgentUI.NeedsType", "This needs a {0}."), FText::FromString(Type)).ToString();
		return false;
	}

	FText SlotHint(const FZettaAskQuestion& Q)
	{
		const FText What = Q.AssetType.IsEmpty()
			? (Q.bMultiple ? LOCTEXT("AgentUI.SlotThings", "things") : LOCTEXT("AgentUI.SlotThing", "something"))
			: FText::FromString(Q.bMultiple ? Q.AssetType + TEXT("s") : TEXT("a ") + Q.AssetType);
		const FText From = Q.Accept == TEXT("level") ? LOCTEXT("AgentUI.FromOutliner", "from the Outliner")
			: Q.Accept == TEXT("asset") ? LOCTEXT("AgentUI.FromBrowser", "from the Content Browser")
			: LOCTEXT("AgentUI.FromEither", "from the Content Browser or the Outliner");
		return FText::Format(LOCTEXT("AgentUI.SlotHint", "Drop {0} {1} here, or use the selection"), What, From);
	}

	void Register()
	{
		FZettaAgentTools::Get().RegisterAsync(TEXT("ask_user"),
			[](const TSharedPtr<FJsonObject>& Input, FZettaToolDone Done)
			{
				FZettaAskRequest Request;
				const FString Problem = Parse(Input, Request);
				if (!Problem.IsEmpty())
				{
					Done(FZettaToolResult::Error(Problem));
					return;
				}
				FZettaAgentRuntime::Get().AskUser(Request, MoveTemp(Done));
			},
			[](const TSharedPtr<FJsonObject>& Input, TFunction<void(const FZettaToolPreview&)> Done)
			{
				FZettaToolPreview Preview;
				Preview.Title = Describe(Input);
				Done(Preview);
			});
	}
}

#undef LOCTEXT_NAMESPACE
