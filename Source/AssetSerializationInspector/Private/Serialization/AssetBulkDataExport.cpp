// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetBulkDataExport.h"

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/Archive.h"
#include "Serialization/BulkData.h"
#include "UObject/EditorObjectVersion.h"
#include "UObject/UE5MainStreamObjectVersion.h"

#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetLegacyBulkData.h"
#include "Serialization/AssetNativeReader.h"
#include "Serialization/AssetSchemaReflection.h"

namespace
{
	// EFlags of FEditorBulkData (EditorBulkData.h), by the bit they set.
	constexpr uint32 FlagIsVirtualized = 1u << 0;
	constexpr uint32 FlagHasPayloadSidecarFile = 1u << 1;
	constexpr uint32 FlagReferencesLegacyFile = 1u << 2;
	constexpr uint32 FlagLegacyFileIsCompressed = 1u << 3;
	constexpr uint32 FlagDisablePayloadCompression = 1u << 4;
	constexpr uint32 FlagLegacyKeyWasGuidDerived = 1u << 5;
	constexpr uint32 FlagReferencesWorkspaceDomain = 1u << 8;
	constexpr uint32 FlagStoredInPackageTrailer = 1u << 9;
	constexpr uint32 FlagIsCooked = 1u << 10;

	/** The flags that only describe the object while it is in memory: they say nothing of the data, and a save may or may not keep them. */
	constexpr uint32 BulkTransientFlags = (1u << 6) | (1u << 7) | (1u << 11);

	bool ClassChainHasName(const UClass* Class, const TCHAR* Name)
	{
		for (; Class != nullptr; Class = Class->GetSuperClass())
		{
			if (Class->GetFName() == Name)
			{
				return true;
			}
		}
		return false;
	}

	FString ReadBulkContentHash(FNativeReader& Reader)
	{
		FString Hash;
		for (int32 Index = 0; Index < 20; ++Index)
		{
			Hash += FString::Printf(TEXT("%02x"), Reader.Read<uint8>());
		}
		return Reader.Ok() ? Hash : FString();
	}

	/** FEditorBulkData::Serialize, for a package that stores it with a GUID of its own: the flags, the ids, and when the flags say so, where the data is. */
	void ReadEditorBulkDataRecord(FNativeReader& Reader, FAssetBulkDataInfo& Out)
	{
		Out.Flags = Reader.Read<uint32>();
		Out.Id = Reader.ReadGuid().ToString(EGuidFormats::DigitsWithHyphens);
		Out.ContentHash = ReadBulkContentHash(Reader);
		Out.PayloadSize = Reader.Read<int64>();

		const bool bCooked = (Out.Flags & FlagIsCooked) != 0;
		const bool bInTrailer = (Out.Flags & FlagStoredInPackageTrailer) != 0;
		const bool bReferencing = (Out.Flags & (FlagReferencesLegacyFile | FlagReferencesWorkspaceDomain)) != 0;
		const bool bVirtualized = (Out.Flags & FlagIsVirtualized) != 0;
		if (!bCooked && !bInTrailer && (bReferencing || !bVirtualized))
		{
			Out.OffsetInFile = Reader.Read<int64>();
		}
	}

	/** The header of the format before FEditorBulkData (see AssetLegacyBulkData) as the record of a source image or a mesh description. */
	void ReadLegacyBulkDataRecord(FNativeReader& Reader, const FAssetPackageDocument& Document, FAssetBulkDataInfo& Out)
	{
		const FAssetLegacyBulkData Legacy = AssetLegacyBulkData::Read(Reader, Document);
		Out.bLegacy = true;
		Out.Flags = Legacy.Flags;
		Out.PayloadSize = Legacy.ElementCount;
		Out.ContentHash = Legacy.PayloadHash;
	}

	FString DescribeBulkContent(const FAssetBulkDataInfo& Bulk)
	{
		return FString::Printf(TEXT("%s (%lld bytes)"), Bulk.ContentHash.IsEmpty() ? TEXT("none") : *Bulk.ContentHash.Left(16), Bulk.PayloadSize);
	}

	/** An entry of the data resource table of a package (FObjectDataResource): where a block of bulk data is and how it is stored. */
	struct FDataResource
	{
		uint32 BulkFlags = 0;
		int64 SerialOffset = INDEX_NONE;
		int64 SerialSize = 0;
		int64 RawSize = 0;
	};

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

