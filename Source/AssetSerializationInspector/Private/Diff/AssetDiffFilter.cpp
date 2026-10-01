// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Diff/AssetDiffFilter.h"

FAssetSearchQuery FAssetSearchQuery::Parse(const FString& Text)
{
	FAssetSearchQuery Query;
	Text.ParseIntoArrayWS(Query.Terms);
	return Query;
}

bool FAssetSearchQuery::Matches(const TConstArrayView<FString> Fields) const
{
	for (const FString& Term : Terms)
	{
		const bool bFound = Fields.ContainsByPredicate([&Term](const FString& Field) { return Field.Contains(Term, ESearchCase::IgnoreCase); });

		if (!bFound)
		{
			return false;
		}
	}

	return true;
}

bool FAssetDiffFilter::MatchesState(const FAssetPackageDiffEntry& Entry) const
{
	switch (Entry.State)
	{
		case EAssetPackageDiffState::Unchanged:
			return bShowUnchanged;

		case EAssetPackageDiffState::Added:
			return EnumHasAnyFlags(States, EAssetDiffStateFilter::Added);

		case EAssetPackageDiffState::Removed:
			return EnumHasAnyFlags(States, EAssetDiffStateFilter::Removed);

		case EAssetPackageDiffState::Modified:
			return EnumHasAnyFlags(States, EAssetDiffStateFilter::Modified);

		case EAssetPackageDiffState::Moved:
			return EnumHasAnyFlags(States, EAssetDiffStateFilter::Moved);
	}

	return false;
}

bool FAssetDiffFilter::MatchesQuery(const FAssetPackageDiffEntry& Entry) const
{
	if (Query.IsEmpty())
	{
		return true;
	}

	TArray<FString, TInlineAllocator<12>> Fields;
	Fields.Add(Entry.DisplayName.ToString());
	Fields.Add(Entry.Key);
	Fields.Add(Entry.SemanticPath);
	Fields.Add(Entry.TypeName);

	if (bSearchValues)
	{
		Fields.Add(Entry.OldValue);
		Fields.Add(Entry.NewValue);
		Fields.Add(Entry.OldDecodedValue);
		Fields.Add(Entry.NewDecodedValue);
		Fields.Add(Entry.OldFinalValue);
		Fields.Add(Entry.NewFinalValue);
	}

	return Query.Matches(Fields);
}

bool FAssetDiffFilter::ShouldIncludeSelf(const FAssetPackageDiffEntry& Entry, const bool bAncestorMatchedQuery) const
{
	return MatchesState(Entry) && (bAncestorMatchedQuery || MatchesQuery(Entry));
}

bool FAssetDiffFilter::ShouldInclude(const FAssetPackageDiffEntry& Entry, const bool bAncestorMatchedQuery) const
{
	if (ShouldIncludeSelf(Entry, bAncestorMatchedQuery))
	{
		return true;
	}

	const bool bMatchedForChildren = bAncestorMatchedQuery || MatchesQuery(Entry);

	for (const FAssetPackageDiffEntry& Child : Entry.Children)
	{
		if (ShouldInclude(Child, bMatchedForChildren))
		{
			return true;
		}
	}

	return false;
}

int32 FAssetDiffFilter::CountMatches(const TArray<FAssetPackageDiffEntry>& Entries, const bool bAncestorMatchedQuery) const
{
	int32 Count = 0;

	for (const FAssetPackageDiffEntry& Entry : Entries)
	{
		if (ShouldIncludeSelf(Entry, bAncestorMatchedQuery))
		{
			++Count;
		}

		Count += CountMatches(Entry.Children, bAncestorMatchedQuery || MatchesQuery(Entry));
	}

	return Count;
}

TArray<FAssetPackageDiffEntry> FAssetDiffFilter::FilterEntries(const TArray<FAssetPackageDiffEntry>& Entries, const bool bAncestorMatchedQuery) const
{
	TArray<FAssetPackageDiffEntry> Result;

	for (const FAssetPackageDiffEntry& Entry : Entries)
	{
		TArray<FAssetPackageDiffEntry> Children = FilterEntries(Entry.Children, bAncestorMatchedQuery || MatchesQuery(Entry));

		if (ShouldIncludeSelf(Entry, bAncestorMatchedQuery) || !Children.IsEmpty())
		{
			FAssetPackageDiffEntry& Copy = Result.Add_GetRef(Entry);
			Copy.Children = MoveTemp(Children);
		}
	}

	return Result;
}

FString FAssetDiffFilter::Describe() const
{
	TArray<FString> Parts;

	if (IsSearchActive())
	{
		Parts.Add(FString::Printf(TEXT("search \"%s\"%s"), *FString::Join(Query.Terms, TEXT(" ")), bSearchValues ? TEXT("") : TEXT(" (names only)")));
	}

	if (IsStateFilterActive())
	{
		TArray<FString> Names;
		if (EnumHasAnyFlags(States, EAssetDiffStateFilter::Added))
		{
			Names.Add(TEXT("added"));
		}
		if (EnumHasAnyFlags(States, EAssetDiffStateFilter::Removed))
		{
			Names.Add(TEXT("removed"));
		}
		if (EnumHasAnyFlags(States, EAssetDiffStateFilter::Modified))
		{
			Names.Add(TEXT("modified"));
		}
		if (EnumHasAnyFlags(States, EAssetDiffStateFilter::Moved))
		{
			Names.Add(TEXT("moved"));
		}

		Parts.Add(FString::Printf(TEXT("states: %s"), Names.IsEmpty() ? TEXT("none") : *FString::Join(Names, TEXT(", "))));
	}

	if (bShowUnchanged)
	{
		Parts.Add(TEXT("unchanged entries included"));
	}

	return FString::Join(Parts, TEXT("; "));
}
