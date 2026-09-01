// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

#include "Diff/AssetPackageDiff.h"
#include "Trace/AssetSerializationTrace.h"

class SCheckBox;
class SEditableTextBox;

template <typename ItemType> class STreeView;

struct FAssetPackageDocument;

struct FHexPreviewText
{
	FText Rich;
	FText Plain;
};

enum class EAssetByteDiffState : uint8
{
	Unchanged,
	Modified,
	Added,
	Removed
};

struct FAssetPackageTraceCollection
{
	TMap<int32, FAssetSerializationTrace> ExportTraces;

	const FAssetSerializationTrace* FindExportTrace(const int32 ExportIndex) const { return ExportTraces.Find(ExportIndex); }
};

struct FAssetSerializationDiffSide
{
	TSharedPtr<FAssetPackageDocument> Document;
	TSharedPtr<FAssetPackageTraceCollection> Traces;
};

struct FAssetSerializationDiffSession
{
	FAssetSerializationDiffSide Old;
	FAssetSerializationDiffSide New;

	TOptional<FAssetPackageDiffResult> DiffResult;
};

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
	using FDiffTreeNodePtr = TSharedPtr<FAssetPackageDiffTreeNode>;

	SLATE_BEGIN_ARGS(SAssetSerializationDiff) {}
	SLATE_ARGUMENT(TSharedPtr<FAssetSerializationDiffSession>, Session)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetSession(TSharedPtr<FAssetSerializationDiffSession> InSession);

private:
	TSharedRef<SWidget> BuildDetailsPanel(bool bOldSide);
	TSharedRef<SWidget> BuildSelectableDetailRow(const FText& Label, TAttribute<FText> Value);

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
	void HandleShowUnchangedChanged(ECheckBoxState NewState);
	void HandleDiffSelectionChanged(FDiffTreeNodePtr Item, ESelectInfo::Type SelectInfo);
	void AnnotateSelectedDiffSpans();
	const FAssetSerializationTrace* FindTraceForDiffEntry(const FAssetPackageDiffEntry& Diff, const bool bOldSide) const;
	bool BuildTracesForSide(FAssetSerializationDiffSide& Side);

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
	FText GetSelectedOldHexRichText() const;
	FText GetSelectedOldHexPlainText() const;
	FText GetSelectedNewHexRichText() const;
	FText GetSelectedNewHexPlainText() const;
	FText GetSelectedByteComparisonText() const;
	int64 GetSelectedChangedByteCount() const;
	FText GetComparisonTitle() const;
	FText GetChangeClassificationText() const;

	FText BuildHexPreview(const FAssetPackageDocument* Document, int64 Offset, int64 Size, bool bRelativeOffsets) const;

	TArray<FAssetByteDiffSpan> BuildByteDiffSpans(const FAssetPackageDocument* OldDoc, int64 OldOffset, int64 OldSize, const FAssetPackageDocument* NewDoc, int64 NewOffset, int64 NewSize) const;
	bool IsByteDifferent(int64 RelativeOffset, const TArray<FAssetByteDiffSpan>& Spans) const;
	FHexPreviewText BuildHighlightedHexPreview(const FAssetPackageDocument* Document, int64 Offset, int64 Size, const TArray<FAssetByteDiffSpan>& Spans) const;

	void LoadSessionIntoUI();
	void SelectFirstMeaningfulDifference();
	void ExpandDiffAncestors(const FDiffTreeNodePtr& Node);

private:
	FHexPreviewText OldHexPreview;
	FHexPreviewText NewHexPreview;

	TSharedPtr<SEditableTextBox> OldFilenameTextBox;
	TSharedPtr<SEditableTextBox> NewFilenameTextBox;

	TSharedPtr<STreeView<FDiffTreeNodePtr>> DiffTreeView;

	TSharedPtr<FAssetSerializationDiffSession> DiffSession;

	TArray<FDiffTreeNodePtr> RootDiffNodes;
	FDiffTreeNodePtr SelectedDiffNode;

	TArray<FAssetByteDiffSpan> SelectedByteDiffSpans;
	TSharedPtr<FSlateStyleSet> HexDiffStyle;

	bool bShowUnchanged = false;

	bool bUseRelativeOffsets = true;

	FText StatusText;
};