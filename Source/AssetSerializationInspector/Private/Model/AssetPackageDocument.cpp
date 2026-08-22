// Copyright Diego Merayo Merayo. All Rights Reserved
#include "Model/AssetPackageDocument.h"

int64 FAssetPackageDocument::GetFileSize() const
{
	return FileData.Num();
}

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

FString FAssetPackageDocument::ResolveNameReference(const FAssetPackageNameReference& Reference) const
{
	if (!Reference.IsValid(NameMap.Num()))
	{
		return FString::Printf(TEXT("<invalid name index %d>"), Reference.NameIndex);
	}

	FString Result = NameMap[Reference.NameIndex].Name;

	/*
	 * FName's internal number is offset by one:
	 *
	 * Number == 0  -> Name
	 * Number == 1  -> Name_0
	 * Number == 2  -> Name_1
	 */
	if (Reference.Number > 0)
	{
		Result += FString::Printf(TEXT("_%d"), Reference.Number - 1);
	}

	return Result;
}

FString FAssetPackageDocument::DescribePackageIndex(const FAssetPackageIndexReference& Reference) const
{
	switch (Reference.GetKind())
	{
		case EAssetPackageIndexKind::Null:
			return TEXT("Null");

		case EAssetPackageIndexKind::Import:
			return FString::Printf(TEXT("Import[%d]"), Reference.GetArrayIndex());

		case EAssetPackageIndexKind::Export:
			return FString::Printf(TEXT("Export[%d]"), Reference.GetArrayIndex());

		default:
			return TEXT("<invalid package index>");
	}
}