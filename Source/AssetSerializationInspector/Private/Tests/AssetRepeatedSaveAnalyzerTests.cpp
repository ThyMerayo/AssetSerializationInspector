// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Save/AssetSaveHistoryManager.h"
#include "Save/RepeatedSaveAnalyzer.h"

namespace RepeatedSaveAnalyzerTestUtils
{
	static FAssetSaveExplanationEntry MakeChange(const FString& SemanticPath, const TCHAR* Title, const FString& NewValue = FString())
	{
		FAssetSaveExplanationEntry Entry;
		Entry.SemanticPath = SemanticPath;
		Entry.Title = FText::FromString(Title);
		Entry.ChangedByteCount = 8;
		Entry.bHasNewValue = !NewValue.IsEmpty();
		Entry.NewValue = NewValue;
		return Entry;
	}

	static const FRepeatedSavePattern* FindPattern(const TArray<FRepeatedSavePattern>& Patterns, const FString& Path)
	{
		return Patterns.FindByPredicate([&Path](const FRepeatedSavePattern& Pattern) { return Pattern.SemanticPath == Path; });
	}
} // namespace RepeatedSaveAnalyzerTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRepeatedSaveAnalyzer_FindsRepeatedHeaderChanges, "AssetSerializationInspector.Save.RepeatedSaveAnalyzer.FindsRepeatedHeaderChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRepeatedSaveAnalyzer_FindsRepeatedHeaderChanges::RunTest(const FString& Parameters)
{
	using namespace RepeatedSaveAnalyzerTestUtils;

	// Four saves: the thumbnail table of the header changes in every one, the name map in two, a property once.
	FAssetSaveHistory History;
	History.PackageName = TEXT("/Game/Hero");

	for (int32 Index = 0; Index < 4; ++Index)
	{
		FObservedSaveHistoryEntry Save;
		Save.SaveId = 100 + Index;

		FAssetSaveExplanationEntry Header = MakeChange(TEXT("Header"), TEXT("Header"));
		Header.Children.Add(MakeChange(TEXT("Header/Thumbnails"), TEXT("Thumbnail table"), FString::Printf(TEXT("%d bytes"), 100 + Index)));
		if (Index % 2 == 0)
		{
			Header.Children.Add(MakeChange(TEXT("Header/NameMap"), TEXT("Name map")));
		}
		Save.HeaderChanges.Add(Header);

		if (Index == 1)
		{
			Save.SemanticChanges.Add(MakeChange(TEXT("Speed"), TEXT("Speed"), TEXT("5")));
		}

		History.Entries.Add(Save);
	}

	const TArray<FRepeatedSavePattern> Patterns = FRepeatedSaveAnalyzer::Analyze(History);

	const FRepeatedSavePattern* Thumbnails = FindPattern(Patterns, TEXT("Header/Thumbnails"));
	if (TestNotNull(TEXT("A header table that changes is a pattern"), Thumbnails))
	{
		TestEqual(TEXT("It was observed in every save"), Thumbnails->ObservationCount, 4);
		TestEqual(TEXT("It changed in every save"), Thumbnails->ChangeCount, 4);
		TestEqual(TEXT("Its changed bytes are added up"), Thumbnails->TotalChangedBytes, static_cast<int64>(32));
		TestEqual(TEXT("It is named by the table"), Thumbnails->DisplayName.ToString(), FString(TEXT("Thumbnail table")));
		TestEqual(TEXT("Different sizes every time are a change of every save"), Thumbnails->ValuePattern, EObservedValuePattern::ContinuouslyChanging);
	}

	const FRepeatedSavePattern* Names = FindPattern(Patterns, TEXT("Header/NameMap"));
	if (TestNotNull(TEXT("A table that changes only sometimes is found too"), Names))
	{
		TestEqual(TEXT("It changed in two saves of four"), Names->ChangeCount, 2);
		TestEqual(TEXT("So it recurs"), Names->ValuePattern, EObservedValuePattern::Recurring);
	}

	const FRepeatedSavePattern* Header = FindPattern(Patterns, TEXT("Header"));
	TestTrue(TEXT("The header as a whole is a pattern"), Header != nullptr && Header->ChangeCount == 4);

	const FRepeatedSavePattern* Speed = FindPattern(Patterns, TEXT("Speed"));
	if (TestNotNull(TEXT("Property patterns are still found"), Speed))
	{
		TestEqual(TEXT("A property changed once is changed once"), Speed->ValuePattern, EObservedValuePattern::ChangedOnce);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
