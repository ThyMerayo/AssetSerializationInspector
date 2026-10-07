// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Serialization/AssetStructNativeData.h"

struct FAssetPackageDocument;
struct FAssetPackageExportEntry;

/** One LOD of a morph target (FMorphTargetLODModel): the vertex deltas it applies to the LOD of the mesh with the same number. */
struct FAssetMorphTargetLod
{
	/** How many vertices the morph target moves in this LOD. */
	int32 DeltaCount = 0;

	/** A hash of the deltas (position, normal and the vertex they apply to), so that a moved vertex is a change without listing them. Empty when they were stripped. */
	FString DeltaHash;

	/** The deltas are not in the data: a cook keeps their count only and stores them compressed elsewhere. */
	bool bDeltasStripped = false;

	/** How many vertices the base mesh had when the morph target was made. */
	int32 NumBaseMeshVerts = 0;

	/** The sections of the mesh that the deltas touch. */
	int32 SectionCount = 0;

	/** Made by the engine (from the import of a LOD) and not by the user. */
	bool bGeneratedByEngine = false;

	/** The file the morph target was imported from. */
	FString SourceFilename;

	FString Describe() const;
};

/**
 * What a morph target (UMorphTarget) writes after its tagged properties, decoded: its LODs with their deltas. The deltas are hashed,
 * not listed. A cooked morph target that stores its deltas compressed is not read.
 */
struct FAssetMorphTargetData
{
	/** The whole range was read and nothing was left over. When false, Error says where it stopped. */
	bool bComplete = false;
	FString Error;

	/** The bytes the data covers in the document. */
	int64 Offset = 0;
	int64 Size = 0;

	FString ObjectGuid;
	bool bCooked = false;

	TArray<FAssetMorphTargetLod> Lods;

	/** A short account: the LODs and the deltas of each. */
	FString Summarize() const;
};

namespace AssetMorphTargetData
{
	/**
	 * Decodes the native data of a morph target. Returns false, without touching Out, for any other export.
	 *
	 * @param NativeOffset Where the native data starts in the document (right after the tagged properties).
	 * @param NativeSize How many bytes it has.
	 */
	bool Decode(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, int64 NativeOffset, int64 NativeSize, FAssetMorphTargetData& Out);

	/** What differs between two versions: the LODs that were added or removed and, for each LOD, its deltas, its base vertex count, its sections and its source file. */
	TArray<FAssetNativeDataChange> Compare(const FAssetMorphTargetData& Old, const FAssetMorphTargetData& New);
} // namespace AssetMorphTargetData
