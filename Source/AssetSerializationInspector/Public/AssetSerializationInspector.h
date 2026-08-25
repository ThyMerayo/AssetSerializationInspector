// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "Modules/ModuleManager.h"

class SAssetSerializationDiff;
struct FAssetSerializationDiffSession;
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

	TSharedRef<class SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);
	TSharedRef<SDockTab> OnSpawnDiffTab(const FSpawnTabArgs& SpawnTabArgs);

	void HandleObservedAssetSave(TSharedPtr<FObservedAssetSave> Save);
	void ShowSaveDiffNotification(TSharedPtr<FObservedAssetSave> Save);
	void OpenObservedSaveDiff(TSharedPtr<FObservedAssetSave> Save);

private:
	TSharedPtr<class FUICommandList> PluginCommands;
	FDelegateHandle ObservedSaveHandle;

	TSharedPtr<FAssetSerializationDiffSession> PendingDiffSession;
	TWeakPtr<SAssetSerializationDiff> ActiveDiffWidget;
};
