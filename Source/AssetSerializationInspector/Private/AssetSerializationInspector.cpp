// Copyright Diego Merayo Merayo. All Rights Reserved

#include "AssetSerializationInspector.h"

#include "ContentBrowserMenuContexts.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IDesktopPlatform.h"
#include "ISourceControlRevision.h"
#include "LevelEditor.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "ToolMenus.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#include "AssetSerializationInspectorCommands.h"
#include "AssetSerializationInspectorStyle.h"
#include "Compare/AssetFolderComparison.h"
#include "Compare/AssetSourceControlCompare.h"
#include "Diff/AssetPackageDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Report/AssetBatchReportWriter.h"
#include "Report/AssetFolderComparisonReportWriter.h"
#include "Save/AssetBatchResave.h"
#include "Save/AssetNoOpResaveTest.h"
#include "Save/AssetSaveAnalyzer.h"
#include "Save/AssetSaveObserver.h"
#include "Trace/AssetPackageFieldDecoder.h"
#include "Widgets/SAssetBatchResults.h"
#include "Widgets/SAssetFolderComparisonResults.h"
#include "Widgets/SAssetSerializationDiff.h"
#include "Widgets/SAssetSerializationInspector.h"

static const FName AssetSerializationInspectorTabName("Asset Serialization Inspector");
static const FName DiffTabName(TEXT("Asset Serialization Diff"));

static void NotifyFailure(const FText& Message)
{
	FNotificationInfo Info(Message);
	Info.ExpireDuration = 8.0f;

	if (const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Notification->SetCompletionState(SNotificationItem::CS_Fail);
	}
}

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
			Section.AddMenuEntry("RunProjectNoOpResaveTest", LOCTEXT("ProjectNoOpResaveTest", "Run No-op Resave Test on Project"),
				LOCTEXT("ProjectNoOpResaveTestTooltip",
					"Save every asset under /Game twice to temporary files, without changing them, and report which assets change when "
					"saved and whether they do so every time. The assets on disk are not modified."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"),
				FUIAction(FExecuteAction::CreateRaw(this, &FAssetSerializationInspectorModule::RunBatchResaveOnPaths, TArray<FString>{ TEXT("/Game") }, FString(TEXT("/Game")))));
			Section.AddMenuEntry("ShowBatchResults", LOCTEXT("ShowBatchResults", "Show Last No-op Resave Results"),
				LOCTEXT("ShowBatchResultsTooltip", "Reopen the window with the assets of the latest no-op resave test run on several assets."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"),
				FUIAction(
					FExecuteAction::CreateRaw(this, &FAssetSerializationInspectorModule::ShowBatchResultsWindow), FCanExecuteAction::CreateLambda([this]() { return LastBatchResult.IsValid(); })));
			Section.AddMenuEntry("ShowFolderComparison", LOCTEXT("ShowFolderComparison", "Show Last Folder Comparison"),
				LOCTEXT("ShowFolderComparisonTooltip", "Reopen the window with the files of the latest comparison of two asset folders."), FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"),
				FUIAction(FExecuteAction::CreateRaw(this, &FAssetSerializationInspectorModule::ShowFolderComparisonWindow),
					FCanExecuteAction::CreateLambda([this]() { return LastFolderComparison.IsValid(); })));
			Section.AddMenuEntry("CompareAssetFolders", LOCTEXT("CompareAssetFolders", "Compare Asset Folders..."),
				LOCTEXT("CompareAssetFoldersTooltip",
					"Compare the .uasset and .umap files of two folders on disk, for example a project before and after moving it to another engine "
					"version. Files are paired by relative path, and the report groups what changed and the engine versions involved."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Diff"), FUIAction(FExecuteAction::CreateRaw(this, &FAssetSerializationInspectorModule::CompareAssetFolders)));
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

			if (SelectedPackages.Num() == 1)
			{
				InSection.AddMenuEntry("CompareWithSourceControlRevision", LOCTEXT("CompareWithRevision", "Compare with Source Control Revision..."),
					LOCTEXT("CompareWithRevisionTooltip",
						"Compare the asset on disk with an earlier revision of it in source control (Perforce, Git or any other provider the editor is "
						"connected to), and explain what changed."),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "SourceControl.Actions.Diff"),
					FUIAction(FExecuteAction::CreateRaw(this, &FAssetSerializationInspectorModule::CompareWithSourceControlRevision, SelectedPackages[0]),
						FCanExecuteAction::CreateLambda([]() { return AssetSourceControlCompare::IsAvailable(); })));
			}

			InSection.AddMenuEntry("RunNoOpResaveTest", LOCTEXT("NoOpResaveTest", "Run No-op Resave Test"),
				LOCTEXT("NoOpResaveTestTooltip",
					"Save the selected assets twice to temporary files, without changing them, and report what the save alone changes "
					"and whether it does so every time. The assets on disk are not modified."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"), FUIAction(FExecuteAction::CreateLambda([this, SelectedPackages]() {
					if (SelectedPackages.Num() > 1)
					{
						RunBatchResaveTest(SelectedPackages, FString::Printf(TEXT("%d selected assets"), SelectedPackages.Num()));
					}
					else
					{
						RunNoOpResaveTests(SelectedPackages);
					}
				})));

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

	// Folder context menu
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.FolderContextMenu");

		FToolMenuSection& Section = Menu->FindOrAddSection("AssetSerializationInspector", LOCTEXT("AssetSerializationSection", "Asset Serialization"));

		Section.AddDynamicEntry("AssetSerializationFolderTests", FNewToolMenuSectionDelegate::CreateLambda([this](FToolMenuSection& InSection) {
			const UContentBrowserFolderContext* Context = InSection.FindContext<UContentBrowserFolderContext>();

			if (Context == nullptr || Context->SelectedPackagePaths.IsEmpty())
			{
				return;
			}

			const TArray<FString> PackagePaths = Context->SelectedPackagePaths;
			const FString Scope = PackagePaths.Num() == 1 ? PackagePaths[0] : FString::Printf(TEXT("%d folders"), PackagePaths.Num());

			InSection.AddMenuEntry("RunFolderNoOpResaveTest", LOCTEXT("FolderNoOpResaveTest", "Run No-op Resave Test on Folder"),
				LOCTEXT("FolderNoOpResaveTestTooltip",
					"Save every asset in the selected folders and their subfolders twice to temporary files, without changing them, and report "
					"which assets change when saved and whether they do so every time. The assets on disk are not modified."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"),
				FUIAction(FExecuteAction::CreateRaw(this, &FAssetSerializationInspectorModule::RunBatchResaveOnPaths, PackagePaths, Scope)));
		}));
	}
}

