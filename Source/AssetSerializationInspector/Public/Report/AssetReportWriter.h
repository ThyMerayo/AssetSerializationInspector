// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

struct FAssetAnalysisReport;

enum class EAssetReportFormat : uint8
{
	Text,
	Json,
	Html
};

namespace AssetReportWriter
{
	/**
	 * A readable report for people: a header, the differences as an indented list, the save analysis and any repeated-save
	 * patterns. Multi-line values are collapsed onto one line.
	 */
	FString ToText(const FAssetAnalysisReport& Report);

	/**
	 * A machine-readable report. The layout is versioned by "schemaVersion" (FAssetAnalysisReport::SchemaVersion):
	 *
	 *   { "schemaVersion", "tool", "generatedAt", "files": { "old": {path, hash}, "new": {path, hash}, "identical" },
	 *     "summary": { added, removed, modified, moved }, "filter",
	 *     "differences": [ { kind, state, key, name, semanticPath, type, old, new, changedBytes, offsets, children } ],
	 *     "saveAnalysis": { result, ..., semanticChanges, layoutChanges, unexplainedChanges },
	 *     "repeatedSaves": [ { semanticPath, name, observations, changes, pattern, samples } ] }
	 *
	 * Absent values are written as null. "saveAnalysis" is null when no analysis was produced.
	 */
	FString ToJson(const FAssetAnalysisReport& Report);

	/**
	 * A self-contained HTML page (styles inline, no scripts) with the same content as the text report: the save analysis, and the
	 * differences as a tree that folds. Values are escaped, so they cannot inject markup. Follows the browser's light or dark theme.
	 */
	FString ToHtml(const FAssetAnalysisReport& Report);

	FString Write(const FAssetAnalysisReport& Report, EAssetReportFormat Format);

	/** File-dialog extension (without a dot) for a format. */
	const TCHAR* GetFileExtension(EAssetReportFormat Format);

	/**
	 * A suggested filename for a report: the asset's name, "Report" and the time, such as "Hero_Report_20261001-140743.txt".
	 * Characters that are not valid in filenames are replaced.
	 *
	 * @param AssetFilename The compared asset's file or package name; only its name is used.
	 * @param Time When the report is saved.
	 */
	FString MakeDefaultFilename(const FString& AssetFilename, const FDateTime& Time, EAssetReportFormat Format);

	/** Picks the format from a filename's extension (.json, .html or .htm), defaulting to text. */
	EAssetReportFormat GetFormatForFilename(const FString& Filename);

	/** Writes the report to disk as UTF-8, in the format implied by the filename's extension. */
	bool SaveToFile(const FAssetAnalysisReport& Report, const FString& Filename, FText& OutError);
} // namespace AssetReportWriter
