// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Curves/CurveFloat.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "Diff/AssetPackageDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Model/AssetPackageHeaderLayout.h"
#include "Readers/AssetPackageReader.h"

namespace AssetPackageHeaderDiffTestUtils
{
	/**
	 * A package whose header is laid out as: summary, name map, import map, export map, depends map. Each region's bytes
	 * depend only on the region and the position inside it, so a region that merely moves keeps identical bytes.
	 */
	static FAssetPackageDocument MakeDocument(const int32 NameMapSize, const bool bHasDependsMap = true)
	{
		FAssetPackageDocument Document;
		FPackageFileSummary& Summary = Document.PackageSummary;

		const int32 NameOffset = 100;
		const int32 ImportOffset = NameOffset + NameMapSize;
		const int32 ExportOffset = ImportOffset + 60;
		const int32 DependsOffset = ExportOffset + 120;
		const int32 HeaderSize = DependsOffset + 20;

		Summary.TotalHeaderSize = HeaderSize;
		Summary.NameOffset = NameOffset;
		Summary.NameCount = 3;
		Summary.ImportOffset = ImportOffset;
		Summary.ImportCount = 2;
		Summary.ExportOffset = ExportOffset;
		Summary.ExportCount = 1;
		Summary.DependsOffset = bHasDependsMap ? DependsOffset : 0;

		Document.FileData.SetNumUninitialized(HeaderSize + 32);
		for (int64 Index = 0; Index < Document.FileData.Num(); ++Index)
		{
			const int64 RegionStart = Index < NameOffset ? 0 : Index < ImportOffset ? NameOffset : Index < ExportOffset ? ImportOffset : Index < DependsOffset ? ExportOffset : DependsOffset;
			const int64 RegionId = Index < NameOffset ? 0 : Index < ImportOffset ? 1 : Index < ExportOffset ? 2 : Index < DependsOffset ? 3 : 4;
			Document.FileData[Index] = static_cast<uint8>(RegionId * 16 + ((Index - RegionStart) % 16));
		}

		return Document;
	}