void FAssetSerializationInspectorModule::RunBatchResaveOnPaths(TArray<FString> PackagePaths, FString Scope)
{
	RunBatchResaveTest(AssetBatchResave::CollectPackages(PackagePaths, true), MoveTemp(Scope));
}

void FAssetSerializationInspectorModule::RunBatchResaveTest(TArray<FName> PackageNames, FString Scope)
{
	if (PackageNames.IsEmpty())
	{
		FNotificationInfo Info(FText::Format(LOCTEXT("NoAssetsToTest", "No assets to test in {0}."), FText::FromString(Scope)));
		Info.ExpireDuration = 5.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	// A long run on many assets is easy to start by accident from a folder menu.
	constexpr int32 ConfirmationThreshold = 20;

	if (PackageNames.Num() > ConfirmationThreshold
		&& FMessageDialog::Open(EAppMsgType::YesNo,
			   FText::Format(
				   LOCTEXT("ConfirmBatchResave", "This loads {0} assets and saves each one twice to temporary files. The assets on disk are not modified, but it can take a long time. Continue?"),
				   FText::AsNumber(PackageNames.Num())))
			!= EAppReturnType::Yes)
	{
		return;
	}

	FScopedSlowTask SlowTask(PackageNames.Num(), LOCTEXT("RunningBatchResaveTest", "Running no-op resave test..."));
	SlowTask.MakeDialog(true);

	FAssetBatchResaveResult Result = AssetBatchResave::Run(PackageNames, Scope, [&SlowTask](const int32 Index, const int32 Total, const FName PackageName) {
		if (SlowTask.ShouldCancel())
		{
			return false;
		}

		SlowTask.EnterProgressFrame(
			1.0f, FText::Format(LOCTEXT("BatchResaveProgress", "{0} of {1}: {2}"), FText::AsNumber(Index + 1), FText::AsNumber(Total), FText::FromString(FPackageName::GetShortName(PackageName))));
		return true;
	});

	LastBatchResult = MakeShared<FAssetBatchResaveResult>(MoveTemp(Result));
	ShowBatchResaveNotification(*LastBatchResult);
	ShowBatchResultsWindow();
}

void FAssetSerializationInspectorModule::ShowBatchResultsWindow()
{
	if (!LastBatchResult.IsValid())
	{
		return;
	}

	const TSharedRef<SAssetBatchResults> Results = SNew(SAssetBatchResults)
													   .Result(LastBatchResult)
													   .OnOpenDiff(FOnOpenBatchResaveDiff::CreateRaw(this, &FAssetSerializationInspectorModule::OpenBatchEntryDiff))
													   .OnSaveReport(FSimpleDelegate::CreateRaw(this, &FAssetSerializationInspectorModule::SaveBatchResaveReport));

	// A new run replaces the content of the window that is already open.
	if (const TSharedPtr<SWindow> Existing = BatchResultsWindow.Pin())
	{
		Existing->SetContent(Results);
		Existing->BringToFront();
		return;
	}

	const TSharedRef<SWindow> Window =
		SNew(SWindow).Title(LOCTEXT("BatchResultsTitle", "No-op Resave Results")).ClientSize(FVector2D(960.0f, 640.0f)).SupportsMinimize(true).SupportsMaximize(true)[Results];

	BatchResultsWindow = Window;
	FSlateApplication::Get().AddWindow(Window);
}

void FAssetSerializationInspectorModule::OpenBatchEntryDiff(const FName PackageName, const bool bSecondResave)
{
	// The batch run kept only condensed results, so the comparison is produced again for this one asset.
	FScopedSlowTask SlowTask(1.0f, LOCTEXT("RerunningNoOpResaveTest", "Testing the asset again..."));
	SlowTask.MakeDialog();
	SlowTask.EnterProgressFrame(1.0f, FText::FromString(FPackageName::GetShortName(PackageName)));

	UPackage* Package = LoadPackage(nullptr, *PackageName.ToString(), LOAD_None);
	const FNoOpResaveResult Result = AssetNoOpResaveTest::Run(Package);
	const TSharedPtr<FObservedAssetSave> Save = bSecondResave ? Result.SecondResave : Result.FirstResave;

	if (Result.bSucceeded && Save.IsValid() && Save->HasChanges())
	{
		OpenObservedSaveDiff(Save);
		return;
	}

	FNotificationInfo Info(Result.bSucceeded ? LOCTEXT("NoChangesThisTime", "Resaving the asset changed nothing this time, so there is no comparison to open.")
											 : FText::Format(LOCTEXT("RerunFailed", "The asset could not be tested again: {0}"), Result.Error));
	Info.ExpireDuration = 8.0f;

	if (const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Notification->SetCompletionState(SNotificationItem::CS_Fail);
	}
}

void FAssetSerializationInspectorModule::ShowBatchResaveNotification(const FAssetBatchResaveResult& Result)
{
	const FAssetBatchResaveSummary Summary = Result.Summarize();

	const FText Message = FText::Format(LOCTEXT("BatchResaveSummary", "No-op resave test of {0}{1}: {2} stable, {3} normalized on the first save, {4} unstable, {5} skipped, {6} failed"),
		FText::FromString(Result.Scope), Result.bCancelled ? LOCTEXT("BatchResaveCancelled", " (cancelled)") : FText::GetEmpty(), FText::AsNumber(Summary.Stable),
		FText::AsNumber(Summary.NormalizedOnFirstSave), FText::AsNumber(Summary.Unstable), FText::AsNumber(Summary.Skipped), FText::AsNumber(Summary.Failed));

	FNotificationInfo Info(Message);
	Info.ExpireDuration = 20.0f;
	Info.bFireAndForget = true;
	Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("ViewBatchResultsButton", "View Results"),
		LOCTEXT("ViewBatchResultsTooltip", "Browse the assets of this run, see what resaving changed in each and open its comparison."),
		FSimpleDelegate::CreateRaw(this, &FAssetSerializationInspectorModule::ShowBatchResultsWindow), SNotificationItem::CS_None));
	Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("SaveBatchReportButton", "Save Report..."),
		LOCTEXT("SaveBatchReportTooltip", "Save the per-asset results and the changes that recur across assets as a text or JSON report."),
		FSimpleDelegate::CreateRaw(this, &FAssetSerializationInspectorModule::SaveBatchResaveReport), SNotificationItem::CS_None));

	if (const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Notification->SetCompletionState(Summary.Unstable == 0 && Summary.Failed == 0 ? SNotificationItem::CS_Success : SNotificationItem::CS_None);
	}
}

