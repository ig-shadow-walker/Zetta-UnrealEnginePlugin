// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "ZettaModels.h"

/**
 * What every Studio tool needs to know about the account: plan gates (3D and 2D), the price list and the
 * image models. Loaded once per sign-in and kept while the editor runs. An unset gate means "unknown" —
 * the tools never lock anything on an unknown gate.
 */
class FZettaStudioContext
{
public:
	static FZettaStudioContext& Get();

	TOptional<FZettaEntitlements3D> Gates3D;
	TOptional<FZettaEntitlements2D> Gates2D;
	TOptional<FZettaPriceList> Prices;
	TOptional<FZettaImageModels> ImageModels;
	bool bLoading = false;
	/** Why something did not load (shown as a banner with a retry), or empty. */
	FString LoadError;

	/** Raised after the account data (re)loads or is forgotten. */
	FSimpleMulticastDelegate OnChanged;

	const FZettaEntitlements3D* G3() const { return Gates3D.GetPtrOrNull(); }

	/** Loads the account data unless it is already loaded for the signed-in account. */
	void EnsureLoaded();
	void Load();
	/** Forgets the account data (sign-out). */
	void Reset();

	/** The 3D credits the account has. */
	int64 Credits() const;
	/** A price from the list, or unset while it is not loaded (or has no such row). */
	TOptional<int32> Price(const FString& Code, const FString& Model = FString()) const;

	/** Re-reads the balance after a job is charged. */
	static void RefreshCredits();

	/** A gate that is known and closed. */
	static bool No(const TOptional<bool>& Gate) { return Gate.IsSet() && !Gate.GetValue(); }
	/** A gate that is known and open. */
	static bool Yes(const TOptional<bool>& Gate) { return Gate.IsSet() && Gate.GetValue(); }

private:
	FZettaStudioContext();
	void OnAuthChanged();
	FString AccountKey() const;

	FString LoadedFor;
	FString DataFor;
	int32 Ticket = 0;
};
