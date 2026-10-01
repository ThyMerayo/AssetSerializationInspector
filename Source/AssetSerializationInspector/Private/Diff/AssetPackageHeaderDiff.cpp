// Copyright Diego Merayo Merayo. All Rights Reserved

#include "CoreMinimal.h"
#include "Misc/EngineVersion.h"
#include "Serialization/CustomVersion.h"
#include "UObject/PackageFileSummary.h"

#include "Diff/AssetByteDiff.h"
#include "Diff/AssetPackageDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Model/AssetPackageHeaderLayout.h"

#define LOCTEXT_NAMESPACE "AssetPackageHeaderDiff"

namespace
{
	FString Text(const int32 Value)
	{
		return LexToString(Value);
	}

	FString Text(const uint32 Value)
	{
		return LexToString(Value);
	}

	FString Text(const int64 Value)
	{
		return LexToString(Value);
	}

	FString Text(const bool Value)
	{
		return Value ? TEXT("true") : TEXT("false");
	}

	FString Text(const FString& Value)
	{
		return Value;
	}

	FString Text(const FGuid& Value)
	{
		return Value.ToString();
	}

	FString Text(const FIoHash& Value)
	{
		return LexToString(Value);
	}

	FString Text(const FEngineVersion& Value)
	{
		return Value.ToString();
	}

	FString Hex(const uint32 Value)
	{
		return FString::Printf(TEXT("0x%08X"), Value);
	}

	FString JoinInts(const TArray<int32>& Values)
	{
		TArray<FString> Parts;
		for (const int32 Value : Values)
		{
			Parts.Add(LexToString(Value));
		}
		return FString::Join(Parts, TEXT(", "));
	}

	/** Adds a header field to the list, marked unchanged when both sides print the same. */
	void AddField(TArray<FAssetPackageDiffEntry>& Out, const TCHAR* Key, const FString& OldValue, const FString& NewValue)
	{
		FAssetPackageDiffEntry Entry;
		Entry.Kind = EAssetPackageDiffKind::SummaryField;
		Entry.Key = Key;
		Entry.DisplayName = FText::FromString(FName::NameToDisplayString(Key, false));
		Entry.SemanticPath = FString::Printf(TEXT("Header/Summary/%s"), Key);
		Entry.OldValue = OldValue;
		Entry.NewValue = NewValue;
		Entry.State = OldValue == NewValue ? EAssetPackageDiffState::Unchanged : EAssetPackageDiffState::Modified;
		Out.Add(MoveTemp(Entry));
	}

	template <typename TValue> void AddField(TArray<FAssetPackageDiffEntry>& Out, const TCHAR* Key, const TValue& OldValue, const TValue& NewValue)
	{
		AddField(Out, Key, Text(OldValue), Text(NewValue));
	}

	void AddCustomVersions(TArray<FAssetPackageDiffEntry>& Out, const FPackageFileSummary& Old, const FPackageFileSummary& New)
	{
		TMap<FGuid, int32> OldVersions;
		TMap<FGuid, int32> NewVersions;
		TMap<FGuid, FString> Names;

		const auto Collect = [&Names](const FPackageFileSummary& Summary, TMap<FGuid, int32>& Versions) {
			for (const FCustomVersion& Version : Summary.GetCustomVersionContainer().GetAllVersions())
			{
				Versions.Add(Version.Key, Version.Version);
				Names.FindOrAdd(Version.Key) = Version.GetFriendlyName().ToString();
			}
		};

		Collect(Old, OldVersions);
		Collect(New, NewVersions);

		TArray<FGuid> Keys;
		Names.GetKeys(Keys);
		Keys.Sort([&Names](const FGuid& Left, const FGuid& Right) { return Names[Left] < Names[Right]; });

		for (const FGuid& Key : Keys)
		{
			const int32* OldVersion = OldVersions.Find(Key);
			const int32* NewVersion = NewVersions.Find(Key);
			const FString& Name = Names[Key];

			FAssetPackageDiffEntry Entry;
			Entry.Kind = EAssetPackageDiffKind::SummaryField;
			Entry.Key = FString::Printf(TEXT("CustomVersion:%s"), *Key.ToString());
			Entry.DisplayName = FText::Format(LOCTEXT("CustomVersionName", "Custom version {0}"), FText::FromString(Name.IsEmpty() ? Key.ToString() : Name));
			Entry.SemanticPath = FString::Printf(TEXT("Header/Summary/%s"), *Entry.Key);
			Entry.OldValue = OldVersion != nullptr ? LexToString(*OldVersion) : FString();
			Entry.NewValue = NewVersion != nullptr ? LexToString(*NewVersion) : FString();
			Entry.State = OldVersion == nullptr ? EAssetPackageDiffState::Added
				: NewVersion == nullptr			? EAssetPackageDiffState::Removed
				: *OldVersion == *NewVersion	? EAssetPackageDiffState::Unchanged
												: EAssetPackageDiffState::Modified;
			Out.Add(MoveTemp(Entry));
		}
	}

