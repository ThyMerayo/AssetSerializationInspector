// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

struct FAssetPackageDocument;
class FNativeReader;

/** The header of a block of bulk data in the format before FEditorBulkData (FByteBulkData), and the hash of its payload. */
struct FAssetLegacyBulkData
{
	/** EBulkDataFlags of the header. */
	uint32 Flags = 0;

	/** How many elements the block has (the bytes, for the byte blocks the editor keeps). */
	int64 ElementCount = 0;

	/** How many bytes the payload takes in the file (smaller than the elements when it is compressed). */
	int64 SizeOnDisk = 0;

	/** SHA1 of the payload as stored, when it is in the document; empty for an empty block or a payload outside the file. */
	FString PayloadHash;

	bool IsAtEndOfFile() const;
	bool IsCompressed() const;
};

namespace AssetLegacyBulkData
{
	/**
	 * Reads what FByteBulkData::Serialize writes in the export: the flags, the sizes and the offset (32 bit sizes unless the flags say 64),
	 * then the payload when it is inline, which is skipped and hashed. A payload at the end of the file is hashed from where the header
	 * says. The reader fails if the sizes are negative or the data ends first.
	 */
	FAssetLegacyBulkData Read(FNativeReader& Reader, const FAssetPackageDocument& Document);
} // namespace AssetLegacyBulkData
