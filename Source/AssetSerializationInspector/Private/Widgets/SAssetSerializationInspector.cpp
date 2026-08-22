// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Widgets/SAssetSerializationInspector.h"

#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SExpanderArrow.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"

#define LOCTEXT_NAMESPACE "SAssetSerializationInspector"

namespace
{
	struct FPackageRegionDescriptor
	{
		FText Name;
		FText Type;
		int64 Offset = 0;
		int32 EntryCount = INDEX_NONE;
	};

	class SAssetPackageTreeRow : public SMultiColumnTableRow<TSharedPtr<FAssetPackageTreeNode>>
	{
	public:
		SLATE_BEGIN_ARGS(SAssetPackageTreeRow) {}
		SLATE_ARGUMENT(TSharedPtr<FAssetPackageTreeNode>, Item)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
		{
			Item = InArgs._Item;

			SMultiColumnTableRow<TSharedPtr<FAssetPackageTreeNode>>::Construct(FSuperRowType::FArguments(), OwnerTable);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
		{
			if (!Item.IsValid())
			{
				return SNullWidget::NullWidget;
			}

			if (ColumnName == TEXT("Name"))
			{
				return SNew(SHorizontalBox)

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SExpanderArrow, SharedThis(this)).IndentAmount(16.0f)]

					+ SHorizontalBox::Slot()
						  .AutoWidth()
						  .VAlign(VAlign_Center)
						  .Padding(2.0f, 0.0f, 5.0f, 0.0f)[SNew(SBox).WidthOverride(16.0f).HeightOverride(
							  16.0f)[SNew(SImage).Image(this, &SAssetPackageTreeRow::GetIconBrush).ColorAndOpacity(this, &SAssetPackageTreeRow::GetIconColor)]]

					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text(Item->DisplayName).ToolTipText(Item->TypeName)];
			}

