// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Save/AssetSaveObserver.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/CoreDelegates.h"
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
	if (Package == nullptr || SuppressionCount > 0)
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

TSharedPtr<FObservedAssetSave> FAssetSaveObserver::BuildObservedSave(const FName PackageName, const FString& BeforeFilename, const FString& AfterFilename)
{
	TSharedPtr<FObservedAssetSave> Save = MakeShared<FObservedAssetSave>();
	Save->SaveId = NextSaveId++;
	Save->PackageName = PackageName;
	Save->BeforeFilename = BeforeFilename;
	Save->AfterFilename = AfterFilename;

	FText Error;

	if (!BeforeFilename.IsEmpty())
	{
		Save->Before = FAssetPackageReader::LoadFromFile(BeforeFilename, Error);
	}

	Save->After = FAssetPackageReader::LoadFromFile(AfterFilename, Error);

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
		Save->Diff = AssetPackageDiff::Compare(*Save->Before, *Save->After, Save->BeforeFields.Get(), Save->AfterFields.Get());
		Save->Analysis = FAssetSaveAnalyzer::Analyze(Save->Diff, *Save->Before, *Save->After);
	}

	Save->Timestamp = FDateTime::Now();
	Save->ChangeKind = ClassifySave(Save->Diff);

	return Save;
}

void FAssetSaveObserver::HandlePackageSaved(const FString& PackageFilename, UPackage* Package, FObjectPostSaveContext SaveContext)
{
	if (Package == nullptr || SuppressionCount > 0)
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

	const TSharedPtr<FObservedAssetSave> Save = BuildObservedSave(PackageName, Snapshot->bHadPreviousFile ? Snapshot->BeforeFilename : FString(), PackageFilename);

	PendingSaves.Remove(PackageName);
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

FAssetMonitoringManager::FAssetMonitoringManager(const bool bInPersistent) : bPersistent(bInPersistent)
{
	if (!bPersistent)
	{
		return;
	}

	ReloadFromSettings();

	// Edits made to the setting (the editor preferences, or a reloaded config) change what is monitored at once.
	SettingsChangedHandle = GetMutableDefault<UAssetSerializationInspectorSettings>()->OnSettingChanged().AddLambda([this](UObject*, FPropertyChangedEvent&) { ReloadFromSettings(); });

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetRenamedHandle = AssetRegistry.OnAssetRenamed().AddRaw(this, &FAssetMonitoringManager::OnRegistryAssetRenamed);
	AssetRemovedHandle = AssetRegistry.OnAssetRemoved().AddRaw(this, &FAssetMonitoringManager::OnRegistryAssetRemoved);
}

FAssetMonitoringManager::~FAssetMonitoringManager()
{
	// The shared instance is destroyed at process exit, when the object system and the asset registry may already be gone.
	if (!bPersistent || IsEngineExitRequested() || !UObjectInitialized())
	{
		return;
	}

	if (UAssetSerializationInspectorSettings* Settings = GetMutableDefault<UAssetSerializationInspectorSettings>())
	{
		Settings->OnSettingChanged().Remove(SettingsChangedHandle);
	}

	if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
	{
		IAssetRegistry& AssetRegistry = FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		AssetRegistry.OnAssetRenamed().Remove(AssetRenamedHandle);
		AssetRegistry.OnAssetRemoved().Remove(AssetRemovedHandle);
	}
}

bool FAssetMonitoringManager::IsMonitored(const FName PackageName) const
{
	return MonitoredPackages.Contains(PackageName);
}

void FAssetMonitoringManager::AddMonitoredAsset(const FName PackageName)
{
	bool bAlreadyMonitored = false;
	MonitoredPackages.Add(PackageName, &bAlreadyMonitored);

	if (!bAlreadyMonitored)
	{
		SaveToSettings();
	}
}

void FAssetMonitoringManager::RemoveMonitoredAsset(const FName PackageName)
{
	if (MonitoredPackages.Remove(PackageName) > 0)
	{
		SaveToSettings();
	}
}

void FAssetMonitoringManager::SetMonitoredAssets(const TArray<FName>& PackageNames)
{
	MonitoredPackages.Reset();
	MonitoredPackages.Append(PackageNames);
}

void FAssetMonitoringManager::HandleAssetRenamed(const FName OldPackageName, const FName NewPackageName)
{
	if (OldPackageName == NewPackageName || !MonitoredPackages.Contains(OldPackageName))
	{
		return;
	}

	MonitoredPackages.Remove(OldPackageName);
	MonitoredPackages.Add(NewPackageName);
	SaveToSettings();
}

void FAssetMonitoringManager::HandleAssetRemoved(const FName PackageName)
{
	RemoveMonitoredAsset(PackageName);
}

void FAssetMonitoringManager::SaveToSettings() const
{
	if (!bPersistent)
	{
		return;
	}

	UAssetSerializationInspectorSettings* Settings = GetMutableDefault<UAssetSerializationInspectorSettings>();

	// Writing the list triggers no change notification of its own, so the manager's set is not reloaded under it.
	Settings->MonitoredPackages = MonitoredPackages.Array();
	Settings->MonitoredPackages.Sort(FNameLexicalLess());
	Settings->SaveConfig();
}

void FAssetMonitoringManager::ReloadFromSettings()
{
	SetMonitoredAssets(GetDefault<UAssetSerializationInspectorSettings>()->MonitoredPackages);
}

void FAssetMonitoringManager::OnRegistryAssetRenamed(const FAssetData& NewAsset, const FString& OldObjectPath)
{
	HandleAssetRenamed(FName(*FPackageName::ObjectPathToPackageName(OldObjectPath)), NewAsset.PackageName);
}

void FAssetMonitoringManager::OnRegistryAssetRemoved(const FAssetData& Asset)
{
	HandleAssetRemoved(Asset.PackageName);
}
