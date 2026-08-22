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

struct FAssetPackageNameReference
{
	int32 NameIndex = INDEX_NONE;
	int32 Number = 0;

	bool IsValid(const int32 NameCount) const { return NameIndex >= 0 && NameIndex < NameCount; }
};

enum class EAssetPackageIndexKind : uint8
{
	Null,
	Import,
	Export
};

struct FAssetPackageIndexReference
{
	int32 RawIndex = 0;

	EAssetPackageIndexKind GetKind() const
	{
		if (RawIndex == 0)
		{
			return EAssetPackageIndexKind::Null;
		}

		return RawIndex < 0 ? EAssetPackageIndexKind::Import : EAssetPackageIndexKind::Export;
	}

	int32 GetArrayIndex() const
	{
		if (RawIndex < 0)
		{
			return -RawIndex - 1;
		}

		if (RawIndex > 0)
		{
			return RawIndex - 1;
		}

		return INDEX_NONE;
	}
};

struct FAssetPackageImportEntry
{
	int32 Index = INDEX_NONE;

	int64 Offset = 0;
	int64 Size = 0;

	FAssetPackageNameReference ClassPackage;
	FAssetPackageNameReference ClassName;

	FAssetPackageIndexReference OuterIndex;

	FAssetPackageNameReference ObjectName;

	/**
	 * Bytes remaining after decoding the stable FObjectImport prefix.
	 *
	 * These may contain PackageName, optional-import state, or other
	 * version-dependent fields.
	 */
	int64 UndecodedTailOffset = 0;
	int64 UndecodedTailSize = 0;
};

struct FAssetPackageDocument
{
	FString Filename;
	TArray64<uint8> FileData;

	FPackageFileSummary PackageSummary;
	int64 SerializedSummarySize = 0;
	bool bHasValidPackageSummary = false;

	TArray<FAssetPackageNameEntry> NameMap;
	int64 NameMapRegionStart = 0;
	int64 NameMapRegionEnd = 0;
	int64 DecodedNameMapEnd = 0;
	FText NameMapError;
	bool bHasDecodedNameMap = false;

	TArray<FAssetPackageImportEntry> ImportMap;
	int64 ImportMapRegionStart = 0;
	int64 ImportMapRegionEnd = 0;
	int64 DecodedImportMapEnd = 0;
	int64 ImportEntryStride = 0;
	FText ImportMapError;
	bool bHasDecodedImportMap = false;

	int64 GetFileSize() const;

	bool IsValidRange(const int64 Offset, const int64 Size) const;

	const FAssetPackageNameEntry* FindNameEntry(const int32 NameIndex) const;

	FString ResolveNameIndex(const int32 NameIndex) const;

	FString ResolveNameReference(const FAssetPackageNameReference& Reference) const;

	FString DescribePackageIndex(const FAssetPackageIndexReference& Reference) const;

	FString ResolveImportPath(int32 ImportIndex) const;

private:
	FString ResolveImportPathInternal(const int32 ImportIndex, TSet<int32>& VisitedImports) const;
};