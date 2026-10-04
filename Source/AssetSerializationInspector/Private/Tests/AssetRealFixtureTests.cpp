// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

#include "Compare/AssetFolderComparison.h"
#include "Coverage/AssetDecoderCoverage.h"
#include "Diff/AssetPackageDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Report/AssetFolderComparisonReportWriter.h"
#include "Save/AssetSaveAnalyzer.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetRealFixture_TracesAreConsistent, "AssetSerializationInspector.RealAssets.TracesAreConsistent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

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

		TestTrue(
			FString::Printf(TEXT("%s: the tables decode"), Name), Fixture.Document->NameMapError.IsEmpty() && Fixture.Document->ImportMapError.IsEmpty() && Fixture.Document->ExportMapError.IsEmpty());

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetRealFixture_BlueprintVariablesDecode, "AssetSerializationInspector.RealAssets.BlueprintVariablesDecode", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetRealFixture_ResolvesDefaultsOfABlueprint, "AssetSerializationInspector.RealAssets.ResolvesDefaultsOfABlueprint", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

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
		TestTrue(TEXT("A component's native property is found by the chain or by live reflection"),
			Native.Status == EAssetArchetypeValueStatus::Found || Native.Status == EAssetArchetypeValueStatus::NativeDefaultFromLiveReflection);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetRealFixture_SaveAnalysisDescribesHeaderChanges, "AssetSerializationInspector.RealAssets.SaveAnalysisDescribesHeaderChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetRealFixture_SaveAnalysisDescribesHeaderChanges::RunTest(const FString& Parameters)
{
	using namespace AssetRealFixtureTestUtils;

	FFixture Older;
	FFixture Newer;
	if (!TestTrue(TEXT("BP_BOX50 loads"), LoadFixture(TEXT("BP_BOX50.uasset"), Older)) || !TestTrue(TEXT("BP_Box1 loads"), LoadFixture(TEXT("BP_Box1.uasset"), Newer)))
	{
		return false;
	}

	// A package against itself changes nothing, header included.
	const FAssetPackageDiffResult Same = AssetPackageDiff::Compare(*Newer.Document, *Newer.Document, Newer.Traces.Get(), Newer.Traces.Get());
	const FAssetSaveAnalysis SameAnalysis = FAssetSaveAnalyzer::Analyze(Same, *Newer.Document, *Newer.Document);
	TestTrue(TEXT("An unchanged package has no header changes"), SameAnalysis.HeaderChanges.IsEmpty());

	// Two packages saved by different engines differ in the header: versions, and the size of several tables.
	const FAssetPackageDiffResult Diff = AssetPackageDiff::Compare(*Older.Document, *Newer.Document, Older.Traces.Get(), Newer.Traces.Get());
	const FAssetSaveAnalysis Analysis = FAssetSaveAnalyzer::Analyze(Diff, *Older.Document, *Newer.Document);

	if (!TestEqual(TEXT("The header is described as one entry"), Analysis.HeaderChanges.Num(), 1))
	{
		return false;
	}

	const FAssetSaveExplanationEntry& Header = Analysis.HeaderChanges[0];
	TestFalse(TEXT("It says what happened to the header's size"), Header.Description.IsEmpty());
	TestTrue(TEXT("Its tables are listed as children"), !Header.Children.IsEmpty());
	TestEqual(TEXT("The change count is the number of tables listed"), Analysis.HeaderChangeCount, Header.Children.Num());

	bool bSummaryWithFields = false;
	for (const FAssetSaveExplanationEntry& Table : Header.Children)
	{
		TestFalse(FString::Printf(TEXT("%s says how it changed"), *Table.Title.ToString()), Table.Description.IsEmpty());
		if (Table.Classification == EAssetSaveChangeClassification::PackageMetadataChanged)
		{
			bSummaryWithFields |= !Table.Children.IsEmpty();
		}
	}

	TestTrue(TEXT("The summary lists the fields that changed"), bSummaryWithFields);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetRealFixture_ComparesFoldersAcrossEngineVersions, "AssetSerializationInspector.RealAssets.ComparesFoldersAcrossEngineVersions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetRealFixture_ComparesFoldersAcrossEngineVersions::RunTest(const FString& Parameters)
{
	using namespace AssetRealFixtureTestUtils;

	// Two folders as they would be around an engine upgrade: a Blueprint saved by UE 5.0 that the new folder holds saved by this
	// engine, one that did not change, and one that exists on one side only.
	const FString Folder = GetFixtureFolder();
	const FString UE50 = FPaths::Combine(Folder, TEXT("BP_BOX50.uasset"));
	const FString Current = FPaths::Combine(Folder, TEXT("BP_Box1.uasset"));

	const FString Root = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("AssetSerializationInspector"), TEXT("UpgradeFolders"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	const FString OldFolder = FPaths::Combine(Root, TEXT("Old"));
	const FString NewFolder = FPaths::Combine(Root, TEXT("New"));

	IFileManager::Get().Copy(*FPaths::Combine(OldFolder, TEXT("Blueprints"), TEXT("Box.uasset")), *UE50);
	IFileManager::Get().Copy(*FPaths::Combine(NewFolder, TEXT("Blueprints"), TEXT("Box.uasset")), *Current);
	IFileManager::Get().Copy(*FPaths::Combine(OldFolder, TEXT("Same.uasset")), *Current);
	IFileManager::Get().Copy(*FPaths::Combine(NewFolder, TEXT("Same.uasset")), *Current);
	IFileManager::Get().Copy(*FPaths::Combine(OldFolder, TEXT("Removed.uasset")), *UE50);
	IFileManager::Get().Copy(*FPaths::Combine(NewFolder, TEXT("Added.uasset")), *Current);

	const FAssetFolderComparisonResult Result = AssetFolderComparison::Run(OldFolder, NewFolder, [](int32, int32, const FString&) { return true; });

	const auto Find = [&Result](const TCHAR* Path) -> const FAssetFolderComparisonEntry* {
		return Result.Entries.FindByPredicate([Path](const FAssetFolderComparisonEntry& Entry) { return Entry.RelativePath == Path; });
	};

	const FAssetFolderComparisonEntry* Upgraded = Find(TEXT("Blueprints/Box.uasset"));
	if (TestNotNull(TEXT("The upgraded Blueprint is paired"), Upgraded))
	{
		TestEqual(TEXT("It changed"), Upgraded->Status, EAssetFolderComparisonStatus::Changed);
		TestTrue(TEXT("The two sides were saved by different versions"), Upgraded->bVersionsDiffer);
		TestEqual(TEXT("The old side is a UE 5.0 package"), Upgraded->OldFileVersion, FString(TEXT("UE4 522 / UE5 1004")));
		TestEqual(TEXT("The new side is this engine's format"), Upgraded->NewFileVersion, FString(TEXT("UE4 522 / UE5 1018")));
		TestFalse(TEXT("The old engine is named"), Upgraded->OldEngineVersion.IsEmpty());
		// A source build without a changelist saves an empty engine version, so the file version is the reliable signal.
		TestNotEqual(TEXT("They are not the same engine"), Upgraded->OldEngineVersion, Upgraded->NewEngineVersion);
		TestFalse(TEXT("What changed is listed"), Upgraded->Changes.IsEmpty());
		TestTrue(TEXT("Both sizes are known"), Upgraded->OldFileSize > 0 && Upgraded->NewFileSize > 0);
	}

	const FAssetFolderComparisonEntry* Same = Find(TEXT("Same.uasset"));
	if (TestNotNull(TEXT("The unchanged file is paired"), Same))
	{
		TestEqual(TEXT("It is identical"), Same->Status, EAssetFolderComparisonStatus::Identical);
		TestFalse(TEXT("With nothing to say about versions"), Same->bVersionsDiffer);
	}

	if (const FAssetFolderComparisonEntry* Removed = Find(TEXT("Removed.uasset")))
	{
		TestEqual(TEXT("A file only in the old folder"), Removed->Status, EAssetFolderComparisonStatus::OnlyInOldFolder);
		TestEqual(TEXT("Its engine is still reported"), Removed->OldFileVersion, FString(TEXT("UE4 522 / UE5 1004")));
	}
	else
	{
		AddError(TEXT("Removed.uasset is missing from the result."));
	}

	if (const FAssetFolderComparisonEntry* Added = Find(TEXT("Added.uasset")))
	{
		TestEqual(TEXT("A file only in the new folder"), Added->Status, EAssetFolderComparisonStatus::OnlyInNewFolder);
	}
	else
	{
		AddError(TEXT("Added.uasset is missing from the result."));
	}

	const FAssetFolderComparisonSummary Summary = Result.Summarize();
	TestEqual(TEXT("One changed file"), Summary.Changed, 1);
	TestEqual(TEXT("Saved by different versions"), Summary.ChangedWithDifferentVersions, 1);
	TestEqual(TEXT("One identical file"), Summary.Identical, 1);

	// Identical files count too, so the unchanged file is a pair of its own.
	const TArray<FAssetEngineVersionPair> Pairs = Result.FindEngineVersionPairs();
	TestEqual(TEXT("Two kinds of engine version change are found"), Pairs.Num(), 2);
	TestTrue(TEXT("One of them is the upgrade"), Upgraded != nullptr && Pairs.ContainsByPredicate([Upgraded](const FAssetEngineVersionPair& Pair) {
		return Pair.OldEngineVersion == Upgraded->OldEngineVersion && Pair.NewEngineVersion == Upgraded->NewEngineVersion && Pair.AssetCount == 1;
	}));

	const FString Report = AssetFolderComparisonReportWriter::ToText(Result);
	TestTrue(TEXT("The report names the upgraded file"), Report.Contains(TEXT("Blueprints/Box.uasset")));
	TestTrue(TEXT("And the engine that saved the old side"), Upgraded != nullptr && Report.Contains(Upgraded->OldEngineVersion));
	TestTrue(TEXT("And the package versions of both sides"), Report.Contains(TEXT("UE4 522 / UE5 1004")) && Report.Contains(TEXT("UE4 522 / UE5 1018")));
	TestTrue(TEXT("An engine the package does not name is called unknown"), Report.Contains(TEXT("unknown")));

	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
