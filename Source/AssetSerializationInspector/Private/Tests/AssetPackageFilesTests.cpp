// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "Compare/AssetFolderComparison.h"
#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Trace/AssetPackageFieldDecoder.h"
#include "Trace/AssetSerializationTrace.h"

namespace
{
	FString GetFixture(const TCHAR* Name)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
		return Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("TestFixtures"), Name) : FString();
	}

	int32 CountProperties(const FAssetPackageTraceCollection& Traces)
	{
		int32 Count = 0;
		for (const TPair<int32, FAssetSerializationTrace>& Item : Traces.ExportTraces)
		{
			if (Item.Value.Root.IsValid())
			{
				for (const TSharedPtr<FAssetSerializationTraceNode>& Node : Item.Value.Root->Children)
				{
					Count += Node.IsValid() && Node->Kind == EAssetSerializationTraceKind::Property ? 1 : 0;
				}
			}
		}
		return Count;
	}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetPackageFiles_ReadsALevel, "AssetSerializationInspector.Readers.AssetPackageFiles.ReadsALevel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPackageFiles_ReadsALevel::RunTest(const FString& Parameters)
{
	const FString PackageName = TEXT("/Game/__AssetSerializationInspectorTests/InspectorTestLevel");
	UPackage* Package = CreatePackage(*PackageName);
	UWorld* World = UWorld::CreateWorld(EWorldType::Inactive, false, TEXT("InspectorTestLevel"), Package, false);
	if (!TestNotNull(TEXT("A world is created"), World))
	{
		return false;
	}

	World->SetFlags(RF_Public | RF_Standalone);
	Package->MarkAsFullyLoaded();
	const FString File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetMapPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.bSlowTask = false;
	const bool bSaved = UPackage::SavePackage(Package, World, *File, SaveArgs);
	TestTrue(TEXT("The level is written"), bSaved);

	if (bSaved)
	{
		FText Error;
		const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(File, Error);
		if (TestTrue(TEXT("A .umap loads"), Document.IsValid()))
		{
			TestTrue(TEXT("Its tables decode"), Document->NameMapError.IsEmpty() && Document->ImportMapError.IsEmpty() && Document->ExportMapError.IsEmpty());
			TestTrue(TEXT("It has exports"), Document->ExportMap.Num() > 0);

			const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);
			TestTrue(TEXT("Its properties are found"), CountProperties(*Traces) > 0);
		}

		// Folder scans pick up levels as well as assets.
		const FString Folder = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("AssetSerializationInspector"), TEXT("Levels"));
		IFileManager::Get().DeleteDirectory(*Folder, false, true);
		IFileManager::Get().Copy(*FPaths::Combine(Folder, TEXT("Level.umap")), *File);
		IFileManager::Get().Copy(*FPaths::Combine(Folder, TEXT("Asset.uasset")), *GetFixture(TEXT("BP_Box1.uasset")));
		TestEqual(TEXT("A folder scan finds the level and the asset"), AssetFolderComparison::FindPackageFiles(Folder).Num(), 2);
		IFileManager::Get().DeleteDirectory(*Folder, false, true);
	}

	IFileManager::Get().Delete(*File, false, true, true);
	World->DestroyWorld(false);
	Package->MarkAsGarbage();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetPackageFiles_JoinsHeaderAndExportFiles, "AssetSerializationInspector.Readers.AssetPackageFiles.JoinsHeaderAndExportFiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPackageFiles_JoinsHeaderAndExportFiles::RunTest(const FString& Parameters)
{
	FText Error;
	const TSharedPtr<FAssetPackageDocument> Whole = FAssetPackageReader::LoadFromFile(GetFixture(TEXT("BP_Box1.uasset")), Error);
	if (!TestTrue(TEXT("The fixture loads"), Whole.IsValid()))
	{
		return false;
	}

	// Split it the way a cooked package is stored: the header in the .uasset, everything after it in the .uexp.
	const int64 HeaderSize = Whole->PackageSummary.TotalHeaderSize;
	TArray<uint8> Bytes;
	FFileHelper::LoadFileToArray(Bytes, *GetFixture(TEXT("BP_Box1.uasset")));
	if (!TestTrue(TEXT("The header is smaller than the file"), HeaderSize > 0 && HeaderSize < Bytes.Num()))
	{
		return false;
	}

	const FString Folder = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("AssetSerializationInspector"), TEXT("Split"));
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	const FString Header = FPaths::Combine(Folder, TEXT("Split.uasset"));
	const FString ExportData = FPaths::Combine(Folder, TEXT("Split.uexp"));
	FFileHelper::SaveArrayToFile(TArrayView<const uint8>(Bytes.GetData(), HeaderSize), *Header);
	FFileHelper::SaveArrayToFile(TArrayView<const uint8>(Bytes.GetData() + HeaderSize, Bytes.Num() - HeaderSize), *ExportData);

	for (const FString& Opened : { Header, ExportData })
	{
		const TSharedPtr<FAssetPackageDocument> Split = FAssetPackageReader::LoadFromFile(Opened, Error);
		if (!TestTrue(FString::Printf(TEXT("%s loads"), *FPaths::GetCleanFilename(Opened)), Split.IsValid()))
		{
			continue;
		}

		TestEqual(TEXT("The two files are read as one"), Split->FileData.Num(), static_cast<int64>(Bytes.Num()));
		TestEqual(TEXT("The size of the header file is kept"), Split->HeaderFileSize, HeaderSize);
		TestEqual(TEXT("The export file is named"), FPaths::GetCleanFilename(Split->ExportDataFilename), FString(TEXT("Split.uexp")));
		TestEqual(TEXT("The exports are the same"), Split->ExportMap.Num(), Whole->ExportMap.Num());
		TestEqual(TEXT("So are the properties found in them"), CountProperties(*FAssetPackageFieldDecoder::Decode(*Split)), CountProperties(*FAssetPackageFieldDecoder::Decode(*Whole)));
	}

	// A header without its export file is still read.
	IFileManager::Get().Delete(*ExportData);
	const TSharedPtr<FAssetPackageDocument> HeaderOnly = FAssetPackageReader::LoadFromFile(Header, Error);
	TestTrue(TEXT("A header alone loads"), HeaderOnly.IsValid());

	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetPackageFiles_ReportsUnversionedProperties, "AssetSerializationInspector.Readers.AssetPackageFiles.ReportsUnversionedProperties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPackageFiles_ReportsUnversionedProperties::RunTest(const FString& Parameters)
{
	FText Error;
	const TSharedPtr<FAssetPackageDocument> Original = FAssetPackageReader::LoadFromFile(GetFixture(TEXT("BP_Box1.uasset")), Error);
	if (!TestTrue(TEXT("The fixture loads"), Original.IsValid()))
	{
		return false;
	}

	// Pretend the package was saved without property tags (as cooked packages usually are), whatever its bytes say: set the flag in the summary, where the
	// package flags follow the total header size, after the custom versions and the folder name.
	TArray<uint8> Bytes;
	FFileHelper::LoadFileToArray(Bytes, *GetFixture(TEXT("BP_Box1.uasset")));
	int32 FlagsOffset = INDEX_NONE;
	const int32 HeaderSize = Original->PackageSummary.TotalHeaderSize;
	const uint32 OriginalFlags = Original->PackageSummary.GetPackageFlags();
	for (int32 Offset = 0; Offset + 4 < FMath::Min(Bytes.Num(), 128) && FlagsOffset == INDEX_NONE; ++Offset)
	{
		int32 Size = 0;
		FMemory::Memcpy(&Size, Bytes.GetData() + Offset, sizeof(Size));
		if (Size != HeaderSize)
		{
			continue;
		}

		for (int32 Gap = 4; Gap < 512 && Offset + Gap + 4 <= Bytes.Num(); ++Gap)
		{
			uint32 Candidate = 0;
			FMemory::Memcpy(&Candidate, Bytes.GetData() + Offset + Gap, sizeof(Candidate));
			if (Candidate == OriginalFlags)
			{
				FlagsOffset = Offset + Gap;
				break;
			}
		}
	}

	if (!TestTrue(TEXT("The package flags are found in the summary"), FlagsOffset != INDEX_NONE))
	{
		return false;
	}

	uint32 Flags = Original->PackageSummary.GetPackageFlags() | PKG_UnversionedProperties;
	FMemory::Memcpy(Bytes.GetData() + FlagsOffset, &Flags, sizeof(Flags));

	const FString File = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("AssetSerializationInspector"), TEXT("Unversioned"), TEXT("Unversioned.uasset"));
	FFileHelper::SaveArrayToFile(Bytes, *File);
	const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(File, Error);
	IFileManager::Get().DeleteDirectory(*FPaths::GetPath(File), false, true);
	if (!TestTrue(TEXT("The changed package loads"), Document.IsValid()) || !TestTrue(TEXT("It is unversioned"), (Document->PackageSummary.GetPackageFlags() & PKG_UnversionedProperties) != 0))
	{
		return false;
	}

	const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);

	// The bytes are really tags, so what an unversioned reading makes of them is meaningless, but it must stay inside the exports and
	// leave no gaps or overlaps that a later step would trip on: every export is covered once, in order.
	TestFalse(TEXT("Every export has a trace"), Traces->ExportTraces.IsEmpty());

	bool bInsideExports = true;
	bool bCoveredInOrder = true;
	for (const FAssetPackageExportEntry& Export : Document->ExportMap)
	{
		const FAssetSerializationTrace* Trace = Traces->FindExportTrace(Export.Index);
		if (Trace == nullptr || !Trace->Root.IsValid())
		{
			continue;
		}

		int64 Next = 0;
		for (const TSharedPtr<FAssetSerializationTraceNode>& Node : Trace->Root->Children)
		{
			bInsideExports &= Node->Offset >= 0 && Node->Size >= 0 && Node->Offset + Node->Size <= Export.SerialSize;
			bCoveredInOrder &= Node->Offset >= Next;
			Next = Node->Offset + Node->Size;
		}
	}
	TestTrue(TEXT("Nothing reaches outside its export"), bInsideExports);
	TestTrue(TEXT("Nothing overlaps"), bCoveredInOrder);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
