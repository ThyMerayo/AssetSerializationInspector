// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Report/AssetReportWriter.h"

struct FAssetBatchResaveResult;

namespace AssetBatchReportWriter
{
	/**
	 * A readable report of a batch no-op resave test: a header, the totals, the changes that recur across assets, and for
	 * every asset its verdict and what the resaves changed.
	 */
	FString ToText(const FAssetBatchResaveResult& Result);

	/**
	 * A machine-readable report. The layout is versioned by "schemaVersion":
	 *
	 *   { "schemaVersion", "kind": "batchNoOpResave", "tool", "scope", "startedAt", "finishedAt", "cancelled",
	 *     "summary": { tested, stable, normalizedOnFirstSave, unstable, skipped, failed },
	 *     "recurringChanges": [ { category, name, assets } ],
	 *     "assets": [ { package, status, message, verdict, seconds,
	 *                   firstResave: { changedBytes, changes: [ { category, name, detail } ], omitted },
	 *                   secondResave: { ... } } ] }
	 *
	 * "verdict" and the resave objects are null for assets that were skipped or failed.
	 */
	FString ToJson(const FAssetBatchResaveResult& Result);

	FString Write(const FAssetBatchResaveResult& Result, EAssetReportFormat Format);

	/** A suggested filename such as "NoOpResave_Characters_20261001-140743.txt". */
	FString MakeDefaultFilename(const FString& Scope, const FDateTime& Time, EAssetReportFormat Format);

	/** Writes the report to disk as UTF-8, in the format implied by the filename's extension. */
	bool SaveToFile(const FAssetBatchResaveResult& Result, const FString& Filename, FText& OutError);
} // namespace AssetBatchReportWriter
