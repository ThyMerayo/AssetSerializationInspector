// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Diff/AssetPackageDiff.h"

/**
 * A search-box query: whitespace-separated terms that must all be found (case-insensitive substring match). A term may
 * be found in any of the fields being searched, so "Health int" matches an entry named Health of type IntProperty.
 */
struct FAssetSearchQuery
{
	TArray<FString> Terms;

	static FAssetSearchQuery Parse(const FString& Text);

	bool IsEmpty() const { return Terms.IsEmpty(); }

	/** True when every term is contained in at least one of the fields. An empty query matches everything. */
	bool Matches(TConstArrayView<FString> Fields) const;
};

enum class EAssetDiffStateFilter : uint8
{
	None = 0,
	Added = 1 << 0,
	Removed = 1 << 1,
	Modified = 1 << 2,
	Moved = 1 << 3,
	All = Added | Removed | Modified | Moved
};
ENUM_CLASS_FLAGS(EAssetDiffStateFilter)

/**
 * Decides which entries of a structural diff are shown.
 *
 * An entry is shown when it passes the state filter and the search, or when any descendant does, so the path to a match
 * stays visible. Once an entry matches the search, its descendants count as matching too (subject to the state filter), so
 * searching for an export or a property also shows what is inside it.
 */
struct FAssetDiffFilter
{
	FAssetSearchQuery Query;

	/** Whether values (old, new, decoded and final) are searched in addition to names, paths and types. */
	bool bSearchValues = true;

	bool bShowUnchanged = false;
	EAssetDiffStateFilter States = EAssetDiffStateFilter::All;

	bool IsSearchActive() const { return !Query.IsEmpty(); }
	bool IsStateFilterActive() const { return States != EAssetDiffStateFilter::All; }

	/** Unchanged entries follow bShowUnchanged; changed ones follow the state flags. */
	bool MatchesState(const FAssetPackageDiffEntry& Entry) const;

	/** Whether the entry itself, ignoring its children and ancestors, matches the search. */
	bool MatchesQuery(const FAssetPackageDiffEntry& Entry) const;

	/**
	 * Whether the entry is shown for its own sake, before considering its descendants.
	 * @param bAncestorMatchedQuery True when an ancestor matched the search, which makes this entry match it as well.
	 */
	bool ShouldIncludeSelf(const FAssetPackageDiffEntry& Entry, bool bAncestorMatchedQuery) const;

	/** Whether the entry or any descendant is shown. */
	bool ShouldInclude(const FAssetPackageDiffEntry& Entry, bool bAncestorMatchedQuery = false) const;

	/** Copies the entries that are shown, pruning the children that are not. Entries kept only to reach a match keep just those children. */
	TArray<FAssetPackageDiffEntry> FilterEntries(const TArray<FAssetPackageDiffEntry>& Entries, bool bAncestorMatchedQuery = false) const;

	/** A human-readable summary of the active filters, or an empty string for the default view (changed entries only, no search). */
	FString Describe() const;

	/** Counts the entries, at any depth, that are shown for their own sake. */
	int32 CountMatches(const TArray<FAssetPackageDiffEntry>& Entries, bool bAncestorMatchedQuery = false) const;
};
