// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Diff/AssetPackageDiff.h"

#include "Misc/SecureHash.h"

#include "Model/AssetPackageDocument.h"

namespace
{
	FString HashRange(const FAssetPackageDocument& Document, const int64 Offset, const int64 Size)
	{
		if (Size < 0 || !Document.IsValidRange(Offset, Size))
		{
			return TEXT("<invalid>");
		}

		if (Size == 0)
		{
			return TEXT("");
		}

		const uint8* Data = Document.FileData.GetData() + Offset;
		const FSHAHash Hash = FSHA1::HashBuffer(Data, static_cast<uint64>(Size));

		return Hash.ToString();
	}

	FString HashDocument(const FAssetPackageDocument& Document)
	{
		if (Document.FileData.IsEmpty())
		{
			return TEXT("");
		}

		return FSHA1::HashBuffer(Document.FileData.GetData(), static_cast<uint64>(Document.FileData.Num())).ToString();
	}

	template <typename TValue> void AddSummaryValueDiff(FAssetPackageDiffResult& Result, const TCHAR* Key, const FText& DisplayName, const TValue& OldValue, const TValue& NewValue)
	{
		FAssetPackageDiffEntry Entry;

		Entry.Kind = EAssetPackageDiffKind::SummaryField;
		Entry.Key = Key;
		Entry.DisplayName = DisplayName;
		Entry.OldValue = LexToString(OldValue);
		Entry.NewValue = LexToString(NewValue);
		Entry.State = Entry.OldValue == Entry.NewValue ? EAssetPackageDiffState::Unchanged : EAssetPackageDiffState::Modified;

		Result.Entries.Add(MoveTemp(Entry));
	}

	void CompareSummary(const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument, FAssetPackageDiffResult& Result)
	{
		const FPackageFileSummary& A = OldDocument.PackageSummary;
		const FPackageFileSummary& B = NewDocument.PackageSummary;

		AddSummaryValueDiff(Result, TEXT("TotalHeaderSize"), NSLOCTEXT("AssetPackageDiff", "TotalHeaderSize", "Total header size"), A.TotalHeaderSize, B.TotalHeaderSize);
		AddSummaryValueDiff(Result, TEXT("NameCount"), NSLOCTEXT("AssetPackageDiff", "NameCount", "Name count"), A.NameCount, B.NameCount);
		AddSummaryValueDiff(Result, TEXT("NameOffset"), NSLOCTEXT("AssetPackageDiff", "NameOffset", "Name offset"), A.NameOffset, B.NameOffset);
		AddSummaryValueDiff(Result, TEXT("ImportCount"), NSLOCTEXT("AssetPackageDiff", "ImportCount", "Import count"), A.ImportCount, B.ImportCount);
		AddSummaryValueDiff(Result, TEXT("ImportOffset"), NSLOCTEXT("AssetPackageDiff", "ImportOffset", "Import offset"), A.ImportOffset, B.ImportOffset);
		AddSummaryValueDiff(Result, TEXT("ExportCount"), NSLOCTEXT("AssetPackageDiff", "ExportCount", "Export count"), A.ExportCount, B.ExportCount);
		AddSummaryValueDiff(Result, TEXT("ExportOffset"), NSLOCTEXT("AssetPackageDiff", "ExportOffset", "Export offset"), A.ExportOffset, B.ExportOffset);
		AddSummaryValueDiff(Result, TEXT("PackageFlags"), NSLOCTEXT("AssetPackageDiff", "PackageFlags", "Package flags"), A.GetPackageFlags(), B.GetPackageFlags());
	}

