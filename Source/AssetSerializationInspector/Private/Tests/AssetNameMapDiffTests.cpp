// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Diff/AssetPackageDiff.h"
#include "Model/AssetPackageDocument.h"

namespace NameMapDiffTestUtils
{
	/** A package that is only a name map, with a few bytes so that two of them are not identical files. */
	static FAssetPackageDocument MakeDocument(const TArray<FString>& Names, const uint8 Fill)
	{
		FAssetPackageDocument Document;
		for (const FString& Name : Names)
		{
			FAssetPackageNameEntry Entry;
			Entry.Name = Name;
			Entry.Offset = Document.NameMap.Num() * 16;
			Entry.Size = 16;
			Entry.NonCasePreservingHash = GetTypeHash(Name.ToLower());
			Entry.CasePreservingHash = GetTypeHash(Name);
			Document.NameMap.Add(Entry);
		}

		Document.FileData.Init(Fill, 64);
		return Document;
	}

	static const FAssetPackageDiffEntry* FindNameMap(const FAssetPackageDiffResult& Diff)
	{
		return Diff.Entries.FindByPredicate([](const FAssetPackageDiffEntry& Entry) { return Entry.Kind == EAssetPackageDiffKind::Name; });
	}

	static int32 Count(const FAssetPackageDiffEntry& NameMap, const EAssetPackageDiffState State)
	{
		int32 Result = 0;
		for (const FAssetPackageDiffEntry& Child : NameMap.Children)
		{
			Result += Child.State == State ? 1 : 0;
		}
		return Result;
	}

	static const FAssetPackageDiffEntry* FindByValue(const FAssetPackageDiffEntry& NameMap, const FString& Name)
	{
		return NameMap.Children.FindByPredicate([&Name](const FAssetPackageDiffEntry& Child) { return Child.NewValue == Name || (Child.NewValue.IsEmpty() && Child.OldValue == Name); });
	}
} // namespace NameMapDiffTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetNameMapDiff_ComparesNamesNotPlaces, "AssetSerializationInspector.Diff.AssetPackageDiff.ComparesNamesNotPlaces", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetNameMapDiff_ComparesNamesNotPlaces::RunTest(const FString& Parameters)
{
	using namespace NameMapDiffTestUtils;

	// A name is inserted in the middle: every name after it moves one place, and none of them changed.
	const FAssetPackageDocument Old = MakeDocument({ TEXT("None"), TEXT("NewVar"), TEXT("NewVar_1"), TEXT("Speed") }, 1);
	const FAssetPackageDocument New = MakeDocument({ TEXT("None"), TEXT("NewVar"), TEXT("Rotator"), TEXT("NewVar_1"), TEXT("Speed") }, 2);

	const FAssetPackageDiffResult Diff = AssetPackageDiff::Compare(Old, New, nullptr, nullptr);
	const FAssetPackageDiffEntry* NameMap = FindNameMap(Diff);
	if (!TestNotNull(TEXT("The name map is compared"), NameMap))
	{
		return false;
	}

	TestEqual(TEXT("Only the new name was added"), Count(*NameMap, EAssetPackageDiffState::Added), 1);
	TestEqual(TEXT("No name is reported as changed"), Count(*NameMap, EAssetPackageDiffState::Modified), 0);
	TestEqual(TEXT("Nothing was removed"), Count(*NameMap, EAssetPackageDiffState::Removed), 0);

	if (const FAssetPackageDiffEntry* Added = FindByValue(*NameMap, TEXT("Rotator")))
	{
		TestEqual(TEXT("Rotator is the addition"), Added->State, EAssetPackageDiffState::Added);
	}

	// The names after the insertion moved, and say where they were.
	for (const TCHAR* Name : { TEXT("NewVar_1"), TEXT("Speed") })
	{
		const FAssetPackageDiffEntry* Moved = FindByValue(*NameMap, Name);
		if (TestNotNull(*FString::Printf(TEXT("%s is in the diff"), Name), Moved))
		{
			TestEqual(*FString::Printf(TEXT("%s only moved"), Name), Moved->State, EAssetPackageDiffState::Moved);
			TestEqual(*FString::Printf(TEXT("%s keeps its name"), Name), Moved->OldValue, Moved->NewValue);
			TestTrue(*FString::Printf(TEXT("%s says where it came from"), Name), Moved->Explanation.ToString().Contains(TEXT("moved from Name[")));
		}
	}

	const FAssetPackageDiffEntry* Untouched = FindByValue(*NameMap, TEXT("NewVar"));
	if (TestNotNull(TEXT("NewVar is in the diff"), Untouched))
	{
		TestEqual(TEXT("NewVar, which neither changed nor moved, is unchanged"), Untouched->State, EAssetPackageDiffState::Unchanged);
	}

	// A name that goes away is removed, and the ones after it move up.
	const FAssetPackageDiffResult Reverse = AssetPackageDiff::Compare(New, Old, nullptr, nullptr);
	const FAssetPackageDiffEntry* ReverseNames = FindNameMap(Reverse);
	if (TestNotNull(TEXT("The reverse comparison has a name map"), ReverseNames))
	{
		TestEqual(TEXT("Rotator was removed"), Count(*ReverseNames, EAssetPackageDiffState::Removed), 1);
		TestEqual(TEXT("And nothing else changed"), Count(*ReverseNames, EAssetPackageDiffState::Modified), 0);
		TestEqual(TEXT("Nothing was added"), Count(*ReverseNames, EAssetPackageDiffState::Added), 0);
	}

	// Identical maps in the same places are unchanged.
	const FAssetPackageDiffResult Same = AssetPackageDiff::Compare(Old, MakeDocument({ TEXT("None"), TEXT("NewVar"), TEXT("NewVar_1"), TEXT("Speed") }, 3), nullptr, nullptr);
	if (const FAssetPackageDiffEntry* SameNames = FindNameMap(Same))
	{
		TestEqual(
			TEXT("Nothing is added"), Count(*SameNames, EAssetPackageDiffState::Added) + Count(*SameNames, EAssetPackageDiffState::Removed) + Count(*SameNames, EAssetPackageDiffState::Modified), 0);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
