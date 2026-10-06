// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Coverage/AssetDecoderBlindSpots.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Compare/AssetFolderComparison.h"
#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Serialization/AssetPropertyValueDecoder.h"
#include "Trace/AssetPackageFieldDecoder.h"
#include "Trace/AssetSerializationTrace.h"

namespace
{
	constexpr int32 MaximumBlindSpotExamples = 4;

	/** Everything a decoded value says, in one string: two decodings with the same signature show the same thing. */
	void AppendSignature(const FAssetDecodedPropertyValue& Value, FString& Out)
	{
		Out += FString::Printf(TEXT("%d|%s|%s|%s|%d{"), static_cast<int32>(Value.Status), *Value.TypeName, *Value.Value, *Value.Error, Value.Children.Num());
		for (const FAssetDecodedPropertyValue& Child : Value.Children)
		{
			AppendSignature(Child, Out);
		}
		Out += TEXT("}");
	}

	/** The innermost value whose bytes include the offset: where the decoder holds that byte. */
	const FAssetDecodedPropertyValue& InnermostAt(const FAssetDecodedPropertyValue& Value, const int64 Offset)
	{
		for (const FAssetDecodedPropertyValue& Child : Value.Children)
		{
			if (Child.Size > 0 && Offset >= Child.AbsoluteOffset && Offset < Child.AbsoluteOffset + Child.Size)
			{
				return InnermostAt(Child, Offset);
			}
		}
		return Value;
	}
} // namespace

FAssetDecoderBlindSpotResult AssetDecoderBlindSpots::Run(
	const FString& Folder, const int32 MaximumPropertyBytes, const int32 MaximumPerType, TFunctionRef<bool(int32 Index, int32 Total)> ShouldContinue)
{
	FAssetDecoderBlindSpotResult Result;
	Result.Folder = Folder;

	TMap<FString, FAssetDecoderBlindSpot> Spots;
	TMap<FString, int32> TestedPerType;

	const TArray<FString> Files = AssetFolderComparison::FindPackageFiles(Folder);

	for (int32 Index = 0; Index < Files.Num(); ++Index)
	{
		if (!ShouldContinue(Index, Files.Num()))
		{
			Result.bCancelled = true;
			break;
		}

		FText Error;
		const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(FPaths::Combine(Folder, Files[Index]), Error);
		if (!Document.IsValid() || !Document->NameMapError.IsEmpty() || !Document->ImportMapError.IsEmpty() || !Document->ExportMapError.IsEmpty())
		{
			continue;
		}

		++Result.AssetsScanned;

		const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);
		TSet<FString> TypesInAsset;

		// The bytes are changed in this copy of the document and put back, one at a time.
		auto& Data = Document->FileData;

		for (const FAssetPackageExportEntry& Export : Document->ExportMap)
		{
			const FAssetSerializationTrace* Trace = Traces->FindExportTrace(Export.Index);
			if (Trace == nullptr || !Trace->Root.IsValid())
			{
				continue;
			}

			for (const TSharedPtr<FAssetSerializationTraceNode>& Node : Trace->Root->Children)
			{
				if (!Node.IsValid() || Node->Kind != EAssetSerializationTraceKind::Property || Node->Size <= 0 || Node->Size > MaximumPropertyBytes)
				{
					continue;
				}

				const FAssetDecodedPropertyValue Original = FAssetPropertyValueDecoder::Decode(*Document, *Node, Export.SerialOffset);
				if (!Original.IsSuccess())
				{
					continue;
				}

				int32& Tested = TestedPerType.FindOrAdd(Node->TypeName);
				if (Tested >= MaximumPerType)
				{
					continue;
				}
				++Tested;
				++Result.PropertiesTested;

				FString OriginalSignature;
				AppendSignature(Original, OriginalSignature);

				const int64 Start = Export.SerialOffset + Node->Offset;
				TMap<FString, TArray<int64>> BlindByType;

				for (int64 Byte = Start; Byte < Start + Node->Size && Byte < Data.Num(); ++Byte)
				{
					// Bytes inside a value that has parts but outside every part (the tags that name and size each field of a struct)
					// say how the value is laid out, not what it holds: they are not counted.
					const FAssetDecodedPropertyValue& Holder = InnermostAt(Original, Byte);
					if (!Holder.Children.IsEmpty())
					{
						continue;
					}

					Data[Byte] ^= 0x01;
					const FAssetDecodedPropertyValue Changed = FAssetPropertyValueDecoder::Decode(*Document, *Node, Export.SerialOffset);
					Data[Byte] ^= 0x01;

					FString ChangedSignature;
					AppendSignature(Changed, ChangedSignature);

					const FString HolderType = Holder.TypeName;
					FAssetDecoderBlindSpot& Spot = Spots.FindOrAdd(HolderType);
					Spot.TypeName = HolderType;
					++Spot.BytesTested;
					++Result.BytesTested;

					if (ChangedSignature == OriginalSignature)
					{
						++Spot.BlindBytes;
						++Result.BlindBytes;
						BlindByType.FindOrAdd(HolderType).Add(Byte - Start);
					}
				}

				for (const TPair<FString, TArray<int64>>& Pair : BlindByType)
				{
					FAssetDecoderBlindSpot& Spot = Spots.FindOrAdd(Pair.Key);
					if (!TypesInAsset.Contains(Pair.Key))
					{
						TypesInAsset.Add(Pair.Key);
						++Spot.AssetCount;
					}

					if (Spot.Examples.Num() < MaximumBlindSpotExamples)
					{
						TArray<FString> Offsets;
						for (int32 Item = 0; Item < Pair.Value.Num() && Item < 6; ++Item)
						{
							Offsets.Add(LexToString(Pair.Value[Item]));
						}
						Spot.Examples.Add(FString::Printf(TEXT("%s: %s, byte(s) %s of %lld"), *Files[Index], *Node->Name, *FString::Join(Offsets, TEXT(",")), Node->Size));
					}
				}
			}
		}
	}

	for (const TPair<FString, FAssetDecoderBlindSpot>& Pair : Spots)
	{
		if (Pair.Value.BlindBytes > 0)
		{
			Result.Spots.Add(Pair.Value);
		}
	}
	Result.Spots.Sort([](const FAssetDecoderBlindSpot& Left, const FAssetDecoderBlindSpot& Right) { return Left.BlindBytes > Right.BlindBytes; });

	return Result;
}

