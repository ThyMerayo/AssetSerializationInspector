// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "Diff/AssetPackageDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Save/AssetSaveAnalyzer.h"
#include "Serialization/AssetPropertyValueDecoder.h"
#include "Serialization/AssetStructNativeData.h"
#include "Summary/AssetExportSummary.h"
#include "Tests/AssetTestPackageNames.h"
#include "Tests/AssetTestUtils.h"
#include "Trace/AssetPackageFieldDecoder.h"
#include "Trace/AssetSerializationTrace.h"

namespace StructNativeDataTestUtils
{
	static TSharedPtr<FAssetPackageDocument> LoadFixture(const TCHAR* Name)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
		FText Error;
		return Plugin.IsValid() ? FAssetPackageReader::LoadFromFile(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("TestFixtures"), Name), Error) : nullptr;
	}

	/** Decodes the native data after the tagged properties of an export: the last range of its trace that no property accounts for. */
	static bool DecodeExport(const FAssetPackageDocument& Document, const FAssetPackageTraceCollection& Traces, const FAssetPackageExportEntry& Export, FAssetStructNativeData& Out)
	{
		return AssetTestUtils::DecodeLastNative(
			Traces, Export, [&](const int64 Offset, const int64 Size, const FAssetSerializationTrace*) { return AssetStructNativeData::Decode(Document, Export, Offset, Size, Out); });
	}
} // namespace StructNativeDataTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetStructNativeData_ReadsTheClassesAndFunctionsOfBlueprints,
	"AssetSerializationInspector.Serialization.AssetStructNativeData.ReadsTheClassesAndFunctionsOfBlueprints", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetStructNativeData_ReadsTheClassesAndFunctionsOfBlueprints::RunTest(const FString& Parameters)
{
	using namespace StructNativeDataTestUtils;

	for (const TCHAR* Fixture : { TEXT("BP_Box1.uasset"), TEXT("BP_BOX50.uasset") })
	{
		const TSharedPtr<FAssetPackageDocument> Document = LoadFixture(Fixture);
		if (!TestTrue(FString::Printf(TEXT("%s loads"), Fixture), Document.IsValid()))
		{
			continue;
		}

		const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);

		int32 Decoded = 0;
		for (const FAssetPackageExportEntry& Export : Document->ExportMap)
		{
			FAssetStructNativeData Data;
			if (!DecodeExport(*Document, *Traces, Export, Data))
			{
				continue;
			}

			++Decoded;
			TestTrue(FString::Printf(TEXT("%s: %s is read to its last byte (%s)"), Fixture, *Document->ResolveExportPath(Export.Index), *Data.Error), Data.bComplete);
			AddInfo(FString::Printf(TEXT("%s %s: %s"), Fixture, *Document->ResolveExportPath(Export.Index), *Data.Summarize()));
		}

		TestTrue(FString::Printf(TEXT("%s has classes or functions to read"), Fixture), Decoded > 0);
	}

	return true;
}

namespace StructNativeDataTestUtils
{
	static const TCHAR* const BlueprintPackage = TEXT("/Game/__AssetSerializationInspectorTests/ClassData/BP_ClassData");
	static const TCHAR* const BlueprintFolder = TEXT("/Game/__AssetSerializationInspectorTests/ClassData");

	static FString SaveBlueprint(UBlueprint* Blueprint)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		const FString File = FPackageName::LongPackageNameToFilename(Blueprint->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		SaveArgs.bSlowTask = false;
		return UPackage::SavePackage(Blueprint->GetPackage(), Blueprint, *File, SaveArgs) ? File : FString();
	}

