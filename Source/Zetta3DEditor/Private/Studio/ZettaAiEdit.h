// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"

/**
 * AI edit — change a picture in a 3D input with a sentence before it becomes 3D (the web Studio's AI edit
 * dialog). Edits build on each other; nothing reaches the tool until the accept button. On a plan without
 * image editing the picture is shown large, with the way to unlock it in place of the field.
 */
namespace ZettaAiEdit
{
	using FOnAccept = TFunction<void(const TArray<uint8>& /*Bytes*/, const FString& /*ContentType*/)>;

	void Open(const FText& Title, const TArray<uint8>& Bytes, const FString& ContentType, FOnAccept OnAccept, const FText& AcceptLabel = FText());
}