	void CompareNames(const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument, FAssetPackageDiffResult& Result)
	{
		const int32 MaximumCount = FMath::Max(OldDocument.NameMap.Num(), NewDocument.NameMap.Num());

		FAssetPackageDiffEntry Root;
		Root.Kind = EAssetPackageDiffKind::Name;
		Root.DisplayName = NSLOCTEXT("AssetPackageDiff", "NameMap", "Name Map");

		for (int32 Index = 0; Index < MaximumCount; ++Index)
		{
			const bool bHasOld = OldDocument.NameMap.IsValidIndex(Index);
			const bool bHasNew = NewDocument.NameMap.IsValidIndex(Index);

			FAssetPackageDiffEntry Entry;
			Entry.Kind = EAssetPackageDiffKind::Name;
			Entry.Key = FString::Printf(TEXT("Name[%d]"), Index);
			Entry.DisplayName = FText::FromString(Entry.Key);

			if (!bHasOld)
			{
				Entry.State = EAssetPackageDiffState::Added;
				Entry.NewValue = NewDocument.NameMap[Index].Name;
				Entry.NewOffset = NewDocument.NameMap[Index].Offset;
				Entry.NewSize = NewDocument.NameMap[Index].Size;
			}
			else if (!bHasNew)
			{
				Entry.State = EAssetPackageDiffState::Removed;
				Entry.OldValue = OldDocument.NameMap[Index].Name;
				Entry.OldOffset = OldDocument.NameMap[Index].Offset;
				Entry.OldSize = OldDocument.NameMap[Index].Size;
			}
			else
			{
				const FAssetPackageNameEntry& A = OldDocument.NameMap[Index];
				const FAssetPackageNameEntry& B = NewDocument.NameMap[Index];

				Entry.OldValue = A.Name;
				Entry.NewValue = B.Name;
				Entry.OldOffset = A.Offset;
				Entry.NewOffset = B.Offset;
				Entry.OldSize = A.Size;
				Entry.NewSize = B.Size;

				if (A.Name != B.Name || A.NonCasePreservingHash != B.NonCasePreservingHash || A.CasePreservingHash != B.CasePreservingHash)
				{
					Entry.State = EAssetPackageDiffState::Modified;
				}
				else if (A.Offset != B.Offset)
				{
					Entry.State = EAssetPackageDiffState::Moved;
				}
				else
				{
					Entry.State = EAssetPackageDiffState::Unchanged;
				}
			}

			Root.Children.Add(MoveTemp(Entry));
		}

		Result.Entries.Add(MoveTemp(Root));
	}

	TMap<FString, const FAssetPackageImportEntry*> BuildImportPathMap(const FAssetPackageDocument& Document)
	{
		TMap<FString, const FAssetPackageImportEntry*> Result;

		for (const FAssetPackageImportEntry& Import : Document.ImportMap)
		{
			Result.Add(Document.ResolveImportPath(Import.Index), &Import);
		}

		return Result;
	}

	void CompareImports(const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument, FAssetPackageDiffResult& Result)
	{
		const auto OldImports = BuildImportPathMap(OldDocument);
		const auto NewImports = BuildImportPathMap(NewDocument);

		TSet<FString> Keys;
		for (const auto& Pair : OldImports)
		{
			Keys.Add(Pair.Key);
		}
		for (const auto& Pair : NewImports)
		{
			Keys.Add(Pair.Key);
		}

		FAssetPackageDiffEntry Root;
		Root.Kind = EAssetPackageDiffKind::Import;
		Root.DisplayName = NSLOCTEXT("AssetPackageDiff", "ImportMap", "Import Map");

		for (const FString& Key : Keys)
		{
			const FAssetPackageImportEntry* const* OldFound = OldImports.Find(Key);
			const FAssetPackageImportEntry* const* NewFound = NewImports.Find(Key);

			FAssetPackageDiffEntry Entry;
			Entry.Kind = EAssetPackageDiffKind::Import;
			Entry.Key = Key;
			Entry.DisplayName = FText::FromString(Key);

			if (!OldFound)
			{
				Entry.State = EAssetPackageDiffState::Added;
			}
			else if (!NewFound)
			{
				Entry.State = EAssetPackageDiffState::Removed;
			}
			else
			{
				const FAssetPackageImportEntry& A = **OldFound;
				const FAssetPackageImportEntry& B = **NewFound;

				Entry.OldOffset = A.Offset;
				Entry.NewOffset = B.Offset;
				Entry.OldSize = A.Size;
				Entry.NewSize = B.Size;

				if (A.Size != B.Size)
				{
					Entry.State = EAssetPackageDiffState::Modified;
				}
				else if (A.Offset != B.Offset)
				{
					Entry.State = EAssetPackageDiffState::Moved;
				}
				else
				{
					Entry.State = EAssetPackageDiffState::Unchanged;
				}
			}

			Root.Children.Add(MoveTemp(Entry));
		}

		Result.Entries.Add(MoveTemp(Root));
	}

