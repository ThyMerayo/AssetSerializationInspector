// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Coverage/AssetDecoderCoverage.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"

#include "Compare/AssetFolderComparison.h"
#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Serialization/AssetBulkDataExport.h"
#include "Serialization/AssetDataTableData.h"
#include "Serialization/AssetGraphNodePins.h"
#include "Serialization/AssetMorphTargetData.h"
#include "Serialization/AssetPropertyValueDecoder.h"
#include "Serialization/AssetSkeletalMeshData.h"
#include "Serialization/AssetStaticMeshData.h"
#include "Serialization/AssetStructNativeData.h"
#include "Trace/AssetPackageFieldDecoder.h"
#include "Trace/AssetSerializationTrace.h"

#define LOCTEXT_NAMESPACE "AssetDecoderCoverage"

namespace
{
	constexpr int32 MaximumExamples = 3;

	void AddExample(TArray<FString>& Examples, const FString& RelativePath)
	{
		if (Examples.Num() < MaximumExamples && !Examples.Contains(RelativePath))
		{
			Examples.Add(RelativePath);
		}
	}

	FString ShortClassName(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export)
	{
		FString Path;
		if (!Document.ResolvePackageIndexPath(Export.ClassIndex, Path) || Path == TEXT("None"))
		{
			return TEXT("<unknown class>");
		}

		int32 Dot = INDEX_NONE;
		return Path.FindLastChar(TEXT('.'), Dot) ? Path.RightChop(Dot + 1) : Path;
	}

	/** Strips an asset-specific reason down to what is common between assets. */
	FString CoverageKey(const FString& A, const FString& B)
	{
		return A + TEXT("\n") + B;
	}
} // namespace

FString AssetDecoderCoverage::NormalizeMessage(const FString& Message)
{
	FString Result;
	bool bInNumber = false;

	for (const TCHAR Character : Message)
	{
		if (FChar::IsDigit(Character))
		{
			if (!bInNumber)
			{
				Result.AppendChar(TEXT('#'));
				bInNumber = true;
			}
		}
		else
		{
			Result.AppendChar(Character);
			bInNumber = false;
		}
	}

	return Result;
}

void AssetDecoderCoverage::CollectFailures(const FAssetDecodedPropertyValue& Value, TArray<FAssetDecoderCoverageFailure>& OutFailures)
{
	const int32 FailuresBefore = OutFailures.Num();

	for (const FAssetDecodedPropertyValue& Child : Value.Children)
	{
		CollectFailures(Child, OutFailures);
	}

	// Report a value that failed only when none of its children explains why.
	if (!Value.IsSuccess() && OutFailures.Num() == FailuresBefore)
	{
		FAssetDecoderCoverageFailure& Failure = OutFailures.AddDefaulted_GetRef();
		Failure.TypeName = Value.TypeName;
		Failure.Message = NormalizeMessage(Value.Error);
	}
}

