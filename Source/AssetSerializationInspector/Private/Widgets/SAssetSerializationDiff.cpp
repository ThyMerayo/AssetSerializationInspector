// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Widgets/SAssetSerializationDiff.h"

#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SExpanderArrow.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STreeView.h"

#include "Diff/AssetByteDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Report/AssetAnalysisReport.h"
#include "Report/AssetReportWriter.h"
#include "Save/AssetSaveAnalyzer.h"
#include "Save/AssetSaveHistoryManager.h"
#include "Save/AssetSaveObserver.h"
#include "Trace/AssetPackageFieldDecoder.h"
#include "Widgets/SSelectableRichText.h"

namespace
{
	/** The decoded value, followed by the final contents of a set or map when they could be reconstructed from its archetype. */
	FString MakeDecodedValueText(const FString& DecodedValue, const FString& FinalValue)
	{
		return FinalValue.IsEmpty() ? DecodedValue : FString::Printf(TEXT("%s  ->  %s"), *DecodedValue, *FinalValue);
	}
} // namespace

#define LOCTEXT_NAMESPACE "SAssetSerializationDiff"

namespace
{
	struct FDiffCounts
	{
		int32 Added = 0;
		int32 Removed = 0;
		int32 Modified = 0;
		int32 Moved = 0;
	};

	void AccumulateDiffCounts(const FAssetPackageDiffEntry& Entry, FDiffCounts& Counts)
	{
		switch (Entry.State)
		{
			case EAssetPackageDiffState::Added:
				++Counts.Added;
				break;

			case EAssetPackageDiffState::Removed:
				++Counts.Removed;
				break;

			case EAssetPackageDiffState::Modified:
				++Counts.Modified;
				break;

			case EAssetPackageDiffState::Moved:
				++Counts.Moved;
				break;

			default:
				break;
		}

		for (const FAssetPackageDiffEntry& Child : Entry.Children)
		{
			AccumulateDiffCounts(Child, Counts);
		}
	}

	SAssetSerializationDiff::FDiffTreeNodePtr FindFirstChangedNode(const SAssetSerializationDiff::FDiffTreeNodePtr& Node)
	{
		if (!Node.IsValid())
		{
			return nullptr;
		}

		if (Node->Diff.State != EAssetPackageDiffState::Unchanged && Node->Children.IsEmpty())
		{
			return Node;
		}

		for (const auto& Child : Node->Children)
		{
			if (auto Found = FindFirstChangedNode(Child))
			{
				return Found;
			}
		}

		if (Node->Diff.State != EAssetPackageDiffState::Unchanged)
		{
			return Node;
		}

		return nullptr;
	}
} // namespace

class SAssetPackageDiffTreeRow : public SMultiColumnTableRow<TSharedPtr<FAssetPackageDiffTreeNode>>
{
public:
	using FItemType = TSharedPtr<FAssetPackageDiffTreeNode>;

	SLATE_BEGIN_ARGS(SAssetPackageDiffTreeRow) {}
	SLATE_ARGUMENT(FItemType, Item)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
	{
		Item = InArgs._Item;

		SMultiColumnTableRow<FItemType>::Construct(FSuperRowType::FArguments(), OwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (!Item.IsValid())
		{
			return SNullWidget::NullWidget;
		}

		const FAssetPackageDiffEntry& Diff = Item->Diff;

		if (ColumnName == TEXT("Name"))
		{
			return SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SExpanderArrow, SharedThis(this)).IndentAmount(16.0f)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f, 6.0f, 0.0f)[SNew(STextBlock).Text(GetStateSymbol()).ToolTipText(GetStateText())]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(Diff.DisplayName).ToolTipText(Diff.Explanation)];
		}

		if (ColumnName == TEXT("Old") && Diff.Kind == EAssetPackageDiffKind::Property)
		{
			if (Diff.bHasOldDecodedValue)
			{
				return SNew(SEditableText)
					.Text(FText::FromString(MakeDecodedValueText(Diff.OldDecodedValue, Diff.OldFinalValue)))
					.ToolTipText(FText::FromString(Diff.OldFinalValueNote))
					.IsReadOnly(true);
			}

			return SNew(STextBlock).Text(FText::Format(LOCTEXT("ChangedBytesOld", "{0} changed bytes"), FText::AsNumber(Diff.ChangedByteCount)));
		}
		else if (ColumnName == TEXT("Old"))
		{
			return SNew(STextBlock).Text(FText::FromString(Diff.OldValue)).ToolTipText(FText::FromString(Diff.OldValue));
		}

		if (ColumnName == TEXT("New") && (Diff.Kind == EAssetPackageDiffKind::Property || Diff.Kind == EAssetPackageDiffKind::UnknownPayloadRange))
		{
			if (Diff.bHasNewDecodedValue)
			{
				return SNew(SEditableText)
					.Text(FText::FromString(MakeDecodedValueText(Diff.NewDecodedValue, Diff.NewFinalValue)))
					.ToolTipText(FText::FromString(Diff.NewFinalValueNote))
					.IsReadOnly(true);
			}

			return SNew(STextBlock).Text(FText::Format(LOCTEXT("ChangedBytesNew", "{0} changed bytes"), FText::AsNumber(Diff.ChangedByteCount)));
		}
		else if (ColumnName == TEXT("New"))
		{
			return SNew(STextBlock).Text(FText::FromString(Diff.NewValue)).ToolTipText(FText::FromString(Diff.NewValue));
		}

		if (ColumnName == TEXT("Type"))
		{
			return SNew(STextBlock).Text(FText::FromString(Diff.TypeName));
		}

		return SNullWidget::NullWidget;
	}

private:
	FText GetStateSymbol() const
	{
		if (!Item.IsValid())
		{
			return FText::GetEmpty();
		}

		switch (Item->Diff.State)
		{
			case EAssetPackageDiffState::Added:
				return FText::FromString(TEXT("+"));

			case EAssetPackageDiffState::Removed:
				return FText::FromString(TEXT("-"));

			case EAssetPackageDiffState::Modified:
				return FText::FromString(TEXT("~"));

			case EAssetPackageDiffState::Moved:
				return FText::FromString(TEXT(">"));

			case EAssetPackageDiffState::Unchanged:
			default:
				return FText::FromString(TEXT("="));
		}
	}

	FText GetStateText() const
	{
		if (!Item.IsValid())
		{
			return FText::GetEmpty();
		}

		switch (Item->Diff.State)
		{
			case EAssetPackageDiffState::Added:
				return LOCTEXT("AddedState", "Added");

			case EAssetPackageDiffState::Removed:
				return LOCTEXT("RemovedState", "Removed");

			case EAssetPackageDiffState::Modified:
				return LOCTEXT("ModifiedState", "Modified");

			case EAssetPackageDiffState::Moved:
				return LOCTEXT("MovedState", "Moved");

			case EAssetPackageDiffState::Unchanged:
			default:
				return LOCTEXT("UnchangedState", "Unchanged");
		}
	}

private:
	FItemType Item;
};

