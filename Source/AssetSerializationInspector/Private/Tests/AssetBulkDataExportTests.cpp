// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "Diff/AssetPackageDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Save/AssetSaveAnalyzer.h"
#include "Serialization/AssetBulkDataExport.h"
#include "Tests/AssetTestPackageNames.h"
#include "Trace/AssetPackageFieldDecoder.h"
#include "Trace/AssetSerializationTrace.h"

namespace BulkDataTestUtils
{
	static const TCHAR* const TexturePackage = TEXT("/Game/__AssetSerializationInspectorTests/BulkData/T_BulkData");
	static const TCHAR* const TextureFolder = TEXT("/Game/__AssetSerializationInspectorTests/BulkData");

	/** The data a texture or mesh description writes after its tagged properties: the last range of its trace that no property accounts for. */
	static bool DecodeData(const FAssetPackageDocument& Document, const FAssetPackageTraceCollection& Traces, const FAssetPackageExportEntry& Export, FAssetBulkDataExport& Out)
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

		return Native != nullptr && AssetBulkDataExport::Decode(Document, Export, Export.SerialOffset + Native->Offset, Native->Size, Out);
	}

	/** A 4 by 4 texture of one grey, saved to the file of its package (and a copy of the file under another extension, which the next save would overwrite). */
	struct FTestTexture
	{
		UPackage* Package = nullptr;
		UTexture2D* Texture = nullptr;

		FTestTexture()
		{
			Package = CreatePackage(*AssetTestPackages::Unique(TexturePackage));
			Texture = NewObject<UTexture2D>(Package, TEXT("T_BulkData"), RF_Public | RF_Standalone);
			SetGrey(0x40);
		}

		/** Gives the source image another content. */
		void SetGrey(const uint8 Value)
		{
			TArray<uint8> Pixels;
			Pixels.Init(Value, 4 * 4 * 4);
			Texture->Source.Init(4, 4, 1, 1, TSF_BGRA8, Pixels.GetData());
		}

		FString Save(const TCHAR* CopyExtension = nullptr) const
		{
			const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			SaveArgs.bSlowTask = false;
			if (!UPackage::SavePackage(Package, Texture, *File, SaveArgs))
			{
				return FString();
			}

			if (CopyExtension == nullptr)
			{
				return File;
			}

			const FString Copy = FPaths::ChangeExtension(File, CopyExtension);
			IFileManager::Get().Copy(*Copy, *File);
			return Copy;
		}
	};

	static const FAssetPackageDiffEntry* FindByKey(const FAssetPackageDiffEntry& Entry, const FString& Key)
	{
		if (Entry.Key == Key && Entry.Kind == EAssetPackageDiffKind::Property)
		{
			return &Entry;
		}

		for (const FAssetPackageDiffEntry& Child : Entry.Children)
		{
			if (const FAssetPackageDiffEntry* Found = FindByKey(Child, Key))
			{
				return Found;
			}
		}
		return nullptr;
	}

	static const FAssetPackageDiffEntry* FindByKey(const FAssetPackageDiffResult& Diff, const FString& Key)
	{
		for (const FAssetPackageDiffEntry& Entry : Diff.Entries)
		{
			if (const FAssetPackageDiffEntry* Found = FindByKey(Entry, Key))
			{
				return Found;
			}
		}
		return nullptr;
	}
} // namespace BulkDataTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBulkDataExport_ReadsTheTexturesAndMeshesOfTheEngineContent,
	"AssetSerializationInspector.Serialization.AssetBulkDataExport.ReadsTheTexturesAndMeshesOfTheEngineContent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBulkDataExport_ReadsTheTexturesAndMeshesOfTheEngineContent::RunTest(const FString& Parameters)
{
	using namespace BulkDataTestUtils;

	const FString Texture = FPaths::Combine(FPaths::EngineContentDir(), TEXT("ArtTools"), TEXT("RenderToTexture"), TEXT("Textures"), TEXT("127grey.uasset"));
	const FString Mesh = FPaths::Combine(FPaths::EngineContentDir(), TEXT("ArtTools"), TEXT("RenderToTexture"), TEXT("Meshes"), TEXT("S_1_Unit_Plane.uasset"));
	if (!IFileManager::Get().FileExists(*Texture) || !IFileManager::Get().FileExists(*Mesh))
	{
		AddInfo(TEXT("The engine does not have the render to texture assets, so there is nothing to read."));
		return true;
	}

	for (const FString& File : { Texture, Mesh })
	{
		FText Error;
		const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(File, Error);
		if (!TestTrue(FString::Printf(TEXT("%s loads"), *File), Document.IsValid()))
		{
			continue;
		}

		const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);

		int32 Read = 0;
		for (const FAssetPackageExportEntry& Export : Document->ExportMap)
		{
			FAssetBulkDataExport Data;
			if (!DecodeData(*Document, *Traces, Export, Data))
			{
				continue;
			}

			++Read;
			TestTrue(FString::Printf(TEXT("%s: %s is read to its last byte (%s)"), *File, *Document->ResolveExportPath(Export.Index), *Data.Error), Data.bComplete);
			AddInfo(FString::Printf(TEXT("%s: %s"), *Document->ResolveExportPath(Export.Index), *Data.Summarize()));

			if (Data.bComplete && Data.bHasBulkData && Data.Bulk.PayloadSize > 0)
			{
				TestEqual(TEXT("The content hash is 20 bytes"), Data.Bulk.ContentHash.Len(), 40);
				TestEqual(TEXT("Its id is a GUID written with hyphens"), Data.Bulk.Id.Len(), 36);
			}
		}

		TestTrue(FString::Printf(TEXT("%s has a texture or a mesh description"), *File), Read > 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBulkDataExport_ShowsAChangeOfTheSourceImage, "AssetSerializationInspector.Serialization.AssetBulkDataExport.ShowsAChangeOfTheSourceImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBulkDataExport_ShowsAChangeOfTheSourceImage::RunTest(const FString& Parameters)
{
	using namespace BulkDataTestUtils;

	IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(TextureFolder), false, true);

	FTestTexture Test;
	const FString Grey = Test.Save(TEXT("grey.uasset"));
	const FString Again = Test.Save(TEXT("again.uasset"));

	Test.SetGrey(0x80);
	const FString Lighter = Test.Save(TEXT("lighter.uasset"));

	FText Error;
	const TSharedPtr<FAssetPackageDocument> GreyDocument = FAssetPackageReader::LoadFromFile(Grey, Error);
	const TSharedPtr<FAssetPackageDocument> AgainDocument = FAssetPackageReader::LoadFromFile(Again, Error);
	const TSharedPtr<FAssetPackageDocument> LighterDocument = FAssetPackageReader::LoadFromFile(Lighter, Error);
	if (!TestTrue(TEXT("The three versions are written and load"), GreyDocument.IsValid() && AgainDocument.IsValid() && LighterDocument.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FAssetPackageTraceCollection> GreyTraces = FAssetPackageFieldDecoder::Decode(*GreyDocument);
	const TSharedPtr<FAssetPackageTraceCollection> AgainTraces = FAssetPackageFieldDecoder::Decode(*AgainDocument);
	const TSharedPtr<FAssetPackageTraceCollection> LighterTraces = FAssetPackageFieldDecoder::Decode(*LighterDocument);

	// What the record says about the source image the editor holds: the size of the 4 by 4 BGRA image.
	FAssetBulkDataExport GreyData;
	bool bRead = false;
	for (const FAssetPackageExportEntry& Export : GreyDocument->ExportMap)
	{
		bRead |= DecodeData(*GreyDocument, *GreyTraces, Export, GreyData);
		if (bRead)
		{
			break;
		}
	}

	if (TestTrue(TEXT("The texture's data is read"), bRead))
	{
		TestTrue(*FString::Printf(TEXT("To the last byte (%s)"), *GreyData.Error), GreyData.bComplete);
		TestTrue(TEXT("It has the record of the source image"), GreyData.bHasBulkData);
		TestEqual(TEXT("Of the size of the image"), GreyData.Bulk.PayloadSize, static_cast<int64>(4 * 4 * 4));
		TestFalse(TEXT("It is not cooked"), GreyData.bCooked);
	}

	// Another content: the diff names the source image, with the hash and the size of both.
	const FAssetPackageDiffResult ToLighter = AssetPackageDiff::Compare(*GreyDocument, *LighterDocument, GreyTraces.Get(), LighterTraces.Get());
	if (const FAssetPackageDiffEntry* Source = FindByKey(ToLighter, TEXT("BulkData/Source")))
	{
		TestEqual(TEXT("The source image is modified"), Source->State, EAssetPackageDiffState::Modified);
		TestNotEqual(TEXT("With another hash"), Source->OldValue, Source->NewValue);
		TestTrue(TEXT("That says how many bytes it has"), Source->NewValue.Contains(TEXT("64 bytes")));
	}
	else
	{
		AddError(TEXT("The change of the source image is not in the diff"));
	}

	// The same content saved again: nothing of the texture changed, whatever the identifier of the save and the place of the data.
	const FAssetPackageDiffResult Same = AssetPackageDiff::Compare(*GreyDocument, *AgainDocument, GreyTraces.Get(), AgainTraces.Get());
	TestNull(TEXT("A resave of the same image reports no change of the source image"), FindByKey(Same, TEXT("BulkData/Source")));

	IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(TextureFolder), false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