	static void AddIntVariable(UBlueprint* Blueprint, const TCHAR* Name)
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = TEXT("int");
		FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, PinType);
	}

	/** The export of the generated class of the test Blueprint. */
	static const FAssetPackageExportEntry* FindClassExport(const FAssetPackageDocument& Document)
	{
		for (const FAssetPackageExportEntry& Export : Document.ExportMap)
		{
			if (Document.ResolveExportPath(Export.Index).EndsWith(TEXT("BP_ClassData_C")))
			{
				return &Export;
			}
		}
		return nullptr;
	}

	static const FAssetPackageDiffEntry* FindEntry(const FAssetPackageDiffEntry& Entry, const FString& Key)
	{
		if (Entry.Key == Key)
		{
			return &Entry;
		}

		for (const FAssetPackageDiffEntry& Child : Entry.Children)
		{
			if (const FAssetPackageDiffEntry* Found = FindEntry(Child, Key))
			{
				return Found;
			}
		}
		return nullptr;
	}

	static const FAssetPackageDiffEntry* FindEntry(const FAssetPackageDiffResult& Diff, const FString& Key)
	{
		for (const FAssetPackageDiffEntry& Entry : Diff.Entries)
		{
			if (const FAssetPackageDiffEntry* Found = FindEntry(Entry, Key))
			{
				return Found;
			}
		}
		return nullptr;
	}
} // namespace StructNativeDataTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetStructNativeData_ExplainsAnAddedAndARemovedVariable, "AssetSerializationInspector.Serialization.AssetStructNativeData.ExplainsAnAddedAndARemovedVariable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetStructNativeData_ExplainsAnAddedAndARemovedVariable::RunTest(const FString& Parameters)
{
	using namespace StructNativeDataTestUtils;

	IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(BlueprintFolder), false, true);

	UPackage* Package = CreatePackage(*AssetTestPackages::Unique(BlueprintPackage));
	UBlueprint* Blueprint =
		FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package, TEXT("BP_ClassData"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("A Blueprint is created"), Blueprint))
	{
		return false;
	}

	AddIntVariable(Blueprint, TEXT("Alpha"));
	const FString First = SaveBlueprint(Blueprint);

	const FString WithAlpha = FPaths::ChangeExtension(First, TEXT("alpha.uasset"));
	IFileManager::Get().Copy(*WithAlpha, *First);

	AddIntVariable(Blueprint, TEXT("Counter"));
	const FString Second = SaveBlueprint(Blueprint);

	FText Error;
	const TSharedPtr<FAssetPackageDocument> OldDocument = FAssetPackageReader::LoadFromFile(WithAlpha, Error);
	const TSharedPtr<FAssetPackageDocument> NewDocument = FAssetPackageReader::LoadFromFile(Second, Error);
	if (!TestTrue(TEXT("Both versions are written and load"), !First.IsEmpty() && !Second.IsEmpty() && OldDocument.IsValid() && NewDocument.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FAssetPackageTraceCollection> OldTraces = FAssetPackageFieldDecoder::Decode(*OldDocument);
	const TSharedPtr<FAssetPackageTraceCollection> NewTraces = FAssetPackageFieldDecoder::Decode(*NewDocument);

	// The decoded variables of the new class are the ones the class declares in the running editor.
	const FAssetPackageExportEntry* NewClassExport = FindClassExport(*NewDocument);
	FAssetStructNativeData NewData;
	if (TestNotNull(TEXT("The generated class is exported"), NewClassExport) && TestTrue(TEXT("Its native data is read"), DecodeExport(*NewDocument, *NewTraces, *NewClassExport, NewData)))
	{
		TestTrue(*FString::Printf(TEXT("To its last byte (%s)"), *NewData.Error), NewData.bComplete);

		TArray<FString> Names;
		for (const FAssetFieldDefinition& Field : NewData.Fields)
		{
			Names.Add(Field.Name);
		}
		TestTrue(TEXT("Both variables are declared"), Names.Contains(TEXT("Alpha")) && Names.Contains(TEXT("Counter")));

		const UClass* Live = Blueprint->GeneratedClass;
		if (TestNotNull(TEXT("The editor has the class"), Live))
		{
			int32 LiveCount = 0;
			for (TFieldIterator<FProperty> It(Live, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				++LiveCount;
				const FAssetFieldDefinition* Field = NewData.Fields.FindByPredicate([&It](const FAssetFieldDefinition& Candidate) { return Candidate.Name == It->GetName(); });
				if (TestNotNull(*FString::Printf(TEXT("%s is among the decoded properties"), *It->GetName()), Field))
				{
					TestTrue(*FString::Printf(TEXT("%s has the type the editor says"), *It->GetName()), Field->Type.StartsWith(It->GetID().ToString()));
					TestEqual(*FString::Printf(TEXT("%s has the flags the editor says"), *It->GetName()), Field->PropertyFlags, static_cast<uint64>(It->PropertyFlags & ~CPF_ComputedFlags));
					TestEqual(*FString::Printf(TEXT("%s has its size"), *It->GetName()), Field->ElementSize, It->GetElementSize());
				}
			}
			TestEqual(TEXT("Every property of the class was decoded"), NewData.Fields.Num(), LiveCount);
		}
	}

	// The diff says which variable was added, and not that the unchanged one changed.
	const FAssetPackageDiffResult Diff = AssetPackageDiff::Compare(*OldDocument, *NewDocument, OldTraces.Get(), NewTraces.Get());
	if (const FAssetPackageDiffEntry* Added = FindEntry(Diff, TEXT("Property/Counter")))
	{
		TestEqual(TEXT("The new variable was added"), Added->State, EAssetPackageDiffState::Added);
		TestTrue(TEXT("With its type"), Added->NewValue.StartsWith(TEXT("IntProperty")));
		TestEqual(TEXT("And no old value"), Added->OldValue, FString());
	}
	else
	{
		AddError(TEXT("The new variable is not in the diff"));
	}

	TestNull(TEXT("Alpha, which did not change, is not in the diff"), FindEntry(Diff, TEXT("Property/Alpha")));

	// The save analysis counts the class data as understood, with the change listed.
	const FAssetSaveAnalysis Analysis = FAssetSaveAnalyzer::Analyze(Diff, *OldDocument, *NewDocument);
	bool bListed = false;
	for (const FAssetSaveExplanationEntry& Entry : Analysis.SemanticChanges)
	{
		for (const FAssetSaveExplanationEntry& Part : Entry.Children)
		{
			bListed |= Part.Key == TEXT("Property/Counter");
		}
	}
	TestTrue(TEXT("The analysis lists the added variable"), bListed);

	// And the other way round: the variable that went away.
	const FAssetPackageDiffResult Reverse = AssetPackageDiff::Compare(*NewDocument, *OldDocument, NewTraces.Get(), OldTraces.Get());
	if (const FAssetPackageDiffEntry* Removed = FindEntry(Reverse, TEXT("Property/Counter")))
	{
		TestEqual(TEXT("It was removed"), Removed->State, EAssetPackageDiffState::Removed);
	}
	else
	{
		AddError(TEXT("The removed variable is not in the diff"));
	}

	IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(BlueprintFolder), false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
