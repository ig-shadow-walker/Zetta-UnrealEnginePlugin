// Copyright Zetta 3D. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;
class FDragDropEvent;
struct FAssetData;
struct FSlateBrush;

/**
 * Something the user pointed the agent at — an actor in the open level, an asset, or a file in the project —
 * and how it is told to the agent: as an address in the message's <mentions> block, never its contents. The
 * agent opens or inspects it itself when it gets to work on it.
 */
struct FZettaMention
{
	/** "actor", "asset" or "file". */
	FString Kind;
	/** Actor: how to target it ("Chair", "Lighting/Sun"). Asset: its package path. File: project-relative. */
	FString Path;
	/** Actor: its object name, the exact target ("StaticMeshActor_12"). */
	FString ObjectName;
	/** Actor or asset class ("StaticMeshActor", "Blueprint"). */
	FString Class;
	/** Actor: the level it is in ("/Game/Maps/Main"). */
	FString Level;
	/** Blueprint asset: its parent class. */
	FString ParentClass;
	/** Why it is here, when the user did not tag it by hand ("copied from your computer"). */
	FString Note;

	bool IsActor() const { return Kind == TEXT("actor"); }
	bool IsAsset() const { return Kind == TEXT("asset"); }
	/** What the chip shows: the last part of the path. */
	FString Name() const;
	/** One line of the <mentions> block. */
	FString Describe() const;
	bool SameAs(const FZettaMention& Other) const { return Kind == Other.Kind && Path == Other.Path && ObjectName == Other.ObjectName; }
	const FSlateBrush* Icon() const;
	/** The actor in the open level, when it still exists. */
	AActor* FindActor() const;

	static TOptional<FZettaMention> FromActor(const AActor* Actor);
	static TOptional<FZettaMention> FromAsset(const FAssetData& Asset);
	static FZettaMention FromFolder(const FString& ContentPath);
	static FZettaMention FromFile(const FString& ProjectRelativePath, const FString& Note = FString());
};

namespace ZettaMentions
{
	/** The block that closes a message, listing what each tag refers to (empty when there are none). */
	FString Block(const TArray<FZettaMention>& Mentions);

	/** Actors in the open level whose label, folder or class matches, best first. An empty query lists them all. */
	TArray<FZettaMention> SearchLevel(const FString& Query, int32 Limit = 60);
	/** Assets under /Game whose name or path matches, best first. */
	TArray<FZettaMention> SearchProject(const FString& Query, int32 Limit = 60);

	/** What is selected: actors in the level, then assets in the Content Browser. */
	TArray<FZettaMention> FromSelection();
	TArray<FZettaMention> SelectedActors();
	TArray<FZettaMention> SelectedAssets();
	/** Actors dragged from the Outliner or the viewport, assets and folders from the Content Browser. */
	TArray<FZettaMention> FromDrag(const FDragDropEvent& Event);
	/** Whether a drag carries actors or assets (files from disk are a different thing). */
	bool DragHasObjects(const FDragDropEvent& Event);

	/** The level the editor is editing. */
	UWorld* EditorWorld();
}
