// Copyright Diego Merayo Merayo. All Rights Reserved
#include "Widgets/SAssetSerializationInspector.h"

#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

#define LOCTEXT_NAMESPACE "SAssetSerializationInspector"

void SAssetSerializationInspector::Construct(const FArguments& InArgs)
{
	StatusText = LOCTEXT("ReadyStatus", "Ready");

	ChildSlot[SNew(SVerticalBox)

		// Toolbar
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f)[SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(
				0.0f, 0.0f, 8.0f, 0.0f)[SAssignNew(AssetPathTextBox, SEditableTextBox).HintText(LOCTEXT("AssetPathHint", "Enter a package path or .uasset filename"))]

			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)[SNew(SButton)
					.Text(LOCTEXT("InspectButton", "Inspect"))
					.ToolTipText(LOCTEXT("InspectButtonTooltip", "Inspect the selected package"))
					.OnClicked(this, &SAssetSerializationInspector::HandleInspectClicked)]

			+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("ClearButton", "Clear")).OnClicked(this, &SAssetSerializationInspector::HandleClearClicked)]]

		+ SVerticalBox::Slot().AutoHeight()[SNew(SSeparator)]

		// Main content
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8.0f)[SNew(SSplitter).Orientation(Orient_Horizontal)

			// Left: package tree
			+ SSplitter::Slot().Value(0.30f)[SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(4.0f)[SNew(SVerticalBox)

						+ SVerticalBox::Slot().AutoHeight().Padding(
							4.0f)[SNew(STextBlock).Text(LOCTEXT("PackageStructureHeading", "Package Structure")).Font(FAppStyle::GetFontStyle("NormalFontBold"))]

						+ SVerticalBox::Slot().FillHeight(1.0f)[SAssignNew(PackageTreeView, STreeView<FTreeNodePtr>)
								.TreeItemsSource(&RootNodes)
								.SelectionMode(ESelectionMode::Single)
								.OnGenerateRow(this, &SAssetSerializationInspector::GenerateTreeRow)
								.OnGetChildren(this, &SAssetSerializationInspector::GetTreeChildren)
								.OnSelectionChanged(this, &SAssetSerializationInspector::HandleTreeSelectionChanged)
								.HeaderRow(SNew(SHeaderRow)

									+ SHeaderRow::Column("Name").DefaultLabel(LOCTEXT("NameColumn", "Name")).FillWidth(0.55f)

									+ SHeaderRow::Column("Offset").DefaultLabel(LOCTEXT("OffsetColumn", "Offset")).FillWidth(0.25f)

									+ SHeaderRow::Column("Size").DefaultLabel(LOCTEXT("SizeColumn", "Size")).FillWidth(0.20f))]]]

			// Right side
			+ SSplitter::Slot().Value(0.70f)[SNew(SSplitter).Orientation(Orient_Vertical)

				// Selection details
				+ SSplitter::Slot().Value(0.35f)[SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(8.0f)[SNew(SVerticalBox)

							+ SVerticalBox::Slot().AutoHeight().Padding(
								0.0f, 0.0f, 0.0f, 8.0f)[SNew(STextBlock).Text(LOCTEXT("SelectionHeading", "Selection Details")).Font(FAppStyle::GetFontStyle("NormalFontBold"))]

							+ SVerticalBox::Slot().AutoHeight().Padding(
								0.0f, 2.0f)[SNew(STextBlock).Text_Lambda([this]() { return FText::Format(LOCTEXT("NameFormat", "Name: {0}"), GetSelectedNodeName()); })]

							+ SVerticalBox::Slot().AutoHeight().Padding(
								0.0f, 2.0f)[SNew(STextBlock).Text_Lambda([this]() { return FText::Format(LOCTEXT("TypeFormat", "Type: {0}"), GetSelectedNodeType()); })]

							+ SVerticalBox::Slot().AutoHeight().Padding(
								0.0f, 2.0f)[SNew(STextBlock).Text_Lambda([this]() { return FText::Format(LOCTEXT("OffsetFormat", "Offset: {0}"), GetSelectedNodeOffset()); })]

							+ SVerticalBox::Slot().AutoHeight().Padding(
								0.0f, 2.0f)[SNew(STextBlock).Text_Lambda([this]() { return FText::Format(LOCTEXT("SizeFormat", "Size: {0}"), GetSelectedNodeSize()); })]]]

				// Hex preview
				+ SSplitter::Slot().Value(0.65f)[SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(8.0f)[SNew(SVerticalBox)

							+ SVerticalBox::Slot().AutoHeight().Padding(
								0.0f, 0.0f, 0.0f, 8.0f)[SNew(STextBlock).Text(LOCTEXT("HexPreviewHeading", "Hex Preview")).Font(FAppStyle::GetFontStyle("NormalFontBold"))]

							+ SVerticalBox::Slot().FillHeight(1.0f)[SNew(SScrollBox)

								+ SScrollBox::Slot()[SNew(STextBlock).Text(this, &SAssetSerializationInspector::GetHexPreviewText).Font(FAppStyle::GetFontStyle("MonoFont")).AutoWrapText(false)]]]]]]

		// Status bar
		+ SVerticalBox::Slot().AutoHeight().Padding(
			8.0f, 0.0f, 8.0f, 8.0f)[SNew(STextBlock).Text(this, &SAssetSerializationInspector::GetStatusText).ColorAndOpacity(FSlateColor::UseSubduedForeground())]];
}

