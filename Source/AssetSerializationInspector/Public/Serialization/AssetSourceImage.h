// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Serialization/AssetBulkDataExport.h"
#include "Serialization/AssetStructNativeData.h"

struct FAssetPackageDocument;
struct FAssetPackageExportEntry;
struct FAssetSerializationTrace;

/**
 * The pixels of the source image of a texture: the first mip of the image the artist imported, as the editor keeps it in the package
 * trailer. Only formats whose pixels are plain values are decoded (8 and 16 bit channels, half and full floats); an image compressed
 * as PNG or JPEG, or one the package does not hold (virtualized, a legacy file), is not.
 */
struct FAssetSourceImage
{
	/** The pixels were found and can be compared. When false, Error says why not. */
	bool bLoaded = false;
	FString Error;

	int32 Width = 0;
	int32 Height = 0;
	int32 NumSlices = 1;

	/** "TSF_BGRA8", "TSF_G16"... */
	FString Format;

	/** The bytes of the first mip of the first block, slices one after the other. */
	TArray64<uint8> Pixels;

	int32 BytesPerPixel() const;
};

namespace AssetSourceImage
{
	/**
	 * Loads the pixels the bulk data record of a texture refers to.
	 *
	 * @param Trace The trace of the texture export, for the properties of its source (size, format).
	 */
	FAssetSourceImage Load(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, const FAssetSerializationTrace* Trace, const FAssetBulkDataInfo& Bulk);

	/**
	 * Appends the change of the pixels between two images to Changes: how many pixels differ, where, how much the largest difference is
	 * and the average color of each. Appends nothing when they are the same, and a note instead when they cannot be compared (another
	 * size or format, or pixels that could not be loaded).
	 */
	void AppendPixelChange(const FAssetSourceImage& Old, const FAssetSourceImage& New, TArray<FAssetNativeDataChange>& Changes);
} // namespace AssetSourceImage
