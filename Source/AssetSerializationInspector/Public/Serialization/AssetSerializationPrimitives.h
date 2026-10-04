// Copyright Diego Merayo Merayo. All Rights Reserved

#include "CoreMinimal.h"

class FAssetPackageMemoryReader;

namespace AssetSerializationPrimitives
{
	bool ReadSerializedString(FAssetPackageMemoryReader& Reader, FString& OutString, FText& OutError);

	/**
	 * Reads a string the way FUtf8String is saved: a length, then UTF-8 bytes (or UTF-16 when the length is negative). Unlike
	 * ReadSerializedString the terminator is optional, and trailing NULs are dropped, as the engine does for soft object path sub paths.
	 */
	bool ReadUtf8SerializedString(FAssetPackageMemoryReader& Reader, FString& OutString, FText& OutError);
} // namespace AssetSerializationPrimitives
