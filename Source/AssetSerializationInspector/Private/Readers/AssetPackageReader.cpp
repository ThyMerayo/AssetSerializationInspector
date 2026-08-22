// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Readers/AssetPackageReader.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/Archive.h"
#include "UObject/ObjectVersion.h"

#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageMemoryReader.h"

#define LOCTEXT_NAMESPACE "AssetPackageReader"

namespace
{
	constexpr int32 MaximumReasonableNameLength = 1024 * 1024;

	bool ReadSerializedString(FAssetPackageMemoryReader& Reader, FString& OutString, FText& OutError)
	{
		OutString.Reset();

		int32 SerializedLength = 0;
		Reader << SerializedLength;

		if (Reader.IsError())
		{
			OutError = NSLOCTEXT("AssetPackageReader", "NameLengthReadFailed", "Could not read the serialized name length.");
			return false;
		}

		if (SerializedLength == 0)
		{
			return true;
		}

		if (SerializedLength == MIN_int32)
		{
			OutError = NSLOCTEXT("AssetPackageReader", "InvalidNameLength", "The serialized name length is invalid.");
			return false;
		}

		const bool bIsWide = SerializedLength < 0;
		const int64 CharacterCount = bIsWide ? -static_cast<int64>(SerializedLength) : static_cast<int64>(SerializedLength);

		if (CharacterCount <= 0 || CharacterCount > MaximumReasonableNameLength)
		{
			OutError = FText::Format(NSLOCTEXT("AssetPackageReader", "UnreasonableNameLength", "The name declares an unreasonable length: {0}."), FText::AsNumber(CharacterCount));
			return false;
		}

		if (bIsWide)
		{
			const int64 ByteCount = CharacterCount * sizeof(UTF16CHAR);

			if (!Reader.CanRead(ByteCount))
			{
				OutError = NSLOCTEXT("AssetPackageReader", "WideNameOutsideRegion", "The UTF-16 name extends beyond the Name Map.");
				return false;
			}

			TArray<UTF16CHAR> Characters;
			Characters.SetNumUninitialized(static_cast<int32>(CharacterCount));

			Reader.Serialize(Characters.GetData(), ByteCount);

			if (Reader.IsError())
			{
				OutError = NSLOCTEXT("AssetPackageReader", "WideNameReadFailed", "Could not read a UTF-16 name.");
				return false;
			}

			if (Characters.Last() != 0)
			{
				OutError = NSLOCTEXT("AssetPackageReader", "WideNameMissingTerminator", "A UTF-16 name has no null terminator.");
				return false;
			}

			OutString = FString(StringCast<TCHAR>(Characters.GetData(), static_cast<int32>(CharacterCount - 1)).Get());
		}
		else
		{
			if (!Reader.CanRead(CharacterCount))
			{
				OutError = NSLOCTEXT("AssetPackageReader", "AnsiNameOutsideRegion", "The narrow name extends beyond the Name Map.");
				return false;
			}

			TArray<ANSICHAR> Characters;
			Characters.SetNumUninitialized(static_cast<int32>(CharacterCount));

			Reader.Serialize(Characters.GetData(), CharacterCount);

			if (Reader.IsError())
			{
				OutError = NSLOCTEXT("AssetPackageReader", "AnsiNameReadFailed", "Could not read a narrow name.");
				return false;
			}

			if (Characters.Last() != 0)
			{
				OutError = NSLOCTEXT("AssetPackageReader", "AnsiNameMissingTerminator", "A narrow name has no null terminator.");
				return false;
			}

			OutString = FString(StringCast<TCHAR>(Characters.GetData(), static_cast<int32>(CharacterCount - 1)).Get());
		}

		return true;
	}

