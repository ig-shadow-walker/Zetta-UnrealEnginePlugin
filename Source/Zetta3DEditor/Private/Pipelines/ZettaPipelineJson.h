// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * JSON with the web's semantics, for the pipeline code: a graph's settings are web-shaped values, read the way the
 * web editor reads them (JavaScript truthiness, "missing" and "null" told apart when it matters). Every reader is
 * forgiving — a missing or mistyped field is a default, never a crash.
 */
namespace ZettaPJson
{
	using FObj = TSharedPtr<FJsonObject>;
	using FVal = TSharedPtr<FJsonValue>;

	/** The field's value, or null when the field is missing. A JSON null comes back as a value of type Null. */
	FVal JGet(const FObj& O, const FString& Key);
	/** The field is there (a JSON null counts). */
	bool JHas(const FObj& O, const FString& Key);

	bool JIsString(const FVal& V);
	bool JIsNumber(const FVal& V);
	bool JIsBool(const FVal& V);
	bool JIsObject(const FVal& V);
	bool JIsArray(const FVal& V);
	/** Missing, or a JSON null. */
	bool JIsNull(const FVal& V);
	/** JavaScript truthiness: false, "", 0, NaN and null are false; objects and arrays are true. */
	bool JTruthy(const FVal& V);

	/** A string field, or unset when the field is missing or not a string. */
	TOptional<FString> JText(const FObj& O, const FString& Key);
	FString JTextOr(const FObj& O, const FString& Key, const FString& Fallback = FString());
	bool JBoolOr(const FObj& O, const FString& Key, bool bFallback);
	TOptional<double> JNumber(const FObj& O, const FString& Key);
	FObj JObjectOf(const FVal& V);
	FObj JObject(const FObj& O, const FString& Key);
	TArray<FVal> JArrayOf(const FVal& V);
	TArray<FString> JStringsOf(const FVal& V);

	/** A value as text, whatever its type (numbers without a trailing .0 when whole; objects as JSON). */
	FString JAsText(const FVal& V);

	FVal JClone(const FVal& V);
	/** A deep copy; an empty object for null. */
	TSharedRef<FJsonObject> JClone(const FObj& O);
	/** Deep equality, ignoring the order of an object's fields. Missing and null differ. */
	bool JEquals(const FVal& A, const FVal& B);
	bool JEquals(const FObj& A, const FObj& B);

	FVal JStr(const FString& Value);
	FVal JNum(double Value);
	FVal JBool(bool bValue);
	FVal JNull();
	FVal JStrings(const TArray<FString>& Values);
	FVal JObj(const TSharedRef<FJsonObject>& Value);

	/** Sets a field to a copy of a value; a null value pointer stores a JSON null. */
	void JSet(const TSharedRef<FJsonObject>& O, const FString& Key, const FVal& Value);
	/** Copies every field of Patch over O (deep). */
	void JMerge(const TSharedRef<FJsonObject>& O, const FObj& Patch);

	FString JWrite(const FObj& O);
	FString JWriteValue(const FVal& V);
	FObj JParse(const FString& Text);
	/** "1,480,000" — the web's en-US toLocaleString. */
	FString JCount(int64 Value);
}
