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

private:
	void HandlePreSavePackage(UPackage* Package, FObjectPreSaveContext SaveContext);
	void HandlePackageSaved(const FString& PackageFilename, UPackage* Package, FObjectPostSaveContext SaveContext);
	FString MakeSnapshotFilename(const FString& SourceFilename) const;

private:
	FObservedSaveId NextSaveId = 1;

	FOnObservedAssetSave ObservedAssetSaveEvent;

	TMap<FName, FAssetSaveSnapshot> PendingSaves;
	TMap<FName, TSharedPtr<FObservedAssetSave>> ObservedSaves;

	TArray<TSharedPtr<FObservedAssetSave>> RecentSaves;

	FDelegateHandle PreSaveHandle;
	FDelegateHandle PostSaveHandle;
};

class FAssetMonitoringManager
{
public:
	static FAssetMonitoringManager& Get();

	FAssetMonitoringManager();

	bool IsMonitored(FName PackageName) const;

	void AddMonitoredAsset(FName PackageName);
	void RemoveMonitoredAsset(FName PackageName);

	const TSet<FName>& GetMonitoredAssets() const { return MonitoredPackages; }

private:
	TSet<FName> MonitoredPackages;
};
