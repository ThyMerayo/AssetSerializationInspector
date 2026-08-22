#pragma once

#include "CoreMinimal.h"
#include "UObject/PackageFileSummary.h"

struct FAssetPackageDocument
{
	FString Filename;
	TArray64<uint8> FileData;

	/** Parsed package summary, when available. */
	FPackageFileSummary PackageSummary;

	/**
	 * Number of physical bytes consumed while deserializing PackageSummary.
	 *
	 * This is not sizeof(FPackageFileSummary). The serialized representation
	 * is version-dependent.
	 */
	int64 SerializedSummarySize = 0;

	bool bHasValidPackageSummary = false;

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

		return Size <= FileSize - Offset;
	}
};