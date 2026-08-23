// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

#include "Diff/AssetPackageDiff.h"

class SCheckBox;
class SEditableTextBox;

template <typename ItemType> class STreeView;

struct FAssetPackageDocument;

/**
 * Slate-friendly representation of a diff entry.
 *
 * FAssetPackageDiffEntry deliberately uses value-owned children because it is
 * a model/result type. Slate's tree widgets are considerably easier to work
 * with using shared pointers, so this is a small view-model layer.
 */
struct FAssetPackageDiffTreeNode
{
	FAssetPackageDiffEntry Diff;

	TWeakPtr<FAssetPackageDiffTreeNode> Parent;
	TArray<TSharedPtr<FAssetPackageDiffTreeNode>> Children;
};

class SAssetSerializationDiff : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssetSerializationDiff) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	using FDiffTreeNodePtr = TSharedPtr<FAssetPackageDiffTreeNode>;

	TSharedRef<SWidget> BuildDetailsPanel(bool bOldSide);

	// File selection
	FReply HandleBrowseOldClicked();
	FReply HandleBrowseNewClicked();
	FReply HandleCompareClicked();

	bool BrowseForAsset(const FText& DialogTitle, FString& OutFilename);
	bool LoadDocument(const FString& Filename, TSharedPtr<FAssetPackageDocument>& OutDocument, FText& OutError);

	// Diff tree
	void RebuildDiffTree();
	FDiffTreeNodePtr BuildDiffTreeNode(const FAssetPackageDiffEntry& Entry, const FDiffTreeNodePtr& Parent);
	bool ShouldIncludeDiffEntry(const FAssetPackageDiffEntry& Entry) const;
	bool HasVisibleChildren(const FAssetPackageDiffEntry& Entry) const;
	void GetDiffTreeChildren(FDiffTreeNodePtr Item, TArray<FDiffTreeNodePtr>& OutChildren) const;
	TSharedRef<ITableRow> GenerateDiffTreeRow(FDiffTreeNodePtr Item, const TSharedRef<STableViewBase>& OwnerTable);
	void HandleDiffSelectionChanged(FDiffTreeNodePtr Item, ESelectInfo::Type SelectInfo);
	void HandleShowUnchangedChanged(ECheckBoxState NewState);

	// Dynamic UI
	FText GetStatusText() const;
	FText GetSummaryText() const;
	FText GetSelectedDisplayName() const;
	FText GetSelectedOldValue() const;
	FText GetSelectedNewValue() const;
	FText GetSelectedOldOffset() const;
	FText GetSelectedNewOffset() const;
	FText GetSelectedOldSize() const;
	FText GetSelectedNewSize() const;
	FText GetSelectedOldHexText() const;
	FText GetSelectedNewHexText() const;
	FText GetSelectedByteComparisonText() const;

	FText BuildHexPreview(const FAssetPackageDocument* Document, int64 Offset, int64 Size, bool bRelativeOffsets) const;

private:
	TSharedPtr<SEditableTextBox> OldFilenameTextBox;
	TSharedPtr<SEditableTextBox> NewFilenameTextBox;

	TSharedPtr<STreeView<FDiffTreeNodePtr>> DiffTreeView;

	TSharedPtr<FAssetPackageDocument> OldDocument;
	TSharedPtr<FAssetPackageDocument> NewDocument;

	TOptional<FAssetPackageDiffResult> DiffResult;

	TArray<FDiffTreeNodePtr> RootDiffNodes;
	FDiffTreeNodePtr SelectedDiffNode;

	bool bShowUnchanged = false;

	FText StatusText;
};