// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Readers/AssetPackageReader.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/Archive.h"

#include "Model/AssetPackageDocument.h"

#define LOCTEXT_NAMESPACE "AssetPackageReader"

namespace
{
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

	return Document;
}

#undef LOCTEXT_NAMESPACE