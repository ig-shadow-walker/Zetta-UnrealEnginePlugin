// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaProperties.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Misc/OutputDevice.h"
#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"
#include "ZettaTargets.h"
#include "ZettaToolCommon.h"

namespace ZettaPropsImpl
{
	/** Collects what a property's text importer complains about, instead of sending it to the log. */
	class FImportErrors : public FOutputDevice
	{
	public:
		FString Text;
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (!Text.IsEmpty()) Text += TEXT(" ");
			Text += V;
		}
	};

	FProperty* FindProperty(const UStruct* Struct, const FString& Name)
	{
		if (!Struct) return nullptr;
		if (FProperty* P = Struct->FindPropertyByName(FName(*Name))) return P;
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FString N = It->GetName();
			if (N.Equals(Name, ESearchCase::IgnoreCase)) return *It;
			// "SimulatePhysics" for "bSimulatePhysics", the Details label for the C++ name, a Blueprint's authored name.
			if (It->IsA<FBoolProperty>() && N.Len() > 1 && N[0] == TEXT('b') && N.RightChop(1).Equals(Name, ESearchCase::IgnoreCase)) return *It;
			if (It->GetAuthoredName().Equals(Name, ESearchCase::IgnoreCase)) return *It;
			if (It->GetDisplayNameText().ToString().Replace(TEXT(" "), TEXT("")).Equals(Name.Replace(TEXT(" "), TEXT("")), ESearchCase::IgnoreCase)) return *It;
		}
		return nullptr;
	}

	struct FSegment
	{
		FString Name;
		int32 Index = INDEX_NONE;
	};

	bool ParsePath(const FString& Path, TArray<FSegment>& Out, FString& OutError)
	{
		TArray<FString> Parts;
		Path.TrimStartAndEnd().ParseIntoArray(Parts, TEXT("."));
		if (Parts.Num() == 0)
		{
			OutError = TEXT("The property path is empty.");
			return false;
		}
		for (FString Part : Parts)
		{
			FSegment S;
			int32 Open;
			if (Part.FindChar(TEXT('['), Open) && Part.EndsWith(TEXT("]")))
			{
				const FString Index = Part.Mid(Open + 1, Part.Len() - Open - 2);
				if (!Index.IsNumeric())
				{
					OutError = FString::Printf(TEXT("\"%s\": an index is a number, as in \"Tags[0]\"."), *Part);
					return false;
				}
				S.Index = FCString::Atoi(*Index);
				Part = Part.Left(Open);
			}
			S.Name = Part.TrimStartAndEnd();
			Out.Add(S);
		}
		return true;
	}

	/** Where a path ends: the object that owns the value, the properties from that object down, and the value. */
	struct FResolved
	{
		UObject* Owner = nullptr;
		/** Declared properties from Owner's class to the leaf (array elements are not separate links). */
		TArray<FProperty*> Chain;
		/** The property describing the value (an array's inner property for "X[2]"). */
		FProperty* Leaf = nullptr;
		void* Value = nullptr;
	};

	bool IsSubobjectOf(const UObject* Sub, const UObject* Root)
	{
		if (!Sub || !Root) return false;
		if (Sub->IsIn(Root)) return true;
		const UActorComponent* Component = Cast<UActorComponent>(Sub);
		return Component && Component->GetOwner() == Root;
	}

	bool Resolve(UObject* Root, const FString& Path, FResolved& Out, FString& OutError)
	{
		TArray<FSegment> Segments;
		if (!ParsePath(Path, Segments, OutError)) return false;
		UObject* Owner = Root;
		const UStruct* Struct = Root->GetClass();
		void* Container = Root;
		TArray<FProperty*> Chain;
		for (int32 i = 0; i < Segments.Num(); ++i)
		{
			const FSegment& S = Segments[i];
			FProperty* P = FindProperty(Struct, S.Name);
			if (!P)
			{
				const FString Near = ZettaTargets::NearMatches(S.Name, ZettaProps::EditableNames(Struct));
				OutError = FString::Printf(TEXT("%s has no property \"%s\".%s"), *Struct->GetName(), *S.Name,
					Near.IsEmpty() ? TEXT(" Use inspect_actor / inspect_asset (all: true) or api_lookup to see its properties.") : *FString::Printf(TEXT(" Did you mean %s?"), *Near));
				return false;
			}
			Chain.Add(P);
			FProperty* Current = P;
			void* Value = nullptr;
			if (S.Index != INDEX_NONE)
			{
				if (FArrayProperty* AP = CastField<FArrayProperty>(P))
				{
					FScriptArrayHelper Helper(AP, AP->ContainerPtrToValuePtr<void>(Container));
					if (!Helper.IsValidIndex(S.Index))
					{
						OutError = FString::Printf(TEXT("%s has %d element(s); index %d is out of range. Set the whole array to add elements."), *P->GetName(), Helper.Num(), S.Index);
						return false;
					}
					Value = Helper.GetRawPtr(S.Index);
					Current = AP->Inner;
				}
				else if (P->ArrayDim > S.Index)
				{
					Value = P->ContainerPtrToValuePtr<void>(Container, S.Index);
				}
				else
				{
					OutError = FString::Printf(TEXT("%s is not an array, so \"[%d]\" does not apply."), *P->GetName(), S.Index);
					return false;
				}
			}
			else
			{
				Value = P->ContainerPtrToValuePtr<void>(Container);
			}

			if (i == Segments.Num() - 1)
			{
				Out.Owner = Owner;
				Out.Chain = Chain;
				Out.Leaf = Current;
				Out.Value = Value;
				return true;
			}

			if (FStructProperty* SP = CastField<FStructProperty>(Current))
			{
				Struct = SP->Struct;
				Container = Value;
				continue;
			}
			if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(Current))
			{
				UObject* Sub = OP->GetObjectPropertyValue(Value);
				if (!Sub)
				{
					OutError = FString::Printf(TEXT("%s is empty (None), so it has no \"%s\"."), *P->GetName(), *Segments[i + 1].Name);
					return false;
				}
				if (!IsSubobjectOf(Sub, Root))
				{
					OutError = FString::Printf(TEXT("%s points at %s, a separate object. Set its properties on that target directly."), *P->GetName(), *Sub->GetPathName());
					return false;
				}
				Owner = Sub;
				Struct = Sub->GetClass();
				Container = Sub;
				Chain.Reset();
				continue;
			}
			OutError = FString::Printf(TEXT("%s is a %s; it has no fields."), *P->GetName(), *ZettaProps::TypeName(Current));
			return false;
		}
		return false;
	}

	bool ParseColor(const FString& In, FLinearColor& Out)
	{
		FString S = In.TrimStartAndEnd();
		if (!S.StartsWith(TEXT("#"))) return false;
		S.RemoveFromStart(TEXT("#"));
		if (S.Len() != 6 && S.Len() != 8) return false;
		for (TCHAR C : S)
		{
			if (!FChar::IsHexDigit(C)) return false;
		}
		const FColor Srgb = FColor::FromHex(S);
		// FLinearColor(FColor) converts from sRGB.
		Out = FLinearColor(Srgb);
		if (S.Len() == 6) Out.A = 1.f;
		return true;
	}

	bool ColorFromJson(const TSharedPtr<FJsonValue>& V, FLinearColor& Out, bool& bOutSrgbAlready)
	{
		bOutSrgbAlready = false;
		if (V->Type == EJson::String) return ParseColor(V->AsString(), Out);
		TArray<double> N;
		if (V->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Item : V->AsArray())
			{
				const TOptional<double> D = ZettaTool::ValueNumber(Item);
				if (!D.IsSet()) return false;
				N.Add(D.GetValue());
			}
		}
		else if (V->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			for (const TCHAR* K : { TEXT("r"), TEXT("g"), TEXT("b"), TEXT("a") })
			{
				TOptional<double> D = ZettaTool::ValueNumber(ZettaTool::Get(O, K));
				if (!D.IsSet()) D = ZettaTool::ValueNumber(ZettaTool::Get(O, FString(K).ToUpper()));
				if (D.IsSet()) N.Add(D.GetValue());
				else if (FCString::Strcmp(K, TEXT("a")) != 0) return false;
			}
		}
		if (N.Num() < 3 || N.Num() > 4) return false;
		// Numbers are linear 0–1 (as Unreal shows them); 0–255 integers are read as sRGB bytes.
		const bool bBytes = N[0] > 1.0 || N[1] > 1.0 || N[2] > 1.0;
		if (bBytes)
		{
			Out = FLinearColor(FColor((uint8)FMath::Clamp(N[0], 0.0, 255.0), (uint8)FMath::Clamp(N[1], 0.0, 255.0), (uint8)FMath::Clamp(N[2], 0.0, 255.0),
				N.Num() > 3 ? (uint8)FMath::Clamp(N[3], 0.0, 255.0) : 255));
			bOutSrgbAlready = true;
		}
		else
		{
			Out = FLinearColor((float)N[0], (float)N[1], (float)N[2], N.Num() > 3 ? (float)N[3] : 1.f);
		}
		return true;
	}

	bool Numbers(const TSharedPtr<FJsonValue>& V, TArray<double>& Out)
	{
		if (V->Type != EJson::Array) return false;
		for (const TSharedPtr<FJsonValue>& Item : V->AsArray())
		{
			const TOptional<double> D = ZettaTool::ValueNumber(Item);
			if (!D.IsSet()) return false;
			Out.Add(D.GetValue());
		}
		return true;
	}

	/** Struct values with a JSON shorthand. Returns false when the struct has none (or the value does not fit it). */
	bool SetKnownStruct(const FStructProperty* SP, void* Ptr, const TSharedPtr<FJsonValue>& V, bool& bOutHandled, FString& OutError)
	{
		bOutHandled = true;
		const FName S = SP->Struct->GetFName();
		TArray<double> N;
		const bool bArray = Numbers(V, N);
		auto Need = [&](int32 Count, const TCHAR* Shape) -> bool
		{
			if (bArray && N.Num() == Count) return true;
			OutError = FString::Printf(TEXT("%s takes %s."), *SP->GetName(), Shape);
			return false;
		};
		if (S == NAME_Vector || S == NAME_Vector3f || S == TEXT("Vector3d"))
		{
			FVector Out;
			if (!ZettaTool::ValueVector(V, Out))
			{
				OutError = FString::Printf(TEXT("%s takes [x, y, z]."), *SP->GetName());
				return false;
			}
			if (S == NAME_Vector3f) *(FVector3f*)Ptr = FVector3f(Out);
			else *(FVector*)Ptr = Out;
			return true;
		}
		if (S == NAME_Rotator)
		{
			FVector Out;
			if (!ZettaTool::ValueVector(V, Out))
			{
				if (V->Type == EJson::Object)
				{
					const TSharedPtr<FJsonObject> O = V->AsObject();
					*(FRotator*)Ptr = FRotator(ZettaTool::Num(O, TEXT("pitch")).Get(0), ZettaTool::Num(O, TEXT("yaw")).Get(0), ZettaTool::Num(O, TEXT("roll")).Get(0));
					return true;
				}
				OutError = FString::Printf(TEXT("%s takes [pitch, yaw, roll] in degrees."), *SP->GetName());
				return false;
			}
			*(FRotator*)Ptr = FRotator(Out.X, Out.Y, Out.Z);
			return true;
		}
		if (S == NAME_LinearColor || S == NAME_Color)
		{
			FLinearColor C;
			bool bSrgb = false;
			if (!ColorFromJson(V, C, bSrgb))
			{
				if (V->Type == EJson::String && !V->AsString().StartsWith(TEXT("#")))
				{
					bOutHandled = false;
					return false;
				}
				OutError = FString::Printf(TEXT("%s takes a colour: \"#rrggbb\" / \"#rrggbbaa\" (sRGB) or [r, g, b(, a)] (0–1 linear)."), *SP->GetName());
				return false;
			}
			if (S == NAME_LinearColor) *(FLinearColor*)Ptr = C;
			else *(FColor*)Ptr = C.ToFColor(true);
			return true;
		}
		if (S == NAME_Vector2D || S == NAME_Vector2f)
		{
			if (!Need(2, TEXT("[x, y]"))) return false;
			if (S == NAME_Vector2f) *(FVector2f*)Ptr = FVector2f((float)N[0], (float)N[1]);
			else *(FVector2D*)Ptr = FVector2D(N[0], N[1]);
			return true;
		}
		if (S == NAME_Vector4 || S == NAME_Vector4f)
		{
			if (!Need(4, TEXT("[x, y, z, w]"))) return false;
			if (S == NAME_Vector4f) *(FVector4f*)Ptr = FVector4f((float)N[0], (float)N[1], (float)N[2], (float)N[3]);
			else *(FVector4*)Ptr = FVector4(N[0], N[1], N[2], N[3]);
			return true;
		}
		if (S == NAME_IntPoint)
		{
			if (!Need(2, TEXT("[x, y]"))) return false;
			*(FIntPoint*)Ptr = FIntPoint((int32)N[0], (int32)N[1]);
			return true;
		}
		if (S == NAME_IntVector)
		{
			if (!Need(3, TEXT("[x, y, z]"))) return false;
			*(FIntVector*)Ptr = FIntVector((int32)N[0], (int32)N[1], (int32)N[2]);
			return true;
		}
		if (S == NAME_Quat)
		{
			if (bArray && N.Num() == 3)
			{
				*(FQuat*)Ptr = FRotator(N[0], N[1], N[2]).Quaternion();
				return true;
			}
			if (!Need(4, TEXT("[x, y, z, w] or [pitch, yaw, roll]"))) return false;
			*(FQuat*)Ptr = FQuat(N[0], N[1], N[2], N[3]);
			return true;
		}
		if (S == NAME_Transform && V->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FTransform& T = *(FTransform*)Ptr;
			FVector L = T.GetLocation(), R = FVector(T.Rotator().Pitch, T.Rotator().Yaw, T.Rotator().Roll), Sc = T.GetScale3D();
			if (ZettaTool::Has(O, TEXT("location")) && !ZettaTool::ValueVector(ZettaTool::Get(O, TEXT("location")), L)) { OutError = TEXT("A transform's location is [x, y, z]."); return false; }
			if (ZettaTool::Has(O, TEXT("rotation")) && !ZettaTool::ValueVector(ZettaTool::Get(O, TEXT("rotation")), R)) { OutError = TEXT("A transform's rotation is [pitch, yaw, roll]."); return false; }
			if (ZettaTool::Has(O, TEXT("scale")) && !ZettaTool::ValueVector(ZettaTool::Get(O, TEXT("scale")), Sc)) { OutError = TEXT("A transform's scale is [x, y, z]."); return false; }
			T = FTransform(FRotator(R.X, R.Y, R.Z), L, Sc);
			return true;
		}
		bOutHandled = false;
		return false;
	}

	bool EnumValue(const UEnum* Enum, const TSharedPtr<FJsonValue>& V, int64& OutValue, FString& OutError)
	{
		const FString Name = ZettaTool::ValueText(V).TrimStartAndEnd();
		if (V->Type == EJson::Number || Name.IsNumeric())
		{
			OutValue = (int64)FCString::Atoi64(*Name);
			if (Enum->IsValidEnumValue(OutValue)) return true;
		}
		const int32 Count = Enum->NumEnums() - (Enum->ContainsExistingMax() ? 1 : 0);
		TArray<FString> Names;
		for (int32 i = 0; i < Count; ++i)
		{
			if (Enum->HasMetaData(TEXT("Hidden"), i)) continue;
			const FString Short = Enum->GetNameStringByIndex(i);
			FString Suffix = Short;
			int32 Underscore;
			if (Short.FindChar(TEXT('_'), Underscore)) Suffix = Short.RightChop(Underscore + 1);
			const FString Display = Enum->GetDisplayNameTextByIndex(i).ToString();
			if (Name.Equals(Short, ESearchCase::IgnoreCase) || Name.Equals(Enum->GetNameByIndex(i).ToString(), ESearchCase::IgnoreCase)
				|| Name.Equals(Display, ESearchCase::IgnoreCase) || Name.Equals(Suffix, ESearchCase::IgnoreCase)
				|| Name.Replace(TEXT(" "), TEXT("")).Equals(Display.Replace(TEXT(" "), TEXT("")), ESearchCase::IgnoreCase))
			{
				OutValue = Enum->GetValueByIndex(i);
				return true;
			}
			Names.Add(Short);
		}
		OutError = FString::Printf(TEXT("\"%s\" is not a %s. It takes: %s."), *Name, *Enum->GetName(), *FString::Join(Names, TEXT(", ")));
		return false;
	}

	/** The object a reference value names: { asset }, { actor }, { class }, or a bare path / target string. */
	bool ReferenceTarget(const FObjectPropertyBase* OP, const TSharedPtr<FJsonValue>& V, UObject*& OutObject, FString& OutError)
	{
		OutObject = nullptr;
		const UClass* Wanted = OP->PropertyClass;
		const bool bClassRef = OP->IsA<FClassProperty>() || OP->IsA<FSoftClassProperty>();
		FString AssetPath, ActorTarget, ClassPath;
		if (V->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			AssetPath = ZettaTool::Str(O, TEXT("asset"));
			ActorTarget = ZettaTool::Str(O, TEXT("actor"));
			ClassPath = ZettaTool::Str(O, TEXT("class"));
			if (AssetPath.IsEmpty() && ActorTarget.IsEmpty() && ClassPath.IsEmpty())
			{
				OutError = FString::Printf(TEXT("%s takes { \"asset\": \"/Game/…\" }, { \"actor\": target }, { \"class\": … } or null."), *OP->GetName());
				return false;
			}
		}
		else if (V->Type == EJson::String)
		{
			const FString S = V->AsString().TrimStartAndEnd();
			if (S.Equals(TEXT("None"), ESearchCase::IgnoreCase) || S.IsEmpty()) return true;
			if (bClassRef) ClassPath = S;
			else if (S.StartsWith(TEXT("/"))) AssetPath = S;
			else ActorTarget = S;
		}
		else
		{
			OutError = FString::Printf(TEXT("%s is a reference: use { \"asset\": \"/Game/…\" }, { \"actor\": target }, { \"class\": … } or null."), *OP->GetName());
			return false;
		}

		if (bClassRef)
		{
			if (ClassPath.IsEmpty()) ClassPath = AssetPath;
			const UClass* Meta = nullptr;
			if (const FClassProperty* CP = CastField<FClassProperty>(OP)) Meta = CP->MetaClass;
			if (const FSoftClassProperty* SCP = CastField<FSoftClassProperty>(OP)) Meta = SCP->MetaClass;
			UClass* Class = ZettaTargets::FindClass(ClassPath, Meta, OutError);
			if (!Class) return false;
			OutObject = Class;
			return true;
		}
		if (!ClassPath.IsEmpty() && AssetPath.IsEmpty() && ActorTarget.IsEmpty())
		{
			OutError = FString::Printf(TEXT("%s holds an object (%s), not a class."), *OP->GetName(), *Wanted->GetName());
			return false;
		}
		if (!ActorTarget.IsEmpty())
		{
			AActor* Actor = ZettaTargets::FindActor(ActorTarget, OutError);
			if (!Actor) return false;
			if (Actor->IsA(Wanted))
			{
				OutObject = Actor;
				return true;
			}
			if (Wanted->IsChildOf(UActorComponent::StaticClass()))
			{
				// An actor given for a component reference: its root when that fits, else its first component of the type.
				if (Actor->GetRootComponent() && Actor->GetRootComponent()->IsA(Wanted)) OutObject = Actor->GetRootComponent();
				else OutObject = Actor->FindComponentByClass(const_cast<UClass*>(Wanted));
				if (OutObject) return true;
			}
			OutError = FString::Printf(TEXT("%s takes a %s; %s is a %s."), *OP->GetName(), *Wanted->GetName(), *ZettaTargets::Describe(Actor), *Actor->GetClass()->GetName());
			return false;
		}
		UObject* Asset = ZettaTargets::LoadAsset(AssetPath, OutError);
		if (!Asset) return false;
		// A Blueprint asset given where its class or an actor class's default is meant.
		if (!Asset->IsA(Wanted))
		{
			OutError = FString::Printf(TEXT("%s takes a %s; %s is a %s."), *OP->GetName(), *Wanted->GetName(), *AssetPath, *Asset->GetClass()->GetName());
			return false;
		}
		OutObject = Asset;
		return true;
	}

	FString ExportRaw(const FProperty* P, const void* Ptr)
	{
		FString S;
		P->ExportText_Direct(S, Ptr, nullptr, nullptr, PPF_None);
		return S;
	}

	bool IsSimpleStruct(const UScriptStruct* S)
	{
		static const FName Simple[] = { NAME_Vector, NAME_Vector3f, NAME_Rotator, NAME_LinearColor, NAME_Color, NAME_Vector2D, NAME_Vector4, NAME_Quat,
			NAME_IntPoint, NAME_IntVector, NAME_Transform, FName(TEXT("Guid")), NAME_Vector2f, NAME_Vector4f };
		for (const FName& N : Simple)
		{
			if (S->GetFName() == N) return true;
		}
		return S->GetFName() == TEXT("SoftObjectPath") || S->GetFName() == TEXT("SoftClassPath") || S->GetFName() == TEXT("GameplayTag");
	}

	bool IsListed(const FProperty* P)
	{
		return P->HasAnyPropertyFlags(CPF_Edit) && !P->HasAnyPropertyFlags(CPF_Deprecated) && !P->HasMetaData(TEXT("HideInDetailPanel"));
	}

	void CollectLines(const UStruct* Struct, const void* Data, const void* Default, const FString& Prefix, bool bAll, int32 Depth, const UObject* Owner, TArray<FString>& Out, int32 Max)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (Out.Num() >= Max) return;
			const FProperty* P = *It;
			if (!IsListed(P)) continue;
			const void* Value = P->ContainerPtrToValuePtr<void>(Data);
			const void* DefaultValue = Default ? P->ContainerPtrToValuePtr<void>(Default) : nullptr;
			// Component and subobject pointers are listed with the components, not as values.
			if (const FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(P))
			{
				const UObject* Ref = OP->GetObjectPropertyValue(Value);
				if (Ref && Owner && (Ref->IsIn(Owner) || Ref->IsA<UActorComponent>()) && !bAll) continue;
				if (P->HasAnyPropertyFlags(CPF_InstancedReference | CPF_ContainsInstancedReference) && !bAll) continue;
			}
			const bool bSame = DefaultValue && P->Identical(Value, DefaultValue, PPF_None);
			if (!bAll && bSame) continue;
			const FString Name = Prefix + P->GetName();
			const FStructProperty* SP = CastField<FStructProperty>(P);
			if (SP && !IsSimpleStruct(SP->Struct) && Depth < 2 && P->ArrayDim == 1)
			{
				// Big structs (BodyInstance, PostProcessSettings): list the fields that changed.
				const int32 Before = Out.Num();
				CollectLines(SP->Struct, Value, DefaultValue, Name + TEXT("."), bAll, Depth + 1, Owner, Out, Max);
				if (Out.Num() > Before || bAll) continue;
			}
			Out.Add(FString::Printf(TEXT("%s = %s"), *Name, *ZettaProps::Text(P, Value)));
		}
	}
}

