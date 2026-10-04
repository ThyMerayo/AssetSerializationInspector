// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/ConfigCacheIni.h"

#include "AssetSerializationInspectorSettings.h"
#include "Save/AssetSaveObserver.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetMonitoringManager_AddsAndRemovesAssets, "AssetSerializationInspector.Save.AssetMonitoringManager.AddsAndRemovesAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetMonitoringManager_AddsAndRemovesAssets::RunTest(const FString& Parameters)
{
	// A manager that does not touch the user's settings.
	FAssetMonitoringManager Manager(false);

	const FName Hero(TEXT("/Game/Characters/Hero"));
	TestFalse(TEXT("Nothing is monitored at first"), Manager.IsMonitored(Hero));

	Manager.AddMonitoredAsset(Hero);
	Manager.AddMonitoredAsset(Hero);
	TestTrue(TEXT("An added asset is monitored"), Manager.IsMonitored(Hero));
	TestEqual(TEXT("Monitoring an asset twice is the same as once"), Manager.GetMonitoredAssets().Num(), 1);

	Manager.RemoveMonitoredAsset(Hero);
	TestFalse(TEXT("A removed asset is not monitored"), Manager.IsMonitored(Hero));
	TestEqual(TEXT("A single removal is enough"), Manager.GetMonitoredAssets().Num(), 0);

	// Removing what is not monitored is harmless.
	Manager.RemoveMonitoredAsset(Hero);
	TestTrue(TEXT("The set stays empty"), Manager.GetMonitoredAssets().IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetMonitoringManager_FollowsRenamesAndDeletes, "AssetSerializationInspector.Save.AssetMonitoringManager.FollowsRenamesAndDeletes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetMonitoringManager_FollowsRenamesAndDeletes::RunTest(const FString& Parameters)
{
	FAssetMonitoringManager Manager(false);

	const FName Old(TEXT("/Game/Old/Hero"));
	const FName New(TEXT("/Game/New/Hero"));
	const FName Other(TEXT("/Game/Other"));
	Manager.SetMonitoredAssets({ Old, Other });

	// A rename of an asset that is not monitored changes nothing.
	Manager.HandleAssetRenamed(FName(TEXT("/Game/Unrelated")), FName(TEXT("/Game/Renamed")));
	TestEqual(TEXT("An unrelated rename is ignored"), Manager.GetMonitoredAssets().Num(), 2);
	TestFalse(TEXT("And does not start monitoring the new name"), Manager.IsMonitored(FName(TEXT("/Game/Renamed"))));

	// Moving a monitored asset keeps it monitored under its new name.
	Manager.HandleAssetRenamed(Old, New);
	TestFalse(TEXT("The old name is no longer monitored"), Manager.IsMonitored(Old));
	TestTrue(TEXT("The new name is"), Manager.IsMonitored(New));
	TestTrue(TEXT("The other asset is untouched"), Manager.IsMonitored(Other));

	// Deleting a monitored asset stops monitoring it.
	Manager.HandleAssetRemoved(New);
	TestFalse(TEXT("A deleted asset is not monitored"), Manager.IsMonitored(New));
	TestEqual(TEXT("One asset is left"), Manager.GetMonitoredAssets().Num(), 1);

	// Replacing the set (the settings were edited) forgets what is not in the new list.
	Manager.SetMonitoredAssets({ New });
	TestTrue(TEXT("The replacement is monitored"), Manager.IsMonitored(New));
	TestFalse(TEXT("What it left out is not"), Manager.IsMonitored(Other));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetMonitoringManager_SettingsArePerUser, "AssetSerializationInspector.Save.AssetMonitoringManager.SettingsArePerUser", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetMonitoringManager_SettingsArePerUser::RunTest(const FString& Parameters)
{
	const UAssetSerializationInspectorSettings* Settings = GetDefault<UAssetSerializationInspectorSettings>();
	TestNotNull(TEXT("The settings exist"), Settings);

	// The list is a user preference, saved under the project's Saved folder rather than into a Default*.ini under version control.
	TestEqual(TEXT("They are saved with the editor per-project user settings"), UAssetSerializationInspectorSettings::StaticClass()->ClassConfigName, FName(TEXT("EditorPerProjectUserSettings")));
	TestFalse(TEXT("Not into the project's default config"), UAssetSerializationInspectorSettings::StaticClass()->HasAnyClassFlags(CLASS_DefaultConfig));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetMonitoringManager_WritesTheSettings, "AssetSerializationInspector.Save.AssetMonitoringManager.WritesTheSettings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetMonitoringManager_WritesTheSettings::RunTest(const FString& Parameters)
{
	// This one uses the settings, so it adds a package that cannot exist and removes it again.
	const FName Package(TEXT("/Game/__AssetSerializationInspectorTests/MonitoringPersistence"));
	const TCHAR* Section = TEXT("/Script/AssetSerializationInspector.AssetSerializationInspectorSettings");

	const auto IsStored = [&Package, Section]() {
		TArray<FString> Stored;
		GConfig->GetArray(Section, TEXT("MonitoredPackages"), Stored, GEditorPerProjectIni);
		return Stored.Contains(Package.ToString());
	};

	UAssetSerializationInspectorSettings* Settings = GetMutableDefault<UAssetSerializationInspectorSettings>();
	const TArray<FName> Before = Settings->MonitoredPackages;

	{
		FAssetMonitoringManager Manager;
		Manager.AddMonitoredAsset(Package);
		TestTrue(TEXT("An added asset is in the settings"), Settings->MonitoredPackages.Contains(Package));
		TestEqual(TEXT("Once"), Settings->MonitoredPackages.FilterByPredicate([&Package](const FName Name) { return Name == Package; }).Num(), 1);
		TestTrue(TEXT("And written to the user's config file"), IsStored());

		// A new manager (as after a restart) starts from what was stored.
		FAssetMonitoringManager Restarted;
		TestTrue(TEXT("A manager started later monitors it"), Restarted.IsMonitored(Package));

		Manager.RemoveMonitoredAsset(Package);
		TestFalse(TEXT("A removed asset is out of the settings"), Settings->MonitoredPackages.Contains(Package));
		TestFalse(TEXT("And out of the config file"), IsStored());
	}

	// Leave the user's list as it was.
	Settings->MonitoredPackages = Before;
	Settings->SaveConfig();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
