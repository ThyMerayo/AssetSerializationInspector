// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "Coverage/AssetDecoderCoverage.h"
#include "Diff/AssetPackageDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Save/AssetSaveAnalyzer.h"
#include "Serialization/AssetGraphNodePins.h"
#include "Tests/AssetTestPackageNames.h"
#include "Trace/AssetPackageFieldDecoder.h"
#include "Trace/AssetSerializationTrace.h"

namespace GraphPinsTestUtils
{
	static const TCHAR* const GraphPackage = TEXT("/Game/__AssetSerializationInspectorTests/GraphPins/GraphPins");
	static const TCHAR* const GraphFolder = TEXT("/Game/__AssetSerializationInspectorTests/GraphPins");

	static TSharedPtr<FAssetPackageDocument> LoadFixture(const TCHAR* Name)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
		FText Error;
		return Plugin.IsValid() ? FAssetPackageReader::LoadFromFile(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("TestFixtures"), Name), Error) : nullptr;
	}

	/** Decodes the pins of an export: what is written after its tagged properties, the last range of its trace that no property accounts for. */
	static bool DecodePins(const FAssetPackageDocument& Document, const FAssetPackageTraceCollection& Traces, const FAssetPackageExportEntry& Export, FAssetGraphNodePins& Out)
	{
		const FAssetSerializationTrace* Trace = Traces.FindExportTrace(Export.Index);
		if (Trace == nullptr || !Trace->Root.IsValid())
		{
			return false;
		}

		const FAssetSerializationTraceNode* Native = nullptr;
		for (const TSharedPtr<FAssetSerializationTraceNode>& Node : Trace->Root->Children)
		{
			if (Node.IsValid() && Node->Kind == EAssetSerializationTraceKind::Native)
			{
				Native = Node.Get();
			}
		}

		return Native != nullptr && AssetGraphNodePins::Decode(Document, Export, Export.SerialOffset + Native->Offset, Native->Size, Out);
	}

	static const FAssetPackageExportEntry* FindExport(const FAssetPackageDocument& Document, const TCHAR* Suffix)
	{
		for (const FAssetPackageExportEntry& Export : Document.ExportMap)
		{
			if (Document.ResolveExportPath(Export.Index).EndsWith(Suffix))
			{
				return &Export;
			}
		}
		return nullptr;
	}

	static FEdGraphPinType IntType()
	{
		return FEdGraphPinType(TEXT("int"), NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
	}

	/** A graph in a package of its own, with two nodes: one with an output pin, one with an input pin that has a default value. */
	struct FTestGraph
	{
		UPackage* Package = nullptr;
		UEdGraph* Graph = nullptr;
		UEdGraphNode* Source = nullptr;
		UEdGraphNode* Target = nullptr;
		UEdGraphPin* Out = nullptr;
		UEdGraphPin* In = nullptr;

		FTestGraph()
		{
			Package = CreatePackage(*AssetTestPackages::Unique(GraphPackage));
			Graph = NewObject<UEdGraph>(Package, TEXT("PinsGraph"), RF_Public | RF_Standalone);

			Source = NewObject<UEdGraphNode>(Graph, TEXT("SourceNode"), RF_Transactional);
			Source->CreateNewGuid();
			Graph->AddNode(Source, false, false);
			Out = Source->CreatePin(EGPD_Output, IntType(), TEXT("Out"));

			Target = NewObject<UEdGraphNode>(Graph, TEXT("TargetNode"), RF_Transactional);
			Target->CreateNewGuid();
			Graph->AddNode(Target, false, false);
			In = Target->CreatePin(EGPD_Input, IntType(), TEXT("In"));
			In->DefaultValue = TEXT("1");
		}

		FString Save() const
		{
			const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			SaveArgs.bSlowTask = false;
			return UPackage::SavePackage(Package, Graph, *File, SaveArgs) ? File : FString();
		}

		/** Saves and keeps a copy of the file, which the next save would overwrite. */
		FString SaveCopy(const TCHAR* Extension) const
		{
			const FString File = Save();
			if (File.IsEmpty())
			{
				return File;
			}

			const FString Copy = FPaths::ChangeExtension(File, Extension);
			IFileManager::Get().Copy(*Copy, *File);
			return Copy;
		}
	};

	static void CollectPinEntries(const FAssetPackageDiffEntry& Entry, TArray<const FAssetPackageDiffEntry*>& Out)
	{
		if (Entry.Key.StartsWith(TEXT("Pin/")))
		{
			Out.Add(&Entry);
		}

		for (const FAssetPackageDiffEntry& Child : Entry.Children)
		{
			CollectPinEntries(Child, Out);
		}
	}

	static TArray<const FAssetPackageDiffEntry*> PinEntries(const FAssetPackageDiffResult& Diff)
	{
		TArray<const FAssetPackageDiffEntry*> Result;
		for (const FAssetPackageDiffEntry& Entry : Diff.Entries)
		{
			CollectPinEntries(Entry, Result);
		}
		return Result;
	}

	static const FAssetPackageDiffEntry* FindPinEntry(const TArray<const FAssetPackageDiffEntry*>& Entries, const TCHAR* PinName)
	{
		for (const FAssetPackageDiffEntry* Entry : Entries)
		{
			if (Entry->DisplayName.ToString().StartsWith(FString::Printf(TEXT("Pin %s"), PinName)))
			{
				return Entry;
			}
		}
		return nullptr;
	}
} // namespace GraphPinsTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetGraphNodePins_ReadsTheNodesOfBlueprints, "AssetSerializationInspector.Serialization.AssetGraphNodePins.ReadsTheNodesOfBlueprints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetGraphNodePins_ReadsTheNodesOfBlueprints::RunTest(const FString& Parameters)
{
	using namespace GraphPinsTestUtils;

	for (const TCHAR* Fixture : { TEXT("BP_Box1.uasset"), TEXT("BP_BOX50.uasset") })
	{
		const TSharedPtr<FAssetPackageDocument> Document = LoadFixture(Fixture);
		if (!TestTrue(FString::Printf(TEXT("%s loads"), Fixture), Document.IsValid()))
		{
			continue;
		}

		const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);

		int32 Nodes = 0;
		for (const FAssetPackageExportEntry& Export : Document->ExportMap)
		{
			FAssetGraphNodePins Data;
			if (!DecodePins(*Document, *Traces, Export, Data))
			{
				continue;
			}

			++Nodes;
			TestTrue(FString::Printf(TEXT("%s: %s is read to its last byte (%s)"), Fixture, *Document->ResolveExportPath(Export.Index), *Data.Error), Data.bComplete);
			AddInfo(FString::Printf(TEXT("%s %s: %s"), Fixture, *Document->ResolveExportPath(Export.Index), *Data.Summarize()));
		}

		TestTrue(FString::Printf(TEXT("%s has graph nodes to read"), Fixture), Nodes > 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetGraphNodePins_ExplainsAConnectionAndADefaultValue, "AssetSerializationInspector.Serialization.AssetGraphNodePins.ExplainsAConnectionAndADefaultValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetGraphNodePins_ExplainsAConnectionAndADefaultValue::RunTest(const FString& Parameters)
{
	using namespace GraphPinsTestUtils;

	IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(GraphFolder), false, true);

	FTestGraph Test;
	const FString Unconnected = Test.SaveCopy(TEXT("before.uasset"));

	Test.Out->MakeLinkTo(Test.In);
	Test.In->DefaultValue = TEXT("5");
	const FString Connected = Test.Save();

	FText Error;
	const TSharedPtr<FAssetPackageDocument> OldDocument = FAssetPackageReader::LoadFromFile(Unconnected, Error);
	const TSharedPtr<FAssetPackageDocument> NewDocument = FAssetPackageReader::LoadFromFile(Connected, Error);
	if (!TestTrue(TEXT("Both versions are written and load"), !Unconnected.IsEmpty() && !Connected.IsEmpty() && OldDocument.IsValid() && NewDocument.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FAssetPackageTraceCollection> OldTraces = FAssetPackageFieldDecoder::Decode(*OldDocument);
	const TSharedPtr<FAssetPackageTraceCollection> NewTraces = FAssetPackageFieldDecoder::Decode(*NewDocument);

	// What is read of the target node is what the editor holds: the same pins, with the same ids, directions and values.
	const FAssetPackageExportEntry* TargetExport = FindExport(*NewDocument, TEXT("TargetNode"));
	FAssetGraphNodePins Target;
	if (TestNotNull(TEXT("The target node is exported"), TargetExport) && TestTrue(TEXT("Its pins are read"), DecodePins(*NewDocument, *NewTraces, *TargetExport, Target)))
	{
		TestTrue(*FString::Printf(TEXT("To the last byte (%s)"), *Target.Error), Target.bComplete);

		if (TestEqual(TEXT("It has its one pin"), Target.Pins.Num(), 1))
		{
			const FAssetGraphPin& Pin = Target.Pins[0];
			TestEqual(TEXT("The id is the editor's"), Pin.PinId, Test.In->PinId);
			TestEqual(TEXT("The name is the editor's"), Pin.Name, Test.In->PinName.ToString());
			TestFalse(TEXT("It is an input"), Pin.bOutput);
			TestEqual(TEXT("The type is int"), Pin.Type, FString(TEXT("int")));
			TestEqual(TEXT("The default value is the editor's"), Pin.DefaultValue, Test.In->DefaultValue);
			TestEqual(TEXT("It has one link"), Pin.LinkedTo.Num(), 1);
			if (!Pin.LinkedTo.IsEmpty())
			{
				TestEqual(TEXT("To the output of the source node"), Pin.LinkedTo[0].Pin, Test.Out->PinId);
			}
		}
	}

	// The diff names the pins that changed, with the link and the new default value.
	const FAssetPackageDiffResult Diff = AssetPackageDiff::Compare(*OldDocument, *NewDocument, OldTraces.Get(), NewTraces.Get());
	const TArray<const FAssetPackageDiffEntry*> Pins = PinEntries(Diff);
	TestEqual(TEXT("Two pins changed"), Pins.Num(), 2);

	if (const FAssetPackageDiffEntry* In = FindPinEntry(Pins, TEXT("In")))
	{
		TestEqual(TEXT("The input pin was modified"), In->State, EAssetPackageDiffState::Modified);
		TestTrue(TEXT("Its default value and links changed"), In->DisplayName.ToString().Contains(TEXT("default value")) && In->DisplayName.ToString().Contains(TEXT("links")));
		TestTrue(TEXT("The old value has no link"), !In->OldValue.Contains(TEXT("linked to")));
		TestTrue(TEXT("The new value names the pin it is linked to"), In->NewValue.Contains(TEXT("linked to SourceNode.Out")));
		TestTrue(TEXT("And the new default"), In->NewValue.Contains(TEXT("default \"5\"")));
	}
	else
	{
		AddError(TEXT("The input pin is not in the diff"));
	}

	if (const FAssetPackageDiffEntry* Out = FindPinEntry(Pins, TEXT("Out")))
	{
		TestTrue(TEXT("The output pin's links changed"), Out->DisplayName.ToString().Contains(TEXT("links")));
		TestTrue(TEXT("It names the pin it is linked to"), Out->NewValue.Contains(TEXT("linked to TargetNode.In")));
	}
	else
	{
		AddError(TEXT("The output pin is not in the diff"));
	}

	// The save analysis counts the pins as understood.
	const FAssetSaveAnalysis Analysis = FAssetSaveAnalyzer::Analyze(Diff, *OldDocument, *NewDocument);
	int32 Listed = 0;
	for (const FAssetSaveExplanationEntry& Entry : Analysis.SemanticChanges)
	{
		for (const FAssetSaveExplanationEntry& Part : Entry.Children)
		{
			Listed += Part.Key.StartsWith(TEXT("Pin/")) ? 1 : 0;
		}
	}
	TestEqual(TEXT("The analysis lists both pins"), Listed, 2);

	// A pin that is added, and one that goes away.
	Test.Source->CreatePin(EGPD_Input, IntType(), TEXT("Extra"));
	const FString WithExtra = Test.Save();
	const TSharedPtr<FAssetPackageDocument> ExtraDocument = FAssetPackageReader::LoadFromFile(WithExtra, Error);
	if (TestTrue(TEXT("The third version loads"), ExtraDocument.IsValid()))
	{
		const TSharedPtr<FAssetPackageTraceCollection> ExtraTraces = FAssetPackageFieldDecoder::Decode(*ExtraDocument);

		const FAssetPackageDiffResult AddedDiff = AssetPackageDiff::Compare(*NewDocument, *ExtraDocument, NewTraces.Get(), ExtraTraces.Get());
		const TArray<const FAssetPackageDiffEntry*> Added = PinEntries(AddedDiff);
		if (const FAssetPackageDiffEntry* Entry = FindPinEntry(Added, TEXT("Extra")))
		{
			TestEqual(TEXT("The new pin was added"), Entry->State, EAssetPackageDiffState::Added);
		}
		else
		{
			AddError(TEXT("The added pin is not in the diff"));
		}

		const FAssetPackageDiffResult RemovedDiff = AssetPackageDiff::Compare(*ExtraDocument, *NewDocument, ExtraTraces.Get(), NewTraces.Get());
		const TArray<const FAssetPackageDiffEntry*> Removed = PinEntries(RemovedDiff);
		if (const FAssetPackageDiffEntry* Entry = FindPinEntry(Removed, TEXT("Extra")))
		{
			TestEqual(TEXT("The pin that went away was removed"), Entry->State, EAssetPackageDiffState::Removed);
		}
		else
		{
			AddError(TEXT("The removed pin is not in the diff"));
		}
	}

	// A node added to the graph renumbers the references of the others (a link stores the number of the node it goes to): the pins did
	// not change, so none is reported.
	UEdGraphNode* Newcomer = NewObject<UEdGraphNode>(Test.Graph, TEXT("AaaNode"), RF_Transactional);
	Newcomer->CreateNewGuid();
	Test.Graph->AddNode(Newcomer, false, false);
	const FString WithNewcomer = Test.Save();
	const TSharedPtr<FAssetPackageDocument> NewcomerDocument = FAssetPackageReader::LoadFromFile(WithNewcomer, Error);
	if (TestTrue(TEXT("The fourth version loads"), NewcomerDocument.IsValid() && ExtraDocument.IsValid()))
	{
		const TSharedPtr<FAssetPackageTraceCollection> ExtraTraces = FAssetPackageFieldDecoder::Decode(*ExtraDocument);
		const TSharedPtr<FAssetPackageTraceCollection> NewcomerTraces = FAssetPackageFieldDecoder::Decode(*NewcomerDocument);
		const FAssetPackageDiffResult NewcomerDiff = AssetPackageDiff::Compare(*ExtraDocument, *NewcomerDocument, ExtraTraces.Get(), NewcomerTraces.Get());
		TestEqual(TEXT("No pin is reported when a node was added"), PinEntries(NewcomerDiff).Num(), 0);
	}

	IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(GraphFolder), false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetGraphNodePins_ReadsAFormattedTextOnAPin, "AssetSerializationInspector.Serialization.AssetGraphNodePins.ReadsAFormattedTextOnAPin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetGraphNodePins_ReadsAFormattedTextOnAPin::RunTest(const FString& Parameters)
{
	using namespace GraphPinsTestUtils;

	IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(GraphFolder), false, true);

	// A text with arguments is a "formatted" history, which a pin's default text can hold (the format of a Print String, for one).
	FTestGraph Test;
	Test.In->DefaultTextValue = FText::FormatNamed(INVTEXT("Hello {Name}"), TEXT("Name"), FText::FromString(TEXT("World")));
	Test.In->PinFriendlyName = FText::FormatOrdered(INVTEXT("Value {0}"), FText::AsNumber(7));
	const FString File = Test.Save();

	FText Error;
	const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(File, Error);
	if (!TestTrue(TEXT("The graph is written and loads"), !File.IsEmpty() && Document.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);
	const FAssetPackageExportEntry* TargetExport = FindExport(*Document, TEXT("TargetNode"));
	FAssetGraphNodePins Target;
	if (TestNotNull(TEXT("The target node is exported"), TargetExport) && TestTrue(TEXT("Its pins are read"), DecodePins(*Document, *Traces, *TargetExport, Target)))
	{
		TestTrue(*FString::Printf(TEXT("To the last byte, with both texts read (%s)"), *Target.Error), Target.bComplete);
		if (TestEqual(TEXT("It has its pin"), Target.Pins.Num(), 1))
		{
			TestTrue(TEXT("The default text keeps its format"), Target.Pins[0].DefaultText.Contains(TEXT("Hello")));
			TestTrue(TEXT("So does the display name"), Target.Pins[0].FriendlyName.Contains(TEXT("Value")));
		}
	}

	IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(GraphFolder), false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetGraphNodePins_ReadsTheNodesOfTheEngineContent, "AssetSerializationInspector.Serialization.AssetGraphNodePins.ReadsTheNodesOfTheEngineContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetGraphNodePins_ReadsTheNodesOfTheEngineContent::RunTest(const FString& Parameters)
{
	using namespace GraphPinsTestUtils;

	// The sequencer's burn-in Blueprint has cast nodes, which write whether they are pure after their pins.
	const FString File = FPaths::Combine(FPaths::EngineContentDir(), TEXT("Sequencer"), TEXT("DefaultBurnIn.uasset"));
	if (!IFileManager::Get().FileExists(*File))
	{
		AddInfo(TEXT("The engine's burn-in Blueprint is not in this engine, so there is nothing to read."));
		return true;
	}

	FText Error;
	const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(File, Error);
	if (!TestTrue(TEXT("The Blueprint loads"), Document.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);

	int32 Nodes = 0;
	int32 Casts = 0;
	for (const FAssetPackageExportEntry& Export : Document->ExportMap)
	{
		FAssetGraphNodePins Data;
		if (!DecodePins(*Document, *Traces, Export, Data))
		{
			continue;
		}

		++Nodes;
		TestTrue(FString::Printf(TEXT("%s is read to its last byte (%s)"), *Document->ResolveExportPath(Export.Index), *Data.Error), Data.bComplete);

		for (const TPair<FString, FString>& Extra : Data.Extras)
		{
			Casts += Extra.Key == TEXT("Purity") ? 1 : 0;
		}
	}

	TestTrue(TEXT("There are graph nodes to read"), Nodes > 0);
	TestTrue(TEXT("And cast nodes, with the purity they write"), Casts > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetGraphNodePins_IsCountedByTheCoverageScan, "AssetSerializationInspector.Coverage.AssetDecoderCoverage.CountsTheGraphNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetGraphNodePins_IsCountedByTheCoverageScan::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
	if (!TestTrue(TEXT("The plugin is found"), Plugin.IsValid()))
	{
		return false;
	}

	const FString Folder = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("TestFixtures"));
	const FAssetDecoderCoverageResult Result = AssetDecoderCoverage::Run(Folder, [](int32, int32, const FString&) { return true; });

	TestTrue(TEXT("The fixture Blueprints have graph nodes"), Result.GraphNodesScanned > 0);
	TestEqual(TEXT("Every one is read to its last byte"), Result.GraphNodesRead, Result.GraphNodesScanned);
	TestTrue(TEXT("So there is nothing to report"), Result.GraphNodeIssues.IsEmpty());
	TestTrue(TEXT("Their pins are counted"), Result.GraphPinsRead > 0);
	TestTrue(TEXT("The text report has the section"), AssetDecoderCoverage::ToText(Result).Contains(TEXT("Graph nodes:")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
