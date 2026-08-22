// Copyright Diego Merayo Merayo. All Rights Reserved
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

struct FAssetPackageDocument;
class SEditableTextBox;
class STextBlock;
template <typename ItemType> class STreeView;

enum class EAssetPackageNodeKind : uint8
{
	File,
	Region,
	Group,
	Field
};
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
	FText ValueText;

	EAssetPackageNodeKind Kind = EAssetPackageNodeKind::Region;

	int64 Offset = 0;
	int64 Size = 0;

	TArray<TSharedPtr<FAssetPackageTreeNode>> Children;

	static TSharedRef<FAssetPackageTreeNode> Make(
		const FText& InDisplayName, const FText& InTypeName, const int64 InOffset = 0, const int64 InSize = 0, const EAssetPackageNodeKind InKind = EAssetPackageNodeKind::Region)
	{
		TSharedRef<FAssetPackageTreeNode> Node = MakeShared<FAssetPackageTreeNode>();

		Node->DisplayName = InDisplayName;
		Node->TypeName = InTypeName;
		Node->Offset = InOffset;
		Node->Size = InSize;
		Node->Kind = InKind;

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

	FReply HandleBrowseClicked();
	FReply HandleInspectClicked();
	FReply HandleClearClicked();

	bool LoadDocument(const FString& Filename);
	void ClearInspector();

	TSharedRef<ITableRow> GenerateTreeRow(FTreeNodePtr Item, const TSharedRef<STableViewBase>& OwnerTable);

	void GetTreeChildren(FTreeNodePtr Item, TArray<FTreeNodePtr>& OutChildren) const;

	void HandleTreeSelectionChanged(FTreeNodePtr Item, ESelectInfo::Type SelectInfo);

	FText GetSelectedNodeName() const;
	FText GetSelectedNodeType() const;
	FText GetSelectedNodeOffset() const;
	FText GetSelectedNodeSize() const;
	FText GetHexPreviewText() const;
	FText GetLoadedFilenameText() const;
	FText GetFileSizeText() const;
	FText GetStatusText() const;
	FText GetSelectedNodeValue() const;
	FText GetSelectedNodeRange() const;

	void BuildPackageTree();

	TSharedRef<FAssetPackageTreeNode> MakeRegionNode(const FText& Name, const FText& Type, int64 Offset, int64 Size) const;
	TSharedRef<FAssetPackageTreeNode> MakeFieldNode(const FText& Name, const FText& Type, const FText& Value) const;

private:
	TSharedPtr<SEditableTextBox> AssetPathTextBox;
	TSharedPtr<STreeView<FTreeNodePtr>> PackageTreeView;

	TSharedPtr<FAssetPackageDocument> Document;

	TArray<FTreeNodePtr> RootNodes;
	FTreeNodePtr SelectedNode;

	FText StatusText;
};