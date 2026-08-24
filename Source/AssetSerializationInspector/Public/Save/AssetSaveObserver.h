// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

class UPackage;
class FObjectPreSaveContext;
class FObjectPostSaveContext;

struct FAssetSaveSnapshot
{
	FName PackageName;
	FString OriginalFilename;
	FString BeforeFilename;

	bool bHadPreviousFile = false;
};

struct FObservedAssetSave
{
	FName PackageName;

	TSharedPtr<FAssetPackageDocument> Before;
	TSharedPtr<FAssetPackageDocument> After;

	TOptional<FAssetPackageDiffResult> StructuralDiff;

	TSharedPtr<FAssetPackageTraceCollection> BeforeFields;
	TSharedPtr<FAssetPackageTraceCollection> AfterFields;
};

class FAssetSaveObserver
{
public:
	static FAssetSaveObserver& Get();

	void Startup();
	void Shutdown();

private:
	void HandlePreSavePackage(UPackage* Package, FObjectPreSaveContext SaveContext);
	void HandlePackageSaved(const FString& PackageFilename, UPackage* Package, FObjectPostSaveContext SaveContext);
	FString MakeSnapshotFilename(const FString& SourceFilename) const;

private:
	TMap<FName, FAssetSaveSnapshot> PendingSaves;
	TMap<FName, TSharedPtr<FObservedAssetSave>> ObservedSaves;

	FDelegateHandle PreSaveHandle;
	FDelegateHandle PostSaveHandle;
};