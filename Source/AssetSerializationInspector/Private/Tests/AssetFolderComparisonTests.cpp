// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Curves/CurveFloat.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/CustomVersion.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "Compare/AssetFolderComparison.h"
#include "Model/AssetPackageDocument.h"
#include "Report/AssetFolderComparisonReportWriter.h"

namespace AssetFolderComparisonTestUtils
{
	static FAssetFolderComparisonEntry MakeEntry(
		const TCHAR* Path, const EAssetFolderComparisonStatus Status, const TCHAR* OldVersion = TEXT(""), const TCHAR* NewVersion = TEXT(""), const TArray<FAssetBatchResaveChange>& Changes = {})
	{
		FAssetFolderComparisonEntry Entry;
		Entry.RelativePath = Path;
		Entry.Status = Status;
		Entry.OldEngineVersion = OldVersion;
		Entry.NewEngineVersion = NewVersion;
		Entry.OldFileVersion = FString(OldVersion).IsEmpty() ? FString() : FString(TEXT("UE4 522 / UE5 1012"));
		Entry.NewFileVersion = FString(NewVersion).IsEmpty() ? FString() : FString(TEXT("UE4 522 / UE5 1017"));
		Entry.bVersionsDiffer = FString(OldVersion) != FString(NewVersion);
		Entry.Changes = Changes;
		Entry.OldFileSize = FString(OldVersion).IsEmpty() ? INDEX_NONE : 1000;
		Entry.NewFileSize = FString(NewVersion).IsEmpty() ? INDEX_NONE : (Changes.IsEmpty() ? 1000 : 1200);
		return Entry;
	}

	static FAssetBatchResaveChange MakeChange(const TCHAR* Category, const TCHAR* Name, const TCHAR* Detail = TEXT(""))
	{
		FAssetBatchResaveChange Change;
		Change.Category = Category;
		Change.Name = Name;
		Change.Detail = Detail;
		return Change;
	}

	static FAssetFolderComparisonResult MakeResult()
	{
		FAssetFolderComparisonResult Result;
		Result.OldFolder = TEXT("D:/Projects/Game_5.5/Content");
		Result.NewFolder = TEXT("D:/Projects/Game_5.8/Content");
		Result.StartedAt = FDateTime(2026, 10, 1, 14, 0, 0);
		Result.FinishedAt = FDateTime(2026, 10, 1, 14, 5, 0);

		// Two assets lost their thumbnails when the project moved to the newer engine; one also had an edit.
		Result.Entries.Add(MakeEntry(
			TEXT("Characters/Hero.uasset"), EAssetFolderComparisonStatus::Changed, TEXT("5.5.1"), TEXT("5.8.0"), { MakeChange(TEXT("HeaderRegion"), TEXT("Thumbnail table"), TEXT("Removed")) }));
		Result.Entries.Add(MakeEntry(TEXT("Characters/Villain.uasset"), EAssetFolderComparisonStatus::Changed, TEXT("5.5.1"), TEXT("5.8.0"),
			{ MakeChange(TEXT("HeaderRegion"), TEXT("Thumbnail table"), TEXT("Removed")), MakeChange(TEXT("PropertyValueChanged"), TEXT("Health"), TEXT("50 => 75")) }));
		Result.Entries.Add(MakeEntry(TEXT("Characters/Statue.uasset"), EAssetFolderComparisonStatus::Identical, TEXT("5.8.0"), TEXT("5.8.0")));
		Result.Entries.Add(MakeEntry(TEXT("Characters/Retired.uasset"), EAssetFolderComparisonStatus::OnlyInOldFolder, TEXT("5.5.1"), TEXT("")));
		Result.Entries.Add(MakeEntry(TEXT("Characters/Newcomer.uasset"), EAssetFolderComparisonStatus::OnlyInNewFolder, TEXT(""), TEXT("5.8.0")));

		FAssetFolderComparisonEntry Failed;
		Failed.RelativePath = TEXT("Characters/Broken.uasset");
		Failed.Status = EAssetFolderComparisonStatus::Failed;
		Failed.Message = TEXT("Old file: The package version is not supported.");
		Result.Entries.Add(Failed);

		return Result;
	}

