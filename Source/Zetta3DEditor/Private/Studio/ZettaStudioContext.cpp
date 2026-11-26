// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaStudioContext.h"

#include "ZettaApi.h"
#include "ZettaAuth.h"
#include "ZettaCatalog.h"
#include "ZettaConfig.h"

FZettaStudioContext& FZettaStudioContext::Get()
{
	static FZettaStudioContext Instance;
	return Instance;
}

FZettaStudioContext::FZettaStudioContext()
{
	FZettaAuth::Get().OnChanged.AddRaw(this, &FZettaStudioContext::OnAuthChanged);
}

void FZettaStudioContext::OnAuthChanged()
{
	if (FZettaAuth::Get().IsSignedIn()) EnsureLoaded();
	else if (!DataFor.IsEmpty() || Gates3D.IsSet() || Prices.IsSet()) Reset();
}

FString FZettaStudioContext::AccountKey() const
{
	const FZettaAuth& Auth = FZettaAuth::Get();
	if (!Auth.IsSignedIn() || Auth.GetUser().Id == 0) return FString();
	return FString::Printf(TEXT("%s|%lld"), *ZettaConfig::ApiBase(), Auth.GetUser().Id);
}

void FZettaStudioContext::EnsureLoaded()
{
	const FString Key = AccountKey();
	if (Key.IsEmpty() || bLoading || LoadedFor == Key) return;
	if (DataFor != Key)
	{
		// A different account: never show the last one's plan or prices while loading.
		Gates3D.Reset();
		Gates2D.Reset();
		Prices.Reset();
		ImageModels.Reset();
	}
	Load();
}

void FZettaStudioContext::Load()
{
	const FString Key = AccountKey();
	if (Key.IsEmpty()) return;
	bLoading = true;
	LoadError.Reset();
	const int32 My = ++Ticket;
	OnChanged.Broadcast();

	struct FBatch
	{
		int32 Left = 4;
		TOptional<FZettaEntitlements3D> G3;
		TOptional<FZettaEntitlements2D> G2;
		TOptional<FZettaPriceList> Prices;
		TOptional<FZettaImageModels> Models;
		FString G3Error, G2Error, PricesError, ModelsError;
	};
	TSharedRef<FBatch> B = MakeShared<FBatch>();

	auto Done = [this, B, My, Key]()
	{
		if (--B->Left > 0 || My != Ticket) return;
		Gates3D = B->G3;
		Gates2D = B->G2;
		Prices = B->Prices;
		ImageModels = B->Models;
		// The price list matters most: it is the first thing named when several failed.
		for (const FString* E : { &B->PricesError, &B->G3Error, &B->ModelsError, &B->G2Error })
		{
			if (!E->IsEmpty())
			{
				LoadError = *E;
				break;
			}
		}
		LoadedFor = LoadError.IsEmpty() ? Key : FString();
		DataFor = Key;
		bLoading = false;
		OnChanged.Broadcast();
	};

	ZettaApi::Entitlements3D([B, Done](const FZettaResponse& R, const FZettaEntitlements3D& V)
	{
		if (R.bOk) B->G3 = V;
		else B->G3Error = ZettaCatalog::Explain(R);
		Done();
	});
	ZettaApi::Entitlements2D([B, Done](const FZettaResponse& R, const FZettaEntitlements2D& V)
	{
		if (R.bOk) B->G2 = V;
		else B->G2Error = ZettaCatalog::Explain(R);
		Done();
	});
	ZettaApi::Pricing([B, Done](const FZettaResponse& R, const FZettaPriceList& V)
	{
		if (R.bOk) B->Prices = V;
		else B->PricesError = ZettaCatalog::Explain(R);
		Done();
	});
	ZettaApi::ImageModels([B, Done](const FZettaResponse& R, const FZettaImageModels& V)
	{
		if (R.bOk) B->Models = V;
		else B->ModelsError = ZettaCatalog::Explain(R);
		Done();
	});
}

void FZettaStudioContext::Reset()
{
	++Ticket;
	Gates3D.Reset();
	Gates2D.Reset();
	Prices.Reset();
	ImageModels.Reset();
	LoadedFor.Reset();
	DataFor.Reset();
	LoadError.Reset();
	bLoading = false;
	OnChanged.Broadcast();
}

int64 FZettaStudioContext::Credits() const
{
	return FZettaAuth::Get().IsSignedIn() ? FZettaAuth::Get().GetUser().Credits3D.Total() : 0;
}

TOptional<int32> FZettaStudioContext::Price(const FString& Code, const FString& Model) const
{
	return Prices.IsSet() ? Prices->Price(Code, Model) : TOptional<int32>();
}

void FZettaStudioContext::RefreshCredits()
{
	FZettaAuth::Get().Refresh();
}
