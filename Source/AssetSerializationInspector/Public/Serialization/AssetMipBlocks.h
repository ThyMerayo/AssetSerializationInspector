// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Serialization/AssetBulkDataExport.h"
#include "Serialization/AssetStructNativeData.h"

struct FAssetPackageDocument;

/** The block compression a pixel format uses, for the ones whose colors this reading can decode. */
enum class EAssetBlockCodec : uint8
{
	None,
	BC1,
	BC2,
	BC3,
	BC4,
	BC5,
	BC6H,
	BC7,
	Astc
};

/** How a pixel format stores a mip: in blocks of Width by Height pixels, each BytesPerBlock bytes (an uncompressed format has blocks of one pixel). */
struct FAssetBlockFormat
{
	int32 BlockWidth = 1;
	int32 BlockHeight = 1;
	int32 BytesPerBlock = 0;

	/** How the colors of a block can be decoded; None when they cannot (the blocks are still compared). */
	EAssetBlockCodec Codec = EAssetBlockCodec::None;

	/** The colors are HDR values, which can be above 1 (BC6H and the HDR ASTC formats). */
	bool bHdr = false;
};

/** What differs between two versions of the same mip, counted in blocks. */
struct FAssetMipBlockDiff
{
	/** The mips could be compared. When false, Reason says why not. */
	bool bComparable = false;
	FString Reason;

	int64 TotalBlocks = 0;
	int64 DifferingBlocks = 0;

	/** The pixels covered by the blocks that differ (the first and last pixel of the box that holds them), when some differ. */
	int32 MinX = 0;
	int32 MinY = 0;
	int32 MaxX = 0;
	int32 MaxY = 0;

	/** The colors were decoded (a format with a codec): the largest change of a channel over the blocks that differ, from 0 to 1, and the average color of the whole mip on each side. */
	bool bColors = false;
	double LargestChange = 0.0;
	FVector4d OldAverage = FVector4d(0.0, 0.0, 0.0, 0.0);
	FVector4d NewAverage = FVector4d(0.0, 0.0, 0.0, 0.0);
};

/**
 * A comparison of cooked mips that does not decode them: the pixels of a block compressed format (BC, ASTC) are stored in blocks of a fixed
 * size, row after row, so two versions of a mip can be compared block by block. A block that differs says where in the image something
 * changed, and how much of it; it does not say how the colors changed.
 */
namespace AssetMipBlocks
{
	/** The block layout of a pixel format by its name ("PF_DXT1", "PF_BC7", "PF_ASTC_6x6", "PF_B8G8R8A8"...). False for a format that is not known. */
	bool FindBlockFormat(const FString& PixelFormat, FAssetBlockFormat& Out);

	/**
	 * Compares two mips of Width by Height pixels in Size bytes. Not comparable when the size is not the number of blocks times the bytes of a
	 * block (a layout this reading does not know, such as a mip packed in a tail).
	 */
	FAssetMipBlockDiff CompareBytes(const uint8* Old, const uint8* New, int64 Size, int32 Width, int32 Height, const FAssetBlockFormat& Format);

	/**
	 * Appends, for each mip of the platform data whose pixels changed (the hashes differ) and that kept its size and storage, the blocks that
	 * changed: how many, where, and the hashes. The pixels are read from the package or its sidecar file; a mip that cannot be read is skipped.
	 *
	 * @param PlatformIndex The index of the platform data in the export, for the key of the change.
	 */
	void AppendBlockChanges(const FAssetPackageDocument& OldDocument, const FAssetTexturePlatformData& OldPlatform, const FAssetPackageDocument& NewDocument,
		const FAssetTexturePlatformData& NewPlatform, int32 PlatformIndex, TArray<FAssetNativeDataChange>& Changes);
} // namespace AssetMipBlocks
