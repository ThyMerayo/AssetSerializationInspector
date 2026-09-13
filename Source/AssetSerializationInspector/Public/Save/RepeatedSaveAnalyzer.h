// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Save/AssetSaveAnalyzer.h"

struct FAssetSaveHistory;

enum class EObservedValuePattern : uint8
{
	Unknown,
	Stable,
	ChangedOnce,
	Recurring,
	ChangedEverySave,
	ContinuouslyChanging,
	Alternating
};

using FObservedSaveId = uint64;

struct FObservedPropertySample
{
	FObservedSaveId SaveId = 0;

	FDateTime Timestamp;

	bool bChanged = false;

	bool bHasOldValue = false;
	bool bHasNewValue = false;

	FString OldValue;
	FString NewValue;

	int64 ChangedByteCount = 0;

	EAssetSaveChangeClassification Classification = EAssetSaveChangeClassification::Unknown;
};

struct FRepeatedSavePattern
{
	FString SemanticPath;
	FText DisplayName;

	int32 ObservationCount = 0;
	int32 ChangeCount = 0;

	int64 TotalChangedBytes = 0;

	EObservedValuePattern ValuePattern = EObservedValuePattern::Unknown;

	TArray<FObservedPropertySample> Samples;
};

struct FObservedPropertyState
{
	FString SemanticPath;
	FText DisplayName;

	bool bHasValue = false;
	FString Value;
};

class FRepeatedSaveAnalyzer
{
public:
	static TArray<FRepeatedSavePattern> Analyze(const FAssetSaveHistory& History);
};