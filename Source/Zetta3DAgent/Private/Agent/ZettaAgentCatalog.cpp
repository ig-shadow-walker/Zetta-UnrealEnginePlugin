// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaAgentCatalog.h"

#include "ZettaAuth.h"
#include "ZettaHttp.h"
#include "ZettaJson.h"
#include "ZettaSecureStore.h"
#include "ZettaSettings.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace
{
	const TCHAR* const CatalogRoute = TEXT("/unrealAgent");

	/** Every key is filed under its provider's name, apart from the plugin's own session. */
	FString CatalogStoreName(const FString& Provider) { return TEXT("provider-") + Provider; }

	FZettaAgentModel CatalogReadModel(const TSharedPtr<FJsonObject>& O)
	{
		FZettaAgentModel M;
		M.Key = ZettaJson::Str(O, TEXT("key"));
		M.Label = ZettaJson::Str(O, TEXT("label"), M.Key);
		M.Blurb = ZettaJson::Str(O, TEXT("blurb"));
		M.bVision = ZettaJson::Bool(O, TEXT("vision"), true);
		M.bDocuments = ZettaJson::Bool(O, TEXT("documents"), true);
		M.Thinking = ZettaJson::Strings(O, TEXT("thinking"));
		return M;
	}

	TArray<FZettaAgentModel> CatalogReadModels(const TSharedPtr<FJsonObject>& O, const FString& Field)
	{
		TArray<FZettaAgentModel> Out;
		for (const TSharedPtr<FJsonObject>& M : ZettaJson::Objects(O, Field))
		{
			FZettaAgentModel Model = CatalogReadModel(M);
			if (!Model.Key.IsEmpty()) Out.Add(MoveTemp(Model));
		}
		return Out;
	}

	const TArray<FString>& CatalogThinkingOrder()
	{
		static const TArray<FString> Order = { TEXT("off"), TEXT("low"), TEXT("medium"), TEXT("high"), TEXT("xhigh"), TEXT("max") };
		return Order;
	}
}

FZettaAgentCatalog& FZettaAgentCatalog::Get()
{
	static FZettaAgentCatalog Instance;
	if (!Instance.bInitialised)
	{
		Instance.bInitialised = true;
		Instance.Fallback();
	}
	return Instance;
}

/** What the menus show before the server answers: the four tiers and every thinking level, all on Auto by default. */
void FZettaAgentCatalog::Fallback()
{
	TierList = {
		{ TEXT("auto"), TEXT("Auto"), TEXT("Picks the model and the thinking for each message."), true },
		{ TEXT("fast"), TEXT("Fast"), TEXT("Quick answers and small edits."), true },
		{ TEXT("capable"), TEXT("Capable"), TEXT("Everyday building."), true },
		{ TEXT("intelligent"), TEXT("Intelligent"), TEXT("The hardest problems."), true },
	};
	ThinkingList.Reset();
	ThinkingList.Add({ TEXT("auto"), TEXT("Auto"), FString(), true });
	for (const FString& Level : CatalogThinkingOrder()) ThinkingList.Add({ Level, ThinkingLabel(Level).ToString(), FString(), true });
}

void FZettaAgentCatalog::Refresh()
{
	if (bLoading || !FZettaAuth::Get().IsSignedIn()) return;
	bLoading = true;
	FZettaHttp::Get(FString(CatalogRoute) + TEXT("/options"), [this](const FZettaResponse& R)
	{
		bLoading = false;
		// Offline or an older server: keep what we had; the Zetta account still works.
		if (!R.bOk) return;
		const TSharedPtr<FJsonObject> Body = R.Object();

		TArray<FZettaAgentOption> Tiers;
		for (const TSharedPtr<FJsonObject>& T : ZettaJson::Objects(Body, TEXT("tiers")))
		{
			Tiers.Add({ ZettaJson::Str(T, TEXT("key")), ZettaJson::Str(T, TEXT("label")), ZettaJson::Str(T, TEXT("blurb")), ZettaJson::Bool(T, TEXT("available"), true) });
		}
		if (Tiers.Num() > 0) TierList = MoveTemp(Tiers);

		TArray<FZettaAgentOption> Levels;
		for (const TSharedPtr<FJsonObject>& T : ZettaJson::Objects(Body, TEXT("thinking")))
		{
			const FString Key = ZettaJson::Str(T, TEXT("key"));
			Levels.Add({ Key, ThinkingLabel(Key).ToString(), FString(), ZettaJson::Bool(T, TEXT("available"), true) });
		}
		if (Levels.Num() > 0) ThinkingList = MoveTemp(Levels);

		ProviderList.Reset();
		for (const TSharedPtr<FJsonObject>& P : ZettaJson::Objects(Body, TEXT("providers")))
		{
			FZettaAgentProvider Provider;
			Provider.Key = ZettaJson::Str(P, TEXT("key"));
			Provider.Label = ZettaJson::Str(P, TEXT("label"), Provider.Key);
			Provider.KeyUrl = ZettaJson::Str(P, TEXT("keyUrl"));
			Provider.Models = CatalogReadModels(P, TEXT("models"));
			if (const TArray<FZettaAgentModel>* Own = Live.Find(Provider.Key); Own && Own->Num() > 0) Provider.Models = *Own;
			if (!Provider.Key.IsEmpty()) ProviderList.Add(MoveTemp(Provider));
		}
		OnChanged.Broadcast();
		for (const FZettaAgentProvider* Keyed : KeyedProviders()) RefreshModels(Keyed->Key);
	});
}