void SAssetSerializationDiff::Construct(const FArguments& InArgs)
{
	DiffSession = InArgs._Session.IsValid() ? InArgs._Session : MakeShared<FAssetSerializationDiffSession>();

	StatusText = LOCTEXT("ReadyStatus", "Select two package files (.uasset or .umap) to compare.");

	HexDiffStyle = MakeShared<FSlateStyleSet>(TEXT("AssetSerializationHexDiffStyle"));
	FTextBlockStyle NormalStyle = FAppStyle::GetWidgetStyle<FTextBlockStyle>(TEXT("NormalText"));
	NormalStyle.SetFont(FAppStyle::GetFontStyle(TEXT("Sequencer.FixedFont")));
	HexDiffStyle->Set(TEXT("Normal"), NormalStyle);
	FTextBlockStyle ChangedStyle = NormalStyle;
	ChangedStyle.SetColorAndOpacity(FSlateColor(FStyleColors::AccentRed));
	HexDiffStyle->Set(TEXT("Changed"), ChangedStyle);
	FTextBlockStyle ShiftedStyle = NormalStyle;
	ShiftedStyle.SetColorAndOpacity(FSlateColor(FStyleColors::Warning));
	HexDiffStyle->Set(TEXT("Shifted"), ShiftedStyle);

	ChildSlot[SNew(SVerticalBox)

		// OLD
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 8.0f, 8.0f, 2.0f)[SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)[SNew(STextBlock).Text(LOCTEXT("OldLabel", "Old"))]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 4.0f, 0.0f)[SAssignNew(OldFilenameTextBox, SEditableTextBox).HintText(LOCTEXT("OldFilenameHint", "Before.uasset"))]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("BrowseOld", "Browse...")).OnClicked(this, &SAssetSerializationDiff::HandleBrowseOldClicked)]]

		// NEW
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 2.0f, 8.0f, 8.0f)[SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)[SNew(STextBlock).Text(LOCTEXT("NewLabel", "New"))]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 4.0f, 0.0f)[SAssignNew(NewFilenameTextBox, SEditableTextBox).HintText(LOCTEXT("NewFilenameHint", "After.uasset"))]
			+ SHorizontalBox::Slot().AutoWidth().Padding(
				0.0f, 0.0f, 4.0f, 0.0f)[SNew(SButton).Text(LOCTEXT("BrowseNew", "Browse...")).OnClicked(this, &SAssetSerializationDiff::HandleBrowseNewClicked)]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("CompareButton", "Compare")).OnClicked(this, &SAssetSerializationDiff::HandleCompareClicked)]]

		+ SVerticalBox::Slot().AutoHeight()[SNew(SSeparator)]

		// Summary/filter bar
		+ SVerticalBox::Slot().AutoHeight().Padding(
			8.0f, 6.0f)[SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(this, &SAssetSerializationDiff::GetSummaryText)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return DiffFilter.bShowUnchanged ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged(this, &SAssetSerializationDiff::HandleShowUnchangedChanged)[SNew(STextBlock).Text(LOCTEXT("ShowUnchanged", "Show unchanged"))]]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)[SNew(SButton)
						.Text(LOCTEXT("ExportReport", "Export report..."))
						.ToolTipText(LOCTEXT("ExportReportTooltip",
							"Save the comparison, save analysis and repeated-save patterns as a text, JSON or HTML report. The report always lists every changed entry, whatever the search and filters in this view are."))
						.OnClicked(this, &SAssetSerializationDiff::HandleExportReportClicked)]]

		// Search and state filters
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 6.0f)[SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)[SNew(SSearchBox)
						.HintText(LOCTEXT("DiffSearchHint", "Search names, paths, types and values"))
						.ToolTipText(LOCTEXT("DiffSearchTooltip", "Separate terms with spaces; every term must match. Matching entries keep their parents visible."))
						.OnTextChanged(this, &SAssetSerializationDiff::HandleSearchTextChanged)]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)[SNew(SCheckBox)
						.IsChecked_Lambda([this]() { return DiffFilter.bSearchValues ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged(this, &SAssetSerializationDiff::HandleSearchValuesChanged)
						.ToolTipText(LOCTEXT("SearchValuesTooltip", "Also search old, new, decoded and final values."))[SNew(STextBlock).Text(LOCTEXT("SearchValues", "Values"))]]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)[BuildStateFilterCheckBox(EAssetDiffStateFilter::Added, LOCTEXT("FilterAdded", "Added"))]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)[BuildStateFilterCheckBox(EAssetDiffStateFilter::Removed, LOCTEXT("FilterRemoved", "Removed"))]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)[BuildStateFilterCheckBox(EAssetDiffStateFilter::Modified, LOCTEXT("FilterModified", "Modified"))]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)[BuildStateFilterCheckBox(EAssetDiffStateFilter::Moved, LOCTEXT("FilterMoved", "Moved"))]
			// Fixed width, so the count appearing or changing never moves the controls next to it.
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(
				120.0f)[SNew(STextBlock).Text(this, &SAssetSerializationDiff::GetFilterResultText).ColorAndOpacity(FSlateColor::UseSubduedForeground()).Justification(ETextJustify::Right)]]]

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 6.0f)[SNew(STextBlock).Text(this, &SAssetSerializationDiff::GetSelectedByteComparisonText)]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 6.0f)[SNew(STextBlock)
				.Text(this, &SAssetSerializationDiff::GetSelectedExplanationText)
				.AutoWrapText(true)
				.Visibility_Lambda([this]() { return GetSelectedExplanationText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8.0f)[SNew(SBox).MinDesiredHeight(400.0f)[SNew(SSplitter).Orientation(Orient_Horizontal)

			// Tree
			+ SSplitter::Slot().Value(0.50f)[SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(4.0f)[SAssignNew(DiffTreeView, STreeView<FDiffTreeNodePtr>)
							.TreeItemsSource(&RootDiffNodes)
							.SelectionMode(ESelectionMode::Single)
							.OnGenerateRow(this, &SAssetSerializationDiff::GenerateDiffTreeRow)
							.OnGetChildren(this, &SAssetSerializationDiff::GetDiffTreeChildren)
							.OnSelectionChanged(this, &SAssetSerializationDiff::HandleDiffSelectionChanged)
							.HeaderRow(SNew(SHeaderRow) + SHeaderRow::Column("Name").DefaultLabel(LOCTEXT("DiffColumn", "Difference")).FillWidth(0.50f)
								+ SHeaderRow::Column("Old").DefaultLabel(LOCTEXT("OldColumn", "Old")).FillWidth(0.25f)
								+ SHeaderRow::Column("New").DefaultLabel(LOCTEXT("NewColumn", "New")).FillWidth(0.25f)
								+ SHeaderRow::Column("Type").DefaultLabel(LOCTEXT("TypeColumn", "Type")).FillWidth(0.20f))]]

			// Details
			+ SSplitter::Slot().Value(0.50f)[SNew(SSplitter).Orientation(Orient_Horizontal)
				+ SSplitter::Slot().Value(0.50f)[SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(8.0f)[BuildDetailsPanel(true)]]
				+ SSplitter::Slot().Value(0.50f)[SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(8.0f)[BuildDetailsPanel(false)]]]]]

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 8.0f)[SNew(SExpandableArea)
				.AreaTitle(LOCTEXT("SaveAnalysis", "Save Analysis"))
				.InitiallyCollapsed(false)
				.BodyContent()[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[BuildSaveAnalysisFilterBar()]
					+ SVerticalBox::Slot().AutoHeight()[SAssignNew(SaveAnalysisBox, SBox)
							.MaxDesiredHeight(
								260.0f)[SAssignNew(SaveAnalysisScrollBox, SScrollBox) + SScrollBox::Slot()[SNew(STextBlock).Text(LOCTEXT("NoSaveAnalysis", "No save analysis is available."))]]]]]

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 8.0f)[SNew(SExpandableArea)
				.AreaTitle(LOCTEXT("RepeatedSaveAnalysis", "Repeated Save Analysis"))
				.InitiallyCollapsed(false)
				.BodyContent()[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[BuildRepeatedSaveFilterBar()]
					+ SVerticalBox::Slot().AutoHeight()[SAssignNew(RepeatedSaveAnalysisBox, SBox)
							.MaxDesiredHeight(260.0f)[SAssignNew(RepeatedSaveAnalysisScrollBox, SScrollBox)
								+ SScrollBox::Slot()[SNew(STextBlock).Text(LOCTEXT("NoRepeatedSaveAnalysis", "No repeated save analysis is available."))]]]]]

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 8.0f)[SNew(STextBlock).Text(this, &SAssetSerializationDiff::GetStatusText).ColorAndOpacity(FSlateColor::UseSubduedForeground())]];

	HexScrollLink.SetBoxes(OldHexScrollBox, NewHexScrollBox);

	if (DiffSession->Old.Document.IsValid() && DiffSession->New.Document.IsValid() && DiffSession->DiffResult.IsSet())
	{
		LoadSessionIntoUI();
	}
}

void SAssetSerializationDiff::SetSession(TSharedPtr<FAssetSerializationDiffSession> InSession)
{
	if (!InSession.IsValid())
	{
		return;
	}

	DiffSession = MoveTemp(InSession);

	LoadSessionIntoUI();
}

TSharedRef<SWidget> SAssetSerializationDiff::BuildDetailsPanel(const bool bOldSide)
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(
			0.0f, 0.0f, 0.0f, 8.0f)[SNew(STextBlock).Text(bOldSide ? LOCTEXT("OldDetailsHeading", "Old") : LOCTEXT("NewDetailsHeading", "New")).Font(FAppStyle::GetFontStyle("NormalFontBold"))]

		+ SVerticalBox::Slot().AutoHeight().Padding(
			0.0f, 2.0f)[BuildSelectableDetailRow(LOCTEXT("SelectedNameFormat", "Name:"), TAttribute<FText>::CreateLambda([this]() { return GetSelectedDisplayName(); }))]

		+ SVerticalBox::Slot().AutoHeight().Padding(
			0.0f, 2.0f)[BuildSelectableDetailRow(LOCTEXT("SelectedValueLabel", "Value:"), TAttribute<FText>::CreateLambda([this, bOldSide]() { return GetSelectedValue(bOldSide); }))]

		+ SVerticalBox::Slot().AutoHeight().Padding(
			0.0f, 2.0f)[BuildSelectableDetailRow(LOCTEXT("SelectedOffsetFormat", "Offset:"), TAttribute<FText>::CreateLambda([this, bOldSide]() { return GetSelectedOffset(bOldSide); }))]

		+ SVerticalBox::Slot().AutoHeight().Padding(
			0.0f, 2.0f)[BuildSelectableDetailRow(LOCTEXT("SelectedSizeFormat", "Size:"), TAttribute<FText>::CreateLambda([this, bOldSide]() { return GetSelectedSize(bOldSide); }))]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f)[SNew(SSeparator)]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				.VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("HexPreviewHeading", "Hex Preview")).Font(FAppStyle::GetFontStyle("NormalFontBold"))]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SButton).Text(LOCTEXT("CopyHex", "Copy")).OnClicked_Lambda([this, bOldSide]() {
				  const FString Text = ((bOldSide ? OldHexPreview : NewHexPreview).Plain).ToString();
				  FPlatformApplicationMisc::ClipboardCopy(*Text);
				  return FReply::Handled();
			  })]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(12.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)[SNew(SCheckBox)
						.ToolTipText(LOCTEXT("LinkHexScrollTip", "Scroll the old and the new hex together, so the same rows stay side by side."))
						.IsChecked_Lambda([this]() { return HexScrollLink.IsLinked() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda(
							[this](const ECheckBoxState State) { HexScrollLink.SetLinked(State == ECheckBoxState::Checked); })[SNew(STextBlock).Text(LOCTEXT("LinkHexScroll", "Link scrolling"))]]]

		+ SVerticalBox::Slot().FillHeight(
			1.0f)[SAssignNew(bOldSide ? OldHexScrollBox : NewHexScrollBox, SScrollBox).OnUserScrolled_Lambda([this, bOldSide](const float Offset) { HexScrollLink.OnScrolled(bOldSide, Offset); })

			+ SScrollBox::Slot()[SNew(SSelectableRichText)
					.RichText_Lambda([this, bOldSide]() { return (bOldSide ? OldHexPreview : NewHexPreview).Rich; })
					.PlainText_Lambda([this, bOldSide]() { return (bOldSide ? OldHexPreview : NewHexPreview).Plain; })
					.TextStyle(HexDiffStyle->GetWidgetStyle<FTextBlockStyle>(TEXT("Normal")))
					.DecoratorStyleSet(HexDiffStyle.Get())]];
}

TSharedRef<SWidget> SAssetSerializationDiff::BuildSelectableDetailRow(const FText& Label, TAttribute<FText> Value)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(Label)]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(SEditableText).Text(Value).IsReadOnly(true)];
}

void SAssetSerializationDiff::UpdateSaveAnalysisLayout()
{
	RefreshSaveAnalysisPanel();
	RefreshRepeatedSavePanel();
}

