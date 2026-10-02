// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Diff/AssetPackageDiff.h"
#include "Save/AssetSaveAnalyzer.h"
#include "Save/RepeatedSaveAnalyzer.h"

struct FAssetDiffFilter;

/** Counts of differences by state, over every level of the diff tree. Unchanged entries are not counted. */
struct FAssetReportSummary
{
	int32 Added = 0;
	int32 Removed = 0;
	int32 Modified = 0;
	int32 Moved = 0;
};

/**
 * Everything an exported analysis report contains, independent of the UI and of the output format.
 *
 * The report owns copies of the data it describes, so it stays valid after the comparison that produced it is closed.
 * Writers (see AssetReportWriter.h) turn it into text or JSON; batch and cross-version analysis can build the same model
 * for many assets.
 */
struct FAssetAnalysisReport
{
	/** Bumped when the JSON layout changes incompatibly. */
	static constexpr int32 SchemaVersion = 1;

	FString ToolVersion;
	FDateTime GeneratedAtUtc;

	FString OldFilename;
	FString NewFilename;
	FString OldFileHash;
	FString NewFileHash;
	bool bFilesIdentical = false;

	/** Totals for the whole comparison, regardless of any filter applied to the listed differences. */
	FAssetReportSummary Summary;

	/** The differences listed in the report. Already pruned when a filter was applied. */
	TArray<FAssetPackageDiffEntry> Differences;

	/** Describes the filter that pruned Differences, or is empty when the full diff is listed. */
	FString FilterDescription;

	TOptional<FAssetSaveAnalysis> SaveAnalysis;

	/** Repeated-save patterns observed for the package, when its save history was available. */
	TArray<FRepeatedSavePattern> RepeatedSavePatterns;
};

namespace AssetAnalysisReport
{
	/**
	 * Builds a report from a finished comparison.
	 *
	 * @param Diff The structural comparison.
	 * @param SaveAnalysis The explanation of the save, if one was produced.
	 * @param RepeatedSavePatterns Patterns across recorded saves, if any.
	 * @param Filter When given, only the differences it shows are listed; Summary still describes the whole comparison.
	 */
	FAssetAnalysisReport Build(const FAssetPackageDiffResult& Diff, const FAssetSaveAnalysis* SaveAnalysis, const TArray<FRepeatedSavePattern>& RepeatedSavePatterns, const FAssetDiffFilter* Filter);

	FAssetReportSummary Summarize(const TArray<FAssetPackageDiffEntry>& Entries);

	/** The plugin's version, or an empty string when it cannot be determined. */
	FString GetToolVersion();
} // namespace AssetAnalysisReport
