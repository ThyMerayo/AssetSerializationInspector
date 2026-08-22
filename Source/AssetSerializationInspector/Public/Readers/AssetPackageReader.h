// Copyright Diego Merayo Merayo. All Rights Reserved
#pragma once

#include "CoreMinimal.h"

struct FAssetPackageDocument;

class FAssetPackageReader
{
public:
	static int64 FindNameMapEnd(const FAssetPackageDocument& Document);

	static TSharedPtr<FAssetPackageDocument> LoadFromFile(const FString& Filename, FText& OutError);
};