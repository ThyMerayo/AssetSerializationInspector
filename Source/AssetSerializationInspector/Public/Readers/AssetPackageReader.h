// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

struct FAssetPackageDocument;

class FAssetPackageReader
{
public:
	static TSharedPtr<FAssetPackageDocument> LoadFromFile(const FString& Filename, FText& OutError);
};