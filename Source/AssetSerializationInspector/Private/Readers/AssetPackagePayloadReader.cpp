// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Readers/AssetPackagePayloadReader.h"

#include "Model/AssetPackageDocument.h"

FAssetPackagePayloadReader::FAssetPackagePayloadReader(const FAssetPackageDocument& InDocument, const int64 InOffset, const int64 InSize)
	: FAssetPackageMemoryReader(InDocument.FileData, InOffset, InSize)
	, Document(InDocument)
{
	SetIsLoading(true);
	SetIsPersistent(true);

	const FPackageFileSummary& Summary = Document.PackageSummary;

	SetUEVer(Summary.GetFileVersionUE());
	SetLicenseeUEVer(Summary.GetFileVersionLicenseeUE());
	SetCustomVersions(Summary.GetCustomVersionContainer());
}

FArchive& FAssetPackagePayloadReader::operator<<(FName& Value)
{
	int32 NameIndex = INDEX_NONE;
	int32 Number = 0;

	*this << NameIndex;
	*this << Number;

	if (IsError())
	{
		Value = NAME_None;
		return *this;
	}

	const FAssetPackageNameEntry* Entry = Document.FindNameEntry(NameIndex);

	if (Entry == nullptr)
	{
		SetError();
		Value = NAME_None;
		return *this;
	}

	if (NameIndex == 0 && Entry->Name == TEXT("None") && Number == 0)
	{
		Value = NAME_None;
		return *this;
	}

	const FString Resolved = Document.ResolveNameReference({ NameIndex, Number });

	Value = FName(*Resolved);

	return *this;
}