			if (ColumnName == TEXT("Offset"))
			{
				if (Item->Kind == EAssetPackageNodeKind::Field)
				{
					return SNew(STextBlock).Text(FText::FromString(TEXT("-")));
				}

				return SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("0x%llX"), Item->Offset)));
			}

			if (ColumnName == TEXT("Size"))
			{
				if (!Item->Children.IsEmpty() && Item->Kind == EAssetPackageNodeKind::Table)
				{
					return SNew(STextBlock).Text(FText::Format(NSLOCTEXT("AssetPackageTree", "ChildCountFormat", "{0} entries"), FText::AsNumber(Item->Children.Num())));
				}

				if (!Item->ValueText.IsEmpty())
				{
					return SNew(STextBlock).Text(Item->ValueText);
				}

				if (Item->HasPhysicalRange())
				{
					return SNew(STextBlock).Text(FText::AsNumber(Item->Size));
				}

				return SNew(STextBlock).Text(FText::FromString(TEXT("—")));
			}

			return SNullWidget::NullWidget;
		}

		const FSlateBrush* GetIconBrush() const
		{
			if (!Item.IsValid())
			{
				return FAppStyle::GetBrush("Icons.Help");
			}

			if (Item.IsValid() && Item->NavigationTarget.IsValid())
			{
				return FAppStyle::GetBrush("Icons.ArrowRight");
			}

			switch (Item->Kind)
			{
				case EAssetPackageNodeKind::File:
					return FAppStyle::GetBrush("Icons.FolderOpen");

				case EAssetPackageNodeKind::Summary:
					return FAppStyle::GetBrush("Icons.Info");

				case EAssetPackageNodeKind::Table:
					return FAppStyle::GetBrush("Icons.FolderOpen");

				case EAssetPackageNodeKind::NameEntry:
					return FAppStyle::GetBrush("Icons.Tag");

				case EAssetPackageNodeKind::Import:
					return FAppStyle::GetBrush("Icons.ArrowRight");

				case EAssetPackageNodeKind::Export:
					return FAppStyle::GetBrush("Icons.ArrowLeft");

				case EAssetPackageNodeKind::Field:
					return FAppStyle::GetBrush("Icons.FilledCircle");

				case EAssetPackageNodeKind::ByteRange:
					return FAppStyle::GetBrush("Icons.Info");

				default:
					return FAppStyle::GetBrush("Icons.Help");
			}
		}

		FSlateColor GetIconColor() const { return FSlateColor::UseForeground(); }

	private:
		TSharedPtr<FAssetPackageTreeNode> Item;
	};

	void AddChild(const TSharedRef<FAssetPackageTreeNode>& Parent, const TSharedRef<FAssetPackageTreeNode>& Child)
	{
		Child->Parent = Parent;
		Parent->Children.Add(Child);
	}
} // namespace

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
								.OnMouseButtonDoubleClick(this, &SAssetSerializationInspector::HandleTreeItemDoubleClicked)
								.HeaderRow(SNew(SHeaderRow)

									+ SHeaderRow::Column("Name").DefaultLabel(LOCTEXT("NameColumn", "Name")).FillWidth(0.55f)

									+ SHeaderRow::Column("Offset").DefaultLabel(LOCTEXT("OffsetColumn", "Offset")).FillWidth(0.25f)

									+ SHeaderRow::Column("Size").DefaultLabel(LOCTEXT("SizeOrValueColumn", "Size / Value")).FillWidth(0.25f))]]]

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
								0.0f, 2.0f)[SNew(STextBlock).Text_Lambda([this]() { return FText::Format(LOCTEXT("ValueFormat", "Value: {0}"), GetSelectedNodeValue()); })]

							+ SVerticalBox::Slot().AutoHeight().Padding(
								0.0f, 2.0f)[SNew(STextBlock).Text_Lambda([this]() { return FText::Format(LOCTEXT("RangeFormat", "Range: {0}"), GetSelectedNodeRange()); })]

							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)[SNew(SButton)
									.Visibility(this, &SAssetSerializationInspector::GetNavigateToReferenceVisibility)
									.Text(this, &SAssetSerializationInspector::GetNavigateToReferenceText)
									.OnClicked(this, &SAssetSerializationInspector::HandleNavigateToReferenceClicked)]]]

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

	BuildPackageTree();

	if (Document->bHasDecodedNameMap)
	{
		StatusText = FText::Format(LOCTEXT("LoadedFileWithNamesStatus", "Loaded {0} ({1} bytes, {2} names)."), FText::FromString(FPaths::GetCleanFilename(Document->Filename)),
			FText::AsNumber(Document->GetFileSize()), FText::AsNumber(Document->NameMap.Num()));
	}
	else
	{
		StatusText = FText::Format(
			LOCTEXT("LoadedFileNameWarningStatus", "Loaded {0}, but the Name Map could not be decoded: {1}"), FText::FromString(FPaths::GetCleanFilename(Document->Filename)), Document->NameMapError);
	}

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

