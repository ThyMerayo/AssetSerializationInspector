// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Diff/AssetPackageDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Model/AssetPackageHeaderLayout.h"

namespace AssetPackageHeaderExplanationTestUtils
{
	/** Writes the bytes of a package header piece by piece. */
	struct FBytes
	{
		TArray<uint8> Data;

		template <typename TValue> void Append(const TValue Value)
		{
			const int32 Start = Data.AddUninitialized(sizeof(TValue));
			FMemory::Memcpy(Data.GetData() + Start, &Value, sizeof(TValue));
		}

		void AppendString(const FString& Value)
		{
			Append<int32>(Value.Len() + 1);
			for (int32 Index = 0; Index < Value.Len(); ++Index)
			{
				Append<uint8>(static_cast<uint8>(Value[Index]));
			}
			Append<uint8>(0);
		}

		void PadTo(const int32 Size)
		{
			if (Data.Num() < Size)
			{
				Data.AddZeroed(Size - Data.Num());
			}
		}
	};

	static void AppendThumbnail(FBytes& Bytes)
	{
		// An empty thumbnail: width, height and a zero-length image.
		Bytes.Append<int32>(0);
		Bytes.Append<int32>(0);
		Bytes.Append<int32>(0);
	}

	/**
	 * A header with a summary, a name map, an import type hierarchies table (4 bytes: its zero count), optionally two empty
	 * thumbnails followed by their index, and a 16 byte asset registry table that starts with an absolute offset.
	 */
	static FAssetPackageDocument MakeDocument(const bool bWithThumbnails, const uint8 LastRegistryByte = 0)
	{
		FBytes Bytes;
		Bytes.PadTo(100);		// summary
		Bytes.PadTo(120);		// name map at 100
		Bytes.Append<int32>(0); // import type hierarchies at 120
		int32 ThumbnailTableOffset = 0;

		if (bWithThumbnails)
		{
			const int32 FirstThumbnail = Bytes.Data.Num();
			AppendThumbnail(Bytes);
			const int32 SecondThumbnail = Bytes.Data.Num();
			AppendThumbnail(Bytes);

			ThumbnailTableOffset = Bytes.Data.Num();
			Bytes.Append<int32>(2);
			Bytes.AppendString(TEXT("Blueprint"));
			Bytes.AppendString(TEXT("BP_A"));
			Bytes.Append<int32>(FirstThumbnail);
			Bytes.AppendString(TEXT("BlueprintGeneratedClass"));
			Bytes.AppendString(TEXT("BP_A_C"));
			Bytes.Append<int32>(SecondThumbnail);
		}

		const int32 RegistryOffset = Bytes.Data.Num();

		// The first 8 bytes are an absolute file offset that moves with the data in front of it.
		Bytes.Append<int64>(5000 + RegistryOffset);
		Bytes.Append<int32>(0x11223344);
		Bytes.Append<uint8>(1);
		Bytes.Append<uint8>(2);
		Bytes.Append<uint8>(3);
		Bytes.Append<uint8>(LastRegistryByte);

		const int32 HeaderSize = Bytes.Data.Num();
		Bytes.PadTo(HeaderSize + 32);

		FAssetPackageDocument Document;
		FPackageFileSummary& Summary = Document.PackageSummary;
		Summary.TotalHeaderSize = HeaderSize;
		Summary.NameOffset = 100;
		Summary.NameCount = 1;
		Summary.ImportTypeHierarchiesOffset = 120;
		Summary.ThumbnailTableOffset = ThumbnailTableOffset;
		Summary.AssetRegistryDataOffset = RegistryOffset;

		Document.FileData.SetNumUninitialized(Bytes.Data.Num());
		FMemory::Memcpy(Document.FileData.GetData(), Bytes.Data.GetData(), Bytes.Data.Num());

		return Document;
	}