void SAssetSerializationDiff::RefreshSaveAnalysisPanel()
{
	if (SaveAnalysisBox.IsValid())
	{
		SaveAnalysisScrollBox->ClearChildren();
		SaveAnalysisScrollBox->AddSlot()[BuildSaveAnalysisWidget()];
	}
}

void SAssetSerializationDiff::RefreshRepeatedSavePanel()
{
	if (RepeatedSaveAnalysisBox.IsValid())
	{
		RepeatedSaveAnalysisScrollBox->ClearChildren();
		RepeatedSaveAnalysisScrollBox->AddSlot()[BuildRepeatedSaveAnalysisWidget(DiffSession->PackageName)];
	}
}

void SAssetSerializationDiff::HandleAnalysisSearchTextChanged(const FText& NewText)
{
	AnalysisFilter.Query = FAssetSearchQuery::Parse(NewText.ToString());
	RefreshSaveAnalysisPanel();
}

void SAssetSerializationDiff::HandleRepeatedSearchTextChanged(const FText& NewText)
{
	RepeatedSaveFilter.Query = FAssetSearchQuery::Parse(NewText.ToString());
	RefreshRepeatedSavePanel();
}

FText SAssetSerializationDiff::GetAnalysisFilterResultText() const
{
	if (!AnalysisFilter.IsActive() || !DiffSession.IsValid() || !DiffSession->Analysis.IsSet())
	{
		return FText::GetEmpty();
	}

	const FAssetSaveAnalysis& Analysis = DiffSession->Analysis.GetValue();
	const int32 Count = AnalysisFilter.CountMatches(Analysis.SemanticChanges) + AnalysisFilter.CountMatches(Analysis.LayoutChanges) + AnalysisFilter.CountMatches(Analysis.HeaderChanges)
		+ AnalysisFilter.CountMatches(Analysis.UnexplainedChanges);

	return FText::Format(LOCTEXT("AnalysisFilterResultCount", "{0} matching"), FText::AsNumber(Count));
}

FText SAssetSerializationDiff::GetRepeatedFilterResultText() const
{
	if (!RepeatedSaveFilter.IsActive() || RepeatedPatternTotal == 0)
	{
		return FText::GetEmpty();
	}

	return FText::Format(LOCTEXT("RepeatedFilterResultCount", "{0} of {1} shown"), FText::AsNumber(RepeatedPatternShown), FText::AsNumber(RepeatedPatternTotal));
}

TSharedRef<SWidget> SAssetSerializationDiff::BuildConfidenceFilterMenu()
{
	// Keep the menu open while toggling, so several confidences can be changed in a row.
	FMenuBuilder Menu(false, nullptr);

	const auto AddItem = [this, &Menu](const EAssetConfidenceFilter Flag, const FText& Label) {
		Menu.AddMenuEntry(Label, FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Flag]() {
				AnalysisFilter.Confidences ^= Flag;
				RefreshSaveAnalysisPanel();
			}),
				FCanExecuteAction(), FIsActionChecked::CreateLambda([this, Flag]() { return EnumHasAnyFlags(AnalysisFilter.Confidences, Flag); })),
			NAME_None, EUserInterfaceActionType::ToggleButton);
	};

	AddItem(EAssetConfidenceFilter::Certain, LOCTEXT("ConfidenceCertain", "Certain"));
	AddItem(EAssetConfidenceFilter::High, LOCTEXT("ConfidenceHigh", "High"));
	AddItem(EAssetConfidenceFilter::Inferred, LOCTEXT("ConfidenceInferred", "Inferred"));
	AddItem(EAssetConfidenceFilter::Unknown, LOCTEXT("ConfidenceUnknown", "Unknown"));

	return Menu.MakeWidget();
}

TSharedRef<SWidget> SAssetSerializationDiff::BuildPatternFilterMenu()
{
	FMenuBuilder Menu(false, nullptr);

	const auto AddItem = [this, &Menu](const EObservedPatternFilter Flag, const EObservedValuePattern Pattern) {
		Menu.AddMenuEntry(GetObservedPatternText(Pattern), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Flag]() {
				RepeatedSaveFilter.Patterns ^= Flag;
				RefreshRepeatedSavePanel();
			}),
				FCanExecuteAction(), FIsActionChecked::CreateLambda([this, Flag]() { return EnumHasAnyFlags(RepeatedSaveFilter.Patterns, Flag); })),
			NAME_None, EUserInterfaceActionType::ToggleButton);
	};

	AddItem(EObservedPatternFilter::ChangedOnce, EObservedValuePattern::ChangedOnce);
	AddItem(EObservedPatternFilter::Recurring, EObservedValuePattern::Recurring);
	AddItem(EObservedPatternFilter::ChangedEverySave, EObservedValuePattern::ChangedEverySave);
	AddItem(EObservedPatternFilter::ContinuouslyChanging, EObservedValuePattern::ContinuouslyChanging);
	AddItem(EObservedPatternFilter::Alternating, EObservedValuePattern::Alternating);
	AddItem(EObservedPatternFilter::Stable, EObservedValuePattern::Stable);
	AddItem(EObservedPatternFilter::Unknown, EObservedValuePattern::Unknown);

	return Menu.MakeWidget();
}

TSharedRef<SWidget> SAssetSerializationDiff::BuildSaveAnalysisFilterBar()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
			  .FillWidth(1.0f)
			  .VAlign(VAlign_Center)
			  .Padding(0.0f, 0.0f, 8.0f, 0.0f)
				  [SNew(SSearchBox).HintText(LOCTEXT("AnalysisSearchHint", "Search explanations, paths and values")).OnTextChanged(this, &SAssetSerializationDiff::HandleAnalysisSearchTextChanged)]
		+ SHorizontalBox::Slot()
			  .AutoWidth()
			  .VAlign(VAlign_Center)
			  .Padding(0.0f, 0.0f, 8.0f, 0.0f)[SNew(SCheckBox)
					  .IsChecked_Lambda([this]() { return AnalysisFilter.bSearchValues ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					  .OnCheckStateChanged_Lambda([this](const ECheckBoxState NewState) {
						  AnalysisFilter.bSearchValues = NewState == ECheckBoxState::Checked;
						  RefreshSaveAnalysisPanel();
					  })
					  .ToolTipText(LOCTEXT("AnalysisSearchValuesTooltip", "Also search old and new values."))[SNew(STextBlock).Text(LOCTEXT("AnalysisSearchValues", "Values"))]]
		+ SHorizontalBox::Slot()
			  .AutoWidth()
			  .VAlign(VAlign_Center)
			  .Padding(0.0f, 0.0f, 8.0f, 0.0f)[SNew(SComboButton)
					  .ButtonContent()[SNew(STextBlock).Text(LOCTEXT("ConfidenceFilterButton", "Confidence"))]
					  .OnGetMenuContent(this, &SAssetSerializationDiff::BuildConfidenceFilterMenu)]
		// Fixed width, so the count appearing or changing never moves the controls next to it.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(
			120.0f)[SNew(STextBlock).Text(this, &SAssetSerializationDiff::GetAnalysisFilterResultText).ColorAndOpacity(FSlateColor::UseSubduedForeground()).Justification(ETextJustify::Right)]];
}

TSharedRef<SWidget> SAssetSerializationDiff::BuildRepeatedSaveFilterBar()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
			  .FillWidth(1.0f)
			  .VAlign(VAlign_Center)
			  .Padding(0.0f, 0.0f, 8.0f, 0.0f)[SNew(SSearchBox)
					  .HintText(LOCTEXT("RepeatedSearchHint", "Search properties and the values seen across saves"))
					  .OnTextChanged(this, &SAssetSerializationDiff::HandleRepeatedSearchTextChanged)]
		+ SHorizontalBox::Slot()
			  .AutoWidth()
			  .VAlign(VAlign_Center)
			  .Padding(0.0f, 0.0f, 8.0f, 0.0f)[SNew(SCheckBox)
					  .IsChecked_Lambda([this]() { return RepeatedSaveFilter.bSearchValues ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					  .OnCheckStateChanged_Lambda([this](const ECheckBoxState NewState) {
						  RepeatedSaveFilter.bSearchValues = NewState == ECheckBoxState::Checked;
						  RefreshRepeatedSavePanel();
					  })
					  .ToolTipText(LOCTEXT("RepeatedSearchValuesTooltip", "Also search the values seen across saves."))[SNew(STextBlock).Text(LOCTEXT("RepeatedSearchValues", "Values"))]]
		+ SHorizontalBox::Slot()
			  .AutoWidth()
			  .VAlign(VAlign_Center)
			  .Padding(0.0f, 0.0f, 8.0f,
				  0.0f)[SNew(SComboButton).ButtonContent()[SNew(STextBlock).Text(LOCTEXT("PatternFilterButton", "Pattern"))].OnGetMenuContent(this, &SAssetSerializationDiff::BuildPatternFilterMenu)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(
			120.0f)[SNew(STextBlock).Text(this, &SAssetSerializationDiff::GetRepeatedFilterResultText).ColorAndOpacity(FSlateColor::UseSubduedForeground()).Justification(ETextJustify::Right)]];
}

TSharedRef<SWidget> SAssetSerializationDiff::BuildSaveAnalysisWidget()
{
	if (!DiffSession.IsValid() || !DiffSession->Analysis.IsSet())
	{
		return SNew(STextBlock).Text(LOCTEXT("NoSaveAnalysis", "No save analysis is available."));
	}

	const FAssetSaveAnalysis& Analysis = DiffSession->Analysis.GetValue();

	return SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[BuildSaveAnalysisSummary(Analysis)]
		+ SVerticalBox::Slot()
			  .AutoHeight()[BuildSaveAnalysisSection(LOCTEXT("MeaningfulChangesSection", "Meaningful Changes"), TEXT("Meaningful"), AnalysisFilter.FilterEntries(Analysis.SemanticChanges))]
		+ SVerticalBox::Slot().AutoHeight().Padding(
			0.0f, 4.0f, 0.0f, 0.0f)[BuildSaveAnalysisSection(LOCTEXT("LayoutChangesSection", "Layout / Serialization"), TEXT("Layout"), AnalysisFilter.FilterEntries(Analysis.LayoutChanges))]
		+ SVerticalBox::Slot().AutoHeight().Padding(
			0.0f, 4.0f, 0.0f, 0.0f)[BuildSaveAnalysisSection(LOCTEXT("HeaderChangesSection", "Package Header"), TEXT("Header"), AnalysisFilter.FilterEntries(Analysis.HeaderChanges))]
		+ SVerticalBox::Slot().AutoHeight().Padding(
			0.0f, 4.0f, 0.0f, 0.0f)[BuildSaveAnalysisSection(LOCTEXT("UnexplainedChangesSection", "Unexplained"), TEXT("Unexplained"), AnalysisFilter.FilterEntries(Analysis.UnexplainedChanges))];
}

TSharedRef<SWidget> SAssetSerializationDiff::BuildSaveAnalysisSummary(const FAssetSaveAnalysis& Analysis)
{
	return SNew(SBorder).Padding(8.0f)[SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(GetSaveAnalysisResultText(Analysis.ResultKind)).Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)[SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 16.0f, 0.0f)[BuildAnalysisStat(LOCTEXT("PropertyChangesStat", "Properties"), FText::AsNumber(Analysis.PropertyChangeCount))]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 16.0f, 0.0f)[BuildAnalysisStat(LOCTEXT("ChangedBytesStat", "Changed bytes"), FText::AsNumber(Analysis.TotalChangedBytes))]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 16.0f, 0.0f)[BuildAnalysisStat(LOCTEXT("ExplainedBytesStat", "Explained"), FText::AsNumber(Analysis.ExplainedChangedBytes))]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 16.0f, 0.0f)[BuildAnalysisStat(LOCTEXT("UnexplainedBytesStat", "Unexplained"), FText::AsNumber(Analysis.UnexplainedChangedBytes))]
			+ SHorizontalBox::Slot().AutoWidth()[BuildAnalysisStat(LOCTEXT("RelocationsStat", "Relocations"), FText::AsNumber(Analysis.RelocationCount))]]];
}

