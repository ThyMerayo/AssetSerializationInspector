// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Serialization/AssetStaticMeshData.h"
#include "Serialization/AssetStructNativeData.h"

struct FAssetPackageDocument;
struct FAssetPackageExportEntry;

/** One bone of the reference skeleton of a skeletal mesh: its name, its parent and its pose. */
struct FAssetSkeletonBone
{
	FString Name;
	FString ParentName;

	/** The name the bone had in the file the mesh was imported from, when the package keeps it. */
	FString ExportName;

	/** The pose of the bone relative to its parent. */
	FString Pose;
};

/**
 * What a skeletal mesh writes first after its tagged properties, decoded (USkeletalMesh::Serialize): its bounds, its material slots and
 * its reference skeleton. What follows (the imported model with its LODs, sections and vertices, and the render data of a cooked mesh)
 * is not read: the data stops being read where the imported model starts, and RemainingSize says how much is left.
 */
struct FAssetSkeletalMeshData
{
	/** The bounds, the materials and the skeleton were read, and the counts of the skeleton agree. When false, Error says where it stopped. */
	bool bPrefixRead = false;
	FString Error;

	/** The bytes the data covers in the document, and how many of them follow what was read. */
	int64 Offset = 0;
	int64 Size = 0;
	int64 RemainingSize = 0;

	FString ObjectGuid;

	/** The bounds the mesh had when it was imported, as text. */
	FString ImportedBounds;

	TArray<FAssetMeshMaterialSlot> Materials;
	TArray<FAssetSkeletonBone> Bones;

	/** A short account: how many material slots and bones. */
	FString Summarize() const;
};

namespace AssetSkeletalMeshData
{
	/**
	 * Decodes the start of the native data of a skeletal mesh export. Returns false, without touching Out, for any other export.
	 *
	 * @param NativeOffset Where the native data starts in the document (right after the tagged properties).
	 * @param NativeSize How many bytes it has.
	 */
	bool Decode(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, int64 NativeOffset, int64 NativeSize, FAssetSkeletalMeshData& Out);

	/**
	 * What differs between the material slots (matched by slot name), the bones (matched by name: added, removed, moved to another
	 * parent, or given another pose) and the imported bounds.
	 */
	TArray<FAssetNativeDataChange> Compare(const FAssetSkeletalMeshData& Old, const FAssetSkeletalMeshData& New);
} // namespace AssetSkeletalMeshData
