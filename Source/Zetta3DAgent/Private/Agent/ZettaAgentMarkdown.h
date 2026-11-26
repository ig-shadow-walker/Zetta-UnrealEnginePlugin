// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"

class ISlateStyle;
class SWidget;

/**
 * Enough Markdown for chat replies: paragraphs, headings, bullet and numbered lists, bold, italic, inline code,
 * links and fenced code blocks (with Copy). Text is selectable, so any of it can be copied.
 */
namespace ZettaAgentMarkdown
{
	/** The text styles the rich text uses ("Md.Bold", "Md.Code"…). Registered once, at module startup. */
	void Initialize();
	void Shutdown();
	const ISlateStyle& Style();
	/** Rebuilds the styles at the user's chat text size (points from the standard). Widgets made after this use it. */
	void SetTextSize(int32 Step);

	TSharedRef<SWidget> Render(const FString& Markdown);
	/**
	 * A reply while it is still being written: one rich-text widget that reads the growing text each frame and
	 * reformats in place. Rebuilding Render's widgets for every streamed word made the message flash (a new text
	 * widget has no height for a frame). Code fences show as text until the reply is done and drawn with Render.
	 */
	TSharedRef<SWidget> RenderLive(TFunction<FString()> GetText);
	/** Plain text, selectable (a user's message, an error), in one of the styles: "Md.Normal", "Md.Muted", "Md.Error". */
	TSharedRef<SWidget> Plain(const FString& Text, FName StyleName = TEXT("Md.Normal"));
	/** A block of code: monospace, selectable, with Copy. */
	TSharedRef<SWidget> Code(const FString& Code, const FString& Language);
}
