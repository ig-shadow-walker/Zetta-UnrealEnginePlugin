// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "ZettaAgentTools.h"

class UWorld;

/**
 * Shared pieces of the agent's Unreal tools: forgiving input readers (the model writes JSON by hand, so a number
 * may arrive as a string), result shaping (≤100,000 characters, errors one actionable line), project paths and
 * the registration helpers every area file uses.
 */
namespace ZettaTool
{
	inline constexpr int32 MaxResultChars = 100000;

	// ─── Input ───────────────────────────────────────────────────────────────

	/** The key is present and not null. */
	bool Has(const TSharedPtr<FJsonObject>& In, const FString& Key);
	TSharedPtr<FJsonValue> Get(const TSharedPtr<FJsonObject>& In, const FString& Key);
	/** A string (numbers and booleans as text); Default when absent. */
	FString Str(const TSharedPtr<FJsonObject>& In, const FString& Key, const FString& Default = FString());
	TOptional<double> Num(const TSharedPtr<FJsonObject>& In, const FString& Key);
	TOptional<int64> Int(const TSharedPtr<FJsonObject>& In, const FString& Key);
	/** An integer clamped to [Min, Max]; Default when absent or unreadable. */
	int32 IntIn(const TSharedPtr<FJsonObject>& In, const FString& Key, int32 Default, int32 Min, int32 Max);
	TOptional<bool> Bool(const TSharedPtr<FJsonObject>& In, const FString& Key);
	bool BoolOr(const TSharedPtr<FJsonObject>& In, const FString& Key, bool Default);
	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& In, const FString& Key);
	TArray<TSharedPtr<FJsonValue>> Arr(const TSharedPtr<FJsonObject>& In, const FString& Key);
	/** A list of strings; a single string counts as a list of one. */
	TArray<FString> Strings(const TSharedPtr<FJsonObject>& In, const FString& Key);

	/** A JSON value as a string (numbers without a trailing .0, booleans as true/false). */
	FString ValueText(const TSharedPtr<FJsonValue>& V);
	TOptional<double> ValueNumber(const TSharedPtr<FJsonValue>& V);
	/** [x, y, z] (or a numeric string list "1, 2, 3"). */
	bool ValueVector(const TSharedPtr<FJsonValue>& V, FVector& Out);
	/**
	 * Reads an optional vector field. Returns false (with OutError set) when it is present but not [x, y, z];
	 * OutValue stays unset when it is absent.
	 */
	bool OptVector(const TSharedPtr<FJsonObject>& In, const FString& Key, TOptional<FVector>& OutValue, FString& OutError);

	// ─── Results ─────────────────────────────────────────────────────────────

	FZettaToolResult Ok(const FString& Text, const FString& Summary = FString());
	FZettaToolResult Fail(const FString& Text);
	/** Cuts text to Max characters with a note saying how much was left out. */
	FString Clip(const FString& Text, int32 Max = MaxResultChars);
	/** One line, at most Max characters. */
	FString Short(const FString& Text, int32 Max);
	FString Plural(int64 N, const TCHAR* One, const TCHAR* Many);
	FString Num(double V);
	FString Vec(const FVector& V);
	FString Rot(const FRotator& R);
	FString Quoted(const FString& S);
	FString JoinQuoted(const TArray<FString>& Items, int32 Max = 10);

	FZettaToolPreview Preview(const FString& Title, const FString& Body = FString());
	FZettaToolPreview PreviewError(const FString& Title, const FString& Error);

	// ─── Editor ──────────────────────────────────────────────────────────────

	/** The level open in the editor (never the Play In Editor world). */
	UWorld* EditorWorld();
	/** The Play In Editor world, when a session runs. */
	UWorld* PlayWorld();
	bool IsPlaying();
	/** "Zetta Agent: <what>" — every undo step the agent makes is named this way. */
	FString TransactionTitle(const FString& What);
	inline const TCHAR* TransactionPrefix = TEXT("Zetta Agent: ");

	// ─── Files ───────────────────────────────────────────────────────────────

	/** The project folder, absolute, with a trailing slash. */
	FString ProjectDir();
	/** This plugin's folder, absolute, with a trailing slash. Never written by the agent. */
	FString PluginDir();
	/** Where pictures and files the user attached to a message are kept, outside the content. */
	FString AttachedDir();
	/** "attached:<name>" → the file, or empty when it is gone. */
	FString AttachedFile(const FString& Ref);
	/** An absolute path inside the project as the project-relative path ("Source/Game/Ship.cpp"). */
	FString ProjectRelative(const FString& Absolute);
	bool IsUnder(const FString& Absolute, const FString& Folder);

	/**
	 * Resolves a project-relative (or absolute, inside the project) path. Reads may go anywhere in the project;
	 * writes only to Source/, Config/, Content/Python/ and a project plugin's Source/ — never this plugin.
	 */
	bool ProjectFile(const FString& Path, bool bWrite, FString& OutAbsolute, FString& OutRelative, FString& OutError);
	/** A file the user attached, or one under the project folder (import_file, picture sources). */
	bool ReadableFile(const FString& Path, FString& OutAbsolute, FString& OutError);

	// ─── Content paths ───────────────────────────────────────────────────────

	/** "/Game/Props/SM_Chair" from "/Game/Props/SM_Chair.SM_Chair" or "SM_Chair.SM_Chair" forms. */
	FString PackagePath(const FString& Path);
	/** "/Game/Props/SM_Chair.SM_Chair". */
	FString ObjectPath(const FString& Path);
	/** A writable content path ("/Game/…"): refuses /Engine, /Script and this plugin's content. */
	bool WritableContentPath(const FString& Path, FString& OutPackage, FString& OutError);
}

/** The area files' registration entry points (one per file), called from ZettaRegisterAgentTools. */
namespace ZettaToolAreas
{
	void RegisterLevelTools(FZettaAgentTools& Tools);
	void RegisterInspectTools(FZettaAgentTools& Tools);
	void RegisterBlueprintTools(FZettaAgentTools& Tools);
	void RegisterAssetTools(FZettaAgentTools& Tools);
	void RegisterMaterialTools(FZettaAgentTools& Tools);
	void RegisterFileTools(FZettaAgentTools& Tools);
	void RegisterCompileTools(FZettaAgentTools& Tools);
	void RegisterCaptureTools(FZettaAgentTools& Tools);
	void RegisterPythonTools(FZettaAgentTools& Tools);
	void RegisterProjectTools(FZettaAgentTools& Tools);
	void RegisterZettaTools(FZettaAgentTools& Tools);
	void RegisterImageTools(FZettaAgentTools& Tools);
	void RegisterPipelineTools(FZettaAgentTools& Tools);
}
