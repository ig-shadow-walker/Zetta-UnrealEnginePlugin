// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
struct FZettaAgentProvider;

/**
 * What the agent runs on: the Zetta account, or the user's own API keys — one row per provider the server offers:
 * paste a key and Save (it is checked straight away), Check, Replace, Remove, and a link to get one. Keys go to the
 * system key store, never to an ini file. Models are picked under the message box, not here.
 */
class SZettaAgentSettings : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZettaAgentSettings) {}
		SLATE_EVENT(FSimpleDelegate, OnBack)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SZettaAgentSettings() override;

private:
	struct FKeyState
	{
		bool bEditing = false;
		FString Status;
		bool bError = false;
		bool bChecking = false;
	};

	void Rebuild();
	TSharedRef<SWidget> Choice(bool bOwn, const FText& Title, const FText& Blurb);
	TSharedRef<SWidget> KeyRow(const FZettaAgentProvider& Provider);
	void Save(const FString& Provider, const FString& Key);
	void Check(const FString& Provider);

	TSharedPtr<SBox> Body;
	TMap<FString, FKeyState> States;
	FDelegateHandle CatalogHandle;
};
