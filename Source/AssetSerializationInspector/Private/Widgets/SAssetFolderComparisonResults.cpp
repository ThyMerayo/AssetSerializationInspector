// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Widgets/SAssetFolderComparisonResults.h"

#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "AssetFolderComparisonResults"

namespace
{
	const FName FolderResultsFileColumn(TEXT("File"));
	const FName FolderResultsStatusColumn(TEXT("Status"));
	const FName FolderResultsVersionColumn(TEXT("Versions"));
	const FName FolderResultsSizeColumn(TEXT("Size"));

	FString DescribeFolderResultsSize(const int64 Size)
	{
		return Size == INDEX_NONE ? FString(TEXT("-")) : FString::Printf(TEXT("%lld"), Size);
	}
} // namespace

class SAssetFolderComparisonRow : public SMultiColumnTableRow<TSharedPtr<FAssetFolderComparisonItem>>
{
public:
	SLATE_BEGIN_ARGS(SAssetFolderComparisonRow) {}
	SLATE_ARGUMENT(TSharedPtr<FAssetFolderComparisonItem>, Item)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
	{
		Item = InArgs._Item;
		SMultiColumnTableRow<TSharedPtr<FAssetFolderComparisonItem>>::Construct(FSuperRowType::FArguments(), OwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		const FAssetFolderComparisonEntry& Entry = Item->Entry;

		if (ColumnName == FolderResultsFileColumn)
		{
			return SNew(STextBlock).Text(FText::FromString(Entry.RelativePath)).ToolTipText(FText::FromString(Entry.RelativePath));
		}

		if (ColumnName == FolderResultsStatusColumn)
		{
			return SNew(STextBlock).Text(SAssetFolderComparisonResults::GetStatusText(Entry)).ToolTipText(FText::FromString(Entry.Message));
		}

		if (ColumnName == FolderResultsVersionColumn)
		{
			const bool bBoth = !Entry.OldEngineVersion.IsEmpty() && !Entry.NewEngineVersion.IsEmpty();
			const FString Text = bBoth
				? (Entry.OldEngineVersion == Entry.NewEngineVersion ? Entry.NewEngineVersion : FString::Printf(TEXT("%s -> %s"), *Entry.OldEngineVersion, *Entry.NewEngineVersion))
				: (Entry.NewEngineVersion.IsEmpty() ? Entry.OldEngineVersion : Entry.NewEngineVersion);
			return SNew(STextBlock).Text(FText::FromString(Text));
		}

		return SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s -> %s"), *DescribeFolderResultsSize(Entry.OldFileSize), *DescribeFolderResultsSize(Entry.NewFileSize))));
	}

private:
	TSharedPtr<FAssetFolderComparisonItem> Item;
};

int32 SAssetFolderComparisonResults::GetStatusRank(const FAssetFolderComparisonEntry& Entry)
{
	switch (Entry.Status)
	{
		case EAssetFolderComparisonStatus::Failed:
			return 0;

		case EAssetFolderComparisonStatus::Changed:
			return 1;

		case EAssetFolderComparisonStatus::OnlyInNewFolder:
			return 2;

		case EAssetFolderComparisonStatus::OnlyInOldFolder:
			return 3;

		default:
			return 4;
	}
}

FText SAssetFolderComparisonResults::GetStatusText(const FAssetFolderComparisonEntry& Entry)
{
	switch (Entry.Status)
	{
		case EAssetFolderComparisonStatus::Failed:
			return LOCTEXT("StatusFailed", "Could not compare");

		case EAssetFolderComparisonStatus::Changed:
			return Entry.bVersionsDiffer ? LOCTEXT("StatusChangedVersions", "Changed (different versions)") : LOCTEXT("StatusChanged", "Changed");

		case EAssetFolderComparisonStatus::OnlyInNewFolder:
			return LOCTEXT("StatusOnlyNew", "Only in the new folder");

		case EAssetFolderComparisonStatus::OnlyInOldFolder:
			return LOCTEXT("StatusOnlyOld", "Only in the old folder");

		default:
			return LOCTEXT("StatusIdentical", "Identical");
	}
}

FString SAssetFolderComparisonResults::BuildDetailsText(const FAssetFolderComparisonEntry& Entry)
{
	TArray<FString> Lines;
	Lines.Add(Entry.RelativePath);
	Lines.Add(FString::Printf(TEXT("Status: %s"), *GetStatusText(Entry).ToString()));

	if (!Entry.Message.IsEmpty())
	{
		Lines.Add(Entry.Message);
	}

	if (!Entry.OldEngineVersion.IsEmpty() || !Entry.NewEngineVersion.IsEmpty())
	{
		Lines.Add(FString::Printf(
			TEXT("Saved by: %s -> %s"), Entry.OldEngineVersion.IsEmpty() ? TEXT("-") : *Entry.OldEngineVersion, Entry.NewEngineVersion.IsEmpty() ? TEXT("-") : *Entry.NewEngineVersion));
	}

	if (!Entry.OldFileVersion.IsEmpty() || !Entry.NewFileVersion.IsEmpty())
	{
		Lines.Add(
			FString::Printf(TEXT("Package version: %s -> %s"), Entry.OldFileVersion.IsEmpty() ? TEXT("-") : *Entry.OldFileVersion, Entry.NewFileVersion.IsEmpty() ? TEXT("-") : *Entry.NewFileVersion));
	}

	Lines.Add(FString::Printf(TEXT("File size: %s -> %s bytes"), *DescribeFolderResultsSize(Entry.OldFileSize), *DescribeFolderResultsSize(Entry.NewFileSize)));

	if (Entry.Status == EAssetFolderComparisonStatus::Changed)
	{
		Lines.Add(FString());
		Lines.Add(FString::Printf(TEXT("%d changes"), Entry.Changes.Num() + Entry.ChangesOmitted));

		for (const FAssetBatchResaveChange& Change : Entry.Changes)
		{
			Lines.Add(FString::Printf(TEXT("  [%s] %s: %s"), *Change.Category, *Change.Name, *Change.Detail));
		}

		if (Entry.ChangesOmitted > 0)
		{
			Lines.Add(FString::Printf(TEXT("  ... and %d more"), Entry.ChangesOmitted));
		}
	}

	return FString::Join(Lines, TEXT("\n"));
}

void SAssetFolderComparisonResults::Construct(const FArguments& InArgs)
{
	Result = InArgs._Result;
	OnOpenPair = InArgs._OnOpenPair;
	OnSaveReport = InArgs._OnSaveReport;

	for (bool& bShow : bShowStatus)
	{
		bShow = true;
	}

	if (Result.IsValid())
	{
		for (const FAssetFolderComparisonEntry& Entry : Result->Entries)
		{
			TSharedPtr<FAssetFolderComparisonItem> Item = MakeShared<FAssetFolderComparisonItem>();
			Item->Entry = Entry;
			AllItems.Add(MoveTemp(Item));
		}

		// Most interesting first; files of the same kind keep their order.
		AllItems.StableSort([](const TSharedPtr<FAssetFolderComparisonItem>& A, const TSharedPtr<FAssetFolderComparisonItem>& B) { return GetStatusRank(A->Entry) < GetStatusRank(B->Entry); });
	}

	RebuildVisibleItems();

	TSharedRef<SHorizontalBox> Toggles = SNew(SHorizontalBox);
	for (const EAssetFolderComparisonStatus Status : { EAssetFolderComparisonStatus::Failed, EAssetFolderComparisonStatus::Changed, EAssetFolderComparisonStatus::OnlyInNewFolder,
			 EAssetFolderComparisonStatus::OnlyInOldFolder, EAssetFolderComparisonStatus::Identical })
	{
		Toggles->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)[BuildStatusToggle(Status)];
	}