FAssetDecoderCoverageResult AssetDecoderCoverage::Run(const FString& Folder, TFunctionRef<bool(int32 Index, int32 Total, const FString& RelativePath)> ShouldContinue)
{
	FAssetDecoderCoverageResult Result;
	Result.Folder = Folder;
	Result.StartedAt = FDateTime::Now();

	TMap<FString, FAssetDecoderCoverageIssue> Issues;
	TMap<FString, FAssetNativeRegionStat> NativeRegions;
	TMap<FString, FAssetDecoderCoverageIssue> GraphNodeIssues;
	TMap<FString, FAssetDecoderCoverageIssue> BytecodeIssues;
	TMap<FString, FAssetDecoderCoverageIssue> BulkDataIssues;
	TMap<FString, FAssetDecoderCoverageIssue> StaticMeshIssues;
	TMap<FString, FAssetDecoderCoverageIssue> SkeletalMeshIssues;
	TMap<FString, FAssetDecoderCoverageIssue> MorphTargetIssues;
	TMap<FString, FAssetDecoderCoverageIssue> DataTableIssues;

	const TArray<FString> Files = AssetFolderComparison::FindPackageFiles(Folder);

	for (int32 Index = 0; Index < Files.Num(); ++Index)
	{
		const FString& RelativePath = Files[Index];

		if (!ShouldContinue(Index, Files.Num(), RelativePath))
		{
			Result.bCancelled = true;
			break;
		}

		FText Error;
		const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(FPaths::Combine(Folder, RelativePath), Error);

		if (!Document.IsValid())
		{
			++Result.AssetsUnreadable;
			++Result.UnreadableReasons.FindOrAdd(NormalizeMessage(Error.ToString().Replace(TEXT("\n"), TEXT(" "))));
			continue;
		}

		// The reader keeps a package whose tables could not be decoded, so a scan would otherwise count it as having no exports.
		const FText* TableError = !Document->NameMapError.IsEmpty()
			? &Document->NameMapError
			: (!Document->ImportMapError.IsEmpty() ? &Document->ImportMapError : (!Document->ExportMapError.IsEmpty() ? &Document->ExportMapError : nullptr));
		if (TableError != nullptr)
		{
			++Result.AssetsUnreadable;
			++Result.UnreadableReasons.FindOrAdd(NormalizeMessage(TableError->ToString().Replace(TEXT("\n"), TEXT(" "))));
			continue;
		}

		++Result.AssetsScanned;

		const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);
		TSet<FString> IssuesInAsset;
		TSet<FString> NativeInAsset;
		TSet<FString> GraphNodesInAsset;

		for (const FAssetPackageExportEntry& Export : Document->ExportMap)
		{
			const FAssetSerializationTrace* Trace = Traces->FindExportTrace(Export.Index);

			if (Trace == nullptr || !Trace->Root.IsValid())
			{
				continue;
			}

			++Result.ExportsScanned;
			const FString ExportClass = ShortClassName(*Document, Export);
			const FAssetSerializationTraceNode* LastNative = nullptr;

			for (const TSharedPtr<FAssetSerializationTraceNode>& Node : Trace->Root->Children)
			{
				if (!Node.IsValid())
				{
					continue;
				}

				if (Node->Kind == EAssetSerializationTraceKind::Native)
				{
					LastNative = Node.Get();
					Result.NativeBytes += Node->Size;

					const FString Reason = NormalizeMessage(Node->TypeName);
					const FString Key = CoverageKey(ExportClass, Reason);
					FAssetNativeRegionStat& Stat = NativeRegions.FindOrAdd(Key);
					Stat.ExportClass = ExportClass;
					Stat.Reason = Reason;
					++Stat.Occurrences;
					Stat.Bytes += Node->Size;
					AddExample(Stat.Examples, RelativePath);

					if (!NativeInAsset.Contains(Key))
					{
						NativeInAsset.Add(Key);
						++Stat.AssetCount;
					}

					continue;
				}

				if (Node->Kind != EAssetSerializationTraceKind::Property)
				{
					continue;
				}

				++Result.PropertiesScanned;
				Result.PropertyBytes += Node->Size;

				const FAssetDecodedPropertyValue Decoded = FAssetPropertyValueDecoder::Decode(*Document, *Node, Export.SerialOffset);

				TArray<FAssetDecoderCoverageFailure> Failures;
				CollectFailures(Decoded, Failures);

				if (Failures.IsEmpty())
				{
					++Result.PropertiesDecoded;
					continue;
				}

				Result.PropertyBytesUndecoded += Node->Size;

				// A property with several failures counts its bytes once, against the first.
				bool bBytesCounted = false;
				for (const FAssetDecoderCoverageFailure& Failure : Failures)
				{
					const FString Key = CoverageKey(Failure.TypeName, Failure.Message);
					FAssetDecoderCoverageIssue& Issue = Issues.FindOrAdd(Key);
					Issue.TypeName = Failure.TypeName;
					Issue.Message = Failure.Message;
					++Issue.Occurrences;
					AddExample(Issue.Examples, RelativePath);

					if (!bBytesCounted)
					{
						Issue.Bytes += Node->Size;
						bBytesCounted = true;
					}

					if (!IssuesInAsset.Contains(Key))
					{
						IssuesInAsset.Add(Key);
						++Issue.AssetCount;
					}
				}
			}

			// A skeletal mesh writes its bounds, material slots and reference skeleton first after its tagged properties.
			FAssetSkeletalMeshData SkeletalMeshData;
			if (LastNative != nullptr && AssetSkeletalMeshData::Decode(*Document, Export, Export.SerialOffset + LastNative->Offset, LastNative->Size, SkeletalMeshData))
			{
				++Result.SkeletalMeshesScanned;
				if (SkeletalMeshData.bPrefixRead)
				{
					++Result.SkeletalMeshesRead;
					if (SkeletalMeshData.bComplete)
					{
						++Result.SkeletalMeshesWholeRead;
					}
					else
					{
						// The start was read but not the imported model: say why, so the layouts that are not read can be found.
						const FString Message = NormalizeMessage(SkeletalMeshData.ModelError);
						FAssetDecoderCoverageIssue& Issue = SkeletalMeshIssues.FindOrAdd(CoverageKey(ExportClass, Message));
						Issue.TypeName = ExportClass;
						Issue.Message = Message;
						++Issue.Occurrences;
						Issue.Bytes += LastNative->Size;
						AddExample(Issue.Examples, RelativePath);
					}
				}
				else
				{
					const FString Message = NormalizeMessage(SkeletalMeshData.Error);
					FAssetDecoderCoverageIssue& Issue = SkeletalMeshIssues.FindOrAdd(CoverageKey(ExportClass, Message));
					Issue.TypeName = ExportClass;
					Issue.Message = Message;
					++Issue.Occurrences;
					Issue.Bytes += LastNative->Size;
					AddExample(Issue.Examples, RelativePath);
				}
			}

			// A morph target writes its LODs, with the vertex deltas of each, after its tagged properties.
			FAssetMorphTargetData MorphTargetData;
			if (LastNative != nullptr && AssetMorphTargetData::Decode(*Document, Export, Export.SerialOffset + LastNative->Offset, LastNative->Size, MorphTargetData))
			{
				++Result.MorphTargetsScanned;
				if (MorphTargetData.bComplete)
				{
					++Result.MorphTargetsRead;
				}
				else
				{
					const FString Message = NormalizeMessage(MorphTargetData.Error);
					FAssetDecoderCoverageIssue& Issue = MorphTargetIssues.FindOrAdd(CoverageKey(ExportClass, Message));
					Issue.TypeName = ExportClass;
					Issue.Message = Message;
					++Issue.Occurrences;
					Issue.Bytes += LastNative->Size;
					AddExample(Issue.Examples, RelativePath);
				}
			}

			// A data table writes its rows after its tagged properties.
			FAssetDataTableData DataTableData;
			if (LastNative != nullptr && AssetDataTableData::Decode(*Document, Export, Export.SerialOffset + LastNative->Offset, LastNative->Size, DataTableData, Trace))
			{
				++Result.DataTablesScanned;
				if (DataTableData.bComplete)
				{
					++Result.DataTablesRead;
				}
				else
				{
					const FString Message = NormalizeMessage(DataTableData.Error);
					FAssetDecoderCoverageIssue& Issue = DataTableIssues.FindOrAdd(CoverageKey(ExportClass, Message));
					Issue.TypeName = ExportClass;
					Issue.Message = Message;
					++Issue.Occurrences;
					Issue.Bytes += LastNative->Size;
					AddExample(Issue.Examples, RelativePath);
				}
			}

			// A static mesh writes its collision, sockets and material slots after its tagged properties.
			FAssetStaticMeshData StaticMeshData;
			if (LastNative != nullptr && AssetStaticMeshData::Decode(*Document, Export, Export.SerialOffset + LastNative->Offset, LastNative->Size, StaticMeshData, Trace))
			{
				++Result.StaticMeshesScanned;
				if (StaticMeshData.bComplete)
				{
					++Result.StaticMeshesRead;
				}
				else
				{
					const FString Message = NormalizeMessage(StaticMeshData.Error);
					FAssetDecoderCoverageIssue& Issue = StaticMeshIssues.FindOrAdd(CoverageKey(ExportClass, Message));
					Issue.TypeName = ExportClass;
					Issue.Message = Message;
					++Issue.Occurrences;
					Issue.Bytes += LastNative->Size;
					AddExample(Issue.Examples, RelativePath);
				}
			}

			// A texture or a mesh description writes the record of its source data after its tagged properties.
			FAssetBulkDataExport BulkData;
			if (LastNative != nullptr && AssetBulkDataExport::Decode(*Document, Export, Export.SerialOffset + LastNative->Offset, LastNative->Size, BulkData))
			{
				++Result.BulkDataExportsScanned;
				if (BulkData.bComplete)
				{
					++Result.BulkDataExportsRead;
				}
				else
				{
					const FString Message = NormalizeMessage(BulkData.Error);
					FAssetDecoderCoverageIssue& Issue = BulkDataIssues.FindOrAdd(CoverageKey(ExportClass, Message));
					Issue.TypeName = ExportClass;
					Issue.Message = Message;
					++Issue.Occurrences;
					Issue.Bytes += LastNative->Size;
					AddExample(Issue.Examples, RelativePath);
				}
			}

			// A function writes its bytecode after its tagged properties: say whether it disassembled completely.
			FAssetStructNativeData FunctionData;
			if (LastNative != nullptr && AssetStructNativeData::Decode(*Document, Export, Export.SerialOffset + LastNative->Offset, LastNative->Size, FunctionData)
				&& FunctionData.BytecodeStorageSize > 0)
			{
				++Result.BytecodeFunctionsScanned;
				if (FunctionData.Bytecode.bComplete)
				{
					++Result.BytecodeFunctionsRead;
					Result.BytecodeStatementsRead += FunctionData.Bytecode.Statements.Num();
				}
				else
				{
					const FString Message = NormalizeMessage(FunctionData.Bytecode.Error);
					FAssetDecoderCoverageIssue& Issue = BytecodeIssues.FindOrAdd(CoverageKey(ExportClass, Message));
					Issue.TypeName = ExportClass;
					Issue.Message = Message;
					++Issue.Occurrences;
					Issue.Bytes += FunctionData.BytecodeStorageSize;
					AddExample(Issue.Examples, RelativePath);
				}
			}

			// A graph node writes its pins after its tagged properties: say whether they read to the last byte.
			FAssetGraphNodePins NodePins;
			if (LastNative != nullptr && AssetGraphNodePins::Decode(*Document, Export, Export.SerialOffset + LastNative->Offset, LastNative->Size, NodePins))
			{
				++Result.GraphNodesScanned;
				if (NodePins.bComplete)
				{
					++Result.GraphNodesRead;
					Result.GraphPinsRead += NodePins.Pins.Num();
				}
				else
				{
					const FString Message = NormalizeMessage(NodePins.Error);
					FAssetDecoderCoverageIssue& Issue = GraphNodeIssues.FindOrAdd(CoverageKey(ExportClass, Message));
					Issue.TypeName = ExportClass;
					Issue.Message = Message;
					++Issue.Occurrences;
					Issue.Bytes += LastNative->Size;
					AddExample(Issue.Examples, RelativePath);

					const FString NodeKey = CoverageKey(ExportClass, Message);
					if (!GraphNodesInAsset.Contains(NodeKey))
					{
						GraphNodesInAsset.Add(NodeKey);
						++Issue.AssetCount;
					}
				}
			}
		}
	}

	MorphTargetIssues.GenerateValueArray(Result.MorphTargetIssues);
	Result.MorphTargetIssues.Sort([](const FAssetDecoderCoverageIssue& Left, const FAssetDecoderCoverageIssue& Right) { return Left.Occurrences > Right.Occurrences; });
	DataTableIssues.GenerateValueArray(Result.DataTableIssues);
	Result.DataTableIssues.Sort([](const FAssetDecoderCoverageIssue& Left, const FAssetDecoderCoverageIssue& Right) { return Left.Occurrences > Right.Occurrences; });
	SkeletalMeshIssues.GenerateValueArray(Result.SkeletalMeshIssues);
	Result.SkeletalMeshIssues.Sort([](const FAssetDecoderCoverageIssue& Left, const FAssetDecoderCoverageIssue& Right) { return Left.Occurrences > Right.Occurrences; });

	StaticMeshIssues.GenerateValueArray(Result.StaticMeshIssues);
	Result.StaticMeshIssues.Sort([](const FAssetDecoderCoverageIssue& Left, const FAssetDecoderCoverageIssue& Right) { return Left.Occurrences > Right.Occurrences; });

	BulkDataIssues.GenerateValueArray(Result.BulkDataIssues);
	Result.BulkDataIssues.Sort([](const FAssetDecoderCoverageIssue& Left, const FAssetDecoderCoverageIssue& Right) { return Left.Occurrences > Right.Occurrences; });

	BytecodeIssues.GenerateValueArray(Result.BytecodeIssues);
	Result.BytecodeIssues.Sort([](const FAssetDecoderCoverageIssue& Left, const FAssetDecoderCoverageIssue& Right) { return Left.Occurrences > Right.Occurrences; });

	GraphNodeIssues.GenerateValueArray(Result.GraphNodeIssues);
	Result.GraphNodeIssues.Sort([](const FAssetDecoderCoverageIssue& Left, const FAssetDecoderCoverageIssue& Right) { return Left.Occurrences > Right.Occurrences; });

	Issues.GenerateValueArray(Result.Issues);
	Result.Issues.Sort([](const FAssetDecoderCoverageIssue& Left, const FAssetDecoderCoverageIssue& Right) {
		if (Left.AssetCount != Right.AssetCount)
		{
			return Left.AssetCount > Right.AssetCount;
		}
		return Left.Bytes > Right.Bytes;
	});

	NativeRegions.GenerateValueArray(Result.NativeRegions);
	Result.NativeRegions.Sort([](const FAssetNativeRegionStat& Left, const FAssetNativeRegionStat& Right) { return Left.Bytes > Right.Bytes; });

	Result.FinishedAt = FDateTime::Now();
	return Result;
}

