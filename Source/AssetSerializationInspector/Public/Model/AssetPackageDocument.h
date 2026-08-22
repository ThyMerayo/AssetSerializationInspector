// Copyright Diego Merayo Merayo. All Rights Reserved
#pragma once

#include "CoreMinimal.h"
#include "UObject/PackageFileSummary.h"

struct FAssetPackageNameEntry
{
	int32 Index = INDEX_NONE;

	/** Exact serialized range, including the FString and serialized hashes. */
	int64 Offset = 0;
	int64 Size = 0;

	FString Name;

	/**
	 * Serialized name hashes.
	 *
	 * These are stored as raw values for analysis. We should not assume they
	 * are stable identifiers outside the package serialization format.
	 */
	uint16 NonCasePreservingHash = 0;
	uint16 CasePreservingHash = 0;
};

struct FAssetPackageDocument
{
	FString Filename;
	TArray64<uint8> FileData;

	FPackageFileSummary PackageSummary;
	int64 SerializedSummarySize = 0;
	bool bHasValidPackageSummary = false;

	TArray<FAssetPackageNameEntry> NameMap;

	int64 DecodedNameMapEnd = 0;
	FText NameMapError;
	bool bHasDecodedNameMap = false;

	int64 GetFileSize() const { return FileData.Num(); }

	bool IsValidRange(const int64 Offset, const int64 Size) const;

	const FAssetPackageNameEntry* FindNameEntry(const int32 NameIndex) const;

	FString ResolveNameIndex(const int32 NameIndex) const;
};