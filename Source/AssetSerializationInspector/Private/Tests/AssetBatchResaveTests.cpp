// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Curves/CurveFloat.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "Report/AssetBatchReportWriter.h"
#include "Save/AssetBatchResave.h"

namespace AssetBatchResaveTestUtils
{
	static FAssetBatchResaveChange MakeChange(const FString& Category, const FString& Name, const FString& Detail = FString())
	{
		FAssetBatchResaveChange Change;
		Change.Category = Category;
		Change.Name = Name;
		Change.Detail = Detail;
		return Change;
	}

	static FAssetBatchResaveEntry MakeTested(const TCHAR* Package, const ENoOpResaveVerdict Verdict, const TArray<FAssetBatchResaveChange>& FirstChanges = {})
	{
		FAssetBatchResaveEntry Entry;
		Entry.PackageName = FName(Package);
		Entry.Status = EAssetBatchResaveStatus::Tested;
		Entry.Verdict = Verdict;
		Entry.FirstResaveChanges = FirstChanges;
		Entry.FirstResaveChangedBytes = FirstChanges.Num() * 8;
		return Entry;
	}

	/** Two tested assets that both lost their thumbnails, one unstable, one skipped and one failed. */
	static FAssetBatchResaveResult MakeResult()
	{
		FAssetBatchResaveResult Result;
		Result.Scope = TEXT("/Game/Characters");
		Result.StartedAt = FDateTime(2026, 10, 1, 14, 0, 0);
		Result.FinishedAt = FDateTime(2026, 10, 1, 14, 5, 0);

		Result.Entries.Add(MakeTested(TEXT("/Game/Characters/Hero"), ENoOpResaveVerdict::NormalizedOnFirstSave, { MakeChange(TEXT("HeaderRegion"), TEXT("Thumbnail table"), TEXT("Removed")) }));
		Result.Entries.Add(MakeTested(TEXT("/Game/Characters/Villain"), ENoOpResaveVerdict::NormalizedOnFirstSave,
			{ MakeChange(TEXT("HeaderRegion"), TEXT("Thumbnail table"), TEXT("Removed")), MakeChange(TEXT("PropertyValueChanged"), TEXT("Health"), TEXT("50 => 75")) }));

		FAssetBatchResaveEntry Unstable = MakeTested(TEXT("/Game/Characters/Sidekick"), ENoOpResaveVerdict::Unstable, { MakeChange(TEXT("PropertyValueChanged"), TEXT("Stamp"), TEXT("1 => 2")) });
		Unstable.SecondResaveChanges = { MakeChange(TEXT("PropertyValueChanged"), TEXT("Stamp"), TEXT("2 => 3")) };
		Unstable.SecondResaveChangedBytes = 4;
		Result.Entries.Add(Unstable);

		Result.Entries.Add(MakeTested(TEXT("/Game/Characters/Statue"), ENoOpResaveVerdict::Stable));

		FAssetBatchResaveEntry Skipped;
		Skipped.PackageName = FName(TEXT("/Game/Characters/Edited"));
		Skipped.Status = EAssetBatchResaveStatus::Skipped;
		Skipped.Message = TEXT("The asset has unsaved changes.");
		Result.Entries.Add(Skipped);

		FAssetBatchResaveEntry Failed;
		Failed.PackageName = FName(TEXT("/Game/Characters/Broken"));
		Failed.Status = EAssetBatchResaveStatus::Failed;
		Failed.Message = TEXT("The package could not be loaded.");
		Result.Entries.Add(Failed);

		return Result;
	}

	static UCurveFloat* SaveCurve(const FString& PackageName, const bool bMarkDirty)
	{
		UPackage* Package = CreatePackage(*PackageName);
		UCurveFloat* Curve = NewObject<UCurveFloat>(Package, *FPackageName::GetShortName(PackageName), RF_Public | RF_Standalone);
		Curve->FloatCurve.AddKey(0.0f, 1.0f);
		Package->MarkAsFullyLoaded();

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		SaveArgs.bSlowTask = false;

		if (!UPackage::SavePackage(Package, Curve, *FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), SaveArgs))
		{
			return nullptr;
		}

		if (bMarkDirty)
		{
			Package->MarkPackageDirty();
		}

