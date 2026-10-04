// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Framework/Application/SlateApplication.h"

#include "Save/AssetBatchResave.h"
#include "Widgets/SAssetBatchResults.h"

namespace AssetBatchResultsTestUtils
{
	static FAssetBatchResaveEntry MakeEntry(const TCHAR* Package, const EAssetBatchResaveStatus Status, const ENoOpResaveVerdict Verdict, const int64 FirstBytes = 0, const int64 SecondBytes = 0)
	{
		FAssetBatchResaveEntry Entry;
		Entry.PackageName = Package;
		Entry.Status = Status;
		Entry.Verdict = Verdict;
		Entry.FirstResaveChangedBytes = FirstBytes;
		Entry.SecondResaveChangedBytes = SecondBytes;
		return Entry;
	}
} // namespace AssetBatchResultsTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetBatchResults_DescribesAnEntry, "AssetSerializationInspector.Widgets.AssetBatchResults.DescribesAnEntry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBatchResults_DescribesAnEntry::RunTest(const FString& Parameters)
{
	using namespace AssetBatchResultsTestUtils;

	FAssetBatchResaveEntry Unstable = MakeEntry(TEXT("/Game/Unstable"), EAssetBatchResaveStatus::Tested, ENoOpResaveVerdict::Unstable, 120, 8);
	Unstable.FirstResaveChanges.Add({ TEXT("HeaderRegion"), TEXT("Thumbnail table"), TEXT("Table removed") });
	Unstable.FirstResaveChangesOmitted = 2;
	Unstable.SecondResaveChanges.Add({ TEXT("PropertyValueChanged"), TEXT("Counter"), TEXT("1 -> 2") });

	TestEqual(TEXT("An unstable asset"), SAssetBatchResults::GetOutcomeText(Unstable).ToString(), FString(TEXT("Unstable")));
	TestEqual(TEXT("A normalized asset"), SAssetBatchResults::GetOutcomeText(MakeEntry(TEXT("/Game/N"), EAssetBatchResaveStatus::Tested, ENoOpResaveVerdict::NormalizedOnFirstSave)).ToString(),
		FString(TEXT("Normalized on first save")));
	TestEqual(
		TEXT("A skipped asset"), SAssetBatchResults::GetOutcomeText(MakeEntry(TEXT("/Game/S"), EAssetBatchResaveStatus::Skipped, ENoOpResaveVerdict::Stable)).ToString(), FString(TEXT("Skipped")));
	TestEqual(TEXT("A failed asset"), SAssetBatchResults::GetOutcomeText(MakeEntry(TEXT("/Game/F"), EAssetBatchResaveStatus::Failed, ENoOpResaveVerdict::Stable)).ToString(), FString(TEXT("Failed")));

	const FString Details = SAssetBatchResults::BuildDetailsText(Unstable);
	TestTrue(TEXT("The details name the package"), Details.Contains(TEXT("/Game/Unstable")));
	TestTrue(TEXT("They give the first resave's change count including omitted ones"), Details.Contains(TEXT("120 changed bytes, 3 changes")));
	TestTrue(TEXT("They list a change"), Details.Contains(TEXT("[HeaderRegion] Thumbnail table: Table removed")));
	TestTrue(TEXT("They say how many were left out"), Details.Contains(TEXT("... and 2 more")));
	TestTrue(TEXT("They list the second resave's changes"), Details.Contains(TEXT("[PropertyValueChanged] Counter: 1 -> 2")));

	FAssetBatchResaveEntry Skipped = MakeEntry(TEXT("/Game/Level"), EAssetBatchResaveStatus::Skipped, ENoOpResaveVerdict::Stable);
	Skipped.Message = TEXT("Levels are not supported.");
	const FString SkippedDetails = SAssetBatchResults::BuildDetailsText(Skipped);
	TestTrue(TEXT("A skipped asset says why"), SkippedDetails.Contains(TEXT("Levels are not supported.")));
	TestFalse(TEXT("And lists no resaves"), SkippedDetails.Contains(TEXT("First resave")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetBatchResults_ListsAndFiltersAssets, "AssetSerializationInspector.Widgets.AssetBatchResults.ListsAndFiltersAssets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBatchResults_ListsAndFiltersAssets::RunTest(const FString& Parameters)
{
	using namespace AssetBatchResultsTestUtils;

	if (!FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("Slate is not initialized in this run; the window itself was not constructed."));
		return true;
	}

	const TSharedRef<FAssetBatchResaveResult> Result = MakeShared<FAssetBatchResaveResult>();
	Result->Scope = TEXT("test");
	Result->Entries.Add(MakeEntry(TEXT("/Game/A_Stable"), EAssetBatchResaveStatus::Tested, ENoOpResaveVerdict::Stable));
	Result->Entries.Add(MakeEntry(TEXT("/Game/B_Skipped"), EAssetBatchResaveStatus::Skipped, ENoOpResaveVerdict::Stable));
	Result->Entries.Add(MakeEntry(TEXT("/Game/C_Unstable"), EAssetBatchResaveStatus::Tested, ENoOpResaveVerdict::Unstable, 10, 2));
	Result->Entries.Add(MakeEntry(TEXT("/Game/D_Normalized"), EAssetBatchResaveStatus::Tested, ENoOpResaveVerdict::NormalizedOnFirstSave, 30));
	Result->Entries.Add(MakeEntry(TEXT("/Game/E_Failed"), EAssetBatchResaveStatus::Failed, ENoOpResaveVerdict::Stable));

	const TSharedRef<SAssetBatchResults> Window = SNew(SAssetBatchResults).Result(Result);

	const TArray<TSharedPtr<FAssetBatchResultItem>>& Items = Window->GetVisibleItems();
	if (TestEqual(TEXT("Every asset is listed"), Items.Num(), 5))
	{
		TestEqual(TEXT("Unstable assets come first"), Items[0]->DisplayName, FString(TEXT("C_Unstable")));
		TestEqual(TEXT("Then those normalized on the first save"), Items[1]->DisplayName, FString(TEXT("D_Normalized")));
		TestEqual(TEXT("Then the failed ones"), Items[2]->DisplayName, FString(TEXT("E_Failed")));
		TestEqual(TEXT("Then the skipped ones"), Items[3]->DisplayName, FString(TEXT("B_Skipped")));
		TestEqual(TEXT("And the stable ones last"), Items[4]->DisplayName, FString(TEXT("A_Stable")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
