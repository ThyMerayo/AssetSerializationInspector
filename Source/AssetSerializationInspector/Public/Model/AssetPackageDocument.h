// Copyright Diego Merayo Merayo. All Rights Reserved
#pragma once

#include "CoreMinimal.h"

/**
 * Owns the exact bytes loaded from a package file.
 *
 * The file is loaded without the package being opened by Unreal's asset
 * loading system. This is important because inspection should not mutate,
 * upgrade, or resave the package.
 */
struct FAssetPackageDocument
{
	FString Filename;
	TArray64<uint8> FileData;

	int64 GetFileSize() const { return FileData.Num(); }

	bool IsValidRange(const int64 Offset, const int64 Size) const
	{
		if (Offset < 0 || Size < 0)
		{
			return false;
		}

		const int64 FileSize = FileData.Num();

		if (Offset > FileSize)
		{
			return false;
		}

		// Written this way to avoid Offset + Size overflowing.
		return Size <= FileSize - Offset;
	}
};