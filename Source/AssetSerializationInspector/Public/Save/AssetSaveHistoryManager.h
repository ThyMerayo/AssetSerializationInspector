// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Save/AssetSaveAnalyzer.h"
#include "Save/RepeatedSaveAnalyzer.h"

struct FObservedAssetSave;

using FObservedSaveId = uint64;

struct FObservedSaveHistoryEntry
{
	FObservedSaveId SaveId = 0;

	FDateTime Timestamp;

	EAssetSaveResultKind ResultKind = EAssetSaveResultKind::Identical;

	TArray<FAssetSaveExplanationEntry> SemanticChanges;
	TArray<FAssetSaveExplanationEntry> LayoutChanges;
	TArray<FAssetSaveExplanationEntry> UnexplainedChanges;

	TMap<FString, FObservedPropertyState> PropertyStates;
};

struct FAssetSaveHistory
{
	FName PackageName;

	TArray<FObservedSaveHistoryEntry> Entries;
};

class FAssetSaveHistoryManager
{
public:
	static FAssetSaveHistoryManager& Get();

	void RecordSave(const TSharedPtr<FObservedAssetSave>& Save);
	const FAssetSaveHistory* FindHistory(FName PackageName) const;
	TSharedPtr<const FObservedAssetSave> FindSave(FObservedSaveId SaveId) const;

private:
	static constexpr int32 MaxHistoryPerAsset = 50;

	TMap<FName, FAssetSaveHistory> Histories;
	TMap<FObservedSaveId, TSharedPtr<FObservedAssetSave>> SavesById;
};