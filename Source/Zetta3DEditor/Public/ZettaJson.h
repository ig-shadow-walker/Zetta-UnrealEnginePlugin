// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/** Small, forgiving readers for the API's JSON: a missing or mistyped field is the default, never a crash. */
namespace ZettaJson
{
	ZETTA3DEDITOR_API TSharedPtr<FJsonValue> ParseValue(const FString& Text);
	ZETTA3DEDITOR_API TSharedPtr<FJsonObject> Parse(const FString& Text);
	ZETTA3DEDITOR_API FString Write(const TSharedPtr<FJsonObject>& Object, bool bPretty = false);
	ZETTA3DEDITOR_API FString WriteValue(const TSharedPtr<FJsonValue>& Value, bool bPretty = false);

	ZETTA3DEDITOR_API FString Str(const TSharedPtr<FJsonObject>& O, const FString& Field, const FString& Default = FString());
	ZETTA3DEDITOR_API int64 Int(const TSharedPtr<FJsonObject>& O, const FString& Field, int64 Default = 0);
	ZETTA3DEDITOR_API double Num(const TSharedPtr<FJsonObject>& O, const FString& Field, double Default = 0);
	ZETTA3DEDITOR_API bool Bool(const TSharedPtr<FJsonObject>& O, const FString& Field, bool Default = false);
	ZETTA3DEDITOR_API TOptional<int64> OptInt(const TSharedPtr<FJsonObject>& O, const FString& Field);
	ZETTA3DEDITOR_API TOptional<bool> OptBool(const TSharedPtr<FJsonObject>& O, const FString& Field);
	ZETTA3DEDITOR_API bool Has(const TSharedPtr<FJsonObject>& O, const FString& Field);
	ZETTA3DEDITOR_API TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O, const FString& Field);
	ZETTA3DEDITOR_API TArray<TSharedPtr<FJsonValue>> Arr(const TSharedPtr<FJsonObject>& O, const FString& Field);
	ZETTA3DEDITOR_API TArray<TSharedPtr<FJsonObject>> Objects(const TSharedPtr<FJsonObject>& O, const FString& Field);
	ZETTA3DEDITOR_API TArray<FString> Strings(const TSharedPtr<FJsonObject>& O, const FString& Field);

	/** A value as text, whatever its type (numbers without a trailing .0 when whole). */
	ZETTA3DEDITOR_API FString AsText(const TSharedPtr<FJsonValue>& V);

	ZETTA3DEDITOR_API TSharedRef<FJsonValue> StringArray(const TArray<FString>& Values);
	ZETTA3DEDITOR_API TSharedRef<FJsonValue> NumberArray(const TArray<double>& Values);
	ZETTA3DEDITOR_API TSharedRef<FJsonObject> Clone(const TSharedPtr<FJsonObject>& O);
}
