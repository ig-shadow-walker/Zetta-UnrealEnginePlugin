// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"

/** One line the editor logged. */
struct FZettaLogEntry
{
	FName Category;
	ELogVerbosity::Type Verbosity = ELogVerbosity::Log;
	FString Message;
	/** Increases by one per line, so a caller can ask for "what came after N". */
	uint64 Sequence = 0;
};

/**
 * The Output Log as the agent sees it: a ring buffer of recent lines, registered on the log as soon as the tools
 * are, so errors from before the first chat are there too. Errors and warnings are kept in their own ring so a
 * chatty log can't push them out.
 */
namespace ZettaLogBuffer
{
	void Start();
	/** Most recent first. Verbosities: Error, Warning, Log (Display and Log both count as log). */
	TArray<FZettaLogEntry> Recent(bool bErrors, bool bWarnings, bool bLogs, const FString& Category, int32 Limit);
	/** Every line after Sequence (oldest first), for the compile tool. */
	TArray<FZettaLogEntry> Since(uint64 Sequence);
	uint64 LastSequence();
}
