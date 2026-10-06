// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Diff/AssetPackageDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetNumberText.h"
#include "Serialization/AssetSerializedPropertyTag.h"
#include "Trace/AssetSerializationTrace.h"

namespace NumberTextTestUtils
{
	/** A synthetic package with one export that stores one property, "Label", of the given type, with the given bytes as its value. */
	struct FValuePackage
	{
		FAssetPackageDocument Document;
		FAssetPackageTraceCollection Traces;

		FValuePackage(const FString& TypeName, const TArray<uint8>& Bytes)
		{
			FAssetPackageNameEntry HolderEntry;
			HolderEntry.Name = TEXT("Holder");

			FAssetPackageExportEntry Export;
			Export.Index = 0;
			Export.ObjectName.NameIndex = Document.NameMap.Add(HolderEntry);
			Export.SerialOffset = 0;

			Document.FileData.Append(Bytes);
			Export.SerialSize = Document.FileData.Num();
			Document.ExportMap.Add(Export);
			Document.bHasDecodedExportMap = true;

			FAssetSerializedPropertyType Type;
			Type.Name = TypeName;

			TSharedPtr<FAssetSerializationTraceNode> Node = MakeShared<FAssetSerializationTraceNode>();
			Node->Kind = EAssetSerializationTraceKind::Property;
			Node->Name = TEXT("Label");
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

	template <typename T> TArray<uint8> BytesOf(const T& Value)
	{
		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(&Value), sizeof(T));
		return Bytes;
	}

	static const FAssetPackageDiffEntry* FindLabel(const FAssetPackageDiffEntry& Entry)
	{
		if (Entry.Kind == EAssetPackageDiffKind::Property && Entry.DisplayName.ToString() == TEXT("Label"))
		{
			return &Entry;
		}

		for (const FAssetPackageDiffEntry& Child : Entry.Children)
		{
			if (const FAssetPackageDiffEntry* Found = FindLabel(Child))
			{
				return Found;
			}
		}
		return nullptr;
	}

	static const FAssetPackageDiffEntry* FindLabel(const FAssetPackageDiffResult& Diff)
	{
		for (const FAssetPackageDiffEntry& Entry : Diff.Entries)
		{
			if (const FAssetPackageDiffEntry* Found = FindLabel(Entry))
			{
				return Found;
			}
		}
		return nullptr;
	}
} // namespace NumberTextTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetNumberText_KeepsTheEnginesTextWhenItIsExact, "AssetSerializationInspector.Serialization.AssetNumberText.KeepsTheEnginesTextWhenItIsExact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetNumberText_KeepsTheEnginesTextWhenItIsExact::RunTest(const FString& Parameters)
{
	// Values that the engine's text gives back exactly are written as they always were.
	TestEqual(TEXT("A float"), AssetNumberText::Text(1.0f), FString(TEXT("1.000000")));
	TestEqual(TEXT("A float with a fraction"), AssetNumberText::Text(0.25f), FString(TEXT("0.250000")));
	TestEqual(TEXT("A double"), AssetNumberText::Text(2.5), LexToString(2.5));
	TestEqual(TEXT("A vector"), AssetNumberText::Text(FVector(1.0, 2.0, 3.0)), FString(TEXT("X=1.000 Y=2.000 Z=3.000")));
	TestEqual(TEXT("A rotator"), AssetNumberText::Text(FRotator(10.0, 20.0, 30.0)), FRotator(10.0, 20.0, 30.0).ToString());
	TestEqual(TEXT("A single precision vector"), AssetNumberText::Text(FVector3f(0.5f, 1.0f, 1.5f)), FVector3f(0.5f, 1.0f, 1.5f).ToString());
	TestEqual(TEXT("Not a number"), AssetNumberText::Text(std::numeric_limits<double>::infinity()), LexToString(std::numeric_limits<double>::infinity()));
	TestEqual(TEXT("An integer"), AssetNumberText::Text(42), FString(TEXT("42")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetNumberText_TellsDifferentValuesApart, "AssetSerializationInspector.Serialization.AssetNumberText.TellsDifferentValuesApart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetNumberText_TellsDifferentValuesApart::RunTest(const FString& Parameters)
{
	// The next float after 1 and a double that is 1 plus a little: the engine prints both as 1.
	const float NextFloat = (1.0f + FLT_EPSILON);
	TestNotEqual(TEXT("Two floats that differ in the last bit"), AssetNumberText::Text(1.0f), AssetNumberText::Text(NextFloat));
	TestEqual(TEXT("The text of a float reads back as the float"), static_cast<float>(FCString::Atod(*AssetNumberText::Text(NextFloat))), NextFloat);

	const double NextDouble = (1.0 + DBL_EPSILON);
	TestNotEqual(TEXT("Two doubles that differ in the last bit"), AssetNumberText::Text(1.0), AssetNumberText::Text(NextDouble));
	TestEqual(TEXT("The text of a double reads back as the double"), FCString::Atod(*AssetNumberText::Text(NextDouble)), NextDouble);

	// A move of a tenth of a millimeter: three decimals show nothing.
	TestNotEqual(TEXT("Two vectors that differ by 0.0001"), AssetNumberText::Text(FVector(100.0, 0.0, 0.0)), AssetNumberText::Text(FVector(100.0001, 0.0, 0.0)));
	TestTrue(TEXT("Only the number that needs it is written longer"), AssetNumberText::Text(FVector(100.0001, 2.0, 0.0)).Contains(TEXT("Y=2.000")));
	TestTrue(TEXT("And it is written in full"), AssetNumberText::Text(FVector(100.0001, 2.0, 0.0)).Contains(TEXT("X=100.0001")));

	TestNotEqual(TEXT("Two rotators"), AssetNumberText::Text(FRotator(0.0, 90.0, 0.0)), AssetNumberText::Text(FRotator(0.0, 90.0000001, 0.0)));
	TestNotEqual(TEXT("Two single precision vectors"), AssetNumberText::Text(FVector3f(1.0f, 0.0f, 0.0f)), AssetNumberText::Text(FVector3f(NextFloat, 0.0f, 0.0f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetNumberText_ShowsASmallChangeInTheDiff, "AssetSerializationInspector.Diff.AssetPackageDiff.ShowsASmallChangeOfAFloat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetNumberText_ShowsASmallChangeInTheDiff::RunTest(const FString& Parameters)
{
	using namespace NumberTextTestUtils;

	const float NextFloat = (1.0f + FLT_EPSILON);
	FValuePackage Old(TEXT("FloatProperty"), BytesOf(1.0f));
	FValuePackage New(TEXT("FloatProperty"), BytesOf(NextFloat));
	const FAssetPackageDiffResult Diff = AssetPackageDiff::Compare(Old.Document, New.Document, &Old.Traces, &New.Traces);

	if (const FAssetPackageDiffEntry* Label = FindLabel(Diff))
	{
		TestEqual(TEXT("The property is modified"), Label->State, EAssetPackageDiffState::Modified);
		TestNotEqual(TEXT("And the two sides do not read the same"), Label->OldValue, Label->NewValue);
	}
	else
	{
		AddError(TEXT("The property is not in the diff"));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
