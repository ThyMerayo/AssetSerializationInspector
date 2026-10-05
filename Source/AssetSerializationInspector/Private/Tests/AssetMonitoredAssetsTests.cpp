// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "Curves/CurveFloat.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "Save/AssetSaveHistoryManager.h"
#include "Save/AssetSaveObserver.h"
#include "Widgets/SAssetMonitoredAssets.h"

namespace MonitoredAssetsTestUtils
{
	static bool SaveTestCurve(const FString& PackageName)
	{
		UPackage* Package = CreatePackage(*PackageName);
		UCurveFloat* Curve = NewObject<UCurveFloat>(Package, *FPackageName::GetShortName(PackageName), RF_Public | RF_Standalone);
		Curve->FloatCurve.AddKey(0.0f, 1.0f);
		Package->MarkAsFullyLoaded();

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		SaveArgs.bSlowTask = false;
		return UPackage::SavePackage(Package, Curve, *FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), SaveArgs);
	}

	/** The test assets are real files in the project; leftovers of an earlier run would change the counts. */
	static void DeleteTestFolder(const FString& PackagePath)
	{
		IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(PackagePath), false, true);
	}
} // namespace MonitoredAssetsTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetMonitoringManager_ChangesManySetsAtOnce, "AssetSerializationInspector.Save.AssetMonitoringManager.ChangesManySetsAtOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetMonitoringManager_ChangesManySetsAtOnce::RunTest(const FString& Parameters)
{
	FAssetMonitoringManager Manager(false);

	int32 Announcements = 0;
	Manager.OnChanged().AddLambda([&Announcements]() { ++Announcements; });

	const FName A(TEXT("/Game/A"));
	const FName B(TEXT("/Game/B"));
	const FName C(TEXT("/Game/C"));

	Manager.AddMonitoredAssets({ A, B, A });
	TestEqual(TEXT("Duplicates count once"), Manager.GetMonitoredAssets().Num(), 2);
	TestEqual(TEXT("Many additions are announced once"), Announcements, 1);

	Manager.AddMonitoredAssets({ A, B });
	TestEqual(TEXT("Nothing new is not announced"), Announcements, 1);

	Manager.AddMonitoredAsset(C);
	TestEqual(TEXT("A single addition is announced"), Announcements, 2);

	Manager.RemoveMonitoredAssets({ A, FName(TEXT("/Game/NotMonitored")) });
	TestEqual(TEXT("Removing several leaves the others"), Manager.GetMonitoredAssets().Num(), 2);
	TestFalse(TEXT("The removed one is gone"), Manager.IsMonitored(A));
	TestEqual(TEXT("Many removals are announced once"), Announcements, 3);

	Manager.RemoveMonitoredAssets({ FName(TEXT("/Game/NotMonitored")) });
	TestEqual(TEXT("Removing what is not monitored is not announced"), Announcements, 3);

	Manager.ClearMonitoredAssets();
	TestEqual(TEXT("Clearing empties the set"), Manager.GetMonitoredAssets().Num(), 0);
	TestEqual(TEXT("And is announced"), Announcements, 4);

	Manager.ClearMonitoredAssets();
	TestEqual(TEXT("Clearing an empty set is not announced"), Announcements, 4);

	Manager.SetMonitoredAssets({ A });
	TestEqual(TEXT("Replacing the set is announced"), Announcements, 5);

	Manager.HandleAssetRenamed(A, B);
	TestEqual(TEXT("A rename is announced"), Announcements, 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetMonitoringManager_MonitorsAFolder, "AssetSerializationInspector.Save.AssetMonitoringManager.MonitorsAFolder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetMonitoringManager_MonitorsAFolder::RunTest(const FString& Parameters)
{
	using namespace MonitoredAssetsTestUtils;

	const FString Folder = TEXT("/Game/__AssetSerializationInspectorTests/Monitoring");
	const FString Nested = Folder / TEXT("Nested");
	DeleteTestFolder(Folder);
	if (!TestTrue(TEXT("The test assets are written"), SaveTestCurve(Folder / TEXT("MonitoredA")) && SaveTestCurve(Folder / TEXT("MonitoredB")) && SaveTestCurve(Nested / TEXT("MonitoredC"))))
	{
		return false;
	}

	FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().ScanPathsSynchronous({ Folder }, true);

	TestEqual(TEXT("A folder lists its assets and its subfolders'"), FAssetMonitoringManager::FindPackagesUnder(Folder, true).Num(), 3);
	TestEqual(TEXT("Without subfolders only its own"), FAssetMonitoringManager::FindPackagesUnder(Folder, false).Num(), 2);

	FAssetMonitoringManager Manager(false);
	Manager.AddMonitoredAsset(FName(*(Folder / TEXT("MonitoredA"))));

	int32 Announcements = 0;
	Manager.OnChanged().AddLambda([&Announcements]() { ++Announcements; });

	TestEqual(TEXT("Only assets that were not monitored count as added"), Manager.AddMonitoredFolder(Folder, true), 2);
	TestEqual(TEXT("The folder's assets are all monitored"), Manager.GetMonitoredAssets().Num(), 3);
	TestEqual(TEXT("Adding a folder is announced once"), Announcements, 1);
	TestEqual(TEXT("Adding it again adds nothing"), Manager.AddMonitoredFolder(Folder, true), 0);
	TestEqual(TEXT("Nothing is announced"), Announcements, 1);
	TestEqual(TEXT("An empty folder adds nothing"), Manager.AddMonitoredFolder(TEXT("/Game/__AssetSerializationInspectorTests/NoSuchFolder"), true), 0);

	DeleteTestFolder(Folder);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetMonitoredAssets_BuildsItems, "AssetSerializationInspector.Widgets.AssetMonitoredAssets.BuildsItems", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetMonitoredAssets_BuildsItems::RunTest(const FString& Parameters)
{
	using namespace MonitoredAssetsTestUtils;

	const FString SavedPackage = TEXT("/Game/__AssetSerializationInspectorTests/MonitoringList/ListedCurve");
	DeleteTestFolder(TEXT("/Game/__AssetSerializationInspectorTests/MonitoringList"));
	if (!TestTrue(TEXT("The test asset is written"), SaveTestCurve(SavedPackage)))
	{
		return false;
	}

	FAssetMonitoringManager Manager(false);
	Manager.AddMonitoredAssets({ FName(TEXT("/Game/Zeta/Never")), FName(*SavedPackage), FName(TEXT("/Game/Alpha/Missing")) });

	const TArray<TSharedPtr<FAssetMonitoredItem>> Items = SAssetMonitoredAssets::BuildItems(Manager, FAssetSaveHistoryManager::Get());
	if (!TestEqual(TEXT("Every monitored asset is listed"), Items.Num(), 3))
	{
		return false;
	}

	// Package names sort case-insensitively, and an underscore comes before letters.
	TestEqual(TEXT("They are sorted by package name"), Items[1]->PackageName, FName(TEXT("/Game/Alpha/Missing")));
	TestEqual(TEXT("The last one is the last name"), Items[2]->PackageName, FName(TEXT("/Game/Zeta/Never")));
	TestEqual(TEXT("The name and the folder are split"), Items[0]->Name, FString(TEXT("ListedCurve")));
	TestEqual(TEXT("The folder is a content path"), Items[0]->Folder, FString(TEXT("/Game/__AssetSerializationInspectorTests/MonitoringList")));
	TestFalse(TEXT("An asset with a file is not flagged"), Items[0]->bFileMissing);
	TestTrue(TEXT("An asset without a file is flagged"), Items[1]->bFileMissing && Items[2]->bFileMissing);
	TestEqual(TEXT("No saves are recorded for them"), Items[0]->SavesRecorded, 0);

	TestEqual(TEXT("Search matches the package name, ignoring case"), SAssetMonitoredAssets::Filter(Items, TEXT("listed")).Num(), 1);
	TestEqual(TEXT("Search matches the folder too"), SAssetMonitoredAssets::Filter(Items, TEXT("/game/zeta")).Num(), 1);
	TestEqual(TEXT("No search keeps all"), SAssetMonitoredAssets::Filter(Items, FString()).Num(), 3);
	TestEqual(TEXT("A search with no match keeps none"), SAssetMonitoredAssets::Filter(Items, TEXT("nothing like this")).Num(), 0);

	TestFalse(TEXT("Every result kind has a phrase"), SAssetMonitoredAssets::GetResultText(EAssetSaveResultKind::NativeOnlyChanges).IsEmpty());
	TestFalse(TEXT("Including property changes"), SAssetMonitoredAssets::GetResultText(EAssetSaveResultKind::SemanticChanges).IsEmpty());

	DeleteTestFolder(TEXT("/Game/__AssetSerializationInspectorTests/MonitoringList"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetMonitoredAssets_ConstructsTheWindow, "AssetSerializationInspector.Widgets.AssetMonitoredAssets.ConstructsTheWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetMonitoredAssets_ConstructsTheWindow::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("Slate is not initialized in this run; the window itself was not constructed."));
		return true;
	}

	// With a manager of its own, and with none given (the window then follows the global one, as the menu entry opens it).
	FAssetMonitoringManager Manager(false);
	Manager.AddMonitoredAssets({ FName(TEXT("/Game/Alpha/Thing")), FName(TEXT("/Game/Beta/Other")) });

	const TSharedRef<SAssetMonitoredAssets> Own = SNew(SAssetMonitoredAssets).Manager(&Manager);
	TestEqual(TEXT("It lists the manager's assets"), Own->GetVisibleItems().Num(), 2);

	Manager.RemoveMonitoredAsset(FName(TEXT("/Game/Alpha/Thing")));
	TestEqual(TEXT("It follows the manager"), Own->GetVisibleItems().Num(), 1);

	// A recorded save of a monitored asset shows up without reopening the window.
	TestEqual(TEXT("No save is recorded yet"), Own->GetVisibleItems()[0]->SavesRecorded, 0);

	const TSharedRef<FObservedAssetSave> Save = MakeShared<FObservedAssetSave>();
	Save->PackageName = FName(TEXT("/Game/Beta/Other"));
	Save->SaveId = 987654321;
	Save->Timestamp = FDateTime::Now();
	Save->Analysis.ResultKind = EAssetSaveResultKind::SemanticChanges;
	FAssetSaveHistoryManager::Get().RecordSave(Save);
	FAssetSaveObserver::Get().OnObservedAssetSave().Broadcast(Save);

	TestEqual(TEXT("The save is counted after the observer announces it"), Own->GetVisibleItems()[0]->SavesRecorded, 1);
	TestEqual(TEXT("With what it did"), Own->GetVisibleItems()[0]->LastResult, EAssetSaveResultKind::SemanticChanges);

	const TSharedRef<SAssetMonitoredAssets> Global = SNew(SAssetMonitoredAssets);
	TestEqual(TEXT("Without a manager it lists the global one"), Global->GetVisibleItems().Num(), FAssetMonitoringManager::Get().GetMonitoredAssets().Num());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
