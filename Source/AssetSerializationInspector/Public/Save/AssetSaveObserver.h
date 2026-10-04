// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Diff/AssetPackageDiff.h"
#include "Save/AssetSaveAnalyzer.h"

class UPackage;
class FObjectPreSaveContext;
class FObjectPostSaveContext;
struct FAssetPackageDocument;
struct FAssetPackageTraceCollection;

enum class EObservedSaveChangeKind : uint8
{
	Identical,
	MetadataOnly,
	LayoutOnly,
	TableChange,
	PayloadChange,
	Unknown
};

struct FAssetSaveSnapshot
{
	FName PackageName;
	FString OriginalFilename;
	FString BeforeFilename;

	bool bHadPreviousFile = false;
};

using FObservedSaveId = uint64;

struct FObservedAssetSave
{
	FObservedSaveId SaveId = 0;

	FName PackageName;

	FString BeforeFilename;
	FString AfterFilename;

	TSharedPtr<FAssetPackageDocument> Before;
	TSharedPtr<FAssetPackageDocument> After;

	FAssetPackageDiffResult Diff;
	FAssetSaveAnalysis Analysis;

	TSharedPtr<FAssetPackageTraceCollection> BeforeFields;
	TSharedPtr<FAssetPackageTraceCollection> AfterFields;

	FDateTime Timestamp;

	EObservedSaveChangeKind ChangeKind = EObservedSaveChangeKind::Unknown;

	int64 ChangedPayloadBytes = 0;
	int32 ChangedPayloadSpans = 0;

	bool HasChanges() const { return !Diff.bFilesIdentical; }
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnObservedAssetSave, TSharedPtr<FObservedAssetSave>);

class FAssetSaveObserver
{
	static constexpr int32 MaxRecentSaves = 50;

public:
	static FAssetSaveObserver& Get();

	FOnObservedAssetSave& OnObservedAssetSave() { return ObservedAssetSaveEvent; }
	TSharedPtr<FObservedAssetSave> FindLatestSave(FName PackageName) const;

	void Startup();
	void Shutdown();

	/**
	 * Loads, decodes and compares two versions of a package into the record the UI works with.
	 *
	 * @param BeforeFilename The earlier version, or empty when there was none.
	 * @param AfterFilename The later version.
	 */
	TSharedPtr<FObservedAssetSave> BuildObservedSave(FName PackageName, const FString& BeforeFilename, const FString& AfterFilename);

	/** While one of these exists, saves are not observed. Used when the plugin saves packages itself. */
	class FScopedSuppression
	{
	public:
		FScopedSuppression() { ++FAssetSaveObserver::Get().SuppressionCount; }
		~FScopedSuppression() { --FAssetSaveObserver::Get().SuppressionCount; }

		FScopedSuppression(const FScopedSuppression&) = delete;
		FScopedSuppression& operator=(const FScopedSuppression&) = delete;
	};

private:
	void HandlePreSavePackage(UPackage* Package, FObjectPreSaveContext SaveContext);
	void HandlePackageSaved(const FString& PackageFilename, UPackage* Package, FObjectPostSaveContext SaveContext);
	FString MakeSnapshotFilename(const FString& SourceFilename) const;

private:
	FObservedSaveId NextSaveId = 1;
	int32 SuppressionCount = 0;

	FOnObservedAssetSave ObservedAssetSaveEvent;

	TMap<FName, FAssetSaveSnapshot> PendingSaves;
	TMap<FName, TSharedPtr<FObservedAssetSave>> ObservedSaves;

	TArray<TSharedPtr<FObservedAssetSave>> RecentSaves;

	FDelegateHandle PreSaveHandle;
	FDelegateHandle PostSaveHandle;
};

/**
 * The set of assets whose saves are observed, kept in the user's editor settings so it survives restarts.
 *
 * The settings object is the stored form: the manager reads it at start, follows edits made to it (in the editor preferences, or
 * by reloading the config) and writes it back whenever the set changes. Monitoring follows an asset when it is renamed or moved
 * and stops when it is deleted, so a monitored asset is not silently lost.
 */
class FAssetMonitoringManager
{
public:
	static FAssetMonitoringManager& Get();

	/**
	 * @param bInPersistent When false the manager neither reads nor writes the settings and does not listen for asset changes; for tests.
	 */
	explicit FAssetMonitoringManager(bool bInPersistent = true);
	~FAssetMonitoringManager();

	bool IsMonitored(FName PackageName) const;

	/** Adds the package to the set. Monitoring an asset twice is the same as once. */
	void AddMonitoredAsset(FName PackageName);
	void RemoveMonitoredAsset(FName PackageName);

	/** Replaces the whole set, for example with the list the settings now hold. Nothing is written back. */
	void SetMonitoredAssets(const TArray<FName>& PackageNames);

	/** The package of a monitored asset was renamed or moved: monitor the new package instead. */
	void HandleAssetRenamed(FName OldPackageName, FName NewPackageName);

	/** The package of a monitored asset was deleted: stop monitoring it. */
	void HandleAssetRemoved(FName PackageName);

	const TSet<FName>& GetMonitoredAssets() const { return MonitoredPackages; }

private:
	void SaveToSettings() const;
	void ReloadFromSettings();

	void OnRegistryAssetRenamed(const struct FAssetData& NewAsset, const FString& OldObjectPath);
	void OnRegistryAssetRemoved(const struct FAssetData& Asset);

	TSet<FName> MonitoredPackages;

	bool bPersistent = true;
	FDelegateHandle SettingsChangedHandle;
	FDelegateHandle AssetRenamedHandle;
	FDelegateHandle AssetRemovedHandle;
};
