// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetCookedBulkData.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/BulkData.h"

#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetNativeReader.h"

namespace AssetCookedBulkData
{

	/**
	 * Reads the data resource table of the package, where the cook records every block of bulk data: the cooked mips of a texture
	 * refer to it by index. The table is a version, a count and the entries (FObjectDataResource::Serialize).
	 */
	void ReadDataResources(const FAssetPackageDocument& Document, TArray<FDataResource>& Out, FString& Error)
	{
		const int64 Start = Document.PackageSummary.DataResourceOffset;
		if (Start <= 0)
		{
			return;
		}

		FNativeReader Header(Document, Start, 8);
		const uint32 Version = Header.Read<uint32>();
		const int32 Count = Header.Read<int32>();
		constexpr uint32 FirstVersionWithCookedIndex = 2;
		if (!Header.Ok() || Version < 1 || Count < 0 || Count > 1 << 20)
		{
			Error = TEXT("The data resource table of the package is not in a layout this reading knows");
			return;
		}

		// Flags 4, cooked index 1 (from version 2), offset 8, duplicate offset 8, serial size 8, raw size 8, outer index 4, legacy flags 4.
		const int64 EntrySize = 4 + (Version >= FirstVersionWithCookedIndex ? 1 : 0) + 8 + 8 + 8 + 8 + 4 + 4;
		if (!Document.IsValidRange(Start + 8, EntrySize * Count))
		{
			Error = TEXT("The data resource table ends after the file does");
			return;
		}

		FNativeReader Reader(Document, Start + 8, EntrySize * Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FDataResource& Resource = Out.AddDefaulted_GetRef();
			Reader.Read<uint32>();
			if (Version >= FirstVersionWithCookedIndex)
			{
				Reader.Read<uint8>();
			}
			Resource.SerialOffset = Reader.Read<int64>();
			Reader.Read<int64>();
			Resource.SerialSize = Reader.Read<int64>();
			Resource.RawSize = Reader.Read<int64>();
			Reader.Read<int32>();
			Resource.BulkFlags = Reader.Read<uint32>();
		}
	}

	/** A hash of pixels, short enough to read: what tells that they changed without saying what they hold. */
	FString HashBytes(const uint8* Data, const int64 Size)
	{
		return Size > 0 ? FSHA1::HashBuffer(Data, static_cast<uint64>(Size)).ToString().Left(16) : FString();
	}

	/**
	 * A hash of a range of the sidecar file of the package (.ubulk for mips that stream), when the file is next to the package. Empty
	 * when it is not there, as it is for a package that is not on disk or one copied without its sidecar.
	 */
	FString HashSidecarRange(const FAssetPackageDocument& Document, const TCHAR* Extension, const int64 Offset, const int64 Size)
	{
		if (Document.Filename.IsEmpty() || Offset < 0 || Size <= 0 || Size > (1LL << 30))
		{
			return FString();
		}

		const FString Path = FPaths::ChangeExtension(Document.Filename, Extension);
		const TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Path));
		if (!File.IsValid() || Offset + Size > File->TotalSize())
		{
			return FString();
		}

		TArray64<uint8> Bytes;
		Bytes.SetNumUninitialized(Size);
		File->Seek(Offset);
		File->Serialize(Bytes.GetData(), Size);
		return File->IsError() ? FString() : HashBytes(Bytes.GetData(), Size);
	}

	/** The sidecar file the payload of a mip is in, by its flags; null for a payload that is inline. */
	const TCHAR* SidecarExtension(const uint32 BulkFlags)
	{
		if ((BulkFlags & BULKDATA_PayloadAtEndOfFile) == 0)
		{
			return nullptr;
		}
		if ((BulkFlags & BULKDATA_OptionalPayload) != 0)
		{
			return TEXT("uptnl");
		}
		return TEXT("ubulk");
	}

	bool LoadBytes(const FAssetPackageDocument& Document, const uint32 BulkFlags, const int64 DataOffset, const int64 StoredSize, TArray64<uint8>& Out)
	{
		if (DataOffset < 0 || StoredSize <= 0 || StoredSize > (1LL << 30))
		{
			return false;
		}

		if (const TCHAR* Sidecar = SidecarExtension(BulkFlags))
		{
			if (Document.Filename.IsEmpty())
			{
				return false;
			}

			const TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*FPaths::ChangeExtension(Document.Filename, Sidecar)));
			if (!File.IsValid() || DataOffset + StoredSize > File->TotalSize())
			{
				return false;
			}

			Out.SetNumUninitialized(StoredSize);
			File->Seek(DataOffset);
			File->Serialize(Out.GetData(), StoredSize);
			return !File->IsError();
		}

		if (!Document.IsValidRange(DataOffset, StoredSize))
		{
			return false;
		}

		Out.SetNumUninitialized(StoredSize);
		FMemory::Memcpy(Out.GetData(), Document.FileData.GetData() + DataOffset, StoredSize);
		return true;
	}

	/** Reads the index, and the payload when it follows inline (skipped and hashed); a payload in a sidecar file is hashed from there. */
	bool ReadBulkReference(FNativeReader& Reader, const FAssetPackageDocument& Document, const TArray<FDataResource>& Resources, const TCHAR* What, FBulkReference& Out)
	{
		const int32 ResourceIndex = Reader.Read<int32>();
		if (!Reader.Ok())
		{
			return false;
		}
		if (!Resources.IsValidIndex(ResourceIndex))
		{
			Reader.Fail(FString::Printf(TEXT("%s does not point into the data resource table of the package"), What));
			return false;
		}

		const FDataResource& Resource = Resources[ResourceIndex];
		Out.Flags = Resource.BulkFlags;
		Out.RawSize = Resource.RawSize;
		Out.Offset = Resource.SerialOffset;

		Out.StoredSize = Resource.SerialSize;
		if (const TCHAR* Sidecar = SidecarExtension(Resource.BulkFlags))
		{
			Out.DataOffset = Resource.SerialOffset;
			Out.PayloadHash = HashSidecarRange(Document, Sidecar, Resource.SerialOffset, Resource.SerialSize);
		}
		else
		{
			// Inline: the payload follows the index.
			const int64 Start = Reader.Tell();
			Out.DataOffset = Start;
			Reader.Skip(Resource.SerialSize);
			if (Reader.Ok())
			{
				Out.PayloadHash = HashBytes(Document.FileData.GetData() + Start, Resource.SerialSize);
			}
		}
		return Reader.Ok();
	}

} // namespace AssetCookedBulkData
