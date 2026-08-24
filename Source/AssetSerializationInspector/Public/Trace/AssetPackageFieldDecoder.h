// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

struct FAssetPackageTraceCollection;

class FAssetPackageFieldDecoder
{
public:
	static TSharedPtr<FAssetPackageTraceCollection> Decode(const FAssetPackageDocument& Document);
};