	/** Every field the summary stores, so a header that changed always shows what changed in it. */
	void AddSummaryFields(TArray<FAssetPackageDiffEntry>& Out, const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument)
	{
		const FPackageFileSummary& A = OldDocument.PackageSummary;
		const FPackageFileSummary& B = NewDocument.PackageSummary;

		AddField(Out, TEXT("FileVersionUE4"), static_cast<int32>(A.GetFileVersionUE().FileVersionUE4), static_cast<int32>(B.GetFileVersionUE().FileVersionUE4));
		AddField(Out, TEXT("FileVersionUE5"), static_cast<int32>(A.GetFileVersionUE().FileVersionUE5), static_cast<int32>(B.GetFileVersionUE().FileVersionUE5));
		AddField(Out, TEXT("FileVersionLicenseeUE"), A.GetFileVersionLicenseeUE(), B.GetFileVersionLicenseeUE());
		AddCustomVersions(Out, A, B);
		AddField(Out, TEXT("PackageFlags"), Hex(A.GetPackageFlags()), Hex(B.GetPackageFlags()));
		AddField(Out, TEXT("TotalHeaderSize"), A.TotalHeaderSize, B.TotalHeaderSize);
		AddField(Out, TEXT("PackageName"), A.PackageName, B.PackageName);
		AddField(Out, TEXT("LocalizationId"), A.LocalizationId, B.LocalizationId);
		AddField(Out, TEXT("NameCount"), A.NameCount, B.NameCount);
		AddField(Out, TEXT("NameOffset"), A.NameOffset, B.NameOffset);
		AddField(Out, TEXT("SoftObjectPathsCount"), A.SoftObjectPathsCount, B.SoftObjectPathsCount);
		AddField(Out, TEXT("SoftObjectPathsOffset"), A.SoftObjectPathsOffset, B.SoftObjectPathsOffset);
		AddField(Out, TEXT("GatherableTextDataCount"), A.GatherableTextDataCount, B.GatherableTextDataCount);
		AddField(Out, TEXT("GatherableTextDataOffset"), A.GatherableTextDataOffset, B.GatherableTextDataOffset);
		AddField(Out, TEXT("MetaDataOffset"), A.MetaDataOffset, B.MetaDataOffset);
		AddField(Out, TEXT("ExportCount"), A.ExportCount, B.ExportCount);
		AddField(Out, TEXT("ExportOffset"), A.ExportOffset, B.ExportOffset);
		AddField(Out, TEXT("ImportCount"), A.ImportCount, B.ImportCount);
		AddField(Out, TEXT("ImportOffset"), A.ImportOffset, B.ImportOffset);
		AddField(Out, TEXT("CellExportCount"), A.CellExportCount, B.CellExportCount);
		AddField(Out, TEXT("CellExportOffset"), A.CellExportOffset, B.CellExportOffset);
		AddField(Out, TEXT("CellImportCount"), A.CellImportCount, B.CellImportCount);
		AddField(Out, TEXT("CellImportOffset"), A.CellImportOffset, B.CellImportOffset);
		AddField(Out, TEXT("DependsOffset"), A.DependsOffset, B.DependsOffset);
		AddField(Out, TEXT("SoftPackageReferencesCount"), A.SoftPackageReferencesCount, B.SoftPackageReferencesCount);
		AddField(Out, TEXT("SoftPackageReferencesOffset"), A.SoftPackageReferencesOffset, B.SoftPackageReferencesOffset);
		AddField(Out, TEXT("SearchableNamesOffset"), A.SearchableNamesOffset, B.SearchableNamesOffset);
		AddField(Out, TEXT("ThumbnailTableOffset"), A.ThumbnailTableOffset, B.ThumbnailTableOffset);
		AddField(Out, TEXT("ImportTypeHierarchiesCount"), A.ImportTypeHierarchiesCount, B.ImportTypeHierarchiesCount);
		AddField(Out, TEXT("ImportTypeHierarchiesOffset"), A.ImportTypeHierarchiesOffset, B.ImportTypeHierarchiesOffset);
		AddField(Out, TEXT("SavedHash"), A.GetSavedHash(), B.GetSavedHash());
		AddField(Out, TEXT("PersistentGuid"), A.PersistentGuid, B.PersistentGuid);
		AddField(Out, TEXT("SavedByEngineVersion"), A.SavedByEngineVersion, B.SavedByEngineVersion);
		AddField(Out, TEXT("CompatibleWithEngineVersion"), A.CompatibleWithEngineVersion, B.CompatibleWithEngineVersion);
		AddField(Out, TEXT("CompressionFlags"), Hex(A.CompressionFlags), Hex(B.CompressionFlags));
		AddField(Out, TEXT("PackageSource"), A.PackageSource, B.PackageSource);
		AddField(Out, TEXT("Unversioned"), A.bUnversioned, B.bUnversioned);
		AddField(Out, TEXT("AssetRegistryDataOffset"), A.AssetRegistryDataOffset, B.AssetRegistryDataOffset);
		AddField(Out, TEXT("BulkDataStartOffset"), A.BulkDataStartOffset, B.BulkDataStartOffset);
		AddField(Out, TEXT("WorldTileInfoDataOffset"), A.WorldTileInfoDataOffset, B.WorldTileInfoDataOffset);
		AddField(Out, TEXT("ChunkIDs"), JoinInts(A.ChunkIDs), JoinInts(B.ChunkIDs));
		AddField(Out, TEXT("PreloadDependencyCount"), A.PreloadDependencyCount, B.PreloadDependencyCount);
		AddField(Out, TEXT("PreloadDependencyOffset"), A.PreloadDependencyOffset, B.PreloadDependencyOffset);
		AddField(Out, TEXT("NamesReferencedFromExportDataCount"), A.NamesReferencedFromExportDataCount, B.NamesReferencedFromExportDataCount);
		AddField(Out, TEXT("PayloadTocOffset"), A.PayloadTocOffset, B.PayloadTocOffset);
		AddField(Out, TEXT("DataResourceOffset"), A.DataResourceOffset, B.DataResourceOffset);

		TArray<FString> OldGenerations;
		TArray<FString> NewGenerations;
		for (const FGenerationInfo& Generation : A.Generations)
		{
			OldGenerations.Add(FString::Printf(TEXT("%d exports/%d names"), Generation.ExportCount, Generation.NameCount));
		}
		for (const FGenerationInfo& Generation : B.Generations)
		{
			NewGenerations.Add(FString::Printf(TEXT("%d exports/%d names"), Generation.ExportCount, Generation.NameCount));
		}
		AddField(Out, TEXT("Generations"), FString::Join(OldGenerations, TEXT("; ")), FString::Join(NewGenerations, TEXT("; ")));
	}