FString AssetDecoderCoverage::ToText(const FAssetDecoderCoverageResult& Result, const int32 MaximumRows)
{
	TArray<FString> Lines;

	Lines.Add(TEXT("Asset Serialization Inspector: decoder coverage"));
	Lines.Add(FString::Printf(TEXT("Folder: %s"), *Result.Folder));
	Lines.Add(FString::Printf(TEXT("Started: %s"), *Result.StartedAt.ToIso8601()));
	Lines.Add(FString::Printf(TEXT("Finished: %s%s"), *Result.FinishedAt.ToIso8601(), Result.bCancelled ? TEXT(" (cancelled)") : TEXT("")));
	Lines.Add(FString());

	Lines.Add(FString::Printf(TEXT("%d assets scanned, %d unreadable, %d exports"), Result.AssetsScanned, Result.AssetsUnreadable, Result.ExportsScanned));
	const double PercentDecoded = Result.PropertiesScanned > 0 ? 100.0 * Result.PropertiesDecoded / Result.PropertiesScanned : 100.0;
	const double PercentBytes = Result.PropertyBytes > 0 ? 100.0 * (Result.PropertyBytes - Result.PropertyBytesUndecoded) / Result.PropertyBytes : 100.0;
	Lines.Add(FString::Printf(TEXT("Tagged properties: %d, fully decoded %d (%.1f%%); %lld of %lld bytes decoded (%.1f%%)"), Result.PropertiesScanned, Result.PropertiesDecoded, PercentDecoded,
		Result.PropertyBytes - Result.PropertyBytesUndecoded, Result.PropertyBytes, PercentBytes));
	Lines.Add(FString::Printf(TEXT("Bytes outside the tagged properties (native or custom serialization): %lld"), Result.NativeBytes));

	if (!Result.UnreadableReasons.IsEmpty())
	{
		Lines.Add(FString());
		Lines.Add(TEXT("Unreadable files"));

		TArray<TPair<FString, int32>> Reasons;
		for (const TPair<FString, int32>& Pair : Result.UnreadableReasons)
		{
			Reasons.Add(Pair);
		}
		Reasons.Sort([](const TPair<FString, int32>& Left, const TPair<FString, int32>& Right) { return Left.Value > Right.Value; });

		for (int32 Index = 0; Index < Reasons.Num() && Index < MaximumRows; ++Index)
		{
			Lines.Add(FString::Printf(TEXT("  %d files: %s"), Reasons[Index].Value, *Reasons[Index].Key));
		}
	}

	Lines.Add(FString());
	Lines.Add(FString::Printf(TEXT("What the decoder could not read (%d kinds, most widespread first)"), Result.Issues.Num()));

	for (int32 Index = 0; Index < Result.Issues.Num() && Index < MaximumRows; ++Index)
	{
		const FAssetDecoderCoverageIssue& Issue = Result.Issues[Index];
		Lines.Add(FString::Printf(TEXT("  %d assets, %d times, %lld bytes: %s"), Issue.AssetCount, Issue.Occurrences, Issue.Bytes, *Issue.TypeName));
		Lines.Add(FString::Printf(TEXT("      %s"), *Issue.Message));
		Lines.Add(FString::Printf(TEXT("      e.g. %s"), *FString::Join(Issue.Examples, TEXT(", "))));
	}

	Lines.Add(FString());
	Lines.Add(FString::Printf(TEXT("Skeletal meshes: %d found, the start of their data read for %d, the whole of it (imported model included) for %d; %d kinds that did not"),
		Result.SkeletalMeshesScanned, Result.SkeletalMeshesRead, Result.SkeletalMeshesWholeRead, Result.SkeletalMeshIssues.Num()));

	for (int32 Index = 0; Index < Result.SkeletalMeshIssues.Num() && Index < MaximumRows; ++Index)
	{
		const FAssetDecoderCoverageIssue& Issue = Result.SkeletalMeshIssues[Index];
		Lines.Add(FString::Printf(TEXT("  %d meshes, %lld bytes: %s"), Issue.Occurrences, Issue.Bytes, *Issue.TypeName));
		Lines.Add(FString::Printf(TEXT("      %s"), *Issue.Message));
		Lines.Add(FString::Printf(TEXT("      e.g. %s"), *FString::Join(Issue.Examples, TEXT(", "))));
	}

	Lines.Add(FString());
	Lines.Add(FString::Printf(TEXT("Morph targets: %d found, %d read to their last byte; %d kinds that did not"), Result.MorphTargetsScanned, Result.MorphTargetsRead, Result.MorphTargetIssues.Num()));

	for (int32 Index = 0; Index < Result.MorphTargetIssues.Num() && Index < MaximumRows; ++Index)
	{
		const FAssetDecoderCoverageIssue& Issue = Result.MorphTargetIssues[Index];
		Lines.Add(FString::Printf(TEXT("  %d morph targets, %lld bytes: %s"), Issue.Occurrences, Issue.Bytes, *Issue.TypeName));
		Lines.Add(FString::Printf(TEXT("      %s"), *Issue.Message));
		Lines.Add(FString::Printf(TEXT("      e.g. %s"), *FString::Join(Issue.Examples, TEXT(", "))));
	}

	Lines.Add(FString());
	Lines.Add(FString::Printf(TEXT("Data tables: %d found, %d read to their last byte; %d kinds that did not"), Result.DataTablesScanned, Result.DataTablesRead, Result.DataTableIssues.Num()));

	for (int32 Index = 0; Index < Result.DataTableIssues.Num() && Index < MaximumRows; ++Index)
	{
		const FAssetDecoderCoverageIssue& Issue = Result.DataTableIssues[Index];
		Lines.Add(FString::Printf(TEXT("  %d data tables, %lld bytes: %s"), Issue.Occurrences, Issue.Bytes, *Issue.TypeName));
		Lines.Add(FString::Printf(TEXT("      %s"), *Issue.Message));
		Lines.Add(FString::Printf(TEXT("      e.g. %s"), *FString::Join(Issue.Examples, TEXT(", "))));
	}

	Lines.Add(FString());
	Lines.Add(FString::Printf(TEXT("Static meshes: %d found, %d read to their last byte; %d kinds that did not"), Result.StaticMeshesScanned, Result.StaticMeshesRead, Result.StaticMeshIssues.Num()));

	for (int32 Index = 0; Index < Result.StaticMeshIssues.Num() && Index < MaximumRows; ++Index)
	{
		const FAssetDecoderCoverageIssue& Issue = Result.StaticMeshIssues[Index];
		Lines.Add(FString::Printf(TEXT("  %d meshes, %lld bytes: %s"), Issue.Occurrences, Issue.Bytes, *Issue.TypeName));
		Lines.Add(FString::Printf(TEXT("      %s"), *Issue.Message));
		Lines.Add(FString::Printf(TEXT("      e.g. %s"), *FString::Join(Issue.Examples, TEXT(", "))));
	}

	Lines.Add(FString());
	Lines.Add(FString::Printf(
		TEXT("Textures and mesh descriptions: %d found, %d read to their last byte; %d kinds that did not"), Result.BulkDataExportsScanned, Result.BulkDataExportsRead, Result.BulkDataIssues.Num()));

	for (int32 Index = 0; Index < Result.BulkDataIssues.Num() && Index < MaximumRows; ++Index)
	{
		const FAssetDecoderCoverageIssue& Issue = Result.BulkDataIssues[Index];
		Lines.Add(FString::Printf(TEXT("  %d exports, %lld bytes: %s"), Issue.Occurrences, Issue.Bytes, *Issue.TypeName));
		Lines.Add(FString::Printf(TEXT("      %s"), *Issue.Message));
		Lines.Add(FString::Printf(TEXT("      e.g. %s"), *FString::Join(Issue.Examples, TEXT(", "))));
	}

	Lines.Add(FString());
	Lines.Add(FString::Printf(TEXT("Bytecode: %d functions found, %d disassembled completely (%lld statements); %d kinds that did not"), Result.BytecodeFunctionsScanned, Result.BytecodeFunctionsRead,
		Result.BytecodeStatementsRead, Result.BytecodeIssues.Num()));

	for (int32 Index = 0; Index < Result.BytecodeIssues.Num() && Index < MaximumRows; ++Index)
	{
		const FAssetDecoderCoverageIssue& Issue = Result.BytecodeIssues[Index];
		Lines.Add(FString::Printf(TEXT("  %d functions, %lld bytes: %s"), Issue.Occurrences, Issue.Bytes, *Issue.TypeName));
		Lines.Add(FString::Printf(TEXT("      %s"), *Issue.Message));
		Lines.Add(FString::Printf(TEXT("      e.g. %s"), *FString::Join(Issue.Examples, TEXT(", "))));
	}

	Lines.Add(FString());
	Lines.Add(FString::Printf(TEXT("Graph nodes: %d found, %d read to their last byte (%lld pins); %d kinds that did not"), Result.GraphNodesScanned, Result.GraphNodesRead, Result.GraphPinsRead,
		Result.GraphNodeIssues.Num()));

	for (int32 Index = 0; Index < Result.GraphNodeIssues.Num() && Index < MaximumRows; ++Index)
	{
		const FAssetDecoderCoverageIssue& Issue = Result.GraphNodeIssues[Index];
		Lines.Add(FString::Printf(TEXT("  %d nodes in %d assets: %s"), Issue.Occurrences, Issue.AssetCount, *Issue.TypeName));
		Lines.Add(FString::Printf(TEXT("      %s"), *Issue.Message));
		Lines.Add(FString::Printf(TEXT("      e.g. %s"), *FString::Join(Issue.Examples, TEXT(", "))));
	}

	Lines.Add(FString());
	Lines.Add(FString::Printf(TEXT("Bytes outside the tagged properties, by export class (%d kinds, largest first)"), Result.NativeRegions.Num()));

	for (int32 Index = 0; Index < Result.NativeRegions.Num() && Index < MaximumRows; ++Index)
	{
		const FAssetNativeRegionStat& Stat = Result.NativeRegions[Index];
		Lines.Add(FString::Printf(TEXT("  %lld bytes in %d assets (%d times): %s: %s"), Stat.Bytes, Stat.AssetCount, Stat.Occurrences, *Stat.ExportClass, *Stat.Reason));
		Lines.Add(FString::Printf(TEXT("      e.g. %s"), *FString::Join(Stat.Examples, TEXT(", "))));
	}

	return FString::Join(Lines, TEXT("\n")) + TEXT("\n");
}

