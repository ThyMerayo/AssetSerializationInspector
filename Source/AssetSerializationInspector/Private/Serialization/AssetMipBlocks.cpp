// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetMipBlocks.h"

#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetAstcDecoder.h"
#include "Serialization/AssetBlockDecoder.h"
#include "Serialization/AssetCookedBulkData.h"

bool AssetMipBlocks::FindBlockFormat(const FString& PixelFormat, FAssetBlockFormat& Out)
{
	struct FKnownFormat
	{
		const TCHAR* Name;
		int32 Width;
		int32 Height;
		int32 Bytes;
		EAssetBlockCodec Codec = EAssetBlockCodec::None;
		bool bHdr = false;
	};

	static const FKnownFormat Formats[] = {
		{ TEXT("PF_DXT1"), 4, 4, 8, EAssetBlockCodec::BC1 },
		{ TEXT("PF_DXT3"), 4, 4, 16, EAssetBlockCodec::BC2 },
		{ TEXT("PF_DXT5"), 4, 4, 16, EAssetBlockCodec::BC3 },
		{ TEXT("PF_BC4"), 4, 4, 8, EAssetBlockCodec::BC4 },
		{ TEXT("PF_BC5"), 4, 4, 16, EAssetBlockCodec::BC5 },
		{ TEXT("PF_BC6H"), 4, 4, 16, EAssetBlockCodec::BC6H },
		{ TEXT("PF_BC7"), 4, 4, 16, EAssetBlockCodec::BC7 },
		{ TEXT("PF_ASTC_4x4"), 4, 4, 16, EAssetBlockCodec::Astc },
		{ TEXT("PF_ASTC_4x4_HDR"), 4, 4, 16, EAssetBlockCodec::Astc, true },
		{ TEXT("PF_ASTC_6x6"), 6, 6, 16, EAssetBlockCodec::Astc },
		{ TEXT("PF_ASTC_6x6_HDR"), 6, 6, 16, EAssetBlockCodec::Astc, true },
		{ TEXT("PF_ASTC_8x8"), 8, 8, 16, EAssetBlockCodec::Astc },
		{ TEXT("PF_ASTC_8x8_HDR"), 8, 8, 16, EAssetBlockCodec::Astc, true },
		{ TEXT("PF_ASTC_10x10"), 10, 10, 16, EAssetBlockCodec::Astc },
		{ TEXT("PF_ASTC_10x10_HDR"), 10, 10, 16, EAssetBlockCodec::Astc, true },
		{ TEXT("PF_ASTC_12x12"), 12, 12, 16, EAssetBlockCodec::Astc },
		{ TEXT("PF_ASTC_12x12_HDR"), 12, 12, 16, EAssetBlockCodec::Astc, true },
		{ TEXT("PF_G8"), 1, 1, 1 },
		{ TEXT("PF_A8"), 1, 1, 1 },
		{ TEXT("PF_R8_UINT"), 1, 1, 1 },
		{ TEXT("PF_G16"), 1, 1, 2 },
		{ TEXT("PF_R16F"), 1, 1, 2 },
		{ TEXT("PF_B8G8R8A8"), 1, 1, 4 },
		{ TEXT("PF_R8G8B8A8"), 1, 1, 4 },
		{ TEXT("PF_R32_FLOAT"), 1, 1, 4 },
		{ TEXT("PF_FloatRGBA"), 1, 1, 8 },
		{ TEXT("PF_R16G16B16A16_UNORM"), 1, 1, 8 },
		{ TEXT("PF_A32B32G32R32F"), 1, 1, 16 },
	};

	for (const FKnownFormat& Format : Formats)
	{
		if (PixelFormat == Format.Name)
		{
			Out.BlockWidth = Format.Width;
			Out.BlockHeight = Format.Height;
			Out.BytesPerBlock = Format.Bytes;
			Out.Codec = Format.Codec;
			Out.bHdr = Format.bHdr || Format.Codec == EAssetBlockCodec::BC6H;
			return true;
		}
	}
	return false;
}