	FString DescribeRegion(const FAssetPackageHeaderRegion& Region)
	{
		FString Text = FString::Printf(TEXT("%lld bytes at 0x%llX"), Region.Size, Region.Offset);

		if (Region.EntryCount != INDEX_NONE)
		{
			Text += FString::Printf(TEXT(", %d entries"), Region.EntryCount);
		}

		return Text;
	}

	bool AreRegionBytesEqual(const FAssetPackageDocument& OldDocument, const FAssetPackageHeaderRegion& OldRegion, const FAssetPackageDocument& NewDocument, const FAssetPackageHeaderRegion& NewRegion)
	{
		if (OldRegion.Size != NewRegion.Size || !OldDocument.IsValidRange(OldRegion.Offset, OldRegion.Size) || !NewDocument.IsValidRange(NewRegion.Offset, NewRegion.Size))
		{
			return false;
		}

		return FMemory::Memcmp(OldDocument.FileData.GetData() + OldRegion.Offset, NewDocument.FileData.GetData() + NewRegion.Offset, OldRegion.Size) == 0;
	}

	bool AreDifferencesShiftedOffsets(const FAssetPackageDocument& OldDocument, const FAssetPackageHeaderRegion& OldRegion, const FAssetPackageDocument& NewDocument,
		const FAssetPackageHeaderRegion& NewRegion, const TArray<FAssetByteDiffSpan>& Spans, int64 Delta, TArray<FAssetByteDiffSpan>& OutShiftedRanges);

