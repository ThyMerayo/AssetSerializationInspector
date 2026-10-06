// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Serialization/AssetPropertyValueDecoder.h"

/** A kind of property the decoder could not read, with how often and how much data it affected across the assets scanned. */
struct FAssetDecoderCoverageIssue
{
	/** The property type that failed, such as "ArrayProperty(StructProperty(MyStruct))". */
	FString TypeName;

	/** Why, with numbers replaced by '#' so similar failures group together. */
	FString Message;

	int32 Occurrences = 0;
	int32 AssetCount = 0;

	/** Serialized bytes of the top-level properties that contained the failure. */
	int64 Bytes = 0;

	/** A few assets (paths relative to the scanned folder) that show it. */
	TArray<FString> Examples;
};

/** Bytes of exports that the property decoder did not attribute to any property, by the class of the export. */
struct FAssetNativeRegionStat
{
	/** The export's class, such as "Texture2D". */
	FString ExportClass;

	/** What the trace says the bytes are, such as "Native/custom serialized data". */
	FString Reason;

	int32 Occurrences = 0;
	int32 AssetCount = 0;
	int64 Bytes = 0;

	TArray<FString> Examples;
};

struct FAssetDecoderCoverageResult
{
	FString Folder;

	FDateTime StartedAt;
	FDateTime FinishedAt;

	bool bCancelled = false;

	int32 AssetsScanned = 0;

	/** Files the package reader could not read, such as an unsupported package version, grouped by reason. */
	int32 AssetsUnreadable = 0;
	TMap<FString, int32> UnreadableReasons;

	int32 ExportsScanned = 0;

	/** Top-level tagged properties found, how many of them decoded completely, and their serialized sizes. */
	int32 PropertiesScanned = 0;
	int32 PropertiesDecoded = 0;
	int64 PropertyBytes = 0;
	int64 PropertyBytesUndecoded = 0;

	/** Export bytes outside the tagged properties: native or custom serialization the decoder does not read. */
	int64 NativeBytes = 0;

	/**
	 * The graph nodes (Blueprint graph nodes: events, calls, variables, ...) found, and how many of them had their pins read to the
	 * last byte. A node that did not is listed in GraphNodeIssues by its class and what stopped the reading.
	 */
	int32 GraphNodesScanned = 0;
	int32 GraphNodesRead = 0;
	int64 GraphPinsRead = 0;
	TArray<FAssetDecoderCoverageIssue> GraphNodeIssues;

	/**
	 * The functions with bytecode found, how many had it disassembled completely (its in-memory size adds up to the size the function
	 * says), and why the others did not.
	 */
	int32 BytecodeFunctionsScanned = 0;
	int32 BytecodeFunctionsRead = 0;
	int64 BytecodeStatementsRead = 0;
	TArray<FAssetDecoderCoverageIssue> BytecodeIssues;

	/**
	 * The textures and mesh descriptions found (their record of the source data after the properties), how many were read to the
	 * last byte, and why the others were not.
	 */
	int32 BulkDataExportsScanned = 0;
	int32 BulkDataExportsRead = 0;
	TArray<FAssetDecoderCoverageIssue> BulkDataIssues;

	/** Most widespread (then largest) first. */
	TArray<FAssetDecoderCoverageIssue> Issues;
	TArray<FAssetNativeRegionStat> NativeRegions;
};

/** One value the decoder could not read, with the leaf-most type and message. */
struct FAssetDecoderCoverageFailure
{
	FString TypeName;
	FString Message;
};

namespace AssetDecoderCoverage
{
	/** Replaces every run of digits with '#', so "Could not decode array element 12" and "... 7" count as one failure. */
	FString NormalizeMessage(const FString& Message);

	/**
	 * Collects the failures inside a decoded value: for each value that did not decode, the deepest one that failed. A value
	 * that succeeded can still contain failed children (a tagged struct keeps the fields it could not read).
	 */
	void CollectFailures(const FAssetDecodedPropertyValue& Value, TArray<FAssetDecoderCoverageFailure>& OutFailures);

	/**
	 * Decodes every tagged property of every export of the .uasset files under a folder and tallies what the decoder could
	 * not read, so serializer work can start with what real assets need most.
	 *
	 * @param ShouldContinue Called before each file with its index; return false to stop. Also the place to report progress.
	 */
	FAssetDecoderCoverageResult Run(const FString& Folder, TFunctionRef<bool(int32 Index, int32 Total, const FString& RelativePath)> ShouldContinue);

	/** A readable report: totals, the issues ranked, and the native regions by export class. */
	FString ToText(const FAssetDecoderCoverageResult& Result, int32 MaximumRows = 60);
	FString ToJson(const FAssetDecoderCoverageResult& Result);
	bool SaveToFile(const FAssetDecoderCoverageResult& Result, const FString& Filename, FText& OutError);
} // namespace AssetDecoderCoverage
