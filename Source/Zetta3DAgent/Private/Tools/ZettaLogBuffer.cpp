// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaLogBuffer.h"

#include "Misc/CoreDelegates.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeLock.h"

namespace ZettaLogBufferImpl
{
	constexpr int32 MaxLines = 3000;
	constexpr int32 MaxProblems = 500;
	constexpr int32 MaxMessage = 4000;

	/**
	 * Logged lines arrive from any thread. The device is allocated once and never freed: the log may still hold a
	 * pointer to it while modules shut down, so it is removed from the log at engine exit and otherwise left alone.
	 */
	class FDevice final : public FOutputDevice
	{
	public:
		FCriticalSection Lock;
		TArray<FZettaLogEntry> Lines;
		TArray<FZettaLogEntry> Problems;
		int32 LinesHead = 0;
		int32 ProblemsHead = 0;
		uint64 Sequence = 0;

		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			const ELogVerbosity::Type Level = (ELogVerbosity::Type)(Verbosity & ELogVerbosity::VerbosityMask);
			if (Level > ELogVerbosity::Log || !V) return; // Verbose and VeryVerbose are noise here.
			FZettaLogEntry Entry;
			Entry.Category = Category;
			Entry.Verbosity = Level;
			Entry.Message = FString(V).Left(MaxMessage);
			FScopeLock Guard(&Lock);
			Entry.Sequence = ++Sequence;
			Push(Lines, LinesHead, MaxLines, Entry);
			if (Level <= ELogVerbosity::Warning) Push(Problems, ProblemsHead, MaxProblems, Entry);
		}

		static void Push(TArray<FZettaLogEntry>& Ring, int32& Head, int32 Max, const FZettaLogEntry& Entry)
		{
			if (Ring.Num() < Max)
			{
				Ring.Add(Entry);
				return;
			}
			Ring[Head] = Entry;
			Head = (Head + 1) % Max;
		}

		/** Oldest first. */
		static TArray<FZettaLogEntry> Ordered(const TArray<FZettaLogEntry>& Ring, int32 Head)
		{
			TArray<FZettaLogEntry> Out;
			Out.Reserve(Ring.Num());
			for (int32 i = 0; i < Ring.Num(); ++i) Out.Add(Ring[(Head + i) % Ring.Num()]);
			return Out;
		}
	};

	FDevice* Device = nullptr;
}

namespace ZettaLogBuffer
{
	void Start()
	{
		using namespace ZettaLogBufferImpl;
		if (Device || !GLog) return;
		Device = new FDevice();
		GLog->AddOutputDevice(Device);
		FCoreDelegates::OnEnginePreExit.AddLambda([]()
		{
			if (GLog && ZettaLogBufferImpl::Device) GLog->RemoveOutputDevice(ZettaLogBufferImpl::Device);
		});
	}

	TArray<FZettaLogEntry> Recent(bool bErrors, bool bWarnings, bool bLogs, const FString& Category, int32 Limit)
	{
		using namespace ZettaLogBufferImpl;
		TArray<FZettaLogEntry> Out;
		if (!Device) return Out;
		TArray<FZettaLogEntry> Source;
		{
			FScopeLock Guard(&Device->Lock);
			// Only errors/warnings wanted: their own ring reaches further back.
			Source = bLogs ? FDevice::Ordered(Device->Lines, Device->LinesHead) : FDevice::Ordered(Device->Problems, Device->ProblemsHead);
		}
		for (int32 i = Source.Num() - 1; i >= 0 && Out.Num() < Limit; --i)
		{
			const FZettaLogEntry& E = Source[i];
			const bool bIsError = E.Verbosity <= ELogVerbosity::Error;
			const bool bIsWarning = E.Verbosity == ELogVerbosity::Warning;
			const bool bIsLog = !bIsError && !bIsWarning;
			if ((bIsError && !bErrors) || (bIsWarning && !bWarnings) || (bIsLog && !bLogs)) continue;
			if (!Category.IsEmpty() && !E.Category.ToString().Contains(Category)) continue;
			Out.Add(E);
		}
		return Out;
	}

	TArray<FZettaLogEntry> Since(uint64 Sequence)
	{
		using namespace ZettaLogBufferImpl;
		TArray<FZettaLogEntry> Out;
		if (!Device) return Out;
		FScopeLock Guard(&Device->Lock);
		for (const FZettaLogEntry& E : FDevice::Ordered(Device->Lines, Device->LinesHead))
		{
			if (E.Sequence > Sequence) Out.Add(E);
		}
		return Out;
	}

	uint64 LastSequence()
	{
		using namespace ZettaLogBufferImpl;
		if (!Device) return 0;
		FScopeLock Guard(&Device->Lock);
		return Device->Sequence;
	}
}
