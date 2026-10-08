// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetMipBlocks.h"

#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetCookedBulkData.h"

bool AssetMipBlocks::FindBlockFormat(const FString& PixelFormat, FAssetBlockFormat& Out)
{
	struct FKnownFormat
	{
		const TCHAR* Name;
		int32 Width;
		int32 Height;
		int32 Bytes;
	};

	static const FKnownFormat Formats[] = {
		{ TEXT("PF_DXT1"), 4, 4, 8 },
		{ TEXT("PF_DXT3"), 4, 4, 16 },
		{ TEXT("PF_DXT5"), 4, 4, 16 },
		{ TEXT("PF_BC4"), 4, 4, 8 },
		{ TEXT("PF_BC5"), 4, 4, 16 },
		{ TEXT("PF_BC6H"), 4, 4, 16 },
		{ TEXT("PF_BC7"), 4, 4, 16 },
		{ TEXT("PF_ASTC_4x4"), 4, 4, 16 },
		{ TEXT("PF_ASTC_4x4_HDR"), 4, 4, 16 },
		{ TEXT("PF_ASTC_6x6"), 6, 6, 16 },
		{ TEXT("PF_ASTC_6x6_HDR"), 6, 6, 16 },
		{ TEXT("PF_ASTC_8x8"), 8, 8, 16 },
		{ TEXT("PF_ASTC_8x8_HDR"), 8, 8, 16 },
		{ TEXT("PF_ASTC_10x10"), 10, 10, 16 },
		{ TEXT("PF_ASTC_10x10_HDR"), 10, 10, 16 },
		{ TEXT("PF_ASTC_12x12"), 12, 12, 16 },
		{ TEXT("PF_ASTC_12x12_HDR"), 12, 12, 16 },
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
	for (int64 Index = 0; Index < Result.TotalBlocks; ++Index)
	{
		if (FMemory::Memcmp(Old + Index * Format.BytesPerBlock, New + Index * Format.BytesPerBlock, Format.BytesPerBlock) != 0)
		{
			++Result.DifferingBlocks;
			const int64 BlockX = Index % BlocksX;
			const int64 BlockY = Index / BlocksX;
			MinBlockX = FMath::Min(MinBlockX, BlockX);
			MaxBlockX = FMath::Max(MaxBlockX, BlockX);
			MinBlockY = FMath::Min(MinBlockY, BlockY);
			MaxBlockY = FMath::Max(MaxBlockY, BlockY);
		}
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
		if (!Diff.bComparable)
		{
			Change.Title = FString::Printf(TEXT("Mip %d blocks: not compared, %s"), MipIndex, *Diff.Reason);
		}
		else
		{
			Change.Title = FString::Printf(TEXT("Mip %d blocks: %lld of %lld differ (%.1f%%), within x %d to %d and y %d to %d (pixels)"), MipIndex, Diff.DifferingBlocks, Diff.TotalBlocks,
				100.0 * static_cast<double>(Diff.DifferingBlocks) / static_cast<double>(FMath::Max<int64>(Diff.TotalBlocks, 1)), Diff.MinX, Diff.MaxX, Diff.MinY, Diff.MaxY);
		}
	}
}
