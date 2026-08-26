// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

class FAssetPackagePayloadReader;

struct FAssetPackageDocument;
struct FAssetSerializedPropertyTag;

class FAssetPropertyTagDecoder
{
public:
	static bool ReadTag(const FAssetPackageDocument& Document, FAssetPackagePayloadReader& Reader, FAssetSerializedPropertyTag& OutTag, FText& OutError);
};