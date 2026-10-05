// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Diff/AssetPackageDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Save/AssetSaveAnalyzer.h"
#include "Serialization/AssetSerializedPropertyTag.h"
#include "Trace/AssetSerializationTrace.h"

namespace RepresentationOnlyTestUtils
{
	/** A synthetic package with one export that stores one property, "Tags", whose payload is the given 32 bit words. */
	struct FOnePropertyPackage
	{
		FAssetPackageDocument Document;
		FAssetPackageTraceCollection Traces;

		FOnePropertyPackage(const FAssetSerializedPropertyType& Type, const TArray<int32>& Words)
		{
			FAssetPackageNameEntry NameEntry;
			NameEntry.Name = TEXT("Holder");

			FAssetPackageExportEntry Export;
			Export.Index = 0;
			Export.ObjectName.NameIndex = Document.NameMap.Add(NameEntry);
			Export.SerialOffset = 0;

			for (const int32 Word : Words)
			{
				const int32 Start = Document.FileData.Num();
				Document.FileData.AddUninitialized(sizeof(int32));
				FMemory::Memcpy(Document.FileData.GetData() + Start, &Word, sizeof(int32));
			}

			Export.SerialSize = Document.FileData.Num();
			Document.ExportMap.Add(Export);
			Document.bHasDecodedExportMap = true;

			TSharedPtr<FAssetSerializationTraceNode> Node = MakeShared<FAssetSerializationTraceNode>();
			Node->Kind = EAssetSerializationTraceKind::Property;
			Node->Name = TEXT("Tags");
			Node->TypeName = Type.ToString();
			Node->PropertyType = Type;
			Node->Offset = 0;
			Node->Size = Export.SerialSize;

			FAssetSerializationTrace Trace;
			Trace.Root = MakeShared<FAssetSerializationTraceNode>();
			Trace.Root->Children.Add(Node);
			Traces.ExportTraces.Add(0, MoveTemp(Trace));
		}
	};

	static FAssetSerializedPropertyType MakeType(const FString& Name, const TArray<FString>& Parameters)
	{
		FAssetSerializedPropertyType Type;
		Type.Name = Name;
		for (const FString& Parameter : Parameters)
		{
			FAssetSerializedPropertyType Inner;
			Inner.Name = Parameter;
			Type.Parameters.Add(Inner);
		}
		return Type;
	}

	static const FAssetPackageDiffEntry* FindProperty(const FAssetPackageDiffEntry& Entry, const FString& Name)
	{
		if (Entry.Kind == EAssetPackageDiffKind::Property && Entry.DisplayName.ToString() == Name)
		{
			return &Entry;
		}

		for (const FAssetPackageDiffEntry& Child : Entry.Children)
		{
			if (const FAssetPackageDiffEntry* Found = FindProperty(Child, Name))
			{
				return Found;
			}
		}
		return nullptr;
	}

	static const FAssetPackageDiffEntry* FindProperty(const FAssetPackageDiffResult& Diff, const FString& Name)
	{
		for (const FAssetPackageDiffEntry& Entry : Diff.Entries)
		{
			if (const FAssetPackageDiffEntry* Found = FindProperty(Entry, Name))
			{
				return Found;
			}
		}
		return nullptr;
	}
} // namespace RepresentationOnlyTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetRepresentationOnly_RecognizesAMapOrSetStoredInAnotherOrder, "AssetSerializationInspector.Diff.AssetPackageDiff.RecognizesAMapOrSetStoredInAnotherOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetRepresentationOnly_RecognizesAMapOrSetStoredInAnotherOrder::RunTest(const FString& Parameters)
{
	using namespace RepresentationOnlyTestUtils;

	const FAssetSerializedPropertyType SetOfInt = MakeType(TEXT("SetProperty"), { TEXT("IntProperty") });
	const FAssetSerializedPropertyType MapOfInts = MakeType(TEXT("MapProperty"), { TEXT("IntProperty"), TEXT("IntProperty") });

	// The diffs and packages are kept so that the entries a comparison returns stay valid.
	TArray<TSharedPtr<FAssetPackageDiffResult>> Keep;
	TArray<TUniquePtr<FOnePropertyPackage>> Packages;

	const auto Compare = [&Keep, &Packages](
							 const FAssetSerializedPropertyType& Type, const TArray<int32>& OldWords, const TArray<int32>& NewWords, FAssetSaveAnalysis& OutAnalysis) -> const FAssetPackageDiffEntry* {
		Packages.Add(MakeUnique<FOnePropertyPackage>(Type, OldWords));
		Packages.Add(MakeUnique<FOnePropertyPackage>(Type, NewWords));
		FOnePropertyPackage& Old = *Packages[Packages.Num() - 2];
		FOnePropertyPackage& New = *Packages[Packages.Num() - 1];

		const TSharedPtr<FAssetPackageDiffResult> Diff = MakeShared<FAssetPackageDiffResult>(AssetPackageDiff::Compare(Old.Document, New.Document, &Old.Traces, &New.Traces));
		Keep.Add(Diff);
		OutAnalysis = FAssetSaveAnalyzer::Analyze(*Diff, Old.Document, New.Document);
		return FindProperty(*Diff, TEXT("Tags"));
	};

	FAssetSaveAnalysis Analysis;

	// A set of {1, 2} written as 1, 2 and as 2, 1.
	const FAssetPackageDiffEntry* Reordered = Compare(SetOfInt, { 0, 2, 1, 2 }, { 0, 2, 2, 1 }, Analysis);
	if (TestNotNull(TEXT("The reordered set is in the diff"), Reordered))
	{
		TestTrue(TEXT("It is recognized as the same value stored differently"), Reordered->bRepresentationOnly);
		TestTrue(TEXT("And says why"), Reordered->Explanation.ToString().Contains(TEXT("another order")));
	}
	TestEqual(TEXT("It is not a property change"), Analysis.PropertyChangeCount, 0);
	TestEqual(TEXT("The save is a layout change only"), Analysis.ResultKind, EAssetSaveResultKind::LayoutOnly);
	if (TestEqual(TEXT("One layout change"), Analysis.LayoutChanges.Num(), 1))
	{
		TestEqual(TEXT("Of the kind that says so"), Analysis.LayoutChanges[0].Classification, EAssetSaveChangeClassification::PropertyStoredDifferently);
	}

	// A map of 1 -> 10 and 2 -> 20, in both orders.
	const FAssetPackageDiffEntry* Map = Compare(MapOfInts, { 0, 2, 1, 10, 2, 20 }, { 0, 2, 2, 20, 1, 10 }, Analysis);
	if (TestNotNull(TEXT("The reordered map is in the diff"), Map))
	{
		TestTrue(TEXT("It is recognized too"), Map->bRepresentationOnly);
	}

	// A real change is a real change.
	const FAssetPackageDiffEntry* Changed = Compare(SetOfInt, { 0, 2, 1, 2 }, { 0, 2, 2, 3 }, Analysis);
	if (TestNotNull(TEXT("The changed set is in the diff"), Changed))
	{
		TestFalse(TEXT("It is not mistaken for a reordering"), Changed->bRepresentationOnly);
	}
	TestTrue(TEXT("It is a property change"), Analysis.PropertyChangeCount > 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
