// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

#include "Save/AssetSaveAnalyzer.h"

class FAssetMonitoringManager;
class FAssetSaveHistoryManager;

template <typename ItemType> class SListView;

/** Opens the comparison of the latest recorded save of a monitored asset. */
DECLARE_DELEGATE_OneParam(FOnOpenMonitoredAssetLastSave, FName /*PackageName*/);

/** One monitored asset as the list shows it. */
struct FAssetMonitoredItem
{
	FName PackageName;

	/** The asset's name and the content path of its folder. */
	FString Name;
	FString Folder;

	/** How many saves were recorded in this editor session, and what the latest one did. */
	int32 SavesRecorded = 0;
	FDateTime LastSave;
	EAssetSaveResultKind LastResult = EAssetSaveResultKind::Identical;

	/** The package has no file on disk (never saved, or deleted outside the editor). */
	bool bFileMissing = false;
};

/**
 * Lists every monitored asset with what was recorded for it, and manages the set: monitor a whole folder, stop monitoring the
 * selected assets or all of them, and open the comparison of an asset's latest save.
 *
 * The list follows the monitoring manager, so it also updates when assets are monitored from the Content Browser or the editor
 * preferences are edited.
 */
class SAssetMonitoredAssets : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssetMonitoredAssets) {}
	/** The manager whose set is shown. The global one when not given. */
	SLATE_ARGUMENT(FAssetMonitoringManager*, Manager)
	SLATE_EVENT(FOnOpenMonitoredAssetLastSave, OnOpenLastSave)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SAssetMonitoredAssets() override;

	/** The assets the list shows now, sorted by package name and narrowed by the search text. */
	const TArray<TSharedPtr<FAssetMonitoredItem>>& GetVisibleItems() const { return VisibleItems; }

	/** The monitored assets as list items, sorted by package name. The recorded saves come from the save history. */
	static TArray<TSharedPtr<FAssetMonitoredItem>> BuildItems(const FAssetMonitoringManager& Manager, const FAssetSaveHistoryManager& History);

	/** Keeps the items whose package name contains the text (ignoring case). An empty text keeps them all. */
	static TArray<TSharedPtr<FAssetMonitoredItem>> Filter(const TArray<TSharedPtr<FAssetMonitoredItem>>& Items, const FString& SearchText);

	/** A short phrase for what a save did, such as "property changes" or "layout only". */
	static FText GetResultText(EAssetSaveResultKind Kind);

private:
	TSharedRef<class ITableRow> GenerateRow(TSharedPtr<FAssetMonitoredItem> Item, const TSharedRef<class STableViewBase>& OwnerTable);
	TSharedRef<SWidget> BuildFolderPicker();
	void Refresh();
	void HandleSearchChanged(const FText& Text);
	void HandleFolderPicked(const FString& Path);
	FReply HandleRemoveSelected();
	FReply HandleRemoveAll();
	FReply HandleOpenLastSave();
	bool CanOpenLastSave() const;
	FText GetCountText() const;

	FAssetMonitoringManager* Manager = nullptr;
	FOnOpenMonitoredAssetLastSave OnOpenLastSave;
	FDelegateHandle ChangedHandle;

	TArray<TSharedPtr<FAssetMonitoredItem>> AllItems;
	TArray<TSharedPtr<FAssetMonitoredItem>> VisibleItems;
	FString SearchText;

	TSharedPtr<SListView<TSharedPtr<FAssetMonitoredItem>>> ListView;
	TSharedPtr<class STextBlock> StatusText;
};