TSharedRef<SWidget> SAssetSerializationDiff::BuildAnalysisStat(const FText& Label, const FText& Value)
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Label)] + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Value).Font(FAppStyle::GetFontStyle("BoldFont"))];
}

TSharedRef<SWidget> SAssetSerializationDiff::BuildSaveAnalysisSection(const FText& Title, const FString& StateKey, const TArray<FAssetSaveExplanationEntry>& Entries)
{
	return SNew(SExpandableArea)
		.AreaTitle(Title)
		.InitiallyCollapsed(CollapsedAnalysisSections.Contains(StateKey))
		.OnAreaExpansionChanged_Lambda([this, StateKey](const bool bExpanded) {
			if (bExpanded)
			{
				CollapsedAnalysisSections.Remove(StateKey);
			}
			else
			{
				CollapsedAnalysisSections.Add(StateKey);
			}
		})
		.BodyContent()[BuildSaveAnalysisEntries(Entries, 0)];
}

TSharedRef<SWidget> SAssetSerializationDiff::BuildSaveAnalysisEntries(const TArray<FAssetSaveExplanationEntry>& Entries, const int32 Depth)
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	if (Entries.IsEmpty())
	{
		Box->AddSlot().AutoHeight().Padding(8.0f, 4.0f)[SNew(STextBlock).Text(AnalysisFilter.IsActive() ? LOCTEXT("NoAnalysisMatches", "No matches") : LOCTEXT("NoAnalysisEntries", "None"))];
		return Box;
	}

	for (const FAssetSaveExplanationEntry& Entry : Entries)
	{
		Box->AddSlot().AutoHeight().Padding(8.0f + static_cast<float>(Depth) * 16.0f, 3.0f)[BuildSaveAnalysisEntry(Entry, Depth)];
	}

	return Box;
}

TSharedRef<SWidget> SAssetSerializationDiff::BuildSaveAnalysisEntry(const FAssetSaveExplanationEntry& Entry, const int32 Depth)
{
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	Content->AddSlot().AutoHeight()[SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(4.0f)
			.OnClicked_Lambda([this, Key = Entry.Key]() {
				NavigateToDiffEntry(Key);

				return FReply::Handled();
			})[SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)[SNew(STextBlock).Text(GetExplanationPrefix(Entry.Classification))]
				+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).Text(Entry.Title).Font(FAppStyle::GetFontStyle("BoldFont"))]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(GetConfidenceText(Entry.Confidence))]]];

	if (!Entry.Description.IsEmpty())
	{
		Content->AddSlot().AutoHeight().Padding(20.0f, 2.0f, 0.0f, 2.0f)[SNew(STextBlock).Text(Entry.Description).AutoWrapText(true)];
	}

	if (!Entry.CauseDescription.IsEmpty())
	{
		Content->AddSlot().AutoHeight().Padding(
			20.0f, 2.0f, 0.0f, 2.0f)[SNew(STextBlock).Text(FText::Format(LOCTEXT("LikelyCauseFormat", "Likely cause: {0}"), Entry.CauseDescription)).AutoWrapText(true)];
	}

	if (!Entry.Children.IsEmpty())
	{
		Content->AddSlot().AutoHeight()[BuildSaveAnalysisEntries(Entry.Children, Depth + 1)];
	}

	return Content;
}

TSharedRef<SWidget> SAssetSerializationDiff::BuildRepeatedSaveAnalysisWidget(const FName PackageName)
{
	const FAssetSaveHistory* History = FAssetSaveHistoryManager::Get().FindHistory(PackageName);

	if (History == nullptr || History->Entries.Num() < 2)
	{
		return SNew(STextBlock).Text(LOCTEXT("NotEnoughSaveHistory", "Save the monitored asset at least twice to analyze repeated behavior."));
	}

	// Only properties that changed are of interest; the filter then narrows those.
	TArray<FRepeatedSavePattern> Changed;
	for (const FRepeatedSavePattern& Pattern : FRepeatedSaveAnalyzer::Analyze(*History))
	{
		if (Pattern.ChangeCount > 0)
		{
			Changed.Add(Pattern);
		}
	}

	const TArray<FRepeatedSavePattern> Patterns = RepeatedSaveFilter.Filter(Changed);
	RepeatedPatternTotal = Changed.Num();
	RepeatedPatternShown = Patterns.Num();

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	Content->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f,
		8.0f)[SNew(STextBlock).Text(FText::Format(LOCTEXT("ObservedSaveCount", "{0} saves observed"), FText::AsNumber(History->Entries.Num()))).Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))];

	if (Patterns.IsEmpty() && RepeatedSaveFilter.IsActive())
	{
		Content->AddSlot().AutoHeight().Padding(0.0f, 2.0f)[SNew(STextBlock).Text(LOCTEXT("NoRepeatedMatches", "No matches"))];
	}

	for (const FRepeatedSavePattern& Pattern : Patterns)
	{
		Content->AddSlot().AutoHeight().Padding(0.0f, 2.0f)[BuildRepeatedSavePatternWidget(Pattern)];
	}

	return Content;
}

TSharedRef<SWidget> SAssetSerializationDiff::BuildRepeatedSavePatternWidget(const FRepeatedSavePattern& Pattern)
{
	return SNew(SExpandableArea)
		.AreaTitle(Pattern.DisplayName)
		.InitiallyCollapsed(!ExpandedRepeatedPatterns.Contains(Pattern.SemanticPath))
		.OnAreaExpansionChanged_Lambda([this, Path = Pattern.SemanticPath](const bool bExpanded) {
			if (bExpanded)
			{
				ExpandedRepeatedPatterns.Add(Path);
			}
			else
			{
				ExpandedRepeatedPatterns.Remove(Path);
			}
		})
		.HeaderContent()[SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).Text(Pattern.DisplayName).Font(FAppStyle::GetFontStyle("BoldFont"))]

			+ SHorizontalBox::Slot().AutoWidth().Padding(
				8.0f, 0.0f)[SNew(STextBlock).Text(FText::Format(LOCTEXT("PatternFrequency", "{0} / {1} saves"), FText::AsNumber(Pattern.ChangeCount), FText::AsNumber(Pattern.ObservationCount)))]

			+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(GetObservedPatternText(Pattern.ValuePattern))]]
		.BodyContent()[BuildObservedValueTimeline(Pattern)];
}

TSharedRef<SWidget> SAssetSerializationDiff::BuildObservedValueTimeline(const FRepeatedSavePattern& Pattern)
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	for (const FObservedPropertySample& Sample : Pattern.Samples)
	{
		Box->AddSlot().AutoHeight().Padding(8.0f, 2.0f)[SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.OnClicked_Lambda([this, SaveId = Sample.SaveId, Path = Pattern.SemanticPath]() {
					OpenHistorySave(SaveId, Path);

					return FReply::Handled();
				})[SNew(SHorizontalBox)

					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)[SNew(STextBlock).Text(FText::FromString(Sample.Timestamp.ToString(TEXT("%H:%M:%S"))))]

					+ SHorizontalBox::Slot().AutoWidth().Padding(
						0.0f, 0.0f, 12.0f, 0.0f)[SNew(STextBlock).Text(Sample.bChanged ? LOCTEXT("ObservedChanged", "Changed") : LOCTEXT("ObservedUnchanged", "Unchanged"))]

					+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).Text(BuildObservedValueText(Sample, true)).ToolTipText(BuildObservedValueText(Sample, false))]]];
	}

	return Box;
}

SAssetSerializationDiff::FDiffTreeNodePtr SAssetSerializationDiff::FindDiffTreeNode(const TArray<FDiffTreeNodePtr>& Nodes, const TFunctionRef<bool(const FAssetPackageDiffEntry&)> Matches) const
{
	for (const FDiffTreeNodePtr& Node : Nodes)
	{
		if (!Node.IsValid())
		{
			continue;
		}

		if (Matches(Node->Diff))
		{
			return Node;
		}

		const FDiffTreeNodePtr Found = FindDiffTreeNode(Node->Children, Matches);
		if (Found.IsValid())
		{
			return Found;
		}
	}

	return nullptr;
}

