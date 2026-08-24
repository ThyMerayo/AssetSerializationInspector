// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Readers/AssetPackageMemoryReader.h"

struct FAssetPackageDocument;

class FAssetPackagePayloadReader : public FAssetPackageMemoryReader
{
public:
	FAssetPackagePayloadReader(const FAssetPackageDocument& InDocument, int64 InOffset, int64 InSize);

	virtual FArchive& operator<<(FName& Value) override;

private:
	const FAssetPackageDocument& Document;
};