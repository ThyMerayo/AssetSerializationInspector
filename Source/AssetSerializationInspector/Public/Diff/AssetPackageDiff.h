// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

struct FAssetPackageDocument;

enum class EAssetPackageDiffState : uint8
{
	Unchanged,
	Added,
	Removed,
	Modified,
	Moved
};

enum class EAssetPackageDiffKind : uint8
{
	File,
	SummaryField,
	Name,
	Import,
	Export,
	ExportPayload
};

struct FAssetPackageDiffEntry
{
	EAssetPackageDiffKind Kind = EAssetPackageDiffKind::File;

	EAssetPackageDiffState State = EAssetPackageDiffState::Unchanged;

	FString Key;
	FText DisplayName;

	FString OldValue;
	FString NewValue;

	int64 OldOffset = INDEX_NONE;
	int64 NewOffset = INDEX_NONE;

	int64 OldSize = 0;
	int64 NewSize = 0;

	int32 OldExportIndex = INDEX_NONE;
	int32 NewExportIndex = INDEX_NONE;

	TArray<FAssetPackageDiffEntry> Children;
};

struct FAssetPackageDiffResult
{
	FString OldFilename;
	FString NewFilename;

	bool bFilesIdentical = false;

	FString OldFileHash;
	FString NewFileHash;

	TArray<FAssetPackageDiffEntry> Entries;

	int32 AddedCount = 0;
	int32 RemovedCount = 0;
	int32 ModifiedCount = 0;
	int32 MovedCount = 0;
};

class FAssetPackageDiff
{
public:
	static FAssetPackageDiffResult Compare(const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument);
};