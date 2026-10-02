// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Diff/AssetDiffFilter.h"
#include "Save/AssetSaveAnalyzer.h"
#include "Save/RepeatedSaveAnalyzer.h"

/** Which explanation confidences the Save Analysis shows. */
enum class EAssetConfidenceFilter : uint8
{
	None = 0,
	Certain = 1 << 0,
	High = 1 << 1,
	Inferred = 1 << 2,
	Unknown = 1 << 3,
	All = Certain | High | Inferred | Unknown
};
ENUM_CLASS_FLAGS(EAssetConfidenceFilter)

EAssetConfidenceFilter ToConfidenceFilter(EAssetExplanationConfidence Confidence);

/**
 * Decides which explanations of a save analysis are shown, with the same rules as the structural diff: an entry is shown
 * when it passes the filters or something inside it does, and once an entry matches the search its children count as
 * matching too (the confidence filter still applies to them).
 */
struct FAssetSaveAnalysisFilter
{
	FAssetSearchQuery Query;

	/** Whether old and new values are searched in addition to titles, descriptions and paths. */
	bool bSearchValues = true;

	EAssetConfidenceFilter Confidences = EAssetConfidenceFilter::All;

	bool IsSearchActive() const { return !Query.IsEmpty(); }
	bool IsConfidenceFilterActive() const { return Confidences != EAssetConfidenceFilter::All; }
	bool IsActive() const { return IsSearchActive() || IsConfidenceFilterActive(); }

	bool MatchesConfidence(const FAssetSaveExplanationEntry& Entry) const;

	/** Whether the entry itself, ignoring its children and ancestors, matches the search. */
	bool MatchesQuery(const FAssetSaveExplanationEntry& Entry) const;

	bool ShouldIncludeSelf(const FAssetSaveExplanationEntry& Entry, bool bAncestorMatchedQuery) const;

	/** Copies the entries that are shown, pruning the children that are not. */
	TArray<FAssetSaveExplanationEntry> FilterEntries(const TArray<FAssetSaveExplanationEntry>& Entries, bool bAncestorMatchedQuery = false) const;

	/** Counts the entries, at any depth, that are shown for their own sake. */
	int32 CountMatches(const TArray<FAssetSaveExplanationEntry>& Entries, bool bAncestorMatchedQuery = false) const;
};

/** Which value patterns the Repeated Save Analysis shows. */
enum class EObservedPatternFilter : uint8
{
	None = 0,
	Unknown = 1 << 0,
	Stable = 1 << 1,
	ChangedOnce = 1 << 2,
	Recurring = 1 << 3,
	ChangedEverySave = 1 << 4,
	ContinuouslyChanging = 1 << 5,
	Alternating = 1 << 6,
	All = Unknown | Stable | ChangedOnce | Recurring | ChangedEverySave | ContinuouslyChanging | Alternating
};
ENUM_CLASS_FLAGS(EObservedPatternFilter)

EObservedPatternFilter ToPatternFilter(EObservedValuePattern Pattern);

/** Decides which properties of the Repeated Save Analysis are shown. */
struct FRepeatedSaveFilter
{
	FAssetSearchQuery Query;

	/** Whether the values seen across saves are searched in addition to the property's name and path. */
	bool bSearchValues = true;

	EObservedPatternFilter Patterns = EObservedPatternFilter::All;

	bool IsSearchActive() const { return !Query.IsEmpty(); }
	bool IsPatternFilterActive() const { return Patterns != EObservedPatternFilter::All; }
	bool IsActive() const { return IsSearchActive() || IsPatternFilterActive(); }

	bool Matches(const FRepeatedSavePattern& Pattern) const;
	TArray<FRepeatedSavePattern> Filter(const TArray<FRepeatedSavePattern>& Patterns) const;
};
