// Copyright Diego Merayo Merayo. All Rights Reserved

#include "AssetSerializationInspector.h"

#include "ContentBrowserMenuContexts.h"
#include "Framework/Notifications/NotificationManager.h"
#include "LevelEditor.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#include "AssetSerializationInspectorCommands.h"
#include "AssetSerializationInspectorStyle.h"
#include "Save/AssetSaveObserver.h"
#include "Widgets/SAssetSerializationDiff.h"
#include "Widgets/SAssetSerializationInspector.h"

static const FName AssetSerializationInspectorTabName("Asset Serialization Inspector");
static const FName DiffTabName(TEXT("Asset Serialization Diff"));

#define LOCTEXT_NAMESPACE "FAssetSerializationInspectorModule"

void FAssetSerializationInspectorModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	FAssetSaveObserver::Get().Startup();

	FAssetSerializationInspectorStyle::Initialize();
	FAssetSerializationInspectorStyle::ReloadTextures();

	FAssetSerializationInspectorCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FAssetSerializationInspectorCommands::Get().OpenPluginWindow, FExecuteAction::CreateRaw(this, &FAssetSerializationInspectorModule::PluginButtonClicked), FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAssetSerializationInspectorModule::RegisterMenus));

	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(AssetSerializationInspectorTabName, FOnSpawnTab::CreateRaw(this, &FAssetSerializationInspectorModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("FAssetSerializationInspectorTabTitle", "Asset Serialization Inspector"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(DiffTabName, FOnSpawnTab::CreateRaw(this, &FAssetSerializationInspectorModule::OnSpawnDiffTab))
		.SetDisplayName(LOCTEXT("DiffTabTitle", "Asset Serialization Diff"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	ObservedSaveHandle = FAssetSaveObserver::Get().OnObservedAssetSave().AddRaw(this, &FAssetSerializationInspectorModule::HandleObservedAssetSave);
}

void FAssetSerializationInspectorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	FAssetSaveObserver::Get().Shutdown();

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FAssetSerializationInspectorStyle::Shutdown();

	FAssetSerializationInspectorCommands::Unregister();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AssetSerializationInspectorTabName);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(DiffTabName);

	FAssetSaveObserver::Get().OnObservedAssetSave().Remove(ObservedSaveHandle);
}

TSharedRef<SDockTab> FAssetSerializationInspectorModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SAssetSerializationInspector)];
}

TSharedRef<SDockTab> FAssetSerializationInspectorModule::OnSpawnDiffTab(const FSpawnTabArgs& SpawnTabArgs)
{
	TSharedRef<SAssetSerializationDiff> DiffWidget = SNew(SAssetSerializationDiff).Session(PendingDiffSession);
	ActiveDiffWidget = DiffWidget;
	PendingDiffSession.Reset();

	return SNew(SDockTab).TabRole(ETabRole::NomadTab)[DiffWidget];
}

void FAssetSerializationInspectorModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(AssetSerializationInspectorTabName);
	FGlobalTabmanager::Get()->TryInvokeTab(DiffTabName);
}

void FAssetSerializationInspectorModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FAssetSerializationInspectorCommands::Get().OpenPluginWindow, PluginCommands);
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FAssetSerializationInspectorCommands::Get().OpenPluginWindow));
				Entry.SetCommandList(PluginCommands);
			}
		}
	}

	// Context Menu
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu");

		FToolMenuSection& Section = Menu->FindOrAddSection("AssetSerializationInspector", LOCTEXT("AssetSerializationSection", "Asset Serialization"));

		Section.AddDynamicEntry("AssetSerializationMonitoring", FNewToolMenuSectionDelegate::CreateLambda([this](FToolMenuSection& InSection) {
			const UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();

			if (Context == nullptr || Context->SelectedAssets.IsEmpty())
			{
				return;
			}

			// Add menu entries here.
			TArray<FName> SelectedPackages;
			for (const FAssetData& AssetData : Context->SelectedAssets)
			{
				SelectedPackages.Add(AssetData.PackageName);
			}

			bool bAllMonitored = true;
			bool bAnyMonitored = false;

			for (const FName PackageName : SelectedPackages)
			{
				const bool bMonitored = FAssetMonitoringManager::Get().IsMonitored(PackageName);

				bAllMonitored &= bMonitored;
				bAnyMonitored |= bMonitored;
			}

			if (!bAllMonitored)
			{
				InSection.AddMenuEntry("StartAssetSerializationMonitoring", LOCTEXT("StartMonitoring", "Start Monitoring"),
					LOCTEXT("StartMonitoringTooltip",
						"Monitor the selected assets and record structural "
						"differences whenever they are saved."),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Eye"), FUIAction(FExecuteAction::CreateLambda([SelectedPackages = SelectedPackages]() {
						for (const FName PackageName : SelectedPackages)
						{
							FAssetMonitoringManager::Get().AddMonitoredAsset(PackageName);
						}
					})));
			}

			if (bAnyMonitored)
			{
				InSection.AddMenuEntry("StopAssetSerializationMonitoring", LOCTEXT("StopMonitoring", "Stop Monitoring"), LOCTEXT("StopMonitoringTooltip", "Stop monitoring the selected assets."),
					FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([SelectedPackages = SelectedPackages]() {
						for (const FName PackageName : SelectedPackages)
						{
							FAssetMonitoringManager::Get().RemoveMonitoredAsset(PackageName);
						}
					})));
			}
		}));
	}
}

void FAssetSerializationInspectorModule::HandleObservedAssetSave(TSharedPtr<FObservedAssetSave> Save)
{
	if (!Save.IsValid() || !Save->HasChanges())
	{
		return;
	}

	ShowSaveDiffNotification(Save);
}

void FAssetSerializationInspectorModule::ShowSaveDiffNotification(TSharedPtr<FObservedAssetSave> Save)
{
	const FString AssetName = FPackageName::GetShortName(Save->PackageName.ToString());

	FText Message;

	switch (Save->ChangeKind)
	{
		case EObservedSaveChangeKind::PayloadChange:
			Message = FText::Format(LOCTEXT("PayloadChangedNotification", "{0} saved with payload changes"), FText::FromString(AssetName));
			break;

		case EObservedSaveChangeKind::LayoutOnly:
			Message = FText::Format(LOCTEXT("LayoutChangedNotification", "{0} saved with layout-only changes"), FText::FromString(AssetName));
			break;

		case EObservedSaveChangeKind::MetadataOnly:
			Message = FText::Format(LOCTEXT("MetadataChangedNotification", "{0} saved with metadata changes"), FText::FromString(AssetName));
			break;

		default:
			Message = FText::Format(LOCTEXT("AssetChangedNotification", "{0} saved with serialized changes"), FText::FromString(AssetName));
			break;
	}

	FNotificationInfo Info(Message);

	Info.ExpireDuration = 8.0f;
	Info.bFireAndForget = true;

	Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("ViewDiffButton", "View Diff"),
		LOCTEXT("ViewDiffButtonTooltip",
			"Open the serialization differences "
			"produced by this save."),
		FSimpleDelegate::CreateRaw(this, &FAssetSerializationInspectorModule::OpenObservedSaveDiff, Save), SNotificationItem::CS_None));

	FSlateNotificationManager::Get().AddNotification(Info);
}

static TSharedRef<FAssetSerializationDiffSession> MakeDiffSession(const FObservedAssetSave& Save)
{
	TSharedRef<FAssetSerializationDiffSession> Session = MakeShared<FAssetSerializationDiffSession>();
	Session->Old.Document = Save.Before;
	Session->Old.Traces = Save.BeforeFields;
	Session->New.Document = Save.After;
	Session->New.Traces = Save.AfterFields;
	Session->DiffResult = Save.Diff;

	return Session;
}

void FAssetSerializationInspectorModule::OpenObservedSaveDiff(TSharedPtr<FObservedAssetSave> Save)
{
	if (!Save.IsValid())
	{
		return;
	}

	PendingDiffSession = MakeDiffSession(*Save);

	TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->TryInvokeTab(DiffTabName);

	if (TSharedPtr<SAssetSerializationDiff> Widget = ActiveDiffWidget.Pin())
	{
		Widget->SetSession(PendingDiffSession);

		PendingDiffSession.Reset();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAssetSerializationInspectorModule, AssetSerializationInspector)