void SAssetSerializationDiff::SelectAndReveal(const FDiffTreeNodePtr& Node)
{
	ExpandDiffAncestors(Node);
	DiffTreeView->SetSelection(Node, ESelectInfo::Direct);
	DiffTreeView->RequestScrollIntoView(Node);
}

void SAssetSerializationDiff::NavigateToDiffEntry(const FString& Key)
{
	FDiffTreeNodePtr Node = FindDiffTreeNode(RootDiffNodes, [&Key](const FAssetPackageDiffEntry& Entry) { return Entry.Key == Key; });

	if (!Node.IsValid() || !DiffTreeView.IsValid())
	{
		return;
	}

	SelectAndReveal(Node);
}

FString SAssetSerializationDiff::MakeCompactHistoryValue(const FString& Value) const
{
	constexpr int32 MaximumLength = 80;

	if (Value.Len() <= MaximumLength)
	{
		return Value;
	}

	return Value.Left(MaximumLength - 3) + TEXT("...");
}

TSharedRef<FAssetSerializationDiffSession> FAssetSerializationDiffSession::FromObservedSave(const FObservedAssetSave& Save)
{
	TSharedRef<FAssetSerializationDiffSession> Session = MakeShared<FAssetSerializationDiffSession>();
	Session->Old.Document = Save.Before;
	Session->Old.Traces = Save.BeforeFields;
	Session->New.Document = Save.After;
	Session->New.Traces = Save.AfterFields;
	Session->DiffResult = Save.Diff;
	Session->Analysis = Save.Analysis;
	Session->ObservedSaveId = Save.SaveId;
	Session->PackageName = Save.PackageName;

	return Session;
}

TSharedPtr<FAssetSerializationDiffSession> FAssetSerializationDiffSession::FromFiles(const FString& OldFilename, const FString& NewFilename, const FName PackageName, FText& OutError)
{
	FText OldError;
	FText NewError;
	const TSharedPtr<FAssetPackageDocument> OldDocument = FAssetPackageReader::LoadFromFile(OldFilename, OldError);
	const TSharedPtr<FAssetPackageDocument> NewDocument = FAssetPackageReader::LoadFromFile(NewFilename, NewError);

	if (!OldDocument.IsValid() || !NewDocument.IsValid())
	{
		OutError = !OldDocument.IsValid() ? OldError : NewError;
		return nullptr;
	}

	const TSharedRef<FAssetSerializationDiffSession> Session = MakeShared<FAssetSerializationDiffSession>();
	Session->Old.Document = OldDocument;
	Session->Old.Traces = FAssetPackageFieldDecoder::Decode(*OldDocument);
	Session->New.Document = NewDocument;
	Session->New.Traces = FAssetPackageFieldDecoder::Decode(*NewDocument);
	Session->DiffResult = AssetPackageDiff::Compare(*OldDocument, *NewDocument, Session->Old.Traces.Get(), Session->New.Traces.Get());
	Session->Analysis = FAssetSaveAnalyzer::Analyze(Session->DiffResult.GetValue(), *OldDocument, *NewDocument);
	Session->PackageName = PackageName;
	return Session;
}

void SAssetSerializationDiff::OpenHistorySave(const FObservedSaveId SaveId, const FString& SemanticPath)
{
	const TSharedPtr<const FObservedAssetSave> Save = FAssetSaveHistoryManager::Get().FindSave(SaveId);
	if (!Save.IsValid())
	{
		return;
	}

	TSharedRef<FAssetSerializationDiffSession> Session = FAssetSerializationDiffSession::FromObservedSave(*Save);
	SetSession(Session);

	NavigateToSemanticPath(SemanticPath);
}

void SAssetSerializationDiff::NavigateToSemanticPath(const FString& SemanticPath)
{
	if (SemanticPath.IsEmpty() || !DiffTreeView.IsValid())
	{
		return;
	}

	const FDiffTreeNodePtr Node = FindDiffTreeNode(RootDiffNodes, [&SemanticPath](const FAssetPackageDiffEntry& Entry) { return Entry.SemanticPath == SemanticPath; });
	if (!Node.IsValid())
	{
		return;
	}

	SelectAndReveal(Node);
}

bool SAssetSerializationDiff::BrowseForAsset(const FText& DialogTitle, FString& OutFilename)
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();

	if (DesktopPlatform == nullptr)
	{
		return false;
	}

	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

	TArray<FString> SelectedFiles;
	const bool bSelected = DesktopPlatform->OpenFileDialog(
		ParentWindowHandle, DialogTitle.ToString(), FPaths::ProjectContentDir(), TEXT(""), TEXT("Unreal Asset (*.uasset;*.umap)|*.uasset;*.umap"), EFileDialogFlags::None, SelectedFiles);

	if (!bSelected || SelectedFiles.IsEmpty())
	{
		return false;
	}

	OutFilename = SelectedFiles[0];
	return true;
}

FReply SAssetSerializationDiff::HandleBrowseOldClicked()
{
	FString Filename;
	if (BrowseForAsset(LOCTEXT("BrowseOldDialog", "Select Old Unreal Asset"), Filename))
	{
		OldFilenameTextBox->SetText(FText::FromString(Filename));
	}
	return FReply::Handled();
}

FReply SAssetSerializationDiff::HandleBrowseNewClicked()
{
	FString Filename;
	if (BrowseForAsset(LOCTEXT("BrowseNewDialog", "Select New Unreal Asset"), Filename))
	{
		NewFilenameTextBox->SetText(FText::FromString(Filename));
	}
	return FReply::Handled();
}

bool SAssetSerializationDiff::BrowseForReportFile(const FString& DefaultFilename, FString& OutFilename)
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();

	if (DesktopPlatform == nullptr)
	{
		return false;
	}

	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

	// The suggested name has no extension: when the name has none, the dialog adds the one of the file type that is selected, so
	// choosing "JSON" or "HTML" saves that format. A name typed with an extension keeps it, and the extension decides the format.
	TArray<FString> SelectedFiles;
	const bool bSelected = DesktopPlatform->SaveFileDialog(ParentWindowHandle, LOCTEXT("ExportReportDialogTitle", "Export Report").ToString(), FPaths::ProjectSavedDir(),
		FPaths::GetBaseFilename(DefaultFilename, false), TEXT("Text report (*.txt)|*.txt|JSON report (*.json)|*.json|HTML report (*.html)|*.html"), EFileDialogFlags::None, SelectedFiles);

	if (!bSelected || SelectedFiles.IsEmpty())
	{
		return false;
	}

	OutFilename = SelectedFiles[0];

	// The format follows the extension; the dialog adds the selected type's one, this is only a safety net.
	if (FPaths::GetExtension(OutFilename).IsEmpty())
	{
		OutFilename += TEXT(".txt");
	}

	return true;
}

FReply SAssetSerializationDiff::HandleExportReportClicked()
{
	if (!DiffSession.IsValid() || !DiffSession->DiffResult.IsSet())
	{
		StatusText = LOCTEXT("ExportNoComparison", "There is no comparison to export.");
		return FReply::Handled();
	}

	// Name the report after the compared asset, preferring the newer file, so reports for different assets do not collide.
	const TSharedPtr<FAssetPackageDocument>& NamedDocument = DiffSession->New.Document.IsValid() ? DiffSession->New.Document : DiffSession->Old.Document;
	const FString DefaultFilename =
		AssetReportWriter::MakeDefaultFilename(NamedDocument.IsValid() ? NamedDocument->Filename : DiffSession->PackageName.ToString(), FDateTime::Now(), EAssetReportFormat::Text);

	FString Filename;
	if (!BrowseForReportFile(DefaultFilename, Filename))
	{
		return FReply::Handled();
	}

	TArray<FRepeatedSavePattern> RepeatedSavePatterns;
	if (!DiffSession->PackageName.IsNone())
	{
		const FAssetSaveHistory* History = FAssetSaveHistoryManager::Get().FindHistory(DiffSession->PackageName);

		if (History != nullptr && History->Entries.Num() >= 2)
		{
			RepeatedSavePatterns = FRepeatedSaveAnalyzer::Analyze(*History);
		}
	}

	const FAssetSaveAnalysis* Analysis = DiffSession->Analysis.IsSet() ? &DiffSession->Analysis.GetValue() : nullptr;
	// The report is not a snapshot of the view: a filtered list could leave out the very entry someone needs later, and a text editor
	// can search it anyway. Only the default view applies, which leaves out entries that did not change.
	const FAssetDiffFilter ReportFilter;
	const FAssetAnalysisReport Report = AssetAnalysisReport::Build(DiffSession->DiffResult.GetValue(), Analysis, RepeatedSavePatterns, &ReportFilter);

	FText Error;
	if (AssetReportWriter::SaveToFile(Report, Filename, Error))
	{
		StatusText = FText::Format(LOCTEXT("ExportSucceeded", "Report saved to {0}"), FText::FromString(Filename));
	}
	else
	{
		StatusText = Error;
	}

	return FReply::Handled();
}

bool SAssetSerializationDiff::LoadDocument(const FString& Filename, TSharedPtr<FAssetPackageDocument>& OutDocument, FText& OutError)
{
	OutDocument = FAssetPackageReader::LoadFromFile(Filename, OutError);
	return OutDocument.IsValid();
}

