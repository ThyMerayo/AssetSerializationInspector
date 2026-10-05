// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "AssetSerializationTraceArchive.h"
#include "Serialization/AssetSerializedPropertyTag.h"

enum class EAssetSerializationTraceKind : uint8
{
	Object,
	Property,
	Struct,
	Array,
	Map,
	Set,
	Native,
	Unknown
};

struct FAssetSerializationTraceNode
{
	EAssetSerializationTraceKind Kind = EAssetSerializationTraceKind::Unknown;

	FString Name;
	FString TypeName;

	FAssetSerializedPropertyType PropertyType;

	int64 Offset = 0;
	int64 Size = 0;
	int32 ArrayIndex = 0;

	bool bHasInlineBoolValue = false;

	/** The value is zero in a package saved without tags: no bytes are stored (Size is 0) and the type says what zero is. */
	bool bIsZeroValue = false;

	/** True when the property tag says the value is written by the type's own serializer rather than as tagged properties. */
	bool bBinaryOrNative = false;
	bool bInlineBoolValue = false;

	TArray<TSharedPtr<FAssetSerializationTraceNode>> Children;

	TWeakPtr<FAssetSerializationTraceNode> Parent;

	int64 EndOffset() const { return Offset + Size; }

	bool ContainsOffset(const int64 InOffset) const { return InOffset >= Offset && InOffset < EndOffset(); }

	bool Overlaps(const int64 InOffset, const int64 InSize) const
	{
		if (InSize <= 0 || Size <= 0)
		{
			return false;
		}

		const int64 AEnd = Offset + Size;
		const int64 BEnd = InOffset + InSize;

		return Offset < BEnd && InOffset < AEnd;
	}
};

struct FAssetSerializationTrace
{
	FString ObjectPath;

	int64 PayloadOffset = 0;
	int64 PayloadSize = 0;

	TSharedPtr<FAssetSerializationTraceNode> Root;
};

struct FAssetPackageTraceCollection
{
	TMap<int32, FAssetSerializationTrace> ExportTraces;

	const FAssetSerializationTrace* FindExportTrace(const int32 ExportIndex) const
	{
		if (ExportIndex == INDEX_NONE)
		{
			return nullptr;
		}

		return ExportTraces.Find(ExportIndex);
	}
};

struct FAssetByteRange
{
	int64 Offset = 0;
	int64 Size = 0;

	int64 End() const { return Offset + Size; }
};

namespace AssetSerializationTrace
{
	const FAssetSerializationTraceNode* FindDeepestTraceNode(const TSharedPtr<FAssetSerializationTraceNode>& Root, int64 Offset, int64 Size);
	FAssetSerializationTrace BuildSerializationTrace(const UObject* Object, int64 PayloadSize, const TArray<FAssetSerializationTraceEvent>& Events);

	const FAssetSerializationTraceNode* FindDeepestFieldTraceNode(const TSharedPtr<FAssetSerializationTraceNode>& Root, const int64 Offset, const int64 Size);

	bool IntersectRanges(const int64 AOffset, const int64 ASize, const int64 BOffset, const int64 BSize, int64& OutOffset, int64& OutSize);
	void FindDeepestOverlappingFieldNodes(const TSharedPtr<FAssetSerializationTraceNode>& Node, const int64 Offset, const int64 Size, TArray<const FAssetSerializationTraceNode*>& OutNodes);
} // namespace AssetSerializationTrace