// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Save/AssetAnalysisFilters.h"

namespace AssetAnalysisFilterTestUtils
{
	static FAssetSaveExplanationEntry MakeEntry(const FString& Title, const EAssetExplanationConfidence Confidence, const FString& NewValue = FString())
	{
		FAssetSaveExplanationEntry Entry;
		Entry.Title = FText::FromString(Title);
		Entry.Key = Title;
		Entry.SemanticPath = TEXT("P/") + Title;
		Entry.Confidence = Confidence;
		Entry.NewValue = NewValue;
		Entry.bHasNewValue = !NewValue.IsEmpty();
		return Entry;
	}

	/**
	 * Export (Certain)
	 *   Health (High, value 75)
	 *   Mana (Inferred)
	 * Relocated (Certain)
	 */
	static TArray<FAssetSaveExplanationEntry> MakeEntries()
	{
		FAssetSaveExplanationEntry Export = MakeEntry(TEXT("Export"), EAssetExplanationConfidence::Certain);
		Export.Children.Add(MakeEntry(TEXT("Health"), EAssetExplanationConfidence::High, TEXT("75")));
		Export.Children.Add(MakeEntry(TEXT("Mana"), EAssetExplanationConfidence::Inferred));

		return { Export, MakeEntry(TEXT("Relocated"), EAssetExplanationConfidence::Certain) };
	}

	static TArray<FString> Titles(const TArray<FAssetSaveExplanationEntry>& Entries)
	{
		TArray<FString> Result;
		for (const FAssetSaveExplanationEntry& Entry : Entries)
		{
			Result.Add(Entry.Title.ToString());
			for (const FAssetSaveExplanationEntry& Child : Entry.Children)
			{
				Result.Add(Entry.Title.ToString() + TEXT("/") + Child.Title.ToString());
			}
		}
		return Result;
	}

