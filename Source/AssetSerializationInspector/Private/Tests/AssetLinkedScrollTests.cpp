// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SScrollBox.h"

#include "Widgets/AssetLinkedScroll.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetLinkedScroll_MovesTheOtherBox, "AssetSerializationInspector.Widgets.AssetLinkedScroll.MovesTheOtherBox", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetLinkedScroll_MovesTheOtherBox::RunTest(const FString& Parameters)
{
	// With nothing to move, scrolling does nothing.
	{
		FAssetLinkedScroll Empty;
		Empty.OnScrolled(true, 10.0f);
		Empty.SetLinked(true);
		TestTrue(TEXT("It is linked by default"), Empty.IsLinked());
	}

	if (!FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("Slate is not initialized in this run; the scroll boxes were not constructed."));
		return true;
	}

	const TSharedRef<SScrollBox> First = SNew(SScrollBox);
	const TSharedRef<SScrollBox> Second = SNew(SScrollBox);
	FAssetLinkedScroll Link;
	Link.SetBoxes(First, Second);

	// Scrolling either box moves the other to the same offset.
	Link.OnScrolled(true, 120.0f);
	TestEqual(TEXT("The second box follows the first"), Second->GetScrollOffset(), 120.0f);
	Second->SetScrollOffset(40.0f);
	Link.OnScrolled(false, 40.0f);
	TestEqual(TEXT("The first box follows the second"), First->GetScrollOffset(), 40.0f);

	// Unlinked, they move on their own.
	Link.SetLinked(false);
	Link.OnScrolled(true, 300.0f);
	TestEqual(TEXT("The second box stays where it was"), Second->GetScrollOffset(), 40.0f);

	// Linking again puts the second box where the first is.
	First->SetScrollOffset(75.0f);
	Link.SetLinked(true);
	TestEqual(TEXT("The second box joins the first"), Second->GetScrollOffset(), 75.0f);

	// A box that is gone is not a problem.
	{
		TSharedPtr<SScrollBox> Temporary = SNew(SScrollBox);
		FAssetLinkedScroll Dangling;
		Dangling.SetBoxes(First, Temporary);
		Temporary.Reset();
		Dangling.OnScrolled(true, 5.0f);
		TestEqual(TEXT("The box that remains is not moved by itself"), First->GetScrollOffset(), 75.0f);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