FAssetMipBlockDiff AssetMipBlocks::CompareBytes(const uint8* Old, const uint8* New, const int64 Size, const int32 Width, const int32 Height, const FAssetBlockFormat& Format)
{
	FAssetMipBlockDiff Result;
	if (Width <= 0 || Height <= 0 || Format.BytesPerBlock <= 0 || Format.BlockWidth <= 0 || Format.BlockHeight <= 0)
	{
		Result.Reason = TEXT("the mip has no size");
		return Result;
	}

	const int64 BlocksX = (Width + Format.BlockWidth - 1) / Format.BlockWidth;
	const int64 BlocksY = (Height + Format.BlockHeight - 1) / Format.BlockHeight;
	if (BlocksX * BlocksY * Format.BytesPerBlock != Size)
	{
		Result.Reason = TEXT("the mip is not laid out as the blocks of its format");
		return Result;
	}

	Result.bComparable = true;
	Result.TotalBlocks = BlocksX * BlocksY;
	int64 MinBlockX = MAX_int64, MinBlockY = MAX_int64, MaxBlockX = -1, MaxBlockY = -1;

	// With a codec the colors of every block are decoded on both sides, for the average of the mip and the largest change.
	const bool bAstc = Format.Codec == EAssetBlockCodec::Astc;
	const bool bDecode = Format.Codec != EAssetBlockCodec::None && !bAstc && Format.BlockWidth == 4 && Format.BlockHeight == 4;
	TArray<bool> DifferingBlock; // for ASTC: which blocks differ, to find the largest change once the images are decoded
	if (bAstc)
	{
		DifferingBlock.Init(false, Result.TotalBlocks);
	}
	FVector4d OldSum(0.0, 0.0, 0.0, 0.0);
	FVector4d NewSum(0.0, 0.0, 0.0, 0.0);
	for (int64 Index = 0; Index < Result.TotalBlocks; ++Index)
	{
		const int64 BlockX = Index % BlocksX;
		const int64 BlockY = Index / BlocksX;
		const bool bDiffers = FMemory::Memcmp(Old + Index * Format.BytesPerBlock, New + Index * Format.BytesPerBlock, Format.BytesPerBlock) != 0;

		if (bDecode)
		{
			FVector4f OldPixels[16];
			FVector4f NewPixels[16];
			AssetBlockDecoder::DecodeBlock(Format.Codec, Old + Index * Format.BytesPerBlock, OldPixels);
			AssetBlockDecoder::DecodeBlock(Format.Codec, New + Index * Format.BytesPerBlock, NewPixels);
			for (int32 Pixel = 0; Pixel < 16; ++Pixel)
			{
				// The pixels of a block that lie outside the image are padding.
				if (BlockX * 4 + (Pixel & 3) >= Width || BlockY * 4 + (Pixel >> 2) >= Height)
				{
					continue;
				}

				OldSum += FVector4d(OldPixels[Pixel]);
				NewSum += FVector4d(NewPixels[Pixel]);
				if (bDiffers)
				{
					const FVector4f Delta = OldPixels[Pixel] - NewPixels[Pixel];
					Result.LargestChange =
						FMath::Max(Result.LargestChange, static_cast<double>(FMath::Max(FMath::Max(FMath::Abs(Delta.X), FMath::Abs(Delta.Y)), FMath::Max(FMath::Abs(Delta.Z), FMath::Abs(Delta.W)))));
				}
			}
		}

		if (bDiffers)
		{
			if (bAstc)
			{
				DifferingBlock[Index] = true;
			}
			++Result.DifferingBlocks;
			MinBlockX = FMath::Min(MinBlockX, BlockX);
			MaxBlockX = FMath::Max(MaxBlockX, BlockX);
			MinBlockY = FMath::Min(MinBlockY, BlockY);
			MaxBlockY = FMath::Max(MaxBlockY, BlockY);
		}
	}

	if (bAstc)
	{
		// ASTC is decoded by the library as whole images; the largest change is looked for in the blocks that differ.
		TArray<FVector4f> OldImage;
		TArray<FVector4f> NewImage;
		FString Unused;
		if (AssetAstcDecoder::DecodeImage(Old, Size, Width, Height, Format.BlockWidth, Format.bHdr, OldImage, Unused)
			&& AssetAstcDecoder::DecodeImage(New, Size, Width, Height, Format.BlockWidth, Format.bHdr, NewImage, Unused))
		{
			for (int32 Y = 0; Y < Height; ++Y)
			{
				for (int32 X = 0; X < Width; ++X)
				{
					const int64 PixelIndex = static_cast<int64>(Y) * Width + X;
					OldSum += FVector4d(OldImage[PixelIndex]);
					NewSum += FVector4d(NewImage[PixelIndex]);
					if (DifferingBlock[(Y / Format.BlockHeight) * BlocksX + X / Format.BlockWidth])
					{
						const FVector4f Delta = OldImage[PixelIndex] - NewImage[PixelIndex];
						Result.LargestChange = FMath::Max(
							Result.LargestChange, static_cast<double>(FMath::Max(FMath::Max(FMath::Abs(Delta.X), FMath::Abs(Delta.Y)), FMath::Max(FMath::Abs(Delta.Z), FMath::Abs(Delta.W)))));
					}
				}
			}

			const double Pixels = static_cast<double>(Width) * Height;
			Result.bColors = true;
			Result.OldAverage = OldSum / Pixels;
			Result.NewAverage = NewSum / Pixels;
		}
	}

	if (bDecode)
	{
		const double Pixels = static_cast<double>(Width) * Height;
		Result.bColors = true;
		Result.OldAverage = OldSum / Pixels;
		Result.NewAverage = NewSum / Pixels;
	}

	if (Result.DifferingBlocks > 0)
	{
		Result.MinX = static_cast<int32>(MinBlockX * Format.BlockWidth);
		Result.MinY = static_cast<int32>(MinBlockY * Format.BlockHeight);
		Result.MaxX = FMath::Min(static_cast<int32>((MaxBlockX + 1) * Format.BlockWidth), Width) - 1;
		Result.MaxY = FMath::Min(static_cast<int32>((MaxBlockY + 1) * Format.BlockHeight), Height) - 1;
	}
	return Result;
}