	bool ParsePackageSummary(FAssetPackageDocument& Document, FText& OutError)
	{
		Document.PackageSummary = FPackageFileSummary();
		Document.SerializedSummarySize = 0;
		Document.bHasValidPackageSummary = false;

		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Document.Filename));

		if (!Reader)
		{
			OutError = FText::Format(LOCTEXT("CouldNotOpenSummaryFile", "Could not reopen the file to parse its package summary:\n{0}"), FText::FromString(Document.Filename));
			return false;
		}

		// Let FPackageFileSummary's own serialization code interpret the
		// version-dependent on-disk representation.
		*Reader << Document.PackageSummary;

		Document.SerializedSummarySize = Reader->Tell();

		const bool bArchiveError = Reader->IsError();

		Reader->Close();

		if (bArchiveError)
		{
			OutError = FText::Format(LOCTEXT("SummaryReadFailed", "Unreal could not deserialize the package summary:\n{0}"), FText::FromString(Document.Filename));
			return false;
		}

		if (Document.PackageSummary.Tag != PACKAGE_FILE_TAG)
		{
			OutError = FText::Format(LOCTEXT("InvalidPackageTag", "The file does not contain a valid Unreal package tag.\nFile: {0}\nTag: 0x{1}"), FText::FromString(Document.Filename),
				FText::FromString(FString::Printf(TEXT("%08X"), static_cast<uint32>(Document.PackageSummary.Tag))));
			return false;
		}

		if (Document.SerializedSummarySize <= 0 || Document.SerializedSummarySize > Document.GetFileSize())
		{
			OutError = FText::Format(LOCTEXT("InvalidSerializedSummarySize", "The package summary consumed an invalid number of bytes.\nSummary size: {0}\nFile size: {1}"),
				FText::AsNumber(Document.SerializedSummarySize), FText::AsNumber(Document.GetFileSize()));
			return false;
		}

		if (!Document.PackageSummary.IsFileVersionValid())
		{
			OutError = FText::Format(LOCTEXT("UnsupportedPackageVersion", "The package version is not supported by this Unreal Editor build:\n{0}"), FText::FromString(Document.Filename));
			return false;
		}

		Document.bHasValidPackageSummary = true;

		return true;
	}

	int64 FindNameMapEnd(const FAssetPackageDocument& Document)
	{
		const FPackageFileSummary& Summary = Document.PackageSummary;

		const int64 NameOffset = Summary.NameOffset;

		int64 Result = Summary.TotalHeaderSize > NameOffset && Summary.TotalHeaderSize <= Document.GetFileSize() ? Summary.TotalHeaderSize : Document.GetFileSize();

		auto ConsiderOffset = [NameOffset, &Result, &Document](const int64 Candidate) {
			if (Candidate > NameOffset && Candidate <= Document.GetFileSize())
			{
				Result = FMath::Min(Result, Candidate);
			}
		};

		ConsiderOffset(Summary.SoftObjectPathsOffset);
		ConsiderOffset(Summary.GatherableTextDataOffset);
		ConsiderOffset(Summary.ImportOffset);
		ConsiderOffset(Summary.ExportOffset);
		ConsiderOffset(Summary.DependsOffset);
		ConsiderOffset(Summary.SoftPackageReferencesOffset);
		ConsiderOffset(Summary.SearchableNamesOffset);
		ConsiderOffset(Summary.AssetRegistryDataOffset);
		ConsiderOffset(Summary.WorldTileInfoDataOffset);
		ConsiderOffset(Summary.ThumbnailTableOffset);

		return Result;
	}

	bool DecodeNameMap(FAssetPackageDocument& Document, FText& OutError)
	{
		Document.NameMap.Reset();
		Document.NameMapError = FText::GetEmpty();
		Document.bHasDecodedNameMap = false;

		Document.NameMapRegionStart = 0;
		Document.NameMapRegionEnd = 0;
		Document.DecodedNameMapEnd = 0;

		const FPackageFileSummary& Summary = Document.PackageSummary;

		if (!Document.bHasValidPackageSummary)
		{
			OutError = LOCTEXT("NameMapRequiresSummary", "The package summary must be parsed before the Name Map.");
			return false;
		}

		if (Summary.NameCount < 0)
		{
			OutError = FText::Format(LOCTEXT("NegativeNameCount", "The package declares a negative Name Map count: {0}."), FText::AsNumber(Summary.NameCount));
			return false;
		}

		if (Summary.NameCount == 0)
		{
			Document.bHasDecodedNameMap = true;
			return true;
		}

		const int64 NameMapStart = Summary.NameOffset;
		const int64 NameMapEnd = FindNameMapEnd(Document);

		Document.NameMapRegionStart = NameMapStart;
		Document.NameMapRegionEnd = NameMapEnd;

		if (NameMapStart <= 0 || NameMapEnd <= NameMapStart || !Document.IsValidRange(NameMapStart, NameMapEnd - NameMapStart))
		{
			OutError = FText::Format(LOCTEXT("InvalidNameMapRange", "The Name Map has an invalid range: 0x{0}–0x{1}."), FText::FromString(FString::Printf(TEXT("%llX"), NameMapStart)),
				FText::FromString(FString::Printf(TEXT("%llX"), NameMapEnd)));
			return false;
		}

		// Every entry consumes at least:
		// int32 string length + two uint16 hashes.
		constexpr int64 MinimumEntrySize = sizeof(int32) + sizeof(uint16) * 2;

		const int64 RegionSize = NameMapEnd - NameMapStart;

		if (Summary.NameCount > RegionSize / MinimumEntrySize)
		{
			OutError = FText::Format(
				LOCTEXT("ImpossibleNameCount", "The package declares {0} names, but the Name Map contains only {1} bytes."), FText::AsNumber(Summary.NameCount), FText::AsNumber(RegionSize));
			return false;
		}

		FAssetPackageMemoryReader Reader(Document.FileData, NameMapStart, RegionSize);

		if (Reader.IsError())
		{
			OutError = LOCTEXT("CouldNotCreateNameReader", "Could not create the bounded Name Map reader.");
			return false;
		}

		Document.NameMap.Reserve(Summary.NameCount);

		for (int32 Index = 0; Index < Summary.NameCount; ++Index)
		{
			const int64 EntryStart = Reader.Tell();

			FAssetPackageNameEntry Entry;
			Entry.Index = Index;
			Entry.Offset = EntryStart;

			FText EntryError;

			if (!ReadSerializedString(Reader, Entry.Name, EntryError))
			{
				OutError = FText::Format(LOCTEXT("NameEntryStringFailed", "Could not decode Name Map entry {0} at offset 0x{1}:\n{2}"), FText::AsNumber(Index),
					FText::FromString(FString::Printf(TEXT("%llX"), EntryStart)), EntryError);

				Document.NameMapError = OutError;
				Document.NameMap.Reset();

				return false;
			}

			if (!Reader.CanRead(sizeof(uint16) * 2))
			{
				OutError = FText::Format(LOCTEXT("NameEntryHashesOutsideRegion", "The serialized hashes for Name Map entry {0} extend beyond the Name Map."), FText::AsNumber(Index));

				Document.NameMapError = OutError;
				Document.NameMap.Reset();

				return false;
			}

			Reader << Entry.NonCasePreservingHash;
			Reader << Entry.CasePreservingHash;

			if (Reader.IsError())
			{
				OutError = FText::Format(LOCTEXT("NameEntryHashReadFailed", "Could not read the hashes for Name Map entry {0}."), FText::AsNumber(Index));

				Document.NameMapError = OutError;
				Document.NameMap.Reset();

				return false;
			}

			Entry.Size = Reader.Tell() - EntryStart;

			if (Entry.Size <= 0)
			{
				OutError = FText::Format(LOCTEXT("EmptyNameEntry", "Name Map entry {0} consumed no bytes."), FText::AsNumber(Index));

				Document.NameMapError = OutError;
				Document.NameMap.Reset();

				return false;
			}

			Document.NameMap.Add(MoveTemp(Entry));
		}

		Document.DecodedNameMapEnd = Reader.Tell();
		Document.bHasDecodedNameMap = true;

		return true;
	}
} // namespace