void FAssetSerializationInspectorModule::SaveBatchResaveReport()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();

	if (!LastBatchResult.IsValid() || DesktopPlatform == nullptr)
	{
		return;
	}

	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

	TArray<FString> SelectedFiles;
	if (!DesktopPlatform->SaveFileDialog(ParentWindowHandle, LOCTEXT("SaveBatchReportDialogTitle", "Save No-op Resave Report").ToString(), FPaths::ProjectSavedDir(),
			AssetBatchReportWriter::MakeDefaultFilename(LastBatchResult->Scope, FDateTime::Now(), EAssetReportFormat::Text), TEXT("Text report (*.txt)|*.txt|JSON report (*.json)|*.json"),
			EFileDialogFlags::None, SelectedFiles)
		|| SelectedFiles.IsEmpty())
	{
		return;
	}

	// The format follows the extension, so make sure there is one.
	FString Filename = SelectedFiles[0];
	if (FPaths::GetExtension(Filename).IsEmpty())
	{
		Filename += TEXT(".txt");
	}

	FText Error;
	const bool bSaved = AssetBatchReportWriter::SaveToFile(*LastBatchResult, Filename, Error);

	FNotificationInfo Info(bSaved ? FText::Format(LOCTEXT("BatchReportSaved", "Report saved to {0}"), FText::FromString(Filename)) : Error);
	Info.ExpireDuration = 8.0f;

	if (const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Notification->SetCompletionState(bSaved ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
}

void FAssetSerializationInspectorModule::CompareAssetFolders()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();

	if (DesktopPlatform == nullptr)
	{
		return;
	}

	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

	FString OldFolder;
	if (!DesktopPlatform->OpenDirectoryDialog(
			ParentWindowHandle, LOCTEXT("PickOldFolder", "Select the folder with the older assets").ToString(), LastComparedFolder.IsEmpty() ? FPaths::ProjectDir() : LastComparedFolder, OldFolder))
	{
		return;
	}

	FString NewFolder;
	if (!DesktopPlatform->OpenDirectoryDialog(ParentWindowHandle, LOCTEXT("PickNewFolder", "Select the folder with the newer assets").ToString(), OldFolder, NewFolder))
	{
		return;
	}

	LastComparedFolder = NewFolder;

	if (FPaths::IsSamePath(OldFolder, NewFolder))
	{
		FNotificationInfo Info(LOCTEXT("SameFolder", "Choose two different folders to compare."));
		Info.ExpireDuration = 6.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	// The number of files is only known once the run has listed both folders, so progress is a fraction of a fixed amount of work.
	constexpr float TotalWork = 1000.0f;
	FScopedSlowTask SlowTask(TotalWork, LOCTEXT("ComparingFolders", "Comparing asset folders..."));
	SlowTask.MakeDialog(true);

	FAssetFolderComparisonResult Result = AssetFolderComparison::Run(OldFolder, NewFolder, [&SlowTask, TotalWork](const int32 Index, const int32 Total, const FString& RelativePath) {
		if (SlowTask.ShouldCancel())
		{
			return false;
		}

		SlowTask.EnterProgressFrame(TotalWork / static_cast<float>(FMath::Max(Total, 1)),
			FText::Format(LOCTEXT("CompareFoldersProgress", "{0} of {1}: {2}"), FText::AsNumber(Index + 1), FText::AsNumber(Total), FText::FromString(RelativePath)));
		return true;
	});

	LastFolderComparison = MakeShared<FAssetFolderComparisonResult>(MoveTemp(Result));
	ShowFolderComparisonNotification(*LastFolderComparison);
	ShowFolderComparisonWindow();
}

void FAssetSerializationInspectorModule::ShowFolderComparisonWindow()
{
	if (!LastFolderComparison.IsValid())
	{
		return;
	}

	const TSharedRef<SAssetFolderComparisonResults> Results = SNew(SAssetFolderComparisonResults)
																  .Result(LastFolderComparison)
																  .OnOpenPair(FOnOpenFolderComparisonPair::CreateRaw(this, &FAssetSerializationInspectorModule::OpenFolderComparisonPairDiff))
																  .OnSaveReport(FSimpleDelegate::CreateRaw(this, &FAssetSerializationInspectorModule::SaveFolderComparisonReport));

	// A new comparison replaces the content of the window that is already open.
	if (const TSharedPtr<SWindow> Existing = FolderComparisonWindow.Pin())
	{
		Existing->SetContent(Results);
		Existing->BringToFront();
		return;
	}

	const TSharedRef<SWindow> Window =
		SNew(SWindow).Title(LOCTEXT("FolderComparisonTitle", "Folder Comparison")).ClientSize(FVector2D(960.0f, 640.0f)).SupportsMinimize(true).SupportsMaximize(true)[Results];

	FolderComparisonWindow = Window;
	FSlateApplication::Get().AddWindow(Window);
}

void FAssetSerializationInspectorModule::OpenFolderComparisonPairDiff(const FString& RelativePath)
{
	if (!LastFolderComparison.IsValid())
	{
		return;
	}

	// The comparison kept only condensed results, so both files are read again for the full comparison.
	FScopedSlowTask SlowTask(1.0f, LOCTEXT("ReadingPair", "Reading the files..."));
	SlowTask.MakeDialog();
	SlowTask.EnterProgressFrame(1.0f, FText::FromString(RelativePath));

	FText Error;
	const TSharedPtr<FAssetSerializationDiffSession> Session = FAssetSerializationDiffSession::FromFiles(
		FPaths::Combine(LastFolderComparison->OldFolder, RelativePath), FPaths::Combine(LastFolderComparison->NewFolder, RelativePath), FName(*FPaths::GetBaseFilename(RelativePath)), Error);

	if (!Session.IsValid())
	{
		NotifyFailure(FText::Format(LOCTEXT("PairNotReadable", "The files could not be read again: {0}"), Error));
		return;
	}

	ShowDiffSession(Session);
}

void FAssetSerializationInspectorModule::CompareWithSourceControlRevision(const FName PackageName)
{
	FString Filename;
	if (!FPackageName::DoesPackageExist(PackageName.ToString(), &Filename))
	{
		NotifyFailure(FText::Format(LOCTEXT("RevisionNoFile", "{0} has no file on disk to compare."), FText::FromName(PackageName)));
		return;
	}

	Filename = FPaths::ConvertRelativePathToFull(Filename);

	TArray<AssetSourceControlCompare::FRevision> Revisions;
	FString Error;
	{
		FScopedSlowTask SlowTask(1.0f, LOCTEXT("ReadingHistory", "Asking source control for the history..."));
		SlowTask.MakeDialog();
		SlowTask.EnterProgressFrame(1.0f);

		if (!AssetSourceControlCompare::FetchHistory(Filename, Revisions, Error))
		{
			NotifyFailure(FText::FromString(Error));
			return;
		}
	}

	// A menu at the cursor lists the revisions, newest first. The asset on disk is compared with the one picked.
	constexpr int32 MaximumRevisions = 25;
	const TArray<FAssetRevisionInfo> Infos = AssetSourceControlCompare::DescribeAll(Revisions);

	FMenuBuilder Menu(true, nullptr);
	Menu.BeginSection("Revisions", FText::Format(LOCTEXT("RevisionsHeading", "Compare {0} on disk with revision..."), FText::FromString(FPackageName::GetShortName(PackageName))));
	for (const FAssetRevisionInfo& Info : Infos)
	{
		if (Info.Index >= MaximumRevisions)
		{
			break;
		}

		const FText Label = FText::FromString(Info.Index == 0 ? Info.ToLabel() + TEXT("  (latest)") : Info.ToLabel());
		Menu.AddMenuEntry(
			Label, FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FAssetSerializationInspectorModule::OpenRevisionDiff, PackageName, Filename, Revisions[Info.Index])));
	}
	Menu.EndSection();

	FSlateApplication::Get().PushMenu(FSlateApplication::Get().GetActiveTopLevelWindow().ToSharedRef(), FWidgetPath(), Menu.MakeWidget(), FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
}

