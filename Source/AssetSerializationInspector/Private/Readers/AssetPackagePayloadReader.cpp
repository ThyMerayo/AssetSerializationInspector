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

// AssetPackagePayloadReader.cpp
FArchive& FAssetPackagePayloadReader::operator<<(UObject*& Value)
{
	int32 RawPackageIndex = 0;
	*this << RawPackageIndex; // uses your existing int32 Serialize path

	// We're offline and have no linker to resolve this into a real object,
	// but we still need to consume the bytes correctly. Discard for now —
	// FAssetPackageIndexReference{ RawPackageIndex } would let you describe
	// it via Document.DescribePackageIndexDetailed() if you want it later.
	Value = nullptr;

	return *this;
}

bool FAssetPackagePayloadReader::ReadNameReference(FAssetPackageNameReference& OutReference)
{
	int32 NameIndex = INDEX_NONE;
	int32 Number = 0;

	if (!CanRead(sizeof(int32) * 2))
	{
		SetError();
		return false;
	}

	*this << NameIndex;
	*this << Number;

	if (IsError())
	{
		return false;
	}

	OutReference.NameIndex = NameIndex;
	OutReference.Number = Number;

	if (!OutReference.IsValid(Document.NameMap.Num()))
	{
		SetError();
		return false;
	}

	return true;
}

bool FAssetPackagePayloadReader::ReadResolvedName(FString& OutName, FAssetPackageNameReference* OutReference)
{
	FAssetPackageNameReference Reference;

	if (!ReadNameReference(Reference))
	{
		return false;
	}

	OutName = Document.ResolveNameReference(Reference);

	if (OutReference != nullptr)
	{
		*OutReference = Reference;
	}

	return true;
}