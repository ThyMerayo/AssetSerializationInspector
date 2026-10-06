// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Diff/AssetDecodedValueDiff.h"
#include "Serialization/AssetPropertyValueDecoder.h"

namespace AssetDecodedValueDiffTestUtils
{
	static FAssetDecodedPropertyValue MakeScalar(const FString& Name, const FString& TypeName, const FString& Value)
	{
		FAssetDecodedPropertyValue Result;
		Result.Status = EAssetPropertyDecodeStatus::Success;
		Result.Kind = EAssetDecodedValueKind::Scalar;
		Result.Name = Name;
		Result.TypeName = TypeName;
		Result.Value = Value;
		return Result;
	}
} // namespace AssetDecodedValueDiffTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetDecodedValueDiff_UnchangedScalarsReportUnchanged, "AssetSerializationInspector.Diff.AssetDecodedValueDiff.UnchangedScalarsReportUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetDecodedValueDiff_UnchangedScalarsReportUnchanged::RunTest(const FString& Parameters)
{
	using namespace AssetDecodedValueDiffTestUtils;

	const FAssetDecodedPropertyValue OldValue = MakeScalar(TEXT("TestFloat"), TEXT("FloatProperty"), TEXT("1.0"));
	const FAssetDecodedPropertyValue NewValue = MakeScalar(TEXT("TestFloat"), TEXT("FloatProperty"), TEXT("1.0"));

	const FAssetDecodedValueDiff Diff = FAssetDecodedValueDiffer::Compare(&OldValue, &NewValue);

	TestEqual(TEXT("Two identical scalars should be unchanged"), static_cast<uint8>(Diff.State), static_cast<uint8>(EAssetDecodedValueDiffState::Unchanged));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetDecodedValueDiff_ChangedScalarsReportModified, "AssetSerializationInspector.Diff.AssetDecodedValueDiff.ChangedScalarsReportModified",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetDecodedValueDiff_ChangedScalarsReportModified::RunTest(const FString& Parameters)
{
	using namespace AssetDecodedValueDiffTestUtils;

	const FAssetDecodedPropertyValue OldValue = MakeScalar(TEXT("TestFloat"), TEXT("FloatProperty"), TEXT("1.0"));
	const FAssetDecodedPropertyValue NewValue = MakeScalar(TEXT("TestFloat"), TEXT("FloatProperty"), TEXT("2.0"));

	const FAssetDecodedValueDiff Diff = FAssetDecodedValueDiffer::Compare(&OldValue, &NewValue);

	TestEqual(TEXT("A changed scalar value should be reported as Modified"), static_cast<uint8>(Diff.State), static_cast<uint8>(EAssetDecodedValueDiffState::Modified));
	TestEqual(TEXT("Old value should be preserved"), Diff.OldValue, FString(TEXT("1.0")));
	TestEqual(TEXT("New value should be preserved"), Diff.NewValue, FString(TEXT("2.0")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetDecodedValueDiff_AddedAndRemovedValuesAreClassified, "AssetSerializationInspector.Diff.AssetDecodedValueDiff.AddedAndRemovedValuesAreClassified",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetDecodedValueDiff_AddedAndRemovedValuesAreClassified::RunTest(const FString& Parameters)
{
	using namespace AssetDecodedValueDiffTestUtils;

	const FAssetDecodedPropertyValue NewValue = MakeScalar(TEXT("TestFloat"), TEXT("FloatProperty"), TEXT("2.0"));
	const FAssetDecodedValueDiff AddedDiff = FAssetDecodedValueDiffer::Compare(nullptr, &NewValue);
	TestEqual(TEXT("A value with no old side is Added"), static_cast<uint8>(AddedDiff.State), static_cast<uint8>(EAssetDecodedValueDiffState::Added));

	const FAssetDecodedPropertyValue OldValue = MakeScalar(TEXT("TestFloat"), TEXT("FloatProperty"), TEXT("1.0"));
	const FAssetDecodedValueDiff RemovedDiff = FAssetDecodedValueDiffer::Compare(&OldValue, nullptr);
	TestEqual(TEXT("A value with no new side is Removed"), static_cast<uint8>(RemovedDiff.State), static_cast<uint8>(EAssetDecodedValueDiffState::Removed));

	return true;
}

// This is the key regression test to protect the documented "sequence-aware
// matching" behavior described in the README: inserting an element in the
// middle of an array must not be reported as N modifications of every
// following element.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetDecodedValueDiff_ArrayElementInsertionDoesNotShiftUnrelatedElements,
	"AssetSerializationInspector.Diff.AssetDecodedValueDiff.ArrayElementInsertionDoesNotShiftUnrelatedElements", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetDecodedValueDiff_ArrayElementInsertionDoesNotShiftUnrelatedElements::RunTest(const FString& Parameters)
{
	using namespace AssetDecodedValueDiffTestUtils;

	FAssetDecodedPropertyValue OldArray;
	OldArray.Kind = EAssetDecodedValueKind::Array;
	OldArray.TypeName = TEXT("IntProperty");
	OldArray.Children.Add(MakeScalar(TEXT("[0]"), TEXT("IntProperty"), TEXT("10")));
	OldArray.Children.Add(MakeScalar(TEXT("[1]"), TEXT("IntProperty"), TEXT("30")));
	for (FAssetDecodedPropertyValue& Child : OldArray.Children)
	{
		Child.SemanticKey = FAssetPropertyValueDecoder::BuildSemanticValueKey(Child);
	}

	FAssetDecodedPropertyValue NewArray;
	NewArray.Kind = EAssetDecodedValueKind::Array;
	NewArray.TypeName = TEXT("IntProperty");
	NewArray.Children.Add(MakeScalar(TEXT("[0]"), TEXT("IntProperty"), TEXT("10")));
	NewArray.Children.Add(MakeScalar(TEXT("[1]"), TEXT("IntProperty"), TEXT("20"))); // inserted
	NewArray.Children.Add(MakeScalar(TEXT("[2]"), TEXT("IntProperty"), TEXT("30")));
	for (FAssetDecodedPropertyValue& Child : NewArray.Children)
	{
		Child.SemanticKey = FAssetPropertyValueDecoder::BuildSemanticValueKey(Child);
	}

	const FAssetDecodedValueDiff Diff = FAssetDecodedValueDiffer::Compare(&OldArray, &NewArray);

	int32 AddedCount = 0;
	int32 UnchangedCount = 0;

	for (const FAssetDecodedValueDiff& Child : Diff.Children)
	{
		if (Child.State == EAssetDecodedValueDiffState::Added)
		{
			++AddedCount;
			TestEqual(TEXT("The inserted element should be the new value"), Child.NewValue, FString(TEXT("20")));
		}
		else if (Child.State == EAssetDecodedValueDiffState::Unchanged)
		{
			++UnchangedCount;
		}
	}

	TestEqual(TEXT("LCS matching should report exactly one inserted element"), AddedCount, 1);
	TestEqual(TEXT("The two untouched values should remain Unchanged, not Modified"), UnchangedCount, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetDecodedValueDiff_ComparesTheDecodedPartOfAnArray, "AssetSerializationInspector.Diff.AssetDecodedValueDiff.ComparesTheDecodedPartOfAnArray",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetDecodedValueDiff_ComparesTheDecodedPartOfAnArray::RunTest(const FString& Parameters)
{
	using namespace AssetDecodedValueDiffTestUtils;

	const auto MakeArray = [](const TArray<FString>& Values, const bool bPartial) {
		FAssetDecodedPropertyValue Array;
		Array.Status = bPartial ? EAssetPropertyDecodeStatus::Partial : EAssetPropertyDecodeStatus::Success;
		Array.Kind = EAssetDecodedValueKind::Array;
		Array.TypeName = TEXT("ArrayProperty(IntProperty)");
		Array.Value = bPartial ? FString::Printf(TEXT("%d of 9 elements decoded"), Values.Num()) : FString::Printf(TEXT("%d elements"), Values.Num());

		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			FAssetDecodedPropertyValue Element = MakeScalar(FString::Printf(TEXT("[%d]"), Index), TEXT("IntProperty"), Values[Index]);
			Element.SemanticKey = FAssetPropertyValueDecoder::BuildSemanticValueKey(Element);
			Array.Children.Add(Element);
		}

		if (bPartial)
		{
			FAssetDecodedPropertyValue Failed;
			Failed.Status = EAssetPropertyDecodeStatus::InvalidData;
			Failed.Name = FString::Printf(TEXT("[%d]"), Values.Num());
			Array.Children.Add(Failed);
		}

		return Array;
	};

	// The old array decoded three elements before failing; the new one is complete. The third differs, the rest is not compared.
	const FAssetDecodedPropertyValue Old = MakeArray({ TEXT("1"), TEXT("2"), TEXT("9") }, true);
	const FAssetDecodedPropertyValue New = MakeArray({ TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5") }, false);
	const FAssetDecodedValueDiff Diff = FAssetDecodedValueDiffer::Compare(&Old, &New);

	TestEqual(TEXT("The array is modified"), Diff.State, EAssetDecodedValueDiffState::Modified);
	if (TestEqual(TEXT("The compared elements and the stop marker are reported"), Diff.Children.Num(), 4))
	{
		TestEqual(TEXT("The first element is unchanged"), Diff.Children[0].State, EAssetDecodedValueDiffState::Unchanged);
		TestEqual(TEXT("The second element is unchanged"), Diff.Children[1].State, EAssetDecodedValueDiffState::Unchanged);
		TestEqual(TEXT("The third element changed"), Diff.Children[2].State, EAssetDecodedValueDiffState::Modified);
		TestEqual(TEXT("From the old value"), Diff.Children[2].OldValue, FString(TEXT("9")));
		TestEqual(TEXT("To the new one"), Diff.Children[2].NewValue, FString(TEXT("3")));

		const FAssetDecodedValueDiff& Rest = Diff.Children[3];
		TestEqual(TEXT("The marker says where the comparison stops"), Rest.Name, FString(TEXT("[3...]")));
		TestEqual(TEXT("The partial side says why"), Rest.OldValue, FString(TEXT("3 of 9 elements decoded; not compared")));
		TestEqual(TEXT("The complete side says what was left out"), Rest.NewValue, FString(TEXT("2 more elements; not compared")));
	}

	// Elements nobody could read are not reported as removed.
	const FAssetDecodedPropertyValue Same = MakeArray({ TEXT("1"), TEXT("2"), TEXT("3") }, false);
	const FAssetDecodedValueDiff SamePrefix = FAssetDecodedValueDiffer::Compare(&Old, &Same);
	for (const FAssetDecodedValueDiff& Child : SamePrefix.Children)
	{
		TestTrue(TEXT("Nothing is added or removed"), Child.State != EAssetDecodedValueDiffState::Added && Child.State != EAssetDecodedValueDiffState::Removed);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetDecodedValueDiff_ReportsAChangeOfCase, "AssetSerializationInspector.Diff.AssetDecodedValueDiff.ReportsAChangeOfCase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetDecodedValueDiff_ReportsAChangeOfCase::RunTest(const FString& Parameters)
{
	using namespace AssetDecodedValueDiffTestUtils;

	// FString compares without regard to case, so "Hello" and "hello" used to be the same value.
	const FAssetDecodedPropertyValue OldText = MakeScalar(TEXT("Label"), TEXT("StrProperty"), TEXT("Hello"));
	const FAssetDecodedPropertyValue NewText = MakeScalar(TEXT("Label"), TEXT("StrProperty"), TEXT("hello"));
	const FAssetDecodedValueDiff Scalar = FAssetDecodedValueDiffer::Compare(&OldText, &NewText);
	TestEqual(TEXT("A string that changed only in case is modified"), static_cast<uint8>(Scalar.State), static_cast<uint8>(EAssetDecodedValueDiffState::Modified));

	const FAssetDecodedPropertyValue Same = MakeScalar(TEXT("Label"), TEXT("StrProperty"), TEXT("Hello"));
	TestEqual(TEXT("The same string is unchanged"), static_cast<uint8>(FAssetDecodedValueDiffer::Compare(&OldText, &Same).State), static_cast<uint8>(EAssetDecodedValueDiffState::Unchanged));

	// The same inside an array of strings.
	const auto MakeArray = [](const TCHAR* First) {
		FAssetDecodedPropertyValue Array;
		Array.Status = EAssetPropertyDecodeStatus::Success;
		Array.Kind = EAssetDecodedValueKind::Array;
		Array.Name = TEXT("Names");
		Array.TypeName = TEXT("ArrayProperty(StrProperty)");
		Array.Value = TEXT("2 elements");
		Array.Children.Add(MakeScalar(TEXT("[0]"), TEXT("StrProperty"), First));
		Array.Children.Add(MakeScalar(TEXT("[1]"), TEXT("StrProperty"), TEXT("b")));
		return Array;
	};

	const FAssetDecodedPropertyValue OldArray = MakeArray(TEXT("A"));
	const FAssetDecodedPropertyValue NewArray = MakeArray(TEXT("a"));
	const FAssetDecodedValueDiff Array = FAssetDecodedValueDiffer::Compare(&OldArray, &NewArray);

	int32 Changed = 0;
	for (const FAssetDecodedValueDiff& Child : Array.Children)
	{
		Changed += Child.State != EAssetDecodedValueDiffState::Unchanged ? 1 : 0;
	}
	TestTrue(TEXT("An element that changed only in case is reported"), Changed > 0);
	return true;
}

namespace AssetDecodedValueDiffTestUtils
{
	static FAssetDecodedPropertyValue MakeGuidMapEntry(const FString& Key, const FString& Guid)
	{
		FAssetDecodedPropertyValue Entry;
		Entry.Status = EAssetPropertyDecodeStatus::Success;
		Entry.Kind = EAssetDecodedValueKind::MapEntry;
		Entry.SemanticKey = Key;
		Entry.Children.Add(MakeScalar(TEXT("Key"), TEXT("NameProperty"), Key));
		Entry.Children.Add(MakeScalar(TEXT("Value"), TEXT("StructProperty(Guid(/Script/CoreUObject))"), Guid));
		return Entry;
	}

	static FAssetDecodedPropertyValue MakeGuidMap(const TArray<TPair<FString, FString>>& Entries)
	{
		FAssetDecodedPropertyValue Map;
		Map.Status = EAssetPropertyDecodeStatus::Success;
		Map.Kind = EAssetDecodedValueKind::Map;
		Map.Name = TEXT("PropertyGuids");
		Map.TypeName = TEXT("MapProperty(NameProperty,StructProperty(Guid(/Script/CoreUObject)))");
		Map.Value = FString::Printf(TEXT("%d entries"), Entries.Num());
		for (const TPair<FString, FString>& Entry : Entries)
		{
			Map.Children.Add(MakeGuidMapEntry(Entry.Key, Entry.Value));
		}
		return Map;
	}

	static int32 CountState(const FAssetDecodedValueDiff& Diff, const EAssetDecodedValueDiffState State)
	{
		int32 Count = 0;
		for (const FAssetDecodedValueDiff& Child : Diff.Children)
		{
			Count += Child.State == State ? 1 : 0;
		}
		return Count;
	}
} // namespace AssetDecodedValueDiffTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetDecodedValueDiff_PairsARenamedKeyByItsGuid, "AssetSerializationInspector.Diff.AssetDecodedValueDiff.PairsARenamedKeyByItsGuid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetDecodedValueDiff_PairsARenamedKeyByItsGuid::RunTest(const FString& Parameters)
{
	using namespace AssetDecodedValueDiffTestUtils;

	const FString First = TEXT("11111111-1111-1111-1111-111111111111");
	const FString Second = TEXT("22222222-2222-2222-2222-222222222222");
	const FString Third = TEXT("33333333-3333-3333-3333-333333333333");
	const FString Zero = TEXT("00000000-0000-0000-0000-000000000000");

	// One key renamed, the other untouched: a modification of the key, not a removal and an addition.
	{
		const FAssetDecodedPropertyValue Old = MakeGuidMap({ { TEXT("Speed"), First }, { TEXT("Health"), Second } });
		const FAssetDecodedPropertyValue New = MakeGuidMap({ { TEXT("Velocity"), First }, { TEXT("Health"), Second } });
		const FAssetDecodedValueDiff Diff = FAssetDecodedValueDiffer::Compare(&Old, &New);

		TestEqual(TEXT("The map is modified"), static_cast<uint8>(Diff.State), static_cast<uint8>(EAssetDecodedValueDiffState::Modified));
		TestEqual(TEXT("Nothing is reported as added"), CountState(Diff, EAssetDecodedValueDiffState::Added), 0);
		TestEqual(TEXT("Nothing is reported as removed"), CountState(Diff, EAssetDecodedValueDiffState::Removed), 0);
		if (TestEqual(TEXT("One entry is modified"), CountState(Diff, EAssetDecodedValueDiffState::Modified), 1))
		{
			const FAssetDecodedValueDiff* Renamed = Diff.Children.FindByPredicate([](const FAssetDecodedValueDiff& Child) { return Child.State == EAssetDecodedValueDiffState::Modified; });
			if (TestEqual(TEXT("It says what changed: the key"), Renamed->Children.Num(), 1))
			{
				TestEqual(TEXT("From the old name"), Renamed->Children[0].OldValue, FString(TEXT("Speed")));
				TestEqual(TEXT("To the new one"), Renamed->Children[0].NewValue, FString(TEXT("Velocity")));
			}
		}
	}

	// Another GUID is another property: the removal and the addition stay.
	{
		const FAssetDecodedPropertyValue Old = MakeGuidMap({ { TEXT("Speed"), First } });
		const FAssetDecodedPropertyValue New = MakeGuidMap({ { TEXT("Velocity"), Third } });
		const FAssetDecodedValueDiff Diff = FAssetDecodedValueDiffer::Compare(&Old, &New);
		TestEqual(TEXT("A different GUID is an addition"), CountState(Diff, EAssetDecodedValueDiffState::Added), 1);
		TestEqual(TEXT("And a removal"), CountState(Diff, EAssetDecodedValueDiffState::Removed), 1);
	}

	// An empty GUID names nothing, so it pairs nothing.
	{
		const FAssetDecodedPropertyValue Old = MakeGuidMap({ { TEXT("Speed"), Zero } });
		const FAssetDecodedPropertyValue New = MakeGuidMap({ { TEXT("Velocity"), Zero } });
		const FAssetDecodedValueDiff Diff = FAssetDecodedValueDiffer::Compare(&Old, &New);
		TestEqual(TEXT("An empty GUID is not matched"), CountState(Diff, EAssetDecodedValueDiffState::Added), 1);
	}

	// A GUID that two new entries hold is not matched either: which of them is the old one is a guess.
	{
		const FAssetDecodedPropertyValue Old = MakeGuidMap({ { TEXT("Speed"), First } });
		const FAssetDecodedPropertyValue New = MakeGuidMap({ { TEXT("Velocity"), First }, { TEXT("Pace"), First } });
		const FAssetDecodedValueDiff Diff = FAssetDecodedValueDiffer::Compare(&Old, &New);
		TestEqual(TEXT("An ambiguous GUID is not matched"), CountState(Diff, EAssetDecodedValueDiffState::Removed), 1);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
