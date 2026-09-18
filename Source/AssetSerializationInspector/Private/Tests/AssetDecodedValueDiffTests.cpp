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

#endif // WITH_DEV_AUTOMATION_TESTS