	/** Saves a curve asset with the given number of keys and returns the file the engine wrote. */
	static UCurveFloat* SaveCurve(const FString& PackageName, const int32 KeyCount, FString& OutFilename)
	{
		UPackage* Package = CreatePackage(*PackageName);
		UCurveFloat* Curve = FindObject<UCurveFloat>(Package, *FPackageName::GetShortName(PackageName));
		if (Curve == nullptr)
		{
			Curve = NewObject<UCurveFloat>(Package, *FPackageName::GetShortName(PackageName), RF_Public | RF_Standalone);
		}

		Curve->FloatCurve.Reset();
		for (int32 Index = 0; Index < KeyCount; ++Index)
		{
			Curve->FloatCurve.AddKey(static_cast<float>(Index), static_cast<float>(Index + 1));
		}
		Package->MarkAsFullyLoaded();
		Package->MarkPackageDirty();

		OutFilename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		SaveArgs.bSlowTask = false;

		return UPackage::SavePackage(Package, Curve, *OutFilename, SaveArgs) ? Curve : nullptr;
	}

	static const FAssetFolderComparisonEntry* FindEntry(const FAssetFolderComparisonResult& Result, const FString& RelativePath)
	{
		return Result.Entries.FindByPredicate([&RelativePath](const FAssetFolderComparisonEntry& Entry) { return Entry.RelativePath == RelativePath; });
	}
} // namespace AssetFolderComparisonTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetFolderComparison_SummarizesAndGroupsCauses, "AssetSerializationInspector.Compare.AssetFolderComparison.SummarizesAndGroupsCauses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetFolderComparison_SummarizesAndGroupsCauses::RunTest(const FString& Parameters)
{
	using namespace AssetFolderComparisonTestUtils;

	const FAssetFolderComparisonResult Result = MakeResult();
	const FAssetFolderComparisonSummary Summary = Result.Summarize();

	TestEqual(TEXT("Identical"), Summary.Identical, 1);
	TestEqual(TEXT("Changed"), Summary.Changed, 2);
	TestEqual(TEXT("Changed with different versions"), Summary.ChangedWithDifferentVersions, 2);
	TestEqual(TEXT("Only in the old folder"), Summary.OnlyInOldFolder, 1);
	TestEqual(TEXT("Only in the new folder"), Summary.OnlyInNewFolder, 1);
	TestEqual(TEXT("Failed"), Summary.Failed, 1);

	// Only files present in both folders say which engine versions are involved.
	const TArray<FAssetEngineVersionPair> Pairs = Result.FindEngineVersionPairs();
	if (TestEqual(TEXT("Two engine version changes"), Pairs.Num(), 2))
	{
		TestEqual(TEXT("The most common is first"), Pairs[0].OldEngineVersion + TEXT(">") + Pairs[0].NewEngineVersion, FString(TEXT("5.5.1>5.8.0")));
		TestEqual(TEXT("It covers two files"), Pairs[0].AssetCount, 2);
		TestEqual(TEXT("The unchanged-version pair"), Pairs[1].OldEngineVersion + TEXT(">") + Pairs[1].NewEngineVersion, FString(TEXT("5.8.0>5.8.0")));
	}

	const TArray<FAssetBatchRecurringChange> Recurring = Result.FindRecurringChanges();
	if (TestEqual(TEXT("One change recurs"), Recurring.Num(), 1))
	{
		TestEqual(TEXT("It is the thumbnail table"), Recurring[0].Name, FString(TEXT("Thumbnail table")));
		TestEqual(TEXT("In two files"), Recurring[0].AssetCount, 2);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetFolderComparison_WritesReports, "AssetSerializationInspector.Report.AssetFolderComparisonReportWriter.WritesReports",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetFolderComparison_WritesReports::RunTest(const FString& Parameters)
{
	using namespace AssetFolderComparisonTestUtils;

	const FAssetFolderComparisonResult Result = MakeResult();

	const FString Text = AssetFolderComparisonReportWriter::ToText(Result);
	TestTrue(TEXT("Both folders are named"), Text.Contains(TEXT("Old folder: D:/Projects/Game_5.5/Content")) && Text.Contains(TEXT("New folder: D:/Projects/Game_5.8/Content")));
	TestTrue(
		TEXT("The totals are stated"), Text.Contains(TEXT("6 files: 1 identical, 2 changed (2 of them saved by different versions), 1 only in the old folder, 1 only in the new folder, 1 failed")));
	TestTrue(TEXT("The engine versions are stated"), Text.Contains(TEXT("2 files: 5.5.1 -> 5.8.0")));
	TestTrue(TEXT("Recurring changes are listed"), Text.Contains(TEXT("2 files: [HeaderRegion] Thumbnail table")));
	TestTrue(TEXT("A changed file shows its versions and changes"),
		Text.Contains(TEXT("(1000 -> 1200 bytes)")) && Text.Contains(TEXT("saved by 5.5.1 (UE4 522 / UE5 1012) -> 5.8.0 (UE4 522 / UE5 1017)"))
			&& Text.Contains(TEXT("[PropertyValueChanged] Health: 50 => 75")));
	TestTrue(TEXT("A failed file gives its reason"), Text.Contains(TEXT("Characters/Broken.uasset: Old file: The package version is not supported.")));
	TestTrue(TEXT("One-sided files are listed"), Text.Contains(TEXT("Characters/Retired.uasset (saved by 5.5.1")) && Text.Contains(TEXT("Characters/Newcomer.uasset (saved by 5.8.0")));
	TestFalse(TEXT("Identical files are only counted"), Text.Contains(TEXT("Statue")));

	TSharedPtr<FJsonObject> Root;
	if (!TestTrue(TEXT("The JSON report is valid"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(AssetFolderComparisonReportWriter::ToJson(Result)), Root) && Root.IsValid()))
	{
		return false;
	}

	TestEqual(TEXT("The report kind"), Root->GetStringField(TEXT("kind")), FString(TEXT("folderComparison")));
	TestEqual(TEXT("The new folder"), Root->GetStringField(TEXT("newFolder")), FString(TEXT("D:/Projects/Game_5.8/Content")));
	TestEqual(TEXT("Changed count"), static_cast<int32>(Root->GetObjectField(TEXT("summary"))->GetNumberField(TEXT("changed"))), 2);
	TestEqual(TEXT("Engine version pairs"), Root->GetArrayField(TEXT("engineVersions")).Num(), 2);
	TestEqual(TEXT("A recurring change"), Root->GetArrayField(TEXT("recurringChanges")).Num(), 1);

	const TArray<TSharedPtr<FJsonValue>>& Assets = Root->GetArrayField(TEXT("assets"));
	if (TestEqual(TEXT("Every file is listed, identical ones too"), Assets.Num(), 6))
	{
		const TSharedPtr<FJsonObject> Villain = Assets[1]->AsObject();
		TestEqual(TEXT("A changed file's status"), Villain->GetStringField(TEXT("status")), FString(TEXT("Changed")));
		TestEqual(TEXT("Its old engine"), Villain->GetStringField(TEXT("oldEngineVersion")), FString(TEXT("5.5.1")));
		TestTrue(TEXT("The versions differ"), Villain->GetBoolField(TEXT("versionsDiffer")));
		TestEqual(TEXT("Its sizes"), static_cast<int32>(Villain->GetNumberField(TEXT("newFileSize"))), 1200);
		TestEqual(TEXT("Its changes"), Villain->GetArrayField(TEXT("changes")).Num(), 2);

		const TSharedPtr<FJsonObject> Retired = Assets[3]->AsObject();
		TestTrue(TEXT("A missing version is null"), Retired->HasTypedField<EJson::Null>(TEXT("newEngineVersion")));
		TestTrue(TEXT("So is the size of a file the folder lacks"), Retired->HasTypedField<EJson::Null>(TEXT("newFileSize")));
		TestTrue(TEXT("So is a missing message"), Retired->HasTypedField<EJson::Null>(TEXT("message")));
	}

	const FDateTime Time(2026, 10, 1, 14, 7, 43);
	TestEqual(TEXT("The report is named after both folders"),
		AssetFolderComparisonReportWriter::MakeDefaultFilename(TEXT("D:\\Projects\\Game 5.5\\Content\\"), TEXT("D:/Projects/Game_5.8/Content"), Time, EAssetReportFormat::Text),
		FString(TEXT("FolderComparison_Content_vs_Content_20261001-140743.txt")));

	const FString Filename = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("AssetSerializationInspector"), TEXT("folder_comparison.json"));
	FText Error;
	TestTrue(TEXT("The report is saved"), AssetFolderComparisonReportWriter::SaveToFile(Result, Filename, Error));
	FString Loaded;
	TestTrue(TEXT("It can be read back"), FFileHelper::LoadFileToString(Loaded, *Filename));
	TestEqual(TEXT("In the format of the extension"), Loaded, AssetFolderComparisonReportWriter::ToJson(Result));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetFolderComparison_DescribesVersions, "AssetSerializationInspector.Compare.AssetFolderComparison.DescribesVersions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetFolderComparison_DescribesVersions::RunTest(const FString& Parameters)
{
	const auto MakeDocument = [](const uint16 Minor, const uint16 Patch, const int32 FileVersionUE5, const int32 CustomVersion) {
		FAssetPackageDocument Document;
		Document.PackageSummary.SetFileVersions(522, FileVersionUE5, 0);
		Document.PackageSummary.SavedByEngineVersion.Set(5, Minor, Patch, 1000, TEXT("++UE5+Release-5.5"));

		FCustomVersionContainer& Versions = const_cast<FCustomVersionContainer&>(Document.PackageSummary.GetCustomVersionContainer());
		Versions.SetVersion(FGuid(1, 2, 3, 4), CustomVersion, TEXT("Dev-Test"));
		return Document;
	};

	const FAssetPackageDocument Old = MakeDocument(5, 1, 1012, 12);

	TestEqual(TEXT("The engine version is short"), AssetFolderComparison::DescribeEngineVersion(Old), FString(TEXT("5.5.1")));
	TestEqual(TEXT("The file version names both numbers"), AssetFolderComparison::DescribeFileVersion(Old), FString(TEXT("UE4 522 / UE5 1012")));
	TestEqual(TEXT("A package that does not say who saved it has no engine version"), AssetFolderComparison::DescribeEngineVersion(FAssetPackageDocument()), FString());

	TestFalse(TEXT("Identical versions are not different"), AssetFolderComparison::HaveDifferentVersions(Old, MakeDocument(5, 1, 1012, 12)));
	TestTrue(TEXT("A different engine version"), AssetFolderComparison::HaveDifferentVersions(Old, MakeDocument(8, 0, 1012, 12)));
	TestTrue(TEXT("A different file version"), AssetFolderComparison::HaveDifferentVersions(Old, MakeDocument(5, 1, 1017, 12)));
	TestTrue(TEXT("A different custom version"), AssetFolderComparison::HaveDifferentVersions(Old, MakeDocument(5, 1, 1012, 15)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetFolderComparison_ComparesTwoFolders, "AssetSerializationInspector.Compare.AssetFolderComparison.ComparesTwoFolders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetFolderComparison_ComparesTwoFolders::RunTest(const FString& Parameters)
{
	using namespace AssetFolderComparisonTestUtils;

	const FString Root = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("AssetSerializationInspector"), TEXT("FolderComparison"));
	const FString OldFolder = FPaths::Combine(Root, TEXT("Old"));
	const FString NewFolder = FPaths::Combine(Root, TEXT("New"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);

	const FString PackageBase = TEXT("/Game/__AssetSerializationInspectorTests/FolderComparison/");
	TArray<UCurveFloat*> Curves;

	// Saves a curve, with the number of keys it should have in each folder, and copies the file to the folders.
	const auto Place = [&](const TCHAR* Name, const TCHAR* RelativePath, const int32 OldKeys, const int32 NewKeys) {
		FString File;
		UCurveFloat* Curve = SaveCurve(PackageBase + Name, OldKeys > 0 ? OldKeys : NewKeys, File);
		if (Curve == nullptr)
		{
			return false;
		}
		Curves.AddUnique(Curve);

		if (OldKeys > 0)
		{
			const FString Destination = FPaths::Combine(OldFolder, RelativePath);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination), true);
			IFileManager::Get().Copy(*Destination, *File);
		}

		if (NewKeys > 0 && NewKeys != OldKeys)
		{
			Curve = SaveCurve(PackageBase + Name, NewKeys, File);
			if (Curve == nullptr)
			{
				return false;
			}
		}

		if (NewKeys > 0)
		{
			const FString Destination = FPaths::Combine(NewFolder, RelativePath);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination), true);
			IFileManager::Get().Copy(*Destination, *File);
		}

		return true;
	};

	bool bPlaced = Place(TEXT("Same"), TEXT("Same.uasset"), 2, 2);
	bPlaced &= Place(TEXT("Changed"), TEXT("Changed.uasset"), 1, 3);
	bPlaced &= Place(TEXT("Nested"), TEXT("Sub/Nested.uasset"), 1, 2);
	bPlaced &= Place(TEXT("OnlyOld"), TEXT("OnlyOld.uasset"), 1, 0);
	bPlaced &= Place(TEXT("OnlyNew"), TEXT("OnlyNew.uasset"), 0, 1);
	TestTrue(TEXT("The test assets are written"), bPlaced);

	if (bPlaced)
	{
		const TArray<FString> OldFiles = AssetFolderComparison::FindPackageFiles(OldFolder);
		TestEqual(
			TEXT("Files are found recursively, relative to the folder"), OldFiles, TArray<FString>({ TEXT("Changed.uasset"), TEXT("OnlyOld.uasset"), TEXT("Same.uasset"), TEXT("Sub/Nested.uasset") }));

		int32 Calls = 0;
		const FAssetFolderComparisonResult Result = AssetFolderComparison::Run(OldFolder, NewFolder, [&Calls](int32, int32, const FString&) {
			++Calls;
			return true;
		});

		TestEqual(TEXT("Every file is considered once"), Calls, 5);
		TestFalse(TEXT("Not cancelled"), Result.bCancelled);

		const FAssetFolderComparisonSummary Summary = Result.Summarize();
		TestEqual(TEXT("One identical file"), Summary.Identical, 1);
		TestEqual(TEXT("Two changed files"), Summary.Changed, 2);
		TestEqual(TEXT("One file only in the old folder"), Summary.OnlyInOldFolder, 1);
		TestEqual(TEXT("One file only in the new folder"), Summary.OnlyInNewFolder, 1);
		TestEqual(TEXT("Nothing failed"), Summary.Failed, 0);
		TestEqual(TEXT("Same engine, so no version differences"), Summary.ChangedWithDifferentVersions, 0);

		const FAssetFolderComparisonEntry* Changed = FindEntry(Result, TEXT("Changed.uasset"));
		const FAssetFolderComparisonEntry* Nested = FindEntry(Result, TEXT("Sub/Nested.uasset"));
		const FAssetFolderComparisonEntry* Same = FindEntry(Result, TEXT("Same.uasset"));
		if (TestTrue(TEXT("The pairs are listed"), Changed && Nested && Same))
		{
			TestEqual(TEXT("A changed file is changed"), Changed->Status, EAssetFolderComparisonStatus::Changed);
			TestTrue(TEXT("Both file sizes are recorded"), Changed->OldFileSize > 0 && Changed->NewFileSize > Changed->OldFileSize);
			TestFalse(TEXT("And a list of changes"), Changed->Changes.IsEmpty());
			TestEqual(TEXT("Both files were saved by this engine"), Changed->OldEngineVersion, Changed->NewEngineVersion);
			TestFalse(TEXT("So the versions do not differ"), Changed->bVersionsDiffer);
			TestEqual(TEXT("A file in a subfolder is paired by its relative path"), Nested->Status, EAssetFolderComparisonStatus::Changed);
			TestEqual(TEXT("An unchanged file is identical"), Same->Status, EAssetFolderComparisonStatus::Identical);
			TestTrue(TEXT("And has no changes"), Same->Changes.IsEmpty());
		}

		const FAssetFolderComparisonEntry* OnlyOld = FindEntry(Result, TEXT("OnlyOld.uasset"));
		if (TestNotNull(TEXT("A one-sided file is listed"), OnlyOld))
		{
			TestEqual(TEXT("Its status"), OnlyOld->Status, EAssetFolderComparisonStatus::OnlyInOldFolder);
			TestTrue(TEXT("Its size is still read"), OnlyOld->OldFileSize > 0);
			TestEqual(TEXT("The missing side has none"), OnlyOld->NewFileSize, static_cast<int64>(INDEX_NONE));
		}

		const FAssetFolderComparisonResult Cancelled = AssetFolderComparison::Run(OldFolder, NewFolder, [](const int32 Index, int32, const FString&) { return Index < 2; });
		TestTrue(TEXT("A run can be cancelled"), Cancelled.bCancelled);
		TestEqual(TEXT("With what was compared so far"), Cancelled.Entries.Num(), 2);
	}

	for (UCurveFloat* Curve : Curves)
	{
		Curve->ClearFlags(RF_Public | RF_Standalone);
		Curve->GetPackage()->SetDirtyFlag(false);
		IFileManager::Get().Delete(*FPackageName::LongPackageNameToFilename(Curve->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension()), false, true, true);
	}
	IFileManager::Get().DeleteDirectory(*Root, false, true);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