	static const FAssetPackageDiffEntry* FindChild(const FAssetPackageDiffEntry& Parent, const FString& Key)
	{
		return Parent.Children.FindByPredicate([&Key](const FAssetPackageDiffEntry& Child) { return Child.Key == Key; });
	}
} // namespace AssetPackageHeaderDiffTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetPackageHeader_BuildsRegionLayout, "AssetSerializationInspector.Model.AssetPackageHeaderLayout.BuildsRegionLayout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPackageHeader_BuildsRegionLayout::RunTest(const FString& Parameters)
{
	using namespace AssetPackageHeaderDiffTestUtils;

	const FAssetPackageDocument Document = MakeDocument(100);
	TestEqual(TEXT("The header size comes from the summary"), AssetPackageHeaderLayout::GetHeaderSize(Document), static_cast<int64>(400));

	const TArray<FAssetPackageHeaderRegion> Regions = AssetPackageHeaderLayout::Build(Document);
	if (!TestEqual(TEXT("The summary and four tables"), Regions.Num(), 5))
	{
		return false;
	}

	const TCHAR* Keys[] = { TEXT("Summary"), TEXT("NameMap"), TEXT("ImportMap"), TEXT("ExportMap"), TEXT("DependsMap") };
	const int64 Offsets[] = { 0, 100, 200, 260, 380 };
	const int64 Sizes[] = { 100, 100, 60, 120, 20 };

	for (int32 Index = 0; Index < 5; ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Region %d key"), Index), Regions[Index].Key, FString(Keys[Index]));
		TestEqual(*FString::Printf(TEXT("Region %d offset"), Index), Regions[Index].Offset, Offsets[Index]);
		TestEqual(*FString::Printf(TEXT("Region %d size runs to the next region"), Index), Regions[Index].Size, Sizes[Index]);
	}

	TestEqual(TEXT("A table reports the entry count the summary declares"), Regions[1].EntryCount, 3);
	TestEqual(TEXT("A table without a declared count reports none"), Regions[4].EntryCount, static_cast<int32>(INDEX_NONE));

	FAssetPackageDocument NoTables;
	NoTables.PackageSummary.TotalHeaderSize = 64;
	NoTables.FileData.SetNumZeroed(128);
	const TArray<FAssetPackageHeaderRegion> OnlySummary = AssetPackageHeaderLayout::Build(NoTables);
	TestEqual(TEXT("Without tables the summary is the whole header"), OnlySummary.Num(), 1);
	TestEqual(TEXT("It spans the header"), OnlySummary[0].Size, static_cast<int64>(64));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetPackageHeader_ReportsWhatChanged, "AssetSerializationInspector.Diff.AssetPackageHeaderDiff.ReportsWhatChanged", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPackageHeader_ReportsWhatChanged::RunTest(const FString& Parameters)
{
	using namespace AssetPackageHeaderDiffTestUtils;

	// The name map grows by 16 bytes, which pushes every later table.
	const FAssetPackageDocument OldDocument = MakeDocument(100);
	const FAssetPackageDocument NewDocument = MakeDocument(116);

	FAssetPackageDiffResult Result;
	AssetPackageDiff::AppendHeaderDiff(OldDocument, NewDocument, Result);

	if (!TestEqual(TEXT("One header entry"), Result.Entries.Num(), 1))
	{
		return false;
	}

	const FAssetPackageDiffEntry& Header = Result.Entries[0];
	TestEqual(TEXT("It is a header entry"), Header.Kind, EAssetPackageDiffKind::Header);
	TestEqual(TEXT("A changed header is modified"), Header.State, EAssetPackageDiffState::Modified);
	TestEqual(TEXT("It covers the old header"), Header.OldSize, static_cast<int64>(400));
	TestEqual(TEXT("It covers the new header"), Header.NewSize, static_cast<int64>(416));
	TestTrue(TEXT("The size change is stated"), Header.NewValue.Contains(TEXT("(+16)")));

	const FAssetPackageDiffEntry* Names = FindChild(Header, TEXT("NameMap"));
	const FAssetPackageDiffEntry* Imports = FindChild(Header, TEXT("ImportMap"));
	const FAssetPackageDiffEntry* Exports = FindChild(Header, TEXT("ExportMap"));
	const FAssetPackageDiffEntry* Depends = FindChild(Header, TEXT("DependsMap"));
	const FAssetPackageDiffEntry* Summary = FindChild(Header, TEXT("Summary"));
	if (!TestTrue(TEXT("Every region is listed"), Names && Imports && Exports && Depends && Summary))
	{
		return false;
	}

	TestEqual(TEXT("A region that grew is modified"), Names->State, EAssetPackageDiffState::Modified);
	TestEqual(TEXT("Its old and new sizes are recorded"), Names->OldSize + 16, Names->NewSize);
	TestTrue(TEXT("It can be shown in the hex view"), Names->OldOffset == 100 && Names->NewOffset == 100);

	TestEqual(TEXT("A region with the same bytes at a new offset moved"), Imports->State, EAssetPackageDiffState::Moved);
	TestEqual(TEXT("Its old offset"), Imports->OldOffset, static_cast<int64>(200));
	TestEqual(TEXT("Its new offset"), Imports->NewOffset, static_cast<int64>(216));
	TestEqual(TEXT("The export map moved too"), Exports->State, EAssetPackageDiffState::Moved);
	TestEqual(TEXT("And the depends map"), Depends->State, EAssetPackageDiffState::Moved);

	// The summary bytes are identical here, but its fields differ: the header size and every offset after the name map.
	TestEqual(TEXT("The summary is modified through its fields"), Summary->State, EAssetPackageDiffState::Modified);

	const FAssetPackageDiffEntry* TotalHeaderSize = FindChild(*Summary, TEXT("TotalHeaderSize"));
	const FAssetPackageDiffEntry* NameOffset = FindChild(*Summary, TEXT("NameOffset"));
	const FAssetPackageDiffEntry* ImportOffset = FindChild(*Summary, TEXT("ImportOffset"));
	if (TestTrue(TEXT("The header size field is listed"), TotalHeaderSize != nullptr && NameOffset != nullptr && ImportOffset != nullptr))
	{
		TestEqual(TEXT("Its old value"), TotalHeaderSize->OldValue, FString(TEXT("400")));
		TestEqual(TEXT("Its new value"), TotalHeaderSize->NewValue, FString(TEXT("416")));
		TestEqual(TEXT("A field that did not change is unchanged"), NameOffset->State, EAssetPackageDiffState::Unchanged);
		TestEqual(TEXT("A changed offset is modified"), ImportOffset->State, EAssetPackageDiffState::Modified);
	}

	TestTrue(TEXT("Fields that older reports skipped are present"), FindChild(*Summary, TEXT("SavedHash")) != nullptr && FindChild(*Summary, TEXT("PackageSource")) != nullptr);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetPackageHeader_ReportsIdenticalAndMissingRegions, "AssetSerializationInspector.Diff.AssetPackageHeaderDiff.ReportsIdenticalAndMissingRegions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPackageHeader_ReportsIdenticalAndMissingRegions::RunTest(const FString& Parameters)
{
	using namespace AssetPackageHeaderDiffTestUtils;

	const FAssetPackageDocument Document = MakeDocument(100);

	FAssetPackageDiffResult Same;
	AssetPackageDiff::AppendHeaderDiff(Document, Document, Same);
	TestEqual(TEXT("An unchanged header is unchanged"), Same.Entries[0].State, EAssetPackageDiffState::Unchanged);
	for (const FAssetPackageDiffEntry& Region : Same.Entries[0].Children)
	{
		TestEqual(*FString::Printf(TEXT("Region %s is unchanged"), *Region.Key), Region.State, EAssetPackageDiffState::Unchanged);
	}

	FAssetPackageDiffResult Missing;
	AssetPackageDiff::AppendHeaderDiff(MakeDocument(100, true), MakeDocument(100, false), Missing);
	const FAssetPackageDiffEntry* Depends = FindChild(Missing.Entries[0], TEXT("DependsMap"));
	if (TestNotNull(TEXT("The depends map is listed"), Depends))
	{
		TestEqual(TEXT("A table only the old header has was removed"), Depends->State, EAssetPackageDiffState::Removed);
	}

	FAssetPackageDiffResult Added;
	AssetPackageDiff::AppendHeaderDiff(MakeDocument(100, false), MakeDocument(100, true), Added);
	Depends = FindChild(Added.Entries[0], TEXT("DependsMap"));
	if (TestNotNull(TEXT("The depends map is listed again"), Depends))
	{
		TestEqual(TEXT("A table only the new header has was added"), Depends->State, EAssetPackageDiffState::Added);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetPackageHeader_DiffsRealPackages, "AssetSerializationInspector.Diff.AssetPackageHeaderDiff.DiffsRealPackages", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPackageHeader_DiffsRealPackages::RunTest(const FString& Parameters)
{
	using namespace AssetPackageHeaderDiffTestUtils;

	const FString PackageName = TEXT("/Game/__AssetSerializationInspectorTests/HeaderDiffCurve");
	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	const FString SnapshotFilename = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AssetSerializationInspector"), TEXT("HeaderDiffTest_Before.uasset"));

	UPackage* Package = CreatePackage(*PackageName);
	UCurveFloat* Curve = NewObject<UCurveFloat>(Package, TEXT("HeaderDiffCurve"), RF_Public | RF_Standalone);
	Curve->FloatCurve.AddKey(0.0f, 1.0f);
	Package->MarkAsFullyLoaded();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	SaveArgs.bSlowTask = false;

	bool bSaved = UPackage::SavePackage(Package, Curve, *Filename, SaveArgs);
	TestTrue(TEXT("The first version is written"), bSaved);

	if (bSaved)
	{
		IFileManager::Get().Copy(*SnapshotFilename, *Filename, true, true);

		Curve->FloatCurve.AddKey(1.0f, 2.0f);
		Package->MarkPackageDirty();
		bSaved = UPackage::SavePackage(Package, Curve, *Filename, SaveArgs);
		TestTrue(TEXT("The second version is written"), bSaved);
	}

	if (bSaved)
	{
		FText Error;
		const TSharedPtr<FAssetPackageDocument> Before = FAssetPackageReader::LoadFromFile(SnapshotFilename, Error);
		const TSharedPtr<FAssetPackageDocument> After = FAssetPackageReader::LoadFromFile(Filename, Error);

		if (TestTrue(TEXT("Both versions can be read"), Before.IsValid() && After.IsValid()))
		{
			const FAssetPackageDiffResult Result = AssetPackageDiff::Compare(*Before, *After);

			const FAssetPackageDiffEntry* Header = Result.Entries.FindByPredicate([](const FAssetPackageDiffEntry& Entry) { return Entry.Kind == EAssetPackageDiffKind::Header; });

			if (TestNotNull(TEXT("The comparison has a header entry"), Header))
			{
				TestEqual(TEXT("The header changed (at least its hash)"), Header->State, EAssetPackageDiffState::Modified);
				TestEqual(TEXT("It covers the old header"), Header->OldSize, AssetPackageHeaderLayout::GetHeaderSize(*Before));
				TestEqual(TEXT("It covers the new header"), Header->NewSize, AssetPackageHeaderLayout::GetHeaderSize(*After));

				const FAssetPackageDiffEntry* Summary = FindChild(*Header, TEXT("Summary"));
				if (TestNotNull(TEXT("The summary region is listed"), Summary))
				{
					TestEqual(TEXT("The summary starts at the beginning of the file"), Summary->OldOffset, static_cast<int64>(0));
					TestTrue(TEXT("It has a size to show in the hex view"), Summary->OldSize > 0 && Summary->NewSize > 0);

					const FAssetPackageDiffEntry* SavedHash = FindChild(*Summary, TEXT("SavedHash"));
					if (TestNotNull(TEXT("The saved hash is listed"), SavedHash))
					{
						TestEqual(TEXT("Different contents give a different hash"), SavedHash->State, EAssetPackageDiffState::Modified);
					}
				}

				const FAssetPackageDiffEntry* Names = FindChild(*Header, TEXT("NameMap"));
				const FAssetPackageDiffEntry* Exports = FindChild(*Header, TEXT("ExportMap"));
				TestTrue(TEXT("The tables of the header are listed"), Names != nullptr && Exports != nullptr);
				if (Exports != nullptr)
				{
					TestEqual(TEXT("The export map changed with the export's size"), Exports->State, EAssetPackageDiffState::Modified);
				}
			}
		}
	}

	Curve->ClearFlags(RF_Public | RF_Standalone);
	Package->SetDirtyFlag(false);
	IFileManager::Get().Delete(*Filename, false, true, true);
	IFileManager::Get().Delete(*SnapshotFilename, false, true, true);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
