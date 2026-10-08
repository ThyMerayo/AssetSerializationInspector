// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SCompoundWidget.h"

#include "Diff/AssetDiffFilter.h"
#include "Diff/AssetPackageDiff.h"
#include "Save/AssetAnalysisFilters.h"
#include "Save/AssetSaveAnalyzer.h"
#include "Save/RepeatedSaveAnalyzer.h"
#include "Trace/AssetSerializationTrace.h"
#include "Widgets/AssetLinkedScroll.h"

class SCheckBox;
class SEditableTextBox;

template <typename ItemType> class STreeView;

struct FAssetPackageDocument;
struct FObservedAssetSave;

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

struct FAssetSerializationDiffSide
{
	TSharedPtr<FAssetPackageDocument> Document;
	TSharedPtr<FAssetPackageTraceCollection> Traces;
};

using FObservedSaveId = uint64;

struct FAssetSerializationDiffSession
{
	FAssetSerializationDiffSide Old;
	FAssetSerializationDiffSide New;

	TOptional<FAssetPackageDiffResult> DiffResult;
	TOptional<FAssetSaveAnalysis> Analysis;

	FObservedSaveId ObservedSaveId = 0;
	FName PackageName;

	/** The session that shows an observed save: its two packages, their traces, the diff and the save analysis. */
	static TSharedRef<FAssetSerializationDiffSession> FromObservedSave(const FObservedAssetSave& Save);

	/** The session that compares two package files on disk. Null, with the reason in OutError, when either cannot be read. */
	static TSharedPtr<FAssetSerializationDiffSession> FromFiles(const FString& OldFilename, const FString& NewFilename, FName PackageName, FText& OutError);
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

	FString Key;
	FString SemanticPath;

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
	TSharedRef<SWidget> BuildSaveAnalysisWidget();
	TSharedRef<SWidget> BuildSaveAnalysisSummary(const FAssetSaveAnalysis& Analysis);
	TSharedRef<SWidget> BuildAnalysisStat(const FText& Label, const FText& Value);
	TSharedRef<SWidget> BuildSaveAnalysisSection(const FText& Title, const FString& StateKey, const TArray<FAssetSaveExplanationEntry>& Entries);
	TSharedRef<SWidget> BuildSaveAnalysisEntry(const FAssetSaveExplanationEntry& Entry, const int32 Depth);
	TSharedRef<SWidget> BuildSaveAnalysisEntries(const TArray<FAssetSaveExplanationEntry>& Entries, const int32 Depth);
	TSharedRef<SWidget> BuildRepeatedSaveAnalysisWidget(FName PackageName);
	TSharedRef<SWidget> BuildRepeatedSavePatternWidget(const FRepeatedSavePattern& Pattern);
	TSharedRef<SWidget> BuildObservedValueTimeline(const FRepeatedSavePattern& Pattern);

	// File selection
	FReply HandleBrowseOldClicked();
	FReply HandleBrowseNewClicked();
	FReply HandleCompareClicked();
	FReply HandleExportReportClicked();

	bool BrowseForAsset(const FText& DialogTitle, FString& OutFilename);
	bool BrowseForReportFile(const FString& DefaultFilename, FString& OutFilename);
	bool LoadDocument(const FString& Filename, TSharedPtr<FAssetPackageDocument>& OutDocument, FText& OutError);

	// Diff tree
	void RebuildDiffTree();
	FDiffTreeNodePtr BuildDiffTreeNode(const FAssetPackageDiffEntry& Entry, const FDiffTreeNodePtr& Parent, bool bAncestorMatchedQuery);
	void GetDiffTreeChildren(FDiffTreeNodePtr Item, TArray<FDiffTreeNodePtr>& OutChildren) const;
	TSharedRef<ITableRow> GenerateDiffTreeRow(FDiffTreeNodePtr Item, const TSharedRef<STableViewBase>& OwnerTable);
	void HandleShowUnchangedChanged(ECheckBoxState NewState);
	void HandleSearchTextChanged(const FText& NewText);
	void HandleSearchValuesChanged(ECheckBoxState NewState);
	void HandleStateFilterChanged(ECheckBoxState NewState, EAssetDiffStateFilter Flag);
	ECheckBoxState GetStateFilterCheckState(EAssetDiffStateFilter Flag) const;
	TSharedRef<SWidget> BuildStateFilterCheckBox(EAssetDiffStateFilter Flag, const FText& Label);
	FText GetFilterResultText() const;
	void ExpandDiffSubtree(const FDiffTreeNodePtr& Node);
	static FString MakeDiffNodeIdentity(const FDiffTreeNodePtr& Node);
	void CollectExpandedDiffNodes(const FDiffTreeNodePtr& Node, TSet<FString>& OutIdentities) const;
	void RestoreDiffNodeExpansion(const FDiffTreeNodePtr& Node);
	void HandleDiffSelectionChanged(FDiffTreeNodePtr Item, ESelectInfo::Type SelectInfo);
	void AnnotateSelectedDiffSpans();
	const FAssetSerializationTrace* FindTraceForDiffEntry(const FAssetPackageDiffEntry& Diff, const bool bOldSide) const;
	bool BuildTracesForSide(FAssetSerializationDiffSide& Side);

