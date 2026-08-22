// Copyright Diego Merayo Merayo. All Rights Reserved
#include "Widgets/SAssetSerializationInspector.h"

#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
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

#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"

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
					.Text(LOCTEXT("BrowseButton", "Browse..."))
					.ToolTipText(LOCTEXT("BrowseButtonTooltip", "Select a .uasset file from disk"))
					.OnClicked(this, &SAssetSerializationInspector::HandleBrowseClicked)]

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

							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)[SNew(STextBlock)
									.Text_Lambda([this]() { return FText::Format(LOCTEXT("FilenameFormat", "File: {0}"), GetLoadedFilenameText()); })
									.ToolTipText(this, &SAssetSerializationInspector::GetLoadedFilenameText)]

							+ SVerticalBox::Slot().AutoHeight().Padding(
								0.0f, 2.0f)[SNew(STextBlock).Text_Lambda([this]() { return FText::Format(LOCTEXT("FileSizeFormat", "Size: {0}"), GetFileSizeText()); })]

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

FReply SAssetSerializationInspector::HandleBrowseClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();

	if (DesktopPlatform == nullptr)
	{
		StatusText = LOCTEXT("DesktopPlatformUnavailable", "The desktop file-dialog service is unavailable.");

		return FReply::Handled();
	}

	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

	FString DefaultPath = FPaths::ProjectContentDir();

	if (Document.IsValid())
	{
		DefaultPath = FPaths::GetPath(Document->Filename);
	}
	else if (AssetPathTextBox.IsValid())
	{
		const FString ExistingPath = AssetPathTextBox->GetText().ToString();

		if (!ExistingPath.IsEmpty())
		{
			DefaultPath = FPaths::GetPath(FPaths::ConvertRelativePathToFull(ExistingPath));
		}
	}

	TArray<FString> SelectedFiles;

	const bool bSelectedFile = DesktopPlatform->OpenFileDialog(
		ParentWindowHandle, LOCTEXT("OpenAssetDialogTitle", "Select Unreal Asset").ToString(), DefaultPath, TEXT(""), TEXT("Unreal Asset (*.uasset)|*.uasset"), EFileDialogFlags::None, SelectedFiles);

	if (!bSelectedFile || SelectedFiles.IsEmpty())
	{
		return FReply::Handled();
	}

	const FString& SelectedFilename = SelectedFiles[0];

	if (AssetPathTextBox.IsValid())
	{
		AssetPathTextBox->SetText(FText::FromString(SelectedFilename));
	}

	LoadDocument(SelectedFilename);

	return FReply::Handled();
}

FReply SAssetSerializationInspector::HandleInspectClicked()
{
	if (!AssetPathTextBox.IsValid())
	{
		StatusText = LOCTEXT("PathControlUnavailable", "The asset-path control is unavailable.");

		return FReply::Handled();
	}

	const FString RequestedPath = AssetPathTextBox->GetText().ToString().TrimStartAndEnd();

	LoadDocument(RequestedPath);

	return FReply::Handled();
}

FReply SAssetSerializationInspector::HandleClearClicked()
{
	ClearInspector();
	return FReply::Handled();
}

bool SAssetSerializationInspector::LoadDocument(const FString& Filename)
{
	FText LoadError;

	TSharedPtr<FAssetPackageDocument> LoadedDocument = FAssetPackageReader::LoadFromFile(Filename, LoadError);

	if (!LoadedDocument.IsValid())
	{
		Document.Reset();
		RootNodes.Reset();
		SelectedNode.Reset();

		if (PackageTreeView.IsValid())
		{
			PackageTreeView->ClearSelection();
			PackageTreeView->RequestTreeRefresh();
		}

		StatusText = LoadError;

		return false;
	}

	Document = MoveTemp(LoadedDocument);

	if (AssetPathTextBox.IsValid())
	{
		AssetPathTextBox->SetText(FText::FromString(Document->Filename));
	}

	BuildRawDocumentTree();

	StatusText = FText::Format(LOCTEXT("LoadedFileStatus", "Loaded {0} ({1} bytes)."), FText::FromString(FPaths::GetCleanFilename(Document->Filename)), FText::AsNumber(Document->GetFileSize()));

	return true;
}