void AssetMipBlocks::AppendBlockChanges(const FAssetPackageDocument& OldDocument, const FAssetTexturePlatformData& OldPlatform, const FAssetPackageDocument& NewDocument,
	const FAssetTexturePlatformData& NewPlatform, const int32 PlatformIndex, TArray<FAssetNativeDataChange>& Changes)
{
	FAssetBlockFormat Format;
	if (OldPlatform.bVirtual || NewPlatform.bVirtual || OldPlatform.PixelFormat != NewPlatform.PixelFormat || !FindBlockFormat(NewPlatform.PixelFormat, Format))
	{
		return;
	}

	for (int32 MipIndex = 0; MipIndex < FMath::Min(OldPlatform.Mips.Num(), NewPlatform.Mips.Num()); ++MipIndex)
	{
		const FAssetTextureMip& OldMip = OldPlatform.Mips[MipIndex];
		const FAssetTextureMip& NewMip = NewPlatform.Mips[MipIndex];

		// Only a mip whose pixels changed while its size and storage did not: the other changes already say what happened.
		const bool bSameShape = OldMip.SizeX == NewMip.SizeX && OldMip.SizeY == NewMip.SizeY && OldMip.SizeZ == NewMip.SizeZ && OldMip.PayloadSize == NewMip.PayloadSize
			&& OldMip.DescribeStorage() == NewMip.DescribeStorage();
		const bool bChanged = !OldMip.PayloadHash.IsEmpty() && !NewMip.PayloadHash.IsEmpty() && !OldMip.PayloadHash.Equals(NewMip.PayloadHash, ESearchCase::CaseSensitive);
		if (!bSameShape || !bChanged || NewMip.SizeZ > 1)
		{
			continue;
		}

		TArray64<uint8> OldBytes;
		TArray64<uint8> NewBytes;
		if (!AssetCookedBulkData::LoadBytes(OldDocument, OldMip.BulkFlags, OldMip.DataOffset, OldMip.StoredSize, OldBytes)
			|| !AssetCookedBulkData::LoadBytes(NewDocument, NewMip.BulkFlags, NewMip.DataOffset, NewMip.StoredSize, NewBytes) || OldBytes.Num() != NewBytes.Num())
		{
			continue;
		}

		const FAssetMipBlockDiff Diff = CompareBytes(OldBytes.GetData(), NewBytes.GetData(), OldBytes.Num(), NewMip.SizeX, NewMip.SizeY, Format);
		FAssetNativeDataChange& Change = Changes.AddDefaulted_GetRef();
		Change.Key = FString::Printf(TEXT("Platform/%d/Mip/%d/Blocks"), PlatformIndex, MipIndex);
		Change.State = FAssetNativeDataChange::EState::Modified;
		Change.OldValue = OldMip.PayloadHash;
		Change.NewValue = NewMip.PayloadHash;
		if (Diff.bColors)
		{
			Change.OldValue += FString::Printf(TEXT("; average color (%.3f, %.3f, %.3f, %.3f)"), Diff.OldAverage.X, Diff.OldAverage.Y, Diff.OldAverage.Z, Diff.OldAverage.W);
			Change.NewValue += FString::Printf(TEXT("; average color (%.3f, %.3f, %.3f, %.3f)"), Diff.NewAverage.X, Diff.NewAverage.Y, Diff.NewAverage.Z, Diff.NewAverage.W);
		}
		if (!Diff.bComparable)
		{
			Change.Title = FString::Printf(TEXT("Mip %d blocks: not compared, %s"), MipIndex, *Diff.Reason);
		}
		else
		{
			Change.Title = FString::Printf(TEXT("Mip %d blocks: %lld of %lld differ (%.1f%%), within x %d to %d and y %d to %d (pixels)"), MipIndex, Diff.DifferingBlocks, Diff.TotalBlocks,
				100.0 * static_cast<double>(Diff.DifferingBlocks) / static_cast<double>(FMath::Max<int64>(Diff.TotalBlocks, 1)), Diff.MinX, Diff.MaxX, Diff.MinY, Diff.MaxY);
			if (Diff.bColors)
			{
				// An HDR format has no range of 0 to 1: the change is in the units of the values.
				Change.Title += Format.bHdr || Format.Codec == EAssetBlockCodec::BC6H ? FString::Printf(TEXT("; the largest change of a channel is %.4g (HDR values)"), Diff.LargestChange)
																					  : FString::Printf(TEXT("; the largest change of a channel is %.3f of the range"), Diff.LargestChange);
			}
		}
	}
}
