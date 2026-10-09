// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"

/**
 * What the windows that list the results of a run have in common: the items and the ones shown after the filters and the search, the
 * sort column, the selection (one item is the selected one for the buttons; the details cover all that are selected) and copying the
 * names. A window says which items pass its filters, what its items are called, how they are sorted and what their details are.
 */
template <typename TItem> class TAssetResultsList : public SCompoundWidget
{
public:
	/** The items the window shows now, filtered, searched and sorted. */
	const TArray<TSharedPtr<TItem>>& GetVisibleItems() const { return VisibleItems; }

	/** Sorts the list by a column, as a click on its header does. */
	void SortBy(const FName ColumnId, const EColumnSortMode::Type Mode)
	{
		SortColumn = ColumnId;
		SortMode = Mode;
		RebuildVisibleItems();
	}

	/** The details of several items: how many (with Noun, "assets"), then each item's details in turn. One item gives its own details. */
	static FString JoinSelectionDetails(const TArray<TSharedPtr<TItem>>& Items, const TCHAR* Noun, const TFunctionRef<FString(const TItem&)> Describe)
	{
		if (Items.Num() == 1)
		{
			return Describe(*Items[0]);
		}

		TArray<FString> Parts;
		Parts.Add(FString::Printf(TEXT("%d %s selected"), Items.Num(), Noun));

		for (const TSharedPtr<TItem>& Item : Items)
		{
			Parts.Add(Describe(*Item));
		}

		return FString::Join(Parts, TEXT("\n\n--------------------------------\n\n"));
	}

	/** The names of the items, one per line, for the clipboard. */
	static FString JoinNames(const TArray<TSharedPtr<TItem>>& Items, const TFunctionRef<FString(const TItem&)> NameOf)
	{
		TArray<FString> Names;
		for (const TSharedPtr<TItem>& Item : Items)
		{
			Names.Add(NameOf(*Item));
		}

		return FString::Join(Names, TEXT("\n"));
	}

protected:
	/** Whether the filters of the window (its toggles) let the item through. */
	virtual bool PassesFilters(const TItem& Item) const = 0;

	/** The text the search box looks for the search text in. */
	virtual FString GetSearchableText(const TItem& Item) const = 0;

	/** Sorts items by a column; EColumnSortMode::None leaves them as they are. */
	virtual void SortVisibleItems(TArray<TSharedPtr<TItem>>& Items, FName ColumnId, EColumnSortMode::Type Mode) const = 0;

	/** The details text of the selected items (one or several). */
	virtual FString BuildSelectionDetails(const TArray<TSharedPtr<TItem>>& Items) const = 0;

	/** The names of the selected items, one per line. */
	virtual FString BuildSelectionNames(const TArray<TSharedPtr<TItem>>& Items) const = 0;

	/** What the details say while nothing is selected. */
	virtual FText GetNothingSelectedText() const = 0;

	void RebuildVisibleItems()
	{
		VisibleItems.Reset();

		for (const TSharedPtr<TItem>& Item : AllItems)
		{
			if (!PassesFilters(*Item))
			{
				continue;
			}

			if (!SearchText.IsEmpty() && !GetSearchableText(*Item).Contains(SearchText))
			{
				continue;
			}

			VisibleItems.Add(Item);
		}

		SortVisibleItems(VisibleItems, SortColumn, SortMode);

		if (ListView.IsValid())
		{
			ListView->RequestListRefresh();
		}
	}

	void HandleSelectionChanged(TSharedPtr<TItem> Item, ESelectInfo::Type SelectInfo)
	{
		const TArray<TSharedPtr<TItem>> Selected = ListView.IsValid() ? ListView->GetSelectedItems() : TArray<TSharedPtr<TItem>>();

		// The comparison buttons need exactly one item; the details cover them all.
		SelectedItem = Selected.Num() == 1 ? Selected[0] : nullptr;

		if (DetailsText.IsValid())
		{
			DetailsText->SetText(Selected.IsEmpty() ? GetNothingSelectedText() : FText::FromString(BuildSelectionDetails(Selected)));
		}
	}

	void HandleSearchChanged(const FText& Text)
	{
		SearchText = Text.ToString();
		RebuildVisibleItems();
	}

	EColumnSortMode::Type GetSortMode(const FName ColumnId) const { return SortColumn == ColumnId ? SortMode : EColumnSortMode::None; }

	void HandleSort(EColumnSortPriority::Type Priority, const FName& ColumnId, EColumnSortMode::Type Mode) { SortBy(ColumnId, Mode); }

	FReply CopySelectedNames() const
	{
		if (ListView.IsValid())
		{
			FPlatformApplicationMisc::ClipboardCopy(*BuildSelectionNames(ListView->GetSelectedItems()));
		}

		return FReply::Handled();
	}

	/** "3 of 10": the items shown and all of them. */
	FText GetCountText() const { return FText::Format(NSLOCTEXT("AssetResultsList", "ShownCount", "{0} of {1}"), FText::AsNumber(VisibleItems.Num()), FText::AsNumber(AllItems.Num())); }

	TArray<TSharedPtr<TItem>> AllItems;
	TArray<TSharedPtr<TItem>> VisibleItems;
	TSharedPtr<TItem> SelectedItem;

	FString SearchText;
	FName SortColumn;
	EColumnSortMode::Type SortMode = EColumnSortMode::None;

	TSharedPtr<SListView<TSharedPtr<TItem>>> ListView;
	TSharedPtr<SMultiLineEditableText> DetailsText;
};