namespace ZettaProps
{
	TArray<FString> EditableNames(const UStruct* Struct)
	{
		TArray<FString> Out;
		if (!Struct) return Out;
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible)) Out.Add(It->GetName());
		}
		return Out;
	}

	FString TypeName(const FProperty* P)
	{
		if (!P) return TEXT("?");
		if (const FStructProperty* SP = CastField<FStructProperty>(P)) return SP->Struct->GetName();
		if (const FEnumProperty* EP = CastField<FEnumProperty>(P)) return EP->GetEnum() ? EP->GetEnum()->GetName() : TEXT("enum");
		if (const FByteProperty* BP = CastField<FByteProperty>(P))
		{
			if (BP->Enum) return BP->Enum->GetName();
		}
		if (const FClassProperty* CP = CastField<FClassProperty>(P)) return FString::Printf(TEXT("class<%s>"), CP->MetaClass ? *CP->MetaClass->GetName() : TEXT("Object"));
		if (const FSoftClassProperty* SCP = CastField<FSoftClassProperty>(P)) return FString::Printf(TEXT("soft class<%s>"), SCP->MetaClass ? *SCP->MetaClass->GetName() : TEXT("Object"));
		if (const FSoftObjectProperty* SOP = CastField<FSoftObjectProperty>(P)) return FString::Printf(TEXT("soft %s"), *SOP->PropertyClass->GetName());
		if (const FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(P)) return OP->PropertyClass->GetName();
		if (const FArrayProperty* AP = CastField<FArrayProperty>(P)) return FString::Printf(TEXT("array<%s>"), *TypeName(AP->Inner));
		if (const FSetProperty* SetP = CastField<FSetProperty>(P)) return FString::Printf(TEXT("set<%s>"), *TypeName(SetP->ElementProp));
		if (const FMapProperty* MP = CastField<FMapProperty>(P)) return FString::Printf(TEXT("map<%s, %s>"), *TypeName(MP->KeyProp), *TypeName(MP->ValueProp));
		if (P->IsA<FBoolProperty>()) return TEXT("bool");
		if (P->IsA<FFloatProperty>()) return TEXT("float");
		if (P->IsA<FDoubleProperty>()) return TEXT("double");
		if (P->IsA<FIntProperty>()) return TEXT("int");
		if (P->IsA<FInt64Property>()) return TEXT("int64");
		if (P->IsA<FStrProperty>()) return TEXT("string");
		if (P->IsA<FNameProperty>()) return TEXT("name");
		if (P->IsA<FTextProperty>()) return TEXT("text");
		return P->GetCPPType();
	}

	FString Text(const FProperty* P, const void* Ptr, int32 MaxLen)
	{
		if (!P || !Ptr) return TEXT("?");
		FString S;
		if (const FBoolProperty* B = CastField<FBoolProperty>(P))
		{
			S = B->GetPropertyValue(Ptr) ? TEXT("true") : TEXT("false");
		}
		else if (const FEnumProperty* EP = CastField<FEnumProperty>(P))
		{
			const int64 V = EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(Ptr);
			S = EP->GetEnum() ? EP->GetEnum()->GetNameStringByValue(V) : FString::Printf(TEXT("%lld"), V);
		}
		else if (const FNumericProperty* NP = CastField<FNumericProperty>(P))
		{
			if (UEnum* Enum = NP->GetIntPropertyEnum()) S = Enum->GetNameStringByValue((int64)NP->GetUnsignedIntPropertyValue(Ptr));
			else if (NP->IsFloatingPoint()) S = ZettaTool::Num(NP->GetFloatingPointPropertyValue(Ptr));
			else S = FString::Printf(TEXT("%lld"), NP->GetSignedIntPropertyValue(Ptr));
		}
		else if (const FStrProperty* SP = CastField<FStrProperty>(P))
		{
			S = ZettaTool::Quoted(SP->GetPropertyValue(Ptr));
		}
		else if (const FNameProperty* NameP = CastField<FNameProperty>(P))
		{
			S = ZettaTool::Quoted(NameP->GetPropertyValue(Ptr).ToString());
		}
		else if (const FTextProperty* TP = CastField<FTextProperty>(P))
		{
			S = ZettaTool::Quoted(TP->GetPropertyValue(Ptr).ToString());
		}
		else if (const FStructProperty* StructP = CastField<FStructProperty>(P))
		{
			const FName N = StructP->Struct->GetFName();
			if (N == NAME_Vector) S = ZettaTool::Vec(*(const FVector*)Ptr);
			else if (N == NAME_Vector3f) S = ZettaTool::Vec(FVector(*(const FVector3f*)Ptr));
			else if (N == NAME_Rotator) S = ZettaTool::Rot(*(const FRotator*)Ptr);
			else if (N == NAME_LinearColor)
			{
				const FLinearColor& C = *(const FLinearColor*)Ptr;
				S = FString::Printf(TEXT("\"#%s\" (linear %s, %s, %s, %s)"), *C.ToFColor(true).ToHex(), *ZettaTool::Num(C.R), *ZettaTool::Num(C.G), *ZettaTool::Num(C.B), *ZettaTool::Num(C.A));
			}
			else if (N == NAME_Color) S = FString::Printf(TEXT("\"#%s\""), *((const FColor*)Ptr)->ToHex());
			else if (N == NAME_Vector2D) S = FString::Printf(TEXT("[%s, %s]"), *ZettaTool::Num(((const FVector2D*)Ptr)->X), *ZettaTool::Num(((const FVector2D*)Ptr)->Y));
			else if (N == NAME_Transform)
			{
				const FTransform& T = *(const FTransform*)Ptr;
				S = FString::Printf(TEXT("{ location %s, rotation %s, scale %s }"), *ZettaTool::Vec(T.GetLocation()), *ZettaTool::Rot(T.Rotator()), *ZettaTool::Vec(T.GetScale3D()));
			}
			else S = ZettaPropsImpl::ExportRaw(P, Ptr);
		}
		else if (const FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(P))
		{
			const UObject* O = OP->GetObjectPropertyValue(Ptr);
			if (const FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(P))
			{
				const FSoftObjectPtr& Ref = *(const FSoftObjectPtr*)Ptr;
				S = Ref.IsNull() ? TEXT("None") : Ref.ToSoftObjectPath().ToString();
			}
			else if (!O) S = TEXT("None");
			else if (const AActor* A = Cast<AActor>(O)) S = FString::Printf(TEXT("actor \"%s\" (#%s)"), *A->GetActorLabel(), *A->GetName());
			else if (const UClass* C = Cast<UClass>(O)) S = C->GetPathName();
			else if (O->IsAsset()) S = ZettaTool::PackagePath(O->GetPathName());
			else S = FString::Printf(TEXT("%s (%s)"), *O->GetName(), *O->GetClass()->GetName());
		}
		else if (const FArrayProperty* AP = CastField<FArrayProperty>(P))
		{
			FScriptArrayHelper Helper(AP, Ptr);
			TArray<FString> Items;
			for (int32 i = 0; i < Helper.Num() && i < 12; ++i) Items.Add(Text(AP->Inner, Helper.GetRawPtr(i), 80));
			S = FString::Printf(TEXT("[%s%s]"), *FString::Join(Items, TEXT(", ")), Helper.Num() > 12 ? *FString::Printf(TEXT(", … %d in all"), Helper.Num()) : TEXT(""));
		}
		else
		{
			S = ZettaPropsImpl::ExportRaw(P, Ptr);
		}
		if (S.Len() > MaxLen) S = S.Left(MaxLen - 1) + TEXT("…");
		return S;
	}

	bool SetValue(FProperty* P, void* Ptr, UObject* Owner, const TSharedPtr<FJsonValue>& V, FString& OutError)
	{
		if (!V.IsValid() || V->IsNull())
		{
			if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(P))
			{
				OP->SetObjectPropertyValue(Ptr, nullptr);
				return true;
			}
			OutError = FString::Printf(TEXT("%s is a %s; null only clears a reference."), *P->GetName(), *TypeName(P));
			return false;
		}

		if (FBoolProperty* B = CastField<FBoolProperty>(P))
		{
			const FString S = ZettaTool::ValueText(V).ToLower();
			if (V->Type == EJson::Boolean) B->SetPropertyValue(Ptr, V->AsBool());
			else if (S == TEXT("true") || S == TEXT("1") || S == TEXT("yes") || S == TEXT("on")) B->SetPropertyValue(Ptr, true);
			else if (S == TEXT("false") || S == TEXT("0") || S == TEXT("no") || S == TEXT("off")) B->SetPropertyValue(Ptr, false);
			else
			{
				OutError = FString::Printf(TEXT("%s is a bool: true or false."), *P->GetName());
				return false;
			}
			return true;
		}
		if (FEnumProperty* EP = CastField<FEnumProperty>(P))
		{
			int64 Value;
			if (!EP->GetEnum() || !ZettaPropsImpl::EnumValue(EP->GetEnum(), V, Value, OutError)) return false;
			EP->GetUnderlyingProperty()->SetIntPropertyValue(Ptr, Value);
			return true;
		}
		if (FNumericProperty* NP = CastField<FNumericProperty>(P))
		{
			if (UEnum* Enum = NP->GetIntPropertyEnum())
			{
				int64 Value;
				if (!ZettaPropsImpl::EnumValue(Enum, V, Value, OutError)) return false;
				NP->SetIntPropertyValue(Ptr, Value);
				return true;
			}
			const TOptional<double> D = ZettaTool::ValueNumber(V);
			if (!D.IsSet())
			{
				OutError = FString::Printf(TEXT("%s is a %s: give a number."), *P->GetName(), *TypeName(P));
				return false;
			}
			if (NP->IsFloatingPoint()) NP->SetFloatingPointPropertyValue(Ptr, D.GetValue());
			else NP->SetIntPropertyValue(Ptr, (int64)FMath::RoundToDouble(D.GetValue()));
			return true;
		}
		if (FStrProperty* SP = CastField<FStrProperty>(P))
		{
			SP->SetPropertyValue(Ptr, ZettaTool::ValueText(V));
			return true;
		}
		if (FNameProperty* NameP = CastField<FNameProperty>(P))
		{
			NameP->SetPropertyValue(Ptr, FName(*ZettaTool::ValueText(V)));
			return true;
		}
		if (FTextProperty* TP = CastField<FTextProperty>(P))
		{
			TP->SetPropertyValue(Ptr, FText::FromString(ZettaTool::ValueText(V)));
			return true;
		}
		if (FStructProperty* StructP = CastField<FStructProperty>(P))
		{
			bool bHandled = false;
			if (ZettaPropsImpl::SetKnownStruct(StructP, Ptr, V, bHandled, OutError)) return true;
			if (bHandled) return false;
			if (V->Type == EJson::Object)
			{
				// Field by field, each checked against its own type.
				for (const auto& Field : V->AsObject()->Values)
				{
					const FString FieldName(Field.Key);
					FProperty* Inner = ZettaPropsImpl::FindProperty(StructP->Struct, FieldName);
					if (!Inner)
					{
						OutError = FString::Printf(TEXT("%s (%s) has no field \"%s\". Its fields: %s."), *P->GetName(), *StructP->Struct->GetName(), *FieldName,
							*FString::Join(EditableNames(StructP->Struct), TEXT(", ")));
						return false;
					}
					if (!SetValue(Inner, Inner->ContainerPtrToValuePtr<void>(Ptr), Owner, Field.Value, OutError)) return false;
				}
				return true;
			}
		}
		if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(P))
		{
			UObject* Target = nullptr;
			if (!ZettaPropsImpl::ReferenceTarget(OP, V, Target, OutError)) return false;
			OP->SetObjectPropertyValue(Ptr, Target);
			return true;
		}
		if (FArrayProperty* AP = CastField<FArrayProperty>(P))
		{
			if (V->Type == EJson::Array)
			{
				const TArray<TSharedPtr<FJsonValue>>& Items = V->AsArray();
				// Build into a scratch copy so a bad element leaves the array as it was.
				FScriptArrayHelper Helper(AP, Ptr);
				FString Before;
				AP->ExportText_Direct(Before, Ptr, nullptr, Owner, PPF_None);
				Helper.Resize(Items.Num());
				for (int32 i = 0; i < Items.Num(); ++i)
				{
					if (!SetValue(AP->Inner, Helper.GetRawPtr(i), Owner, Items[i], OutError))
					{
						OutError = FString::Printf(TEXT("%s[%d]: %s"), *P->GetName(), i, *OutError);
						AP->ImportText_Direct(*Before, Ptr, Owner, PPF_None, nullptr);
						return false;
					}
				}
				return true;
			}
		}

		// Anything else: the property's own Unreal text form.
		if (V->Type == EJson::Object || V->Type == EJson::Array)
		{
			FString Example;
			P->ExportText_Direct(Example, Ptr, nullptr, Owner, PPF_None);
			OutError = FString::Printf(TEXT("%s is a %s; write it in Unreal's text form, e.g. %s"), *P->GetName(), *TypeName(P), *ZettaTool::Short(Example, 200));
			return false;
		}
		ZettaPropsImpl::FImportErrors Errors;
		const FString Text = ZettaTool::ValueText(V);
		const TCHAR* End = P->ImportText_Direct(*Text, Ptr, Owner, PPF_None, &Errors);
		if (!End || !Errors.Text.IsEmpty())
		{
			FString Example;
			P->ExportText_Direct(Example, Ptr, nullptr, Owner, PPF_None);
			OutError = FString::Printf(TEXT("\"%s\" is not a valid %s for %s%s. Its current value reads %s"), *ZettaTool::Short(Text, 120), *TypeName(P), *P->GetName(),
				Errors.Text.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *ZettaTool::Short(Errors.Text, 200)), *ZettaTool::Short(Example, 200));
			return false;
		}
		return true;
	}

	bool Read(UObject* Root, const FString& Path, FString& OutText, FString& OutError)
	{
		ZettaPropsImpl::FResolved R;
		if (!ZettaPropsImpl::Resolve(Root, Path, R, OutError)) return false;
		OutText = Text(R.Leaf, R.Value);
		return true;
	}

	bool Write(UObject* Root, const FString& Path, const TSharedPtr<FJsonValue>& Value, FString& OutLine, FString& OutError)
	{
		if (!Root)
		{
			OutError = TEXT("Nothing to write to.");
			return false;
		}
		ZettaPropsImpl::FResolved R;
		if (!ZettaPropsImpl::Resolve(Root, Path, R, OutError)) return false;
		if (R.Chain.Num() > 0 && R.Chain[0]->HasAnyPropertyFlags(CPF_EditConst) && !R.Chain[0]->HasAnyPropertyFlags(CPF_Edit))
		{
			OutError = FString::Printf(TEXT("%s is read-only."), *Path);
			return false;
		}

		UObject* Owner = R.Owner;
		Owner->Modify();
		FEditPropertyChain Chain;
		for (FProperty* P : R.Chain) Chain.AddTail(P);
		Chain.SetActiveMemberPropertyNode(R.Chain[0]);
		Chain.SetActivePropertyNode(R.Chain.Last());
		Owner->PreEditChange(Chain);

		const bool bOk = SetValue(R.Leaf, R.Value, Owner, Value, OutError);

		FPropertyChangedEvent Event(R.Chain.Last(), EPropertyChangeType::ValueSet);
		Event.SetActiveMemberProperty(R.Chain[0]);
		FPropertyChangedChainEvent ChainEvent(Chain, Event);
		Owner->PostEditChangeChainProperty(ChainEvent);
		if (!bOk) return false;

		// Read the value back after the object had its say (clamps, rebuilt components).
		ZettaPropsImpl::FResolved After;
		FString Ignored;
		OutLine = ZettaPropsImpl::Resolve(Root, Path, After, Ignored)
			? FString::Printf(TEXT("%s = %s"), *Path, *Text(After.Leaf, After.Value))
			: FString::Printf(TEXT("%s set"), *Path);
		return true;
	}

	bool WriteOnActor(AActor* Actor, const FString& Path, const TSharedPtr<FJsonValue>& Value, FString& OutLine, FString& OutError)
	{
		TArray<FString> Parts;
		Path.ParseIntoArray(Parts, TEXT("."));
		const FString First = Parts.Num() > 0 ? Parts[0] : Path;
		int32 Bracket;
		FString FirstName = First;
		if (FirstName.FindChar(TEXT('['), Bracket)) FirstName = FirstName.Left(Bracket);

		if (ZettaPropsImpl::FindProperty(Actor->GetClass(), FirstName)) return Write(Actor, Path, Value, OutLine, OutError);

		// "LightComponent0.Intensity": a component named by the first part.
		if (Parts.Num() > 1)
		{
			if (UActorComponent* Component = ZettaTargets::FindComponent(Actor, FirstName))
			{
				const FString Rest = Path.RightChop(First.Len() + 1);
				if (!Write(Component, Rest, Value, OutLine, OutError)) return false;
				OutLine = Component->GetName() + TEXT(".") + OutLine;
				return true;
			}
		}

		// "Intensity" on a light actor: the root component first, then the others.
		TInlineComponentArray<UActorComponent*> Components(Actor);
		Components.Sort([Root = Actor->GetRootComponent()](const UActorComponent& A, const UActorComponent& B) { return &A == Root && &B != Root; });
		for (UActorComponent* Component : Components)
		{
			if (Component && ZettaPropsImpl::FindProperty(Component->GetClass(), FirstName))
			{
				if (!Write(Component, Path, Value, OutLine, OutError)) return false;
				OutLine = Component->GetName() + TEXT(".") + OutLine;
				return true;
			}
		}

		TArray<FString> Names = EditableNames(Actor->GetClass());
		for (const UActorComponent* Component : Components)
		{
			if (Component) Names.Append(EditableNames(Component->GetClass()));
		}
		const FString Near = ZettaTargets::NearMatches(FirstName, Names);
		OutError = FString::Printf(TEXT("%s has no property \"%s\" on the actor or its components.%s"), *ZettaTargets::Describe(Actor), *FirstName,
			Near.IsEmpty() ? TEXT(" Use inspect_actor with all: true to see what it has.") : *FString::Printf(TEXT(" Did you mean %s?"), *Near));
		return false;
	}

	bool WriteAll(UObject* Root, const TSharedPtr<FJsonObject>& Properties, TArray<FString>& OutLines, TArray<FString>& OutErrors)
	{
		if (!Properties.IsValid()) return true;
		AActor* Actor = Cast<AActor>(Root);
		for (const auto& Pair : Properties->Values)
		{
			const FString Key(Pair.Key);
			FString Line, Error;
			const bool bOk = Actor ? WriteOnActor(Actor, Key, Pair.Value, Line, Error) : Write(Root, Key, Pair.Value, Line, Error);
			if (bOk) OutLines.Add(Line);
			else OutErrors.Add(FString::Printf(TEXT("%s: %s"), *Key, *Error));
		}
		return OutErrors.Num() == 0;
	}

	bool WriteArchetype(UObject* Archetype, const TSharedPtr<FJsonObject>& Properties, TArray<FString>& OutLines, TArray<FString>& OutErrors)
	{
		if (!Properties.IsValid() || !Archetype) return true;
		TArray<UObject*> Instances;
		Archetype->GetArchetypeInstances(Instances);
		for (const auto& Pair : Properties->Values)
		{
			const FString Path(Pair.Key);
			// The old default, as text, to find the instances that never overrode it.
			FString Before;
			{
				ZettaPropsImpl::FResolved R;
				FString Ignored;
				if (ZettaPropsImpl::Resolve(Archetype, Path, R, Ignored)) R.Leaf->ExportText_Direct(Before, R.Value, nullptr, nullptr, PPF_None);
			}
			FString Line, Error;
			if (!Write(Archetype, Path, Pair.Value, Line, Error))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: %s"), *Path, *Error));
				continue;
			}
			OutLines.Add(Line);
			int32 Followed = 0;
			for (UObject* Instance : Instances)
			{
				if (!IsValid(Instance) || Instance->HasAnyFlags(RF_Transient)) continue;
				ZettaPropsImpl::FResolved R;
				FString Ignored;
				if (!ZettaPropsImpl::Resolve(Instance, Path, R, Ignored)) continue;
				FString Current;
				R.Leaf->ExportText_Direct(Current, R.Value, nullptr, nullptr, PPF_None);
				if (Current != Before) continue;
				FString InstanceLine;
				if (Write(Instance, Path, Pair.Value, InstanceLine, Ignored)) ++Followed;
			}
			if (Followed > 0) OutLines.Last() += FString::Printf(TEXT(" (and %d placed instance(s) that used the default)"), Followed);
		}
		return OutErrors.Num() == 0;
	}

	TArray<FString> Lines(UObject* Object, bool bAll, int32 Max)
	{
		TArray<FString> Out;
		if (!Object) return Out;
		const UObject* Archetype = Object->GetArchetype();
		ZettaPropsImpl::CollectLines(Object->GetClass(), Object, Archetype, FString(), bAll, 0, Object, Out, Max);
		if (Out.Num() >= Max) Out.Add(FString::Printf(TEXT("… (first %d shown)"), Max));
		return Out;
	}
}
