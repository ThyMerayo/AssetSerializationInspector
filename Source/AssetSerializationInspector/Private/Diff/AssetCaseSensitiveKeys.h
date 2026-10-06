// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

/**
 * Key functions for maps and sets of strings that tell "B" from "b". FString compares without regard to case, so a TMap<FString, ...> or
 * a TSet<FString> holds one entry for both, which is not what a name map, or the keys of a map property, mean.
 */
template <typename ValueType> struct TCaseSensitiveStringMapKeyFuncs : BaseKeyFuncs<TPair<FString, ValueType>, FString, false>
{
	using KeyInitType = const FString&;
	using ElementInitType = const TPair<FString, ValueType>&;

	static KeyInitType GetSetKey(ElementInitType Element) { return Element.Key; }
	static bool Matches(KeyInitType A, KeyInitType B) { return A.Equals(B, ESearchCase::CaseSensitive); }
	static uint32 GetKeyHash(KeyInitType Key) { return GetTypeHash(Key); }
};

struct FCaseSensitiveStringSetKeyFuncs : BaseKeyFuncs<FString, FString, false>
{
	using KeyInitType = const FString&;
	using ElementInitType = const FString&;

	static KeyInitType GetSetKey(ElementInitType Element) { return Element; }
	static bool Matches(KeyInitType A, KeyInitType B) { return A.Equals(B, ESearchCase::CaseSensitive); }
	static uint32 GetKeyHash(KeyInitType Key) { return GetTypeHash(Key); }
};
