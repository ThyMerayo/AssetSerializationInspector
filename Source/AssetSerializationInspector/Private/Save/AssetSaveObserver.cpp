// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Save/AssetSaveObserver.h"

#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"

#include "AssetSerializationInspectorSettings.h"
#include "Save/AssetSaveHistoryManager.h"
#include "Trace/AssetPackageFieldDecoder.h"

FAssetSaveObserver& FAssetSaveObserver::Get()
{
	static FAssetSaveObserver Instance;
	return Instance;
}

void FAssetSaveObserver::Startup()
{
	PreSaveHandle = UPackage::PreSavePackageWithContextEvent.AddRaw(this, &FAssetSaveObserver::HandlePreSavePackage);
	PostSaveHandle = UPackage::PackageSavedWithContextEvent.AddRaw(this, &FAssetSaveObserver::HandlePackageSaved);
}

void FAssetSaveObserver::Shutdown()
{
	UPackage::PreSavePackageWithContextEvent.Remove(PreSaveHandle);
	UPackage::PackageSavedWithContextEvent.Remove(PostSaveHandle);

	PendingSaves.Reset();
}

FString FAssetSaveObserver::MakeSnapshotFilename(const FString& SourceFilename) const
{
	const FString SnapshotDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AssetSerializationInspector"), TEXT("Snapshots"));
	IFileManager::Get().MakeDirectory(*SnapshotDirectory, true);
	const FString BaseName = FPaths::GetBaseFilename(SourceFilename);

	return FPaths::Combine(SnapshotDirectory, BaseName + TEXT("_Before.uasset"));
}

void FAssetSaveObserver::HandlePreSavePackage(UPackage* Package, FObjectPreSaveContext SaveContext)
{
	if (Package == nullptr)
	{
		return;
	}

	const FName PackageName = Package->GetFName();

	if (!FAssetMonitoringManager::Get().IsMonitored(PackageName))
	{
		return;
	}

	// The pre-save delegate may occur more than once as part of a save.
	// Don't overwrite our original "before" snapshot.
	if (PendingSaves.Contains(PackageName))
	{
		return;
	}

	const FString LongPackageName = Package->GetName();
	const FString SourceFilename = FPackageName::LongPackageNameToFilename(LongPackageName, FPackageName::GetAssetPackageExtension());

	FAssetSaveSnapshot Snapshot;

	Snapshot.PackageName = PackageName;
	Snapshot.OriginalFilename = SourceFilename;

	if (FPaths::FileExists(SourceFilename))
	{
		Snapshot.BeforeFilename = MakeSnapshotFilename(SourceFilename);
		const uint32 CopyResult = IFileManager::Get().Copy(*Snapshot.BeforeFilename, *SourceFilename, true, true);
		Snapshot.bHadPreviousFile = CopyResult == COPY_OK;
	}

	PendingSaves.Add(PackageName, MoveTemp(Snapshot));
}

static EObservedSaveChangeKind ClassifySave(const FAssetPackageDiffResult& Diff)
{
	if (Diff.bFilesIdentical)
	{
		return EObservedSaveChangeKind::Identical;
	}

	bool bHasPayloadChange = false;
	bool bHasTableChange = false;
	bool bOnlyMoved = true;

	TFunction<void(const FAssetPackageDiffEntry&)> Visit;

	Visit = [&](const FAssetPackageDiffEntry& Entry) {
		if (Entry.State == EAssetPackageDiffState::Modified)
		{
			bOnlyMoved = false;
		}

		if (Entry.Kind == EAssetPackageDiffKind::ExportPayload && Entry.State == EAssetPackageDiffState::Modified)
		{
			bHasPayloadChange = true;
		}

		if ((Entry.Kind == EAssetPackageDiffKind::Name || Entry.Kind == EAssetPackageDiffKind::Import || Entry.Kind == EAssetPackageDiffKind::Export)
			&& Entry.State != EAssetPackageDiffState::Unchanged)
		{
			bHasTableChange = true;
		}

		for (const auto& Child : Entry.Children)
		{
			Visit(Child);
		}
	};

	for (const auto& Entry : Diff.Entries)
	{
		Visit(Entry);
	}

	if (bHasPayloadChange)
	{
		return EObservedSaveChangeKind::PayloadChange;
	}

	if (bOnlyMoved)
	{
		return EObservedSaveChangeKind::LayoutOnly;
	}

	if (bHasTableChange)
	{
		return EObservedSaveChangeKind::TableChange;
	}

	return EObservedSaveChangeKind::MetadataOnly;
}

