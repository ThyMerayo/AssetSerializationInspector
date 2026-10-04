// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Widgets/SAssetBatchResults.h"

#include "Misc/PackageName.h"
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

#define LOCTEXT_NAMESPACE "AssetBatchResults"

namespace
{
	const FName BatchResultsAssetColumn(TEXT("Asset"));
	const FName BatchResultsOutcomeColumn(TEXT("Outcome"));
	const FName BatchResultsChangedColumn(TEXT("Changed"));
	const FName BatchResultsNoteColumn(TEXT("Note"));
} // namespace

class SAssetBatchResultRow : public SMultiColumnTableRow<TSharedPtr<FAssetBatchResultItem>>
{
public:
	SLATE_BEGIN_ARGS(SAssetBatchResultRow) {}
	SLATE_ARGUMENT(TSharedPtr<FAssetBatchResultItem>, Item)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
	{
		Item = InArgs._Item;
		SMultiColumnTableRow<TSharedPtr<FAssetBatchResultItem>>::Construct(FSuperRowType::FArguments(), OwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		const FAssetBatchResaveEntry& Entry = Item->Entry;

		if (ColumnName == BatchResultsAssetColumn)
		{
			return SNew(STextBlock).Text(FText::FromString(Item->DisplayName)).ToolTipText(FText::FromName(Entry.PackageName));
		}

		if (ColumnName == BatchResultsOutcomeColumn)
		{
			return SNew(STextBlock).Text(SAssetBatchResults::GetOutcomeText(Entry));
		}

		if (ColumnName == BatchResultsChangedColumn)
		{
			if (Entry.Status != EAssetBatchResaveStatus::Tested)
			{
				return SNew(STextBlock).Text(FText::GetEmpty());
			}

			return SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("ChangedBytesCell", "{0} bytes, {1} changes"), FText::AsNumber(Entry.FirstResaveChangedBytes),
					FText::AsNumber(Entry.FirstResaveChanges.Num() + Entry.FirstResaveChangesOmitted)));
		}

		return SNew(STextBlock).Text(FText::FromString(Entry.Message)).ToolTipText(FText::FromString(Entry.Message));
	}

private:
	TSharedPtr<FAssetBatchResultItem> Item;
};

SAssetBatchResults::EOutcome SAssetBatchResults::GetOutcome(const FAssetBatchResaveEntry& Entry)
{
	switch (Entry.Status)
	{
		case EAssetBatchResaveStatus::Skipped:
			return EOutcome::Skipped;

		case EAssetBatchResaveStatus::Failed:
			return EOutcome::Failed;

		case EAssetBatchResaveStatus::Tested:
			break;
	}

	switch (Entry.Verdict)
	{
		case ENoOpResaveVerdict::Unstable:
			return EOutcome::Unstable;

		case ENoOpResaveVerdict::NormalizedOnFirstSave:
			return EOutcome::Normalized;

		case ENoOpResaveVerdict::Stable:
			break;
	}

	return EOutcome::Stable;
}

FText SAssetBatchResults::GetOutcomeText(const FAssetBatchResaveEntry& Entry)
{
	switch (GetOutcome(Entry))
	{
		case EOutcome::Unstable:
			return LOCTEXT("OutcomeUnstable", "Unstable");

		case EOutcome::Normalized:
			return LOCTEXT("OutcomeNormalized", "Normalized on first save");

		case EOutcome::Failed:
			return LOCTEXT("OutcomeFailed", "Failed");

		case EOutcome::Skipped:
			return LOCTEXT("OutcomeSkipped", "Skipped");

		default:
			return LOCTEXT("OutcomeStable", "Stable");
	}
}

FString SAssetBatchResults::BuildDetailsText(const FAssetBatchResaveEntry& Entry)
{
	TArray<FString> Lines;
	Lines.Add(Entry.PackageName.ToString());
	Lines.Add(FString::Printf(TEXT("Outcome: %s"), *GetOutcomeText(Entry).ToString()));

	if (!Entry.Message.IsEmpty())
	{
		Lines.Add(Entry.Message);
	}

	const auto AddChanges = [&Lines](const TCHAR* Heading, const int64 ChangedBytes, const TArray<FAssetBatchResaveChange>& Changes, const int32 Omitted) {
		Lines.Add(FString());
		Lines.Add(FString::Printf(TEXT("%s: %lld changed bytes, %d changes"), Heading, ChangedBytes, Changes.Num() + Omitted));

		for (const FAssetBatchResaveChange& Change : Changes)
		{
			Lines.Add(FString::Printf(TEXT("  [%s] %s: %s"), *Change.Category, *Change.Name, *Change.Detail));
		}

		if (Omitted > 0)
		{
			Lines.Add(FString::Printf(TEXT("  ... and %d more"), Omitted));
		}
	};

	if (Entry.Status == EAssetBatchResaveStatus::Tested)
	{
		AddChanges(TEXT("First resave (compared with the file on disk)"), Entry.FirstResaveChangedBytes, Entry.FirstResaveChanges, Entry.FirstResaveChangesOmitted);
		AddChanges(TEXT("Second resave (compared with the first)"), Entry.SecondResaveChangedBytes, Entry.SecondResaveChanges, Entry.SecondResaveChangesOmitted);
	}

	return FString::Join(Lines, TEXT("\n"));
}

