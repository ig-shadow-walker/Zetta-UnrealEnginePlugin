// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"

/**
 * Secrets (the plugin session, the user's own AI keys) in the system's key store: the macOS login
 * Keychain, Windows Credential Manager, or — on Linux, where the engine has no keyring binding — a file
 * in the user's config folder readable only by them. Never the project, never the editor's ini files.
 */
namespace ZettaSecureStore
{
	ZETTA3DEDITOR_API bool Set(const FString& Name, const FString& Secret);
	ZETTA3DEDITOR_API FString Get(const FString& Name);
	ZETTA3DEDITOR_API bool Remove(const FString& Name);
	/** Where secrets are kept, in the user's words ("the macOS Keychain"). */
	ZETTA3DEDITOR_API FText Describe();
}