	FAssetPackageDiffEntry MakeRegionEntry(const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument, const FAssetPackageHeaderRegion* OldRegion,
		const FAssetPackageHeaderRegion* NewRegion, const int64 HeaderDelta)
	{
		const FAssetPackageHeaderRegion& Named = OldRegion != nullptr ? *OldRegion : *NewRegion;

		FAssetPackageDiffEntry Entry;
		Entry.Kind = EAssetPackageDiffKind::HeaderRegion;
		Entry.Key = Named.Key;
		Entry.DisplayName = Named.Name;
		Entry.SemanticPath = FString::Printf(TEXT("Header/%s"), *Named.Key);

		if (OldRegion != nullptr)
		{
			Entry.OldValue = DescribeRegion(*OldRegion);
			Entry.OldOffset = OldRegion->Offset;
			Entry.OldSize = OldRegion->Size;
		}

		if (NewRegion != nullptr)
		{
			Entry.NewValue = DescribeRegion(*NewRegion);
			Entry.NewOffset = NewRegion->Offset;
			Entry.NewSize = NewRegion->Size;
		}

		if (OldRegion == nullptr)
		{
			Entry.State = EAssetPackageDiffState::Added;
		}
		else if (NewRegion == nullptr)
		{
			Entry.State = EAssetPackageDiffState::Removed;
		}
		else if (!AreRegionBytesEqual(OldDocument, *OldRegion, NewDocument, *NewRegion))
		{
			Entry.State = EAssetPackageDiffState::Modified;

			if (OldRegion->Size == NewRegion->Size)
			{
				Entry.ChangedSpans = FAssetByteDiff::Compare(OldDocument, OldRegion->Offset, OldRegion->Size, NewDocument, NewRegion->Offset, NewRegion->Size);
				for (const FAssetByteDiffSpan& Span : Entry.ChangedSpans)
				{
					Entry.ChangedByteCount += Span.Size;
				}

				// Tables that store absolute file offsets change when the data before them changes size, without changing meaning.
				if (AreDifferencesShiftedOffsets(OldDocument, *OldRegion, NewDocument, *NewRegion, Entry.ChangedSpans, HeaderDelta, Entry.ShiftedOffsetRanges))
				{
					Entry.State = EAssetPackageDiffState::Moved;
					Entry.Explanation = FText::Format(LOCTEXT("ShiftedOffsetsExplanation", "Only absolute file offsets differ: {0} stored offsets {1} by {2} bytes, the header's size change."),
						FText::AsNumber(Entry.ShiftedOffsetRanges.Num()), HeaderDelta < 0 ? LOCTEXT("ShiftedDown", "moved back") : LOCTEXT("ShiftedUp", "moved forward"),
						FText::AsNumber(FMath::Abs(HeaderDelta)));
				}
				else
				{
					Entry.Explanation = FText::Format(LOCTEXT("BytesDifferExplanation", "{0} of {1} bytes differ."), FText::AsNumber(Entry.ChangedByteCount), FText::AsNumber(OldRegion->Size));

					if (!Entry.ShiftedOffsetRanges.IsEmpty())
					{
						Entry.Explanation =
							FText::Format(LOCTEXT("MixedDifferencesExplanation", "{0} {1} of them are stored offsets that moved by the header's size change; the rest are other changes."),
								Entry.Explanation, FText::AsNumber(Entry.ShiftedOffsetRanges.Num()));
					}
				}
			}
		}
		else
		{
			Entry.State = OldRegion->Offset == NewRegion->Offset ? EAssetPackageDiffState::Unchanged : EAssetPackageDiffState::Moved;
		}

		return Entry;
	}

	/** Reads a little-endian integer of 4 or 8 bytes, sign-extended. */
	int64 ReadInteger(const FAssetPackageDocument& Document, const int64 Offset, const int32 Width)
	{
		if (Width == 4)
		{
			int32 Value = 0;
			FMemory::Memcpy(&Value, Document.FileData.GetData() + Offset, sizeof(Value));
			return Value;
		}

		int64 Value = 0;
		FMemory::Memcpy(&Value, Document.FileData.GetData() + Offset, sizeof(Value));
		return Value;
	}

