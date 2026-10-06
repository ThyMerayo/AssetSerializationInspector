// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Save/RepeatedSaveAnalyzer.h"

#include "Diff/AssetPackageDiff.h"
#include "Save/AssetSaveHistoryManager.h"

using FExplanationMap = TMap<FString, const FAssetSaveExplanationEntry*>;

static void CollectExplanations(const FAssetSaveExplanationEntry& Entry, FExplanationMap& OutEntries)
{
	if (!Entry.SemanticPath.IsEmpty())
	{
		OutEntries.Add(Entry.SemanticPath, &Entry);
	}

	for (const FAssetSaveExplanationEntry& Child : Entry.Children)
	{
		CollectExplanations(Child, OutEntries);
	}
}

/** The changes of a save that repeat across saves: property changes, and the package header's tables. */
static void CollectSaveChanges(const FObservedSaveHistoryEntry& Save, FExplanationMap& OutChanges)
{
	for (const FAssetSaveExplanationEntry& Entry : Save.SemanticChanges)
	{
		CollectExplanations(Entry, OutChanges);
	}

	for (const FAssetSaveExplanationEntry& Entry : Save.HeaderChanges)
	{
		CollectExplanations(Entry, OutChanges);
	}
}

static bool IsContinuouslyChanging(const FRepeatedSavePattern& Pattern)
{
	FString PreviousValue;
	bool bHasPreviousValue = false;

	int32 ValueSamples = 0;

	for (const FObservedPropertySample& Sample : Pattern.Samples)
	{
		if (!Sample.bHasNewValue)
		{
			continue;
		}

		if (bHasPreviousValue && Sample.NewValue.Equals(PreviousValue, ESearchCase::CaseSensitive))
		{
			return false;
		}

		PreviousValue = Sample.NewValue;

		bHasPreviousValue = true;
		ValueSamples++;
	}

	return ValueSamples >= 3;
}

static bool IsAlternating(const FRepeatedSavePattern& Pattern)
{
	TArray<FString> Values;
	for (const FObservedPropertySample& Sample : Pattern.Samples)
	{
		if (Sample.bHasNewValue)
		{
			Values.Add(Sample.NewValue);
		}
	}

	if (Values.Num() < 4)
	{
		return false;
	}

	const FString& A = Values[0];
	const FString& B = Values[1];

	if (A == B)
	{
		return false;
	}

	for (int32 Index = 2; Index < Values.Num(); ++Index)
	{
		const FString& Expected = (Index % 2 == 0) ? A : B;
		if (Values[Index] != Expected)
		{
			return false;
		}
	}

	return true;
}

static EObservedValuePattern ClassifyValuePattern(const FRepeatedSavePattern& Pattern)
{
	if (Pattern.ObservationCount == 0)
	{
		return EObservedValuePattern::Unknown;
	}

	if (Pattern.ChangeCount == 0)
	{
		return EObservedValuePattern::Stable;
	}

	if (Pattern.ChangeCount == 1)
	{
		return EObservedValuePattern::ChangedOnce;
	}

	if (Pattern.ChangeCount == Pattern.ObservationCount)
	{
		if (IsAlternating(Pattern))
		{
			return EObservedValuePattern::Alternating;
		}

		if (IsContinuouslyChanging(Pattern))
		{
			return EObservedValuePattern::ContinuouslyChanging;
		}

		return EObservedValuePattern::ChangedEverySave;
	}

	return EObservedValuePattern::Recurring;
}

TArray<FRepeatedSavePattern> FRepeatedSaveAnalyzer::Analyze(const FAssetSaveHistory& History)
{
	TSet<FString> AllPaths;

	for (const FObservedSaveHistoryEntry& Save : History.Entries)
	{
		for (const auto& Pair : Save.PropertyStates)
		{
			AllPaths.Add(Pair.Key);
		}

		FExplanationMap Changes;
		CollectSaveChanges(Save, Changes);

		for (const auto& Pair : Changes)
		{
			AllPaths.Add(Pair.Key);
		}
	}

	TArray<FRepeatedSavePattern> Result;

	for (const FString& Path : AllPaths)
	{
		FRepeatedSavePattern Pattern;
		Pattern.SemanticPath = Path;

		for (const FObservedSaveHistoryEntry& Save : History.Entries)
		{
			FExplanationMap Changes;
			CollectSaveChanges(Save, Changes);

			const FAssetSaveExplanationEntry* const* ChangeFound = Changes.Find(Path);

			const FObservedPropertyState* State = Save.PropertyStates.Find(Path);

			FObservedPropertySample Sample;
			Sample.SaveId = Save.SaveId;
			Sample.Timestamp = Save.Timestamp;

			if (ChangeFound != nullptr)
			{
				const FAssetSaveExplanationEntry& Change = **ChangeFound;
				Sample.bChanged = true;
				Sample.Classification = Change.Classification;
				Sample.ChangedByteCount = Change.ChangedByteCount;
				Sample.bHasOldValue = Change.bHasOldValue;
				Sample.bHasNewValue = Change.bHasNewValue;
				Sample.OldValue = Change.OldValue;
				Sample.NewValue = Change.NewValue;

				Pattern.ChangeCount++;
				Pattern.TotalChangedBytes += Change.ChangedByteCount;

				if (Pattern.DisplayName.IsEmpty())
				{
					Pattern.DisplayName = Change.Title;
				}
			}
			else if (State != nullptr)
			{
				Sample.bChanged = false;
				Sample.bHasNewValue = State->bHasValue;
				Sample.NewValue = State->Value;

				if (Pattern.DisplayName.IsEmpty())
				{
					Pattern.DisplayName = State->DisplayName;
				}
			}

			Pattern.Samples.Add(MoveTemp(Sample));
		}

		Pattern.ObservationCount = History.Entries.Num();
		Pattern.ValuePattern = ClassifyValuePattern(Pattern);

		Result.Add(MoveTemp(Pattern));
	}

	return Result;
}