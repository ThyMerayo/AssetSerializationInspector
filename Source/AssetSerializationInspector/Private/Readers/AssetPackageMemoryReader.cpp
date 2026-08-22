// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Readers/AssetPackageMemoryReader.h"

FAssetPackageMemoryReader::FAssetPackageMemoryReader(const TArray64<uint8>& InFileData, const int64 InStartOffset, const int64 InSize)
	: FileData(InFileData)
	, RegionStart(InStartOffset)
	, RegionEnd(InStartOffset + InSize)
	, Position(InStartOffset)
{
	SetIsLoading(true);
	SetIsPersistent(true);

	if (InStartOffset < 0 || InSize < 0 || InStartOffset > FileData.Num() || InSize > FileData.Num() - InStartOffset)
	{
		SetError();

		RegionStart = 0;
		RegionEnd = 0;
		Position = 0;
	}
}

void FAssetPackageMemoryReader::Serialize(void* Data, const int64 Num)
{
	if (IsError() || Data == nullptr || !CanRead(Num))
	{
		SetError();
		return;
	}

	if (Num > 0)
	{
		FMemory::Memcpy(Data, FileData.GetData() + Position, Num);

		Position += Num;
	}
}

int64 FAssetPackageMemoryReader::Tell()
{
	return Position;
}

int64 FAssetPackageMemoryReader::TotalSize()
{
	return RegionEnd - RegionStart;
}

void FAssetPackageMemoryReader::Seek(const int64 InPos)
{
	// Positions are absolute file positions.
	if (InPos < RegionStart || InPos > RegionEnd)
	{
		SetError();
		return;
	}

	Position = InPos;
}