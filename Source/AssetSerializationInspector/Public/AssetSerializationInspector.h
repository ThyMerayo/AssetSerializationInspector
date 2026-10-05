// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "Modules/ModuleManager.h"

class SAssetSerializationDiff;
struct FAssetSerializationDiffSession;
struct FAssetBatchResaveResult;
struct FAssetFolderComparisonResult;
struct FNoOpResaveResult;
struct FObservedAssetSave;

class FAssetSerializationInspectorModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** This function will be bound to Command (by default it will bring up plugin window) */
	void PluginButtonClicked();

private:
	void RegisterMenus();
	void FillWindowSubMenu(class UToolMenu* SubMenu);

	TSharedRef<class SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);
	TSharedRef<SDockTab> OnSpawnDiffTab(const FSpawnTabArgs& SpawnTabArgs);

	void HandleObservedAssetSave(TSharedPtr<FObservedAssetSave> Save);
	void ShowSaveDiffNotification(TSharedPtr<FObservedAssetSave> Save);
	void OpenObservedSaveDiff(TSharedPtr<FObservedAssetSave> Save);
	void RunNoOpResaveTests(TArray<FName> PackageNames);
	void ShowNoOpResaveNotification(const FNoOpResaveResult& Result);
	void RunBatchResaveTest(TArray<FName> PackageNames, FString Scope);
	void RunBatchResaveOnPaths(TArray<FString> PackagePaths, FString Scope);
	void ShowBatchResaveNotification(const FAssetBatchResaveResult& Result);
	void SaveBatchResaveReport();
	void ShowBatchResultsWindow();
	void ShowMonitoredAssetsWindow();
	void OpenMonitoredAssetLastSave(FName PackageName);
	void OpenBatchEntryDiff(FName PackageName, bool bSecondResave);
	void CompareAssetFolders();
	void ShowFolderComparisonNotification(const FAssetFolderComparisonResult& Result);
	void SaveFolderComparisonReport();
	void ShowFolderComparisonWindow();
	void OpenFolderComparisonPairDiff(const FString& RelativePath);
	void CompareWithSourceControlRevision(FName PackageName);
	void OpenRevisionDiff(FName PackageName, FString Filename, TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> Revision);
	void ShowDiffSession(TSharedPtr<FAssetSerializationDiffSession> Session);

private:
	TSharedPtr<class FUICommandList> PluginCommands;
	FDelegateHandle ObservedSaveHandle;

	TSharedPtr<FAssetSerializationDiffSession> PendingDiffSession;
	TWeakPtr<SAssetSerializationDiff> ActiveDiffWidget;

	/** The latest batch run, kept so its report can be saved from the notification. */
	TSharedPtr<FAssetBatchResaveResult> LastBatchResult;

	/** The window that lists the latest batch run's assets. */
	TWeakPtr<class SWindow> BatchResultsWindow;

	/** The window that lists the monitored assets. */
	TWeakPtr<class SWindow> MonitoredAssetsWindow;

	/** The latest folder comparison, kept so its report can be saved from the notification. */
	TSharedPtr<FAssetFolderComparisonResult> LastFolderComparison;

	/** The window that lists the latest folder comparison's files. */
	TWeakPtr<class SWindow> FolderComparisonWindow;
	FString LastComparedFolder;
};
