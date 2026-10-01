// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Diff/AssetDiffFilter.h"

namespace AssetDiffFilterTestUtils
{
	static FAssetPackageDiffEntry MakeEntry(const FString& Name, const EAssetPackageDiffState State, const FString& TypeName = FString(), const FString& NewValue = FString())
	{
		FAssetPackageDiffEntry Entry;
		Entry.Key = Name;
		Entry.DisplayName = FText::FromString(Name);
		Entry.SemanticPath = Name;
		Entry.State = State;
		Entry.TypeName = TypeName;
		Entry.NewValue = NewValue;
		return Entry;
	}

	/**
	 * Export
	 *   Health (Modified, IntProperty, value 75)
	 *   Name (Added, StrProperty, value "Bob")
	 *   Mana (Unchanged, IntProperty)
	 * Other (Removed)
	 */
	static TArray<FAssetPackageDiffEntry> MakeTree()
	{
		FAssetPackageDiffEntry Export = MakeEntry(TEXT("Export"), EAssetPackageDiffState::Modified);
		Export.Children.Add(MakeEntry(TEXT("Health"), EAssetPackageDiffState::Modified, TEXT("IntProperty"), TEXT("75")));
		Export.Children.Add(MakeEntry(TEXT("Name"), EAssetPackageDiffState::Added, TEXT("StrProperty"), TEXT("Bob")));
		Export.Children.Add(MakeEntry(TEXT("Mana"), EAssetPackageDiffState::Unchanged, TEXT("IntProperty")));

		return { Export, MakeEntry(TEXT("Other"), EAssetPackageDiffState::Removed) };
	}