	TMap<FString, const FAssetPackageExportEntry*> BuildExportPathMap(const FAssetPackageDocument& Document)
	{
		TMap<FString, const FAssetPackageExportEntry*> Result;

		for (const FAssetPackageExportEntry& Export : Document.ExportMap)
		{
			Result.Add(Document.ResolveExportPath(Export.Index), &Export);
		}

		return Result;
	}

	void CompareExports(const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument, FAssetPackageDiffResult& Result)
	{
		const auto OldExports = BuildExportPathMap(OldDocument);
		const auto NewExports = BuildExportPathMap(NewDocument);

		TSet<FString> Keys;
		for (const auto& Pair : OldExports)
		{
			Keys.Add(Pair.Key);
		}
		for (const auto& Pair : NewExports)
		{
			Keys.Add(Pair.Key);
		}

		FAssetPackageDiffEntry Root;
		Root.Kind = EAssetPackageDiffKind::Export;
		Root.DisplayName = NSLOCTEXT("AssetPackageDiff", "ExportMap", "Export Map");

		for (const FString& Key : Keys)
		{
			const FAssetPackageExportEntry* const* OldFound = OldExports.Find(Key);
			const FAssetPackageExportEntry* const* NewFound = NewExports.Find(Key);

			FAssetPackageDiffEntry Entry;
			Entry.Kind = EAssetPackageDiffKind::Export;
			Entry.Key = Key;
			Entry.DisplayName = FText::FromString(Key);

			if (!OldFound)
			{
				Entry.State = EAssetPackageDiffState::Added;
			}
			else if (!NewFound)
			{
				Entry.State = EAssetPackageDiffState::Removed;
			}
			else
			{
				const FAssetPackageExportEntry& A = **OldFound;
				const FAssetPackageExportEntry& B = **NewFound;

				const FString OldPayloadHash = HashRange(OldDocument, A.SerialOffset, A.SerialSize);
				const FString NewPayloadHash = HashRange(NewDocument, B.SerialOffset, B.SerialSize);

				const bool bPayloadIdentical = A.SerialSize == B.SerialSize && OldPayloadHash == NewPayloadHash;
				const bool bPayloadMoved = A.SerialOffset != B.SerialOffset;

				if (!bPayloadIdentical)
				{
					Entry.State = EAssetPackageDiffState::Modified;
				}
				else if (bPayloadMoved)
				{
					Entry.State = EAssetPackageDiffState::Moved;
				}
				else
				{
					Entry.State = EAssetPackageDiffState::Unchanged;
				}

				FAssetPackageDiffEntry Payload;
				Payload.Kind = EAssetPackageDiffKind::ExportPayload;
				Payload.DisplayName = NSLOCTEXT("AssetPackageDiff", "Payload", "Serialized Payload");
				Payload.OldOffset = A.SerialOffset;
				Payload.NewOffset = B.SerialOffset;
				Payload.OldSize = A.SerialSize;
				Payload.NewSize = B.SerialSize;
				Payload.OldValue = OldPayloadHash;
				Payload.NewValue = NewPayloadHash;

				if (OldPayloadHash != NewPayloadHash || A.SerialSize != B.SerialSize)
				{
					Payload.State = EAssetPackageDiffState::Modified;
				}
				else if (A.SerialOffset != B.SerialOffset)
				{
					Payload.State = EAssetPackageDiffState::Moved;
				}
				else
				{
					Payload.State = EAssetPackageDiffState::Unchanged;
				}

				Entry.Children.Add(MoveTemp(Payload));
			}

			Root.Children.Add(MoveTemp(Entry));
		}

		Result.Entries.Add(MoveTemp(Root));
	}

} // namespace

FAssetPackageDiffResult FAssetPackageDiff::Compare(const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument)
{
	FAssetPackageDiffResult Result;

	Result.OldFilename = OldDocument.Filename;
	Result.NewFilename = NewDocument.Filename;
	Result.OldFileHash = HashDocument(OldDocument);
	Result.NewFileHash = HashDocument(NewDocument);
	Result.bFilesIdentical = OldDocument.GetFileSize() == NewDocument.GetFileSize() && Result.OldFileHash == Result.NewFileHash;

	if (Result.bFilesIdentical)
	{
		return Result;
	}

	CompareSummary(OldDocument, NewDocument, Result);
	CompareNames(OldDocument, NewDocument, Result);
	CompareImports(OldDocument, NewDocument, Result);
	CompareExports(OldDocument, NewDocument, Result);

	return Result;
}