	/**
	 * True when every difference between two same-sized regions is an absolute file offset (a 4 or 8 byte integer) that grew by
	 * exactly Delta. That is what happens to the offsets stored in a table when the data in front of it changes size.
	 */
	bool AreDifferencesShiftedOffsets(const FAssetPackageDocument& OldDocument, const FAssetPackageHeaderRegion& OldRegion, const FAssetPackageDocument& NewDocument,
		const FAssetPackageHeaderRegion& NewRegion, const TArray<FAssetByteDiffSpan>& Spans, const int64 Delta, TArray<FAssetByteDiffSpan>& OutShiftedRanges)
	{
		OutShiftedRanges.Reset();

		if (Delta == 0 || Spans.IsEmpty())
		{
			return false;
		}

		int64 CoveredUntil = 0;
		bool bAllExplained = true;

		for (const FAssetByteDiffSpan& Span : Spans)
		{
			const int64 SpanStart = FMath::Max(Span.Offset, CoveredUntil);
			const int64 SpanEnd = Span.End();

			if (SpanStart >= SpanEnd)
			{
				continue;
			}

			bool bExplained = false;

			for (const int32 Width : { 8, 4 })
			{
				// The integer must contain the whole remaining span, and not start before what is already explained.
				for (int64 Start = FMath::Max(CoveredUntil, SpanEnd - Width); Start <= SpanStart && !bExplained; ++Start)
				{
					if (Start + Width > OldRegion.Size)
					{
						continue;
					}

					if (ReadInteger(NewDocument, NewRegion.Offset + Start, Width) - ReadInteger(OldDocument, OldRegion.Offset + Start, Width) == Delta)
					{
						bExplained = true;
						CoveredUntil = Start + Width;

						FAssetByteDiffSpan& Range = OutShiftedRanges.AddDefaulted_GetRef();
						Range.Offset = Start;
						Range.Size = Width;
					}
				}

				if (bExplained)
				{
					break;
				}
			}

			// Keep going: the offsets that are explained are still worth showing apart from the real changes.
			bAllExplained &= bExplained;
		}

		return bAllExplained;
	}

	FString DescribeThumbnails(const FAssetPackageDocument& Document)
	{
		TArray<FString> Parts;
		for (const FAssetPackageThumbnailEntry& Thumbnail : AssetPackageHeaderLayout::ReadThumbnailIndex(Document))
		{
			Parts.Add(FString::Printf(TEXT("%s %s%s"), *Thumbnail.ObjectClassName, *Thumbnail.ObjectPath, Thumbnail.bEmpty ? TEXT(" (no image)") : TEXT("")));
		}

		return FString::Join(Parts, TEXT(", "));
	}

	FString SignedBytes(const int64 Value)
	{
		return FString::Printf(TEXT("%s%lld"), Value > 0 ? TEXT("+") : TEXT(""), Value);
	}

	/** A sentence about how the header changed size, broken down by region. Empty when its size did not change. */
	FText ExplainHeaderSize(const TArray<FAssetPackageHeaderRegion>& OldRegions, const TArray<FAssetPackageHeaderRegion>& NewRegions, const int64 HeaderDelta)
	{
		if (HeaderDelta == 0)
		{
			return FText::GetEmpty();
		}

		struct FRegionChange
		{
			FText Name;
			int64 Delta = 0;
		};

		TArray<FRegionChange> Changes;

		const auto Consider = [&Changes](const FAssetPackageHeaderRegion& Region, const int64 OldSize, const int64 NewSize) {
			if (OldSize != NewSize)
			{
				Changes.Add({ Region.Name, NewSize - OldSize });
			}
		};

		for (const FAssetPackageHeaderRegion& NewRegion : NewRegions)
		{
			const FAssetPackageHeaderRegion* OldRegion = OldRegions.FindByPredicate([&NewRegion](const FAssetPackageHeaderRegion& Region) { return Region.Key == NewRegion.Key; });
			Consider(NewRegion, OldRegion != nullptr ? OldRegion->Size : 0, NewRegion.Size);
		}

		for (const FAssetPackageHeaderRegion& OldRegion : OldRegions)
		{
			if (!NewRegions.ContainsByPredicate([&OldRegion](const FAssetPackageHeaderRegion& Region) { return Region.Key == OldRegion.Key; }))
			{
				Consider(OldRegion, OldRegion.Size, 0);
			}
		}

		Changes.Sort([](const FRegionChange& Left, const FRegionChange& Right) { return FMath::Abs(Left.Delta) > FMath::Abs(Right.Delta); });

		TArray<FString> Parts;
		for (const FRegionChange& Change : Changes)
		{
			Parts.Add(FString::Printf(TEXT("%s (%s)"), *Change.Name.ToString(), *SignedBytes(Change.Delta)));
		}

		return FText::Format(LOCTEXT("HeaderSizeExplanation", "The header is {0} bytes {1}: {2}."), FText::AsNumber(FMath::Abs(HeaderDelta)),
			HeaderDelta < 0 ? LOCTEXT("Smaller", "smaller") : LOCTEXT("Larger", "larger"), FText::FromString(FString::Join(Parts, TEXT(", "))));
	}