void SAssetSerializationInspector::ClearInspector()
{
	Document.Reset();
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

void SAssetSerializationInspector::BuildRawDocumentTree()
{
	RootNodes.Reset();
	SelectedNode.Reset();

	if (!Document.IsValid())
	{
		return;
	}

	TSharedRef<FAssetPackageTreeNode> FileNode =
		FAssetPackageTreeNode::Make(FText::FromString(FPaths::GetCleanFilename(Document->Filename)), LOCTEXT("RawPackageFileType", "Raw package file"), 0, Document->GetFileSize());

	RootNodes.Add(FileNode);

	if (PackageTreeView.IsValid())
	{
		PackageTreeView->RequestTreeRefresh();
		PackageTreeView->SetSelection(FileNode);
	}
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
		return LOCTEXT("NoSelectionOffset", "-");
	}

	return FText::FromString(FString::Printf(TEXT("0x%llX"), SelectedNode->Offset));
}

FText SAssetSerializationInspector::GetSelectedNodeSize() const
{
	if (!SelectedNode.IsValid())
	{
		return LOCTEXT("NoSelectionSize", "-");
	}

	return FText::Format(LOCTEXT("ByteCountFormat", "{0} bytes"), FText::AsNumber(SelectedNode->Size));
}

FText SAssetSerializationInspector::GetHexPreviewText() const
{
	if (!Document.IsValid())
	{
		return LOCTEXT("NoDocumentForHex", "Load a .uasset file to display its bytes.");
	}

	if (!SelectedNode.IsValid())
	{
		return LOCTEXT("NoHexSelection", "Select a package node to display its bytes.");
	}

	if (!Document->IsValidRange(SelectedNode->Offset, SelectedNode->Size))
	{
		return LOCTEXT("InvalidByteRange", "The selected node refers to an invalid byte range.");
	}

	if (SelectedNode->Size == 0)
	{
		return LOCTEXT("EmptyByteRange", "The selected byte range is empty.");
	}

	// Avoid constructing an enormous text object when the root node represents
	// a multi-megabyte asset. A virtualized hex widget will replace this later.
	constexpr int64 MaximumPreviewBytes = 4096;

	const int64 PreviewSize = FMath::Min(SelectedNode->Size, MaximumPreviewBytes);

	constexpr int32 BytesPerRow = 16;

	FString Result;

	// Approximate reservation:
	// 16 offset characters + hex + ASCII + spacing per line.
	const int64 EstimatedLineCount = (PreviewSize + BytesPerRow - 1) / BytesPerRow;

	Result.Reserve(static_cast<int32>(FMath::Min<int64>(EstimatedLineCount * 80, MAX_int32)));

	for (int64 RelativeOffset = 0; RelativeOffset < PreviewSize; RelativeOffset += BytesPerRow)
	{
		const int64 AbsoluteOffset = SelectedNode->Offset + RelativeOffset;

		Result += FString::Printf(TEXT("%016llX  "), AbsoluteOffset);

		for (int32 Column = 0; Column < BytesPerRow; ++Column)
		{
			const int64 ByteOffset = RelativeOffset + Column;

			if (ByteOffset < PreviewSize)
			{
				const uint8 Byte = Document->FileData[SelectedNode->Offset + ByteOffset];

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
			const int64 ByteOffset = RelativeOffset + Column;

			if (ByteOffset >= PreviewSize)
			{
				break;
			}

			const uint8 Byte = Document->FileData[SelectedNode->Offset + ByteOffset];

			const TCHAR Character = Byte >= 32 && Byte <= 126 ? static_cast<TCHAR>(Byte) : TEXT('.');

			Result.AppendChar(Character);
		}

		Result += TEXT("|");
		Result += LINE_TERMINATOR;
	}

	if (SelectedNode->Size > PreviewSize)
	{
		Result += LINE_TERMINATOR;

		Result += FString::Printf(TEXT("Preview limited to %lld of %lld bytes."), PreviewSize, SelectedNode->Size);
	}

	return FText::FromString(MoveTemp(Result));
}

FText SAssetSerializationInspector::GetLoadedFilenameText() const
{
	if (!Document.IsValid())
	{
		return LOCTEXT("NoLoadedFilename", "No file loaded");
	}

	return FText::FromString(Document->Filename);
}

FText SAssetSerializationInspector::GetFileSizeText() const
{
	if (!Document.IsValid())
	{
		return LOCTEXT("NoLoadedFileSize", "-");
	}

	return FText::Format(LOCTEXT("LoadedFileSizeFormat", "{0} bytes"), FText::AsNumber(Document->GetFileSize()));
}

FText SAssetSerializationInspector::GetStatusText() const
{
	return StatusText;
}

#undef LOCTEXT_NAMESPACE