// Copyright Diego Merayo Merayo. All Rights Reserved
#include "Model/AssetPackageDocument.h"

bool FAssetPackageDocument::IsValidRange(const int64 Offset, const int64 Size) const
{
	if (Offset < 0 || Size < 0)
	{
		return false;
	}

	const int64 FileSize = FileData.Num();

	if (Offset > FileSize)
	{
		return false;
	}

	return Size <= FileSize - Offset;
}

const FAssetPackageNameEntry* FAssetPackageDocument::FindNameEntry(const int32 NameIndex) const
{
	return NameMap.IsValidIndex(NameIndex) ? &NameMap[NameIndex] : nullptr;
}

FString FAssetPackageDocument::ResolveNameIndex(const int32 NameIndex) const
{
	const FAssetPackageNameEntry* Entry = FindNameEntry(NameIndex);

	return Entry ? Entry->Name : FString::Printf(TEXT("<invalid name %d>"), NameIndex);
}
