// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"

/** A model the agent can run on with the user's own key. */
struct FZettaAgentModel
{
	FString Key;
	FString Label;
	FString Blurb;
	bool bVision = true;
	bool bDocuments = true;
	/** The thinking levels this model has, in order ("off", "low", …). Auto is always offered in front. */
	TArray<FString> Thinking;
};

/** A provider the user can bring a key for, and its models. */
struct FZettaAgentProvider
{
	FString Key;
	FString Label;
	FString KeyUrl;
	TArray<FZettaAgentModel> Models;
};

/** An entry of the Zetta account's model or thinking menu, as the server offers it for the user's plan. */
struct FZettaAgentOption
{
	FString Key;
	FString Label;
	FString Blurb;
	bool bAvailable = true;
};

/**
 * What the agent can run on, from the server's /options: the tiers and thinking levels of the user's plan, and
 * — when the server offers it — the providers and models for the user's own keys. Nothing is hardcoded but a
 * fallback until the list arrives: a new or retired model needs no plugin update.
 *
 * Own keys live in the system key store (ZettaSecureStore, "provider-<key>"), never in an ini file. For each
 * provider with a saved key, the provider itself is asked (through the server, with that key) which models it
 * offers, and that list replaces the server's starting one.
 */
class FZettaAgentCatalog
{
public:
	static FZettaAgentCatalog& Get();

	/** Asks the server again (sign-in, opening the page). Safe to call often. */
	void Refresh();
	/** Asks a provider (through the server, with the user's key) for every model the key can reach. */
	void RefreshModels(const FString& Provider);

	const TArray<FZettaAgentOption>& Tiers() const { return TierList; }
	const TArray<FZettaAgentOption>& ThinkingLevels() const { return ThinkingList; }
	/** The own-key providers, or empty when the server offers none (the feature is off there, or not loaded yet). */
	const TArray<FZettaAgentProvider>& Providers() const { return ProviderList; }
	bool OffersOwnKeys() const { return ProviderList.Num() > 0; }
	const FZettaAgentProvider* Provider(const FString& Key) const;

	// ─── The user's keys ───────────────────────────────────────────────────

	bool HasKey(const FString& Provider);
	/** The saved key, or empty. Read only to send a message or check the key. */
	FString GetKey(const FString& Provider);
	/** Saves or replaces a key. Returns an error to show, or empty. */
	FString SetKey(const FString& Provider, const FString& Key);
	void RemoveKey(const FString& Provider);
	/** Checks a saved key with the provider (through the server): Done(ok, message). */
	void CheckKey(const FString& Provider, TFunction<void(bool, const FString&)> Done);

	// ─── What a message runs on ───────────────────────────────────────────

	/** The user chose their own keys, and this server offers them. */
	bool OwnKeys() const;
	/** Providers with a saved key, in the server's order. */
	TArray<const FZettaAgentProvider*> KeyedProviders();
	/** The provider a message goes to: the one picked, if it still has a key; otherwise the first that does. */
	const FZettaAgentProvider* ActiveProvider();
	/** The model a message goes to on the active provider: the one last used with it, or its first. */
	const FZettaAgentModel* ActiveModel();
	FString ModelFor(const FString& Provider) const;
	void SetModelFor(const FString& Provider, const FString& Model);

	/** The thinking a message asks for: Auto, the chosen level, or — when the model lacks it — the nearest it has. */
	static FString ThinkingFor(const FZettaAgentModel* Model, const FString& Wanted);
	/** "Medium thinking", "No thinking", "Auto thinking". */
	static FText ThinkingLabel(const FString& Level);

	/** The lists, a key, or the user's choice changed: menus rebuild. */
	FSimpleMulticastDelegate OnChanged;

private:
	void Fallback();

	TArray<FZettaAgentOption> TierList;
	TArray<FZettaAgentOption> ThinkingList;
	TArray<FZettaAgentProvider> ProviderList;
	/** Each provider's own list for the user's key, by provider. */
	TMap<FString, TArray<FZettaAgentModel>> Live;
	TMap<FString, bool> KnownKeys;
	TSet<FString> Fetching;
	bool bLoading = false;
	bool bInitialised = false;
};