	FText ExplainField(const FAssetPackageDiffEntry& Field, const int64 HeaderDelta, const FText& HeaderExplanation)
	{
		if (Field.State == EAssetPackageDiffState::Unchanged)
		{
			return FText::GetEmpty();
		}

		if (Field.Key == TEXT("SavedHash"))
		{
			return LOCTEXT("SavedHashExplanation", "A hash of the saved file. It changes whenever anything in the file changes, so it is always different after a real change.");
		}

		if (Field.Key == TEXT("PackageSource"))
		{
			return LOCTEXT("PackageSourceExplanation", "A checksum of the saved file's name. It differs when the package was saved under a different file name.");
		}

		if (Field.Key == TEXT("PersistentGuid"))
		{
			return LOCTEXT("PersistentGuidExplanation", "The package's identity. The engine gives the package a new one when it is saved to a different file.");
		}

		if (Field.Key == TEXT("TotalHeaderSize"))
		{
			return HeaderExplanation;
		}

		if (Field.Key.EndsWith(TEXT("Offset")) && Field.OldValue.IsNumeric() && Field.NewValue.IsNumeric())
		{
			const int64 OldOffset = FCString::Atoi64(*Field.OldValue);
			const int64 NewOffset = FCString::Atoi64(*Field.NewValue);

			if (NewOffset == 0)
			{
				return LOCTEXT("OffsetRemovedExplanation", "This part of the package no longer exists, so it has no offset.");
			}

			if (OldOffset == 0)
			{
				return LOCTEXT("OffsetAddedExplanation", "This part of the package did not exist before.");
			}

			if (NewOffset - OldOffset == HeaderDelta)
			{
				return FText::Format(LOCTEXT("OffsetShiftedExplanation", "Moved by {0} bytes, the same as the header's size change: it sits after the part of the header that changed size."),
					FText::FromString(SignedBytes(HeaderDelta)));
			}

			return FText::Format(LOCTEXT("OffsetMovedExplanation", "Moved by {0} bytes."), FText::FromString(SignedBytes(NewOffset - OldOffset)));
		}

		return FText::GetEmpty();
	}

	FText ExplainRegion(const FAssetPackageDiffEntry& Region, const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument)
	{
		const bool bThumbnails = Region.Key == TEXT("ThumbnailTable") || Region.Key == TEXT("ThumbnailData");

		if (Region.State == EAssetPackageDiffState::Removed && bThumbnails)
		{
			return FText::Format(
				LOCTEXT("ThumbnailsRemovedExplanation", "The package was saved without thumbnails. Before it stored: {0}. Thumbnails are only written when the package has them loaded in memory."),
				FText::FromString(DescribeThumbnails(OldDocument)));
		}

		if (Region.State == EAssetPackageDiffState::Added && bThumbnails)
		{
			return FText::Format(LOCTEXT("ThumbnailsAddedExplanation", "The package now stores thumbnails: {0}."), FText::FromString(DescribeThumbnails(NewDocument)));
		}

		if (Region.State == EAssetPackageDiffState::Added)
		{
			return LOCTEXT("RegionAddedExplanation", "Only the new header has this part.");
		}

		if (Region.State == EAssetPackageDiffState::Removed)
		{
			return LOCTEXT("RegionRemovedExplanation", "Only the old header had this part.");
		}

		if (Region.OldSize != Region.NewSize)
		{
			return FText::Format(LOCTEXT("RegionResizedExplanation", "{0} by {1} bytes."), Region.NewSize > Region.OldSize ? LOCTEXT("Grew", "Grew") : LOCTEXT("Shrank", "Shrank"),
				FText::AsNumber(FMath::Abs(Region.NewSize - Region.OldSize)));
		}

		return FText::GetEmpty();
	}