	static TArray<FString> VisibleNames(const FAssetDiffFilter& Filter, const TArray<FAssetPackageDiffEntry>& Entries)
	{
		TArray<FString> Names;
		for (const FAssetPackageDiffEntry& Entry : Entries)
		{
			if (Filter.ShouldInclude(Entry))
			{
				Names.Add(Entry.Key);
				for (const FAssetPackageDiffEntry& Child : Entry.Children)
				{
					if (Filter.ShouldInclude(Child, Filter.MatchesQuery(Entry)))
					{
						Names.Add(Entry.Key + TEXT("/") + Child.Key);
					}
				}
			}
		}
		return Names;
	}
} // namespace AssetDiffFilterTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetDiffFilter_ParsesAndMatchesSearchTerms, "AssetSerializationInspector.Diff.AssetDiffFilter.ParsesAndMatchesSearchTerms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetDiffFilter_ParsesAndMatchesSearchTerms::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Blank text is an empty query"), FAssetSearchQuery::Parse(TEXT("   \t ")).IsEmpty());
	TestEqual(TEXT("Terms are split on whitespace"), FAssetSearchQuery::Parse(TEXT("  health   int ")).Terms.Num(), 2);

	const TArray<FString> Fields = { TEXT("Health"), TEXT("IntProperty"), TEXT("/Game/Hero.Hero") };

	TestTrue(TEXT("An empty query matches anything"), FAssetSearchQuery::Parse(TEXT("")).Matches(Fields));
	TestTrue(TEXT("Matching ignores case"), FAssetSearchQuery::Parse(TEXT("hEaLtH")).Matches(Fields));
	TestTrue(TEXT("Terms can be found in different fields"), FAssetSearchQuery::Parse(TEXT("health intprop")).Matches(Fields));
	TestFalse(TEXT("Every term must be found"), FAssetSearchQuery::Parse(TEXT("health float")).Matches(Fields));
	TestTrue(TEXT("Matching is by substring"), FAssetSearchQuery::Parse(TEXT("game/her")).Matches(Fields));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDiffFilter_FiltersByState, "AssetSerializationInspector.Diff.AssetDiffFilter.FiltersByState", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetDiffFilter_FiltersByState::RunTest(const FString& Parameters)
{
	using namespace AssetDiffFilterTestUtils;

	const TArray<FAssetPackageDiffEntry> Tree = MakeTree();

	FAssetDiffFilter Filter;
	TestEqual(
		TEXT("By default changed entries show and unchanged ones hide"), VisibleNames(Filter, Tree), TArray<FString>({ TEXT("Export"), TEXT("Export/Health"), TEXT("Export/Name"), TEXT("Other") }));

	Filter.bShowUnchanged = true;
	TestEqual(TEXT("Showing unchanged adds them back"), VisibleNames(Filter, Tree).Num(), 5);

	Filter.bShowUnchanged = false;
	Filter.States = EAssetDiffStateFilter::Added;
	TestEqual(TEXT("Only additions keep the ancestors needed to reach them"), VisibleNames(Filter, Tree), TArray<FString>({ TEXT("Export"), TEXT("Export/Name") }));
	TestTrue(TEXT("The state filter is reported as active"), Filter.IsStateFilterActive());

	Filter.States = EAssetDiffStateFilter::Removed;
	TestEqual(TEXT("Only removals"), VisibleNames(Filter, Tree), TArray<FString>({ TEXT("Other") }));

	Filter.States = EAssetDiffStateFilter::None;
	TestTrue(TEXT("With no states selected nothing changed is shown"), VisibleNames(Filter, Tree).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetDiffFilter_SearchKeepsAncestorsAndSubtrees, "AssetSerializationInspector.Diff.AssetDiffFilter.SearchKeepsAncestorsAndSubtrees",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetDiffFilter_SearchKeepsAncestorsAndSubtrees::RunTest(const FString& Parameters)
{
	using namespace AssetDiffFilterTestUtils;

	const TArray<FAssetPackageDiffEntry> Tree = MakeTree();

	FAssetDiffFilter Filter;
	Filter.Query = FAssetSearchQuery::Parse(TEXT("health"));
	TestEqual(TEXT("A matching child keeps its parent visible"), VisibleNames(Filter, Tree), TArray<FString>({ TEXT("Export"), TEXT("Export/Health") }));
	TestEqual(TEXT("Only the matching entry counts as a match"), Filter.CountMatches(Tree), 1);
	TestTrue(TEXT("The search is reported as active"), Filter.IsSearchActive());

	// Matching the parent brings its changed descendants along.
	Filter.Query = FAssetSearchQuery::Parse(TEXT("export"));
	TestEqual(TEXT("A matching parent shows everything changed inside it"), VisibleNames(Filter, Tree), TArray<FString>({ TEXT("Export"), TEXT("Export/Health"), TEXT("Export/Name") }));

	// The state filter still applies inside a matching subtree.
	Filter.States = EAssetDiffStateFilter::Added;
	TestEqual(TEXT("The state filter narrows a matching subtree"), VisibleNames(Filter, Tree), TArray<FString>({ TEXT("Export"), TEXT("Export/Name") }));

	Filter.Query = FAssetSearchQuery::Parse(TEXT("nonexistent"));
	Filter.States = EAssetDiffStateFilter::All;
	TestTrue(TEXT("A query that matches nothing hides everything"), VisibleNames(Filter, Tree).IsEmpty());
	TestEqual(TEXT("Nothing counts as a match"), Filter.CountMatches(Tree), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDiffFilter_SearchesValuesWhenEnabled, "AssetSerializationInspector.Diff.AssetDiffFilter.SearchesValuesWhenEnabled", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetDiffFilter_SearchesValuesWhenEnabled::RunTest(const FString& Parameters)
{
	using namespace AssetDiffFilterTestUtils;

	FAssetPackageDiffEntry Entry = MakeEntry(TEXT("Health"), EAssetPackageDiffState::Modified, TEXT("IntProperty"), TEXT("75"));
	Entry.OldDecodedValue = TEXT("50");
	Entry.NewFinalValue = TEXT("2 elements: alpha, beta");

	FAssetDiffFilter Filter;
	Filter.Query = FAssetSearchQuery::Parse(TEXT("75"));
	TestTrue(TEXT("New values are searched"), Filter.MatchesQuery(Entry));

	Filter.Query = FAssetSearchQuery::Parse(TEXT("50"));
	TestTrue(TEXT("Decoded values are searched"), Filter.MatchesQuery(Entry));

	Filter.Query = FAssetSearchQuery::Parse(TEXT("beta"));
	TestTrue(TEXT("Final container values are searched"), Filter.MatchesQuery(Entry));

	Filter.bSearchValues = false;
	TestFalse(TEXT("Values are skipped when value search is off"), Filter.MatchesQuery(Entry));

	Filter.Query = FAssetSearchQuery::Parse(TEXT("health"));
	TestTrue(TEXT("Names are still searched without values"), Filter.MatchesQuery(Entry));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
