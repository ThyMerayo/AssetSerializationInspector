// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Widgets/SAssetSerializationDiff.h"

#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
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
#include "Widgets/SSelectableRichText.h"

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
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(Diff.DisplayName)];
		}

		if (ColumnName == TEXT("Old") && Diff.Kind == EAssetPackageDiffKind::Property)
		{
			if (Diff.bHasOldDecodedValue)
			{
				return SNew(SEditableText).Text(FText::FromString(Diff.OldDecodedValue)).IsReadOnly(true);
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
				return SNew(SEditableText).Text(FText::FromString(Diff.NewDecodedValue)).IsReadOnly(true);
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

	StatusText = LOCTEXT("ReadyStatus", "Select two .uasset files to compare.");

	HexDiffStyle = MakeShared<FSlateStyleSet>(TEXT("AssetSerializationHexDiffStyle"));
	FTextBlockStyle NormalStyle = FAppStyle::GetWidgetStyle<FTextBlockStyle>(TEXT("NormalText"));
	NormalStyle.SetFont(FAppStyle::GetFontStyle(TEXT("Sequencer.FixedFont")));
	HexDiffStyle->Set(TEXT("Normal"), NormalStyle);
	FTextBlockStyle ChangedStyle = NormalStyle;
	ChangedStyle.SetColorAndOpacity(FSlateColor(FStyleColors::AccentRed));
	HexDiffStyle->Set(TEXT("Changed"), ChangedStyle);

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
					.IsChecked_Lambda([this]() { return bShowUnchanged ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged(this, &SAssetSerializationDiff::HandleShowUnchangedChanged)[SNew(STextBlock).Text(LOCTEXT("ShowUnchanged", "Show unchanged"))]]]

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 6.0f)[SNew(STextBlock).Text(this, &SAssetSerializationDiff::GetSelectedByteComparisonText)]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8.0f)[SNew(SSplitter).Orientation(Orient_Horizontal)

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
				+ SSplitter::Slot().Value(0.50f)[SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(8.0f)[BuildDetailsPanel(false)]]]]

		// TODO: This needs to be made dynamic, so the Analysis data is loaded after it is ready
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 8.0f)[SNew(SExpandableArea)
				.AreaTitle(LOCTEXT("SaveAnalysis", "Save Analysis"))
				.InitiallyCollapsed(false)
				.BodyContent()[SAssignNew(SaveAnalysisBox, SBox)[SNew(STextBlock).Text(LOCTEXT("NoSaveAnalysis", "No save analysis is available."))]]]

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 8.0f)[SNew(STextBlock).Text(this, &SAssetSerializationDiff::GetStatusText).ColorAndOpacity(FSlateColor::UseSubduedForeground())]];

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

		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)[BuildSelectableDetailRow(
			LOCTEXT("SelectedValueLabel", "Value:"), TAttribute<FText>::CreateLambda([this, bOldSide]() { return bOldSide ? GetSelectedOldValue() : GetSelectedNewValue(); }))]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)[BuildSelectableDetailRow(
			LOCTEXT("SelectedOffsetFormat", "Offset:"), TAttribute<FText>::CreateLambda([this, bOldSide]() { return bOldSide ? GetSelectedOldOffset() : GetSelectedNewOffset(); }))]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)[BuildSelectableDetailRow(
			LOCTEXT("SelectedSizeFormat", "Size:"), TAttribute<FText>::CreateLambda([this, bOldSide]() { return bOldSide ? GetSelectedOldSize() : GetSelectedNewSize(); }))]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f)[SNew(SSeparator)]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				.VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("HexPreviewHeading", "Hex Preview")).Font(FAppStyle::GetFontStyle("NormalFontBold"))]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SButton).Text(LOCTEXT("CopyHex", "Copy")).OnClicked_Lambda([this, bOldSide]() {
				  const FString Text = (bOldSide ? GetSelectedOldHexPlainText() : GetSelectedNewHexPlainText()).ToString();
				  FPlatformApplicationMisc::ClipboardCopy(*Text);
				  return FReply::Handled();
			  })]]

		+ SVerticalBox::Slot().FillHeight(1.0f)[SNew(SScrollBox)

			+ SScrollBox::Slot()[SNew(SSelectableRichText)
					.RichText_Lambda([this, bOldSide]() { return bOldSide ? GetSelectedOldHexRichText() : GetSelectedNewHexRichText(); })
					.PlainText_Lambda([this, bOldSide]() { return bOldSide ? GetSelectedOldHexPlainText() : GetSelectedNewHexPlainText(); })
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
	if (SaveAnalysisBox.IsValid())
	{
		TSharedRef<SWidget> NewWidget = BuildSaveAnalysisWidget();
		SaveAnalysisBox->SetContent(NewWidget);
		SaveAnalysisBox->Invalidate(EInvalidateWidgetReason::Layout);
	}
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
		+ SVerticalBox::Slot().AutoHeight()[BuildSaveAnalysisSection(LOCTEXT("MeaningfulChangesSection", "Meaningful Changes"), Analysis.SemanticChanges)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)[BuildSaveAnalysisSection(LOCTEXT("LayoutChangesSection", "Layout / Serialization"), Analysis.LayoutChanges)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)[BuildSaveAnalysisSection(LOCTEXT("UnexplainedChangesSection", "Unexplained"), Analysis.UnexplainedChanges)];
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

TSharedRef<SWidget> SAssetSerializationDiff::BuildSaveAnalysisSection(const FText& Title, const TArray<FAssetSaveExplanationEntry>& Entries)
{
	return SNew(SExpandableArea).AreaTitle(Title).InitiallyCollapsed(false).BodyContent()[BuildSaveAnalysisEntries(Entries, 0)];
}

TSharedRef<SWidget> SAssetSerializationDiff::BuildSaveAnalysisEntries(const TArray<FAssetSaveExplanationEntry>& Entries, const int32 Depth)
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	if (Entries.IsEmpty())
	{
		Box->AddSlot().AutoHeight().Padding(8.0f, 4.0f)[SNew(STextBlock).Text(LOCTEXT("NoAnalysisEntries", "None"))];
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

void SAssetSerializationDiff::NavigateToDiffEntry(const FString& Key)
{
	FDiffTreeNodePtr Node = FindDiffTreeNodeByKey(RootDiffNodes, Key);

	if (!Node.IsValid() || !DiffTreeView.IsValid())
	{
		return;
	}

	ExpandDiffAncestors(Node);
	DiffTreeView->SetSelection(Node, ESelectInfo::Direct);
	DiffTreeView->RequestScrollIntoView(Node);
}

SAssetSerializationDiff::FDiffTreeNodePtr SAssetSerializationDiff::FindDiffTreeNodeByKey(const TArray<FDiffTreeNodePtr>& Nodes, const FString& Key) const
{
	for (const FDiffTreeNodePtr& Node : Nodes)
	{
		if (!Node.IsValid())
		{
			continue;
		}

		if (Node->Diff.Key == Key)
		{
			return Node;
		}

		FDiffTreeNodePtr Found = FindDiffTreeNodeByKey(Node->Children, Key);

		if (Found.IsValid())
		{
			return Found;
		}
	}

	return nullptr;
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
		ParentWindowHandle, DialogTitle.ToString(), FPaths::ProjectContentDir(), TEXT(""), TEXT("Unreal Asset (*.uasset)|*.uasset"), EFileDialogFlags::None, SelectedFiles);

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

bool SAssetSerializationDiff::LoadDocument(const FString& Filename, TSharedPtr<FAssetPackageDocument>& OutDocument, FText& OutError)
{
	OutDocument = FAssetPackageReader::LoadFromFile(Filename, OutError);
	return OutDocument.IsValid();
}

// Test for Tracing the serialization of UTexture2D
// #include "Serialization/BufferArchive.h"
// #include "Serialization/ObjectAndNameAsStringProxyArchive.h"

FReply SAssetSerializationDiff::HandleCompareClicked()
{
	// 	{
	// 		if (auto Object = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/AICON-Red.AICON-Red")))
	// 		{
	// 			FBufferArchive Buffer;
	// 			FObjectAndNameAsStringProxyArchive UObjectArchive(Buffer, false);
	// 			FAssetSerializationTraceArchive TraceArchive(UObjectArchive, 0);
	// 			Object->Serialize(TraceArchive);
	// 			FAssetSerializationTrace Trace = AssetSerializationTrace::BuildSerializationTrace(Object, Buffer.Num(), TraceArchive.GetEvents());
	//
	// 			for (const FAssetSerializationTraceEvent& Event : TraceArchive.GetEvents())
	// 			{
	// 				UE_LOG(LogTemp, Log, TEXT("%08llX  %8lld  %-50s  %s"), Event.Offset, Event.Size, *Event.PropertyPath, *Event.PropertyType);
	// 			}
	// 		}
	// 	}

	const FString OldFilename = OldFilenameTextBox.IsValid() ? OldFilenameTextBox->GetText().ToString().TrimStartAndEnd() : FString();
	const FString NewFilename = NewFilenameTextBox.IsValid() ? NewFilenameTextBox->GetText().ToString().TrimStartAndEnd() : FString();

	if (OldFilename.IsEmpty() || NewFilename.IsEmpty())
	{
		StatusText = LOCTEXT("MissingFilenames", "Select both an old and a new .uasset file.");
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

	DiffSession->DiffResult = FAssetPackageDiff::Compare(*DiffSession->Old.Document.Get(), *DiffSession->New.Document.Get(), DiffSession->Old.Traces.Get(), DiffSession->New.Traces.Get());

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
		if (!ShouldIncludeDiffEntry(Entry))
		{
			continue;
		}

		FDiffTreeNodePtr Node = BuildDiffTreeNode(Entry, nullptr);

		if (Node.IsValid())
		{
			RootDiffNodes.Add(Node);
		}
	}

	if (DiffTreeView.IsValid())
	{
		DiffTreeView->RequestTreeRefresh();

		for (const FDiffTreeNodePtr& Node : RootDiffNodes)
		{
			DiffTreeView->SetItemExpansion(Node, true);
		}
	}
}

SAssetSerializationDiff::FDiffTreeNodePtr SAssetSerializationDiff::BuildDiffTreeNode(const FAssetPackageDiffEntry& Entry, const FDiffTreeNodePtr& Parent)
{
	if (!ShouldIncludeDiffEntry(Entry))
	{
		return nullptr;
	}

	FDiffTreeNodePtr Node = MakeShared<FAssetPackageDiffTreeNode>();

	Node->Diff = Entry;
	Node->Parent = Parent;

	for (const FAssetPackageDiffEntry& Child : Entry.Children)
	{
		FDiffTreeNodePtr ChildNode = BuildDiffTreeNode(Child, Node);

		if (ChildNode.IsValid())
		{
			Node->Children.Add(ChildNode);
		}
	}

	return Node;
}

bool SAssetSerializationDiff::HasVisibleChildren(const FAssetPackageDiffEntry& Entry) const
{
	for (const FAssetPackageDiffEntry& Child : Entry.Children)
	{
		if (ShouldIncludeDiffEntry(Child))
		{
			return true;
		}
	}

	return false;
}

bool SAssetSerializationDiff::ShouldIncludeDiffEntry(const FAssetPackageDiffEntry& Entry) const
{
	if (bShowUnchanged)
	{
		return true;
	}

	if (Entry.State != EAssetPackageDiffState::Unchanged)
	{
		return true;
	}

	return HasVisibleChildren(Entry);
}

void SAssetSerializationDiff::HandleShowUnchangedChanged(const ECheckBoxState NewState)
{
	bShowUnchanged = NewState == ECheckBoxState::Checked;

	RebuildDiffTree();
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

	OldHexPreview = BuildHighlightedHexPreview(DiffSession->Old.Document.Get(), Diff.OldOffset, Diff.OldSize, SelectedByteDiffSpans);
	NewHexPreview = BuildHighlightedHexPreview(DiffSession->New.Document.Get(), Diff.NewOffset, Diff.NewSize, SelectedByteDiffSpans);
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

			Span.OldFieldPath = FAssetPackageDiff::BuildTracePath(Node);
		}

		if (NewTrace != nullptr && NewTrace->Root.IsValid())
		{
			const FAssetSerializationTraceNode* Node = AssetSerializationTrace::FindDeepestTraceNode(NewTrace->Root, Span.Offset, Span.Size);

			Span.NewFieldPath = FAssetPackageDiff::BuildTracePath(Node);
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

	// 	Side.Traces = MakeShared<FAssetPackageTraceCollection>();
	//
	// 	for (const FAssetPackageExportEntry& Export : Side.Document->ExportMap)
	// 	{
	// 		const int32 ExportIndex = Export.Index;
	//
	// 		// For our current TEST tracing approach, we need to resolve
	// 		// this export to an actual loaded UObject.
	// 		UObject* Object = FindObjectForExport(*Side.Document, Export);
	//
	// 		if (Object == nullptr)
	// 		{
	// 			continue;
	// 		}
	//
	// 		FBufferArchive Buffer;
	//
	// 		FObjectAndNameAsStringProxyArchive UObjectArchive(Buffer, false);
	//
	// 		FAssetSerializationTraceArchive TraceArchive(UObjectArchive, 0);
	//
	// 		Object->Serialize(TraceArchive);
	//
	// 		FAssetSerializationTrace Trace = BuildSerializationTrace(Object, Buffer.Num(), TraceArchive.GetEvents());
	//
	// 		// HERE:
	// 		Side.Traces->ExportTraces.Add(ExportIndex, MoveTemp(Trace));
	// 	}

	return true;
}

// Details
FText SAssetSerializationDiff::GetSelectedDisplayName() const
{
	return SelectedDiffNode.IsValid() ? SelectedDiffNode->Diff.DisplayName : FText::FromString(TEXT("-"));
}

FText SAssetSerializationDiff::GetSelectedOldValue() const
{
	if (!SelectedDiffNode.IsValid() || SelectedDiffNode->Diff.OldValue.IsEmpty())
	{
		return FText::FromString(TEXT("-"));
	}

	return FText::FromString(SelectedDiffNode->Diff.OldValue);
}

FText SAssetSerializationDiff::GetSelectedNewValue() const
{
	if (!SelectedDiffNode.IsValid() || SelectedDiffNode->Diff.NewValue.IsEmpty())
	{
		return FText::FromString(TEXT("-"));
	}

	return FText::FromString(SelectedDiffNode->Diff.NewValue);
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

FText SAssetSerializationDiff::GetSelectedOldOffset() const
{
	return SelectedDiffNode.IsValid() ? FormatDiffOffset(SelectedDiffNode->Diff.OldOffset) : FText::FromString(TEXT("-"));
}

FText SAssetSerializationDiff::GetSelectedNewOffset() const
{
	return SelectedDiffNode.IsValid() ? FormatDiffOffset(SelectedDiffNode->Diff.NewOffset) : FText::FromString(TEXT("-"));
}

// Sizes
FText SAssetSerializationDiff::GetSelectedOldSize() const
{
	if (!SelectedDiffNode.IsValid() || SelectedDiffNode->Diff.OldOffset == INDEX_NONE)
	{
		return FText::FromString(TEXT("-"));
	}

	return FText::Format(LOCTEXT("DiffSizeBytes", "{0} bytes"), FText::AsNumber(SelectedDiffNode->Diff.OldSize));
}

FText SAssetSerializationDiff::GetSelectedNewSize() const
{
	if (!SelectedDiffNode.IsValid() || SelectedDiffNode->Diff.NewOffset == INDEX_NONE)
	{
		return FText::FromString(TEXT("-"));
	}

	return FText::Format(LOCTEXT("DiffSizeBytes", "{0} bytes"), FText::AsNumber(SelectedDiffNode->Diff.NewSize));
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

FText SAssetSerializationDiff::GetComparisonTitle() const
{
	if (!DiffSession.IsValid() || !DiffSession->New.Document.IsValid())
	{
		return LOCTEXT("ManualComparison", "Asset Comparison");
	}

	return FText::FromString(FPaths::GetBaseFilename(DiffSession->New.Document->Filename));
}

FText SAssetSerializationDiff::BuildHexPreview(const FAssetPackageDocument* Document, const int64 Offset, const int64 Size, bool bRelativeOffsets) const
{
	if (Document == nullptr)
	{
		return LOCTEXT("NoHexDocument", "No document loaded.");
	}

	if (Offset == INDEX_NONE)
	{
		return LOCTEXT("NoHexRange", "No byte range is available.");
	}

	if (Size < 0 || !Document->IsValidRange(Offset, Size))
	{
		return LOCTEXT("InvalidHexRange", "The selected byte range is invalid.");
	}

	if (Size == 0)
	{
		return LOCTEXT("EmptyHexRange", "The selected byte range is empty.");
	}

	constexpr int64 MaximumPreviewBytes = 4096;
	constexpr int32 BytesPerRow = 16;

	const int64 PreviewSize = FMath::Min<int64>(Size, MaximumPreviewBytes);

	FString Result;

	for (int64 RelativeOffset = 0; RelativeOffset < PreviewSize; RelativeOffset += BytesPerRow)
	{
		const int64 AbsoluteOffset = Offset + RelativeOffset;
		const int64 DisplayOffset = bRelativeOffsets ? RelativeOffset : AbsoluteOffset;

		Result += FString::Printf(TEXT("%08llX  "), DisplayOffset);

		for (int32 Column = 0; Column < BytesPerRow; ++Column)
		{
			const int64 ByteIndex = RelativeOffset + Column;

			if (ByteIndex < PreviewSize)
			{
				const uint8 Byte = Document->FileData[Offset + ByteIndex];

				Result += FString::Printf(TEXT("%02X "), Byte);
			}
			else
			{
				Result += TEXT("   ");
			}

			if (Column == 7)
			{
				Result += TEXT(" ");
			}
		}

		Result += TEXT(" |");

		for (int32 Column = 0; Column < BytesPerRow; ++Column)
		{
			const int64 ByteIndex = RelativeOffset + Column;

			if (ByteIndex >= PreviewSize)
			{
				break;
			}

			const uint8 Byte = Document->FileData[Offset + ByteIndex];

			Result.AppendChar(Byte >= 32 && Byte <= 126 ? static_cast<TCHAR>(Byte) : TEXT('.'));
		}

		Result += TEXT("|");
		Result += LINE_TERMINATOR;
	}

	if (Size > PreviewSize)
	{
		Result += LINE_TERMINATOR;
		Result += FString::Printf(TEXT("Preview limited to %lld of %lld bytes."), PreviewSize, Size);
	}

	return FText::FromString(MoveTemp(Result));
}

FText SAssetSerializationDiff::GetSelectedOldHexRichText() const
{
	return OldHexPreview.Rich;
}

FText SAssetSerializationDiff::GetSelectedOldHexPlainText() const
{
	return OldHexPreview.Plain;
}

FText SAssetSerializationDiff::GetSelectedNewHexRichText() const
{
	return NewHexPreview.Rich;
}

FText SAssetSerializationDiff::GetSelectedNewHexPlainText() const
{
	return NewHexPreview.Plain;
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

FHexPreviewText SAssetSerializationDiff::BuildHighlightedHexPreview(const FAssetPackageDocument* Document, const int64 Offset, const int64 Size, const TArray<FAssetByteDiffSpan>& Spans) const
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

	for (int64 RowOffset = 0; RowOffset < PreviewSize; RowOffset += BytesPerRow)
	{
		// Relative offsets are much better for side-by-side diffing.
		const FString OffsetText = FString::Printf(TEXT("%08llX  "), RowOffset);
		RichResult += OffsetText;
		PlainResult += OffsetText;

		// HEX
		bool bMarkupOpen = false;

		for (int32 Column = 0; Column < BytesPerRow; ++Column)
		{
			const int64 RelativeOffset = RowOffset + Column;

			if (RelativeOffset >= PreviewSize)
			{
				break;
			}

			const bool bChanged = IsByteDifferent(RelativeOffset, Spans);

			if (bChanged && !bMarkupOpen)
			{
				RichResult += TEXT("<Changed>");
				bMarkupOpen = true;
			}
			else if (!bChanged && bMarkupOpen)
			{
				RichResult += TEXT("</>");
				bMarkupOpen = false;
			}

			const uint8 Byte = Document->FileData[Offset + RelativeOffset];

			const FString ByteText = FString::Printf(TEXT("%02X "), Byte);
			RichResult += ByteText;
			PlainResult += ByteText;

			if (Column == 7)
			{
				RichResult += TEXT(" ");
			}
		}

		if (bMarkupOpen)
		{
			RichResult += TEXT("</>");
		}

		// ASCII
		RichResult += TEXT(" |");
		PlainResult += TEXT(" |");

		bMarkupOpen = false;

		for (int32 Column = 0; Column < BytesPerRow; ++Column)
		{
			const int64 RelativeOffset = RowOffset + Column;

			if (RelativeOffset >= PreviewSize)
			{
				break;
			}

			const bool bChanged = IsByteDifferent(RelativeOffset, Spans);

			if (bChanged && !bMarkupOpen)
			{
				RichResult += TEXT("<Changed>");
				bMarkupOpen = true;
			}
			else if (!bChanged && bMarkupOpen)
			{
				RichResult += TEXT("</>");
				bMarkupOpen = false;
			}

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

		if (bMarkupOpen)
		{
			RichResult += TEXT("</>");
		}

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
			ExpandDiffAncestors(Found);

			DiffTreeView->SetSelection(Found, ESelectInfo::Direct);
			DiffTreeView->RequestScrollIntoView(Found);

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