// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "ZettaAgentMentions.h"

struct FZettaChatItem;

struct FZettaAskOption
{
	FString Label;
	FString Description;
};

/** One question on an ask_user card. */
struct FZettaAskQuestion
{
	FString Id;
	FString Question;
	/** "object", "choice" or "text". */
	FString Kind;
	/** object: "any", "level" or "asset". */
	FString Accept = TEXT("any");
	/** object: a class name ("StaticMesh", "Blueprint", an actor or component class); empty takes anything. */
	FString AssetType;
	TArray<FZettaAskOption> Options;
	bool bMultiple = false;
	/** choice and object: a box for the user's own words. */
	bool bAllowOther = true;
	FString Placeholder;
	bool bOptional = false;
};

struct FZettaAskRequest
{
	FString Title;
	TArray<FZettaAskQuestion> Questions;
};

/** What the user has given for one question — kept on the card, so a redraw of the chat keeps it. */
struct FZettaAskAnswer
{
	FString Id;
	TArray<FString> Choices;
	TArray<FZettaMention> Objects;
	/** Their own words: a text answer, or "something else" on a choice or an object question. */
	FString Text;
	bool bOtherOpen = false;

	bool IsEmpty() const { return Choices.Num() == 0 && Objects.Num() == 0 && Text.TrimStartAndEnd().IsEmpty(); }
};

/**
 * The ask_user client tool: a card in the chat — drop actors or assets in, pick options, or write — that waits for
 * the user's OK. The turn carries on with the answers as the call's result, so the agent never has to start over.
 */
namespace ZettaAgentAsk
{
	/** Reads and checks the call. Returns what to fix, or empty. */
	FString Parse(const TSharedPtr<FJsonObject>& Input, FZettaAskRequest& Out);
	/** The answers as the agent reads them: one line per question, objects as targetable addresses. */
	FString Format(const FZettaAskRequest& Request, const TArray<FZettaAskAnswer>& Answers);
	/** What the agent gets when the user presses Skip. */
	FString Skipped();
	/** "Asking you: Pick the projectile". */
	FString Describe(const TSharedPtr<FJsonObject>& Input);

	/** Whether an actor or asset fits an object question; the reason when it does not. */
	bool Fits(const FZettaAskQuestion& Question, const FZettaMention& Mention, FString& OutWhy);
	/** "Drop a StaticMesh from the Content Browser here". */
	FText SlotHint(const FZettaAskQuestion& Question);

	/** Registers the tool with FZettaAgentTools. */
	void Register();
}
