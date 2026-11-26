// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

/** What the compile tool knows between calls, shared with the file tools and the snapshot. */
namespace ZettaCompile
{
	struct FError
	{
		FString File;
		int32 Line = 0;
		FString Message;
	};

	/** A file the agent wrote: new C++ files and reflected-header edits are worth a restart note. */
	void NoteWrite(const FString& RelativePath, bool bNewFile, const FString& Content);
	/** The last compile's errors (≤20), for the snapshot. */
	TArray<TSharedPtr<FJsonValue>> LastErrorsJson();
	/** Live Coding can build this project's C++ (Windows, enabled). */
	bool LiveCodingAvailable();
	/** The project's own C++ modules (from the .uproject). */
	TArray<FString> ProjectModules();
	/** Compiler errors read from build output (MSVC and clang forms), project-relative paths. */
	TArray<FError> ParseErrors(const TArray<FString>& Lines);
}
