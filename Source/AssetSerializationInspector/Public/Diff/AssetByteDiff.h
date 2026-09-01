// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "AssetPackageDiff.h"

struct FAssetPackageDocument;

class FAssetByteDiff
{
public:
	static TArray<FAssetByteDiffSpan> Compare(const FAssetPackageDocument& OldDocument, int64 OldOffset, int64 OldSize, const FAssetPackageDocument& NewDocument, int64 NewOffset, int64 NewSize);
};