FReply SAssetSerializationInspector::HandleInspectClicked()
{
	const FString RequestedPath = AssetPathTextBox.IsValid() ? AssetPathTextBox->GetText().ToString() : FString();

	if (RequestedPath.IsEmpty())
	{
		StatusText = LOCTEXT("MissingAssetPathStatus", "Enter an asset path before inspecting.");

		return FReply::Handled();
	}

	// Temporary until the package reader exists.
	BuildPlaceholderTree();

	StatusText = FText::Format(LOCTEXT("PlaceholderLoadedStatus", "Displaying placeholder structure for {0}"), FText::FromString(RequestedPath));

	return FReply::Handled();
}

FReply SAssetSerializationInspector::HandleClearClicked()
{
	ClearInspector();
	return FReply::Handled();
}

TSharedRef<ITableRow> SAssetSerializationInspector::GenerateTreeRow(FTreeNodePtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	check(Item.IsValid());

	return SNew(STableRow<FTreeNodePtr>, OwnerTable)[SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().FillWidth(0.55f).VAlign(VAlign_Center).Padding(4.0f, 2.0f)[SNew(STextBlock).Text(Item->DisplayName)]

		+ SHorizontalBox::Slot().FillWidth(0.25f).VAlign(VAlign_Center).Padding(4.0f, 2.0f)[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("0x%llX"), Item->Offset)))]

		+ SHorizontalBox::Slot().FillWidth(0.20f).VAlign(VAlign_Center).Padding(4.0f, 2.0f)[SNew(STextBlock).Text(FText::AsNumber(Item->Size))]];
}

void SAssetSerializationInspector::GetTreeChildren(FTreeNodePtr Item, TArray<FTreeNodePtr>& OutChildren) const
{
	if (Item.IsValid())
	{
		OutChildren.Append(Item->Children);
	}
}

void SAssetSerializationInspector::HandleTreeSelectionChanged(FTreeNodePtr Item, ESelectInfo::Type SelectInfo)
{
	SelectedNode = Item;
}

FText SAssetSerializationInspector::GetSelectedNodeName() const
{
	return SelectedNode.IsValid() ? SelectedNode->DisplayName : LOCTEXT("NoSelectionName", "None");
}

FText SAssetSerializationInspector::GetSelectedNodeType() const
{
	return SelectedNode.IsValid() ? SelectedNode->TypeName : LOCTEXT("NoSelectionType", "None");
}

FText SAssetSerializationInspector::GetSelectedNodeOffset() const
{
	if (!SelectedNode.IsValid())
	{
		return LOCTEXT("NoSelectionOffset", "—");
	}

	return FText::FromString(FString::Printf(TEXT("0x%llX"), SelectedNode->Offset));
}

FText SAssetSerializationInspector::GetSelectedNodeSize() const
{
	if (!SelectedNode.IsValid())
	{
		return LOCTEXT("NoSelectionSize", "—");
	}

	return FText::Format(LOCTEXT("ByteCountFormat", "{0} bytes"), FText::AsNumber(SelectedNode->Size));
}

