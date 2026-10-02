// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Report/AssetReportWriter.h"

struct FAssetFolderComparisonResult;

namespace AssetFolderComparisonReportWriter
{
	/**
	 * A readable report of a folder comparison: both folders, the totals, the engine version changes, the changes that recur
	 * across assets, and then each changed, failed and one-sided file. Identical files are only counted.
	 */
	FString ToText(const FAssetFolderComparisonResult& Result);

	/**
	 * A machine-readable report. The layout is versioned by "schemaVersion":
	 *
	 *   { "schemaVersion", "kind": "folderComparison", "tool", "oldFolder", "newFolder", "startedAt", "finishedAt", "cancelled",
	 *     "summary": { identical, changed, onlyInOldFolder, onlyInNewFolder, failed, changedWithDifferentVersions },
	 *     "engineVersions": [ { old, new, assets } ],
	 *     "recurringChanges": [ { category, name, assets } ],
	 *     "assets": [ { path, status, message, oldEngineVersion, newEngineVersion, oldFileVersion, newFileVersion,
	 *                   versionsDiffer, oldFileSize, newFileSize, changes: [ { category, name, detail } ], omitted } ] }
	 *
	 * Absent versions and messages are null. Every file is listed, identical ones included.
	 */
	FString ToJson(const FAssetFolderComparisonResult& Result);

	FString Write(const FAssetFolderComparisonResult& Result, EAssetReportFormat Format);

	/** A suggested filename such as "FolderComparison_Content_vs_Content_20261001-140743.txt". */
	FString MakeDefaultFilename(const FString& OldFolder, const FString& NewFolder, const FDateTime& Time, EAssetReportFormat Format);

	/** Writes the report to disk as UTF-8, in the format implied by the filename's extension. */
	bool SaveToFile(const FAssetFolderComparisonResult& Result, const FString& Filename, FText& OutError);
} // namespace AssetFolderComparisonReportWriter