FReply SAssetSerializationDiff::HandleCompareClicked()
{
	const FString OldFilename = OldFilenameTextBox.IsValid() ? OldFilenameTextBox->GetText().ToString().TrimStartAndEnd() : FString();
	const FString NewFilename = NewFilenameTextBox.IsValid() ? NewFilenameTextBox->GetText().ToString().TrimStartAndEnd() : FString();

	if (OldFilename.IsEmpty() || NewFilename.IsEmpty())
	{
		StatusText = LOCTEXT("MissingFilenames", "Select both an old and a new package file (.uasset or .umap).");
		return FReply::Handled();
	}

	FText Error;

	if (!LoadDocument(OldFilename, DiffSession->Old.Document, Error))
	{
		StatusText = FText::Format(LOCTEXT("OldLoadFailed", "Could not load old asset: {0}"), Error);
		return FReply::Handled();
	}

	if (!LoadDocument(NewFilename, DiffSession->New.Document, Error))
	{
		StatusText = FText::Format(LOCTEXT("NewLoadFailed", "Could not load new asset: {0}"), Error);
		return FReply::Handled();
	}

	BuildTracesForSide(DiffSession->Old);
	BuildTracesForSide(DiffSession->New);

	DiffSession->DiffResult = AssetPackageDiff::Compare(*DiffSession->Old.Document.Get(), *DiffSession->New.Document.Get(), DiffSession->Old.Traces.Get(), DiffSession->New.Traces.Get());

	RebuildDiffTree();

	if (DiffSession->DiffResult->bFilesIdentical)
	{
		StatusText = LOCTEXT("FilesIdentical", "The two files are byte-for-byte identical.");
	}
	else
	{
		StatusText = LOCTEXT("ComparisonComplete", "Comparison complete.");
	}

	return FReply::Handled();
}

void SAssetSerializationDiff::RebuildDiffTree()
{
	// Keep the selection across filter changes when the selected entry is still visible.
	const FString PreviousSemanticPath = SelectedDiffNode.IsValid() ? SelectedDiffNode->Diff.SemanticPath : FString();
	const FString PreviousKey = SelectedDiffNode.IsValid() ? SelectedDiffNode->Diff.Key : FString();

	// Rebuilding creates new nodes, which the tree view would show collapsed. Remember what the user had expanded, unless the
	// tree was only expanded to show search results.
	if (DiffTreeView.IsValid() && !bDiffTreeExpandedForSearch)
	{
		ExpandedDiffNodeIdentities.Reset();

		for (const FDiffTreeNodePtr& Node : RootDiffNodes)
		{
			CollectExpandedDiffNodes(Node, ExpandedDiffNodeIdentities);
		}
	}

	RootDiffNodes.Reset();
	SelectedDiffNode.Reset();

	if (!DiffSession->DiffResult.IsSet())
	{
		if (DiffTreeView.IsValid())
		{
			DiffTreeView->RequestTreeRefresh();
		}

		return;
	}

	for (const FAssetPackageDiffEntry& Entry : DiffSession->DiffResult->Entries)
	{
		FDiffTreeNodePtr Node = BuildDiffTreeNode(Entry, nullptr, false);

		if (Node.IsValid())
		{
			RootDiffNodes.Add(Node);
		}
	}

	if (DiffTreeView.IsValid())
	{
		DiffTreeView->RequestTreeRefresh();

		bDiffTreeExpandedForSearch = DiffFilter.IsSearchActive();

		for (const FDiffTreeNodePtr& Node : RootDiffNodes)
		{
			// While searching, open everything so the matches are visible without hunting for them.
			if (bDiffTreeExpandedForSearch)
			{
				ExpandDiffSubtree(Node);
			}
			else
			{
				DiffTreeView->SetItemExpansion(Node, true);
				RestoreDiffNodeExpansion(Node);
			}
		}

		FDiffTreeNodePtr Previous;
		if (!PreviousSemanticPath.IsEmpty())
		{
			Previous = FindDiffTreeNode(RootDiffNodes, [&PreviousSemanticPath](const FAssetPackageDiffEntry& Entry) { return Entry.SemanticPath == PreviousSemanticPath; });
		}
		else if (!PreviousKey.IsEmpty())
		{
			Previous = FindDiffTreeNode(RootDiffNodes, [&PreviousKey](const FAssetPackageDiffEntry& Entry) { return Entry.Key == PreviousKey; });
		}

		if (Previous.IsValid())
		{
			SelectAndReveal(Previous);
		}
	}
}

SAssetSerializationDiff::FDiffTreeNodePtr SAssetSerializationDiff::BuildDiffTreeNode(const FAssetPackageDiffEntry& Entry, const FDiffTreeNodePtr& Parent, const bool bAncestorMatchedQuery)
{
	FDiffTreeNodePtr Node = MakeShared<FAssetPackageDiffTreeNode>();

	Node->Diff = Entry;
	Node->Parent = Parent;

	// Once an entry matches the search, everything inside it counts as matching.
	const bool bMatchedForChildren = bAncestorMatchedQuery || DiffFilter.MatchesQuery(Entry);

	for (const FAssetPackageDiffEntry& Child : Entry.Children)
	{
		FDiffTreeNodePtr ChildNode = BuildDiffTreeNode(Child, Node, bMatchedForChildren);

		if (ChildNode.IsValid())
		{
			Node->Children.Add(ChildNode);
		}
	}

	// An entry stays when it matches or when something inside it does.
	if (Node->Children.IsEmpty() && !DiffFilter.ShouldIncludeSelf(Entry, bAncestorMatchedQuery))
	{
		return nullptr;
	}

	return Node;
}

FString SAssetSerializationDiff::MakeDiffNodeIdentity(const FDiffTreeNodePtr& Node)
{
	FString Identity;

	for (FDiffTreeNodePtr Current = Node; Current.IsValid(); Current = Current->Parent.Pin())
	{
		Identity = FString::Printf(TEXT("/%s|%s%s"), *Current->Diff.Key, *Current->Diff.SemanticPath, *Identity);
	}

	return Identity;
}

void SAssetSerializationDiff::CollectExpandedDiffNodes(const FDiffTreeNodePtr& Node, TSet<FString>& OutIdentities) const
{
	if (!Node.IsValid())
	{
		return;
	}

	if (DiffTreeView->IsItemExpanded(Node))
	{
		OutIdentities.Add(MakeDiffNodeIdentity(Node));
	}

	for (const FDiffTreeNodePtr& Child : Node->Children)
	{
		CollectExpandedDiffNodes(Child, OutIdentities);
	}
}

void SAssetSerializationDiff::RestoreDiffNodeExpansion(const FDiffTreeNodePtr& Node)
{
	if (!Node.IsValid())
	{
		return;
	}

	if (ExpandedDiffNodeIdentities.Contains(MakeDiffNodeIdentity(Node)))
	{
		DiffTreeView->SetItemExpansion(Node, true);
	}

	for (const FDiffTreeNodePtr& Child : Node->Children)
	{
		RestoreDiffNodeExpansion(Child);
	}
}

void SAssetSerializationDiff::ExpandDiffSubtree(const FDiffTreeNodePtr& Node)
{
	if (!Node.IsValid() || !DiffTreeView.IsValid())
	{
		return;
	}

	DiffTreeView->SetItemExpansion(Node, true);

	for (const FDiffTreeNodePtr& Child : Node->Children)
	{
		ExpandDiffSubtree(Child);
	}
}

void SAssetSerializationDiff::HandleShowUnchangedChanged(const ECheckBoxState NewState)
{
	DiffFilter.bShowUnchanged = NewState == ECheckBoxState::Checked;

	RebuildDiffTree();
}

void SAssetSerializationDiff::HandleSearchTextChanged(const FText& NewText)
{
	DiffFilter.Query = FAssetSearchQuery::Parse(NewText.ToString());

	RebuildDiffTree();
}

void SAssetSerializationDiff::HandleSearchValuesChanged(const ECheckBoxState NewState)
{
	DiffFilter.bSearchValues = NewState == ECheckBoxState::Checked;

	RebuildDiffTree();
}

void SAssetSerializationDiff::HandleStateFilterChanged(const ECheckBoxState NewState, const EAssetDiffStateFilter Flag)
{
	if (NewState == ECheckBoxState::Checked)
	{
		DiffFilter.States |= Flag;
	}
	else
	{
		DiffFilter.States &= ~Flag;
	}

	RebuildDiffTree();
}

ECheckBoxState SAssetSerializationDiff::GetStateFilterCheckState(const EAssetDiffStateFilter Flag) const
{
	return EnumHasAnyFlags(DiffFilter.States, Flag) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

TSharedRef<SWidget> SAssetSerializationDiff::BuildStateFilterCheckBox(const EAssetDiffStateFilter Flag, const FText& Label)
{
	return SNew(SCheckBox)
		.IsChecked(this, &SAssetSerializationDiff::GetStateFilterCheckState, Flag)
		.OnCheckStateChanged(this, &SAssetSerializationDiff::HandleStateFilterChanged, Flag)[SNew(STextBlock).Text(Label)];
}

FText SAssetSerializationDiff::GetFilterResultText() const
{
	if (!DiffSession.IsValid() || !DiffSession->DiffResult.IsSet() || (!DiffFilter.IsSearchActive() && !DiffFilter.IsStateFilterActive()))
	{
		return FText::GetEmpty();
	}

	return FText::Format(LOCTEXT("FilterResultCount", "{0} matching"), FText::AsNumber(DiffFilter.CountMatches(DiffSession->DiffResult->Entries)));
}

void SAssetSerializationDiff::GetDiffTreeChildren(FDiffTreeNodePtr Item, TArray<FDiffTreeNodePtr>& OutChildren) const
{
	if (Item.IsValid())
	{
		OutChildren.Append(Item->Children);
	}
}

TSharedRef<ITableRow> SAssetSerializationDiff::GenerateDiffTreeRow(FDiffTreeNodePtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SAssetPackageDiffTreeRow, OwnerTable).Item(Item);
}

void SAssetSerializationDiff::HandleDiffSelectionChanged(FDiffTreeNodePtr Item, ESelectInfo::Type SelectInfo)
{
	SelectedDiffNode = Item;
	SelectedByteDiffSpans.Reset();

	if (!SelectedDiffNode.IsValid() || !DiffSession->Old.Document.IsValid() || !DiffSession->New.Document.IsValid())
	{
		return;
	}

	const FAssetPackageDiffEntry& Diff = SelectedDiffNode->Diff;

	SelectedByteDiffSpans = FAssetByteDiff::Compare(*DiffSession->Old.Document.Get(), Diff.OldOffset, Diff.OldSize, *DiffSession->New.Document.Get(), Diff.NewOffset, Diff.NewSize);

	AnnotateSelectedDiffSpans();

	OldHexPreview = BuildHighlightedHexPreview(DiffSession->Old.Document.Get(), Diff.OldOffset, Diff.OldSize, SelectedByteDiffSpans, Diff.ShiftedOffsetRanges);
	NewHexPreview = BuildHighlightedHexPreview(DiffSession->New.Document.Get(), Diff.NewOffset, Diff.NewSize, SelectedByteDiffSpans, Diff.ShiftedOffsetRanges);
}

