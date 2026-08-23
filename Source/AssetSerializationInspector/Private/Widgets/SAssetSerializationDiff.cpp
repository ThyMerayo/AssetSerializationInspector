// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Widgets/SAssetSerializationDiff.h"

#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SExpanderArrow.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STreeView.h"

#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"

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

		if (ColumnName == TEXT("Old"))
		{
			return SNew(STextBlock).Text(FText::FromString(Diff.OldValue)).ToolTipText(FText::FromString(Diff.OldValue));
		}

		if (ColumnName == TEXT("New"))
		{
			return SNew(STextBlock).Text(FText::FromString(Diff.NewValue)).ToolTipText(FText::FromString(Diff.NewValue));
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
	StatusText = LOCTEXT("ReadyStatus", "Select two .uasset files to compare.");

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
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 6.0f)[SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(this, &SAssetSerializationDiff::GetSummaryText)]

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
							.HeaderRow(SNew(SHeaderRow)

								+ SHeaderRow::Column("Name").DefaultLabel(LOCTEXT("DiffColumn", "Difference")).FillWidth(0.50f)

								+ SHeaderRow::Column("Old").DefaultLabel(LOCTEXT("OldColumn", "Old")).FillWidth(0.25f)

								+ SHeaderRow::Column("New").DefaultLabel(LOCTEXT("NewColumn", "New")).FillWidth(0.25f))]]

			// Details
			+ SSplitter::Slot().Value(0.50f)[SNew(SSplitter).Orientation(Orient_Horizontal)

				+ SSplitter::Slot().Value(0.50f)[SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(8.0f)[BuildDetailsPanel(true)]]

				+ SSplitter::Slot().Value(0.50f)[SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(8.0f)[BuildDetailsPanel(false)]]]]

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 8.0f)[SNew(STextBlock).Text(this, &SAssetSerializationDiff::GetStatusText).ColorAndOpacity(FSlateColor::UseSubduedForeground())]];
}

TSharedRef<SWidget> SAssetSerializationDiff::BuildDetailsPanel(const bool bOldSide)
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(
			0.0f, 0.0f, 0.0f, 8.0f)[SNew(STextBlock).Text(bOldSide ? LOCTEXT("OldDetailsHeading", "Old") : LOCTEXT("NewDetailsHeading", "New")).Font(FAppStyle::GetFontStyle("NormalFontBold"))]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)[SNew(STextBlock).Text_Lambda([this]() { return FText::Format(LOCTEXT("SelectedNameFormat", "Name: {0}"), GetSelectedDisplayName()); })]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)[SNew(STextBlock)
				.Text_Lambda([this, bOldSide]() { return FText::Format(LOCTEXT("SelectedValueFormat", "Value: {0}"), bOldSide ? GetSelectedOldValue() : GetSelectedNewValue()); })
				.AutoWrapText(true)]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)[SNew(STextBlock).Text_Lambda([this, bOldSide]() {
			  return FText::Format(LOCTEXT("SelectedOffsetFormat", "Offset: {0}"), bOldSide ? GetSelectedOldOffset() : GetSelectedNewOffset());
		  })]

		+ SVerticalBox::Slot().AutoHeight().Padding(
			0.0f, 2.0f)[SNew(STextBlock).Text_Lambda([this, bOldSide]() { return FText::Format(LOCTEXT("SelectedSizeFormat", "Size: {0}"), bOldSide ? GetSelectedOldSize() : GetSelectedNewSize()); })]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f)[SNew(SSeparator)]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[SNew(STextBlock).Text(LOCTEXT("HexPreviewHeading", "Hex Preview")).Font(FAppStyle::GetFontStyle("NormalFontBold"))]

		+ SVerticalBox::Slot().FillHeight(1.0f)[SNew(SScrollBox)

			+ SScrollBox::Slot()[SNew(STextBlock)
					.Text_Lambda([this, bOldSide]() { return bOldSide ? GetSelectedOldHexText() : GetSelectedNewHexText(); })
					.Font(FAppStyle::GetFontStyle("MonoFont"))
					.AutoWrapText(false)]];
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

FReply SAssetSerializationDiff::HandleCompareClicked()
{
	const FString OldFilename = OldFilenameTextBox.IsValid() ? OldFilenameTextBox->GetText().ToString().TrimStartAndEnd() : FString();
	const FString NewFilename = NewFilenameTextBox.IsValid() ? NewFilenameTextBox->GetText().ToString().TrimStartAndEnd() : FString();

	if (OldFilename.IsEmpty() || NewFilename.IsEmpty())
	{
		StatusText = LOCTEXT("MissingFilenames", "Select both an old and a new .uasset file.");
		return FReply::Handled();
	}

	FText Error;

	if (!LoadDocument(OldFilename, OldDocument, Error))
	{
		StatusText = FText::Format(LOCTEXT("OldLoadFailed", "Could not load old asset: {0}"), Error);
		return FReply::Handled();
	}

	if (!LoadDocument(NewFilename, NewDocument, Error))
	{
		StatusText = FText::Format(LOCTEXT("NewLoadFailed", "Could not load new asset: {0}"), Error);
		return FReply::Handled();
	}

	DiffResult = FAssetPackageDiff::Compare(*OldDocument, *NewDocument);

	RebuildDiffTree();

	if (DiffResult->bFilesIdentical)
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

	if (!DiffResult.IsSet())
	{
		if (DiffTreeView.IsValid())
		{
			DiffTreeView->RequestTreeRefresh();
		}

		return;
	}

	for (const FAssetPackageDiffEntry& Entry : DiffResult->Entries)
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
}