void FZettaAgentCatalog::RefreshModels(const FString& ProviderKey)
{
	if (!FZettaAuth::Get().IsSignedIn() || !Provider(ProviderKey) || Fetching.Contains(ProviderKey)) return;
	const FString Key = GetKey(ProviderKey);
	if (Key.IsEmpty()) return;
	Fetching.Add(ProviderKey);
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("provider"), ProviderKey);
	FZettaHttp::Post(FString(CatalogRoute) + TEXT("/provider-models"), Body, [this, ProviderKey](const FZettaResponse& R)
	{
		Fetching.Remove(ProviderKey);
		// Offline, an older server, or the provider said no: the server's starting list still works.
		if (!R.bOk || !ZettaJson::Bool(R.Object(), TEXT("ok"))) return;
		TArray<FZettaAgentModel> Models = CatalogReadModels(R.Object(), TEXT("models"));
		if (Models.Num() == 0) return;
		Live.Add(ProviderKey, Models);
		for (FZettaAgentProvider& P : ProviderList)
		{
			if (P.Key == ProviderKey) P.Models = Models;
		}
		OnChanged.Broadcast();
	}, { { TEXT("X-Zetta-Provider-Key"), Key } }, 30.f);
}

const FZettaAgentProvider* FZettaAgentCatalog::Provider(const FString& Key) const
{
	return ProviderList.FindByPredicate([&Key](const FZettaAgentProvider& P) { return P.Key == Key; });
}

// ─── Keys ─────────────────────────────────────────────────────────────────────

bool FZettaAgentCatalog::HasKey(const FString& ProviderKey)
{
	// Remembered: the key store is asked once per provider, not on every frame a menu is drawn.
	if (const bool* Known = KnownKeys.Find(ProviderKey)) return *Known;
	const bool bHas = !ZettaSecureStore::Get(CatalogStoreName(ProviderKey)).IsEmpty();
	KnownKeys.Add(ProviderKey, bHas);
	return bHas;
}

FString FZettaAgentCatalog::GetKey(const FString& ProviderKey)
{
	return ZettaSecureStore::Get(CatalogStoreName(ProviderKey)).TrimStartAndEnd();
}

FString FZettaAgentCatalog::SetKey(const FString& ProviderKey, const FString& InKey)
{
	const FString Key = InKey.TrimStartAndEnd();
	if (Key.IsEmpty()) return LOCTEXT("AgentUI.PasteKeyFirst", "Paste a key first.").ToString();
	if (Key.Len() > 512) return LOCTEXT("AgentUI.KeyTooLong", "That doesn't look like an API key — it is too long.").ToString();
	if (!ZettaSecureStore::Set(CatalogStoreName(ProviderKey), Key))
	{
		return FText::Format(LOCTEXT("AgentUI.KeyNotSaved", "Could not save the key in {0}."), ZettaSecureStore::Describe()).ToString();
	}
	KnownKeys.Add(ProviderKey, true);
	OnChanged.Broadcast();
	return FString();
}

void FZettaAgentCatalog::RemoveKey(const FString& ProviderKey)
{
	ZettaSecureStore::Remove(CatalogStoreName(ProviderKey));
	KnownKeys.Add(ProviderKey, false);
	// A removed key takes its provider's own model list with it.
	Live.Remove(ProviderKey);
	OnChanged.Broadcast();
}

void FZettaAgentCatalog::CheckKey(const FString& ProviderKey, TFunction<void(bool, const FString&)> Done)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("provider"), ProviderKey);
	FZettaHttp::Post(FString(CatalogRoute) + TEXT("/provider-key/check"), Body, [this, ProviderKey, Done](const FZettaResponse& R)
	{
		if (!R.bOk)
		{
			Done(false, R.Status == 404 ? LOCTEXT("AgentUI.OwnKeysOff", "Using your own API key is not available on this server yet.").ToString() : R.Message);
			return;
		}
		const bool bOk = ZettaJson::Bool(R.Object(), TEXT("ok"));
		const FString Message = bOk ? LOCTEXT("AgentUI.KeyWorks", "The key works.").ToString()
			: ZettaJson::Str(R.Object(), TEXT("message"), LOCTEXT("AgentUI.KeyDidNotWork", "The key didn't work.").ToString());
		// A working key: its provider's own model list replaces the server's starting one.
		if (bOk) RefreshModels(ProviderKey);
		Done(bOk, Message);
	}, { { TEXT("X-Zetta-Provider-Key"), GetKey(ProviderKey) } }, 30.f);
}