FString AssetDecoderCoverage::ToJson(const FAssetDecoderCoverageResult& Result)
{
	using FJsonWriter = TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>;

	FString Output;
	const TSharedRef<FJsonWriter> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);

	const auto WriteExamples = [&Writer](const TArray<FString>& Examples) {
		Writer->WriteArrayStart(TEXT("examples"));
		for (const FString& Example : Examples)
		{
			Writer->WriteValue(Example);
		}
		Writer->WriteArrayEnd();
	};

	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("schemaVersion"), static_cast<int64>(1));
	Writer->WriteValue(TEXT("kind"), TEXT("decoderCoverage"));
	Writer->WriteValue(TEXT("folder"), Result.Folder);
	Writer->WriteValue(TEXT("startedAt"), Result.StartedAt.ToIso8601());
	Writer->WriteValue(TEXT("finishedAt"), Result.FinishedAt.ToIso8601());
	Writer->WriteValue(TEXT("cancelled"), Result.bCancelled);

	Writer->WriteObjectStart(TEXT("totals"));
	Writer->WriteValue(TEXT("assetsScanned"), static_cast<int64>(Result.AssetsScanned));
	Writer->WriteValue(TEXT("assetsUnreadable"), static_cast<int64>(Result.AssetsUnreadable));
	Writer->WriteValue(TEXT("exportsScanned"), static_cast<int64>(Result.ExportsScanned));
	Writer->WriteValue(TEXT("propertiesScanned"), static_cast<int64>(Result.PropertiesScanned));
	Writer->WriteValue(TEXT("propertiesDecoded"), static_cast<int64>(Result.PropertiesDecoded));
	Writer->WriteValue(TEXT("propertyBytes"), Result.PropertyBytes);
	Writer->WriteValue(TEXT("propertyBytesUndecoded"), Result.PropertyBytesUndecoded);
	Writer->WriteValue(TEXT("nativeBytes"), Result.NativeBytes);
	Writer->WriteObjectEnd();

	Writer->WriteArrayStart(TEXT("unreadable"));
	for (const TPair<FString, int32>& Pair : Result.UnreadableReasons)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("reason"), Pair.Key);
		Writer->WriteValue(TEXT("files"), static_cast<int64>(Pair.Value));
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

	Writer->WriteArrayStart(TEXT("issues"));
	for (const FAssetDecoderCoverageIssue& Issue : Result.Issues)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("type"), Issue.TypeName);
		Writer->WriteValue(TEXT("message"), Issue.Message);
		Writer->WriteValue(TEXT("occurrences"), static_cast<int64>(Issue.Occurrences));
		Writer->WriteValue(TEXT("assets"), static_cast<int64>(Issue.AssetCount));
		Writer->WriteValue(TEXT("bytes"), Issue.Bytes);
		WriteExamples(Issue.Examples);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

	Writer->WriteObjectStart(TEXT("skeletalMeshes"));
	Writer->WriteValue(TEXT("scanned"), static_cast<int64>(Result.SkeletalMeshesScanned));
	Writer->WriteValue(TEXT("read"), static_cast<int64>(Result.SkeletalMeshesRead));
	Writer->WriteArrayStart(TEXT("issues"));
	for (const FAssetDecoderCoverageIssue& Issue : Result.SkeletalMeshIssues)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("class"), Issue.TypeName);
		Writer->WriteValue(TEXT("message"), Issue.Message);
		Writer->WriteValue(TEXT("occurrences"), static_cast<int64>(Issue.Occurrences));
		Writer->WriteValue(TEXT("bytes"), Issue.Bytes);
		WriteExamples(Issue.Examples);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();

	Writer->WriteObjectStart(TEXT("morphTargets"));
	Writer->WriteValue(TEXT("scanned"), static_cast<int64>(Result.MorphTargetsScanned));
	Writer->WriteValue(TEXT("read"), static_cast<int64>(Result.MorphTargetsRead));
	Writer->WriteArrayStart(TEXT("issues"));
	for (const FAssetDecoderCoverageIssue& Issue : Result.MorphTargetIssues)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("class"), Issue.TypeName);
		Writer->WriteValue(TEXT("message"), Issue.Message);
		Writer->WriteValue(TEXT("occurrences"), static_cast<int64>(Issue.Occurrences));
		Writer->WriteValue(TEXT("bytes"), Issue.Bytes);
		WriteExamples(Issue.Examples);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();

	Writer->WriteObjectStart(TEXT("dataTables"));
	Writer->WriteValue(TEXT("scanned"), static_cast<int64>(Result.DataTablesScanned));
	Writer->WriteValue(TEXT("read"), static_cast<int64>(Result.DataTablesRead));
	Writer->WriteArrayStart(TEXT("issues"));
	for (const FAssetDecoderCoverageIssue& Issue : Result.DataTableIssues)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("class"), Issue.TypeName);
		Writer->WriteValue(TEXT("message"), Issue.Message);
		Writer->WriteValue(TEXT("occurrences"), static_cast<int64>(Issue.Occurrences));
		Writer->WriteValue(TEXT("bytes"), Issue.Bytes);
		WriteExamples(Issue.Examples);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();

	Writer->WriteObjectStart(TEXT("staticMeshes"));
	Writer->WriteValue(TEXT("scanned"), static_cast<int64>(Result.StaticMeshesScanned));
	Writer->WriteValue(TEXT("read"), static_cast<int64>(Result.StaticMeshesRead));
	Writer->WriteArrayStart(TEXT("issues"));
	for (const FAssetDecoderCoverageIssue& Issue : Result.StaticMeshIssues)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("class"), Issue.TypeName);
		Writer->WriteValue(TEXT("message"), Issue.Message);
		Writer->WriteValue(TEXT("occurrences"), static_cast<int64>(Issue.Occurrences));
		Writer->WriteValue(TEXT("bytes"), Issue.Bytes);
		WriteExamples(Issue.Examples);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();

	Writer->WriteObjectStart(TEXT("bulkDataExports"));
	Writer->WriteValue(TEXT("scanned"), static_cast<int64>(Result.BulkDataExportsScanned));
	Writer->WriteValue(TEXT("read"), static_cast<int64>(Result.BulkDataExportsRead));
	Writer->WriteArrayStart(TEXT("issues"));
	for (const FAssetDecoderCoverageIssue& Issue : Result.BulkDataIssues)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("class"), Issue.TypeName);
		Writer->WriteValue(TEXT("message"), Issue.Message);
		Writer->WriteValue(TEXT("occurrences"), static_cast<int64>(Issue.Occurrences));
		Writer->WriteValue(TEXT("bytes"), Issue.Bytes);
		WriteExamples(Issue.Examples);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();

	Writer->WriteObjectStart(TEXT("bytecode"));
	Writer->WriteValue(TEXT("functions"), static_cast<int64>(Result.BytecodeFunctionsScanned));
	Writer->WriteValue(TEXT("read"), static_cast<int64>(Result.BytecodeFunctionsRead));
	Writer->WriteValue(TEXT("statementsRead"), Result.BytecodeStatementsRead);
	Writer->WriteArrayStart(TEXT("issues"));
	for (const FAssetDecoderCoverageIssue& Issue : Result.BytecodeIssues)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("class"), Issue.TypeName);
		Writer->WriteValue(TEXT("message"), Issue.Message);
		Writer->WriteValue(TEXT("occurrences"), static_cast<int64>(Issue.Occurrences));
		Writer->WriteValue(TEXT("bytes"), Issue.Bytes);
		WriteExamples(Issue.Examples);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();

	Writer->WriteObjectStart(TEXT("graphNodes"));
	Writer->WriteValue(TEXT("scanned"), static_cast<int64>(Result.GraphNodesScanned));
	Writer->WriteValue(TEXT("read"), static_cast<int64>(Result.GraphNodesRead));
	Writer->WriteValue(TEXT("pinsRead"), Result.GraphPinsRead);
	Writer->WriteArrayStart(TEXT("issues"));
	for (const FAssetDecoderCoverageIssue& Issue : Result.GraphNodeIssues)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("class"), Issue.TypeName);
		Writer->WriteValue(TEXT("message"), Issue.Message);
		Writer->WriteValue(TEXT("occurrences"), static_cast<int64>(Issue.Occurrences));
		Writer->WriteValue(TEXT("assets"), static_cast<int64>(Issue.AssetCount));
		WriteExamples(Issue.Examples);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();

	Writer->WriteArrayStart(TEXT("nativeRegions"));
	for (const FAssetNativeRegionStat& Stat : Result.NativeRegions)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("exportClass"), Stat.ExportClass);
		Writer->WriteValue(TEXT("reason"), Stat.Reason);
		Writer->WriteValue(TEXT("occurrences"), static_cast<int64>(Stat.Occurrences));
		Writer->WriteValue(TEXT("assets"), static_cast<int64>(Stat.AssetCount));
		Writer->WriteValue(TEXT("bytes"), Stat.Bytes);
		WriteExamples(Stat.Examples);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

	Writer->WriteObjectEnd();
	Writer->Close();

	return Output;
}