	static FRepeatedSavePattern MakePattern(const FString& Name, const EObservedValuePattern ValuePattern, const FString& NewValue)
	{
		FObservedPropertySample Sample;
		Sample.bChanged = true;
		Sample.bHasNewValue = true;
		Sample.NewValue = NewValue;

		FRepeatedSavePattern Pattern;
		Pattern.DisplayName = FText::FromString(Name);
		Pattern.SemanticPath = TEXT("Export/") + Name;
		Pattern.ValuePattern = ValuePattern;
		Pattern.Samples.Add(Sample);
		return Pattern;
	}
} // namespace AssetAnalysisFilterTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetSaveAnalysisFilter_FiltersBySearch, "AssetSerializationInspector.Save.AssetSaveAnalysisFilter.FiltersBySearch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSaveAnalysisFilter_FiltersBySearch::RunTest(const FString& Parameters)
{
	using namespace AssetAnalysisFilterTestUtils;

	const TArray<FAssetSaveExplanationEntry> Entries = MakeEntries();

	FAssetSaveAnalysisFilter Filter;
	TestFalse(TEXT("A default filter is inactive"), Filter.IsActive());
	TestEqual(TEXT("It shows everything"), Titles(Filter.FilterEntries(Entries)), Titles(Entries));

	Filter.Query = FAssetSearchQuery::Parse(TEXT("health"));
	TestTrue(TEXT("A search makes the filter active"), Filter.IsActive());
	TestEqual(TEXT("A matching child keeps its parent"), Titles(Filter.FilterEntries(Entries)), TArray<FString>({ TEXT("Export"), TEXT("Export/Health") }));
	TestEqual(TEXT("Only the matching entry counts"), Filter.CountMatches(Entries), 1);

	Filter.Query = FAssetSearchQuery::Parse(TEXT("export"));
	TestEqual(TEXT("A matching parent shows what is inside it"), Titles(Filter.FilterEntries(Entries)), TArray<FString>({ TEXT("Export"), TEXT("Export/Health"), TEXT("Export/Mana") }));

	Filter.Query = FAssetSearchQuery::Parse(TEXT("75"));
	TestEqual(TEXT("Values are searched"), Titles(Filter.FilterEntries(Entries)), TArray<FString>({ TEXT("Export"), TEXT("Export/Health") }));

	Filter.bSearchValues = false;
	TestTrue(TEXT("Values are skipped when value search is off"), Filter.FilterEntries(Entries).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSaveAnalysisFilter_FiltersByConfidence, "AssetSerializationInspector.Save.AssetSaveAnalysisFilter.FiltersByConfidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSaveAnalysisFilter_FiltersByConfidence::RunTest(const FString& Parameters)
{
	using namespace AssetAnalysisFilterTestUtils;

	const TArray<FAssetSaveExplanationEntry> Entries = MakeEntries();

	FAssetSaveAnalysisFilter Filter;
	Filter.Confidences = EAssetConfidenceFilter::Inferred;
	TestTrue(TEXT("A confidence filter makes the filter active"), Filter.IsConfidenceFilterActive());
	TestEqual(TEXT("Only inferred entries, with the parent needed to reach them"), Titles(Filter.FilterEntries(Entries)), TArray<FString>({ TEXT("Export"), TEXT("Export/Mana") }));

	Filter.Confidences = EAssetConfidenceFilter::Certain;
	TestEqual(TEXT("Only certain entries"), Titles(Filter.FilterEntries(Entries)), TArray<FString>({ TEXT("Export"), TEXT("Relocated") }));

	Filter.Confidences = EAssetConfidenceFilter::None;
	TestTrue(TEXT("With no confidence selected nothing is shown"), Filter.FilterEntries(Entries).IsEmpty());

	// The confidence filter still applies inside an entry that matches the search.
	Filter.Confidences = EAssetConfidenceFilter::High;
	Filter.Query = FAssetSearchQuery::Parse(TEXT("export"));
	TestEqual(TEXT("Search and confidence combine"), Titles(Filter.FilterEntries(Entries)), TArray<FString>({ TEXT("Export"), TEXT("Export/Health") }));

	TestEqual(TEXT("Confidences map to filter flags"), ToConfidenceFilter(EAssetExplanationConfidence::High), EAssetConfidenceFilter::High);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRepeatedSaveFilter_FiltersPatterns, "AssetSerializationInspector.Save.RepeatedSaveFilter.FiltersPatterns", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRepeatedSaveFilter_FiltersPatterns::RunTest(const FString& Parameters)
{
	using namespace AssetAnalysisFilterTestUtils;

	const TArray<FRepeatedSavePattern> Patterns = {
		MakePattern(TEXT("Counter"), EObservedValuePattern::ChangedEverySave, TEXT("41")),
		MakePattern(TEXT("Toggle"), EObservedValuePattern::Alternating, TEXT("true")),
		MakePattern(TEXT("Label"), EObservedValuePattern::ChangedOnce, TEXT("Hello")),
	};

	const auto Names = [](const TArray<FRepeatedSavePattern>& List) {
		TArray<FString> Result;
		for (const FRepeatedSavePattern& Pattern : List)
		{
			Result.Add(Pattern.DisplayName.ToString());
		}
		return Result;
	};

	FRepeatedSaveFilter Filter;
	TestFalse(TEXT("A default filter is inactive"), Filter.IsActive());
	TestEqual(TEXT("It shows everything"), Names(Filter.Filter(Patterns)), TArray<FString>({ TEXT("Counter"), TEXT("Toggle"), TEXT("Label") }));

	Filter.Query = FAssetSearchQuery::Parse(TEXT("tog"));
	TestEqual(TEXT("Names are searched"), Names(Filter.Filter(Patterns)), TArray<FString>({ TEXT("Toggle") }));

	Filter.Query = FAssetSearchQuery::Parse(TEXT("hello"));
	TestEqual(TEXT("Values seen across saves are searched"), Names(Filter.Filter(Patterns)), TArray<FString>({ TEXT("Label") }));

	Filter.bSearchValues = false;
	TestTrue(TEXT("Values are skipped when value search is off"), Filter.Filter(Patterns).IsEmpty());

	Filter = FRepeatedSaveFilter();
	Filter.Patterns = EObservedPatternFilter::ChangedEverySave | EObservedPatternFilter::ChangedOnce;
	TestTrue(TEXT("A pattern filter makes the filter active"), Filter.IsPatternFilterActive());
	TestEqual(TEXT("Only the chosen patterns are shown"), Names(Filter.Filter(Patterns)), TArray<FString>({ TEXT("Counter"), TEXT("Label") }));

	Filter.Query = FAssetSearchQuery::Parse(TEXT("label"));
	TestEqual(TEXT("Search and pattern combine"), Names(Filter.Filter(Patterns)), TArray<FString>({ TEXT("Label") }));

	Filter.Patterns = EObservedPatternFilter::None;
	TestTrue(TEXT("With no pattern selected nothing is shown"), Filter.Filter(Patterns).IsEmpty());

	TestEqual(TEXT("Every pattern maps to its own flag"), ToPatternFilter(EObservedValuePattern::Alternating), EObservedPatternFilter::Alternating);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