	// The details can be long, so the text scrolls both ways: the lines are not wrapped.
	const TSharedRef<SScrollBar> DetailsVerticalScrollBar = SNew(SScrollBar).Orientation(Orient_Vertical);
	const TSharedRef<SScrollBar> DetailsHorizontalScrollBar = SNew(SScrollBar).Orientation(Orient_Horizontal);

	ChildSlot[SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 8.0f, 8.0f, 0.0f)[SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("ResultsHeading", "Folder comparison{0}"), Result.IsValid() && Result->bCancelled ? LOCTEXT("ResultsCancelled", " (cancelled)") : FText::GetEmpty()))
				.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))]

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 2.0f, 8.0f, 4.0f)[SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("ResultsFolders", "{0}  ->  {1}"), FText::FromString(Result.IsValid() ? Result->OldFolder : FString()),
					FText::FromString(Result.IsValid() ? Result->NewFolder : FString())))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())]

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f)[SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)[Toggles]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(
				220.0f)[SNew(SSearchBox).HintText(LOCTEXT("SearchFiles", "Search files")).OnTextChanged(this, &SAssetFolderComparisonResults::HandleSearchChanged)]]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8.0f, 0.0f, 0.0f,
					0.0f)[SNew(SBox).WidthOverride(90.0f)[SNew(STextBlock).Text(this, &SAssetFolderComparisonResults::GetCountText).ColorAndOpacity(FSlateColor::UseSubduedForeground())]]]

		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8.0f)[SNew(SSplitter).Orientation(Orient_Vertical)

			+ SSplitter::Slot().Value(0.6f)[SNew(SBorder).Padding(0.0f)[SAssignNew(ListView, SListView<TSharedPtr<FAssetFolderComparisonItem>>)
					.ListItemsSource(&VisibleItems)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SAssetFolderComparisonResults::GenerateRow)
					.OnSelectionChanged(this, &SAssetFolderComparisonResults::HandleSelectionChanged)
					.HeaderRow(SNew(SHeaderRow) + SHeaderRow::Column(FolderResultsFileColumn).DefaultLabel(LOCTEXT("FileColumn", "File")).FillWidth(0.45f)
						+ SHeaderRow::Column(FolderResultsStatusColumn).DefaultLabel(LOCTEXT("StatusColumn", "Status")).FillWidth(0.2f)
						+ SHeaderRow::Column(FolderResultsVersionColumn).DefaultLabel(LOCTEXT("VersionColumn", "Saved by")).FillWidth(0.2f)
						+ SHeaderRow::Column(FolderResultsSizeColumn).DefaultLabel(LOCTEXT("SizeColumn", "Size (bytes)")).FillWidth(0.15f))]]

			+ SSplitter::Slot().Value(0.4f)[SNew(SBorder).Padding(4.0f)[SNew(SGridPanel).FillColumn(0, 1.0f).FillRow(0, 1.0f)
				+ SGridPanel::Slot(0, 0)[SAssignNew(DetailsText, SMultiLineEditableText)
						.IsReadOnly(true)
						.AutoWrapText(false)
						.VScrollBar(DetailsVerticalScrollBar)
						.HScrollBar(DetailsHorizontalScrollBar)
						.Text(LOCTEXT("SelectAFile", "Select a file to see what differs between the folders."))]
				+ SGridPanel::Slot(1, 0)[DetailsVerticalScrollBar] + SGridPanel::Slot(0, 1)[DetailsHorizontalScrollBar]]]]

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 8.0f)[SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)[SNew(SButton)
					.Text(LOCTEXT("OpenComparison", "Open Comparison"))
					.ToolTipText(LOCTEXT("OpenComparisonTooltip", "Compare the two versions of the selected file in the diff window. Both files are read again."))
					.IsEnabled_Lambda([this]() { return CanOpenSelected(); })
					.OnClicked_Lambda([this]() {
						if (CanOpenSelected())
						{
							OnOpenPair.Execute(SelectedItem->Entry.RelativePath);
						}
						return FReply::Handled();
					})]
			+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SSpacer)] + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("SaveReport", "Save Report...")).OnClicked_Lambda([this]() {
				  OnSaveReport.ExecuteIfBound();
				  return FReply::Handled();
			  })]]];
}

