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
