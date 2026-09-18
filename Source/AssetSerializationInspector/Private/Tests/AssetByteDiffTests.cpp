// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Diff/AssetByteDiff.h"
#include "Model/AssetPackageDocument.h"

namespace AssetByteDiffTestUtils
{
	static FAssetPackageDocument MakeDocument(std::initializer_list<uint8> Bytes)
	{
		FAssetPackageDocument Document;

		for (const uint8 Byte : Bytes)
		{
			Document.FileData.Add(Byte);
		}

		return Document;
	}
} // namespace AssetByteDiffTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetByteDiff_IdenticalRangesProduceNoSpans, "AssetSerializationInspector.Diff.AssetByteDiff.IdenticalRangesProduceNoSpans",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetByteDiff_IdenticalRangesProduceNoSpans::RunTest(const FString& Parameters)
{
	using namespace AssetByteDiffTestUtils;

	const FAssetPackageDocument OldDoc = MakeDocument({ 1, 2, 3, 4, 5 });
	const FAssetPackageDocument NewDoc = MakeDocument({ 1, 2, 3, 4, 5 });

	const TArray<FAssetByteDiffSpan> Spans = FAssetByteDiff::Compare(OldDoc, 0, 5, NewDoc, 0, 5);

	TestEqual(TEXT("Identical byte ranges should not produce any diff spans"), Spans.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetByteDiff_DetectsSingleContiguousChange, "AssetSerializationInspector.Diff.AssetByteDiff.DetectsSingleContiguousChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetByteDiff_DetectsSingleContiguousChange::RunTest(const FString& Parameters)
{
	using namespace AssetByteDiffTestUtils;

	const FAssetPackageDocument OldDoc = MakeDocument({ 1, 2, 3, 4, 5 });
	const FAssetPackageDocument NewDoc = MakeDocument({ 1, 9, 9, 4, 5 });

	const TArray<FAssetByteDiffSpan> Spans = FAssetByteDiff::Compare(OldDoc, 0, 5, NewDoc, 0, 5);

	if (!TestEqual(TEXT("Exactly one changed span is expected"), Spans.Num(), 1))
	{
		return false;
	}

	TestEqual(TEXT("Span should start at the first changed byte"), Spans[0].Offset, static_cast<int64>(1));
	TestEqual(TEXT("Span should cover both changed bytes"), Spans[0].Size, static_cast<int64>(2));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetByteDiff_DetectsMultipleDisjointChanges, "AssetSerializationInspector.Diff.AssetByteDiff.DetectsMultipleDisjointChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetByteDiff_DetectsMultipleDisjointChanges::RunTest(const FString& Parameters)
{
	using namespace AssetByteDiffTestUtils;

	const FAssetPackageDocument OldDoc = MakeDocument({ 1, 2, 3, 4, 5, 6, 7 });
	const FAssetPackageDocument NewDoc = MakeDocument({ 9, 2, 3, 4, 5, 6, 0 });

	const TArray<FAssetByteDiffSpan> Spans = FAssetByteDiff::Compare(OldDoc, 0, 7, NewDoc, 0, 7);

	if (!TestEqual(TEXT("Two disjoint single-byte changes are expected"), Spans.Num(), 2))
	{
		return false;
	}

	TestEqual(TEXT("First span at offset 0"), Spans[0].Offset, static_cast<int64>(0));
	TestEqual(TEXT("First span size 1"), Spans[0].Size, static_cast<int64>(1));
	TestEqual(TEXT("Second span at offset 6"), Spans[1].Offset, static_cast<int64>(6));
	TestEqual(TEXT("Second span size 1"), Spans[1].Size, static_cast<int64>(1));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetByteDiff_TrailingSizeMismatchIsTreatedAsChanged, "AssetSerializationInspector.Diff.AssetByteDiff.TrailingSizeMismatchIsTreatedAsChanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetByteDiff_TrailingSizeMismatchIsTreatedAsChanged::RunTest(const FString& Parameters)
{
	using namespace AssetByteDiffTestUtils;

	const FAssetPackageDocument OldDoc = MakeDocument({ 1, 2, 3 });
	const FAssetPackageDocument NewDoc = MakeDocument({ 1, 2, 3, 4, 5 });

	const TArray<FAssetByteDiffSpan> Spans = FAssetByteDiff::Compare(OldDoc, 0, 3, NewDoc, 0, 5);

	if (!TestEqual(TEXT("The extra trailing bytes should show up as a single span"), Spans.Num(), 1))
	{
		return false;
	}

	TestEqual(TEXT("The trailing span should start right after the common length"), Spans[0].Offset, static_cast<int64>(3));
	TestEqual(TEXT("The trailing span should cover the size difference"), Spans[0].Size, static_cast<int64>(2));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetByteDiff_OutOfRangeRequestProducesNoSpans, "AssetSerializationInspector.Diff.AssetByteDiff.OutOfRangeRequestProducesNoSpans",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetByteDiff_OutOfRangeRequestProducesNoSpans::RunTest(const FString& Parameters)
{
	using namespace AssetByteDiffTestUtils;

	const FAssetPackageDocument OldDoc = MakeDocument({ 1, 2, 3 });
	const FAssetPackageDocument NewDoc = MakeDocument({ 1, 2, 3 });

	// Requesting a range that runs off the end of the buffer should fail safe
	// rather than reading (or comparing) out of bounds.
	const TArray<FAssetByteDiffSpan> Spans = FAssetByteDiff::Compare(OldDoc, 0, 10, NewDoc, 0, 3);

	TestEqual(TEXT("An out-of-range comparison should return no spans"), Spans.Num(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
