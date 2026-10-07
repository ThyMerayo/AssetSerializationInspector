// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetLegacyBulkData.h"

#include "Misc/SecureHash.h"
#include "Serialization/BulkData.h"

#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetNativeReader.h"

bool FAssetLegacyBulkData::IsAtEndOfFile() const
{
	return (Flags & BULKDATA_PayloadAtEndOfFile) != 0;
}

bool FAssetLegacyBulkData::IsCompressed() const
{
	return (Flags & BULKDATA_SerializeCompressed) != 0;
}

FAssetLegacyBulkData AssetLegacyBulkData::Read(FNativeReader& Reader, const FAssetPackageDocument& Document)
{
	constexpr uint32 FlagBadDataVersion = 1u << 15;

	FAssetLegacyBulkData Out;
	Out.Flags = Reader.Read<uint32>();

	const bool b64Bit = (Out.Flags & BULKDATA_Size64Bit) != 0;
	Out.ElementCount = b64Bit ? Reader.Read<int64>() : Reader.Read<int32>();
	Out.SizeOnDisk = b64Bit ? Reader.Read<int64>() : Reader.Read<int32>();
	const int64 Offset = Reader.Read<int64>();
	if ((Out.Flags & FlagBadDataVersion) != 0)
	{
		Reader.Read<uint16>();
	}
	if ((Out.Flags & BULKDATA_DuplicateNonOptionalPayload) != 0)
	{
		Reader.Read<uint32>();
		if (b64Bit)
		{
			Reader.Read<int64>();
		}
		else
		{
			Reader.Read<int32>();
		}
		Reader.Read<int64>();
	}

	if (!Reader.Ok())
	{
		return Out;
	}
	if (Out.ElementCount < 0 || Out.SizeOnDisk < 0)
	{
		Reader.Fail(TEXT("The header of the older bulk data format has a negative size"));
		return Out;
	}

	const auto Hash = [&Out, &Document](const int64 Start, const int64 Size) {
		if (Size > 0 && Document.IsValidRange(Start, Size))
		{
			Out.PayloadHash = FSHA1::HashBuffer(Document.FileData.GetData() + Start, static_cast<uint64>(Size)).ToString();
		}
	};

	if (!Out.IsAtEndOfFile())
	{
		const int64 Start = Reader.Tell();
		Reader.Skip(Out.SizeOnDisk);
		if (Reader.Ok())
		{
			Hash(Start, Out.SizeOnDisk);
		}
	}
	else
	{
		// The offset is relative to where the bulk data section of the package starts, unless the flags say it is absolute.
		Hash((Out.Flags & BULKDATA_NoOffsetFixUp) != 0 ? Offset : Offset + Document.PackageSummary.BulkDataStartOffset, Out.SizeOnDisk);
	}

	return Out;
}