FText SAssetSerializationInspector::GetHexPreviewText() const
{
	if (!SelectedNode.IsValid())
	{
		return LOCTEXT("NoHexSelection", "Select a package node to display its bytes.");
	}

	if (SelectedNode->PreviewBytes.IsEmpty())
	{
		return LOCTEXT("NoPreviewBytes", "No byte preview is available for this node.");
	}

	FString Result;

	constexpr int32 BytesPerRow = 16;

	for (int32 Index = 0; Index < SelectedNode->PreviewBytes.Num(); Index += BytesPerRow)
	{
		Result += FString::Printf(TEXT("%08llX  "), SelectedNode->Offset + Index);

		for (int32 Column = 0; Column < BytesPerRow; ++Column)
		{
			const int32 ByteIndex = Index + Column;

			if (ByteIndex < SelectedNode->PreviewBytes.Num())
			{
				Result += FString::Printf(TEXT("%02X "), SelectedNode->PreviewBytes[ByteIndex]);
			}
			else
			{
				Result += TEXT("   ");
			}
		}

		Result += TEXT(" ");

		for (int32 Column = 0; Column < BytesPerRow; ++Column)
		{
			const int32 ByteIndex = Index + Column;

			if (ByteIndex >= SelectedNode->PreviewBytes.Num())
			{
				break;
			}

			const uint8 Byte = SelectedNode->PreviewBytes[ByteIndex];

			Result.AppendChar(Byte >= 32 && Byte <= 126 ? static_cast<TCHAR>(Byte) : TEXT('.'));
		}

		Result += LINE_TERMINATOR;
	}

	return FText::FromString(Result);
}

FText SAssetSerializationInspector::GetStatusText() const
{
	return StatusText;
}

void SAssetSerializationInspector::BuildPlaceholderTree()
{
	RootNodes.Reset();
	SelectedNode.Reset();

	TSharedRef<FAssetPackageTreeNode> PackageNode = FAssetPackageTreeNode::Make(LOCTEXT("PackageNode", "Package"), LOCTEXT("PackageType", "Package"));

	TSharedRef<FAssetPackageTreeNode> SummaryNode = FAssetPackageTreeNode::Make(LOCTEXT("SummaryNode", "Summary"), LOCTEXT("SummaryType", "FPackageFileSummary"), 0, 256);

	TSharedRef<FAssetPackageTreeNode> NamesNode = FAssetPackageTreeNode::Make(LOCTEXT("NamesNode", "Name Map"), LOCTEXT("NamesType", "Name Table"), 256, 512);

	TSharedRef<FAssetPackageTreeNode> ImportsNode = FAssetPackageTreeNode::Make(LOCTEXT("ImportsNode", "Imports"), LOCTEXT("ImportsType", "Import Table"), 768, 192);

	TSharedRef<FAssetPackageTreeNode> ExportsNode = FAssetPackageTreeNode::Make(LOCTEXT("ExportsNode", "Exports"), LOCTEXT("ExportsType", "Export Table"), 960, 384);

	SummaryNode->PreviewBytes = { 0xC1, 0x83, 0x2A, 0x9E, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00 };

	PackageNode->Children.Add(SummaryNode);
	PackageNode->Children.Add(NamesNode);
	PackageNode->Children.Add(ImportsNode);
	PackageNode->Children.Add(ExportsNode);

	RootNodes.Add(PackageNode);

	if (PackageTreeView.IsValid())
	{
		PackageTreeView->RequestTreeRefresh();
		PackageTreeView->SetItemExpansion(PackageNode, true);
		PackageTreeView->SetSelection(SummaryNode);
	}
}

void SAssetSerializationInspector::ClearInspector()
{
	RootNodes.Reset();
	SelectedNode.Reset();

	if (AssetPathTextBox.IsValid())
	{
		AssetPathTextBox->SetText(FText::GetEmpty());
	}

	if (PackageTreeView.IsValid())
	{
		PackageTreeView->ClearSelection();
		PackageTreeView->RequestTreeRefresh();
	}

	StatusText = LOCTEXT("ClearedStatus", "Inspector cleared.");
}

#undef LOCTEXT_NAMESPACE