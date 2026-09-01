// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

struct FAssetPackageDocument;
struct FAssetPackageTraceCollection;
struct FAssetSerializationTrace;
struct FAssetSerializationTraceNode;

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
	ExportPayload,
	Property,
	UnknownPayloadRange
};

struct FAssetByteDiffSpan
{
	int64 Offset = 0; // relative to selected range
	int64 Size = 0;

	FString OldFieldPath;
	FString NewFieldPath;

	int64 End() const { return Offset + Size; }
};

struct FPropertyDiffAccumulator
{
	const FAssetSerializationTraceNode* OldNode = nullptr;
	const FAssetSerializationTraceNode* NewNode = nullptr;

	int64 ChangedByteCount = 0;

	TArray<FAssetByteDiffSpan> Spans;
};

struct FAssetPackageDiffEntry
{
	EAssetPackageDiffKind Kind = EAssetPackageDiffKind::File;

	EAssetPackageDiffState State = EAssetPackageDiffState::Unchanged;

	FString Key;
	FText DisplayName;

	FString OldValue;
	FString NewValue;

	FString TypeName;

	int64 OldOffset = INDEX_NONE;
	int64 NewOffset = INDEX_NONE;

	int64 OldSize = 0;
	int64 NewSize = 0;

	int32 OldExportIndex = INDEX_NONE;
	int32 NewExportIndex = INDEX_NONE;

	int64 ChangedByteCount = 0;

	TArray<FAssetByteDiffSpan> ChangedSpans;

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
	static FAssetPackageDiffResult Compare(const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument, const FAssetPackageTraceCollection* OldTraces = nullptr,
		const FAssetPackageTraceCollection* NewTraces = nullptr);

	static void BuildPropertyDiffs(
		const FAssetSerializationTrace* OldTrace, const FAssetSerializationTrace* NewTrace, const TArray<FAssetByteDiffSpan>& ChangedSpans, FAssetPackageDiffEntry& PayloadEntry);

	static FString BuildTracePath(const FAssetSerializationTraceNode* Node);
};