bool AssetDecoderCoverage::SaveToFile(const FAssetDecoderCoverageResult& Result, const FString& Filename, FText& OutError)
{
	if (Filename.IsEmpty())
	{
		OutError = LOCTEXT("NoReportFilename", "No filename was provided for the report.");
		return false;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);

	const FString Content = FPaths::GetExtension(Filename).Equals(TEXT("json"), ESearchCase::IgnoreCase) ? ToJson(Result) : ToText(Result, MAX_int32);

	if (!FFileHelper::SaveStringToFile(Content, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FText::Format(LOCTEXT("ReportWriteFailed", "The report could not be written to:\n{0}"), FText::FromString(Filename));
		return false;
	}

	return true;
}

// ASI.DecodeCoverage <Folder> [ReportFile]: scans a folder from the console or a commandlet run, logs the totals and saves the report
// as text and JSON. Useful for surveying a project's assets without opening the editor UI.
static FAutoConsoleCommand DecodeCoverageCommand(TEXT("ASI.DecodeCoverage"),
	TEXT("ASI.DecodeCoverage <Folder> [ReportFile]: report what the asset decoder cannot read in the .uasset files under Folder."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) {
		if (Args.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("ASI.DecodeCoverage needs a folder."));
			return;
		}

		const FString Folder = Args[0];
		const FString ReportFile = Args.Num() > 1 ? Args[1] : FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AssetSerializationInspector"), TEXT("Coverage"), TEXT("DecoderCoverage.txt"));

		double LastLog = FPlatformTime::Seconds();
		const FAssetDecoderCoverageResult Result = AssetDecoderCoverage::Run(Folder, [&LastLog](const int32 Index, const int32 Total, const FString&) {
			if (FPlatformTime::Seconds() - LastLog > 15.0)
			{
				LastLog = FPlatformTime::Seconds();
				UE_LOG(LogTemp, Display, TEXT("ASI.DecodeCoverage: %d of %d"), Index, Total);
			}
			return true;
		});

		FText Error;
		AssetDecoderCoverage::SaveToFile(Result, ReportFile, Error);
		AssetDecoderCoverage::SaveToFile(Result, FPaths::ChangeExtension(ReportFile, TEXT("json")), Error);

		UE_LOG(LogTemp, Display, TEXT("ASI.DecodeCoverage: %d assets, %d unreadable, %d/%d properties decoded, %d kinds of failure. Report: %s"), Result.AssetsScanned, Result.AssetsUnreadable,
			Result.PropertiesDecoded, Result.PropertiesScanned, Result.Issues.Num(), *ReportFile);
	}));

#undef LOCTEXT_NAMESPACE
