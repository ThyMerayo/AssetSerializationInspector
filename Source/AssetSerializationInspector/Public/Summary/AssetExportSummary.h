// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

struct FAssetPackageDocument;
struct FAssetPackageExportEntry;
struct FAssetSerializationTrace;

/** What an export is and how much of it the inspector reads, so that bytes it cannot decode can still be described. */
struct FAssetExportSummary
{
	/** The export's class, such as "SkeletalMesh". */
	FString ClassName;

	/** What the bytes outside the tagged properties hold for this class, such as "mesh render data (LODs, vertices, skin weights)". */
	FString NativeDataKind;

	/** Facts read from the export's decoded properties, such as "3 LODs" or "imported size X=1024 Y=1024". */
	TArray<FString> Facts;

	/** The export's size, and the part of it the trace could not attribute to a property. */
	int64 PayloadBytes = 0;
	int64 NativeBytes = 0;

	/** One sentence for a report or a tooltip. */
	FString ToText() const;
};

namespace AssetExportSummary
{
	/** The export's class name without its package, such as "Texture2D" (empty when it cannot be resolved). */
	FString GetClassName(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export);

	/** Describes an export from its class, its decoded properties and the size of its native data. Trace may be null. */
	FAssetExportSummary Summarize(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, const FAssetSerializationTrace* Trace);
} // namespace AssetExportSummary
