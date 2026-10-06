// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetBulkDataExport.h"

#include "Engine/Texture.h"
#include "UObject/EditorObjectVersion.h"
#include "UObject/UE5MainStreamObjectVersion.h"

#include "Model/AssetPackageDocument.h"
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

	FString DescribeBulkContent(const FAssetBulkDataInfo& Bulk)
	{
		return FString::Printf(TEXT("%s (%lld bytes)"), Bulk.ContentHash.IsEmpty() ? TEXT("none") : *Bulk.ContentHash.Left(16), Bulk.PayloadSize);
	}
} // namespace

FString FAssetBulkDataInfo::DescribeStorage() const
{
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

FString FAssetBulkDataExport::Summarize() const
{
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

	// The record is written with an identifier of its own for each block of data since this version.
	if (Reader.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::VirtualizedBulkDataHaveUniqueGuids)
	{
		Out.Error = TEXT("The package stores its bulk data in an older format");
		return true;
	}

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
			ReadEditorBulkDataRecord(Reader, Out.Bulk);
		}

		// UTexture2D, UTextureCube and the others of its kind: strip flags, whether it is cooked, and the cooked data.
		if (Reader.Ok() && Reader.Remaining() > 0)
		{
			Reader.Read<uint8>();
			Reader.Read<uint8>();
			Out.bCooked = Reader.ReadBool();
			if (Reader.Ok() && Out.bCooked)
			{
				Reader.Fail(TEXT("The platform data of a cooked texture is not read"));
			}
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
			ReadEditorBulkDataRecord(Reader, Out.Bulk);
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
		Add(TEXT("BulkData/Source"), DataName, New.bHasBulkData ? FAssetNativeDataChange::EState::Added : FAssetNativeDataChange::EState::Removed, Old.bHasBulkData ? DescribeBulkContent(Old.Bulk) : FString(),
			New.bHasBulkData ? DescribeBulkContent(New.Bulk) : FString());
	}
	else if (Old.bHasBulkData)
	{
		if (!Old.Bulk.ContentHash.Equals(New.Bulk.ContentHash, ESearchCase::CaseSensitive) || Old.Bulk.PayloadSize != New.Bulk.PayloadSize)
		{
			Add(TEXT("BulkData/Source"), DataName + TEXT(" content"), FAssetNativeDataChange::EState::Modified, DescribeBulkContent(Old.Bulk), DescribeBulkContent(New.Bulk));
		}
		else if (!Old.MeshGuid.Equals(New.MeshGuid, ESearchCase::CaseSensitive))
		{
			Add(TEXT("BulkData/MeshGuid"), TEXT("Mesh description id"), FAssetNativeDataChange::EState::Modified, Old.MeshGuid, New.MeshGuid);
		}

		if ((Old.Bulk.Flags & ~BulkTransientFlags) != (New.Bulk.Flags & ~BulkTransientFlags))
		{
			Add(TEXT("BulkData/Storage"), DataName + TEXT(" storage"), FAssetNativeDataChange::EState::Modified, Old.Bulk.DescribeStorage(), New.Bulk.DescribeStorage());
		}
	}

	if (Old.bCooked != New.bCooked)
	{
		Add(TEXT("BulkData/Cooked"), TEXT("Cooked"), FAssetNativeDataChange::EState::Modified, Old.bCooked ? TEXT("yes") : TEXT("no"), New.bCooked ? TEXT("yes") : TEXT("no"));
	}

	return Changes;
}
