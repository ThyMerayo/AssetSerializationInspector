// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

#include "Save/AssetBatchResave.h"

class SMultiLineEditableText;

template <typename ItemType> class SListView;

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
class SAssetBatchResults : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssetBatchResults) {}
	SLATE_ARGUMENT(TSharedPtr<FAssetBatchResaveResult>, Result)
	SLATE_EVENT(FOnOpenBatchResaveDiff, OnOpenDiff)
	SLATE_EVENT(FSimpleDelegate, OnSaveReport)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The results the window shows now, most worrying first (unstable, normalized, failed, skipped, stable). */
	const TArray<TSharedPtr<FAssetBatchResultItem>>& GetVisibleItems() const { return VisibleItems; }

	/** Text for the details of an entry: its status, message and the changes of each resave. */
	static FString BuildDetailsText(const FAssetBatchResaveEntry& Entry);

	/** A short word for the entry's outcome: Stable, Normalized, Unstable, Skipped or Failed. */
	static FText GetOutcomeText(const FAssetBatchResaveEntry& Entry);

private:
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
	void RebuildVisibleItems();
	void HandleSelectionChanged(TSharedPtr<FAssetBatchResultItem> Item, ESelectInfo::Type SelectInfo);
	void HandleSearchChanged(const FText& Text);
	FText GetCountText() const;
	FReply OpenSelected(bool bSecondResave) const;
	bool CanOpenSelected(bool bSecondResave) const;

	TSharedPtr<FAssetBatchResaveResult> Result;
	FOnOpenBatchResaveDiff OnOpenDiff;
	FSimpleDelegate OnSaveReport;

	TArray<TSharedPtr<FAssetBatchResultItem>> AllItems;
	TArray<TSharedPtr<FAssetBatchResultItem>> VisibleItems;
	TSharedPtr<FAssetBatchResultItem> SelectedItem;

	bool bShowOutcome[static_cast<int32>(EOutcome::Count)];
	FString SearchText;

	TSharedPtr<SListView<TSharedPtr<FAssetBatchResultItem>>> ListView;
	TSharedPtr<SMultiLineEditableText> DetailsText;
};
