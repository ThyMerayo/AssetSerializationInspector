// Copyright Diego Merayo Merayo. All Rights Reserved

#include "CoreMinimal.h"

class FAssetPackageMemoryReader;

namespace AssetSerializationPrimitives
{
	bool ReadSerializedString(FAssetPackageMemoryReader& Reader, FString& OutString, FText& OutError);
}
