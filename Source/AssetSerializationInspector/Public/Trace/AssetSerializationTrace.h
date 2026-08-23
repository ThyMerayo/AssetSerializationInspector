// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

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

	int64 Offset = 0;
	int64 Size = 0;

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

namespace AssetSerializationTrace
{
	const FAssetSerializationTraceNode* FindDeepestTraceNode(const TSharedPtr<FAssetSerializationTraceNode>& Root, int64 Offset, int64 Size);

} // namespace AssetSerializationTrace