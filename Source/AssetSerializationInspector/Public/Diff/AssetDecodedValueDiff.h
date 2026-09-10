// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

struct FAssetDecodedPropertyValue;

enum class EAssetDecodedValueDiffState : uint8
{
	Unchanged,
	Added,
	Removed,
	Modified,
	Moved
};

struct FAssetDecodedValueDiff
{
	EAssetDecodedValueDiffState State = EAssetDecodedValueDiffState::Unchanged;

	FString Name;
	FString TypeName;

	FString OldValue;
	FString NewValue;

	FString SemanticKey;

	bool bHasOldValue = false;
	bool bHasNewValue = false;

	int64 OldOffset = INDEX_NONE;
	int64 NewOffset = INDEX_NONE;

	int64 OldSize = 0;
	int64 NewSize = 0;

	int32 OldArrayIndex = INDEX_NONE;
	int32 NewArrayIndex = INDEX_NONE;

	TArray<FAssetDecodedValueDiff> Children;
};

class FAssetDecodedValueDiffer
{
public:
	static FAssetDecodedValueDiff Compare(const FAssetDecodedPropertyValue* OldValue, const FAssetDecodedPropertyValue* NewValue);
};
