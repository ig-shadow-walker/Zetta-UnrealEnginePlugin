// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "ZettaModels.h"

class FZettaCallbackServer;

/**
 * Sign-in through the browser, the same handoff the other Zetta plugins use: the editor listens on
 * 127.0.0.1, opens zetta3d.ai/plugin-auth, the user presses Connect, and the page POSTs {token, state}
 * to http://127.0.0.1:<port>/callback. The token is a plugin session — the editor never sees a password.
 */
class ZETTA3DEDITOR_API FZettaAuth
{
public:
	enum class EState : uint8 { SignedOut, WaitingForBrowser, Checking, SignedIn };

	static FZettaAuth& Get();

	void Initialize();
	void Shutdown();

	EState GetState() const { return State; }
	bool IsSignedIn() const { return State == EState::SignedIn; }
	const FZettaUser& GetUser() const { return User; }
	const FString& GetLastError() const { return LastError; }
	FString GetToken() const;

	/** Opens the browser and waits for the page to hand the session back (10 minutes at most). */
	void BeginSignIn();
	void CancelSignIn();
	void SignOut(const FString& Reason = FString());
	/** Re-reads the account (plan, credits). Safe to call often. */
	void Refresh();

	/** Raised on the game thread whenever the state or the account changes. */
	FSimpleMulticastDelegate OnChanged;

private:
	void Set(EState NewState);
	void Complete(const FString& Token);
	bool TokenExpired() const;
	static int64 ReadExpiry(const FString& Jwt);

	EState State = EState::SignedOut;
	FZettaUser User;
	FString LastError;
	mutable FString CachedToken;
	mutable bool bTokenLoaded = false;
	TSharedPtr<FZettaCallbackServer> Server;
	FDelegateHandle UnauthorizedHandle;
};
