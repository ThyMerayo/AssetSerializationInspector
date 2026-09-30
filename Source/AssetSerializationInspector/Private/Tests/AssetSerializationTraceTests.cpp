// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Trace/AssetSerializationTrace.h"

namespace AssetSerializationTraceTestUtils
{
	static TSharedRef<FAssetSerializationTraceNode> MakeNode(
		const FString& Name, const EAssetSerializationTraceKind Kind, const int64 Offset, const int64 Size, const TSharedPtr<FAssetSerializationTraceNode>& Parent = nullptr)
	{
		TSharedRef<FAssetSerializationTraceNode> Node = MakeShared<FAssetSerializationTraceNode>();
		Node->Name = Name;
		Node->Kind = Kind;
		Node->Offset = Offset;
		Node->Size = Size;

		if (Parent.IsValid())
		{
			Node->Parent = Parent;
			Parent->Children.Add(Node);
		}

		return Node;
	}
} // namespace AssetSerializationTraceTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSerializationTrace_OverlapsDetectsIntersection, "AssetSerializationInspector.Trace.AssetSerializationTrace.OverlapsDetectsIntersection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSerializationTrace_OverlapsDetectsIntersection::RunTest(const FString& Parameters)
{
	FAssetSerializationTraceNode Node;
	Node.Offset = 10;
	Node.Size = 10; // covers [10, 20)

	TestTrue(TEXT("A span fully inside the node overlaps"), Node.Overlaps(12, 4));
	TestTrue(TEXT("A span straddling the start overlaps"), Node.Overlaps(5, 10));
	TestTrue(TEXT("A span straddling the end overlaps"), Node.Overlaps(15, 10));
	TestFalse(TEXT("A span entirely before the node does not overlap"), Node.Overlaps(0, 5));
	TestFalse(TEXT("A span entirely after the node does not overlap"), Node.Overlaps(20, 5));
	TestFalse(TEXT("A zero-size span never overlaps"), Node.Overlaps(12, 0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSerializationTrace_IntersectRangesComputesOverlap, "AssetSerializationInspector.Trace.AssetSerializationTrace.IntersectRangesComputesOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSerializationTrace_IntersectRangesComputesOverlap::RunTest(const FString& Parameters)
{
	int64 OutOffset = 0;
	int64 OutSize = 0;

	const bool bIntersects = AssetSerializationTrace::IntersectRanges(0, 10, 5, 10, OutOffset, OutSize);

	TestTrue(TEXT("Overlapping ranges should intersect"), bIntersects);
	TestEqual(TEXT("Intersection offset"), OutOffset, static_cast<int64>(5));
	TestEqual(TEXT("Intersection size"), OutSize, static_cast<int64>(5));

	const bool bDisjoint = AssetSerializationTrace::IntersectRanges(0, 5, 10, 5, OutOffset, OutSize);
	TestFalse(TEXT("Disjoint ranges should not intersect"), bDisjoint);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSerializationTrace_FindDeepestFieldTraceNodeReturnsInnermostMatch,
	"AssetSerializationInspector.Trace.AssetSerializationTrace.FindDeepestFieldTraceNodeReturnsInnermostMatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSerializationTrace_FindDeepestFieldTraceNodeReturnsInnermostMatch::RunTest(const FString& Parameters)
{
	using namespace AssetSerializationTraceTestUtils;

	// Root [0, 100) -> Struct [10, 30) -> Leaf [12, 4)
	const TSharedRef<FAssetSerializationTraceNode> Root = MakeNode(TEXT("Root"), EAssetSerializationTraceKind::Object, 0, 100);
	const TSharedRef<FAssetSerializationTraceNode> Struct = MakeNode(TEXT("MyStruct"), EAssetSerializationTraceKind::Struct, 10, 20, Root);
	MakeNode(TEXT("Leaf"), EAssetSerializationTraceKind::Property, 12, 4, Struct);

	const FAssetSerializationTraceNode* Found = AssetSerializationTrace::FindDeepestFieldTraceNode(Root, 12, 4);

	if (!TestNotNull(TEXT("A matching node should be found"), Found))
	{
		return false;
	}

	TestEqual(TEXT("The deepest matching node should be returned"), Found->Name, FString(TEXT("Leaf")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSerializationTrace_FindDeepestOverlappingFieldNodesOnlyReturnsLeaves,
	"AssetSerializationInspector.Trace.AssetSerializationTrace.FindDeepestOverlappingFieldNodesOnlyReturnsLeaves", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSerializationTrace_FindDeepestOverlappingFieldNodesOnlyReturnsLeaves::RunTest(const FString& Parameters)
{
	using namespace AssetSerializationTraceTestUtils;

	// Root [0, 100) -> Struct [0, 20) -> { LeafA [0, 10), LeafB [10, 10) }
	const TSharedRef<FAssetSerializationTraceNode> Root = MakeNode(TEXT("Root"), EAssetSerializationTraceKind::Object, 0, 100);
	const TSharedRef<FAssetSerializationTraceNode> Struct = MakeNode(TEXT("MyStruct"), EAssetSerializationTraceKind::Struct, 0, 20, Root);
	MakeNode(TEXT("LeafA"), EAssetSerializationTraceKind::Property, 0, 10, Struct);
	MakeNode(TEXT("LeafB"), EAssetSerializationTraceKind::Property, 10, 10, Struct);

	TArray<const FAssetSerializationTraceNode*> Found;
	AssetSerializationTrace::FindDeepestOverlappingFieldNodes(Root, 0, 20, Found);

	if (!TestEqual(TEXT("Both leaves should be found"), Found.Num(), 2))
	{
		return false;
	}

	TestEqual(TEXT("First leaf name"), Found[0]->Name, FString(TEXT("LeafA")));
	TestEqual(TEXT("Second leaf name"), Found[1]->Name, FString(TEXT("LeafB")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
