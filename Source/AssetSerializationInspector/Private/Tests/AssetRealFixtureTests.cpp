// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

#include "Coverage/AssetDecoderCoverage.h"
#include "Diff/AssetPackageDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Serialization/AssetArchetypeResolver.h"
#include "Serialization/AssetPropertyValueDecoder.h"
#include "Trace/AssetPackageFieldDecoder.h"
#include "Trace/AssetSerializationTrace.h"

/*
 * Tests over the packages in Resources/TestFixtures, which were saved by real editors:
 *
 *  - BP_BOX50: a Blueprint migrated from UE 5.0 (older tag layout, no script serialization offsets, inline soft object paths).
 *  - BP_Box1: a Blueprint saved by this engine version, with NewVar_11, an array of three texts.
 */
namespace AssetRealFixtureTestUtils
{
	struct FFixture
	{
		TSharedPtr<FAssetPackageDocument> Document;
		TSharedPtr<FAssetPackageTraceCollection> Traces;
	};

	static FString GetFixtureFolder()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
		return Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("TestFixtures")) : FString();
	}

	static bool LoadFixture(const TCHAR* Name, FFixture& OutFixture)
	{
		FText Error;
		OutFixture.Document = FAssetPackageReader::LoadFromFile(FPaths::Combine(GetFixtureFolder(), Name), Error);
		if (!OutFixture.Document.IsValid())
		{
			return false;
		}

		OutFixture.Traces = FAssetPackageFieldDecoder::Decode(*OutFixture.Document);
		return true;
	}

	static FString ClassNameOf(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export)
	{
		FString Path;
		if (!Document.ResolvePackageIndexPath(Export.ClassIndex, Path))
		{
			return FString();
		}

		int32 Dot = INDEX_NONE;
		return Path.FindLastChar(TEXT('.'), Dot) ? Path.RightChop(Dot + 1) : Path;
	}

	static const FAssetSerializationTraceNode* FindProperty(const FFixture& Fixture, const FString& Name, const FAssetPackageExportEntry*& OutExport)
	{
		for (const FAssetPackageExportEntry& Export : Fixture.Document->ExportMap)
		{
			const FAssetSerializationTrace* Trace = Fixture.Traces->FindExportTrace(Export.Index);
			if (Trace == nullptr || !Trace->Root.IsValid())
			{
				continue;
			}

			for (const TSharedPtr<FAssetSerializationTraceNode>& Node : Trace->Root->Children)
			{
				if (Node.IsValid() && Node->Kind == EAssetSerializationTraceKind::Property && Node->Name == Name)
				{
					OutExport = &Export;
					return Node.Get();
				}
			}
		}

		return nullptr;
	}

	static const FAssetDecodedPropertyValue* FindChild(const FAssetDecodedPropertyValue& Value, const FString& Name)
	{
		for (const FAssetDecodedPropertyValue& Child : Value.Children)
		{
			if (Child.Name == Name)
			{
				return &Child;
			}
		}

		return nullptr;
	}
} // namespace AssetRealFixtureTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetRealFixture_TracesAreConsistent, "AssetSerializationInspector.RealAssets.TracesAreConsistent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetRealFixture_TracesAreConsistent::RunTest(const FString& Parameters)
{
	using namespace AssetRealFixtureTestUtils;

	for (const TCHAR* Name : { TEXT("BP_BOX50.uasset"), TEXT("BP_Box1.uasset") })
	{
		FFixture Fixture;
		if (!TestTrue(FString::Printf(TEXT("%s loads"), Name), LoadFixture(Name, Fixture)))
		{
			continue;
		}

		TestTrue(FString::Printf(TEXT("%s: the tables decode"), Name), Fixture.Document->NameMapError.IsEmpty() && Fixture.Document->ImportMapError.IsEmpty() && Fixture.Document->ExportMapError.IsEmpty());

		int32 PropertyCount = 0;
		for (const FAssetPackageExportEntry& Export : Fixture.Document->ExportMap)
		{
			const FAssetSerializationTrace* Trace = Fixture.Traces->FindExportTrace(Export.Index);
			if (Trace == nullptr || !Trace->Root.IsValid())
			{
				continue;
			}

			// Whatever the trace attributes to a node must lie inside the export, and siblings must not overlap.
			int64 PreviousEnd = 0;
			for (const TSharedPtr<FAssetSerializationTraceNode>& Node : Trace->Root->Children)
			{
				if (!Node.IsValid())
				{
					continue;
				}

				if (Node->Kind == EAssetSerializationTraceKind::Property)
				{
					++PropertyCount;
				}

				const bool bInside = Node->Offset >= 0 && Node->Size >= 0 && Node->Offset + Node->Size <= Export.SerialSize;
				TestTrue(FString::Printf(TEXT("%s: '%s' of export %d lies inside it"), Name, *Node->Name, Export.Index), bInside);
				TestTrue(FString::Printf(TEXT("%s: '%s' of export %d does not overlap the previous node"), Name, *Node->Name, Export.Index), Node->Offset >= PreviousEnd - 1);
				PreviousEnd = FMath::Max(PreviousEnd, Node->Offset + Node->Size);
			}
		}

		TestTrue(FString::Printf(TEXT("%s: properties were found"), Name), PropertyCount > 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetRealFixture_BlueprintVariablesDecode, "AssetSerializationInspector.RealAssets.BlueprintVariablesDecode", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetRealFixture_BlueprintVariablesDecode::RunTest(const FString& Parameters)
{
	using namespace AssetRealFixtureTestUtils;

	FFixture Fixture;
	if (!TestTrue(TEXT("BP_Box1 loads"), LoadFixture(TEXT("BP_Box1.uasset"), Fixture)))
	{
		return false;
	}

	const FAssetPackageExportEntry* BlueprintExport = nullptr;
	const FAssetSerializationTraceNode* Node = FindProperty(Fixture, TEXT("NewVariables"), BlueprintExport);
	if (!TestNotNull(TEXT("The Blueprint stores its variable list"), Node))
	{
		return false;
	}

	TestEqual(TEXT("The variable list is on the Blueprint"), ClassNameOf(*Fixture.Document, *BlueprintExport), FString(TEXT("Blueprint")));

	const FAssetDecodedPropertyValue Variables = FAssetPropertyValueDecoder::Decode(*Fixture.Document, *Node, BlueprintExport->SerialOffset);
	if (!TestTrue(TEXT("The variable list decodes"), Variables.IsSuccess()))
	{
		return false;
	}

	bool bFoundTexts = false;
	for (const FAssetDecodedPropertyValue& Variable : Variables.Children)
	{
		const FAssetDecodedPropertyValue* Name = FindChild(Variable, TEXT("VarName"));
		TestNotNull(TEXT("Each variable has a name"), Name);
		TestNotNull(TEXT("Each variable has a type"), FindChild(Variable, TEXT("VarType")));
		TestNotNull(TEXT("Each variable has a default value"), FindChild(Variable, TEXT("DefaultValue")));
		bFoundTexts |= Name != nullptr && Name->Value == TEXT("NewVar_11");
	}

	TestTrue(TEXT("The array of texts is among the variables"), bFoundTexts);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetRealFixture_ResolvesDefaultsOfABlueprint, "AssetSerializationInspector.RealAssets.ResolvesDefaultsOfABlueprint", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetRealFixture_ResolvesDefaultsOfABlueprint::RunTest(const FString& Parameters)
{
	using namespace AssetRealFixtureTestUtils;

	FFixture Fixture;
	if (!TestTrue(TEXT("BP_Box1 loads"), LoadFixture(TEXT("BP_Box1.uasset"), Fixture)))
	{
		return false;
	}

	int32 ClassDefaultObject = INDEX_NONE;
	int32 Component = INDEX_NONE;
	for (const FAssetPackageExportEntry& Export : Fixture.Document->ExportMap)
	{
		if (Fixture.Document->IsExportClassDefaultObject(Export))
		{
			ClassDefaultObject = Export.Index;
		}

		if (ClassNameOf(*Fixture.Document, Export).EndsWith(TEXT("Component")))
		{
			Component = Export.Index;
		}
	}

	if (!TestTrue(TEXT("The class default object is found"), ClassDefaultObject != INDEX_NONE))
	{
		return false;
	}

	FAssetArchetypeResolver Resolver(*Fixture.Document, *Fixture.Traces);

	// A variable of the Blueprint has no native default: it is zero or empty unless the variable stores a default value.
	const FAssetOmittedPropertyDefault Variable = Resolver.DescribeOmittedProperty(ClassDefaultObject, TEXT("NewVar_11"), 0);
	TestEqual(TEXT("A Blueprint variable is declared by the Blueprint"), Variable.Status, EAssetArchetypeValueStatus::DeclaredByBlueprint);

	// A property no Blueprint declares and no package stores comes from native code (read live) or is unavailable; it is never
	// reported as a Blueprint variable.
	const FAssetOmittedPropertyDefault Unknown = Resolver.DescribeOmittedProperty(ClassDefaultObject, TEXT("NotAVariable"), 0);
	TestNotEqual(TEXT("An unknown property is not a Blueprint variable"), Unknown.Status, EAssetArchetypeValueStatus::DeclaredByBlueprint);

	if (Component != INDEX_NONE)
	{
		const FAssetOmittedPropertyDefault Native = Resolver.DescribeOmittedProperty(Component, TEXT("bHiddenInGame"), 0);
		TestTrue(TEXT("A component's native property is found by the chain or by live reflection"), Native.Status == EAssetArchetypeValueStatus::Found || Native.Status == EAssetArchetypeValueStatus::NativeDefaultFromLiveReflection);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetRealFixture_DiffsPackages, "AssetSerializationInspector.RealAssets.DiffsPackages", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetRealFixture_DiffsPackages::RunTest(const FString& Parameters)
{
	using namespace AssetRealFixtureTestUtils;

	FFixture Older;
	FFixture Newer;
	if (!TestTrue(TEXT("BP_BOX50 loads"), LoadFixture(TEXT("BP_BOX50.uasset"), Older)) || !TestTrue(TEXT("BP_Box1 loads"), LoadFixture(TEXT("BP_Box1.uasset"), Newer)))
	{
		return false;
	}

	// A package against itself has nothing to report, for either file layout.
	for (FFixture* Fixture : { &Older, &Newer })
	{
		const FAssetPackageDiffResult Same = AssetPackageDiff::Compare(*Fixture->Document, *Fixture->Document, Fixture->Traces.Get(), Fixture->Traces.Get());
		TestTrue(TEXT("A package is identical to itself"), Same.bFilesIdentical);
		TestEqual(TEXT("With nothing added"), Same.AddedCount, 0);
		TestEqual(TEXT("Nothing removed"), Same.RemovedCount, 0);
		TestEqual(TEXT("Nothing modified"), Same.ModifiedCount, 0);
	}

	// Two different Blueprints saved by different engine versions compare without trouble and report entries, including
	// the package header (versions, counts).
	const FAssetPackageDiffResult Different = AssetPackageDiff::Compare(*Older.Document, *Newer.Document, Older.Traces.Get(), Newer.Traces.Get());
	TestFalse(TEXT("Different packages are not identical"), Different.bFilesIdentical);
	TestFalse(TEXT("The entries are not empty"), Different.Entries.IsEmpty());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
