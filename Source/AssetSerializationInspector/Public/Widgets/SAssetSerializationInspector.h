// Copyright Diego Merayo Merayo. All Rights Reserved
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class STextBlock;
template <typename ItemType> class STreeView;

/**
 * A node displayed in the package structure tree.
 *
 * This is intentionally independent from UObject and package APIs.
 * Later, the package reader will create these nodes.
 */
struct FAssetPackageTreeNode
{
	FText DisplayName;
	FText TypeName;

	int64 Offset = 0;
	int64 Size = 0;

	TArray<uint8> PreviewBytes;
	TArray<TSharedPtr<FAssetPackageTreeNode>> Children;

	static TSharedRef<FAssetPackageTreeNode> Make(const FText& InDisplayName, const FText& InTypeName, const int64 InOffset = 0, const int64 InSize = 0)
	{
		TSharedRef<FAssetPackageTreeNode> Node = MakeShared<FAssetPackageTreeNode>();

		Node->DisplayName = InDisplayName;
		Node->TypeName = InTypeName;
		Node->Offset = InOffset;
		Node->Size = InSize;

		return Node;
	}
};

class SAssetSerializationInspector : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssetSerializationInspector) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	using FTreeNodePtr = TSharedPtr<FAssetPackageTreeNode>;

	// Commands
	FReply HandleInspectClicked();
	FReply HandleClearClicked();

	// Tree delegates
	TSharedRef<ITableRow> GenerateTreeRow(FTreeNodePtr Item, const TSharedRef<STableViewBase>& OwnerTable);

	void GetTreeChildren(FTreeNodePtr Item, TArray<FTreeNodePtr>& OutChildren) const;

	void HandleTreeSelectionChanged(FTreeNodePtr Item, ESelectInfo::Type SelectInfo);

	// Dynamic text
	FText GetSelectedNodeName() const;
	FText GetSelectedNodeType() const;
	FText GetSelectedNodeOffset() const;
	FText GetSelectedNodeSize() const;
	FText GetHexPreviewText() const;
	FText GetStatusText() const;

	// Temporary data setup
	void BuildPlaceholderTree();
	void ClearInspector();

private:
	TSharedPtr<SEditableTextBox> AssetPathTextBox;
	TSharedPtr<STreeView<FTreeNodePtr>> PackageTreeView;

	TArray<FTreeNodePtr> RootNodes;
	FTreeNodePtr SelectedNode;

	FText StatusText;
};