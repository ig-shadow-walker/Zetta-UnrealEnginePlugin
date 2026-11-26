// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaJobs.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "ZettaApi.h"
#include "ZettaAuth.h"
#include "ZettaCatalog.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

FZettaJobs& FZettaJobs::Get()
{
	static FZettaJobs Instance;
	return Instance;
}

FZettaJobPtr FZettaJobs::Track(const FZettaGenerationPtr& Generation, bool bAutoImport, const FZettaImportOptions& Options, const FString& Source)
{
	if (!Generation.IsValid()) return nullptr;
	if (FZettaJobPtr Existing = Find(Generation->Id))
	{
		Existing->Generation = Generation;
		Existing->bAutoImport = bAutoImport;
		Existing->ImportOptions = Options;
		OnChanged.Broadcast();
		return Existing;
	}
	FZettaJobPtr Job = MakeShared<FZettaJob>();
	Job->Generation = Generation;
	Job->bAutoImport = bAutoImport;
	Job->ImportOptions = Options;
	Job->Source = Source;
	Job->StartedAt = FPlatformTime::Seconds();
	Jobs.Insert(Job, 0);
	if (!TickHandle.IsValid())
	{
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FZettaJobs::Tick), 0.5f);
	}
	if (Generation->IsTerminal()) Completed(Job);
	OnChanged.Broadcast();
	return Job;
}

FZettaJobPtr FZettaJobs::Find(int64 Id) const
{
	const FZettaJobPtr* Job = Jobs.FindByPredicate([Id](const FZettaJobPtr& J) { return J->Id() == Id; });
	return Job ? *Job : nullptr;
}

void FZettaJobs::Dismiss(int64 Id)
{
	Jobs.RemoveAll([Id](const FZettaJobPtr& J) { return J->Id() == Id; });
	OnChanged.Broadcast();
}

void FZettaJobs::DismissFinished()
{
	Jobs.RemoveAll([](const FZettaJobPtr& J) { return J->IsFinished(); });
	OnChanged.Broadcast();
}

int32 FZettaJobs::RunningCount() const
{
	int32 N = 0;
	for (const FZettaJobPtr& J : Jobs) if (!J->IsFinished()) ++N;
	return N;
}

void FZettaJobs::Cancel(int64 Id)
{
	ZettaApi::CancelGeneration(Id, [this, Id](const FZettaResponse& R, const FZettaGenerationPtr& G)
	{
		FZettaJobPtr Job = Find(Id);
		if (!Job) return;
		if (R.bOk && G.IsValid()) Job->Generation = G;
		else Job->Error = ZettaCatalog::Explain(R);
		if (Job->Generation->IsTerminal()) Completed(Job);
		OnChanged.Broadcast();
	});
}

bool FZettaJobs::Tick(float)
{
	if (!FZettaAuth::Get().IsSignedIn()) return true;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastPoll < 2.5) return true;
	LastPoll = Now;
	for (const FZettaJobPtr& Job : Jobs)
	{
		if (!Job->Generation->IsTerminal() && !Job->bPollInFlight) Poll(Job);
	}
	return true;
}

void FZettaJobs::Poll(const FZettaJobPtr& Job)
{
	Job->bPollInFlight = true;
	TWeakPtr<FZettaJob> Weak = Job;
	ZettaApi::GetGeneration(Job->Id(), [this, Weak](const FZettaResponse& R, const FZettaGenerationPtr& G, TOptional<int32> Position)
	{
		FZettaJobPtr Job = Weak.Pin();
		if (!Job) return;
		Job->bPollInFlight = false;
		if (!R.bOk || !G.IsValid())
		{
			// A blip: keep polling. A missing row is the end of it.
			if (R.Status == 404)
			{
				Job->Generation->Status = TEXT("failed");
				Job->Error = TEXT("This job no longer exists.");
				Completed(Job);
			}
			return;
		}
		const bool bChanged = G->Status != Job->Generation->Status || Position != Job->QueuePosition;
		Job->Generation = G;
		Job->QueuePosition = Position;
		if (G->IsTerminal()) Completed(Job);
		else if (bChanged) OnChanged.Broadcast();
	});
}