void SAssetBatchResults::Construct(const FArguments& InArgs)
{
	Result = InArgs._Result;
	OnOpenDiff = InArgs._OnOpenDiff;
	OnSaveReport = InArgs._OnSaveReport;

	for (bool& bShow : bShowOutcome)
	{
		bShow = true;
	}

	if (Result.IsValid())
	{
		for (const FAssetBatchResaveEntry& Entry : Result->Entries)
		{
			TSharedPtr<FAssetBatchResultItem> Item = MakeShared<FAssetBatchResultItem>();
			Item->Entry = Entry;
			Item->DisplayName = FPackageName::GetShortName(Entry.PackageName);
			AllItems.Add(MoveTemp(Item));
		}

		// Most worrying first; assets of the same outcome keep their order.
		AllItems.StableSort([](const TSharedPtr<FAssetBatchResultItem>& A, const TSharedPtr<FAssetBatchResultItem>& B) { return GetOutcome(A->Entry) < GetOutcome(B->Entry); });
	}

	RebuildVisibleItems();

	// The details can be long, so the text scrolls both ways: the lines are not wrapped.
	const TSharedRef<SScrollBar> DetailsVerticalScrollBar = SNew(SScrollBar).Orientation(Orient_Vertical);
	const TSharedRef<SScrollBar> DetailsHorizontalScrollBar = SNew(SScrollBar).Orientation(Orient_Horizontal);

	TSharedRef<SHorizontalBox> Toggles = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < static_cast<int32>(EOutcome::Count); ++Index)
	{
		Toggles->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)[BuildOutcomeToggle(static_cast<EOutcome>(Index))];
	}

	ChildSlot[SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 8.0f, 8.0f, 4.0f)[SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("ResultsHeading", "No-op resave test of {0}{1}"), FText::FromString(Result.IsValid() ? Result->Scope : FString()),
					Result.IsValid() && Result->bCancelled ? LOCTEXT("ResultsCancelled", " (cancelled)") : FText::GetEmpty()))
				.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))]

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f)[SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)[Toggles]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(
				VAlign_Center)[SNew(SBox).WidthOverride(220.0f)[SNew(SSearchBox).HintText(LOCTEXT("SearchAssets", "Search assets")).OnTextChanged(this, &SAssetBatchResults::HandleSearchChanged)]]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)[SNew(SBox).WidthOverride(90.0f)[SNew(STextBlock).Text(this, &SAssetBatchResults::GetCountText).ColorAndOpacity(FSlateColor::UseSubduedForeground())]]]

		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8.0f)[SNew(SSplitter).Orientation(Orient_Vertical)

			+ SSplitter::Slot().Value(0.6f)[SNew(SBorder).Padding(0.0f)[SAssignNew(ListView, SListView<TSharedPtr<FAssetBatchResultItem>>)
					.ListItemsSource(&VisibleItems)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SAssetBatchResults::GenerateRow)
					.OnSelectionChanged(this, &SAssetBatchResults::HandleSelectionChanged)
					.HeaderRow(SNew(SHeaderRow) + SHeaderRow::Column(BatchResultsAssetColumn).DefaultLabel(LOCTEXT("AssetColumn", "Asset")).FillWidth(0.35f)
						+ SHeaderRow::Column(BatchResultsOutcomeColumn).DefaultLabel(LOCTEXT("OutcomeColumn", "Result")).FillWidth(0.2f)
						+ SHeaderRow::Column(BatchResultsChangedColumn).DefaultLabel(LOCTEXT("ChangedColumn", "First resave")).FillWidth(0.2f)
						+ SHeaderRow::Column(BatchResultsNoteColumn).DefaultLabel(LOCTEXT("NoteColumn", "Note")).FillWidth(0.25f))]]

			+ SSplitter::Slot().Value(0.4f)[SNew(SBorder).Padding(4.0f)[SNew(SGridPanel).FillColumn(0, 1.0f).FillRow(0, 1.0f)
				+ SGridPanel::Slot(0, 0)[SAssignNew(DetailsText, SMultiLineEditableText)
						.IsReadOnly(true)
						.AutoWrapText(false)
						.VScrollBar(DetailsVerticalScrollBar)
						.HScrollBar(DetailsHorizontalScrollBar)
						.Text(LOCTEXT("SelectAnAsset", "Select an asset to see what resaving it changed."))]
				+ SGridPanel::Slot(1, 0)[DetailsVerticalScrollBar] + SGridPanel::Slot(0, 1)[DetailsHorizontalScrollBar]]]]

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 8.0f)[SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)[SNew(SButton)
					.Text(LOCTEXT("OpenFirstResave", "Open First Resave"))
					.ToolTipText(LOCTEXT("OpenFirstResaveTooltip", "Compare the file on disk with what the first resave writes. The asset is tested again to produce the comparison."))
					.IsEnabled_Lambda([this]() { return CanOpenSelected(false); })
					.OnClicked_Lambda([this]() { return OpenSelected(false); })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)[SNew(SButton)
					.Text(LOCTEXT("OpenSecondResave", "Open Second Resave"))
					.ToolTipText(
						LOCTEXT("OpenSecondResaveTooltip", "Compare the first resave with a second one, which shows what keeps changing. The asset is tested again to produce the comparison."))
					.IsEnabled_Lambda([this]() { return CanOpenSelected(true); })
					.OnClicked_Lambda([this]() { return OpenSelected(true); })]
			+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SSpacer)] + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("SaveReport", "Save Report...")).OnClicked_Lambda([this]() {
				  OnSaveReport.ExecuteIfBound();
				  return FReply::Handled();
			  })]]];
}