	/** Whether two mips are the same: their size, their storage and their pixels, which are compared only when both could be read. */
	bool SameMip(const FAssetTextureMip& A, const FAssetTextureMip& B)
	{
		const bool bSamePixels = A.PayloadHash.IsEmpty() || B.PayloadHash.IsEmpty() || A.PayloadHash.Equals(B.PayloadHash, ESearchCase::CaseSensitive);
		return A.SizeX == B.SizeX && A.SizeY == B.SizeY && A.SizeZ == B.SizeZ && A.PayloadSize == B.PayloadSize && A.DescribeStorage() == B.DescribeStorage() && bSamePixels;
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

	/** A block of cooked bulk data (a mip, a chunk of tiles) as the package refers to it: by an index into the data resource table. */
	struct FBulkReference
	{
		uint32 Flags = 0;
		int64 RawSize = 0;
		int64 Offset = INDEX_NONE;

		/** A hash of the payload, when it is inline or in the sidecar file next to the package. */
		FString PayloadHash;
	};

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

		if (const TCHAR* Sidecar = SidecarExtension(Resource.BulkFlags))
		{
			Out.PayloadHash = HashSidecarRange(Document, Sidecar, Resource.SerialOffset, Resource.SerialSize);
		}
		else
		{
			// Inline: the payload follows the index.
			const int64 Start = Reader.Tell();
			Reader.Skip(Resource.SerialSize);
			if (Reader.Ok())
			{
				Out.PayloadHash = HashBytes(Document.FileData.GetData() + Start, Resource.SerialSize);
			}
		}
		return Reader.Ok();
	}

	/** An array of 32 bit values that is skipped: its count must fit what is left of the data. */
	int32 SkipUInt32Array(FNativeReader& Reader, const TCHAR* What)
	{
		const int32 Count = Reader.Read<int32>();
		if (Reader.Ok() && (Count < 0 || static_cast<int64>(Count) * 4 > Reader.Remaining()))
		{
			Reader.Fail(FString::Printf(TEXT("The number of %s does not fit the data"), What));
			return 0;
		}
		Reader.Skip(static_cast<int64>(Count) * 4);
		return Count;
	}

	/**
	 * FVirtualTextureBuiltData::Serialize for a cooked texture: the layout of the tiles (sizes, the chunk and offset of each mip, the
	 * blocks of tiles that exist), the pixel format and fallback color of each layer, and the chunks, each with the hash and size the
	 * engine keeps for it and a reference to its data.
	 */
	void ReadVirtualTextureData(FNativeReader& Reader, const FAssetPackageDocument& Document, const TArray<FDataResource>& Resources, FAssetVirtualTextureData& Out)
	{
		constexpr uint32 MaximumLayers = 8;

		const bool bCooked = Reader.ReadBool();
		Out.NumLayers = Reader.Read<uint32>();
		Out.WidthInBlocks = Reader.Read<uint32>();
		Out.HeightInBlocks = Reader.Read<uint32>();
		Out.TileSize = Reader.Read<uint32>();
		Out.TileBorderSize = Reader.Read<uint32>();
		if (Reader.Ok() && Out.NumLayers > MaximumLayers)
		{
			Reader.Fail(TEXT("A virtual texture does not have a number of layers that makes sense"));
			return;
		}

		SkipUInt32Array(Reader, TEXT("layer offsets"));
		Out.NumMips = Reader.Read<uint32>();
		Out.Width = Reader.Read<uint32>();
		Out.Height = Reader.Read<uint32>();
		SkipUInt32Array(Reader, TEXT("chunks of the mips"));
		SkipUInt32Array(Reader, TEXT("base offsets of the mips"));

		// The blocks of tiles that exist in each mip (FVirtualTextureTileOffsetData).
		const int32 TileOffsets = Reader.Read<int32>();
		if (Reader.Ok() && (TileOffsets < 0 || static_cast<int64>(TileOffsets) * 20 > Reader.Remaining()))
		{
			Reader.Fail(TEXT("The number of tile offset tables does not fit the data"));
			return;
		}
		for (int32 Index = 0; Index < TileOffsets && Reader.Ok(); ++Index)
		{
			Reader.Read<uint32>();
			Reader.Read<uint32>();
			Reader.Read<uint32>();
			SkipUInt32Array(Reader, TEXT("tile block addresses"));
			SkipUInt32Array(Reader, TEXT("tile block offsets"));
		}

		SkipUInt32Array(Reader, TEXT("tiles of the chunks"));
		SkipUInt32Array(Reader, TEXT("tiles of the mips"));
		SkipUInt32Array(Reader, TEXT("tile offsets in the chunks"));

		for (uint32 Layer = 0; Layer < Out.NumLayers && Reader.Ok(); ++Layer)
		{
			Out.LayerFormats.Add(Reader.ReadString());
		}
		Reader.Skip(static_cast<int64>(Out.NumLayers) * 16); // the fallback color of each layer (FLinearColor)

		const int32 ChunkCount = Reader.Read<int32>();
		if (Reader.Ok() && (ChunkCount < 0 || ChunkCount > Reader.Remaining() / 28))
		{
			Reader.Fail(TEXT("The number of chunks does not fit the data"));
			return;
		}
		for (int32 Index = 0; Index < ChunkCount && Reader.Ok(); ++Index)
		{
			FAssetVirtualTextureChunk& Chunk = Out.Chunks.AddDefaulted_GetRef();
			for (int32 Byte = 0; Byte < 20; ++Byte)
			{
				Chunk.ContentHash += FString::Printf(TEXT("%02x"), Reader.Read<uint8>());
			}
			Chunk.SizeInBytes = Reader.Read<uint32>();
			Reader.Read<uint32>(); // the size of the payload of the codecs
			for (uint32 Layer = 0; Layer < Out.NumLayers; ++Layer)
			{
				Reader.Read<uint8>();  // the codec of the layer
				Reader.Read<uint32>(); // and where its payload starts in the chunk
			}

			FBulkReference Data;
			if (!ReadBulkReference(Reader, Document, Resources, TEXT("A chunk"), Data))
			{
				return;
			}
			Chunk.BulkFlags = Data.Flags;

			if (!bCooked)
			{
				Reader.ReadString(); // the key of the chunk in the derived data cache
			}
		}
	}

