// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Framework/Application/SlateApplication.h"
#include "ToolMenus.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetWindowMenu_GroupsTheEntriesInOneSubMenu, "AssetSerializationInspector.Widgets.AssetWindowMenu.GroupsTheEntriesInOneSubMenu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetWindowMenu_GroupsTheEntriesInOneSubMenu::RunTest(const FString& Parameters)
{
	// Menus only exist where the editor UI does.
	if (!FSlateApplication::IsInitialized() || !UToolMenus::IsToolMenuUIEnabled())
	{
		AddInfo(TEXT("The editor menus are not available in this run; the Window menu was not inspected."));
		return true;
	}

	UToolMenus* ToolMenus = UToolMenus::Get();
	const UToolMenu* WindowMenu = ToolMenus->FindMenu("LevelEditor.MainMenu.Window");
	if (!TestNotNull(TEXT("The Window menu exists"), WindowMenu))
	{
		return false;
	}

	// One entry of the plugin in the Window menu: the submenu.
	int32 Entries = 0;
	bool bHasSubMenu = false;
	const FToolMenuEntry* SubMenuEntry = nullptr;
	for (const FToolMenuSection& Section : WindowMenu->Sections)
	{
		for (const FToolMenuEntry& Entry : Section.Blocks)
		{
			if (Entry.Name == TEXT("AssetSerializationInspectorMenu") && Entry.IsSubMenu())
			{
				bHasSubMenu = true;
				SubMenuEntry = &Entry;
			}

			for (const TCHAR* Name : { TEXT("ShowMonitoredAssets"), TEXT("RunProjectNoOpResaveTest"), TEXT("ShowBatchResults"), TEXT("ShowFolderComparison"), TEXT("CompareAssetFolders") })
			{
				Entries += Entry.Name == Name ? 1 : 0;
			}
		}
	}

	TestTrue(TEXT("The Window menu has the plugin's submenu"), bHasSubMenu);
	TestEqual(TEXT("None of its entries is loose in the Window menu"), Entries, 0);

	// And the submenu holds them, in sections. Its content is built when it opens, so build it into a menu of the test's.
	if (SubMenuEntry != nullptr)
	{
		const FName TestMenuName("AssetSerializationInspector.WindowMenuTest");
		UToolMenu* SubMenu = ToolMenus->RegisterMenu(TestMenuName);
		SubMenuEntry->SubMenuData.ConstructMenu.NewToolMenu.ExecuteIfBound(SubMenu);

		TSet<FName> Names;
		for (const FToolMenuSection& Section : SubMenu->Sections)
		{
			for (const FToolMenuEntry& Entry : Section.Blocks)
			{
				Names.Add(Entry.Name);
			}
		}

		TestTrue(TEXT("The submenu has sections"), SubMenu->Sections.Num() >= 3);
		for (const TCHAR* Name : { TEXT("ShowMonitoredAssets"), TEXT("RunProjectNoOpResaveTest"), TEXT("ShowBatchResults"), TEXT("ShowFolderComparison"), TEXT("CompareAssetFolders") })
		{
			TestTrue(*FString::Printf(TEXT("The submenu holds %s"), Name), Names.Contains(Name));
		}

		ToolMenus->RemoveMenu(TestMenuName);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
