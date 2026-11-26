// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaPipelineJson.h"

#include "ZettaJson.h"

namespace ZettaPJson
{
	FVal JGet(const FObj& O, const FString& Key)
	{
		if (!O.IsValid()) return nullptr;
		return O->TryGetField(Key);
	}

	bool JHas(const FObj& O, const FString& Key) { return O.IsValid() && O->HasField(Key); }

	bool JIsString(const FVal& V) { return V.IsValid() && V->Type == EJson::String; }
	bool JIsNumber(const FVal& V) { return V.IsValid() && V->Type == EJson::Number; }
	bool JIsBool(const FVal& V) { return V.IsValid() && V->Type == EJson::Boolean; }
	bool JIsObject(const FVal& V) { return V.IsValid() && V->Type == EJson::Object; }
	bool JIsArray(const FVal& V) { return V.IsValid() && V->Type == EJson::Array; }
	bool JIsNull(const FVal& V) { return !V.IsValid() || V->Type == EJson::Null || V->Type == EJson::None; }

	bool JTruthy(const FVal& V)
	{
		if (!V.IsValid()) return false;
		switch (V->Type)
		{
		case EJson::Boolean: return V->AsBool();
		case EJson::String: return !V->AsString().IsEmpty();
		case EJson::Number:
		{
			const double N = V->AsNumber();
			return N != 0 && !FMath::IsNaN(N);
		}
		case EJson::Null:
		case EJson::None: return false;
		default: return true;
		}
	}

	TOptional<FString> JText(const FObj& O, const FString& Key)
	{
		const FVal V = JGet(O, Key);
		if (JIsString(V)) return V->AsString();
		return {};
	}

	FString JTextOr(const FObj& O, const FString& Key, const FString& Fallback)
	{
		const TOptional<FString> T = JText(O, Key);
		return T.IsSet() ? T.GetValue() : Fallback;
	}

	bool JBoolOr(const FObj& O, const FString& Key, bool bFallback)
	{
		const FVal V = JGet(O, Key);
		if (JIsBool(V)) return V->AsBool();
		if (JIsNull(V)) return bFallback;
		return JTruthy(V);
	}

	TOptional<double> JNumber(const FObj& O, const FString& Key)
	{
		const FVal V = JGet(O, Key);
		if (JIsNumber(V)) return V->AsNumber();
		return {};
	}

	FObj JObjectOf(const FVal& V) { return JIsObject(V) ? V->AsObject() : nullptr; }
	FObj JObject(const FObj& O, const FString& Key) { return JObjectOf(JGet(O, Key)); }

	TArray<FVal> JArrayOf(const FVal& V)
	{
		if (!JIsArray(V)) return {};
		return V->AsArray();
	}

	TArray<FString> JStringsOf(const FVal& V)
	{
		TArray<FString> Out;
		for (const FVal& Item : JArrayOf(V)) Out.Add(JAsText(Item));
		return Out;
	}

	FString JAsText(const FVal& V)
	{
		if (!V.IsValid()) return FString();
		switch (V->Type)
		{
		case EJson::String: return V->AsString();
		case EJson::Boolean: return V->AsBool() ? TEXT("true") : TEXT("false");
		case EJson::Number:
		{
			const double N = V->AsNumber();
			if (FMath::IsFinite(N) && FMath::Abs(N) < 9.0e15 && FMath::FloorToDouble(N) == N) return FString::Printf(TEXT("%lld"), (int64)N);
			return FString::Printf(TEXT("%.15g"), N);
		}
		case EJson::Null:
		case EJson::None: return FString();
		default: return JWriteValue(V);
		}
	}

	FVal JClone(const FVal& V)
	{
		if (!V.IsValid()) return nullptr;
		switch (V->Type)
		{
		case EJson::Object: return MakeShared<FJsonValueObject>(JClone(V->AsObject()));
		case EJson::Array:
		{
			TArray<FVal> Items;
			for (const FVal& Item : V->AsArray()) Items.Add(JClone(Item));
			return MakeShared<FJsonValueArray>(Items);
		}
		case EJson::String: return MakeShared<FJsonValueString>(V->AsString());
		case EJson::Number: return MakeShared<FJsonValueNumber>(V->AsNumber());
		case EJson::Boolean: return MakeShared<FJsonValueBoolean>(V->AsBool());
		default: return MakeShared<FJsonValueNull>();
		}
	}

