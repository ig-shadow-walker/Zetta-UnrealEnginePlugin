// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class AActor;
class FProperty;
class UObject;
class UStruct;

/**
 * Reading and writing reflected properties for the agent. Paths follow structs, array elements ("Tags[0]") and
 * instanced subobjects ("LightComponent.Intensity", "BodyInstance.bSimulatePhysics"). Values are the
 * protocol's JSON forms (numbers, booleans, enum names, [x, y, z], "#rrggbb" sRGB colours, { asset }, { actor },
 * { class }, null) or, for anything else, the property's own Unreal text form, imported with its own parser.
 *
 * A write goes through Modify / PreEditChange / PostEditChangeChainProperty on the object that owns the value,
 * exactly as the Details panel does, so the caller's FScopedTransaction records it and Undo takes it back.
 */
namespace ZettaProps
{
	/** Writes one path; OutLine is "Path = <new value>". */
	bool Write(UObject* Root, const FString& Path, const TSharedPtr<FJsonValue>& Value, FString& OutLine, FString& OutError);
	/** On an actor: the actor's own property, else a component named by the path's first part, else the first component that has it. */
	bool WriteOnActor(AActor* Actor, const FString& Path, const TSharedPtr<FJsonValue>& Value, FString& OutLine, FString& OutError);
	/** Writes every "path": value pair (actor-aware). Returns false when any write failed (others still apply). */
	bool WriteAll(UObject* Root, const TSharedPtr<FJsonObject>& Properties, TArray<FString>& OutLines, TArray<FString>& OutErrors);

	/**
	 * Writes a class default object or a component template, and carries each change to the instances that still had
	 * the old default (as the Details panel does), so placed actors follow their Blueprint.
	 */
	bool WriteArchetype(UObject* Archetype, const TSharedPtr<FJsonObject>& Properties, TArray<FString>& OutLines, TArray<FString>& OutErrors);

	/** Sets a value in place (no edit notifications): the type-checked conversion from JSON. */
	bool SetValue(FProperty* Property, void* ValuePtr, UObject* Owner, const TSharedPtr<FJsonValue>& Value, FString& OutError);

	/** A value as the model reads it: [x, y, z], "#rrggbb", asset paths, enum names; Unreal text otherwise. */
	FString Text(const FProperty* Property, const void* ValuePtr, int32 MaxLen = 300);
	/** "float", "bool", "Vector", "StaticMesh*", "TArray<Name>", "EComponentMobility". */
	FString TypeName(const FProperty* Property);
	/** Reads one path as text (for confirmations and inspect). */
	bool Read(UObject* Root, const FString& Path, FString& OutText, FString& OutError);

	/** "Name = value" lines: the editable properties that differ from the object's archetype, or all of them. */
	TArray<FString> Lines(UObject* Object, bool bAll, int32 Max = 150);
	/** Editable property names of a struct or class, for "Did you mean …?". */
	TArray<FString> EditableNames(const UStruct* Struct);
}
