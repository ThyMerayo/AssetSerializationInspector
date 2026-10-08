// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Serialization/AssetMipBlocks.h"

/**
 * Decoders for the block formats BC1 to BC5 (plain interpolation) and BC7 (eight modes), so the colors of a cooked mip can be compared
 * and not only its blocks. BC6H and ASTC are not decoded.
 */
namespace AssetBlockDecoder
{
	/**
	 * Decodes one 4 by 4 block into 16 pixels, row by row, each as red, green, blue and alpha from 0 to 1 (as stored, without a gamma
	 * conversion). BC4 is a grey (the red channel in all three); BC5 has red and green and no blue.
	 */
	void DecodeBlock(EAssetBlockCodec Codec, const uint8* Block, FVector4f OutPixels[16]);
} // namespace AssetBlockDecoder