	// Analysis
	void UpdateSaveAnalysisLayout();
	void RefreshSaveAnalysisPanel();
	void RefreshRepeatedSavePanel();
	TSharedRef<SWidget> BuildSaveAnalysisFilterBar();
	TSharedRef<SWidget> BuildRepeatedSaveFilterBar();
	TSharedRef<SWidget> BuildConfidenceFilterMenu();
	TSharedRef<SWidget> BuildPatternFilterMenu();
	void HandleAnalysisSearchTextChanged(const FText& NewText);
	void HandleRepeatedSearchTextChanged(const FText& NewText);
	FText GetAnalysisFilterResultText() const;
	FText GetRepeatedFilterResultText() const;
	void NavigateToDiffEntry(const FString& Key);
	FDiffTreeNodePtr FindDiffTreeNodeByKey(const TArray<FDiffTreeNodePtr>& Nodes, const FString& Key) const;

	// History
	FString MakeCompactHistoryValue(const FString& Value) const;
	void OpenHistorySave(const FObservedSaveId SaveId, const FString& SemanticPath);
	void NavigateToSemanticPath(const FString& SemanticPath);
	FDiffTreeNodePtr FindDiffTreeNodeBySemanticPath(const TArray<FDiffTreeNodePtr>& Nodes, const FString& SemanticPath) const;

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
	FText GetSelectedExplanationText() const;
	int64 GetSelectedChangedByteCount() const;
	FText GetComparisonTitle() const;
	FText GetChangeClassificationText() const;
	FText GetSaveAnalysisResultText(EAssetSaveResultKind ResultKind) const;
	FText GetExplanationPrefix(const EAssetSaveChangeClassification Classification) const;
	FText GetConfidenceText(const EAssetExplanationConfidence Confidence) const;
	FText BuildObservedValueText(const FObservedPropertySample& Sample) const;
	FText BuildObservedValueFullText(const FObservedPropertySample& Sample) const;
	FText GetObservedPatternText(const EObservedValuePattern Pattern) const;

	FText BuildHexPreview(const FAssetPackageDocument* Document, int64 Offset, int64 Size, bool bRelativeOffsets) const;

	bool IsByteDifferent(int64 RelativeOffset, const TArray<FAssetByteDiffSpan>& Spans) const;
	static bool IsByteInRanges(int64 RelativeOffset, const TArray<FAssetByteDiffSpan>& Ranges);
	FHexPreviewText BuildHighlightedHexPreview(
		const FAssetPackageDocument* Document, int64 Offset, int64 Size, const TArray<FAssetByteDiffSpan>& Spans, const TArray<FAssetByteDiffSpan>& ShiftedRanges) const;

	void LoadSessionIntoUI();
	void SelectFirstMeaningfulDifference();
	void ExpandDiffAncestors(const FDiffTreeNodePtr& Node);

private:
	/** The old and the new hex panels scroll together while this is linked. */
	FAssetLinkedScroll HexScrollLink;
	TSharedPtr<SScrollBox> OldHexScrollBox;
	TSharedPtr<SScrollBox> NewHexScrollBox;

	FHexPreviewText OldHexPreview;
	FHexPreviewText NewHexPreview;

	TSharedPtr<SEditableTextBox> OldFilenameTextBox;
	TSharedPtr<SEditableTextBox> NewFilenameTextBox;

	TSharedPtr<STreeView<FDiffTreeNodePtr>> DiffTreeView;
	TSharedPtr<SBox> SaveAnalysisBox;
	TSharedPtr<SScrollBox> SaveAnalysisScrollBox;
	TSharedPtr<SBox> RepeatedSaveAnalysisBox;
	TSharedPtr<SScrollBox> RepeatedSaveAnalysisScrollBox;

	TSharedPtr<FAssetSerializationDiffSession> DiffSession;

	TArray<FDiffTreeNodePtr> RootDiffNodes;
	FDiffTreeNodePtr SelectedDiffNode;

	TArray<FAssetByteDiffSpan> SelectedByteDiffSpans;
	TSharedPtr<FSlateStyleSet> HexDiffStyle;

	FAssetDiffFilter DiffFilter;

	FAssetSaveAnalysisFilter AnalysisFilter;
	FRepeatedSaveFilter RepeatedSaveFilter;

	/** Rebuilding a panel for a filter change creates new expandable areas, so what the user opened or closed is remembered here. */
	TSet<FString> CollapsedAnalysisSections;
	TSet<FString> ExpandedRepeatedPatterns;

	/** How many patterns with changes the last Repeated Save Analysis had before filtering. */
	int32 RepeatedPatternTotal = 0;
	int32 RepeatedPatternShown = 0;

	/** Expanded entries, by identity, as the user left them. Rebuilding the tree creates new nodes, so expansion is restored from this. */
	TSet<FString> ExpandedDiffNodeIdentities;

	/** True while every node was expanded to show search results, which says nothing about what the user chose to expand. */
	bool bDiffTreeExpandedForSearch = false;

	bool bUseRelativeOffsets = true;

	FText StatusText;
};