		return Curve;
	}
} // namespace AssetBatchResaveTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBatchResave_SummarizesAndFindsRecurringChanges, "AssetSerializationInspector.Save.AssetBatchResave.SummarizesAndFindsRecurringChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBatchResave_SummarizesAndFindsRecurringChanges::RunTest(const FString& Parameters)
{
	using namespace AssetBatchResaveTestUtils;

	const FAssetBatchResaveResult Result = MakeResult();
	const FAssetBatchResaveSummary Summary = Result.Summarize();

	TestEqual(TEXT("Tested assets"), Summary.Tested, 4);
	TestEqual(TEXT("Stable"), Summary.Stable, 1);
	TestEqual(TEXT("Normalized on the first save"), Summary.NormalizedOnFirstSave, 2);
	TestEqual(TEXT("Unstable"), Summary.Unstable, 1);
	TestEqual(TEXT("Skipped"), Summary.Skipped, 1);
	TestEqual(TEXT("Failed"), Summary.Failed, 1);

	const TArray<FAssetBatchRecurringChange> Recurring = Result.FindRecurringChanges();
	if (TestEqual(TEXT("Only the change seen in several assets recurs"), Recurring.Num(), 1))
	{
		TestEqual(TEXT("Its category"), Recurring[0].Category, FString(TEXT("HeaderRegion")));
		TestEqual(TEXT("Its name"), Recurring[0].Name, FString(TEXT("Thumbnail table")));
		TestEqual(TEXT("The assets it appeared in"), Recurring[0].AssetCount, 2);
	}

	TestEqual(TEXT("A lower threshold lists the rest"), Result.FindRecurringChanges(1).Num(), 3);

	// An asset counts once for a change, however many times it appears inside it.
	FAssetBatchResaveResult Repeated;
	Repeated.Entries.Add(MakeTested(TEXT("/Game/A"), ENoOpResaveVerdict::Unstable, { MakeChange(TEXT("X"), TEXT("Same")), MakeChange(TEXT("X"), TEXT("Same")) }));
	TestTrue(TEXT("A change repeated inside one asset is not a recurring change"), Repeated.FindRecurringChanges().IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetBatchResave_WritesReports, "AssetSerializationInspector.Report.AssetBatchReportWriter.WritesReports", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBatchResave_WritesReports::RunTest(const FString& Parameters)
{
	using namespace AssetBatchResaveTestUtils;

	const FAssetBatchResaveResult Result = MakeResult();

	const FString Text = AssetBatchReportWriter::ToText(Result);
	TestTrue(TEXT("The scope is stated"), Text.Contains(TEXT("Scope: /Game/Characters")));
	TestTrue(TEXT("The totals are stated"), Text.Contains(TEXT("6 assets: 4 tested, 1 skipped, 1 failed")));
	TestTrue(TEXT("The verdict totals are stated"), Text.Contains(TEXT("1 stable, 2 normalized on the first save, 1 unstable")));
	TestTrue(TEXT("Recurring changes are listed"), Text.Contains(TEXT("2 assets: [HeaderRegion] Thumbnail table")));
	TestTrue(TEXT("The unstable asset shows both resaves"), Text.Contains(TEXT("First resave")) && Text.Contains(TEXT("Second resave")) && Text.Contains(TEXT("[PropertyValueChanged] Stamp: 2 => 3")));
	TestTrue(TEXT("Skipped assets give their reason"), Text.Contains(TEXT("/Game/Characters/Edited: The asset has unsaved changes.")));
	TestTrue(TEXT("Failed assets give their reason"), Text.Contains(TEXT("/Game/Characters/Broken: The package could not be loaded.")));
	TestTrue(TEXT("Unstable assets come before stable ones"), Text.Find(TEXT("Sidekick")) < Text.Find(TEXT("Statue")));

	TSharedPtr<FJsonObject> Root;
	if (!TestTrue(TEXT("The JSON report is valid"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(AssetBatchReportWriter::ToJson(Result)), Root) && Root.IsValid()))
	{
		return false;
	}

	TestEqual(TEXT("The report kind"), Root->GetStringField(TEXT("kind")), FString(TEXT("batchNoOpResave")));
	TestEqual(TEXT("The scope"), Root->GetStringField(TEXT("scope")), FString(TEXT("/Game/Characters")));
	TestFalse(TEXT("Not cancelled"), Root->GetBoolField(TEXT("cancelled")));
	TestEqual(TEXT("Unstable count"), static_cast<int32>(Root->GetObjectField(TEXT("summary"))->GetNumberField(TEXT("unstable"))), 1);
	TestEqual(TEXT("A recurring change is listed"), Root->GetArrayField(TEXT("recurringChanges")).Num(), 1);

	const TArray<TSharedPtr<FJsonValue>>& Assets = Root->GetArrayField(TEXT("assets"));
	if (TestEqual(TEXT("Every asset is listed"), Assets.Num(), 6))
	{
		const TSharedPtr<FJsonObject> Unstable = Assets[2]->AsObject();
		TestEqual(TEXT("An asset's verdict"), Unstable->GetStringField(TEXT("verdict")), FString(TEXT("Unstable")));
		TestEqual(TEXT("Its second resave changes"), Unstable->GetObjectField(TEXT("secondResave"))->GetArrayField(TEXT("changes")).Num(), 1);

		const TSharedPtr<FJsonObject> Skipped = Assets[4]->AsObject();
		TestEqual(TEXT("A skipped asset's status"), Skipped->GetStringField(TEXT("status")), FString(TEXT("Skipped")));
		TestTrue(TEXT("It has no verdict"), Skipped->HasTypedField<EJson::Null>(TEXT("verdict")));
		TestTrue(TEXT("Or resave data"), Skipped->HasTypedField<EJson::Null>(TEXT("firstResave")));
	}

	const FDateTime Time(2026, 10, 1, 14, 7, 43);
	TestEqual(TEXT("The report is named after the folder and the time"), AssetBatchReportWriter::MakeDefaultFilename(TEXT("/Game/Characters"), Time, EAssetReportFormat::Text),
		FString(TEXT("NoOpResave_Characters_20261001-140743.txt")));
	TestEqual(TEXT("A description works as a scope"), AssetBatchReportWriter::MakeDefaultFilename(TEXT("5 selected assets"), Time, EAssetReportFormat::Json),
		FString(TEXT("NoOpResave_5selectedassets_20261001-140743.json")));

	const FString Filename = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("AssetSerializationInspector"), TEXT("batch_report.json"));
	FText Error;
	TestTrue(TEXT("The report is saved"), AssetBatchReportWriter::SaveToFile(Result, Filename, Error));
	FString Loaded;
	TestTrue(TEXT("It can be read back"), FFileHelper::LoadFileToString(Loaded, *Filename));
	TestEqual(TEXT("In the format of the extension"), Loaded, AssetBatchReportWriter::ToJson(Result));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetBatchResave_CondensesResults, "AssetSerializationInspector.Save.AssetBatchResave.CondensesResults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBatchResave_CondensesResults::RunTest(const FString& Parameters)
{
	FNoOpResaveResult Skipped;
	Skipped.PackageName = FName(TEXT("/Game/A"));
	Skipped.bSkipped = true;
	Skipped.Error = FText::FromString(TEXT("Dirty."));
	FAssetBatchResaveEntry Entry = AssetBatchResave::Condense(Skipped);
	TestEqual(TEXT("An expected refusal is a skip"), Entry.Status, EAssetBatchResaveStatus::Skipped);
	TestEqual(TEXT("Its reason is kept"), Entry.Message, FString(TEXT("Dirty.")));

	FNoOpResaveResult Failed;
	Failed.PackageName = FName(TEXT("/Game/B"));
	Failed.Error = FText::FromString(TEXT("Could not save."));
	Entry = AssetBatchResave::Condense(Failed);
	TestEqual(TEXT("Any other failure is a failure"), Entry.Status, EAssetBatchResaveStatus::Failed);

	FNoOpResaveResult Tainted;
	Tainted.PackageName = FName(TEXT("/Game/C"));
	Tainted.bSucceeded = true;
	Tainted.bOriginalFileModified = true;
	Entry = AssetBatchResave::Condense(Tainted);
	TestEqual(TEXT("A test that modified the original cannot be trusted"), Entry.Status, EAssetBatchResaveStatus::Failed);

	FNoOpResaveResult Fine;
	Fine.PackageName = FName(TEXT("/Game/D"));
	Fine.bSucceeded = true;
	Fine.Verdict = ENoOpResaveVerdict::NormalizedOnFirstSave;
	Entry = AssetBatchResave::Condense(Fine);
	TestEqual(TEXT("A finished test is tested"), Entry.Status, EAssetBatchResaveStatus::Tested);
	TestEqual(TEXT("With its verdict"), Entry.Verdict, ENoOpResaveVerdict::NormalizedOnFirstSave);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBatchResave_RunsOnAFolderOfRealAssets, "AssetSerializationInspector.Save.AssetBatchResave.RunsOnAFolderOfRealAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBatchResave_RunsOnAFolderOfRealAssets::RunTest(const FString& Parameters)
{
	using namespace AssetBatchResaveTestUtils;

	const FString Folder = TEXT("/Game/__AssetSerializationInspectorTests/Batch");
	const TArray<FString> Names = { TEXT("BatchCurveA"), TEXT("BatchCurveB"), TEXT("BatchCurveC"), TEXT("BatchCurveEdited") };

	TArray<UCurveFloat*> Curves;
	for (const FString& Name : Names)
	{
		// The last asset is saved and then edited, so the run must leave it alone.
		Curves.Add(SaveCurve(Folder / Name, Name == Names.Last()));
	}

	if (!TestFalse(TEXT("The test assets are written"), Curves.ContainsByPredicate([](const UCurveFloat* Curve) { return Curve == nullptr; })))
	{
		return false;
	}

	FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().ScanPathsSynchronous({ Folder }, true);

	const TArray<FName> Packages = AssetBatchResave::CollectPackages({ Folder }, true);
	TestEqual(TEXT("Every asset in the folder is found"), Packages.Num(), 4);

	int32 Calls = 0;
	const FAssetBatchResaveResult Result = AssetBatchResave::Run(Packages, Folder, [&Calls](const int32 Index, const int32 Total, const FName) {
		++Calls;
		return Index < Total;
	});

	TestEqual(TEXT("The callback is asked about every package"), Calls, 4);
	TestFalse(TEXT("The run was not cancelled"), Result.bCancelled);
	TestEqual(TEXT("Every package has an entry"), Result.Entries.Num(), 4);

	const FAssetBatchResaveSummary Summary = Result.Summarize();
	TestEqual(TEXT("Three assets were tested"), Summary.Tested, 3);
	TestEqual(TEXT("A freshly saved asset resaves identically"), Summary.Stable, 3);
	TestEqual(TEXT("The edited asset was skipped, not saved"), Summary.Skipped, 1);

	for (const FAssetBatchResaveEntry& Entry : Result.Entries)
	{
		if (Entry.PackageName.ToString().EndsWith(TEXT("BatchCurveEdited")))
		{
			TestEqual(TEXT("The edited asset is skipped"), Entry.Status, EAssetBatchResaveStatus::Skipped);
			TestTrue(TEXT("Because of its unsaved changes"), Entry.Message.Contains(TEXT("unsaved")));
		}
		else
		{
			TestEqual(*FString::Printf(TEXT("%s is tested"), *Entry.PackageName.ToString()), Entry.Status, EAssetBatchResaveStatus::Tested);
			TestTrue(TEXT("A stable asset reports no changes"), Entry.FirstResaveChanges.IsEmpty());
		}
	}

	// Stopping from the callback ends the run with what has been tested so far.
	const FAssetBatchResaveResult Cancelled = AssetBatchResave::Run(Packages, Folder, [](const int32 Index, const int32, const FName) { return Index < 1; });
	TestTrue(TEXT("A run can be cancelled"), Cancelled.bCancelled);
	TestEqual(TEXT("Only the first package was tested"), Cancelled.Entries.Num(), 1);

	for (int32 Index = 0; Index < Curves.Num(); ++Index)
	{
		Curves[Index]->ClearFlags(RF_Public | RF_Standalone);
		Curves[Index]->GetPackage()->SetDirtyFlag(false);
		IFileManager::Get().Delete(*FPackageName::LongPackageNameToFilename(Folder / Names[Index], FPackageName::GetAssetPackageExtension()), false, true, true);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
