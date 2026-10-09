// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Serialization/AssetStructNativeData.h"

struct FAssetPackageDocument;
struct FAssetPackageExportEntry;
struct FAssetSerializationTrace;

/** One row of a data table: its name and the properties of the row struct, each as the text the inspector shows for its value. */
struct FAssetDataTableRow
{
	FString Name;

	/** The properties of the row, in the order the row wrote them. */
	TArray<TPair<FString, FString>> Properties;

	/** One line: Property=Value, for every property. */
	FString Describe() const;
};

/**
 * What a data table (UDataTable) writes after its tagged properties, decoded: its rows, each a name and the tagged properties of the
 * row struct. The struct comes from the RowStruct property; a row struct whose rows cannot be decoded (a user struct this reading
 * cannot name, a property type it does not read) leaves the table unread, as a whole.
 */
struct FAssetDataTableData
{
	/** The whole range was read and nothing was left over. When false, Error says where it stopped. */
	bool bComplete = false;
	FString Error;

	/** The path of the struct that each row is an instance of; empty when the table has none (its rows are then the base row struct). */
	FString RowStruct;

	TArray<FAssetDataTableRow> Rows;

	/** A short account: the rows and the struct. */
	FString Summarize() const;
};

namespace AssetDataTableData
{
	/**
	 * Decodes the native data of a data table. Returns false, without touching Out, for any other export.
	 *
	 * @param NativeOffset Where the native data starts in the document (right after the tagged properties).
	 * @param NativeSize How many bytes it has.
	 * @param Trace The properties of the export, where the RowStruct property is found. Without it the rows cannot be read.
	 */
	bool Decode(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, int64 NativeOffset, int64 NativeSize, FAssetDataTableData& Out, const FAssetSerializationTrace* Trace);

	/** What differs between two versions: the rows that were added or removed and, for each row that stayed, the properties that changed. The order of the rows is not a change. */
	TArray<FAssetNativeDataChange> Compare(const FAssetDataTableData& Old, const FAssetDataTableData& New);
} // namespace AssetDataTableData