void SAssetSerializationDiff::AnnotateSelectedDiffSpans()
{
	if (!SelectedDiffNode.IsValid())
	{
		return;
	}

	const FAssetPackageDiffEntry& Diff = SelectedDiffNode->Diff;
	const FAssetSerializationTrace* OldTrace = FindTraceForDiffEntry(Diff, true);
	const FAssetSerializationTrace* NewTrace = FindTraceForDiffEntry(Diff, false);

	for (FAssetByteDiffSpan& Span : SelectedByteDiffSpans)
	{
		if (OldTrace != nullptr && OldTrace->Root.IsValid())
		{
			const FAssetSerializationTraceNode* Node = AssetSerializationTrace::FindDeepestTraceNode(OldTrace->Root, Span.Offset, Span.Size);

			Span.OldFieldPath = AssetPackageDiff::BuildTracePath(Node);
		}

		if (NewTrace != nullptr && NewTrace->Root.IsValid())
		{
			const FAssetSerializationTraceNode* Node = AssetSerializationTrace::FindDeepestTraceNode(NewTrace->Root, Span.Offset, Span.Size);

			Span.NewFieldPath = AssetPackageDiff::BuildTracePath(Node);
		}
	}
}

const FAssetSerializationTrace* SAssetSerializationDiff::FindTraceForDiffEntry(const FAssetPackageDiffEntry& Diff, const bool bOldSide) const
{
	if (!DiffSession.IsValid())
	{
		return nullptr;
	}

	const FAssetSerializationDiffSide& Side = bOldSide ? DiffSession->Old : DiffSession->New;

	if (!Side.Traces.IsValid())
	{
		return nullptr;
	}

	const int32 ExportIndex = bOldSide ? Diff.OldExportIndex : Diff.NewExportIndex;

	if (ExportIndex == INDEX_NONE)
	{
		return nullptr;
	}

	return Side.Traces->FindExportTrace(ExportIndex);
}

bool SAssetSerializationDiff::BuildTracesForSide(FAssetSerializationDiffSide& Side)
{
	if (!Side.Document.IsValid())
	{
		return false;
	}

	Side.Traces = FAssetPackageFieldDecoder::Decode(*Side.Document.Get());

	return true;
}

// Details
FText SAssetSerializationDiff::GetSelectedDisplayName() const
{
	return SelectedDiffNode.IsValid() ? SelectedDiffNode->Diff.DisplayName : FText::FromString(TEXT("-"));
}

// Offsets
static FText FormatDiffOffset(const int64 Offset)
{
	if (Offset == INDEX_NONE)
	{
		return FText::FromString(TEXT("-"));
	}

	return FText::FromString(FString::Printf(TEXT("0x%llX"), Offset));
}

FText SAssetSerializationDiff::GetSelectedValue(const bool bOldSide) const
{
	const FString& Value = SelectedDiffNode.IsValid() ? (bOldSide ? SelectedDiffNode->Diff.OldValue : SelectedDiffNode->Diff.NewValue) : FString();
	return Value.IsEmpty() ? FText::FromString(TEXT("-")) : FText::FromString(Value);
}

FText SAssetSerializationDiff::GetSelectedOffset(const bool bOldSide) const
{
	return SelectedDiffNode.IsValid() ? FormatDiffOffset(bOldSide ? SelectedDiffNode->Diff.OldOffset : SelectedDiffNode->Diff.NewOffset) : FText::FromString(TEXT("-"));
}

FText SAssetSerializationDiff::GetSelectedSize(const bool bOldSide) const
{
	if (!SelectedDiffNode.IsValid() || (bOldSide ? SelectedDiffNode->Diff.OldOffset : SelectedDiffNode->Diff.NewOffset) == INDEX_NONE)
	{
		return FText::FromString(TEXT("-"));
	}

	return FText::Format(LOCTEXT("DiffSizeBytes", "{0} bytes"), FText::AsNumber(bOldSide ? SelectedDiffNode->Diff.OldSize : SelectedDiffNode->Diff.NewSize));
}

// Status
FText SAssetSerializationDiff::GetStatusText() const
{
	return StatusText;
}

// Summary
FText SAssetSerializationDiff::GetSummaryText() const
{
	if (!DiffSession->DiffResult.IsSet())
	{
		return LOCTEXT("NoDiffSummary", "No comparison loaded");
	}

	if (DiffSession->DiffResult->bFilesIdentical)
	{
		return LOCTEXT("IdenticalSummary", "Files are identical");
	}

	FDiffCounts Counts;

	for (const FAssetPackageDiffEntry& Entry : DiffSession->DiffResult->Entries)
	{
		AccumulateDiffCounts(Entry, Counts);
	}

	return FText::Format(LOCTEXT("DiffSummaryFormat",
							 "{0} modified · {1} moved · "
							 "{2} added · {3} removed"),
		FText::AsNumber(Counts.Modified), FText::AsNumber(Counts.Moved), FText::AsNumber(Counts.Added), FText::AsNumber(Counts.Removed));
}

FText SAssetSerializationDiff::GetSelectedExplanationText() const
{
	return SelectedDiffNode.IsValid() ? SelectedDiffNode->Diff.Explanation : FText::GetEmpty();
}

FText SAssetSerializationDiff::GetSelectedByteComparisonText() const
{
	if (!SelectedDiffNode.IsValid())
	{
		return FText::GetEmpty();
	}

	const FAssetPackageDiffEntry& Diff = SelectedDiffNode->Diff;

	if (!DiffSession->Old.Document.IsValid() || !DiffSession->New.Document.IsValid() || Diff.OldOffset == INDEX_NONE || Diff.NewOffset == INDEX_NONE)
	{
		return FText::GetEmpty();
	}

	if (Diff.OldSize != Diff.NewSize)
	{
		return FText::Format(LOCTEXT("ByteSizesDiffer", "Byte ranges differ in size: {0} vs {1} bytes"), FText::AsNumber(Diff.OldSize), FText::AsNumber(Diff.NewSize));
	}

	if (!DiffSession->Old.Document->IsValidRange(Diff.OldOffset, Diff.OldSize) || !DiffSession->New.Document->IsValidRange(Diff.NewOffset, Diff.NewSize))
	{
		return FText::GetEmpty();
	}

	const bool bEqual =
		Diff.OldSize == 0 || FMemory::Memcmp(DiffSession->Old.Document->FileData.GetData() + Diff.OldOffset, DiffSession->New.Document->FileData.GetData() + Diff.NewOffset, Diff.OldSize) == 0;

	if (bEqual)
	{
		if (Diff.OldOffset != Diff.NewOffset)
		{
			return LOCTEXT("BytesIdenticalMoved", "Bytes are identical; only their file location changed.");
		}

		return LOCTEXT("BytesIdentical", "Bytes are identical.");
	}

	if (!Diff.ShiftedOffsetRanges.IsEmpty())
	{
		return LOCTEXT("BytesDifferShifted", "Byte contents differ only in stored file offsets (shown in orange).");
	}

	return LOCTEXT("BytesDifferent", "Byte contents differ.");
}

int64 SAssetSerializationDiff::GetSelectedChangedByteCount() const
{
	int64 Result = 0;

	for (const FAssetByteDiffSpan& Span : SelectedByteDiffSpans)
	{
		Result += Span.Size;
	}

	return Result;
}

FText SAssetSerializationDiff::GetSaveAnalysisResultText(const EAssetSaveResultKind ResultKind) const
{
	switch (ResultKind)
	{
		case EAssetSaveResultKind::Identical:
			return LOCTEXT("SaveResultIdentical", "Result: Byte-identical");

		case EAssetSaveResultKind::LayoutOnly:
			return LOCTEXT("SaveResultLayoutOnly", "Result: Layout-only changes");

		case EAssetSaveResultKind::MetadataOnly:
			return LOCTEXT("SaveResultMetadataOnly", "Result: Metadata-only changes");

		case EAssetSaveResultKind::SemanticChanges:
			return LOCTEXT("SaveResultSemantic", "Result: Semantic property changes");

		case EAssetSaveResultKind::SemanticAndNativeChanges:
			return LOCTEXT("SaveResultSemanticNative", "Result: Semantic and native/undecoded changes");

		case EAssetSaveResultKind::NativeOnlyChanges:
			return LOCTEXT("SaveResultNativeOnly", "Result: Native / undecoded changes");

		default:
			return LOCTEXT("SaveResultUnknown", "Result: Unknown changes");
	}
}

FText SAssetSerializationDiff::GetExplanationPrefix(const EAssetSaveChangeClassification Classification) const
{
	switch (Classification)
	{
		case EAssetSaveChangeClassification::PropertyValueChanged:
		case EAssetSaveChangeClassification::ContainerChanged:
			return FText::FromString(TEXT("~"));

		case EAssetSaveChangeClassification::PropertyBecameSerialized:
			return FText::FromString(TEXT("+"));

		case EAssetSaveChangeClassification::PropertyBecameOmitted:
			return FText::FromString(TEXT("-"));

		case EAssetSaveChangeClassification::ExportRelocated:
			return FText::FromString(TEXT(">"));

		case EAssetSaveChangeClassification::NativeOrUndecodedChanged:
			return FText::FromString(TEXT("?"));

		case EAssetSaveChangeClassification::PropertyStoredDifferently:
			return FText::FromString(TEXT("="));

		default:
			return FText::FromString(TEXT("•"));
	}
}

