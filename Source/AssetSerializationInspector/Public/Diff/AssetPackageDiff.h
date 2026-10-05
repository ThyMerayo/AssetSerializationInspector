// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

class FAssetArchetypeResolver;
struct FAssetDecodedValueDiff;
struct FAssetPackageDocument;
struct FAssetPackageExportEntry;
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
	Header,
	HeaderRegion,
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

enum class EAssetSerializedPropertyPresence : uint8
{
	Present,
	NotSerialized
};

struct FAssetPackageDiffEntry
{
	EAssetPackageDiffKind Kind = EAssetPackageDiffKind::File;
	EAssetPackageDiffState State = EAssetPackageDiffState::Unchanged;

	FString Key;
	FString SemanticPath;
	FText DisplayName;

	FString OldValue;
	FString NewValue;

	FString OldDecodedValue;
	FString NewDecodedValue;

	bool bHasOldDecodedValue = false;
	bool bHasNewDecodedValue = false;

	/** For sets and maps stored as deltas: the contents after applying them to the archetype's value. Empty when unknown. */
	FString OldFinalValue;
	FString NewFinalValue;

	/** Caveats about the final values, such as an assumed empty default. Empty when the final value is certain. */
	FString OldFinalValueNote;
	FString NewFinalValueNote;

	FString OldFieldPath;
	FString NewFieldPath;

	FString TypeName;

	/** Why the entry differs, when that can be said: a consequence of another change, an expected effect of saving, and so on. */
	FText Explanation;

	int64 OldOffset = INDEX_NONE;
	int64 NewOffset = INDEX_NONE;

	int64 OldSize = 0;
	int64 NewSize = 0;

	int32 OldExportIndex = INDEX_NONE;
	int32 NewExportIndex = INDEX_NONE;

	EAssetSerializedPropertyPresence OldPresence = EAssetSerializedPropertyPresence::Present;
	EAssetSerializedPropertyPresence NewPresence = EAssetSerializedPropertyPresence::Present;

	int64 ChangedByteCount = 0;

	/**
	 * For a native range of a class or function (UnknownPayloadRange): the data was read on both sides to its last byte, and the
	 * children are what differs in it, so the changed bytes are accounted for.
	 */
	bool bNativeDataDecoded = false;

	/**
	 * A property whose bytes differ although its decoded value is identical on both sides: a map or set saved with its entries in
	 * another order (they are written in the order of an internal hash table, which can change between sessions).
	 */
	bool bRepresentationOnly = false;

	TArray<FAssetByteDiffSpan> ChangedSpans;

	/**
	 * Parts of the selected range, relative to its start, that hold an absolute file offset which only moved with the data in
	 * front of it. The hex view shows differences inside them apart from real changes.
	 */
	TArray<FAssetByteDiffSpan> ShiftedOffsetRanges;
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

struct FAssetAttributedByteDiffSpan
{
	int64 Offset = 0;
	int64 Size = 0;

	const FAssetSerializationTraceNode* OldNode = nullptr;
	const FAssetSerializationTraceNode* NewNode = nullptr;
};

struct FPropertyDiffData
{
	const FAssetPackageDocument& Document;
	const FAssetPackageExportEntry& Export;
	const FAssetSerializationTrace* Trace;
	FAssetArchetypeResolver* ArchetypeResolver = nullptr;
};

namespace AssetPackageDiff
{
	/**
	 * Adds the "Package header" entry: every field of the package summary, and each region of the header (the summary and the
	 * tables it locates) with its offset, size and byte-level differences, so a changed header can be inspected like a payload.
	 */
	void AppendHeaderDiff(const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument, FAssetPackageDiffResult& Result);

	FAssetPackageDiffResult Compare(const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument, const FAssetPackageTraceCollection* OldTraces = nullptr,
		const FAssetPackageTraceCollection* NewTraces = nullptr);

	FString BuildTracePath(const FAssetSerializationTraceNode* Node);

	FString AppendSemanticPath(const FString& ParentPath, const FString& Segment);
}; // namespace AssetPackageDiff
