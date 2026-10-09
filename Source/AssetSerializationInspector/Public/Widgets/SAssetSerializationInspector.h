// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

struct FAssetPackageDocument;
struct FAssetPackageIndexReference;
class SEditableTextBox;
class STextBlock;
template <typename ItemType> class STreeView;

enum class EAssetPackageNodeKind : uint8
{
	File,
	Summary,
	Table,
	NameEntry,
	Import,
	Export,
	Field,
	ByteRange,
	Unknown
};

enum class EAssetPackageNavigationTargetKind : uint8
{
	None,
	Import,
	Export
};

struct FAssetPackageNavigationTarget
{
	EAssetPackageNavigationTargetKind Kind = EAssetPackageNavigationTargetKind::None;

	int32 Index = INDEX_NONE;

	bool IsValid() const { return Kind != EAssetPackageNavigationTargetKind::None && Index != INDEX_NONE; }
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

	EAssetPackageNodeKind Kind = EAssetPackageNodeKind::ByteRange;

	int64 Offset = 0;
	int64 Size = 0;

	FAssetPackageNavigationTarget NavigationTarget;
	TWeakPtr<FAssetPackageTreeNode> Parent;
	TArray<TSharedPtr<FAssetPackageTreeNode>> Children;

	static TSharedRef<FAssetPackageTreeNode> Make(
		const FText& InDisplayName, const FText& InTypeName, const int64 InOffset = 0, const int64 InSize = 0, const EAssetPackageNodeKind InKind = EAssetPackageNodeKind::Unknown)
	{
		TSharedRef<FAssetPackageTreeNode> Node = MakeShared<FAssetPackageTreeNode>();

		Node->DisplayName = InDisplayName;
		Node->TypeName = InTypeName;
		Node->Offset = InOffset;
		Node->Size = InSize;
		Node->Kind = InKind;

		return Node;
	}

	bool HasPhysicalRange() const { return Offset >= 0 && Size > 0; }
};

class SAssetSerializationInspector : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssetSerializationInspector) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	using FTreeNodePtr = TSharedPtr<FAssetPackageTreeNode>;

	TSharedRef<SWidget> BuildSelectableDetailRow(const FText& Label, TAttribute<FText> Value);

	FReply HandleBrowseClicked();
	FReply HandleInspectClicked();
	FReply HandleClearClicked();

	bool LoadDocument(const FString& Filename);
	void ClearInspector();

	TSharedRef<ITableRow> GenerateTreeRow(FTreeNodePtr Item, const TSharedRef<STableViewBase>& OwnerTable);

	void GetTreeChildren(FTreeNodePtr Item, TArray<FTreeNodePtr>& OutChildren) const;

	void HandleTreeSelectionChanged(FTreeNodePtr Item, ESelectInfo::Type SelectInfo);

	FText GetSelectedNodeType() const;
	FText GetSelectedNodeOffset() const;
	FText GetHexPreviewText() const;
	FText GetLoadedFilenameText() const;
	FText GetFileSizeText() const;
	FText GetStatusText() const;
	FText GetSelectedNodeValue() const;
	FText GetSelectedNodeRange() const;

	TSharedRef<FAssetPackageTreeNode> MakePackageIndexNode(const FText& Name, const FAssetPackageIndexReference& Reference, const int64 Offset, const int64 Size) const;
	void BuildPackageTree();

	TSharedRef<FAssetPackageTreeNode> MakeRegionNode(const FText& Name, const FText& Type, int64 Offset, int64 Size, EAssetPackageNodeKind Kind = EAssetPackageNodeKind::ByteRange) const;
	TSharedRef<FAssetPackageTreeNode> MakeFieldNode(const FText& Name, const FText& Type, const FText& Value) const;
	TSharedRef<FAssetPackageTreeNode> MakeValueRegionNode(const FText& Name, const FText& Type, const FText& Value, int64 Offset, int64 Size) const;

	// Navigation
	bool NavigateToTarget(const FAssetPackageNavigationTarget& Target);
	void NavigateToNode(const TSharedPtr<FAssetPackageTreeNode>& Node);
	void ExpandAncestors(const TSharedPtr<FAssetPackageTreeNode>& Node);
	FReply HandleNavigateToReferenceClicked();
	EVisibility GetNavigateToReferenceVisibility() const;
	FText GetNavigateToReferenceText() const;
	void HandleTreeItemDoubleClicked(FTreeNodePtr Item);

private:
	TSharedPtr<SEditableTextBox> AssetPathTextBox;
	TSharedPtr<STreeView<FTreeNodePtr>> PackageTreeView;

	TSharedPtr<FAssetPackageDocument> Document;

	TArray<FTreeNodePtr> RootNodes;
	FTreeNodePtr SelectedNode;

	TMap<int32, TWeakPtr<FAssetPackageTreeNode>> ImportNodesByIndex;
	TMap<int32, TWeakPtr<FAssetPackageTreeNode>> ExportNodesByIndex;


	FText StatusText;
};