// Details
FText SAssetSerializationDiff::GetSelectedDisplayName() const
{
	return SelectedDiffNode.IsValid() ? SelectedDiffNode->Diff.DisplayName : FText::FromString(TEXT("—"));
}

FText SAssetSerializationDiff::GetSelectedOldValue() const
{
	if (!SelectedDiffNode.IsValid() || SelectedDiffNode->Diff.OldValue.IsEmpty())
	{
		return FText::FromString(TEXT("—"));
	}

	return FText::FromString(SelectedDiffNode->Diff.OldValue);
}

FText SAssetSerializationDiff::GetSelectedNewValue() const
{
	if (!SelectedDiffNode.IsValid() || SelectedDiffNode->Diff.NewValue.IsEmpty())
	{
		return FText::FromString(TEXT("—"));
	}

	return FText::FromString(SelectedDiffNode->Diff.NewValue);
}

// Offsets
static FText FormatDiffOffset(const int64 Offset)
{
	if (Offset == INDEX_NONE)
	{
		return FText::FromString(TEXT("—"));
	}

	return FText::FromString(FString::Printf(TEXT("0x%llX"), Offset));
}

FText SAssetSerializationDiff::GetSelectedOldOffset() const
{
	return SelectedDiffNode.IsValid() ? FormatDiffOffset(SelectedDiffNode->Diff.OldOffset) : FText::FromString(TEXT("—"));
}

FText SAssetSerializationDiff::GetSelectedNewOffset() const
{
	return SelectedDiffNode.IsValid() ? FormatDiffOffset(SelectedDiffNode->Diff.NewOffset) : FText::FromString(TEXT("—"));
}

// Sizes
FText SAssetSerializationDiff::GetSelectedOldSize() const
{
	if (!SelectedDiffNode.IsValid() || SelectedDiffNode->Diff.OldOffset == INDEX_NONE)
	{
		return FText::FromString(TEXT("—"));
	}

	return FText::Format(LOCTEXT("DiffSizeBytes", "{0} bytes"), FText::AsNumber(SelectedDiffNode->Diff.OldSize));
}

FText SAssetSerializationDiff::GetSelectedNewSize() const
{
	if (!SelectedDiffNode.IsValid() || SelectedDiffNode->Diff.NewOffset == INDEX_NONE)
	{
		return FText::FromString(TEXT("—"));
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
	if (!DiffResult.IsSet())
	{
		return LOCTEXT("NoDiffSummary", "No comparison loaded");
	}

	if (DiffResult->bFilesIdentical)
	{
		return LOCTEXT("IdenticalSummary", "Files are identical");
	}

	FDiffCounts Counts;

	for (const FAssetPackageDiffEntry& Entry : DiffResult->Entries)
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

	if (!OldDocument.IsValid() || !NewDocument.IsValid() || Diff.OldOffset == INDEX_NONE || Diff.NewOffset == INDEX_NONE)
	{
		return FText::GetEmpty();
	}

	if (Diff.OldSize != Diff.NewSize)
	{
		return FText::Format(LOCTEXT("ByteSizesDiffer", "Byte ranges differ in size: {0} vs {1} bytes"), FText::AsNumber(Diff.OldSize), FText::AsNumber(Diff.NewSize));
	}

	if (!OldDocument->IsValidRange(Diff.OldOffset, Diff.OldSize) || !NewDocument->IsValidRange(Diff.NewOffset, Diff.NewSize))
	{
		return FText::GetEmpty();
	}

	const bool bEqual = Diff.OldSize == 0 || FMemory::Memcmp(OldDocument->FileData.GetData() + Diff.OldOffset, NewDocument->FileData.GetData() + Diff.NewOffset, Diff.OldSize) == 0;

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

FText SAssetSerializationDiff::GetSelectedOldHexText() const
{
	if (!SelectedDiffNode.IsValid())
	{
		return LOCTEXT("NoOldHexSelection", "Select a diff node.");
	}

	return BuildHexPreview(OldDocument.Get(), SelectedDiffNode->Diff.OldOffset, SelectedDiffNode->Diff.OldSize, true);
}

FText SAssetSerializationDiff::GetSelectedNewHexText() const
{
	if (!SelectedDiffNode.IsValid())
	{
		return LOCTEXT("NoNewHexSelection", "Select a diff node.");
	}

	return BuildHexPreview(NewDocument.Get(), SelectedDiffNode->Diff.NewOffset, SelectedDiffNode->Diff.NewSize, true);
}

#undef LOCTEXT_NAMESPACE