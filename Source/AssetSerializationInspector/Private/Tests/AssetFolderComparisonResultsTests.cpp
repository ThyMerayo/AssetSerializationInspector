// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Framework/Application/SlateApplication.h"

#include "Compare/AssetFolderComparison.h"
#include "Widgets/SAssetFolderComparisonResults.h"

namespace AssetFolderComparisonResultsTestUtils
{
	static FAssetFolderComparisonEntry MakeEntry(const TCHAR* Path, const EAssetFolderComparisonStatus Status)
	{
		FAssetFolderComparisonEntry Entry;
		Entry.RelativePath = Path;
		Entry.Status = Status;
		return Entry;
	}
} // namespace AssetFolderComparisonResultsTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetFolderComparisonResults_DescribesAFile, "AssetSerializationInspector.Widgets.AssetFolderComparisonResults.DescribesAFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetFolderComparisonResults_DescribesAFile::RunTest(const FString& Parameters)
{
	using namespace AssetFolderComparisonResultsTestUtils;

	FAssetFolderComparisonEntry Changed = MakeEntry(TEXT("Characters/Hero.uasset"), EAssetFolderComparisonStatus::Changed);
	Changed.bVersionsDiffer = true;
	Changed.OldEngineVersion = TEXT("5.3.2");
	Changed.NewEngineVersion = TEXT("5.8.1");
	Changed.OldFileVersion = TEXT("UE4 522 / UE5 1009");
	Changed.NewFileVersion = TEXT("UE4 522 / UE5 1018");
	Changed.OldFileSize = 1000;
	Changed.NewFileSize = 1200;
	Changed.Changes.Add({ TEXT("HeaderRegion"), TEXT("Thumbnail table"), TEXT("Table removed") });
	Changed.ChangesOmitted = 4;

	TestEqual(TEXT("A changed pair saved by different versions"), SAssetFolderComparisonResults::GetStatusText(Changed).ToString(), FString(TEXT("Changed (different versions)")));
	TestEqual(TEXT("An identical file"), SAssetFolderComparisonResults::GetStatusText(MakeEntry(TEXT("A.uasset"), EAssetFolderComparisonStatus::Identical)).ToString(), FString(TEXT("Identical")));
	TestEqual(TEXT("A file only in the new folder"), SAssetFolderComparisonResults::GetStatusText(MakeEntry(TEXT("A.uasset"), EAssetFolderComparisonStatus::OnlyInNewFolder)).ToString(),
		FString(TEXT("Only in the new folder")));

	const FString Details = SAssetFolderComparisonResults::BuildDetailsText(Changed);
	TestTrue(TEXT("The details name the file"), Details.Contains(TEXT("Characters/Hero.uasset")));
	TestTrue(TEXT("And the engines that saved it"), Details.Contains(TEXT("Saved by: 5.3.2 -> 5.8.1")));
	TestTrue(TEXT("And the package versions"), Details.Contains(TEXT("UE5 1009 -> UE4 522 / UE5 1018")) || Details.Contains(TEXT("UE4 522 / UE5 1009 -> UE4 522 / UE5 1018")));
	TestTrue(TEXT("And the sizes"), Details.Contains(TEXT("File size: 1000 -> 1200 bytes")));
	TestTrue(TEXT("They count the changes including omitted ones"), Details.Contains(TEXT("5 changes")));
	TestTrue(TEXT("They list a change"), Details.Contains(TEXT("[HeaderRegion] Thumbnail table: Table removed")));

	FAssetFolderComparisonEntry Failed = MakeEntry(TEXT("Broken.uasset"), EAssetFolderComparisonStatus::Failed);
	Failed.Message = TEXT("The package version is not supported.");
	const FString FailedDetails = SAssetFolderComparisonResults::BuildDetailsText(Failed);
	TestTrue(TEXT("A failed pair says why"), FailedDetails.Contains(TEXT("The package version is not supported.")));
	TestFalse(TEXT("And lists no changes"), FailedDetails.Contains(TEXT("changes")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetFolderComparisonResults_ListsFilesByInterest, "AssetSerializationInspector.Widgets.AssetFolderComparisonResults.ListsFilesByInterest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetFolderComparisonResults_ListsFilesByInterest::RunTest(const FString& Parameters)
{
	using namespace AssetFolderComparisonResultsTestUtils;

	if (!FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("Slate is not initialized in this run; the window itself was not constructed."));
		return true;
	}

	const TSharedRef<FAssetFolderComparisonResult> Result = MakeShared<FAssetFolderComparisonResult>();
	Result->Entries.Add(MakeEntry(TEXT("A_Identical.uasset"), EAssetFolderComparisonStatus::Identical));
	Result->Entries.Add(MakeEntry(TEXT("B_OnlyOld.uasset"), EAssetFolderComparisonStatus::OnlyInOldFolder));
	Result->Entries.Add(MakeEntry(TEXT("C_Changed.uasset"), EAssetFolderComparisonStatus::Changed));
	Result->Entries.Add(MakeEntry(TEXT("D_OnlyNew.uasset"), EAssetFolderComparisonStatus::OnlyInNewFolder));
	Result->Entries.Add(MakeEntry(TEXT("E_Failed.uasset"), EAssetFolderComparisonStatus::Failed));

	const TSharedRef<SAssetFolderComparisonResults> Window = SNew(SAssetFolderComparisonResults).Result(Result);

	const TArray<TSharedPtr<FAssetFolderComparisonItem>>& Items = Window->GetVisibleItems();
	if (TestEqual(TEXT("Every file is listed"), Items.Num(), 5))
	{
		TestEqual(TEXT("Failed pairs come first"), Items[0]->Entry.RelativePath, FString(TEXT("E_Failed.uasset")));
		TestEqual(TEXT("Then changed ones"), Items[1]->Entry.RelativePath, FString(TEXT("C_Changed.uasset")));
		TestEqual(TEXT("Then files only in the new folder"), Items[2]->Entry.RelativePath, FString(TEXT("D_OnlyNew.uasset")));
		TestEqual(TEXT("Then files only in the old folder"), Items[3]->Entry.RelativePath, FString(TEXT("B_OnlyOld.uasset")));
		TestEqual(TEXT("And identical ones last"), Items[4]->Entry.RelativePath, FString(TEXT("A_Identical.uasset")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
