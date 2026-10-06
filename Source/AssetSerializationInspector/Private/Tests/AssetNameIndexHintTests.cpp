// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Diff/AssetPackageDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetSerializedPropertyTag.h"
#include "Trace/AssetSerializationTrace.h"

namespace NameIndexHintTestUtils
{
	/** A synthetic package with one export that stores one Name property, "Label", with the given names in its name map. */
	struct FNamePropertyPackage
	{
		FAssetPackageDocument Document;
		FAssetPackageTraceCollection Traces;

		/** @param Names The name map after the name of the export. @param NameIndex The place in the whole name map that the property points at. */
		FNamePropertyPackage(const TArray<FString>& Names, const int32 NameIndex)
		{
			FAssetPackageNameEntry HolderEntry;
			HolderEntry.Name = TEXT("Holder");

			FAssetPackageExportEntry Export;
			Export.Index = 0;
			Export.ObjectName.NameIndex = Document.NameMap.Add(HolderEntry);
			Export.SerialOffset = 0;

			for (const FString& Name : Names)
			{
				FAssetPackageNameEntry Entry;
				Entry.Name = Name;
				Document.NameMap.Add(Entry);
			}

			for (const int32 Word : { NameIndex, 0 })
			{
				const int32 Start = Document.FileData.Num();
				Document.FileData.AddUninitialized(sizeof(int32));
				FMemory::Memcpy(Document.FileData.GetData() + Start, &Word, sizeof(int32));
			}

			Export.SerialSize = Document.FileData.Num();
			Document.ExportMap.Add(Export);
			Document.bHasDecodedExportMap = true;

			FAssetSerializedPropertyType Type;
			Type.Name = TEXT("NameProperty");

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
} // namespace NameIndexHintTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetNameIndexHint_ExplainsANameThatMovedInTheNameMap, "AssetSerializationInspector.Diff.AssetPackageDiff.ExplainsANameThatMovedInTheNameMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetNameIndexHint_ExplainsANameThatMovedInTheNameMap::RunTest(const FString& Parameters)
{
	using namespace NameIndexHintTestUtils;

	// The same name, with a name added before it: it points at Name[2] instead of Name[1].
	{
		FNamePropertyPackage Old({ TEXT("Alpha") }, 1);
		FNamePropertyPackage New({ TEXT("Extra"), TEXT("Alpha") }, 2);
		const FAssetPackageDiffResult Diff = AssetPackageDiff::Compare(Old.Document, New.Document, &Old.Traces, &New.Traces);

		if (const FAssetPackageDiffEntry* Label = FindLabel(Diff))
		{
			TestEqual(TEXT("The property is still reported as modified: its bytes did change"), Label->State, EAssetPackageDiffState::Modified);
			TestEqual(TEXT("The value is the same on both sides"), Label->OldValue, Label->NewValue);
			TestEqual(TEXT("It is the name"), Label->NewValue, FString(TEXT("Alpha")));

			const FString Explanation = Label->Explanation.ToString();
			TestTrue(TEXT("The hint names the name and where it moved from and to"), Explanation.Contains(TEXT("\"Alpha\" from Name[1] to Name[2]")));
		}
		else
		{
			AddError(TEXT("The property is not in the diff"));
		}
	}

	// A different name is a change of the value, with no hint.
	{
		FNamePropertyPackage Old({ TEXT("Alpha") }, 1);
		FNamePropertyPackage New({ TEXT("Alpha"), TEXT("Beta") }, 2);
		const FAssetPackageDiffResult Diff = AssetPackageDiff::Compare(Old.Document, New.Document, &Old.Traces, &New.Traces);

		if (const FAssetPackageDiffEntry* Label = FindLabel(Diff))
		{
			TestEqual(TEXT("The old value"), Label->OldValue, FString(TEXT("Alpha")));
			TestEqual(TEXT("The new value"), Label->NewValue, FString(TEXT("Beta")));
			TestTrue(TEXT("Nothing is said about the name map"), Label->Explanation.IsEmpty());
		}
		else
		{
			AddError(TEXT("The changed property is not in the diff"));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