	/**
	 * FTexturePlatformData as a cooked texture stores it (SerializePlatformData, bulk data format): a flag for the derived data format
	 * and a zeroed block that stands for it, the size, the packed flags, the pixel format, the optional data, the mips, and whether the
	 * texture is virtual. The derived data format is not read.
	 */
	void ReadPlatformData(FNativeReader& Reader, const FAssetPackageDocument& Document, const TArray<FDataResource>& Resources, const bool bMipData, FAssetTexturePlatformData& Out)
	{
		constexpr uint32 PackedCubeMap = 1u << 31;
		constexpr uint32 PackedHasOptData = 1u << 30;
		constexpr uint32 PackedHasCpuCopy = 1u << 29;
		constexpr uint32 PackedNumSlices = PackedHasCpuCopy - 1;

		if (Reader.Read<uint8>() != 0)
		{
			Reader.Fail(TEXT("The platform data is stored as references to derived data, which are not read"));
			return;
		}
		for (int32 Index = 0; Index < 15 && Reader.Ok(); ++Index)
		{
			if (Reader.Read<uint8>() != 0)
			{
				Reader.Fail(TEXT("The block that stands for the derived data format is not empty"));
			}
		}

		Out.SizeX = Reader.Read<int32>();
		Out.SizeY = Reader.Read<int32>();
		const uint32 PackedData = Reader.Read<uint32>();
		Out.NumSlices = static_cast<int32>(PackedData & PackedNumSlices);
		Out.bCubeMap = (PackedData & PackedCubeMap) != 0;
		Out.PixelFormat = Reader.ReadString();

		if (bMipData && (PackedData & PackedHasOptData) != 0)
		{
			Reader.Read<uint32>();
			Out.NumMipsInTail = Reader.Read<int32>();
		}
		if ((PackedData & PackedHasCpuCopy) != 0)
		{
			// A copy of the image the CPU keeps (FSharedImage): its size, slices, format and gamma, then the pixels.
			Reader.Read<int32>();
			Reader.Read<int32>();
			Reader.Read<int32>();
			Reader.Read<uint8>();
			Reader.Read<uint8>();
			const int64 PixelBytes = Reader.Read<int64>();
			const int64 Start = Reader.Tell();
			if (Reader.Ok() && (PixelBytes < 0 || PixelBytes > Reader.Remaining()))
			{
				Reader.Fail(TEXT("The CPU copy of a texture is longer than the data"));
				return;
			}
			Reader.Skip(PixelBytes);
			if (Reader.Ok())
			{
				Out.CpuCopyHash = HashBytes(Document.FileData.GetData() + Start, PixelBytes);
			}
		}

		Out.FirstMipToSerialize = Reader.Read<int32>();
		const int32 NumMips = Reader.Read<int32>();
		if (!Reader.Ok() || NumMips < 0 || NumMips > 64)
		{
			Reader.Fail(TEXT("A texture does not have a number of mips that makes sense"));
			return;
		}

		const bool bEditorOnlyStripped = (Document.PackageSummary.GetPackageFlags() & PKG_FilterEditorOnly) != 0;
		for (int32 MipIndex = 0; MipIndex < NumMips && Reader.Ok(); ++MipIndex)
		{
			FAssetTextureMip& Mip = Out.Mips.AddDefaulted_GetRef();
			if (bMipData)
			{
				FBulkReference Data;
				if (!ReadBulkReference(Reader, Document, Resources, TEXT("A mip"), Data))
				{
					return;
				}
				Mip.BulkFlags = Data.Flags;
				Mip.PayloadSize = Data.RawSize;
				Mip.Offset = Data.Offset;
				Mip.PayloadHash = Data.PayloadHash;
			}

			Mip.SizeX = Reader.Read<int32>();
			Mip.SizeY = Reader.Read<int32>();
			Mip.SizeZ = Reader.Read<int32>();
			if (!bEditorOnlyStripped)
			{
				Reader.Fail(TEXT("A texture cooked for the editor, with the data of the editor kept, is not read"));
			}
		}

		Out.bVirtual = Reader.ReadBool();
		if (Out.bVirtual && Reader.Ok())
		{
			ReadVirtualTextureData(Reader, Document, Resources, Out.Virtual);
		}
	}

