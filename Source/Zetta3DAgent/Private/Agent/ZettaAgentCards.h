// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "ZettaAgentMentions.h"
#include "ZettaAgentRuntime.h"

class SWidget;

/** The cards in the agent's conversation that the user acts on, and the small pieces they share. */
namespace ZettaAgentCards
{
	/** A short note in the corner of the editor ("Up to 4 images per message."). */
	void Toast(const FText& Text, bool bError = false);
	/** A small rounded label ("Needs your input", "New"). */
	TSharedRef<SWidget> Chip(const FText& Text, const FLinearColor& Color);
	/** A button that looks like a link or a quiet toolbar button. */
	TSharedRef<SWidget> QuietButton(const TAttribute<FText>& Text, FSimpleDelegate OnClicked, const FText& Tooltip = FText(), const TAttribute<bool>& Enabled = true);
	/** A row of a mention list: icon, name, and where it is. */
	TSharedRef<SWidget> MentionRow(const FZettaMention& Mention, const TAttribute<bool>& Highlighted, FSimpleDelegate OnPicked);
	/** A tagged thing above the message box or in an object slot, with ✕ to remove it. */
	TSharedRef<SWidget> MentionChip(const FZettaMention& Mention, FSimpleDelegate OnRemove);

	/** Allow or Decline before a tool runs: what it will do, its code, what it costs. */
	TSharedRef<SWidget> Approval(const FZettaChatItemPtr& Item);
	/** ask_options: the agent's turn ended with options to choose. */
	TSharedRef<SWidget> Question(const FZettaChatItemPtr& Item);
	/** ask_user: object slots, choices and text, with OK and Skip. */
	TSharedRef<SWidget> Ask(const FZettaChatItemPtr& Item);
	/** What a message changed, with Keep and Undo per line and for all of it. */
	TSharedRef<SWidget> Changes(const FString& Turn);

	/**
	 * A list to search actors or assets and pick one (an object slot's Pick…, + › Picture from the project). Accept is
	 * "any", "level" or "asset"; AssetClass, when set, keeps only assets of that class ("Texture2D").
	 */
	TSharedRef<SWidget> SearchPicker(const FString& Accept, TFunction<void(const FZettaMention&)> OnPicked, const FString& AssetClass = FString());
}