void FZettaJobs::Completed(const FZettaJobPtr& Job)
{
	const FZettaGenerationPtr G = Job->Generation;
	if (G->IsSuccess() && Job->bAutoImport && !Job->Import.IsSet() && !G->IsSplat())
	{
		Job->bImporting = true;
		Job->ImportStatus = TEXT("Downloading…");
		OnChanged.Broadcast();
		TWeakPtr<FZettaJob> Weak = Job;
		ZettaImporter::Import(G, Job->ImportOptions, [this, Weak](const FZettaImportResult& Result)
		{
			FZettaJobPtr Job = Weak.Pin();
			if (!Job) return;
			Job->bImporting = false;
			Job->Import = Result;
			if (!Result.bOk) Job->Error = Result.Error;
			Notify(Job);
			OnChanged.Broadcast();
			OnFinished.Broadcast(Job);
		}, [this, Weak](const FString& Status, float Progress)
		{
			if (FZettaJobPtr Job = Weak.Pin())
			{
				Job->ImportStatus = Status;
				Job->ImportProgress = Progress;
				OnChanged.Broadcast();
			}
		});
		return;
	}
	if (G->Status == TEXT("failed") && Job->Error.IsEmpty()) Job->Error = G->ErrorMessage.IsEmpty() ? TEXT("The job failed. Any credits it took were returned.") : G->ErrorMessage;
	Notify(Job);
	OnChanged.Broadcast();
	OnFinished.Broadcast(Job);
}

void FZettaJobs::ImportNow(int64 Id, const FZettaImportOptions& Options)
{
	FZettaJobPtr Job = Find(Id);
	if (!Job || Job->bImporting) return;
	Job->Import.Reset();
	Job->Error.Reset();
	Job->bAutoImport = true;
	Job->ImportOptions = Options;
	Completed(Job);
}

void FZettaJobs::Notify(const FZettaJobPtr& Job)
{
	const FZettaGenerationPtr G = Job->Generation;
	FText Text;
	SNotificationItem::ECompletionState State = SNotificationItem::CS_Success;
	if (G->IsSuccess())
	{
		Text = Job->Import.IsSet() && Job->Import->bOk
			? FText::Format(LOCTEXT("JobImported", "Zetta 3D: “{0}” is ready and imported."), FText::FromString(G->DisplayTitle()))
			: Job->Error.IsEmpty()
				? FText::Format(LOCTEXT("JobReady", "Zetta 3D: “{0}” is ready."), FText::FromString(G->DisplayTitle()))
				: FText::Format(LOCTEXT("JobImportFailed", "Zetta 3D: “{0}” is ready, but the import failed: {1}"), FText::FromString(G->DisplayTitle()), FText::FromString(Job->Error));
		if (!Job->Error.IsEmpty()) State = SNotificationItem::CS_Fail;
	}
	else if (G->Status == TEXT("cancelled"))
	{
		Text = FText::Format(LOCTEXT("JobCancelled", "Zetta 3D: “{0}” was cancelled."), FText::FromString(G->DisplayTitle()));
		State = SNotificationItem::CS_None;
	}
	else
	{
		Text = FText::Format(LOCTEXT("JobFailed", "Zetta 3D: “{0}” failed. {1}"), FText::FromString(G->DisplayTitle()), FText::FromString(Job->Error));
		State = SNotificationItem::CS_Fail;
	}
	FNotificationInfo Info(Text);
	Info.ExpireDuration = 6.f;
	Info.bUseLargeFont = false;
	if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info)) Item->SetCompletionState(State);
}

void FZettaJobs::Shutdown()
{
	if (TickHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	TickHandle.Reset();
	Jobs.Reset();
}

#undef LOCTEXT_NAMESPACE
