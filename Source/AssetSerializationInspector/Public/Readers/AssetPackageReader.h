// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

struct FAssetPackageDocument;

class FAssetPackageReader
{
public:
	/**
	 * Loads the file exactly as stored on disk.
	 *
	 * This function does not deserialize the package and does not load
	 * any UObject contained by it.
	 */
	static TSharedPtr<FAssetPackageDocument> LoadFromFile(const FString& Filename, FText& OutError);
};