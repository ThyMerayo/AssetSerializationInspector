// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

struct FAssetPackageDocument;

/** A contiguous part of a package's header: the summary at the start, or one of the tables the summary points to. */
struct FAssetPackageHeaderRegion
{
	/** Stable identifier used to match the same region between two packages, such as "NameMap". */
	FString Key;

	FText Name;

	int64 Offset = 0;
	int64 Size = 0;

	/** The entry count the summary declares for the table, or INDEX_NONE when it does not declare one. */
	int32 EntryCount = INDEX_NONE;

	int64 End() const { return Offset + Size; }
};

namespace AssetPackageHeaderLayout
{
	/** The size of the header: where the export data starts. Falls back to the file size when the summary does not say. */
	int64 GetHeaderSize(const FAssetPackageDocument& Document);

	/**
	 * Splits the header into regions ordered by offset: the package summary, then every table the summary locates (name map,
	 * import map, export map, and so on). A table ends where the next one starts, and the last one at the end of the header.
	 */
	TArray<FAssetPackageHeaderRegion> Build(const FAssetPackageDocument& Document);
} // namespace AssetPackageHeaderLayout
