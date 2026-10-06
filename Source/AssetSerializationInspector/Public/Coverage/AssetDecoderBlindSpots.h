// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

/**
 * A kind of value whose bytes the decoder reads without keeping what they say: changing a byte inside it leaves the decoded value
 * as it was, so a difference there would not show in the diff.
 */
struct FAssetDecoderBlindSpot
{
	/** The type of the value that holds the byte, such as "StructProperty(EdGraphPinType(/Script/Engine))". */
	FString TypeName;

	/** How many bytes of this type were changed one at a time, and how many of those changed nothing in the decoded value. */
	int64 BytesTested = 0;
	int64 BlindBytes = 0;

	int32 AssetCount = 0;

	/** A few places that show it: asset, property and the offsets inside the property's value. */
	TArray<FString> Examples;
};

struct FAssetDecoderBlindSpotResult
{
	FString Folder;
	int32 AssetsScanned = 0;
	int32 PropertiesTested = 0;
	int64 BytesTested = 0;
	int64 BlindBytes = 0;
	bool bCancelled = false;

	/** The types with blind bytes, the most blind bytes first. */
	TArray<FAssetDecoderBlindSpot> Spots;
};

namespace AssetDecoderBlindSpots
{
	/**
	 * Finds the values the decoder reads without keeping what they say. For the top-level tagged properties of the .uasset files under
	 * a folder that decode completely, changes each byte of the value in turn, decodes it again, and counts the bytes whose change
	 * left the decoded value as it was (it is a value that is stored but not shown).
	 *
	 * @param MaximumPropertyBytes Properties with a larger value are skipped, to bound the time.
	 * @param MaximumPerType How many properties of one type are tested.
	 * @param ShouldContinue Called before each file; return false to stop.
	 */
	FAssetDecoderBlindSpotResult Run(const FString& Folder, int32 MaximumPropertyBytes, int32 MaximumPerType, TFunctionRef<bool(int32 Index, int32 Total)> ShouldContinue);

	FString ToText(const FAssetDecoderBlindSpotResult& Result);
} // namespace AssetDecoderBlindSpots
