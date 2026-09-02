// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

struct FAssetPackageDocument;
struct FAssetSerializationTraceNode;

struct FAssetDecodedPropertyValue
{
	bool bSuccess = false;

	FString Value;

	FString Error;
};

class FAssetPropertyValueDecoder
{
public:
	static FAssetDecodedPropertyValue Decode(const FAssetPackageDocument& Document, const FAssetSerializationTraceNode& Node, int64 ExportSerialOffset);
};