TSharedRef<SWidget> SAssetFolderComparisonResults::BuildStatusToggle(const EAssetFolderComparisonStatus Status)
{
	const int32 Index = static_cast<int32>(Status);

	int32 Count = 0;
	for (const TSharedPtr<FAssetFolderComparisonItem>& Item : AllItems)
	{
		Count += Item->Entry.Status == Status ? 1 : 0;
	}

	FAssetFolderComparisonEntry Sample;
	Sample.Status = Status;

	return SNew(SCheckBox)
		.IsChecked_Lambda([this, Index]() { return bShowStatus[Index] ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([this, Index](const ECheckBoxState State) {
			bShowStatus[Index] = State == ECheckBoxState::Checked;
			RebuildVisibleItems();
		})[
			// A changed file is described as plain "Changed" here: the version difference is a property of the row, not a filter.
			SNew(STextBlock).Text(FText::Format(LOCTEXT("StatusToggle", "{0} ({1})"), GetStatusText(Sample), FText::AsNumber(Count)))];
}

TSharedRef<ITableRow> SAssetFolderComparisonResults::GenerateRow(TSharedPtr<FAssetFolderComparisonItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SAssetFolderComparisonRow, OwnerTable).Item(Item);
}

void SAssetFolderComparisonResults::RebuildVisibleItems()
{
	VisibleItems.Reset();

	for (const TSharedPtr<FAssetFolderComparisonItem>& Item : AllItems)
	{
		if (!bShowStatus[static_cast<int32>(Item->Entry.Status)])
		{
			continue;
		}

		if (!SearchText.IsEmpty() && !Item->Entry.RelativePath.Contains(SearchText))
		{
			continue;
		}

		VisibleItems.Add(Item);
	}

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

void SAssetFolderComparisonResults::HandleSelectionChanged(TSharedPtr<FAssetFolderComparisonItem> Item, ESelectInfo::Type SelectInfo)
{
	SelectedItem = Item;

	if (DetailsText.IsValid())
	{
		DetailsText->SetText(Item.IsValid() ? FText::FromString(BuildDetailsText(Item->Entry)) : LOCTEXT("SelectAFileAgain", "Select a file to see what differs between the folders."));
	}
}

void SAssetFolderComparisonResults::HandleSearchChanged(const FText& Text)
{
	SearchText = Text.ToString();
	RebuildVisibleItems();
}

FText SAssetFolderComparisonResults::GetCountText() const
{
	return FText::Format(LOCTEXT("ShownCount", "{0} of {1}"), FText::AsNumber(VisibleItems.Num()), FText::AsNumber(AllItems.Num()));
}

bool SAssetFolderComparisonResults::CanOpenSelected() const
{
	return SelectedItem.IsValid() && SelectedItem->Entry.Status == EAssetFolderComparisonStatus::Changed && OnOpenPair.IsBound();
}

#undef LOCTEXT_NAMESPACE
