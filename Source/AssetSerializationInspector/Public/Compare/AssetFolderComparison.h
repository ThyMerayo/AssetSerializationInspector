// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Save/AssetBatchResave.h"

struct FAssetPackageDocument;

enum class EAssetFolderComparisonStatus : uint8
{
	/** Both folders have the file and its bytes are the same. */
	Identical,

	/** Both folders have the file and it differs. */
	Changed,

	OnlyInOldFolder,
	OnlyInNewFolder,

	/** The pair could not be compared, for example because a file uses a package version this editor cannot read. */
	Failed
};

/** What comparing one asset file between the two folders found. */
struct FAssetFolderComparisonEntry
{
	/** The file's path relative to its folder, such as "Characters/Hero.uasset". */
	FString RelativePath;

	EAssetFolderComparisonStatus Status = EAssetFolderComparisonStatus::Identical;

	/** Why a pair could not be compared. */
	FString Message;

	/** The engine that saved each file, such as "5.5.1", and its package file version. Empty for a file the folder lacks. */
	FString OldEngineVersion;
	FString NewEngineVersion;
	FString OldFileVersion;
	FString NewFileVersion;

	/** True when the two files were saved with different engine, file or custom versions. */
	bool bVersionsDiffer = false;

	/** The size of each file in bytes, or INDEX_NONE when the folder lacks the file or it could not be read. */
	int64 OldFileSize = INDEX_NONE;
	int64 NewFileSize = INDEX_NONE;

	/** What changed, reduced to text. Long lists are cut at FAssetBatchResaveEntry::MaximumChangesPerResave. */
	TArray<FAssetBatchResaveChange> Changes;
	int32 ChangesOmitted = 0;
};

struct FAssetFolderComparisonSummary
{
	int32 Identical = 0;
	int32 Changed = 0;
	int32 OnlyInOldFolder = 0;
	int32 OnlyInNewFolder = 0;
	int32 Failed = 0;

	/** Changed files whose two versions were saved by different engine, file or custom versions. */
	int32 ChangedWithDifferentVersions = 0;
};

/** How many changed assets went from one engine version to another. */
struct FAssetEngineVersionPair
{
	FString OldEngineVersion;
	FString NewEngineVersion;
	int32 AssetCount = 0;
};

struct FAssetFolderComparisonResult
{
	FString OldFolder;
	FString NewFolder;

	FDateTime StartedAt;
	FDateTime FinishedAt;

	/** True when the run was stopped before every file was compared. */
	bool bCancelled = false;

	TArray<FAssetFolderComparisonEntry> Entries;

	FAssetFolderComparisonSummary Summarize() const;

	/** The engine version changes among the files present in both folders, most common first. */
	TArray<FAssetEngineVersionPair> FindEngineVersionPairs() const;

	/** Changes made to at least MinimumAssets files, most widespread first: causes that are not about any one asset. */
	TArray<FAssetBatchRecurringChange> FindRecurringChanges(int32 MinimumAssets = 2) const;
};

namespace AssetFolderComparison
{
	/** The .uasset files under a folder, as paths relative to it with forward slashes, sorted. */
	TArray<FString> FindPackageFiles(const FString& Folder);

	/** The engine that saved the package, such as "5.5.1" (empty when the summary does not say). */
	FString DescribeEngineVersion(const FAssetPackageDocument& Document);

	/** The package file version, such as "UE4 522 / UE5 1012". */
	FString DescribeFileVersion(const FAssetPackageDocument& Document);

	/** True when the packages differ in engine version, file version, licensee version or any custom version. */
	bool HaveDifferentVersions(const FAssetPackageDocument& Old, const FAssetPackageDocument& New);

	/** Compares one file present in both folders. */
	FAssetFolderComparisonEntry ComparePair(const FString& RelativePath, const FString& OldFilename, const FString& NewFilename);

	/**
	 * Compares the .uasset files of two folders, pairing them by relative path (ignoring case). Files in only one folder are
	 * listed as added or removed. Each pair is compared and condensed on its own, so memory does not grow with the folders.
	 *
	 * @param ShouldContinue Called before each file with its index; return false to stop. Also the place to report progress.
	 */
	FAssetFolderComparisonResult Run(const FString& OldFolder, const FString& NewFolder, TFunctionRef<bool(int32 Index, int32 Total, const FString& RelativePath)> ShouldContinue);
} // namespace AssetFolderComparison