TSharedRef<ITableRow> SAssetSerializationInspector::GenerateTreeRow(FTreeNodePtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SAssetPackageTreeRow, OwnerTable).Item(Item);
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

	if (!SelectedNode->HasPhysicalRange())
	{
		return FText::Format(LOCTEXT("LogicalNodeHexMessage",
								 "{0}\n\n"
								 "This is a logical value. Its exact serialized byte range "
								 "has not been captured."),
			GetSelectedNodeValue());
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

FText SAssetSerializationInspector::GetSelectedNodeValue() const
{
	if (!SelectedNode.IsValid() || SelectedNode->ValueText.IsEmpty())
	{
		return LOCTEXT("NoSelectedValue", "-");
	}

	return SelectedNode->ValueText;
}

FText SAssetSerializationInspector::GetSelectedNodeRange() const
{
	if (!SelectedNode.IsValid())
	{
		return LOCTEXT("NoSelectedRange", "-");
	}

	if (!SelectedNode->HasPhysicalRange())
	{
		return LOCTEXT("LogicalFieldRange", "Logical value; exact byte range not captured");
	}

	if (!Document.IsValid() || !Document->IsValidRange(SelectedNode->Offset, SelectedNode->Size))
	{
		return LOCTEXT("InvalidSelectedRange", "Invalid");
	}

	return FText::FromString(FString::Printf(TEXT("0x%llX - 0x%llX"), SelectedNode->Offset, SelectedNode->Offset + SelectedNode->Size));
}

TSharedRef<FAssetPackageTreeNode> SAssetSerializationInspector::MakePackageIndexNode(const FText& Name, const FAssetPackageIndexReference& Reference, const int64 Offset, const int64 Size) const
{
	TSharedRef<FAssetPackageTreeNode> Node =
		MakeValueRegionNode(Name, LOCTEXT("PackageIndexType", "FPackageIndex"), FText::FromString(Document->DescribePackageIndexDetailed(Reference)), Offset, Size);

	switch (Reference.GetKind())
	{
		case EAssetPackageIndexKind::Import:
			Node->NavigationTarget.Kind = EAssetPackageNavigationTargetKind::Import;
			Node->NavigationTarget.Index = Reference.GetArrayIndex();
			break;

		case EAssetPackageIndexKind::Export:
			Node->NavigationTarget.Kind = EAssetPackageNavigationTargetKind::Export;
			Node->NavigationTarget.Index = Reference.GetArrayIndex();
			break;

		default:
			break;
	}

	return Node;
}

void SAssetSerializationInspector::BuildPackageTree()
{
	RootNodes.Reset();
	SelectedNode.Reset();

	ImportNodesByIndex.Reset();
	ExportNodesByIndex.Reset();

	if (!Document.IsValid() || !Document->bHasValidPackageSummary)
	{
		return;
	}

	const FPackageFileSummary& Summary = Document->PackageSummary;

	TSharedRef<FAssetPackageTreeNode> FileNode = FAssetPackageTreeNode::Make(
		FText::FromString(FPaths::GetCleanFilename(Document->Filename)), LOCTEXT("PackageFileType", "Unreal package file"), 0, Document->GetFileSize(), EAssetPackageNodeKind::File);

	RootNodes.Add(FileNode);

	// The exact serialized size comes from Archive::Tell(), not sizeof().
	TSharedRef<FAssetPackageTreeNode> SummaryNode =
		MakeRegionNode(LOCTEXT("PackageSummaryNode", "Package Summary"), LOCTEXT("PackageSummaryType", "FPackageFileSummary"), 0, Document->SerializedSummarySize, EAssetPackageNodeKind::Summary);

	AddChild(FileNode, SummaryNode);

	// Logical fields. We know their interpreted values, but we are not yet
	// claiming exact byte ranges for every individual field.
	AddChild(SummaryNode, MakeFieldNode(LOCTEXT("TagField", "Tag"), LOCTEXT("Int32Type", "int32"), FText::FromString(FString::Printf(TEXT("0x%08X"), static_cast<uint32>(Summary.Tag)))));
	AddChild(SummaryNode, MakeFieldNode(LOCTEXT("PackageNameField", "Package name"), LOCTEXT("FStringType", "FString"), FText::FromString(Summary.PackageName)));
	AddChild(SummaryNode, MakeFieldNode(LOCTEXT("TotalHeaderSizeField", "Total header size"), LOCTEXT("Int32Type", "int32"), FText::AsNumber(Summary.TotalHeaderSize)));
	AddChild(SummaryNode, MakeFieldNode(LOCTEXT("PackageFlagsField", "Package flags"), LOCTEXT("Uint32Type", "uint32"), FText::FromString(FString::Printf(TEXT("0x%08X"), Summary.GetPackageFlags()))));
	AddChild(SummaryNode, MakeFieldNode(LOCTEXT("NameCountField", "Name count"), LOCTEXT("Int32Type", "int32"), FText::AsNumber(Summary.NameCount)));
	AddChild(SummaryNode, MakeFieldNode(LOCTEXT("ImportCountField", "Import count"), LOCTEXT("Int32Type", "int32"), FText::AsNumber(Summary.ImportCount)));
	AddChild(SummaryNode, MakeFieldNode(LOCTEXT("ExportCountField", "Export count"), LOCTEXT("Int32Type", "int32"), FText::AsNumber(Summary.ExportCount)));

	TArray<FPackageRegionDescriptor> Regions;

	auto AddRegion = [&Regions](const FText& Name, const FText& Type, const int64 Offset, const int32 Count = INDEX_NONE) {
		if (Offset <= 0)
		{
			return;
		}

		FPackageRegionDescriptor& Region = Regions.AddDefaulted_GetRef();

		Region.Name = Name;
		Region.Type = Type;
		Region.Offset = Offset;
		Region.EntryCount = Count;
	};

	AddRegion(LOCTEXT("NameMapRegion", "Name Map"), LOCTEXT("NameMapRegionType", "Name entries"), Summary.NameOffset, Summary.NameCount);
	AddRegion(LOCTEXT("SoftObjectPathsRegion", "Soft Object Paths"), LOCTEXT("SoftObjectPathsRegionType", "Soft object path entries"), Summary.SoftObjectPathsOffset, Summary.SoftObjectPathsCount);
	AddRegion(
		LOCTEXT("GatherableTextRegion", "Gatherable Text Data"), LOCTEXT("GatherableTextRegionType", "Gatherable text entries"), Summary.GatherableTextDataOffset, Summary.GatherableTextDataCount);
	AddRegion(LOCTEXT("ImportMapRegion", "Import Map"), LOCTEXT("ImportMapRegionType", "FObjectImport entries"), Summary.ImportOffset, Summary.ImportCount);
	AddRegion(LOCTEXT("ExportMapRegion", "Export Map"), LOCTEXT("ExportMapRegionType", "FObjectExport entries"), Summary.ExportOffset, Summary.ExportCount);
	AddRegion(LOCTEXT("DependsMapRegion", "Depends Map"), LOCTEXT("DependsMapRegionType", "Export dependencies"), Summary.DependsOffset);
	AddRegion(LOCTEXT("SoftPackageReferencesRegion", "Soft Package References"), LOCTEXT("SoftPackageReferencesRegionType", "Soft package reference entries"), Summary.SoftPackageReferencesOffset,
		Summary.SoftPackageReferencesCount);
	AddRegion(LOCTEXT("SearchableNamesRegion", "Searchable Names"), LOCTEXT("SearchableNamesRegionType", "Searchable name entries"), Summary.SearchableNamesOffset);
	AddRegion(LOCTEXT("AssetRegistryDataRegion", "Asset Registry Data"), LOCTEXT("AssetRegistryDataRegionType", "Asset registry payload"), Summary.AssetRegistryDataOffset);
	AddRegion(LOCTEXT("WorldTileInfoRegion", "World Tile Info"), LOCTEXT("WorldTileInfoRegionType", "World tile information"), Summary.WorldTileInfoDataOffset);
	AddRegion(LOCTEXT("ThumbnailTableRegion", "Thumbnail Table"), LOCTEXT("ThumbnailTableRegionType", "Thumbnail metadata"), Summary.ThumbnailTableOffset);

	Regions.Sort([](const FPackageRegionDescriptor& Left, const FPackageRegionDescriptor& Right) { return Left.Offset < Right.Offset; });

	const int64 HeaderBoundary = Summary.TotalHeaderSize > 0 && Summary.TotalHeaderSize <= Document->GetFileSize() ? Summary.TotalHeaderSize : Document->GetFileSize();

	TSharedPtr<FAssetPackageTreeNode> NameMapNode;
	TSharedPtr<FAssetPackageTreeNode> ImportMapNode;
	TSharedPtr<FAssetPackageTreeNode> ExportMapNode;

	for (int32 Index = 0; Index < Regions.Num(); ++Index)
	{
		const FPackageRegionDescriptor& Region = Regions[Index];

		if (Region.Offset < 0 || Region.Offset >= Document->GetFileSize())
		{
			continue;
		}

		int64 RegionEnd = HeaderBoundary;

		for (int32 NextIndex = Index + 1; NextIndex < Regions.Num(); ++NextIndex)
		{
			if (Regions[NextIndex].Offset > Region.Offset)
			{
				RegionEnd = Regions[NextIndex].Offset;
				break;
			}
		}

		RegionEnd = FMath::Clamp<int64>(RegionEnd, Region.Offset, Document->GetFileSize());

		const int64 RegionSize = RegionEnd - Region.Offset;

		TSharedRef<FAssetPackageTreeNode> RegionNode = MakeRegionNode(Region.Name, Region.Type, Region.Offset, RegionSize, EAssetPackageNodeKind::NameEntry);

		if (Region.Offset == Summary.NameOffset)
		{
			RegionNode->Kind = EAssetPackageNodeKind::Table;
			NameMapNode = RegionNode;
		}
		else if (Region.Offset == Summary.ImportOffset)
		{
			RegionNode->Kind = EAssetPackageNodeKind::Table;
			ImportMapNode = RegionNode;
		}
		else if (Region.Offset == Summary.ExportOffset)
		{
			RegionNode->Kind = EAssetPackageNodeKind::Table;
			ExportMapNode = RegionNode;
		}

		if (Region.EntryCount != INDEX_NONE)
		{
			AddChild(RegionNode, MakeFieldNode(LOCTEXT("EntryCountField", "Entry count"), LOCTEXT("Int32Type", "int32"), FText::AsNumber(Region.EntryCount)));
		}

		AddChild(FileNode, RegionNode);
	}

	if (NameMapNode.IsValid())
	{
		if (Document->bHasDecodedNameMap)
		{
			for (const FAssetPackageNameEntry& Entry : Document->NameMap)
			{
				TSharedRef<FAssetPackageTreeNode> EntryNode = MakeRegionNode(FText::Format(LOCTEXT("NameEntryLabel", "[{0}] {1}"), FText::AsNumber(Entry.Index), FText::FromString(Entry.Name)),
					LOCTEXT("NameEntryType", "Serialized package name"), Entry.Offset, Entry.Size, EAssetPackageNodeKind::NameEntry);

				EntryNode->ValueText = FText::FromString(Entry.Name);

				EntryNode->Children.Add(MakeFieldNode(LOCTEXT("NameIndexField", "Index"), LOCTEXT("Int32Type", "int32"), FText::AsNumber(Entry.Index)));

				EntryNode->Children.Add(MakeFieldNode(LOCTEXT("NameStringField", "String"), LOCTEXT("FStringType", "FString"), FText::FromString(Entry.Name)));

				EntryNode->Children.Add(MakeFieldNode(
					LOCTEXT("NonCaseHashField", "Non-case-preserving hash"), LOCTEXT("Uint16Type", "uint16"), FText::FromString(FString::Printf(TEXT("0x%04X"), Entry.NonCasePreservingHash))));

				EntryNode->Children.Add(
					MakeFieldNode(LOCTEXT("CaseHashField", "Case-preserving hash"), LOCTEXT("Uint16Type", "uint16"), FText::FromString(FString::Printf(TEXT("0x%04X"), Entry.CasePreservingHash))));

				NameMapNode->Children.Add(EntryNode);
			}
		}
		else
		{
			TSharedRef<FAssetPackageTreeNode> ErrorNode = MakeFieldNode(LOCTEXT("NameMapDecodeErrorNode", "Decode error"), LOCTEXT("ErrorType", "Error"), Document->NameMapError);

			NameMapNode->Children.Add(ErrorNode);
		}
	}

	if (ImportMapNode.IsValid())
	{
		if (Document->bHasDecodedImportMap)
		{
			for (const FAssetPackageImportEntry& Import : Document->ImportMap)
			{
				const FString ObjectName = Document->ResolveNameReference(Import.ObjectName);
				const FString ResolvedPath = Document->ResolveImportPath(Import.Index);
				const FString ClassPackage = Document->ResolveNameReference(Import.ClassPackage);
				const FString ClassName = Document->ResolveNameReference(Import.ClassName);

				TSharedRef<FAssetPackageTreeNode> ImportNode = MakeRegionNode(FText::Format(LOCTEXT("ImportEntryLabel", "[{0}] {1}"), FText::AsNumber(Import.Index), FText::FromString(ResolvedPath)),
					LOCTEXT("ImportEntryType", "FObjectImport"), Import.Offset, Import.Size, EAssetPackageNodeKind::Import);

				ImportNodesByIndex.Add(Import.Index, ImportNode);

				ImportNode->ValueText = FText::FromString(ResolvedPath);
				ImportNode->Children.Insert(MakeFieldNode(LOCTEXT("ImportResolvedPathField", "Resolved path"), LOCTEXT("ObjectPathType", "Object path"), FText::FromString(ResolvedPath)), 0);

				int64 FieldOffset = Import.Offset;

				AddChild(ImportNode,
					MakeValueRegionNode(LOCTEXT("ImportClassPackageField", "Class package"), LOCTEXT("PackageNameReferenceType", "Package FName"), FText::FromString(ClassPackage), FieldOffset, 8));

				FieldOffset += 8;

				AddChild(
					ImportNode, MakeValueRegionNode(LOCTEXT("ImportClassNameField", "Class name"), LOCTEXT("PackageNameReferenceType", "Package FName"), FText::FromString(ClassName), FieldOffset, 8));

				FieldOffset += 8;

				AddChild(ImportNode, MakePackageIndexNode(LOCTEXT("ImportOuterIndexField", "Outer index"), Import.OuterIndex, FieldOffset, 4));

				FieldOffset += 4;

				AddChild(ImportNode,
					MakeValueRegionNode(LOCTEXT("ImportObjectNameField", "Object name"), LOCTEXT("PackageNameReferenceType", "Package FName"), FText::FromString(ObjectName), FieldOffset, 8));

				if (Import.UndecodedTailSize > 0)
				{
					AddChild(ImportNode,
						MakeRegionNode(LOCTEXT("ImportEntryTail", "Version-dependent tail"), LOCTEXT("ImportEntryTailType", "Undecoded FObjectImport data"), Import.UndecodedTailOffset,
							Import.UndecodedTailSize));
				}

				AddChild(ImportMapNode.ToSharedRef(), ImportNode);
			}
		}
		else
		{
			AddChild(ImportMapNode.ToSharedRef(), MakeFieldNode(LOCTEXT("ImportMapDecodeError", "Decode error"), LOCTEXT("ErrorType", "Error"), Document->ImportMapError));
		}
	}

	if (ExportMapNode.IsValid())
	{
		if (Document->bHasDecodedExportMap)
		{
			for (const FAssetPackageExportEntry& Export : Document->ExportMap)
			{
				const FString ResolvedPath = Document->ResolveExportPath(Export.Index);

				const FString ClassIndex = Document->DescribePackageIndexDetailed(Export.ClassIndex);
				const FString SuperIndex = Document->DescribePackageIndexDetailed(Export.SuperIndex);
				const FString TemplateIndex = Document->DescribePackageIndexDetailed(Export.TemplateIndex);
				const FString OuterIndex = Document->DescribePackageIndexDetailed(Export.OuterIndex);

				TSharedRef<FAssetPackageTreeNode> ExportNode = MakeRegionNode(FText::Format(LOCTEXT("ExportEntryLabel", "[{0}] {1}"), FText::AsNumber(Export.Index), FText::FromString(ResolvedPath)),
					LOCTEXT("ExportEntryType", "FObjectExport"), Export.Offset, Export.Size, EAssetPackageNodeKind::Export);
				ExportNode->ValueText = FText::FromString(ResolvedPath);

				ExportNodesByIndex.Add(Export.Index, ExportNode);

				int64 FieldOffset = Export.Offset;
				AddChild(ExportNode, MakePackageIndexNode(LOCTEXT("ExportClassIndexField", "Class index"), Export.ClassIndex, FieldOffset, 4));
				FieldOffset += 4;
				AddChild(ExportNode, MakePackageIndexNode(LOCTEXT("ExportSuperIndexField", "Super index"), Export.SuperIndex, FieldOffset, 4));
				FieldOffset += 4;
				AddChild(ExportNode, MakePackageIndexNode(LOCTEXT("ExportTemplateIndexField", "Template index"), Export.TemplateIndex, FieldOffset, 4));
				FieldOffset += 4;
				AddChild(ExportNode, MakePackageIndexNode(LOCTEXT("ExportOuterIndexField", "Outer index"), Export.OuterIndex, FieldOffset, 4));
				FieldOffset += 4;
				AddChild(ExportNode,
					MakeValueRegionNode(LOCTEXT("ExportObjectNameField", "Object name"), LOCTEXT("PackageFNameType", "Package FName"),
						FText::FromString(Document->ResolveNameReference(Export.ObjectName)), FieldOffset, 8));
				FieldOffset += 8;
				AddChild(ExportNode,
					MakeValueRegionNode(LOCTEXT("ExportObjectFlagsField", "Object flags"), LOCTEXT("EObjectFlagsType", "EObjectFlags"),
						FText::FromString(FString::Printf(TEXT("0x%08X"), Export.ObjectFlags)), FieldOffset, 4));
				FieldOffset += 4;
				AddChild(ExportNode, MakeValueRegionNode(LOCTEXT("ExportSerialSizeField", "Serial size"), LOCTEXT("Int64Type", "int64"), FText::AsNumber(Export.SerialSize), FieldOffset, 8));
				FieldOffset += 8;
				AddChild(ExportNode,
					MakeValueRegionNode(
						LOCTEXT("ExportSerialOffsetField", "Serial offset"), LOCTEXT("Int64Type", "int64"), FText::FromString(FString::Printf(TEXT("0x%llX"), Export.SerialOffset)), FieldOffset, 8));

				if (Export.SerialSize > 0)
				{
					if (Document->IsValidExportPayload(Export))
					{
						AddChild(ExportNode,
							MakeRegionNode(LOCTEXT("ExportSerializedPayloadField", "Serialized Payload"), LOCTEXT("SerializedUObjectPayload", "Serialized UObject data"), Export.SerialOffset,
								Export.SerialSize, EAssetPackageNodeKind::ByteRange));
					}
					else
					{
						AddChild(ExportNode,
							MakeFieldNode(
								LOCTEXT("InvalidExportPayload", "Payload error"), LOCTEXT("ErrorType", "Error"), LOCTEXT("InvalidExportPayloadValue", "Serial offset/size is outside the file")));
					}
				}

				if (Export.UndecodedTailSize > 0)
				{
					AddChild(ExportNode,
						MakeRegionNode(LOCTEXT("ExportVersionTail", "Version-dependent tail"), LOCTEXT("ExportVersionTailType", "Undecoded FObjectExport data"), Export.UndecodedTailOffset,
							Export.UndecodedTailSize, EAssetPackageNodeKind::ByteRange));
				}

				AddChild(ExportMapNode.ToSharedRef(), ExportNode);
			}
		}
		else
		{
			AddChild(ExportMapNode.ToSharedRef(), MakeFieldNode(LOCTEXT("ExportDecodeError", "Decode error"), LOCTEXT("ErrorType", "Error"), Document->ExportMapError));
		}
	}

	if (Summary.TotalHeaderSize > 0 && Summary.TotalHeaderSize < Document->GetFileSize())
	{
		AddChild(FileNode,
			MakeRegionNode(LOCTEXT("PackagePayloadRegion", "Package Payload"), LOCTEXT("PackagePayloadRegionType", "Export and bulk payload data"), Summary.TotalHeaderSize,
				Document->GetFileSize() - Summary.TotalHeaderSize));
	}

	if (PackageTreeView.IsValid())
	{
		PackageTreeView->RequestTreeRefresh();
		PackageTreeView->SetItemExpansion(FileNode, true);
		PackageTreeView->SetItemExpansion(SummaryNode, true);
		PackageTreeView->SetSelection(SummaryNode);
	}

	if (NameMapNode.IsValid() && Document->bHasDecodedNameMap && Document->DecodedNameMapEnd < Document->NameMapRegionEnd)
	{
		const int64 TrailingOffset = Document->DecodedNameMapEnd;

		const int64 TrailingSize = Document->NameMapRegionEnd - Document->DecodedNameMapEnd;

		if (Document->IsValidRange(TrailingOffset, TrailingSize))
		{
			AddChild(NameMapNode.ToSharedRef(),
				MakeRegionNode(LOCTEXT("NameMapTrailingBytes", "Trailing / Undecoded Bytes"), LOCTEXT("UnknownNameMapData", "Unknown or aligned data"), TrailingOffset, TrailingSize));
		}
	}
}

TSharedRef<FAssetPackageTreeNode> SAssetSerializationInspector::MakeRegionNode(
	const FText& Name, const FText& Type, const int64 Offset, const int64 Size, EAssetPackageNodeKind Kind /*= EAssetPackageNodeKind::ByteRange*/) const
{
	return FAssetPackageTreeNode::Make(Name, Type, Offset, Size, Kind);
}

TSharedRef<FAssetPackageTreeNode> SAssetSerializationInspector::MakeFieldNode(const FText& Name, const FText& Type, const FText& Value) const
{
	TSharedRef<FAssetPackageTreeNode> Node = FAssetPackageTreeNode::Make(Name, Type, 0, 0, EAssetPackageNodeKind::Field);
	Node->ValueText = Value;

	return Node;
}

TSharedRef<FAssetPackageTreeNode> SAssetSerializationInspector::MakeValueRegionNode(const FText& Name, const FText& Type, const FText& Value, const int64 Offset, const int64 Size) const
{
	TSharedRef<FAssetPackageTreeNode> Node = FAssetPackageTreeNode::Make(Name, Type, Offset, Size, EAssetPackageNodeKind::Field);
	Node->ValueText = Value;

	return Node;
}

bool SAssetSerializationInspector::NavigateToTarget(const FAssetPackageNavigationTarget& Target)
{
	if (!Target.IsValid())
	{
		return false;
	}

	TSharedPtr<FAssetPackageTreeNode> TargetNode;

	switch (Target.Kind)
	{
		case EAssetPackageNavigationTargetKind::Import:
		{
			const TWeakPtr<FAssetPackageTreeNode>* Found = ImportNodesByIndex.Find(Target.Index);
			if (Found != nullptr)
			{
				TargetNode = Found->Pin();
			}

			break;
		}

		case EAssetPackageNavigationTargetKind::Export:
		{
			const TWeakPtr<FAssetPackageTreeNode>* Found = ExportNodesByIndex.Find(Target.Index);
			if (Found != nullptr)
			{
				TargetNode = Found->Pin();
			}

			break;
		}

		default:
			break;
	}

	if (!TargetNode.IsValid())
	{
		return false;
	}

	NavigateToNode(TargetNode);

	return true;
}

void SAssetSerializationInspector::NavigateToNode(const TSharedPtr<FAssetPackageTreeNode>& Node)
{
	if (!Node.IsValid() || !PackageTreeView.IsValid())
	{
		return;
	}

	if (!bIsApplyingNavigationHistory && SelectedNode.IsValid() && SelectedNode != Node)
	{
		if (NavigationHistoryIndex + 1 < NavigationHistory.Num())
		{
			NavigationHistory.SetNum(NavigationHistoryIndex + 1);
		}

		NavigationHistory.Add(SelectedNode);
		NavigationHistoryIndex = NavigationHistory.Num() - 1;
	}

	ExpandAncestors(Node);

	PackageTreeView->SetSelection(Node, ESelectInfo::Direct);

	PackageTreeView->RequestScrollIntoView(Node);
}

void SAssetSerializationInspector::ExpandAncestors(const TSharedPtr<FAssetPackageTreeNode>& Node)
{
	if (!Node.IsValid() || !PackageTreeView.IsValid())
	{
		return;
	}

	TArray<TSharedPtr<FAssetPackageTreeNode>> Ancestors;

	TSharedPtr<FAssetPackageTreeNode> Parent = Node->Parent.Pin();

	while (Parent.IsValid())
	{
		Ancestors.Add(Parent);
		Parent = Parent->Parent.Pin();
	}

	// Optional, but makes the operation read naturally:
	// root -> ... -> immediate parent.
	for (int32 Index = Ancestors.Num() - 1; Index >= 0; --Index)
	{
		PackageTreeView->SetItemExpansion(Ancestors[Index], true);
	}
}

EVisibility SAssetSerializationInspector::GetNavigateToReferenceVisibility() const
{
	return SelectedNode.IsValid() && SelectedNode->NavigationTarget.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SAssetSerializationInspector::HandleNavigateToReferenceClicked()
{
	if (SelectedNode.IsValid())
	{
		NavigateToTarget(SelectedNode->NavigationTarget);
	}

	return FReply::Handled();
}

FText SAssetSerializationInspector::GetNavigateToReferenceText() const
{
	if (!SelectedNode.IsValid())
	{
		return LOCTEXT("GoToReference", "Go to Reference");
	}

	const FAssetPackageNavigationTarget& Target = SelectedNode->NavigationTarget;

	switch (Target.Kind)
	{
		case EAssetPackageNavigationTargetKind::Import:
			return FText::Format(LOCTEXT("GoToImportFormat", "Go to Import [{0}]"), FText::AsNumber(Target.Index));

		case EAssetPackageNavigationTargetKind::Export:
			return FText::Format(LOCTEXT("GoToExportFormat", "Go to Export [{0}]"), FText::AsNumber(Target.Index));

		default:
			return LOCTEXT("GoToReference", "Go to Reference");
	}
}

void SAssetSerializationInspector::HandleTreeItemDoubleClicked(FTreeNodePtr Item)
{
	if (!Item.IsValid())
	{
		return;
	}

	if (Item->NavigationTarget.IsValid())
	{
		NavigateToTarget(Item->NavigationTarget);
		return;
	}

	if (!Item->Children.IsEmpty() && PackageTreeView.IsValid())
	{
		PackageTreeView->SetItemExpansion(Item, !PackageTreeView->IsItemExpanded(Item));
	}
}

#undef LOCTEXT_NAMESPACE