FString AssetDecoderBlindSpots::ToText(const FAssetDecoderBlindSpotResult& Result)
{
	TArray<FString> Lines;
	Lines.Add(TEXT("Asset Serialization Inspector: decoder blind spots"));
	Lines.Add(FString::Printf(TEXT("Folder: %s%s"), *Result.Folder, Result.bCancelled ? TEXT(" (cancelled)") : TEXT("")));
	Lines.Add(FString::Printf(TEXT("%d assets, %d properties tested, %lld bytes changed one at a time, %lld left the decoded value as it was"), Result.AssetsScanned, Result.PropertiesTested,
		Result.BytesTested, Result.BlindBytes));
	Lines.Add(FString());

	for (const FAssetDecoderBlindSpot& Spot : Result.Spots)
	{
		Lines.Add(FString::Printf(TEXT("%lld of %lld bytes in %d assets: %s"), Spot.BlindBytes, Spot.BytesTested, Spot.AssetCount, *Spot.TypeName));
		for (const FString& Example : Spot.Examples)
		{
			Lines.Add(FString::Printf(TEXT("      e.g. %s"), *Example));
		}
	}

	return FString::Join(Lines, TEXT("\n")) + TEXT("\n");
}

// ASI.DecodeBlindSpots <Folder> [ReportFile]: changes the bytes of the decoded values one at a time and reports the types whose
// decoded value does not notice (a value that is read to find where it ends, but whose content is not kept).
static FAutoConsoleCommand DecodeBlindSpotsCommand(TEXT("ASI.DecodeBlindSpots"),
	TEXT("ASI.DecodeBlindSpots <Folder> [ReportFile]: report the values whose bytes can change without the decoded value changing."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) {
		if (Args.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("ASI.DecodeBlindSpots needs a folder."));
			return;
		}

		const FString ReportFile = Args.Num() > 1 ? Args[1] : FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AssetSerializationInspector"), TEXT("Coverage"), TEXT("DecoderBlindSpots.txt"));

		double LastLog = FPlatformTime::Seconds();
		const FAssetDecoderBlindSpotResult Result = AssetDecoderBlindSpots::Run(Args[0], 256, 25, [&LastLog](const int32 Index, const int32 Total) {
			if (FPlatformTime::Seconds() - LastLog > 15.0)
			{
				LastLog = FPlatformTime::Seconds();
				UE_LOG(LogTemp, Display, TEXT("ASI.DecodeBlindSpots: %d of %d"), Index, Total);
			}
			return true;
		});

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportFile), true);
		FFileHelper::SaveStringToFile(AssetDecoderBlindSpots::ToText(Result), *ReportFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

		UE_LOG(LogTemp, Display, TEXT("ASI.DecodeBlindSpots: %d assets, %d properties, %d types with blind bytes. Report: %s"), Result.AssetsScanned, Result.PropertiesTested, Result.Spots.Num(),
			*ReportFile);
	}));
