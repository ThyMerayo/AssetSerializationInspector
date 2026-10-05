// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

struct FAssetPackageDocument;
struct FAssetSerializationTraceNode;

enum class EAssetPropertyDecodeStatus : uint8
{
	Success,
	Unsupported,
	InvalidData,

	/**
	 * An array some of whose elements could not be decoded. The elements before the first failure are in Children (the element that
	 * failed is the last child, with its own error), and Value says how many decoded. The elements after it are not read, because an
	 * element that fails leaves the reader at an unknown position.
	 */
	Partial
};

enum class EAssetDecodedValueKind : uint8
{
	Scalar,
	Struct,
	Array,
	Set,
	Map,
	MapEntry
};

enum class EAssetDecodedContainerOperation : uint8
{
	None,

	Add,
	Remove,
	AddOrModify,
	Replace,
	Modify,
	Shadow
};

enum class EAssetDecodedContainerSerializationMode : uint8
{
	Unknown,
	Full,
	Delta
};

struct FAssetDecodedPropertyValue
{
	EAssetPropertyDecodeStatus Status = EAssetPropertyDecodeStatus::Unsupported;
	EAssetDecodedValueKind Kind = EAssetDecodedValueKind::Scalar;
	EAssetDecodedContainerOperation ContainerOperation = EAssetDecodedContainerOperation::None;
	EAssetDecodedContainerSerializationMode ContainerMode = EAssetDecodedContainerSerializationMode::Unknown;

	FString Name;
	FString TypeName;
	FString Value;
	FString Error;
	FString SemanticKey;

	int64 AbsoluteOffset = 0;
	int64 Size = 0;

	TArray<FAssetDecodedPropertyValue> Children;

	bool IsSuccess() const { return Status == EAssetPropertyDecodeStatus::Success; }

	/** Whether the children that decoded can be shown and compared: a complete value, or an array that decoded in part. */
	bool HasDecodedChildren() const { return Status == EAssetPropertyDecodeStatus::Success || Status == EAssetPropertyDecodeStatus::Partial; }

	/** How many leading elements decoded: all of them for a complete array, those before the first failure for a partial one. */
	int32 CountDecodedElements() const
	{
		int32 Count = 0;
		for (const FAssetDecodedPropertyValue& Child : Children)
		{
			if (!Child.IsSuccess())
			{
				break;
			}
			++Count;
		}
		return Count;
	}
};

class FAssetPropertyValueDecoder
{
public:
	static FAssetDecodedPropertyValue Decode(const FAssetPackageDocument& Document, const FAssetSerializationTraceNode& Node, int64 ExportSerialOffset);

	static FString BuildSemanticValueKey(const FAssetDecodedPropertyValue& Value);

	/**
	 * A single-line summary of a decoded value for display: scalars as-is, structs as {Name=Value, ...}, and containers as
	 * "<count>: element, element, ...". Long containers are cut after MaximumElements entries and deep values are summarized.
	 */
	static FString FormatForDisplay(const FAssetDecodedPropertyValue& Value, int32 MaximumElements = 8);
};
