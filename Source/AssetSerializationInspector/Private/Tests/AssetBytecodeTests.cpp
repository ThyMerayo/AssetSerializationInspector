// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Serialization/AssetBytecode.h"
#include "Serialization/AssetStructNativeData.h"
#include "Trace/AssetPackageFieldDecoder.h"
#include "Trace/AssetSerializationTrace.h"

namespace BytecodeTestUtils
{
	/** The data a function or class writes after its tagged properties: the last range of its trace that no property accounts for. */
	static bool DecodeData(const FAssetPackageDocument& Document, const FAssetPackageTraceCollection& Traces, const FAssetPackageExportEntry& Export, FAssetStructNativeData& Out)
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

		return Native != nullptr && AssetStructNativeData::Decode(Document, Export, Export.SerialOffset + Native->Offset, Native->Size, Out);
	}

	/** A bytecode of the given statements, as the texts a function would have (a jump is written as the statement it goes to). */
	static FAssetBytecode MakeBytecode(const TArray<FString>& Texts)
	{
		FAssetBytecode Bytecode;
		Bytecode.bComplete = true;
		for (const FString& Text : Texts)
		{
			FAssetBytecodeStatement& Statement = Bytecode.Statements.AddDefaulted_GetRef();
			Statement.Text = Text;
			Statement.Key = Text;
		}
		return Bytecode;
	}
} // namespace BytecodeTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBytecode_ReadsTheFunctionsOfBlueprints, "AssetSerializationInspector.Serialization.AssetBytecode.ReadsTheFunctionsOfBlueprints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBytecode_ReadsTheFunctionsOfBlueprints::RunTest(const FString& Parameters)
{
	using namespace BytecodeTestUtils;

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
	if (!TestTrue(TEXT("The plugin is found"), Plugin.IsValid()))
	{
		return false;
	}

	int32 Functions = 0;
	for (const TCHAR* Fixture : { TEXT("BP_Box1.uasset"), TEXT("BP_BOX50.uasset") })
	{
		FText Error;
		const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("TestFixtures"), Fixture), Error);
		if (!TestTrue(FString::Printf(TEXT("%s loads"), Fixture), Document.IsValid()))
		{
			continue;
		}

		const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);
		for (const FAssetPackageExportEntry& Export : Document->ExportMap)
		{
			FAssetStructNativeData Data;
			if (!DecodeData(*Document, *Traces, Export, Data) || Data.BytecodeStorageSize <= 0)
			{
				continue;
			}

			++Functions;
			TestTrue(FString::Printf(TEXT("%s: %s has its bytecode read, and its size in memory adds up (%s)"), Fixture, *Document->ResolveExportPath(Export.Index), *Data.Bytecode.Error),
				Data.Bytecode.bComplete);

			// A function ends with the end of its script.
			if (Data.Bytecode.bComplete && TestTrue(TEXT("It has statements"), !Data.Bytecode.Statements.IsEmpty()))
			{
				TestTrue(TEXT("The last one is the end of the script"), Data.Bytecode.Statements.Last().Text == TEXT("EndOfScript"));
				AddInfo(FString::Printf(
					TEXT("%s: %d statements, the first is %s"), *Document->ResolveExportPath(Export.Index), Data.Bytecode.Statements.Num(), *Data.Bytecode.Statements[0].Text.Left(120)));
			}
		}
	}

	TestTrue(TEXT("The fixtures have functions with bytecode"), Functions > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBytecode_ReadsTheFunctionsOfTheEngineContent, "AssetSerializationInspector.Serialization.AssetBytecode.ReadsTheFunctionsOfTheEngineContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBytecode_ReadsTheFunctionsOfTheEngineContent::RunTest(const FString& Parameters)
{
	using namespace BytecodeTestUtils;

	// A Blueprint of the engine with logic in it: calls, branches, loops, casts, local variables.
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

	int32 Functions = 0;
	int32 Statements = 0;
	bool bCall = false;
	bool bJump = false;
	for (const FAssetPackageExportEntry& Export : Document->ExportMap)
	{
		FAssetStructNativeData Data;
		if (!DecodeData(*Document, *Traces, Export, Data) || Data.BytecodeStorageSize <= 0)
		{
			continue;
		}

		++Functions;
		TestTrue(FString::Printf(TEXT("%s: the bytecode is read (%s)"), *Document->ResolveExportPath(Export.Index), *Data.Bytecode.Error), Data.Bytecode.bComplete);
		Statements += Data.Bytecode.Statements.Num();

		for (const FAssetBytecodeStatement& Statement : Data.Bytecode.Statements)
		{
			bCall |= Statement.Text.Contains(TEXT("Function(")) || Statement.Text.Contains(TEXT("CallMath("));
			bJump |= Statement.Text.Contains(TEXT("JumpIfNot(#")) || Statement.Text.StartsWith(TEXT("Jump(#"));
		}
	}

	TestTrue(TEXT("There are functions"), Functions > 0);
	TestTrue(TEXT("With statements"), Statements > 0);
	TestTrue(TEXT("Calls name the function they call"), bCall);
	TestTrue(TEXT("Jumps name the statement they go to"), bJump);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetBytecode_ComparesStatements, "AssetSerializationInspector.Serialization.AssetBytecode.ComparesStatements", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBytecode_ComparesStatements::RunTest(const FString& Parameters)
{
	using namespace BytecodeTestUtils;

	const FAssetBytecode Base = MakeBytecode({ TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D") });

	TestTrue(TEXT("The same statements are no change"), AssetBytecode::Compare(Base, Base).IsEmpty());

	// A statement inserted in the middle.
	{
		const TArray<AssetBytecode::FStatementChange> Changes = AssetBytecode::Compare(Base, MakeBytecode({ TEXT("A"), TEXT("B"), TEXT("X"), TEXT("C"), TEXT("D") }));
		if (TestEqual(TEXT("An insertion is one change"), Changes.Num(), 1))
		{
			TestTrue(TEXT("Nothing was removed"), Changes[0].Removed.IsEmpty());
			TestEqual(TEXT("The new statement was added"), Changes[0].Added, TArray<FString>({ TEXT("X") }));
			TestEqual(TEXT("After the second statement"), Changes[0].NewStart, 2);
		}
	}

	// A statement removed.
	{
		const TArray<AssetBytecode::FStatementChange> Changes = AssetBytecode::Compare(Base, MakeBytecode({ TEXT("A"), TEXT("C"), TEXT("D") }));
		if (TestEqual(TEXT("A removal is one change"), Changes.Num(), 1))
		{
			TestEqual(TEXT("The statement went away"), Changes[0].Removed, TArray<FString>({ TEXT("B") }));
			TestTrue(TEXT("Nothing was added"), Changes[0].Added.IsEmpty());
		}
	}

	// A statement replaced by another: removed and added together.
	{
		const TArray<AssetBytecode::FStatementChange> Changes = AssetBytecode::Compare(Base, MakeBytecode({ TEXT("A"), TEXT("Y"), TEXT("C"), TEXT("D") }));
		if (TestEqual(TEXT("A replacement is one change"), Changes.Num(), 1))
		{
			TestEqual(TEXT("The old statement"), Changes[0].Removed, TArray<FString>({ TEXT("B") }));
			TestEqual(TEXT("The new one"), Changes[0].Added, TArray<FString>({ TEXT("Y") }));
		}
	}

	// Two changes apart from each other are two changes.
	{
		const TArray<AssetBytecode::FStatementChange> Changes = AssetBytecode::Compare(Base, MakeBytecode({ TEXT("Q"), TEXT("A"), TEXT("B"), TEXT("C") }));
		TestEqual(TEXT("A statement at the start and one gone at the end"), Changes.Num(), 2);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBytecode_IsReportedInTheDataOfAFunction, "AssetSerializationInspector.Serialization.AssetBytecode.IsReportedInTheDataOfAFunction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBytecode_IsReportedInTheDataOfAFunction::RunTest(const FString& Parameters)
{
	using namespace BytecodeTestUtils;

	const FAssetPackageDocument Document;

	FAssetStructNativeData Old;
	Old.bIsFunction = true;
	Old.BytecodeStorageSize = 40;
	Old.BytecodeSize = 60;
	Old.Bytecode = MakeBytecode({ TEXT("LetBool(Local:Done, False)"), TEXT("Return(Nothing)"), TEXT("EndOfScript") });

	FAssetStructNativeData New = Old;
	New.BytecodeStorageSize = 52;
	New.BytecodeSize = 76;
	New.Bytecode = MakeBytecode({ TEXT("LetBool(Local:Done, False)"), TEXT("LocalFinalFunction(/Script/Engine.KismetSystemLibrary:PrintString)"), TEXT("Return(Nothing)"), TEXT("EndOfScript") });

	const TArray<FAssetNativeDataChange> Changes = AssetStructNativeData::Compare(Document, Old, Document, New);
	if (TestEqual(TEXT("The change is one statement, not a size"), Changes.Num(), 1))
	{
		TestEqual(TEXT("It was added"), static_cast<uint8>(Changes[0].State), static_cast<uint8>(FAssetNativeDataChange::EState::Added));
		TestTrue(TEXT("It names the statement"), Changes[0].Title.Contains(TEXT("statement 2")));
		TestTrue(TEXT("And what it calls"), Changes[0].NewValue.Contains(TEXT("PrintString")));
		TestTrue(TEXT("It has no old value"), Changes[0].OldValue.IsEmpty());
	}

	// The same statements with other bytes (the references of the package were numbered differently) are no change of the function.
	FAssetStructNativeData Same = Old;
	Same.BytecodeStorageSize = 44;
	TestTrue(TEXT("The same statements are no change"), AssetStructNativeData::Compare(Document, Old, Document, Same).IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
