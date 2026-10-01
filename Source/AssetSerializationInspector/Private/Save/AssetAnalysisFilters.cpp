// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Save/AssetAnalysisFilters.h"

EAssetConfidenceFilter ToConfidenceFilter(const EAssetExplanationConfidence Confidence)
{
	switch (Confidence)
	{
		case EAssetExplanationConfidence::Certain:
			return EAssetConfidenceFilter::Certain;

		case EAssetExplanationConfidence::High:
			return EAssetConfidenceFilter::High;

		case EAssetExplanationConfidence::Inferred:
			return EAssetConfidenceFilter::Inferred;

		case EAssetExplanationConfidence::Unknown:
			return EAssetConfidenceFilter::Unknown;
	}

	return EAssetConfidenceFilter::Unknown;
}

bool FAssetSaveAnalysisFilter::MatchesConfidence(const FAssetSaveExplanationEntry& Entry) const
{
	return EnumHasAnyFlags(Confidences, ToConfidenceFilter(Entry.Confidence));
}

bool FAssetSaveAnalysisFilter::MatchesQuery(const FAssetSaveExplanationEntry& Entry) const
{
	if (Query.IsEmpty())
	{
		return true;
	}

	TArray<FString, TInlineAllocator<8>> Fields;
	Fields.Add(Entry.Title.ToString());
	Fields.Add(Entry.Description.ToString());
	Fields.Add(Entry.CauseDescription.ToString());
	Fields.Add(Entry.Key);
	Fields.Add(Entry.SemanticPath);

	if (bSearchValues)
	{
		Fields.Add(Entry.OldValue);
		Fields.Add(Entry.NewValue);
	}

	return Query.Matches(Fields);
}

bool FAssetSaveAnalysisFilter::ShouldIncludeSelf(const FAssetSaveExplanationEntry& Entry, const bool bAncestorMatchedQuery) const
{
	return MatchesConfidence(Entry) && (bAncestorMatchedQuery || MatchesQuery(Entry));
}

TArray<FAssetSaveExplanationEntry> FAssetSaveAnalysisFilter::FilterEntries(const TArray<FAssetSaveExplanationEntry>& Entries, const bool bAncestorMatchedQuery) const
{
	TArray<FAssetSaveExplanationEntry> Result;

	for (const FAssetSaveExplanationEntry& Entry : Entries)
	{
		TArray<FAssetSaveExplanationEntry> Children = FilterEntries(Entry.Children, bAncestorMatchedQuery || MatchesQuery(Entry));

		if (ShouldIncludeSelf(Entry, bAncestorMatchedQuery) || !Children.IsEmpty())
		{
			FAssetSaveExplanationEntry& Copy = Result.Add_GetRef(Entry);
			Copy.Children = MoveTemp(Children);
		}
	}

	return Result;
}

int32 FAssetSaveAnalysisFilter::CountMatches(const TArray<FAssetSaveExplanationEntry>& Entries, const bool bAncestorMatchedQuery) const
{
	int32 Count = 0;

	for (const FAssetSaveExplanationEntry& Entry : Entries)
	{
		if (ShouldIncludeSelf(Entry, bAncestorMatchedQuery))
		{
			++Count;
		}

		Count += CountMatches(Entry.Children, bAncestorMatchedQuery || MatchesQuery(Entry));
	}

	return Count;
}

EObservedPatternFilter ToPatternFilter(const EObservedValuePattern Pattern)
{
	switch (Pattern)
	{
		case EObservedValuePattern::Unknown:
			return EObservedPatternFilter::Unknown;

		case EObservedValuePattern::Stable:
			return EObservedPatternFilter::Stable;

		case EObservedValuePattern::ChangedOnce:
			return EObservedPatternFilter::ChangedOnce;

		case EObservedValuePattern::Recurring:
			return EObservedPatternFilter::Recurring;

		case EObservedValuePattern::ChangedEverySave:
			return EObservedPatternFilter::ChangedEverySave;

		case EObservedValuePattern::ContinuouslyChanging:
			return EObservedPatternFilter::ContinuouslyChanging;

		case EObservedValuePattern::Alternating:
			return EObservedPatternFilter::Alternating;
	}

	return EObservedPatternFilter::Unknown;
}

bool FRepeatedSaveFilter::Matches(const FRepeatedSavePattern& Pattern) const
{
	if (!EnumHasAnyFlags(Patterns, ToPatternFilter(Pattern.ValuePattern)))
	{
		return false;
	}

	if (Query.IsEmpty())
	{
		return true;
	}

	TArray<FString> Fields;
	Fields.Add(Pattern.DisplayName.ToString());
	Fields.Add(Pattern.SemanticPath);

	if (bSearchValues)
	{
		for (const FObservedPropertySample& Sample : Pattern.Samples)
		{
			if (Sample.bHasOldValue)
			{
				Fields.Add(Sample.OldValue);
			}

			if (Sample.bHasNewValue)
			{
				Fields.Add(Sample.NewValue);
			}
		}
	}

	return Query.Matches(Fields);
}

TArray<FRepeatedSavePattern> FRepeatedSaveFilter::Filter(const TArray<FRepeatedSavePattern>& InPatterns) const
{
	TArray<FRepeatedSavePattern> Result;

	for (const FRepeatedSavePattern& Pattern : InPatterns)
	{
		if (Matches(Pattern))
		{
			Result.Add(Pattern);
		}
	}

	return Result;
}