	/**
	 * What UTexture::SerializeCookedPlatformData writes: whether the pixels are in the data, then for each pixel format the cook made its
	 * name, the distance to the next one and its platform data, and at the end the name None.
	 */
	void ReadCookedPlatformData(FNativeReader& Reader, const FAssetPackageDocument& Document, const bool bWritesMipDataFlag, FAssetBulkDataExport& Out)
	{
		TArray<FDataResource> Resources;
		FString TableError;
		ReadDataResources(Document, Resources, TableError);
		if (!TableError.IsEmpty())
		{
			Reader.Fail(TableError);
			return;
		}

		// Only UTexture2D writes whether the pixels are in the data (the other kinds always have them).
		const bool bMipData = bWritesMipDataFlag ? Reader.ReadBool() : true;
		while (Reader.Ok())
		{
			const FString Format = Reader.ReadName();
			if (!Reader.Ok() || Format == TEXT("None"))
			{
				break;
			}
			if (Out.PlatformData.Num() >= 8)
			{
				Reader.Fail(TEXT("A texture has more pixel formats than a cook writes"));
				break;
			}

			const int64 SkipStart = Reader.Tell();
			const int64 Skip = Reader.Read<int64>();
			FAssetTexturePlatformData& Data = Out.PlatformData.AddDefaulted_GetRef();
			ReadPlatformData(Reader, Document, Resources, bMipData, Data);
			if (Reader.Ok() && Reader.Tell() != SkipStart + Skip)
			{
				Reader.Fail(TEXT("The platform data does not end where its size says"));
			}
		}
	}
} // namespace

FString FAssetBulkDataInfo::DescribeStorage() const
{
	if (bLegacy)
	{
		return FString::Printf(TEXT("older bulk data format, %s%s"), (Flags & BULKDATA_PayloadAtEndOfFile) != 0 ? TEXT("at the end of the package") : TEXT("inline"),
			(Flags & BULKDATA_SerializeCompressed) != 0 ? TEXT(", compressed") : TEXT(""));
	}

	static const TPair<uint32, const TCHAR*> Names[] = { { FlagIsVirtualized, TEXT("virtualized") }, { FlagHasPayloadSidecarFile, TEXT("sidecar file") },
		{ FlagReferencesLegacyFile, TEXT("legacy file") }, { FlagLegacyFileIsCompressed, TEXT("legacy file compressed") }, { FlagDisablePayloadCompression, TEXT("not compressed") },
		{ FlagLegacyKeyWasGuidDerived, TEXT("key from guid") }, { FlagReferencesWorkspaceDomain, TEXT("workspace domain") }, { FlagStoredInPackageTrailer, TEXT("package trailer") },
		{ FlagIsCooked, TEXT("cooked") } };

	TArray<FString> Parts;
	for (const TPair<uint32, const TCHAR*>& Name : Names)
	{
		if ((Flags & Name.Key) != 0)
		{
			Parts.Add(Name.Value);
		}
	}

	return Parts.IsEmpty() ? FString(TEXT("in the package")) : FString::Join(Parts, TEXT(", "));
}

bool FAssetVirtualTextureChunk::IsInline() const
{
	return (BulkFlags & BULKDATA_PayloadAtEndOfFile) == 0;
}

FString FAssetVirtualTextureChunk::DescribeStorage() const
{
	if (IsInline())
	{
		return TEXT("inline");
	}

	const TCHAR* Extension = SidecarExtension(BulkFlags);
	return FString::Printf(TEXT("%s (.%s)"), (BulkFlags & BULKDATA_OptionalPayload) != 0 ? TEXT("optional") : TEXT("streamed"), Extension != nullptr ? Extension : TEXT("?"));
}

FString FAssetVirtualTextureChunk::Describe() const
{
	return FString::Printf(TEXT("%u bytes, %s, %s"), SizeInBytes, *DescribeStorage(), *ContentHash.Left(16));
}

FString FAssetVirtualTextureData::Describe() const
{
	return FString::Printf(TEXT("%u layers (%s), %ux%u, %u pixel tiles with a %u pixel border, %u mips, %d chunks"), NumLayers, *FString::Join(LayerFormats, TEXT(", ")), Width, Height, TileSize,
		TileBorderSize, NumMips, Chunks.Num());
}

bool FAssetTextureMip::IsInline() const
{
	return (BulkFlags & BULKDATA_PayloadAtEndOfFile) == 0;
}

