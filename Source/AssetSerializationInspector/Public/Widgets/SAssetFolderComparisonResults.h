// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SHeaderRow.h"

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

	/** The ids of the list's columns, for sorting. */
	static const FName FileColumnId;
	static const FName StatusColumnId;
	static const FName VersionColumnId;
	static const FName SizeColumnId;

	/**
	 * Sorts items by a column: files by path, statuses from the most interesting, versions by text, sizes by the size of the new
	 * file (the old one when there is none). Equal items keep their order. EColumnSortMode::None leaves the items as they are.
	 */
	static void SortItems(TArray<TSharedPtr<FAssetFolderComparisonItem>>& Items, FName ColumnId, EColumnSortMode::Type Mode);

	/** Sorts the list by a column, as a click on its header does. */
	void SortBy(FName ColumnId, EColumnSortMode::Type Mode);

	/** The details of several files: how many, then each file's details in turn. One file gives its own details. */
	static FString BuildSelectionDetailsText(const TArray<TSharedPtr<FAssetFolderComparisonItem>>& Items);

	/** The relative paths of the items, one per line, for the clipboard. */
	static FString BuildNamesText(const TArray<TSharedPtr<FAssetFolderComparisonItem>>& Items);

private:
	static int32 GetStatusRank(const FAssetFolderComparisonEntry& Entry);

	TSharedRef<class ITableRow> GenerateRow(TSharedPtr<FAssetFolderComparisonItem> Item, const TSharedRef<class STableViewBase>& OwnerTable);
	TSharedRef<SWidget> BuildStatusToggle(EAssetFolderComparisonStatus Status);
	void RebuildVisibleItems();
	void HandleSelectionChanged(TSharedPtr<FAssetFolderComparisonItem> Item, ESelectInfo::Type SelectInfo);
	void HandleSearchChanged(const FText& Text);
	EColumnSortMode::Type GetSortMode(FName ColumnId) const;
	void HandleSort(EColumnSortPriority::Type Priority, const FName& ColumnId, EColumnSortMode::Type Mode);
	FReply CopySelectedNames() const;
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
	FName SortColumn;
	EColumnSortMode::Type SortMode = EColumnSortMode::None;

	TSharedPtr<SListView<TSharedPtr<FAssetFolderComparisonItem>>> ListView;
	TSharedPtr<SMultiLineEditableText> DetailsText;
};
