// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Views/SHeaderRow.h"

#include "Save/AssetBatchResave.h"
#include "Widgets/TAssetResultsList.h"

/** The package and which resave (the first, or the second compared with the first) to open the comparison of. */
DECLARE_DELEGATE_TwoParams(FOnOpenBatchResaveDiff, FName /*PackageName*/, bool /*bSecondResave*/);

/** One asset of a batch run as the results list shows it. */
struct FAssetBatchResultItem
{
	FAssetBatchResaveEntry Entry;

	FString DisplayName;
};

/**
 * Lists the assets of a no-op resave batch run with their verdicts, filters them, shows what the resaves changed in the
 * selected one and opens its comparison.
 *
 * The run keeps only condensed results so that memory does not grow with the project, so a comparison is made again for the
 * asset when it is opened.
 */
class SAssetBatchResults : public TAssetResultsList<FAssetBatchResultItem>
{
public:
	SLATE_BEGIN_ARGS(SAssetBatchResults) {}
	SLATE_ARGUMENT(TSharedPtr<FAssetBatchResaveResult>, Result)
	SLATE_EVENT(FOnOpenBatchResaveDiff, OnOpenDiff)
	SLATE_EVENT(FSimpleDelegate, OnSaveReport)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Text for the details of an entry: its status, message and the changes of each resave. */
	static FString BuildDetailsText(const FAssetBatchResaveEntry& Entry);

	/** A short word for the entry's outcome: Stable, Normalized, Unstable, Skipped or Failed. */
	static FText GetOutcomeText(const FAssetBatchResaveEntry& Entry);

	/** The ids of the list's columns, for sorting. */
	static const FName AssetColumnId;
	static const FName OutcomeColumnId;
	static const FName ChangedColumnId;
	static const FName NoteColumnId;

	/**
	 * Sorts items by a column: assets by name, outcomes from the most worrying, results by the bytes the first resave changed, notes
	 * by text. Equal items keep their order. EColumnSortMode::None leaves the items as they are.
	 */
	static void SortItems(TArray<TSharedPtr<FAssetBatchResultItem>>& Items, FName ColumnId, EColumnSortMode::Type Mode);

	/** The details of several assets: how many, then each asset's details in turn. One asset gives its own details. */
	static FString BuildSelectionDetailsText(const TArray<TSharedPtr<FAssetBatchResultItem>>& Items);

	/** The package names of the items, one per line, for the clipboard. */
	static FString BuildNamesText(const TArray<TSharedPtr<FAssetBatchResultItem>>& Items);

private:
	// What the list of results asks of this window.
	virtual bool PassesFilters(const FAssetBatchResultItem& Item) const override;
	virtual FString GetSearchableText(const FAssetBatchResultItem& Item) const override;
	virtual void SortVisibleItems(TArray<TSharedPtr<FAssetBatchResultItem>>& Items, FName ColumnId, EColumnSortMode::Type Mode) const override;
	virtual FString BuildSelectionDetails(const TArray<TSharedPtr<FAssetBatchResultItem>>& Items) const override;
	virtual FString BuildSelectionNames(const TArray<TSharedPtr<FAssetBatchResultItem>>& Items) const override;
	virtual FText GetNothingSelectedText() const override;

	enum class EOutcome : uint8
	{
		Unstable,
		Normalized,
		Failed,
		Skipped,
		Stable,
		Count
	};

	static EOutcome GetOutcome(const FAssetBatchResaveEntry& Entry);

	TSharedRef<class ITableRow> GenerateRow(TSharedPtr<FAssetBatchResultItem> Item, const TSharedRef<class STableViewBase>& OwnerTable);
	TSharedRef<SWidget> BuildOutcomeToggle(EOutcome Outcome);
	FReply OpenSelected(bool bSecondResave) const;
	bool CanOpenSelected(bool bSecondResave) const;

	TSharedPtr<FAssetBatchResaveResult> Result;
	FOnOpenBatchResaveDiff OnOpenDiff;
	FSimpleDelegate OnSaveReport;

	bool bShowOutcome[static_cast<int32>(EOutcome::Count)];
};
