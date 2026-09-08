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
	Map
};

struct FAssetDecodedPropertyValue
{
	EAssetPropertyDecodeStatus Status = EAssetPropertyDecodeStatus::Unsupported;
	EAssetDecodedValueKind Kind = EAssetDecodedValueKind::Scalar;

	FString Name;
	FString TypeName;
	FString Value;
	FString Error;
	FString SemanticKey;

	int64 RelativeOffset = 0;
	int64 Size = 0;

	TArray<FAssetDecodedPropertyValue> Children;

	bool IsSuccess() const { return Status == EAssetPropertyDecodeStatus::Success; }
};

class FAssetPropertyValueDecoder
{
public:
	static FAssetDecodedPropertyValue Decode(const FAssetPackageDocument& Document, const FAssetSerializationTraceNode& Node, int64 ExportSerialOffset);
};