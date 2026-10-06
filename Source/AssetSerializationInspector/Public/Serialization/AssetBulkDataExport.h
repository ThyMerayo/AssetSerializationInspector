// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Serialization/AssetStructNativeData.h"

struct FAssetPackageDocument;
struct FAssetPackageExportEntry;

/**
 * The record of a block of editor data kept outside the properties of an object (FEditorBulkData): the source image of a texture, the
 * mesh description of a static mesh. The record is small and holds what identifies the data, not the data: its content hash, its size
 * and how it is stored. The data itself is in the package, a sidecar file or a virtualization backend.
 */
struct FAssetBulkDataInfo
{
	/** EFlags of FEditorBulkData. */
	uint32 Flags = 0;

	/** The identifier of the bulk data, and the hash of its content (what decides whether it changed). */
	FString Id;
	FString ContentHash;

	int64 PayloadSize = 0;

	/** Where the data is in the file, when the record says so (it moves from save to save). */
	int64 OffsetInFile = INDEX_NONE;

	/** The names of the flags that describe how the data is stored ("Virtualized, StoredInPackageTrailer"). */
	FString DescribeStorage() const;
};

/**
 * What a texture or the bulk data of a mesh description writes after its tagged properties, decoded: for a texture the record of its
 * source image, and whether it is cooked; for a mesh description the record of the mesh and its id. The pixels and the vertices
 * themselves are not here.
 */
struct FAssetBulkDataExport
{
	/** The whole range was read and nothing was left over. When false, Error says where it stopped. */
	bool bComplete = false;
	FString Error;

	/** The bytes the data covers in the document. */
	int64 Offset = 0;
	int64 Size = 0;

	/** "Texture" or "Mesh description". */
	FString Kind;

	FString ObjectGuid;

	/** The record of the source image or of the mesh description; absent when the editor data was stripped. */
	bool bHasBulkData = false;
	FAssetBulkDataInfo Bulk;

	/** A texture saved for a platform (its mips are stored cooked). */
	bool bCooked = false;

	/** A mesh description: its id, and whether the id is a hash of the content. */
	FString MeshGuid;

	/** A short account: the kind, the size of the data and its hash. */
	FString Summarize() const;
};

namespace AssetBulkDataExport
{
	/**
	 * Decodes the native data of a texture (a UTexture of any kind) or of the bulk data of a mesh description. Returns false, without
	 * touching Out, for any other export.
	 *
	 * @param NativeOffset Where the native data starts in the document (right after the tagged properties).
	 * @param NativeSize How many bytes it has.
	 */
	bool Decode(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, int64 NativeOffset, int64 NativeSize, FAssetBulkDataExport& Out);

	/**
	 * What differs between two versions: the content of the source data (its hash and size, in one change), how it is stored, and for
	 * a texture whether it is cooked. Where the data is in the file, and the identifier the save gives it, are not differences.
	 */
	TArray<FAssetNativeDataChange> Compare(const FAssetBulkDataExport& Old, const FAssetBulkDataExport& New);
} // namespace AssetBulkDataExport