	TSharedRef<FJsonObject> JClone(const FObj& O)
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		if (!O.IsValid()) return Out;
		for (const TPair<FString, FVal>& Pair : O->Values) Out->SetField(Pair.Key, Pair.Value.IsValid() ? JClone(Pair.Value) : JNull());
		return Out;
	}

	bool JEquals(const FObj& A, const FObj& B)
	{
		if (!A.IsValid() || !B.IsValid()) return A.IsValid() == B.IsValid();
		if (A->Values.Num() != B->Values.Num()) return false;
		for (const TPair<FString, FVal>& Pair : A->Values)
		{
			if (!B->HasField(Pair.Key) || !JEquals(Pair.Value, B->TryGetField(Pair.Key))) return false;
		}
		return true;
	}

	bool JEquals(const FVal& A, const FVal& B)
	{
		if (JIsNull(A) || JIsNull(B)) return JIsNull(A) && JIsNull(B) && A.IsValid() == B.IsValid();
		if (A->Type != B->Type) return false;
		switch (A->Type)
		{
		case EJson::String: return A->AsString() == B->AsString();
		case EJson::Number: return A->AsNumber() == B->AsNumber();
		case EJson::Boolean: return A->AsBool() == B->AsBool();
		case EJson::Object: return JEquals(A->AsObject(), B->AsObject());
		case EJson::Array:
		{
			const TArray<FVal>& X = A->AsArray();
			const TArray<FVal>& Y = B->AsArray();
			if (X.Num() != Y.Num()) return false;
			for (int32 I = 0; I < X.Num(); ++I) if (!JEquals(X[I], Y[I])) return false;
			return true;
		}
		default: return true;
		}
	}

	FVal JStr(const FString& Value) { return MakeShared<FJsonValueString>(Value); }
	FVal JNum(double Value) { return MakeShared<FJsonValueNumber>(Value); }
	FVal JBool(bool bValue) { return MakeShared<FJsonValueBoolean>(bValue); }
	FVal JNull() { return MakeShared<FJsonValueNull>(); }

	FVal JStrings(const TArray<FString>& Values)
	{
		TArray<FVal> Items;
		for (const FString& S : Values) Items.Add(JStr(S));
		return MakeShared<FJsonValueArray>(Items);
	}

	FVal JObj(const TSharedRef<FJsonObject>& Value) { return MakeShared<FJsonValueObject>(Value); }

	void JSet(const TSharedRef<FJsonObject>& O, const FString& Key, const FVal& Value)
	{
		O->SetField(Key, Value.IsValid() ? JClone(Value) : JNull());
	}

	void JMerge(const TSharedRef<FJsonObject>& O, const FObj& Patch)
	{
		if (!Patch.IsValid()) return;
		for (const TPair<FString, FVal>& Pair : Patch->Values) JSet(O, Pair.Key, Pair.Value);
	}

	FString JWrite(const FObj& O) { return ZettaJson::Write(O); }
	FString JWriteValue(const FVal& V) { return ZettaJson::WriteValue(V); }
	FObj JParse(const FString& Text) { return ZettaJson::Parse(Text); }

	FString JCount(int64 Value)
	{
		const bool bNegative = Value < 0;
		FString Digits = FString::Printf(TEXT("%lld"), bNegative ? -Value : Value);
		FString Out;
		const int32 Len = Digits.Len();
		for (int32 I = 0; I < Len; ++I)
		{
			if (I > 0 && (Len - I) % 3 == 0) Out.AppendChar(TEXT(','));
			Out.AppendChar(Digits[I]);
		}
		return bNegative ? TEXT("-") + Out : Out;
	}
}
