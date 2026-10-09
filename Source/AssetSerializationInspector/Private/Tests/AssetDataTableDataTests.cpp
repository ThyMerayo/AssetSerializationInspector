// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Serialization/AssetDataTableData.h"
#include "Trace/AssetPackageFieldDecoder.h"
#include "Trace/AssetSerializationTrace.h"

namespace DataTableTestUtils
{
	/** The data a data table writes after its tagged properties: the last range of its trace that no property accounts for. */
	static bool DecodeData(const FAssetPackageDocument& Document, const FAssetPackageTraceCollection& Traces, const FAssetPackageExportEntry& Export, FAssetDataTableData& Out)
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

		return Native != nullptr && AssetDataTableData::Decode(Document, Export, Export.SerialOffset + Native->Offset, Native->Size, Out, Trace);
	}

	/** Reads the first data table of a package. */
	static bool ReadTable(FAutomationTestBase& Test, const FString& File, FAssetDataTableData& Out)
	{
		FText Error;
		const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(File, Error);
		if (!Test.TestTrue(FString::Printf(TEXT("%s loads"), *FPaths::GetCleanFilename(File)), Document.IsValid()))
		{
			return false;
		}

		const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);
		for (const FAssetPackageExportEntry& Export : Document->ExportMap)
		{
			if (DecodeData(*Document, *Traces, Export, Out))
			{
				return true;
			}
		}
		return Test.TestTrue(TEXT("The package has a data table"), false);
	}

	static FString FixturePath(const TCHAR* Name)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
		return Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("DataTableFixtures"), Name) : FString();
	}

	static FAssetDataTableRow MakeRow(const TCHAR* Name, std::initializer_list<TPair<FString, FString>> Properties)
	{
		FAssetDataTableRow Row;
		Row.Name = Name;
		Row.Properties = TArray<TPair<FString, FString>>(Properties);
		return Row;
	}

	static FAssetDataTableData MakeData(const TArray<FAssetDataTableRow>& Rows)
	{
		FAssetDataTableData Data;
		Data.bComplete = true;
		Data.Rows = Rows;
		return Data;
	}

	static const FAssetNativeDataChange* Find(const TArray<FAssetNativeDataChange>& Changes, const TCHAR* Key)
	{
		return Changes.FindByPredicate([Key](const FAssetNativeDataChange& Change) { return Change.Key == Key; });
	}
} // namespace DataTableTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetDataTableData_ReadsTheRowsOfADataTable, "AssetSerializationInspector.Serialization.AssetDataTableData.ReadsTheRowsOfADataTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetDataTableData_ReadsTheRowsOfADataTable::RunTest(const FString& Parameters)
{
	using namespace DataTableTestUtils;

	FAssetDataTableData Data;
	if (!ReadTable(*this, FixturePath(TEXT("DT_SolarSystemPlanets.uasset")), Data))
	{
		return false;
	}

	TestTrue(FString::Printf(TEXT("The table is read to its last byte (%s)"), *Data.Error), Data.bComplete);
	TestEqual(TEXT("The struct of the rows"), Data.RowStruct, FString(TEXT("/Script/CelestialVault.PlanetaryBodyInputData")));
	TestEqual(TEXT("Seven rows"), Data.Rows.Num(), 7);
	TestTrue(TEXT("It says what it holds"), Data.Summarize().Contains(TEXT("7 rows of PlanetaryBodyInputData")));

	const FAssetDataTableRow* Mars = Data.Rows.FindByPredicate(
		[](const FAssetDataTableRow& Row) { return Row.Properties.ContainsByPredicate([](const TPair<FString, FString>& Property) { return Property.Value == TEXT("Mars"); }); });
	if (TestNotNull(TEXT("A row is Mars"), Mars))
	{
		TestEqual(TEXT("Five properties"), Mars->Properties.Num(), 5);
		const TPair<FString, FString>* Radius = Mars->Properties.FindByPredicate([](const TPair<FString, FString>& Property) { return Property.Key == TEXT("Radius"); });
		if (TestNotNull(TEXT("With a radius"), Radius))
		{
			TestTrue(TEXT("Of 3390"), Radius->Value.StartsWith(TEXT("3390")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetDataTableData_ShowsTheValueThatChangedInARow, "AssetSerializationInspector.Serialization.AssetDataTableData.ShowsTheValueThatChangedInARow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetDataTableData_ShowsTheValueThatChangedInARow::RunTest(const FString& Parameters)
{
	using namespace DataTableTestUtils;

	const FString Source = FixturePath(TEXT("DT_SolarSystemPlanets.uasset"));
	FAssetDataTableData Old;
	if (!ReadTable(*this, Source, Old))
	{
		return false;
	}

	// A copy in which the radius of Mars (a double, 3390) is 3391: the only byte range that differs is that value.
	TArray<uint8> Bytes;
	if (!TestTrue(TEXT("The fixture is read"), FFileHelper::LoadFileToArray(Bytes, *Source)))
	{
		return false;
	}

	const double From = 3390.0;
	const double To = 3391.0;
	int32 Patched = 0;
	for (int32 Index = 0; Index + static_cast<int32>(sizeof(double)) <= Bytes.Num(); ++Index)
	{
		if (FMemory::Memcmp(Bytes.GetData() + Index, &From, sizeof(double)) == 0)
		{
			FMemory::Memcpy(Bytes.GetData() + Index, &To, sizeof(double));
			++Patched;
		}
	}
	TestEqual(TEXT("The radius is in the file once"), Patched, 1);

	const FString Folder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("__AssetSerializationInspectorTests"), TEXT("DataTable"));
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	const FString Copy = FPaths::Combine(Folder, TEXT("DT_SolarSystemPlanets.uasset"));
	if (!TestTrue(TEXT("The copy is saved"), FFileHelper::SaveArrayToFile(Bytes, *Copy)))
	{
		return false;
	}

	FAssetDataTableData New;
	const bool bRead = ReadTable(*this, Copy, New);
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	if (!bRead)
	{
		return false;
	}

	const TArray<FAssetNativeDataChange> Changes = AssetDataTableData::Compare(Old, New);
	if (TestEqual(TEXT("One change"), Changes.Num(), 1))
	{
		TestEqual(TEXT("A property of a row"), Changes[0].State, FAssetNativeDataChange::EState::Modified);
		TestTrue(TEXT("The radius"), Changes[0].Key.EndsWith(TEXT("/Radius")));
		TestTrue(TEXT("Of Mars"), Changes[0].OldValue.StartsWith(TEXT("3390")) && Changes[0].NewValue.StartsWith(TEXT("3391")));
	}
	TestTrue(TEXT("The same table is no change"), AssetDataTableData::Compare(Old, Old).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetDataTableData_ShowsRowsAddedRemovedAndChanged, "AssetSerializationInspector.Serialization.AssetDataTableData.ShowsRowsAddedRemovedAndChanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetDataTableData_ShowsRowsAddedRemovedAndChanged::RunTest(const FString& Parameters)
{
	using namespace DataTableTestUtils;

	const FAssetDataTableRow Sword = MakeRow(TEXT("Sword"), { { TEXT("Damage"), TEXT("10") }, { TEXT("Name"), TEXT("Sword") } });
	const FAssetDataTableRow Shield = MakeRow(TEXT("Shield"), { { TEXT("Armor"), TEXT("5") } });
	const FAssetDataTableData Base = MakeData({ Sword, Shield });

	// The order the rows are written in is not a change: a map has no order.
	TestTrue(TEXT("The same rows in another order are no change"), AssetDataTableData::Compare(Base, MakeData({ Shield, Sword })).IsEmpty());

	// A row added, a row removed: each is listed property by property, not as one entry with all the values. A Blueprint struct names its members Name_Number_Guid.
	{
		const FString Guid = TEXT("6FB71CC6442A07670EDFF79A8B733C15");
		const FAssetDataTableRow Bow = MakeRow(TEXT("Bow"), { { TEXT("Range_16_") + Guid, TEXT("X=4.000 Y=0.5 Z=6.000") }, { TEXT("Speed"), TEXT("30") } });
		const TArray<FAssetNativeDataChange> Changes = AssetDataTableData::Compare(Base, MakeData({ Sword, Bow }));
		TestEqual(TEXT("One change for the property of the removed row, two for the added row"), Changes.Num(), 3);
		if (const FAssetNativeDataChange* Removed = Find(Changes, TEXT("Row/Shield/Armor")))
		{
			TestEqual(TEXT("A property of the removed row"), Removed->State, FAssetNativeDataChange::EState::Removed);
			TestEqual(TEXT("With its value"), Removed->OldValue, FString(TEXT("5")));
			TestEqual(TEXT("Under the row"), Removed->Title, FString(TEXT("Row Shield: Armor")));
		}
		else
		{
			AddError(TEXT("The removed row is listed by property"));
		}
		if (const FAssetNativeDataChange* Added = Find(Changes, *(TEXT("Row/Bow/Range_16_") + Guid)))
		{
			TestEqual(TEXT("A property of the added row"), Added->State, FAssetNativeDataChange::EState::Added);
			TestEqual(TEXT("With its value alone"), Added->NewValue, FString(TEXT("X=4.000 Y=0.5 Z=6.000")));
			TestEqual(TEXT("Named without the number and the identifier of the struct member"), Added->Title, FString(TEXT("Row Bow: Range")));
		}
		else
		{
			AddError(TEXT("The added row is listed by property"));
		}
		TestNotNull(TEXT("Every property has its own entry"), Find(Changes, TEXT("Row/Bow/Speed")));
		TestNull(TEXT("There is no entry for the whole row"), Find(Changes, TEXT("Row/Bow")));
	}

	// A value of a row, a property that went away and one that came.
	{
		const FAssetDataTableRow Changed = MakeRow(TEXT("Sword"), { { TEXT("Damage"), TEXT("12") }, { TEXT("Weight"), TEXT("3") } });
		const TArray<FAssetNativeDataChange> Changes = AssetDataTableData::Compare(Base, MakeData({ Changed, Shield }));
		TestEqual(TEXT("Three changes"), Changes.Num(), 3);
		if (const FAssetNativeDataChange* Damage = Find(Changes, TEXT("Row/Sword/Damage")))
		{
			TestEqual(TEXT("A value changed"), Damage->State, FAssetNativeDataChange::EState::Modified);
			TestTrue(TEXT("From 10 to 12"), Damage->OldValue == TEXT("10") && Damage->NewValue == TEXT("12"));
		}
		else
		{
			AddError(TEXT("The value is a change"));
		}
		const FAssetNativeDataChange* Name = Find(Changes, TEXT("Row/Sword/Name"));
		TestTrue(TEXT("A property was removed"), Name != nullptr && Name->State == FAssetNativeDataChange::EState::Removed);
		const FAssetNativeDataChange* Weight = Find(Changes, TEXT("Row/Sword/Weight"));
		TestTrue(TEXT("A property was added"), Weight != nullptr && Weight->State == FAssetNativeDataChange::EState::Added);
	}

	// Another row struct is a change of its own; a table that was not read compares to nothing.
	{
		FAssetDataTableData Other = Base;
		Other.RowStruct = TEXT("/Game/FWeapon.FWeapon");
		TestNotNull(TEXT("The row struct"), Find(AssetDataTableData::Compare(Base, Other), TEXT("RowStruct")));

		FAssetDataTableData Unread = Base;
		Unread.bComplete = false;
		TestTrue(TEXT("An unread table has no changes to list"), AssetDataTableData::Compare(Base, Unread).IsEmpty());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetDataTableData_ReadsTheDataTablesOfTheEngineContent, "AssetSerializationInspector.Serialization.AssetDataTableData.ReadsTheDataTablesOfTheEngineContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetDataTableData_ReadsTheDataTablesOfTheEngineContent::RunTest(const FString& Parameters)
{
	using namespace DataTableTestUtils;

	// The star catalogs of the celestial vault: the biggest data tables of the engine (the full one has hundreds of thousands of rows).
	const FString Folder = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Runtime"), TEXT("CelestialVault"), TEXT("Content"), TEXT("Data"));
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Folder, TEXT("DT_*.uasset")), true, false);
	if (Files.IsEmpty())
	{
		AddInfo(TEXT("The engine does not have the data tables of the celestial vault, so there is nothing to read."));
		return true;
	}

	int32 Rows = 0;
	for (const FString& File : Files)
	{
		FAssetDataTableData Data;
		if (!ReadTable(*this, FPaths::Combine(Folder, File), Data))
		{
			return false;
		}
		TestTrue(FString::Printf(TEXT("%s is read to its last byte (%s)"), *File, *Data.Error), Data.bComplete);
		Rows += Data.Rows.Num();
	}

	AddInfo(FString::Printf(TEXT("%d data tables, %d rows"), Files.Num(), Rows));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
