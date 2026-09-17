// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Save/AssetSaveHistoryManager.h"

#include "Save/AssetSaveObserver.h"

FAssetSaveHistoryManager& FAssetSaveHistoryManager::Get()
{
	static FAssetSaveHistoryManager Instance;
	return Instance;
}

static void CollectPropertyStates(const FAssetPackageDiffEntry& Entry, const FString& ParentPath, TMap<FString, FObservedPropertyState>& OutStates)
{
	FString Path = ParentPath;

	if (!Entry.Key.IsEmpty())
	{
		Path = ParentPath.IsEmpty() ? Entry.Key : AssetPackageDiff::AppendSemanticPath(ParentPath, Entry.Key);
	}

	if (Entry.Kind == EAssetPackageDiffKind::Property)
	{
		FObservedPropertyState State;
		State.SemanticPath = Path;
		State.DisplayName = Entry.DisplayName;

		if (Entry.bHasNewDecodedValue)
		{
			State.bHasValue = true;
			State.Value = Entry.NewDecodedValue;
		}

		OutStates.Add(Path, MoveTemp(State));
	}

	for (const FAssetPackageDiffEntry& Child : Entry.Children)
	{
		CollectPropertyStates(Child, Path, OutStates);
	}
}

void FAssetSaveHistoryManager::RecordSave(const TSharedPtr<FObservedAssetSave>& Save)
{
	if (!Save.IsValid())
	{
		return;
	}

	FAssetSaveHistory& History = Histories.FindOrAdd(Save->PackageName);
	History.PackageName = Save->PackageName;

	FObservedSaveHistoryEntry Entry;
	Entry.SaveId = Save->SaveId;
	Entry.Timestamp = Save->Timestamp;
	Entry.ResultKind = Save->Analysis.ResultKind;
	Entry.SemanticChanges = Save->Analysis.SemanticChanges;
	Entry.LayoutChanges = Save->Analysis.LayoutChanges;
	Entry.UnexplainedChanges = Save->Analysis.UnexplainedChanges;

	// Populate PropertyStates, etc.
	for (const FAssetPackageDiffEntry& Root : Save->Diff.Entries)
	{
		CollectPropertyStates(Root, FString(), Entry.PropertyStates);
	}

	History.Entries.Add(MoveTemp(Entry));
	SavesById.Add(Save->SaveId, Save);

	while (History.Entries.Num() > MaxHistoryPerAsset)
	{
		const FObservedSaveId RemovedSaveId = History.Entries[0].SaveId;
		History.Entries.RemoveAt(0);
		SavesById.Remove(RemovedSaveId);
	}
}

const FAssetSaveHistory* FAssetSaveHistoryManager::FindHistory(FName PackageName) const
{
	return Histories.Find(PackageName);
}

TSharedPtr<const FObservedAssetSave> FAssetSaveHistoryManager::FindSave(const FObservedSaveId SaveId) const
{
	const TSharedPtr<FObservedAssetSave>* Found = SavesById.Find(SaveId);
	return Found != nullptr ? *Found : nullptr;
}