TSharedRef<SWidget> SAssetBatchResults::BuildOutcomeToggle(const EOutcome Outcome)
{
	const int32 Index = static_cast<int32>(Outcome);

	int32 Count = 0;
	for (const TSharedPtr<FAssetBatchResultItem>& Item : AllItems)
	{
		Count += GetOutcome(Item->Entry) == Outcome ? 1 : 0;
	}

	FAssetBatchResaveEntry Sample;
	switch (Outcome)
	{
		case EOutcome::Unstable:
			Sample.Status = EAssetBatchResaveStatus::Tested;
			Sample.Verdict = ENoOpResaveVerdict::Unstable;
			break;

		case EOutcome::Normalized:
			Sample.Status = EAssetBatchResaveStatus::Tested;
			Sample.Verdict = ENoOpResaveVerdict::NormalizedOnFirstSave;
			break;

		case EOutcome::Failed:
			Sample.Status = EAssetBatchResaveStatus::Failed;
			break;

		case EOutcome::Skipped:
			Sample.Status = EAssetBatchResaveStatus::Skipped;
			break;

		default:
			Sample.Status = EAssetBatchResaveStatus::Tested;
			break;
	}

	return SNew(SCheckBox)
		.IsChecked_Lambda([this, Index]() { return bShowOutcome[Index] ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([this, Index](const ECheckBoxState State) {
			bShowOutcome[Index] = State == ECheckBoxState::Checked;
			RebuildVisibleItems();
		})[SNew(STextBlock).Text(FText::Format(LOCTEXT("OutcomeToggle", "{0} ({1})"), GetOutcomeText(Sample), FText::AsNumber(Count)))];
}

TSharedRef<ITableRow> SAssetBatchResults::GenerateRow(TSharedPtr<FAssetBatchResultItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SAssetBatchResultRow, OwnerTable).Item(Item);
}

void SAssetBatchResults::RebuildVisibleItems()
{
	VisibleItems.Reset();

	for (const TSharedPtr<FAssetBatchResultItem>& Item : AllItems)
	{
		if (!bShowOutcome[static_cast<int32>(GetOutcome(Item->Entry))])
		{
			continue;
		}

		if (!SearchText.IsEmpty() && !Item->Entry.PackageName.ToString().Contains(SearchText))
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

void SAssetBatchResults::HandleSelectionChanged(TSharedPtr<FAssetBatchResultItem> Item, ESelectInfo::Type SelectInfo)
{
	SelectedItem = Item;

	if (DetailsText.IsValid())
	{
		DetailsText->SetText(Item.IsValid() ? FText::FromString(BuildDetailsText(Item->Entry)) : LOCTEXT("SelectAnAssetAgain", "Select an asset to see what resaving it changed."));
	}
}

void SAssetBatchResults::HandleSearchChanged(const FText& Text)
{
	SearchText = Text.ToString();
	RebuildVisibleItems();
}

FText SAssetBatchResults::GetCountText() const
{
	return FText::Format(LOCTEXT("ShownCount", "{0} of {1}"), FText::AsNumber(VisibleItems.Num()), FText::AsNumber(AllItems.Num()));
}

bool SAssetBatchResults::CanOpenSelected(const bool bSecondResave) const
{
	if (!SelectedItem.IsValid() || SelectedItem->Entry.Status != EAssetBatchResaveStatus::Tested || !OnOpenDiff.IsBound())
	{
		return false;
	}

	return (bSecondResave ? SelectedItem->Entry.SecondResaveChangedBytes : SelectedItem->Entry.FirstResaveChangedBytes) > 0;
}

FReply SAssetBatchResults::OpenSelected(const bool bSecondResave) const
{
	if (CanOpenSelected(bSecondResave))
	{
		OnOpenDiff.Execute(SelectedItem->Entry.PackageName, bSecondResave);
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