FString FAssetTextureMip::DescribeStorage() const
{
	if (IsInline())
	{
		return TEXT("inline");
	}

	const TCHAR* Extension = SidecarExtension(BulkFlags);
	return FString::Printf(TEXT("%s (.%s)"), (BulkFlags & BULKDATA_OptionalPayload) != 0 ? TEXT("optional") : TEXT("streamed"), Extension != nullptr ? Extension : TEXT("?"));
}

FString FAssetTextureMip::Describe() const
{
	const FString Size = SizeZ > 1 ? FString::Printf(TEXT("%dx%dx%d"), SizeX, SizeY, SizeZ) : FString::Printf(TEXT("%dx%d"), SizeX, SizeY);
	const FString Pixels = PayloadHash.IsEmpty() ? FString(TEXT("pixels not read")) : FString::Printf(TEXT("pixels %s"), *PayloadHash);
	return FString::Printf(TEXT("%s, %lld bytes, %s, %s"), *Size, PayloadSize, *DescribeStorage(), *Pixels);
}

FString FAssetTexturePlatformData::Describe() const
{
	int32 Inline = 0;
	for (const FAssetTextureMip& Mip : Mips)
	{
		Inline += Mip.IsInline() ? 1 : 0;
	}

	if (bVirtual)
	{
		return FString::Printf(TEXT("virtual texture, %s"), *Virtual.Describe());
	}

	FString Result = FString::Printf(TEXT("%s, %dx%d"), *PixelFormat, SizeX, SizeY);
	if (bCubeMap)
	{
		Result += TEXT(", cube map");
	}
	else if (NumSlices > 1)
	{
		Result += FString::Printf(TEXT(", %d slices"), NumSlices);
	}
	Result += FString::Printf(TEXT(", %d mips (%d inline, %d in files next to the package)"), Mips.Num(), Inline, Mips.Num() - Inline);
	return Result;
}

FString FAssetBulkDataExport::Summarize() const
{
	if (!PlatformData.IsEmpty())
	{
		return FString::Printf(TEXT("%s (cooked): %s"), *Kind, *PlatformData[0].Describe());
	}

	return bHasBulkData ? FString::Printf(TEXT("%s: %s"), *Kind, *DescribeBulkContent(Bulk)) : FString::Printf(TEXT("%s: no editor data"), *Kind);
}

bool AssetBulkDataExport::Decode(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, const int64 NativeOffset, const int64 NativeSize, FAssetBulkDataExport& Out)
{
	const UClass* NativeClass = AssetSchemaReflection::FindNativeClass(Document, Export.Index);
	if (NativeClass == nullptr || NativeSize <= 0 || !Document.IsValidRange(NativeOffset, NativeSize))
	{
		return false;
	}

	const bool bTexture = NativeClass->IsChildOf(UTexture::StaticClass());
	const bool bMeshDescription = ClassChainHasName(NativeClass, TEXT("MeshDescriptionBaseBulkData"));
	if (!bTexture && !bMeshDescription)
	{
		return false;
	}

	Out = FAssetBulkDataExport();
	Out.Offset = NativeOffset;
	Out.Size = NativeSize;
	Out.Kind = bTexture ? TEXT("Texture") : TEXT("Mesh description");

	FNativeReader Reader(Document, NativeOffset, NativeSize);

	// UObject::Serialize ends with the object's GUID, when it has one.
	if (Reader.ReadBool())
	{
		Out.ObjectGuid = Reader.ReadGuid().ToString(EGuidFormats::DigitsWithHyphens);
	}

	if (bTexture)
	{
		// UTexture::Serialize: strip flags, then the source image unless the editor data was stripped.
		const uint8 GlobalStripFlags = Reader.Read<uint8>();
		Reader.Read<uint8>();
		if ((GlobalStripFlags & 1) == 0 && Reader.Ok())
		{
			Out.bHasBulkData = true;
			if (Reader.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::TextureSourceVirtualization)
			{
				ReadLegacyBulkDataRecord(Reader, Document, Out.Bulk);
			}
			else
			{
				ReadEditorBulkDataRecord(Reader, Out.Bulk);
			}
		}

		// UTexture2D, UTextureCube and the others of its kind: strip flags, whether it is cooked, and the cooked data.
		if (Reader.Ok() && Reader.Remaining() > 0)
		{
			Reader.Read<uint8>();
			Reader.Read<uint8>();
			Out.bCooked = Reader.ReadBool();
			if (Reader.Ok() && Out.bCooked)
			{
				ReadCookedPlatformData(Reader, Document, NativeClass->IsChildOf(UTexture2D::StaticClass()), Out);
			}
		}

		// ULightMapTexture2D::Serialize writes its lightmap flags after the data of the texture.
		if (Reader.Ok() && ClassChainHasName(NativeClass, TEXT("LightMapTexture2D")))
		{
			Out.bHasLightmapFlags = true;
			Out.LightmapFlags = Reader.Read<uint32>();
		}
	}
	else
	{
		// UMeshDescriptionBaseBulkData::Serialize and FMeshDescriptionBulkData::Serialize: the record, an id and whether the id is a hash.
		if (Reader.CustomVer(FEditorObjectVersion::GUID) < FEditorObjectVersion::MeshDescriptionBulkDataGuid)
		{
			Reader.Fail(TEXT("The mesh description is stored in an older format"));
		}
		else
		{
			Out.bHasBulkData = true;
			if (Reader.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::MeshDescriptionVirtualization)
			{
				ReadLegacyBulkDataRecord(Reader, Document, Out.Bulk);
			}
			else
			{
				ReadEditorBulkDataRecord(Reader, Out.Bulk);
			}
			Out.MeshGuid = Reader.ReadGuid().ToString(EGuidFormats::DigitsWithHyphens);
			Reader.ReadBool();
		}
	}

	if (!Reader.Ok())
	{
		Out.Error = Reader.GetError();
	}
	else if (Reader.Remaining() != 0)
	{
		Out.Error = FString::Printf(TEXT("%lld bytes follow what this reading knows"), Reader.Remaining());
	}
	else
	{
		Out.bComplete = true;
	}

	return true;
}