void FAssetSerializationInspectorModule::OpenRevisionDiff(const FName PackageName, const FString Filename, const TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> Revision)
{
	if (!Revision.IsValid())
	{
		return;
	}

	FScopedSlowTask SlowTask(2.0f, LOCTEXT("ReadingRevision", "Getting the revision..."));
	SlowTask.MakeDialog();

	FString RevisionFile;
	FString Error;
	SlowTask.EnterProgressFrame(1.0f, FText::FromString(Revision->GetRevision()));
	if (!AssetSourceControlCompare::DownloadRevision(*Revision, Filename, RevisionFile, Error))
	{
		NotifyFailure(FText::FromString(Error));
		return;
	}

	SlowTask.EnterProgressFrame(1.0f, LOCTEXT("ComparingRevision", "Comparing..."));
	FText ReadError;
	const TSharedPtr<FAssetSerializationDiffSession> Session = FAssetSerializationDiffSession::FromFiles(RevisionFile, Filename, PackageName, ReadError);
	if (!Session.IsValid())
	{
		NotifyFailure(FText::Format(LOCTEXT("RevisionNotReadable", "The revision could not be read: {0}"), ReadError));
		return;
	}

	ShowDiffSession(Session);
}

void FAssetSerializationInspectorModule::ShowFolderComparisonNotification(const FAssetFolderComparisonResult& Result)
{
	const FAssetFolderComparisonSummary Summary = Result.Summarize();

	const FText Message = FText::Format(
		LOCTEXT("FolderComparisonSummary", "Folder comparison{0}: {1} identical, {2} changed ({3} saved by different versions), {4} only in the old folder, {5} only in the new folder, {6} failed"),
		Result.bCancelled ? LOCTEXT("FolderComparisonCancelled", " (cancelled)") : FText::GetEmpty(), FText::AsNumber(Summary.Identical), FText::AsNumber(Summary.Changed),
		FText::AsNumber(Summary.ChangedWithDifferentVersions), FText::AsNumber(Summary.OnlyInOldFolder), FText::AsNumber(Summary.OnlyInNewFolder), FText::AsNumber(Summary.Failed));

	FNotificationInfo Info(Message);
	Info.ExpireDuration = 20.0f;
	Info.bFireAndForget = true;
	Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("ViewFolderComparisonButton", "View Results"),
		LOCTEXT("ViewFolderComparisonTooltip", "Browse the files of this comparison, see what differs in each and open the comparison of a changed pair."),
		FSimpleDelegate::CreateRaw(this, &FAssetSerializationInspectorModule::ShowFolderComparisonWindow), SNotificationItem::CS_None));
	Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("SaveFolderComparisonButton", "Save Report..."),
		LOCTEXT("SaveFolderComparisonTooltip", "Save what changed per file, the engine versions involved and the changes found in several files as a text or JSON report."),
		FSimpleDelegate::CreateRaw(this, &FAssetSerializationInspectorModule::SaveFolderComparisonReport), SNotificationItem::CS_None));

	if (const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Notification->SetCompletionState(
			Summary.Changed == 0 && Summary.Failed == 0 && Summary.OnlyInOldFolder == 0 && Summary.OnlyInNewFolder == 0 ? SNotificationItem::CS_Success : SNotificationItem::CS_None);
	}
}

