// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

struct FAssetPackageDocument;
struct FAssetSerializationTraceNode;

enum class EAssetPropertyDecodeStatus : uint8
{
	Success,
	Unsupported,
	InvalidData
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
