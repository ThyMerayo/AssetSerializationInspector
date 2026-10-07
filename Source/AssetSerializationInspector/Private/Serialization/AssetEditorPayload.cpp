// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetEditorPayload.h"

#include "Compression/CompressedBuffer.h"
#include "HAL/FileManager.h"
#include "Memory/SharedBuffer.h"
#include "UObject/PackageTrailer.h"

#include "Model/AssetPackageDocument.h"

namespace
{
	/** EFlags of FEditorBulkData that say where the payload is. */
	constexpr uint32 PayloadFlagIsVirtualized = 1u << 0;
	constexpr uint32 PayloadFlagReferencesLegacyFile = 1u << 2;
	constexpr uint32 PayloadFlagStoredInPackageTrailer = 1u << 9;
} // namespace

bool AssetEditorPayload::Load(const FAssetPackageDocument& Document, const FAssetBulkDataInfo& Bulk, const TCHAR* What, const int64 MaximumBytes, TArray64<uint8>& Out, FString& Error)
{
	if (Bulk.bLegacy || (Bulk.Flags & PayloadFlagReferencesLegacyFile) != 0)
	{
		Error = FString::Printf(TEXT("%s is in the older bulk data format"), What);
		return false;
	}
	if ((Bulk.Flags & PayloadFlagIsVirtualized) != 0)
	{
		Error = FString::Printf(TEXT("%s is virtualized, outside the package"), What);
		return false;
	}
	const bool bInTrailer = (Bulk.Flags & PayloadFlagStoredInPackageTrailer) != 0;
	if (!bInTrailer && Bulk.OffsetInFile < 0)
	{
		Error = FString::Printf(TEXT("%s is not in the package trailer or at an offset of the package"), What);
		return false;
	}
	if (Bulk.PayloadSize <= 0 || Bulk.PayloadSize > MaximumBytes)
	{
		Error = Bulk.PayloadSize <= 0 ? FString::Printf(TEXT("%s is empty"), What) : FString::Printf(TEXT("%s is too large to compare"), What);
		return false;
	}
	if (Document.Filename.IsEmpty())
	{
		Error = TEXT("The package is not on disk, so its trailer cannot be read");
		return false;
	}

	const TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Document.Filename));
	if (!File.IsValid())
	{
		Error = FString::Printf(TEXT("%s could not be read from the package"), What);
		return false;
	}

	FCompressedBuffer Compressed;
	if (bInTrailer)
	{
		UE::FPackageTrailer Trailer;
		if (!UE::FPackageTrailer::TryLoadFromFile(Document.Filename, Trailer))
		{
			Error = TEXT("The package has no trailer");
			return false;
		}

		const FIoHash Id(FWideStringView(*Bulk.ContentHash));
		if (Trailer.FindPayloadStatus(Id) != UE::EPayloadStatus::StoredLocally)
		{
			Error = FString::Printf(TEXT("The trailer does not hold %s"), *FString(What).ToLower());
			return false;
		}

		const int64 Offset = Trailer.FindPayloadOffsetInFile(Id);
		const int64 Size = Trailer.FindPayloadSizeOnDisk(Id);
		if (Offset < 0 || Size <= 0 || Offset + Size > File->TotalSize())
		{
			Error = FString::Printf(TEXT("%s could not be read from the package"), What);
			return false;
		}

		FUniqueBuffer Stored = FUniqueBuffer::Alloc(static_cast<uint64>(Size));
		File->Seek(Offset);
		File->Serialize(Stored.GetData(), Size);
		Compressed = FCompressedBuffer::FromCompressed(Stored.MoveToShared());
	}
	else
	{
		// A package saved without a trailer keeps the payload after its exports, as a compressed buffer, at the offset the record says.
		if (Bulk.OffsetInFile >= File->TotalSize())
		{
			Error = FString::Printf(TEXT("%s could not be read from the package"), What);
			return false;
		}

		File->Seek(Bulk.OffsetInFile);
		Compressed = FCompressedBuffer::Load(*File);
	}

	if (Compressed.IsNull())
	{
		Error = FString::Printf(TEXT("%s is not in a compressed buffer"), What);
		return false;
	}

	const FSharedBuffer Raw = Compressed.Decompress();
	if (Raw.IsNull())
	{
		Error = FString::Printf(TEXT("%s could not be decompressed"), What);
		return false;
	}

	Out.SetNumUninitialized(static_cast<int64>(Raw.GetSize()));
	FMemory::Memcpy(Out.GetData(), Raw.GetData(), Raw.GetSize());
	return true;
}
