// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "ZettaAgentMentions.h"
#include "ZettaAgentRuntime.h"

/**
 * A file the user attached to a message, sorted into what the agent can take: a picture, a PDF, text (inlined in
 * the message), or a mention — a file already in the project, or anything else copied into
 * Content/Zetta3D/Attachments/ so the agent can import it.
 */
struct FZettaAttachment
{
	/** "image", "document", "text" or "mention". */
	FString Kind;
	FString Label;
	/** Image or document: what is sent. */
	FZettaAgentFile File;
	/** Text: its contents. */
	FString Text;
	/** Mention: the file or asset it became. */
	FZettaMention Mention;
	/** Image: the picture as it was on disk (File may be scaled down), kept for the picture tools. */
	TArray<uint8> Original;
	FString OriginalName;
};

namespace ZettaAgentAttachments
{
	constexpr int32 MaxTextBytes = 200 * 1024;
	constexpr int32 MaxPdfBytes = 6 * 1024 * 1024;
	/** One picture's base64, as the server takes it. */
	constexpr int32 MaxImageChars = 4000000;

	/** Sorts a file from disk. Returns why it can't be used, or empty. */
	FString FromPath(const FString& Path, FZettaAttachment& Out);
	/** The block a text attachment adds to the message. */
	FString TextBlock(const FZettaAttachment& Attachment);

	/** A picture to send: as it is when small, otherwise scaled to 1600 px and re-encoded as JPEG. */
	FString Picture(const TArray<uint8>& Bytes, const FString& Name, FZettaAgentFile& Out);
	/** A picture of the level viewport, as JPEG (≤1024 px wide). Returns why it could not, or empty. */
	FString CaptureViewport(TArray<uint8>& OutJpeg);

	/**
	 * Keeps an attached picture under Saved/Zetta3D/Attached (outside the content, for a week) so the picture tools
	 * can edit or combine it by name. Returns its "attached:<name>", or empty when it could not be kept.
	 */
	FString Keep(const TArray<uint8>& Bytes, const FString& Name);
	/** The file behind an "attached:<name>", or empty. */
	FString FileOf(const FString& Key);
}