	static const FAssetPackageDiffEntry* FindChild(const FAssetPackageDiffEntry& Parent, const FString& Key)
	{
		return Parent.Children.FindByPredicate([&Key](const FAssetPackageDiffEntry& Child) { return Child.Key == Key; });
	}
} // namespace AssetPackageHeaderExplanationTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetPackageHeaderExplanation_ReadsThumbnails, "AssetSerializationInspector.Model.AssetPackageHeaderLayout.ReadsThumbnails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPackageHeaderExplanation_ReadsThumbnails::RunTest(const FString& Parameters)
{
	using namespace AssetPackageHeaderExplanationTestUtils;

	const FAssetPackageDocument Document = MakeDocument(true);

	const TArray<FAssetPackageThumbnailEntry> Entries = AssetPackageHeaderLayout::ReadThumbnailIndex(Document);
	if (!TestEqual(TEXT("Both thumbnails are read"), Entries.Num(), 2))
	{
		return false;
	}

	TestEqual(TEXT("The class of the first"), Entries[0].ObjectClassName, FString(TEXT("Blueprint")));
	TestEqual(TEXT("The object path of the first"), Entries[0].ObjectPath, FString(TEXT("BP_A")));
	TestEqual(TEXT("Where its image data starts"), Entries[0].FileOffset, 124);
	TestTrue(TEXT("A thumbnail without an image is marked empty"), Entries[0].bEmpty && Entries[1].bEmpty);

	TestTrue(TEXT("A package without thumbnails has none"), AssetPackageHeaderLayout::ReadThumbnailIndex(MakeDocument(false)).IsEmpty());

	// The image data gets its own region, so it is not counted as part of the table in front of it.
	const TArray<FAssetPackageHeaderRegion> Regions = AssetPackageHeaderLayout::Build(Document);
	TArray<FString> Keys;
	for (const FAssetPackageHeaderRegion& Region : Regions)
	{
		Keys.Add(Region.Key);
	}
	TestEqual(TEXT("The regions in file order"), Keys,
		TArray<FString>({ TEXT("Summary"), TEXT("NameMap"), TEXT("ImportTypeHierarchies"), TEXT("ThumbnailData"), TEXT("ThumbnailTable"), TEXT("AssetRegistryData") }));

	const FAssetPackageHeaderRegion* Hierarchies = Regions.FindByPredicate([](const FAssetPackageHeaderRegion& Region) { return Region.Key == TEXT("ImportTypeHierarchies"); });
	const FAssetPackageHeaderRegion* Data = Regions.FindByPredicate([](const FAssetPackageHeaderRegion& Region) { return Region.Key == TEXT("ThumbnailData"); });
	if (TestTrue(TEXT("Both regions exist"), Hierarchies != nullptr && Data != nullptr))
	{
		TestEqual(TEXT("The table in front keeps only its own 4 bytes"), Hierarchies->Size, static_cast<int64>(4));
		TestEqual(TEXT("The image data starts at the first thumbnail"), Data->Offset, static_cast<int64>(124));
		TestEqual(TEXT("It holds both images"), Data->Size, static_cast<int64>(24));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetPackageHeaderExplanation_ExplainsRemovedThumbnails, "AssetSerializationInspector.Diff.AssetPackageHeaderDiff.ExplainsRemovedThumbnails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPackageHeaderExplanation_ExplainsRemovedThumbnails::RunTest(const FString& Parameters)
{
	using namespace AssetPackageHeaderExplanationTestUtils;

	const FAssetPackageDocument OldDocument = MakeDocument(true);
	const FAssetPackageDocument NewDocument = MakeDocument(false);
	const int64 Delta = AssetPackageHeaderLayout::GetHeaderSize(NewDocument) - AssetPackageHeaderLayout::GetHeaderSize(OldDocument);

	FAssetPackageDiffResult Result;
	AssetPackageDiff::AppendHeaderDiff(OldDocument, NewDocument, Result);
	const FAssetPackageDiffEntry& Header = Result.Entries[0];

	const FString HeaderWhy = Header.Explanation.ToString();
	TestTrue(TEXT("The header says it shrank"), HeaderWhy.Contains(FString::Printf(TEXT("%lld bytes smaller"), -Delta)));
	TestTrue(TEXT("It names the thumbnail data"), HeaderWhy.Contains(TEXT("Thumbnail data (-24)")));
	TestTrue(TEXT("It names the thumbnail table"), HeaderWhy.Contains(TEXT("Thumbnail table (")));
	TestFalse(TEXT("It does not blame the table in front of the thumbnails"), HeaderWhy.Contains(TEXT("Import type hierarchies")));

	const FAssetPackageDiffEntry* Hierarchies = FindChild(Header, TEXT("ImportTypeHierarchies"));
	const FAssetPackageDiffEntry* Table = FindChild(Header, TEXT("ThumbnailTable"));
	const FAssetPackageDiffEntry* Data = FindChild(Header, TEXT("ThumbnailData"));
	const FAssetPackageDiffEntry* Registry = FindChild(Header, TEXT("AssetRegistryData"));
	const FAssetPackageDiffEntry* Summary = FindChild(Header, TEXT("Summary"));
	if (!TestTrue(TEXT("The regions are listed"), Hierarchies && Table && Data && Registry && Summary))
	{
		return false;
	}

	TestEqual(TEXT("The table in front of the thumbnails did not change"), Hierarchies->State, EAssetPackageDiffState::Unchanged);
	TestEqual(TEXT("The thumbnail table was removed"), Table->State, EAssetPackageDiffState::Removed);
	TestEqual(TEXT("And its image data"), Data->State, EAssetPackageDiffState::Removed);
	TestTrue(TEXT("The explanation lists what was stored"), Table->Explanation.ToString().Contains(TEXT("BlueprintGeneratedClass BP_A_C (no image)")));

	// The asset registry table keeps its content except for the absolute offset, which moved with the header.
	TestEqual(TEXT("Only its offsets changed, so it moved"), Registry->State, EAssetPackageDiffState::Moved);
	TestTrue(TEXT("The explanation says why"), Registry->Explanation.ToString().Contains(TEXT("Only absolute file offsets differ: 1 stored offsets moved back")));
	if (TestEqual(TEXT("The shifted offset is recorded for the hex view"), Registry->ShiftedOffsetRanges.Num(), 1))
	{
		TestEqual(TEXT("It is the 8 byte integer at the start of the table"), Registry->ShiftedOffsetRanges[0].Offset, static_cast<int64>(0));
		TestEqual(TEXT("With its width"), Registry->ShiftedOffsetRanges[0].Size, static_cast<int64>(8));

		for (const FAssetByteDiffSpan& Span : Registry->ChangedSpans)
		{
			TestTrue(TEXT("Every differing byte lies inside a shifted offset"), Span.Offset >= 0 && Span.End() <= 8);
		}
	}

	const FAssetPackageDiffEntry* TableOffset = FindChild(*Summary, TEXT("ThumbnailTableOffset"));
	const FAssetPackageDiffEntry* RegistryOffset = FindChild(*Summary, TEXT("AssetRegistryDataOffset"));
	const FAssetPackageDiffEntry* HeaderSize = FindChild(*Summary, TEXT("TotalHeaderSize"));
	const FAssetPackageDiffEntry* Hash = FindChild(*Summary, TEXT("SavedHash"));
	const FAssetPackageDiffEntry* NameOffset = FindChild(*Summary, TEXT("NameOffset"));
	if (TestTrue(TEXT("The summary fields are listed"), TableOffset && RegistryOffset && HeaderSize && Hash && NameOffset))
	{
		TestTrue(TEXT("A table offset that became 0 says the table is gone"), TableOffset->Explanation.ToString().Contains(TEXT("no longer exists")));
		TestTrue(TEXT("An offset that shifted with the header says so"), RegistryOffset->Explanation.ToString().Contains(TEXT("same as the header's size change")));
		TestEqual(TEXT("The total size explains itself through the regions"), HeaderSize->Explanation.ToString(), HeaderWhy);
		TestTrue(TEXT("An unchanged field needs no explanation"), NameOffset->Explanation.IsEmpty());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetPackageHeaderExplanation_DistinguishesShiftsFromRealChanges, "AssetSerializationInspector.Diff.AssetPackageHeaderDiff.DistinguishesShiftsFromRealChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPackageHeaderExplanation_DistinguishesShiftsFromRealChanges::RunTest(const FString& Parameters)
{
	using namespace AssetPackageHeaderExplanationTestUtils;

	// Same layout change as before, but one byte of the asset registry data differs beyond the shifted offset.
	FAssetPackageDiffResult Result;
	AssetPackageDiff::AppendHeaderDiff(MakeDocument(true), MakeDocument(false, 9), Result);

	const FAssetPackageDiffEntry* Registry = FindChild(Result.Entries[0], TEXT("AssetRegistryData"));
	if (TestNotNull(TEXT("The asset registry region is listed"), Registry))
	{
		TestEqual(TEXT("A change that is not an offset is a real modification"), Registry->State, EAssetPackageDiffState::Modified);
		TestTrue(TEXT("The explanation counts the differing bytes"), Registry->Explanation.ToString().Contains(TEXT("of 16 bytes differ")));
		TestEqual(TEXT("The offset that did shift is still shown apart"), Registry->ShiftedOffsetRanges.Num(), 1);
		TestTrue(TEXT("The explanation separates it from the other change"), Registry->Explanation.ToString().Contains(TEXT("1 of them are stored offsets")));
	}

	// With the header the same size there is nothing to blame a difference on.
	FAssetPackageDiffResult SameSize;
	AssetPackageDiff::AppendHeaderDiff(MakeDocument(false, 1), MakeDocument(false, 2), SameSize);
	const FAssetPackageDiffEntry* SameSizeRegistry = FindChild(SameSize.Entries[0], TEXT("AssetRegistryData"));
	if (TestNotNull(TEXT("The region is listed"), SameSizeRegistry))
	{
		TestEqual(TEXT("It is modified"), SameSizeRegistry->State, EAssetPackageDiffState::Modified);
		TestTrue(TEXT("It states how many bytes differ"), SameSizeRegistry->Explanation.ToString().Contains(TEXT("1 of 16 bytes differ")));
		TestTrue(TEXT("Nothing is presented as a shifted offset"), SameSizeRegistry->ShiftedOffsetRanges.IsEmpty());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