TArray<FAssetNativeDataChange> AssetBulkDataExport::Compare(const FAssetBulkDataExport& Old, const FAssetBulkDataExport& New)
{
	TArray<FAssetNativeDataChange> Changes;

	const auto Add = [&Changes](const TCHAR* Key, const FString& Title, const FAssetNativeDataChange::EState State, const FString& OldValue, const FString& NewValue) {
		FAssetNativeDataChange& Change = Changes.AddDefaulted_GetRef();
		Change.Key = Key;
		Change.Title = Title;
		Change.State = State;
		Change.OldValue = OldValue;
		Change.NewValue = NewValue;
	};

	const FString DataName = New.Kind == TEXT("Texture") ? TEXT("Source image") : TEXT("Mesh description");

	if (Old.bHasBulkData != New.bHasBulkData)
	{
		Add(TEXT("BulkData/Source"), DataName, New.bHasBulkData ? FAssetNativeDataChange::EState::Added : FAssetNativeDataChange::EState::Removed,
			Old.bHasBulkData ? DescribeBulkContent(Old.Bulk) : FString(), New.bHasBulkData ? DescribeBulkContent(New.Bulk) : FString());
	}
	else if (Old.bHasBulkData)
	{
		// The hash of a record in the older format is of the stored bytes, and the one of the current format is of the content, so
		// across a resave that upgraded the format only the size can be compared.
		const bool bSameFormat = Old.Bulk.bLegacy == New.Bulk.bLegacy;
		if ((bSameFormat && !Old.Bulk.ContentHash.Equals(New.Bulk.ContentHash, ESearchCase::CaseSensitive)) || Old.Bulk.PayloadSize != New.Bulk.PayloadSize)
		{
			Add(TEXT("BulkData/Source"), DataName + TEXT(" content"), FAssetNativeDataChange::EState::Modified, DescribeBulkContent(Old.Bulk), DescribeBulkContent(New.Bulk));
		}
		else if (!Old.MeshGuid.Equals(New.MeshGuid, ESearchCase::CaseSensitive))
		{
			Add(TEXT("BulkData/MeshGuid"), TEXT("Mesh description id"), FAssetNativeDataChange::EState::Modified, Old.MeshGuid, New.MeshGuid);
		}

		const bool bStorageChanged =
			!bSameFormat || (Old.Bulk.bLegacy ? Old.Bulk.DescribeStorage() != New.Bulk.DescribeStorage() : (Old.Bulk.Flags & ~BulkTransientFlags) != (New.Bulk.Flags & ~BulkTransientFlags));
		if (bStorageChanged)
		{
			Add(TEXT("BulkData/Storage"), DataName + TEXT(" storage"), FAssetNativeDataChange::EState::Modified, Old.Bulk.DescribeStorage(), New.Bulk.DescribeStorage());
		}
	}

	if (Old.bCooked != New.bCooked)
	{
		Add(TEXT("BulkData/Cooked"), TEXT("Cooked"), FAssetNativeDataChange::EState::Modified, Old.bCooked ? TEXT("yes") : TEXT("no"), New.bCooked ? TEXT("yes") : TEXT("no"));
	}

	if (Old.bHasLightmapFlags && New.bHasLightmapFlags && Old.LightmapFlags != New.LightmapFlags)
	{
		Add(TEXT("BulkData/LightmapFlags"), TEXT("Lightmap flags"), FAssetNativeDataChange::EState::Modified, FString::Printf(TEXT("0x%X"), Old.LightmapFlags),
			FString::Printf(TEXT("0x%X"), New.LightmapFlags));
	}

	// The platform data of a cooked texture: the format, the size and the mips, each matched by its place. Where the pixels are in
	// the file (and so their offsets) moves from cook to cook and is not a difference.
	const int32 PlatformCount = FMath::Max(Old.PlatformData.Num(), New.PlatformData.Num());
	for (int32 PlatformIndex = 0; PlatformIndex < PlatformCount; ++PlatformIndex)
	{
		const FString Key = FString::Printf(TEXT("Platform/%d"), PlatformIndex);
		const FString Prefix = PlatformCount > 1 ? FString::Printf(TEXT("Platform data %d: "), PlatformIndex) : FString();
		const auto AddPlatform = [&Changes, &Key](const FString& KeySuffix, const FString& Title, const FAssetNativeDataChange::EState State, const FString& OldValue, const FString& NewValue) {
			FAssetNativeDataChange& Change = Changes.AddDefaulted_GetRef();
			Change.Key = Key + KeySuffix;
			Change.Title = Title;
			Change.State = State;
			Change.OldValue = OldValue;
			Change.NewValue = NewValue;
		};

		if (!Old.PlatformData.IsValidIndex(PlatformIndex))
		{
			AddPlatform(FString(), Prefix + TEXT("Platform data"), FAssetNativeDataChange::EState::Added, FString(), New.PlatformData[PlatformIndex].Describe());
			continue;
		}
		if (!New.PlatformData.IsValidIndex(PlatformIndex))
		{
			AddPlatform(FString(), Prefix + TEXT("Platform data"), FAssetNativeDataChange::EState::Removed, Old.PlatformData[PlatformIndex].Describe(), FString());
			continue;
		}

		const FAssetTexturePlatformData& OldPlatform = Old.PlatformData[PlatformIndex];
		const FAssetTexturePlatformData& NewPlatform = New.PlatformData[PlatformIndex];
		const auto Modified = FAssetNativeDataChange::EState::Modified;

		if (!OldPlatform.PixelFormat.Equals(NewPlatform.PixelFormat, ESearchCase::CaseSensitive))
		{
			AddPlatform(TEXT("/PixelFormat"), Prefix + TEXT("Pixel format"), Modified, OldPlatform.PixelFormat, NewPlatform.PixelFormat);
		}
		if (OldPlatform.SizeX != NewPlatform.SizeX || OldPlatform.SizeY != NewPlatform.SizeY)
		{
			AddPlatform(TEXT("/Size"), Prefix + TEXT("Size"), Modified, FString::Printf(TEXT("%dx%d"), OldPlatform.SizeX, OldPlatform.SizeY),
				FString::Printf(TEXT("%dx%d"), NewPlatform.SizeX, NewPlatform.SizeY));
		}
		if (OldPlatform.NumSlices != NewPlatform.NumSlices || OldPlatform.bCubeMap != NewPlatform.bCubeMap)
		{
			const auto Describe = [](const FAssetTexturePlatformData& Data) {
				return Data.bCubeMap ? FString::Printf(TEXT("cube map, %d slices"), Data.NumSlices) : FString::Printf(TEXT("%d slices"), Data.NumSlices);
			};
			AddPlatform(TEXT("/Slices"), Prefix + TEXT("Slices"), Modified, Describe(OldPlatform), Describe(NewPlatform));
		}
		if (OldPlatform.NumMipsInTail != NewPlatform.NumMipsInTail)
		{
			AddPlatform(TEXT("/MipTail"), Prefix + TEXT("Mips in the tail"), Modified, FString::FromInt(OldPlatform.NumMipsInTail), FString::FromInt(NewPlatform.NumMipsInTail));
		}
		if (OldPlatform.FirstMipToSerialize != NewPlatform.FirstMipToSerialize)
		{
			AddPlatform(TEXT("/FirstMip"), Prefix + TEXT("Mips left out of the cook"), Modified, FString::FromInt(OldPlatform.FirstMipToSerialize), FString::FromInt(NewPlatform.FirstMipToSerialize));
		}
		if (OldPlatform.bVirtual != NewPlatform.bVirtual)
		{
			AddPlatform(TEXT("/Virtual"), Prefix + TEXT("Virtual texture"), Modified, OldPlatform.bVirtual ? TEXT("yes") : TEXT("no"), NewPlatform.bVirtual ? TEXT("yes") : TEXT("no"));
		}
		else if (OldPlatform.bVirtual)
		{
			// The tiled data: its layout, then each chunk by the hash and size the engine keeps for it (where it is stored moves between cooks).
			const FAssetVirtualTextureData& OldVirtual = OldPlatform.Virtual;
			const FAssetVirtualTextureData& NewVirtual = NewPlatform.Virtual;
			if (OldVirtual.LayerFormats != NewVirtual.LayerFormats)
			{
				AddPlatform(TEXT("/VirtualLayers"), Prefix + TEXT("Layers"), Modified, FString::Join(OldVirtual.LayerFormats, TEXT(", ")), FString::Join(NewVirtual.LayerFormats, TEXT(", ")));
			}
			if (OldVirtual.Width != NewVirtual.Width || OldVirtual.Height != NewVirtual.Height || OldVirtual.NumMips != NewVirtual.NumMips || OldVirtual.WidthInBlocks != NewVirtual.WidthInBlocks
				|| OldVirtual.HeightInBlocks != NewVirtual.HeightInBlocks)
			{
				AddPlatform(TEXT("/VirtualSize"), Prefix + TEXT("Virtual texture size"), Modified,
					FString::Printf(TEXT("%ux%u, %u mips, %ux%u blocks"), OldVirtual.Width, OldVirtual.Height, OldVirtual.NumMips, OldVirtual.WidthInBlocks, OldVirtual.HeightInBlocks),
					FString::Printf(TEXT("%ux%u, %u mips, %ux%u blocks"), NewVirtual.Width, NewVirtual.Height, NewVirtual.NumMips, NewVirtual.WidthInBlocks, NewVirtual.HeightInBlocks));
			}
			if (OldVirtual.TileSize != NewVirtual.TileSize || OldVirtual.TileBorderSize != NewVirtual.TileBorderSize)
			{
				AddPlatform(TEXT("/VirtualTiles"), Prefix + TEXT("Tile size"), Modified, FString::Printf(TEXT("%u + border %u"), OldVirtual.TileSize, OldVirtual.TileBorderSize),
					FString::Printf(TEXT("%u + border %u"), NewVirtual.TileSize, NewVirtual.TileBorderSize));
			}
			if (OldVirtual.Chunks.Num() != NewVirtual.Chunks.Num())
			{
				AddPlatform(TEXT("/VirtualChunkCount"), Prefix + TEXT("Number of chunks"), Modified, FString::FromInt(OldVirtual.Chunks.Num()), FString::FromInt(NewVirtual.Chunks.Num()));
			}
			for (int32 ChunkIndex = 0; ChunkIndex < FMath::Min(OldVirtual.Chunks.Num(), NewVirtual.Chunks.Num()); ++ChunkIndex)
			{
				const FAssetVirtualTextureChunk& OldChunk = OldVirtual.Chunks[ChunkIndex];
				const FAssetVirtualTextureChunk& NewChunk = NewVirtual.Chunks[ChunkIndex];
				if (!OldChunk.ContentHash.Equals(NewChunk.ContentHash, ESearchCase::CaseSensitive) || OldChunk.SizeInBytes != NewChunk.SizeInBytes
					|| OldChunk.DescribeStorage() != NewChunk.DescribeStorage())
				{
					AddPlatform(FString::Printf(TEXT("/VirtualChunk/%d"), ChunkIndex), FString::Printf(TEXT("%sChunk %d"), *Prefix, ChunkIndex), Modified, OldChunk.Describe(), NewChunk.Describe());
				}
			}
		}
		if (!OldPlatform.CpuCopyHash.Equals(NewPlatform.CpuCopyHash, ESearchCase::CaseSensitive))
		{
			AddPlatform(TEXT("/CpuCopy"), Prefix + TEXT("CPU copy of the image"), Modified, OldPlatform.CpuCopyHash.IsEmpty() ? FString(TEXT("none")) : OldPlatform.CpuCopyHash,
				NewPlatform.CpuCopyHash.IsEmpty() ? FString(TEXT("none")) : NewPlatform.CpuCopyHash);
		}
		if (OldPlatform.Mips.Num() != NewPlatform.Mips.Num())
		{
			AddPlatform(TEXT("/MipCount"), Prefix + TEXT("Number of mips"), Modified, FString::FromInt(OldPlatform.Mips.Num()), FString::FromInt(NewPlatform.Mips.Num()));
		}

		const int32 MipCount = FMath::Max(OldPlatform.Mips.Num(), NewPlatform.Mips.Num());
		for (int32 MipIndex = 0; MipIndex < MipCount; ++MipIndex)
		{
			const FString MipKey = FString::Printf(TEXT("/Mip/%d"), MipIndex);
			const FString MipTitle = FString::Printf(TEXT("%sMip %d"), *Prefix, MipIndex);
			if (!OldPlatform.Mips.IsValidIndex(MipIndex))
			{
				AddPlatform(MipKey, MipTitle, FAssetNativeDataChange::EState::Added, FString(), NewPlatform.Mips[MipIndex].Describe());
			}
			else if (!NewPlatform.Mips.IsValidIndex(MipIndex))
			{
				AddPlatform(MipKey, MipTitle, FAssetNativeDataChange::EState::Removed, OldPlatform.Mips[MipIndex].Describe(), FString());
			}
			else if (!SameMip(OldPlatform.Mips[MipIndex], NewPlatform.Mips[MipIndex]))
			{
				AddPlatform(MipKey, MipTitle, Modified, OldPlatform.Mips[MipIndex].Describe(), NewPlatform.Mips[MipIndex].Describe());
			}
		}
	}

	return Changes;
}