TSharedPtr<FAssetPackageDocument> FAssetPackageReader::LoadFromFile(const FString& Filename, FText& OutError)
{
	OutError = FText::GetEmpty();

	if (Filename.IsEmpty())
	{
		OutError = LOCTEXT("EmptyFilename", "No filename was provided.");
		return nullptr;
	}

	const FString FullFilename = FPaths::ConvertRelativePathToFull(Filename);

	if (!FPaths::FileExists(FullFilename))
	{
		OutError = FText::Format(LOCTEXT("FileDoesNotExist", "The file does not exist:\n{0}"), FText::FromString(FullFilename));
		return nullptr;
	}

	const FString Extension = FPaths::GetExtension(FullFilename, true);

	if (!Extension.Equals(TEXT(".uasset"), ESearchCase::IgnoreCase))
	{
		OutError = FText::Format(LOCTEXT("UnsupportedExtension", "Expected a .uasset file, but received:\n{0}"), FText::FromString(Extension));
		return nullptr;
	}

	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*FullFilename));

	if (!Reader)
	{
		OutError = FText::Format(LOCTEXT("CouldNotOpenFile", "Could not open the file for reading:\n{0}"), FText::FromString(FullFilename));
		return nullptr;
	}

	const int64 FileSize = Reader->TotalSize();

	if (FileSize <= 0)
	{
		OutError = FText::Format(LOCTEXT("EmptyFile", "The file is empty:\n{0}"), FText::FromString(FullFilename));
		return nullptr;
	}

	TSharedRef<FAssetPackageDocument> Document = MakeShared<FAssetPackageDocument>();

	Document->Filename = FullFilename;
	Document->FileData.SetNumUninitialized(FileSize);

	Reader->Serialize(Document->FileData.GetData(), FileSize);

	const bool bReadFailed = Reader->IsError();

	Reader->Close();

	if (bReadFailed)
	{
		OutError = FText::Format(LOCTEXT("ReadFailed", "An error occurred while reading:\n{0}"), FText::FromString(FullFilename));

		return nullptr;
	}

	if (!ParsePackageSummary(*Document, OutError))
	{
		return nullptr;
	}

	FText NameMapError;
	if (!DecodeNameMap(*Document, NameMapError))
	{
		// Keep the document usable. A table decoding failure should not prevent
		// inspection of the raw package and summary.
		Document->NameMapError = NameMapError;
	}

	return Document;
}

#undef LOCTEXT_NAMESPACE