FText SAssetSerializationDiff::GetConfidenceText(const EAssetExplanationConfidence Confidence) const
{
	switch (Confidence)
	{
		case EAssetExplanationConfidence::Certain:
			return LOCTEXT("ConfidenceCertain", "Certain");

		case EAssetExplanationConfidence::High:
			return LOCTEXT("ConfidenceHigh", "High confidence");

		case EAssetExplanationConfidence::Inferred:
			return LOCTEXT("ConfidenceInferred", "Inferred");

		case EAssetExplanationConfidence::Unknown:
		default:
			return FText::GetEmpty();
	}
}

FText SAssetSerializationDiff::BuildObservedValueText(const FObservedPropertySample& Sample, const bool bCompact) const
{
	const auto Show = [this, bCompact](const FString& Value) { return bCompact ? MakeCompactHistoryValue(Value) : Value; };

	if (Sample.bChanged)
	{
		const FString Old = Sample.bHasOldValue ? Show(Sample.OldValue) : TEXT("<not serialized>");
		const FString New = Sample.bHasNewValue ? Show(Sample.NewValue) : TEXT("<not serialized>");
		return FText::FromString(Old + TEXT(" -> ") + New);
	}

	if (Sample.bHasNewValue)
	{
		return FText::FromString(Show(Sample.NewValue));
	}

	return LOCTEXT("ObservedNoValue", "<no decoded value>");
}

FText SAssetSerializationDiff::GetObservedPatternText(const EObservedValuePattern Pattern) const
{
	switch (Pattern)
	{
		case EObservedValuePattern::Stable:
			return LOCTEXT("PatternStable", "Stable");

		case EObservedValuePattern::ChangedOnce:
			return LOCTEXT("PatternChangedOnce", "Changed once");

		case EObservedValuePattern::Recurring:
			return LOCTEXT("PatternRecurring", "Recurring");

		case EObservedValuePattern::ChangedEverySave:
			return LOCTEXT("PatternEverySave", "Changed every save");

		case EObservedValuePattern::ContinuouslyChanging:
			return LOCTEXT("PatternContinuous", "Continuously changing");

		case EObservedValuePattern::Alternating:
			return LOCTEXT("PatternAlternating", "Alternating");

		default:
			return FText::GetEmpty();
	}
}

bool SAssetSerializationDiff::IsByteDifferent(const int64 RelativeOffset, const TArray<FAssetByteDiffSpan>& Spans) const
{
	for (const FAssetByteDiffSpan& Span : Spans)
	{
		if (RelativeOffset >= Span.Offset && RelativeOffset < Span.End())
		{
			return true;
		}

		if (Span.Offset > RelativeOffset)
		{
			break;
		}
	}

	return false;
}

bool SAssetSerializationDiff::IsByteInRanges(const int64 RelativeOffset, const TArray<FAssetByteDiffSpan>& Ranges)
{
	for (const FAssetByteDiffSpan& Range : Ranges)
	{
		if (RelativeOffset >= Range.Offset && RelativeOffset < Range.End())
		{
			return true;
		}
	}

	return false;
}

FHexPreviewText SAssetSerializationDiff::BuildHighlightedHexPreview(
	const FAssetPackageDocument* Document, const int64 Offset, const int64 Size, const TArray<FAssetByteDiffSpan>& Spans, const TArray<FAssetByteDiffSpan>& ShiftedRanges) const
{
	if (Document == nullptr || Offset == INDEX_NONE)
	{
		FText Result = FText::FromString(TEXT("No byte range."));
		return { Result, Result };
	}

	if (Size < 0 || !Document->IsValidRange(Offset, Size))
	{
		FText Result = FText::FromString(TEXT("Invalid byte range."));
		return { Result, Result };
	}

	constexpr int64 MaximumPreviewBytes = 4096;
	constexpr int32 BytesPerRow = 16;

	const int64 PreviewSize = FMath::Min<int64>(Size, MaximumPreviewBytes);

	FString RichResult;
	FString PlainResult;

	// A difference inside a stored file offset that merely moved is shown apart from a real change: orange, not red. The span of a style
	// opens when the style of the bytes changes and closes when it changes again or the line ends.
	const auto SetStyleOfByte = [&](const int64 RelativeOffset, int32& OpenStyle) {
		const bool bChanged = IsByteDifferent(RelativeOffset, Spans);
		const bool bShifted = bChanged && IsByteInRanges(RelativeOffset, ShiftedRanges);
		const int32 Style = !bChanged ? 0 : bShifted ? 2 : 1;

		if (Style != OpenStyle)
		{
			if (OpenStyle != 0)
			{
				RichResult += TEXT("</>");
			}

			if (Style != 0)
			{
				RichResult += Style == 2 ? TEXT("<Shifted>") : TEXT("<Changed>");
			}

			OpenStyle = Style;
		}
	};
	const auto CloseStyle = [&](const int32 OpenStyle) {
		if (OpenStyle != 0)
		{
			RichResult += TEXT("</>");
		}
	};

	for (int64 RowOffset = 0; RowOffset < PreviewSize; RowOffset += BytesPerRow)
	{
		// Relative offsets are much better for side-by-side diffing.
		const FString OffsetText = FString::Printf(TEXT("%08llX  "), RowOffset);
		RichResult += OffsetText;
		PlainResult += OffsetText;

		// HEX
		int32 OpenStyle = 0;

		for (int32 Column = 0; Column < BytesPerRow; ++Column)
		{
			const int64 RelativeOffset = RowOffset + Column;

			if (RelativeOffset >= PreviewSize)
			{
				break;
			}

			SetStyleOfByte(RelativeOffset, OpenStyle);

			const uint8 Byte = Document->FileData[Offset + RelativeOffset];

			const FString ByteText = FString::Printf(TEXT("%02X "), Byte);
			RichResult += ByteText;
			PlainResult += ByteText;

			if (Column == 7)
			{
				RichResult += TEXT(" ");
			}
		}

		CloseStyle(OpenStyle);

		// ASCII
		RichResult += TEXT(" |");
		PlainResult += TEXT(" |");

		OpenStyle = 0;

		for (int32 Column = 0; Column < BytesPerRow; ++Column)
		{
			const int64 RelativeOffset = RowOffset + Column;

			if (RelativeOffset >= PreviewSize)
			{
				break;
			}

			SetStyleOfByte(RelativeOffset, OpenStyle);

			const uint8 Byte = Document->FileData[Offset + RelativeOffset];

			const TCHAR Character = Byte >= 32 && Byte <= 126 ? static_cast<TCHAR>(Byte) : TEXT('.');

			PlainResult.AppendChar(Character);

			/*
			 * Escape markup-sensitive ASCII.
			 */
			switch (Character)
			{
				case TEXT('<'):
					RichResult += TEXT("&lt;");
					break;

				case TEXT('>'):
					RichResult += TEXT("&gt;");
					break;

				case TEXT('&'):
					RichResult += TEXT("&amp;");
					break;

				default:
					RichResult.AppendChar(Character);
					break;
			}
		}

		CloseStyle(OpenStyle);

		FString LineResult;
		LineResult += TEXT("|");
		LineResult += LINE_TERMINATOR;

		RichResult += LineResult;
		PlainResult += LineResult;
	}

	FString FooterResult;
	FooterResult += LINE_TERMINATOR;
	FooterResult += FString::Printf(TEXT("Payload size: %lld"), Size);
	FooterResult += LINE_TERMINATOR;
	FooterResult += FString::Printf(TEXT("Changed bytes: %lld"), GetSelectedChangedByteCount());
	FooterResult += LINE_TERMINATOR;

	if (!ShiftedRanges.IsEmpty())
	{
		FooterResult += TEXT("Orange bytes are stored file offsets that moved with the header; red bytes are other changes.");
		FooterResult += LINE_TERMINATOR;
	}

	RichResult += FooterResult;
	PlainResult += FooterResult;

	if (Size > PreviewSize)
	{
		RichResult += LINE_TERMINATOR;

		RichResult += FString::Printf(TEXT("Preview limited to %lld of %lld bytes."), PreviewSize, Size);
	}

	return { FText::FromString(MoveTemp(RichResult)), FText::FromString(MoveTemp(PlainResult)) };
}

void SAssetSerializationDiff::LoadSessionIntoUI()
{
	if (!DiffSession.IsValid())
	{
		return;
	}

	if (OldFilenameTextBox.IsValid() && DiffSession->Old.Document.IsValid())
	{
		OldFilenameTextBox->SetText(FText::FromString(DiffSession->Old.Document->Filename));
	}

	if (NewFilenameTextBox.IsValid() && DiffSession->New.Document.IsValid())
	{
		NewFilenameTextBox->SetText(FText::FromString(DiffSession->New.Document->Filename));
	}

	RebuildDiffTree();

	SelectFirstMeaningfulDifference();
	StatusText =
		DiffSession->DiffResult->bFilesIdentical ? LOCTEXT("ObservedFilesIdentical", "Saved file is byte-identical.") : LOCTEXT("ObservedSaveLoaded", "Displaying changes produced by the save.");

	UpdateSaveAnalysisLayout();
}

void SAssetSerializationDiff::SelectFirstMeaningfulDifference()
{
	if (!DiffTreeView.IsValid())
	{
		return;
	}

	for (const FDiffTreeNodePtr& Root : RootDiffNodes)
	{
		FDiffTreeNodePtr Found = FindFirstChangedNode(Root);
		if (Found.IsValid())
		{
			SelectAndReveal(Found);

			break;
		}
	}
}

void SAssetSerializationDiff::ExpandDiffAncestors(const FDiffTreeNodePtr& Node)
{
	if (!Node.IsValid() || !DiffTreeView.IsValid())
	{
		return;
	}

	TArray<FDiffTreeNodePtr> Ancestors;

	FDiffTreeNodePtr Parent = Node->Parent.Pin();

	while (Parent.IsValid())
	{
		Ancestors.Add(Parent);
		Parent = Parent->Parent.Pin();
	}

	// Expand from the root downward.
	for (int32 Index = Ancestors.Num() - 1; Index >= 0; --Index)
	{
		DiffTreeView->SetItemExpansion(Ancestors[Index], true);
	}
}

#undef LOCTEXT_NAMESPACE