void FAssetSaveObserver::HandlePackageSaved(const FString& PackageFilename, UPackage* Package, FObjectPostSaveContext SaveContext)
{
	if (Package == nullptr)
	{
		return;
	}

	const FName PackageName = Package->GetFName();

	if (!FAssetMonitoringManager::Get().IsMonitored(PackageName))
	{
		return;
	}

	FAssetSaveSnapshot* Snapshot = PendingSaves.Find(PackageName);

	if (Snapshot == nullptr)
	{
		return;
	}

	TSharedPtr<FObservedAssetSave> Save = MakeShared<FObservedAssetSave>();
	Save->SaveId = NextSaveId++;
	Save->PackageName = PackageName;

	FText Error;

	if (Snapshot->bHadPreviousFile)
	{
		Save->Before = FAssetPackageReader::LoadFromFile(Snapshot->BeforeFilename, Error);
	}

	Save->After = FAssetPackageReader::LoadFromFile(PackageFilename, Error);

	// We'll implement these below.
	if (Save->Before.IsValid())
	{
		Save->BeforeFields = FAssetPackageFieldDecoder::Decode(*Save->Before);
	}

	if (Save->After.IsValid())
	{
		Save->AfterFields = FAssetPackageFieldDecoder::Decode(*Save->After);
	}

	if (Save->Before.IsValid() && Save->After.IsValid())
	{
		Save->Diff = FAssetPackageDiff::Compare(*Save->Before, *Save->After, Save->BeforeFields.Get(), Save->AfterFields.Get());
		Save->Analysis = FAssetSaveAnalyzer::Analyze(Save->Diff, *Save->Before, *Save->After);
	}

	PendingSaves.Remove(PackageName);
	Save->Timestamp = FDateTime::Now();
	Save->ChangeKind = ClassifySave(Save->Diff);
	ObservedSaves.Add(PackageName, Save);
	RecentSaves.Insert(Save, 0);
	FAssetSaveHistoryManager::Get().RecordSave(Save);

	if (RecentSaves.Num() > MaxRecentSaves)
	{
		RecentSaves.SetNum(MaxRecentSaves);
	}

	ObservedAssetSaveEvent.Broadcast(Save);
}

FAssetMonitoringManager& FAssetMonitoringManager::Get()
{
	static FAssetMonitoringManager Instance;
	return Instance;
}

FAssetMonitoringManager::FAssetMonitoringManager()
{
	const UAssetSerializationInspectorSettings* Settings = GetDefault<UAssetSerializationInspectorSettings>();

	for (const FName PackageName : Settings->MonitoredPackages)
	{
		MonitoredPackages.Add(PackageName);
	}
}

bool FAssetMonitoringManager::IsMonitored(const FName PackageName) const
{
	return MonitoredPackages.Contains(PackageName);
}

void FAssetMonitoringManager::AddMonitoredAsset(const FName PackageName)
{
	MonitoredPackages.Add(PackageName);

	UAssetSerializationInspectorSettings* Settings = GetMutableDefault<UAssetSerializationInspectorSettings>();
	Settings->MonitoredPackages.Add(PackageName);
	Settings->SaveConfig();
}

void FAssetMonitoringManager::RemoveMonitoredAsset(const FName PackageName)
{
	MonitoredPackages.Remove(PackageName);

	UAssetSerializationInspectorSettings* Settings = GetMutableDefault<UAssetSerializationInspectorSettings>();
	Settings->MonitoredPackages.Remove(PackageName);
	Settings->SaveConfig();
}