void FAssetSerializationInspectorModule::SaveFolderComparisonReport()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();

	if (!LastFolderComparison.IsValid() || DesktopPlatform == nullptr)
	{
		return;
	}

	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

	TArray<FString> SelectedFiles;
	if (!DesktopPlatform->SaveFileDialog(ParentWindowHandle, LOCTEXT("SaveFolderComparisonDialogTitle", "Save Folder Comparison Report").ToString(), FPaths::ProjectSavedDir(),
			AssetFolderComparisonReportWriter::MakeDefaultFilename(LastFolderComparison->OldFolder, LastFolderComparison->NewFolder, FDateTime::Now(), EAssetReportFormat::Text),
			TEXT("Text report (*.txt)|*.txt|JSON report (*.json)|*.json"), EFileDialogFlags::None, SelectedFiles)
		|| SelectedFiles.IsEmpty())
	{
		return;
	}

	// The format follows the extension, so make sure there is one.
	FString Filename = SelectedFiles[0];
	if (FPaths::GetExtension(Filename).IsEmpty())
	{
		Filename += TEXT(".txt");
	}

	FText Error;
	const bool bSaved = AssetFolderComparisonReportWriter::SaveToFile(*LastFolderComparison, Filename, Error);

	FNotificationInfo Info(bSaved ? FText::Format(LOCTEXT("FolderComparisonReportSaved", "Report saved to {0}"), FText::FromString(Filename)) : Error);
	Info.ExpireDuration = 8.0f;

	if (const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Notification->SetCompletionState(bSaved ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
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

void FAssetSerializationInspectorModule::RunNoOpResaveTests(TArray<FName> PackageNames)
{
	FScopedSlowTask SlowTask(PackageNames.Num(), LOCTEXT("RunningNoOpResaveTest", "Running no-op resave test..."));
	SlowTask.MakeDialog();

	for (const FName PackageName : PackageNames)
	{
		SlowTask.EnterProgressFrame(1.0f, FText::FromString(FPackageName::GetShortName(PackageName)));

		UPackage* Package = LoadPackage(nullptr, *PackageName.ToString(), LOAD_None);
		ShowNoOpResaveNotification(AssetNoOpResaveTest::Run(Package));
	}
}

void FAssetSerializationInspectorModule::ShowNoOpResaveNotification(const FNoOpResaveResult& Result)
{
	const FText AssetName = FText::FromString(FPackageName::GetShortName(Result.PackageName));

	FText Message;
	SNotificationItem::ECompletionState State = SNotificationItem::CS_None;

	if (!Result.bSucceeded)
	{
		Message = FText::Format(LOCTEXT("NoOpResaveFailed", "No-op resave test of {0} failed: {1}"), AssetName, Result.Error);
		State = SNotificationItem::CS_Fail;
	}
	else if (Result.bOriginalFileModified)
	{
		Message = FText::Format(LOCTEXT("NoOpResaveModifiedOriginal", "No-op resave test of {0}: the original file changed during the test, so the result cannot be trusted"), AssetName);
		State = SNotificationItem::CS_Fail;
	}
	else
	{
		switch (Result.Verdict)
		{
			case ENoOpResaveVerdict::Stable:
				Message = FText::Format(LOCTEXT("NoOpResaveStable", "{0}: resaving without changes leaves the file byte-identical"), AssetName);
				State = SNotificationItem::CS_Success;
				break;

			case ENoOpResaveVerdict::NormalizedOnFirstSave:
				Message = FText::Format(LOCTEXT("NoOpResaveNormalized", "{0}: the first resave changes the file, later resaves change nothing more"), AssetName);
				break;

			case ENoOpResaveVerdict::Unstable:
				Message = FText::Format(LOCTEXT("NoOpResaveUnstable", "{0}: resaving without changes keeps changing the file"), AssetName);
				break;
		}
	}

	FNotificationInfo Info(Message);
	Info.ExpireDuration = 10.0f;
	Info.bFireAndForget = true;

	if (Result.bSucceeded)
	{
		if (Result.FirstResave.IsValid() && Result.FirstResave->HasChanges())
		{
			Info.ButtonDetails.Add(
				FNotificationButtonInfo(LOCTEXT("ViewFirstResaveButton", "View First Resave"), LOCTEXT("ViewFirstResaveTooltip", "Open what the first resave changed compared with the file on disk."),
					FSimpleDelegate::CreateRaw(this, &FAssetSerializationInspectorModule::OpenObservedSaveDiff, Result.FirstResave), SNotificationItem::CS_None));
		}

		if (Result.SecondResave.IsValid() && Result.SecondResave->HasChanges())
		{
			Info.ButtonDetails.Add(
				FNotificationButtonInfo(LOCTEXT("ViewSecondResaveButton", "View Second Resave"), LOCTEXT("ViewSecondResaveTooltip", "Open what a second resave changed compared with the first one."),
					FSimpleDelegate::CreateRaw(this, &FAssetSerializationInspectorModule::OpenObservedSaveDiff, Result.SecondResave), SNotificationItem::CS_None));
		}
	}

	if (const TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Notification->SetCompletionState(State);
	}
}

void FAssetSerializationInspectorModule::OpenObservedSaveDiff(TSharedPtr<FObservedAssetSave> Save)
{
	if (!Save.IsValid())
	{
		return;
	}

	ShowDiffSession(FAssetSerializationDiffSession::FromObservedSave(*Save));
}

void FAssetSerializationInspectorModule::ShowDiffSession(TSharedPtr<FAssetSerializationDiffSession> Session)
{
	PendingDiffSession = MoveTemp(Session);

	TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->TryInvokeTab(DiffTabName);

	if (TSharedPtr<SAssetSerializationDiff> Widget = ActiveDiffWidget.Pin())
	{
		Widget->SetSession(PendingDiffSession);

		PendingDiffSession.Reset();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAssetSerializationInspectorModule, AssetSerializationInspector)