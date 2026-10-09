// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

struct FAssetPackageDocument;
class FNativeReader;

/** Reading the blocks of bulk data that a cooked package refers to through its data resource table: textures' mips, tiles, mesh LODs. */
namespace AssetCookedBulkData
{
	/** An entry of the data resource table of a package (FObjectDataResource): where a block of bulk data is and how it is stored. */
	struct FDataResource
	{
		uint32 BulkFlags = 0;
		int64 SerialOffset = INDEX_NONE;
		int64 SerialSize = 0;
		int64 RawSize = 0;
	};

	/** A block of cooked bulk data (a mip, a chunk of tiles, a LOD) as the package refers to it: by an index into the data resource table. */
	struct FBulkReference
	{
		uint32 Flags = 0;
		int64 RawSize = 0;
		int64 Offset = INDEX_NONE;

		/** Where the payload is to be read and how many bytes it takes: its offset in the document when inline, in the sidecar file otherwise. */
		int64 DataOffset = INDEX_NONE;
		int64 StoredSize = 0;

		/** A hash of the payload, when it is inline or in the sidecar file next to the package. */
		FString PayloadHash;
	};

	/** Reads the data resource table of the package, where the cook records every block of bulk data. Error is set when the table is not readable. */
	void ReadDataResources(const FAssetPackageDocument& Document, TArray<FDataResource>& Out, FString& Error);

	/** A hash of bytes, short enough to read: what tells that they changed without saying what they hold. */
	FString HashBytes(const uint8* Data, int64 Size);

	/** A hash of a range of the document; empty when the range is empty or is not in the document. */
	FString HashDocumentRange(const FAssetPackageDocument& Document, int64 Offset, int64 Size);

	/** A hash of a range of the sidecar file of the package (Extension), when the file is next to the package; empty when it is not. */
	FString HashSidecarRange(const FAssetPackageDocument& Document, const TCHAR* Extension, int64 Offset, int64 Size);

	/** The extension of the sidecar file a payload is in, by its bulk data flags; null for a payload that is inline. */
	const TCHAR* SidecarExtension(uint32 BulkFlags);

	/**
	 * The bytes of a block of cooked bulk data: from the document when they are inline, from the sidecar file next to the package otherwise.
	 * Returns false when the file is not there or is shorter than the block.
	 */
	bool LoadBytes(const FAssetPackageDocument& Document, uint32 BulkFlags, int64 DataOffset, int64 StoredSize, TArray64<uint8>& Out);

	/** Reads the index of a block, and the payload when it follows inline (skipped and hashed); a payload in a sidecar file is hashed from there. */
	bool ReadBulkReference(FNativeReader& Reader, const FAssetPackageDocument& Document, const TArray<FDataResource>& Resources, const TCHAR* What, FBulkReference& Out);
} // namespace AssetCookedBulkData
