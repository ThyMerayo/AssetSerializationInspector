// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Save/AssetNoOpResaveTest.h"

struct FObservedAssetSave;

enum class EAssetBatchResaveStatus : uint8
{
	/** The no-op resave test ran and has a verdict. */
	Tested,

	/** The asset was left out for an expected reason (unsaved changes, a level, no file on disk, ...). */
	Skipped,

	/** The asset could not be tested because loading, saving or reading it back failed. */
	Failed
};

/** One thing a resave changed in an asset, reduced to text so the heavy comparison data can be dropped. */
struct FAssetBatchResaveChange
{
	/** What kind of change it is: a save-analysis classification such as "PropertyValueChanged", or "HeaderRegion". */
	FString Category;

	/** The property, header region or other thing that changed. */
	FString Name;

	/** Old and new values, or the explanation of the change. */
	FString Detail;
};

/** What the no-op resave test found for one asset. */
struct FAssetBatchResaveEntry
{
	FName PackageName;

	EAssetBatchResaveStatus Status = EAssetBatchResaveStatus::Skipped;

	/** Why the asset was skipped or failed. */
	FString Message;

	ENoOpResaveVerdict Verdict = ENoOpResaveVerdict::Stable;

	/** Changed bytes of the original file compared with the first resave, and of the first resave with the second. */
	int64 FirstResaveChangedBytes = 0;
	int64 SecondResaveChangedBytes = 0;

	/** What the first resave changed, and what a second resave changed again. Long lists are cut at MaximumChangesPerResave. */
	TArray<FAssetBatchResaveChange> FirstResaveChanges;
	TArray<FAssetBatchResaveChange> SecondResaveChanges;

	/** Changes beyond MaximumChangesPerResave that were counted but not kept. */
	int32 FirstResaveChangesOmitted = 0;
	int32 SecondResaveChangesOmitted = 0;

	double Seconds = 0.0;

	static constexpr int32 MaximumChangesPerResave = 200;
};

struct FAssetBatchResaveSummary
{
	int32 Tested = 0;
	int32 Stable = 0;
	int32 NormalizedOnFirstSave = 0;
	int32 Unstable = 0;
	int32 Skipped = 0;
	int32 Failed = 0;
};

/** A change that showed up in several assets, such as every asset losing its thumbnails. */
struct FAssetBatchRecurringChange
{
	FString Category;
	FString Name;
	int32 AssetCount = 0;
};

struct FAssetBatchResaveResult
{
	/** Describes what was tested, such as "/Game/Characters" or "the project". */
	FString Scope;

	FDateTime StartedAt;
	FDateTime FinishedAt;

	/** True when the run was stopped before every asset was tested. */
	bool bCancelled = false;

	TArray<FAssetBatchResaveEntry> Entries;

	FAssetBatchResaveSummary Summarize() const;

	/**
	 * Changes the first resave made in at least MinimumAssets assets, most widespread first. They point at causes that are
	 * not about any one asset, such as a save format change or a setting.
	 */
	TArray<FAssetBatchRecurringChange> FindRecurringChanges(int32 MinimumAssets = 2) const;
};

namespace AssetBatchResave
{
	/**
	 * Lists the packages of the assets under the given content paths (such as "/Game/Characters"). Levels and redirectors
	 * are left out because the test cannot use them. The result is sorted and has no duplicates.
	 */
	TArray<FName> CollectPackages(const TArray<FString>& PackagePaths, bool bRecursive);

	/** Reduces what a resave changed to a list of changes: the save analysis' explanations plus the header regions that changed. */
	void ExtractChanges(const FObservedAssetSave& Save, TArray<FAssetBatchResaveChange>& OutChanges, int32& OutOmitted);

	/** Reduces the result of the test on one asset to what a batch report keeps. */
	FAssetBatchResaveEntry Condense(const FNoOpResaveResult& Result);

	/**
	 * Runs the no-op resave test on every package in turn and condenses each result, so memory does not grow with the number
	 * of assets. Packages the run had to load are released again.
	 *
	 * @param ShouldContinue Called before each package with its index; return false to stop. Also the place to report progress.
	 */
	FAssetBatchResaveResult Run(const TArray<FName>& PackageNames, const FString& Scope, TFunctionRef<bool(int32 Index, int32 Total, FName PackageName)> ShouldContinue);
} // namespace AssetBatchResave
