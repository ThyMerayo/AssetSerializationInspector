// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

#include "Compare/AssetFolderComparison.h"

class SMultiLineEditableText;

template <typename ItemType> class SListView;

/** The relative path of the pair whose comparison to open. */
DECLARE_DELEGATE_OneParam(FOnOpenFolderComparisonPair, const FString& /*RelativePath*/);

/** One file of a folder comparison as the results list shows it. */
struct FAssetFolderComparisonItem
{
	FAssetFolderComparisonEntry Entry;
};

/**
 * Lists the files of a folder comparison with what happened to each, filters them, shows the details of the selected one and
 * opens the comparison of a changed pair.
 *
 * The comparison keeps only condensed results, so opening a pair reads both files again.
 */
class SAssetFolderComparisonResults : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssetFolderComparisonResults) {}
	SLATE_ARGUMENT(TSharedPtr<FAssetFolderComparisonResult>, Result)
	SLATE_EVENT(FOnOpenFolderComparisonPair, OnOpenPair)
	SLATE_EVENT(FSimpleDelegate, OnSaveReport)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The files the window shows now: failed first, then changed, new, removed and identical ones. */
	const TArray<TSharedPtr<FAssetFolderComparisonItem>>& GetVisibleItems() const { return VisibleItems; }

	/** Text for the details of a file: its status, versions, sizes and what changed. */
	static FString BuildDetailsText(const FAssetFolderComparisonEntry& Entry);

	static FText GetStatusText(const FAssetFolderComparisonEntry& Entry);

private:
	static int32 GetStatusRank(const FAssetFolderComparisonEntry& Entry);

	TSharedRef<class ITableRow> GenerateRow(TSharedPtr<FAssetFolderComparisonItem> Item, const TSharedRef<class STableViewBase>& OwnerTable);
	TSharedRef<SWidget> BuildStatusToggle(EAssetFolderComparisonStatus Status);
	void RebuildVisibleItems();
	void HandleSelectionChanged(TSharedPtr<FAssetFolderComparisonItem> Item, ESelectInfo::Type SelectInfo);
	void HandleSearchChanged(const FText& Text);
	FText GetCountText() const;
	bool CanOpenSelected() const;

	TSharedPtr<FAssetFolderComparisonResult> Result;
	FOnOpenFolderComparisonPair OnOpenPair;
	FSimpleDelegate OnSaveReport;

	TArray<TSharedPtr<FAssetFolderComparisonItem>> AllItems;
	TArray<TSharedPtr<FAssetFolderComparisonItem>> VisibleItems;
	TSharedPtr<FAssetFolderComparisonItem> SelectedItem;

	bool bShowStatus[5];
	FString SearchText;

	TSharedPtr<SListView<TSharedPtr<FAssetFolderComparisonItem>>> ListView;
	TSharedPtr<SMultiLineEditableText> DetailsText;
};