// ─── What a message runs on ───────────────────────────────────────────────────

bool FZettaAgentCatalog::OwnKeys() const
{
	return UZettaUserSettings::Get()->bAgentUseOwnKeys && OffersOwnKeys();
}

TArray<const FZettaAgentProvider*> FZettaAgentCatalog::KeyedProviders()
{
	TArray<const FZettaAgentProvider*> Out;
	for (const FZettaAgentProvider& P : ProviderList)
	{
		if (HasKey(P.Key)) Out.Add(&P);
	}
	return Out;
}

const FZettaAgentProvider* FZettaAgentCatalog::ActiveProvider()
{
	if (!OwnKeys()) return nullptr;
	const TArray<const FZettaAgentProvider*> Keyed = KeyedProviders();
	const FString Picked = UZettaUserSettings::Get()->AgentProvider;
	for (const FZettaAgentProvider* P : Keyed)
	{
		if (P->Key == Picked) return P;
	}
	return Keyed.Num() > 0 ? Keyed[0] : nullptr;
}

const FZettaAgentModel* FZettaAgentCatalog::ActiveModel()
{
	const FZettaAgentProvider* P = ActiveProvider();
	if (!P || P->Models.Num() == 0) return nullptr;
	const FString Last = ModelFor(P->Key);
	const FZettaAgentModel* Found = P->Models.FindByPredicate([&Last](const FZettaAgentModel& M) { return M.Key == Last; });
	return Found ? Found : &P->Models[0];
}

FString FZettaAgentCatalog::ModelFor(const FString& ProviderKey) const
{
	const FString Prefix = ProviderKey + TEXT("=");
	for (const FString& Entry : UZettaUserSettings::Get()->AgentModels)
	{
		if (Entry.StartsWith(Prefix)) return Entry.Mid(Prefix.Len());
	}
	return FString();
}

void FZettaAgentCatalog::SetModelFor(const FString& ProviderKey, const FString& Model)
{
	UZettaUserSettings* Settings = UZettaUserSettings::Get();
	const FString Prefix = ProviderKey + TEXT("=");
	Settings->AgentModels.RemoveAll([&Prefix](const FString& Entry) { return Entry.StartsWith(Prefix); });
	Settings->AgentModels.Add(Prefix + Model);
	Settings->Save();
	OnChanged.Broadcast();
}

FString FZettaAgentCatalog::ThinkingFor(const FZettaAgentModel* Model, const FString& Wanted)
{
	if (Wanted == TEXT("auto") || !Model || Model->Thinking.Num() == 0 || Model->Thinking.Contains(Wanted)) return Wanted;
	// The nearest level the model has, deeper on a tie — the same rule the server runs by.
	const int32 At = CatalogThinkingOrder().IndexOfByKey(Wanted);
	FString Best = Model->Thinking[0];
	int32 BestDistance = MAX_int32;
	for (const FString& Level : Model->Thinking)
	{
		const int32 Index = CatalogThinkingOrder().IndexOfByKey(Level);
		const int32 Distance = FMath::Abs(Index - At);
		if (Distance < BestDistance || (Distance == BestDistance && Index > CatalogThinkingOrder().IndexOfByKey(Best)))
		{
			Best = Level;
			BestDistance = Distance;
		}
	}
	return Best;
}

FText FZettaAgentCatalog::ThinkingLabel(const FString& Level)
{
	if (Level == TEXT("off")) return LOCTEXT("AgentUI.ThinkOff", "No thinking");
	if (Level == TEXT("low")) return LOCTEXT("AgentUI.ThinkLow", "Low thinking");
	if (Level == TEXT("medium")) return LOCTEXT("AgentUI.ThinkMedium", "Medium thinking");
	if (Level == TEXT("high")) return LOCTEXT("AgentUI.ThinkHigh", "High thinking");
	if (Level == TEXT("xhigh")) return LOCTEXT("AgentUI.ThinkXHigh", "Extra high thinking");
	if (Level == TEXT("max")) return LOCTEXT("AgentUI.ThinkMax", "Max thinking");
	return LOCTEXT("AgentUI.ThinkAuto", "Auto thinking");
}

#undef LOCTEXT_NAMESPACE