	FString DescribeHeaderSize(const int64 Size, const int64 OtherSize, const bool bIsNew)
	{
		FString Text = FString::Printf(TEXT("%lld bytes"), Size);

		if (bIsNew && Size != OtherSize)
		{
			Text += FString::Printf(TEXT(" (%s%lld)"), Size > OtherSize ? TEXT("+") : TEXT(""), Size - OtherSize);
		}

		return Text;
	}
} // namespace

void AssetPackageDiff::AppendHeaderDiff(const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument, FAssetPackageDiffResult& Result)
{
	const TArray<FAssetPackageHeaderRegion> OldRegions = AssetPackageHeaderLayout::Build(OldDocument);
	const TArray<FAssetPackageHeaderRegion> NewRegions = AssetPackageHeaderLayout::Build(NewDocument);

	FAssetPackageDiffEntry Header;
	Header.Kind = EAssetPackageDiffKind::Header;
	Header.Key = TEXT("Header");
	Header.DisplayName = LOCTEXT("PackageHeader", "Package header");
	Header.SemanticPath = TEXT("Header");

	const int64 OldHeaderSize = AssetPackageHeaderLayout::GetHeaderSize(OldDocument);
	const int64 NewHeaderSize = AssetPackageHeaderLayout::GetHeaderSize(NewDocument);
	Header.OldOffset = 0;
	Header.NewOffset = 0;
	Header.OldSize = OldHeaderSize;
	Header.NewSize = NewHeaderSize;
	const int64 HeaderDelta = NewHeaderSize - OldHeaderSize;
	const FText HeaderExplanation = ExplainHeaderSize(OldRegions, NewRegions, HeaderDelta);
	Header.Explanation = HeaderExplanation;
	Header.OldValue = DescribeHeaderSize(OldHeaderSize, NewHeaderSize, false);
	Header.NewValue = DescribeHeaderSize(NewHeaderSize, OldHeaderSize, true);

	// Regions are matched by key, in the order of the newer header and then any only the older one has.
	TArray<FString> Keys;
	for (const FAssetPackageHeaderRegion& Region : NewRegions)
	{
		Keys.AddUnique(Region.Key);
	}
	for (const FAssetPackageHeaderRegion& Region : OldRegions)
	{
		Keys.AddUnique(Region.Key);
	}

	const auto FindRegion = [](const TArray<FAssetPackageHeaderRegion>& Regions, const FString& Key) {
		return Regions.FindByPredicate([&Key](const FAssetPackageHeaderRegion& Region) { return Region.Key == Key; });
	};

	for (const FString& Key : Keys)
	{
		FAssetPackageDiffEntry RegionEntry = MakeRegionEntry(OldDocument, NewDocument, FindRegion(OldRegions, Key), FindRegion(NewRegions, Key), HeaderDelta);

		if (Key == TEXT("Summary"))
		{
			// The summary is where the individual fields live.
			AddSummaryFields(RegionEntry.Children, OldDocument, NewDocument);

			for (FAssetPackageDiffEntry& Field : RegionEntry.Children)
			{
				Field.Explanation = ExplainField(Field, HeaderDelta, HeaderExplanation);
				if (Field.State != EAssetPackageDiffState::Unchanged && RegionEntry.State == EAssetPackageDiffState::Unchanged)
				{
					RegionEntry.State = EAssetPackageDiffState::Modified;
				}
			}
		}

		if (RegionEntry.Explanation.IsEmpty())
		{
			RegionEntry.Explanation = ExplainRegion(RegionEntry, OldDocument, NewDocument);
		}

		if (RegionEntry.State != EAssetPackageDiffState::Unchanged && Header.State == EAssetPackageDiffState::Unchanged)
		{
			Header.State = EAssetPackageDiffState::Modified;
		}

		Header.Children.Add(MoveTemp(RegionEntry));
	}

	Result.Entries.Add(MoveTemp(Header));
}

#undef LOCTEXT_NAMESPACE
