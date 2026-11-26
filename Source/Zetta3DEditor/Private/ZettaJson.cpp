// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaJson.h"

#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace ZettaJson
{
	TSharedPtr<FJsonValue> ParseValue(const FString& Text)
	{
		TSharedPtr<FJsonValue> Value;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Value)) return nullptr;
		return Value;
	}

	TSharedPtr<FJsonObject> Parse(const FString& Text)
	{
		const TSharedPtr<FJsonValue> Value = ParseValue(Text);
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
	}

	FString Write(const TSharedPtr<FJsonObject>& Object, bool bPretty)
	{
		if (!Object.IsValid()) return TEXT("{}");
		FString Out;
		if (bPretty)
		{
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
			FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		}
		else
		{
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
			FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		}
		return Out;
	}

	FString WriteValue(const TSharedPtr<FJsonValue>& Value, bool bPretty)
	{
		if (!Value.IsValid()) return TEXT("null");
		if (Value->Type == EJson::Object) return Write(Value->AsObject(), bPretty);
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Value.ToSharedRef(), FString(), Writer);
		return Out;
	}

	static TSharedPtr<FJsonValue> Field(const TSharedPtr<FJsonObject>& O, const FString& Name)
	{
		if (!O.IsValid()) return nullptr;
		const TSharedPtr<FJsonValue> V = O->TryGetField(Name);
		return V.IsValid() && V->Type != EJson::Null ? V : nullptr;
	}

	FString Str(const TSharedPtr<FJsonObject>& O, const FString& Name, const FString& Default)
	{
		const TSharedPtr<FJsonValue> V = Field(O, Name);
		if (!V.IsValid()) return Default;
		if (V->Type == EJson::String) return V->AsString();
		if (V->Type == EJson::Number || V->Type == EJson::Boolean) return AsText(V);
		return Default;
	}

	int64 Int(const TSharedPtr<FJsonObject>& O, const FString& Name, int64 Default)
	{
		const TOptional<int64> V = OptInt(O, Name);
		return V.IsSet() ? V.GetValue() : Default;
	}

	TOptional<int64> OptInt(const TSharedPtr<FJsonObject>& O, const FString& Name)
	{
		const TSharedPtr<FJsonValue> V = Field(O, Name);
		if (!V.IsValid()) return {};
		if (V->Type == EJson::Number) return (int64)V->AsNumber();
		if (V->Type == EJson::String)
		{
			const FString S = V->AsString();
			if (S.IsNumeric()) return FCString::Atoi64(*S);
		}
		return {};
	}

	double Num(const TSharedPtr<FJsonObject>& O, const FString& Name, double Default)
	{
		const TSharedPtr<FJsonValue> V = Field(O, Name);
		if (!V.IsValid()) return Default;
		if (V->Type == EJson::Number) return V->AsNumber();
		if (V->Type == EJson::String && V->AsString().IsNumeric()) return FCString::Atod(*V->AsString());
		return Default;
	}

	bool Bool(const TSharedPtr<FJsonObject>& O, const FString& Name, bool Default)
	{
		const TOptional<bool> V = OptBool(O, Name);
		return V.IsSet() ? V.GetValue() : Default;
	}

	TOptional<bool> OptBool(const TSharedPtr<FJsonObject>& O, const FString& Name)
	{
		const TSharedPtr<FJsonValue> V = Field(O, Name);
		if (!V.IsValid() || V->Type != EJson::Boolean) return {};
		return V->AsBool();
	}

	bool Has(const TSharedPtr<FJsonObject>& O, const FString& Name)
	{
		return Field(O, Name).IsValid();
	}

	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O, const FString& Name)
	{
		const TSharedPtr<FJsonValue> V = Field(O, Name);
		return V.IsValid() && V->Type == EJson::Object ? V->AsObject() : nullptr;
	}

	TArray<TSharedPtr<FJsonValue>> Arr(const TSharedPtr<FJsonObject>& O, const FString& Name)
	{
		const TSharedPtr<FJsonValue> V = Field(O, Name);
		return V.IsValid() && V->Type == EJson::Array ? V->AsArray() : TArray<TSharedPtr<FJsonValue>>();
	}

	TArray<TSharedPtr<FJsonObject>> Objects(const TSharedPtr<FJsonObject>& O, const FString& Name)
	{
		TArray<TSharedPtr<FJsonObject>> Out;
		for (const TSharedPtr<FJsonValue>& V : Arr(O, Name))
		{
			if (V.IsValid() && V->Type == EJson::Object) Out.Add(V->AsObject());
		}
		return Out;
	}

	TArray<FString> Strings(const TSharedPtr<FJsonObject>& O, const FString& Name)
	{
		TArray<FString> Out;
		for (const TSharedPtr<FJsonValue>& V : Arr(O, Name))
		{
			if (V.IsValid() && V->Type == EJson::String) Out.Add(V->AsString());
		}
		return Out;
	}

	FString AsText(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid()) return FString();
		switch (V->Type)
		{
		case EJson::String: return V->AsString();
		case EJson::Boolean: return V->AsBool() ? TEXT("true") : TEXT("false");
		case EJson::Number:
		{
			const double D = V->AsNumber();
			if (FMath::IsNearlyEqual(D, FMath::RoundToDouble(D)) && FMath::Abs(D) < 9.0e15) return FString::Printf(TEXT("%lld"), (int64)FMath::RoundToDouble(D));
			return FString::SanitizeFloat(D);
		}
		case EJson::Null: return TEXT("null");
		default: return WriteValue(V);
		}
	}

	TSharedRef<FJsonValue> StringArray(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& S : Values) Out.Add(MakeShared<FJsonValueString>(S));
		return MakeShared<FJsonValueArray>(Out);
	}

	TSharedRef<FJsonValue> NumberArray(const TArray<double>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const double D : Values) Out.Add(MakeShared<FJsonValueNumber>(D));
		return MakeShared<FJsonValueArray>(Out);
	}

	TSharedRef<FJsonObject> Clone(const TSharedPtr<FJsonObject>& O)
	{
		const TSharedPtr<FJsonObject> Copy = O.IsValid() ? Parse(Write(O)) : nullptr;
		return Copy.IsValid() ? Copy.ToSharedRef() : MakeShared<FJsonObject>();
	}
}
