// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
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

namespace CookedTextureTestUtils
{
	/** Reads the texture of a cooked package kept with the plugin (cooked for Windows by this engine version), with its sidecar file next to it. */
	static bool ReadFixture(FAutomationTestBase& Test, const TCHAR* Fixture, FAssetBulkDataExport& Out)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
		if (!Test.TestTrue(TEXT("The plugin is found"), Plugin.IsValid()))
		{
			return false;
		}

		FText Error;
		const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("CookedTestFixtures"), Fixture), Error);
		if (!Test.TestTrue(FString::Printf(TEXT("%s loads"), Fixture), Document.IsValid()))
		{
			return false;
		}

		const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);
		for (const FAssetPackageExportEntry& Export : Document->ExportMap)
		{
			if (BulkDataTestUtils::DecodeData(*Document, *Traces, Export, Out))
			{
				return true;
			}
		}

		Test.AddError(FString::Printf(TEXT("%s has no texture"), Fixture));
		return false;
	}

	static FAssetTextureMip MakeMip(const int32 Size, const TCHAR* Hash, const bool bInline = true)
	{
		FAssetTextureMip Mip;
		Mip.SizeX = Size;
		Mip.SizeY = Size;
		Mip.SizeZ = 1;
		Mip.PayloadSize = static_cast<int64>(Size) * Size;
		Mip.PayloadHash = Hash;
		Mip.BulkFlags = bInline ? 0u : 1u;
		return Mip;
	}

	static FAssetBulkDataExport MakeTexture(const TArray<FAssetTextureMip>& Mips)
	{
		FAssetBulkDataExport Data;
		Data.Kind = TEXT("Texture");
		Data.bComplete = true;
		Data.bCooked = true;

		FAssetTexturePlatformData& Platform = Data.PlatformData.AddDefaulted_GetRef();
		Platform.PixelFormat = TEXT("PF_DXT1");
		Platform.SizeX = Mips.IsEmpty() ? 0 : Mips[0].SizeX;
		Platform.SizeY = Platform.SizeX;
		Platform.NumSlices = 1;
		Platform.Mips = Mips;
		return Data;
	}

	static const FAssetNativeDataChange* Find(const TArray<FAssetNativeDataChange>& Changes, const TCHAR* Key)
	{
		return Changes.FindByPredicate([Key](const FAssetNativeDataChange& Change) { return Change.Key == Key; });
	}
} // namespace CookedTextureTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBulkDataExport_ReadsTheMipsOfACookedTexture, "AssetSerializationInspector.Serialization.AssetBulkDataExport.ReadsTheMipsOfACookedTexture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBulkDataExport_ReadsTheMipsOfACookedTexture::RunTest(const FString& Parameters)
{
	using namespace CookedTextureTestUtils;

	// One texture with its mips inline, and one whose larger mips stream from a sidecar file.
	for (const TCHAR* Fixture : { TEXT("T_EV_BlankWhite_01.uasset"), TEXT("T_Default_Material_Grid_N.uasset") })
	{
		FAssetBulkDataExport Data;
		if (!ReadFixture(*this, Fixture, Data))
		{
			continue;
		}

		if (!TestTrue(FString::Printf(TEXT("%s is read to its last byte (%s)"), Fixture, *Data.Error), Data.bComplete))
		{
			continue;
		}

		TestTrue(TEXT("It is cooked"), Data.bCooked);
		TestFalse(TEXT("Cooked packages keep no source image"), Data.bHasBulkData);
		AddInfo(FString::Printf(TEXT("%s: %s"), Fixture, *Data.Summarize()));
		if (!TestEqual(TEXT("It has the data of one platform"), Data.PlatformData.Num(), 1))
		{
			continue;
		}

		const FAssetTexturePlatformData& Platform = Data.PlatformData[0];
		TestTrue(TEXT("The pixel format is named"), Platform.PixelFormat.StartsWith(TEXT("PF_")));
		TestTrue(TEXT("It has a size"), Platform.SizeX > 0 && Platform.SizeY > 0);
		if (!TestFalse(TEXT("It has mips"), Platform.Mips.IsEmpty()))
		{
			continue;
		}

		for (int32 Index = 0; Index < Platform.Mips.Num(); ++Index)
		{
			const FAssetTextureMip& Mip = Platform.Mips[Index];
			TestTrue(TEXT("A mip has a size"), Mip.SizeX > 0 && Mip.SizeY > 0 && Mip.PayloadSize > 0);
			TestFalse(FString::Printf(TEXT("%s mip %d: the pixels are hashed"), Fixture, Index), Mip.PayloadHash.IsEmpty());
			if (Index > 0)
			{
				TestEqual(TEXT("Each mip is half as wide as the one before"), Mip.SizeX, FMath::Max(Platform.Mips[Index - 1].SizeX / 2, 1));
			}
		}

		TestEqual(TEXT("The first mip is the size of the texture"), Platform.Mips[0].SizeX, Platform.SizeX);

		// Reading the same file twice says nothing changed.
		TestTrue(TEXT("The same texture is no change"), AssetBulkDataExport::Compare(Data, Data).IsEmpty());
	}

	FAssetBulkDataExport Streaming;
	if (ReadFixture(*this, TEXT("T_Default_Material_Grid_N.uasset"), Streaming) && Streaming.bComplete && !Streaming.PlatformData.IsEmpty())
	{
		int32 Streamed = 0;
		for (const FAssetTextureMip& Mip : Streaming.PlatformData[0].Mips)
		{
			Streamed += Mip.IsInline() ? 0 : 1;
		}
		TestTrue(TEXT("Some of its mips stream from the sidecar file"), Streamed > 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBulkDataExport_ShowsAChangeOfTheCookedMips, "AssetSerializationInspector.Serialization.AssetBulkDataExport.ShowsAChangeOfTheCookedMips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBulkDataExport_ShowsAChangeOfTheCookedMips::RunTest(const FString& Parameters)
{
	using namespace CookedTextureTestUtils;

	const FAssetBulkDataExport Base = MakeTexture({ MakeMip(8, TEXT("aaaa")), MakeMip(4, TEXT("bbbb")), MakeMip(2, TEXT("cccc")) });

	// Other pixels in one mip: that mip, and only that one.
	{
		FAssetBulkDataExport Repainted = Base;
		Repainted.PlatformData[0].Mips[1].PayloadHash = TEXT("zzzz");

		const TArray<FAssetNativeDataChange> Changes = AssetBulkDataExport::Compare(Base, Repainted);
		TestEqual(TEXT("One change"), Changes.Num(), 1);
		if (const FAssetNativeDataChange* Change = Find(Changes, TEXT("Platform/0/Mip/1")))
		{
			TestEqual(TEXT("The mip is modified"), static_cast<uint8>(Change->State), static_cast<uint8>(FAssetNativeDataChange::EState::Modified));
			TestTrue(TEXT("From the old pixels"), Change->OldValue.Contains(TEXT("bbbb")));
			TestTrue(TEXT("To the new"), Change->NewValue.Contains(TEXT("zzzz")));
		}
		else
		{
			AddError(TEXT("The change of the pixels is not reported"));
		}
	}

	// Where the pixels are kept moves from cook to cook, and a hash that could not be read says nothing.
	{
		FAssetBulkDataExport Moved = Base;
		Moved.PlatformData[0].Mips[0].Offset = 1234;
		Moved.PlatformData[0].Mips[2].PayloadHash.Empty();
		TestTrue(TEXT("A move of the data is no change"), AssetBulkDataExport::Compare(Base, Moved).IsEmpty());
	}

	// Another format, size or number of mips.
	{
		FAssetBulkDataExport Other = MakeTexture({ MakeMip(16, TEXT("xxxx")), MakeMip(8, TEXT("aaaa")), MakeMip(4, TEXT("bbbb")), MakeMip(2, TEXT("cccc")) });
		Other.PlatformData[0].PixelFormat = TEXT("PF_BC7");

		const TArray<FAssetNativeDataChange> Changes = AssetBulkDataExport::Compare(Base, Other);
		TestNotNull(TEXT("The pixel format"), Find(Changes, TEXT("Platform/0/PixelFormat")));
		TestNotNull(TEXT("The size"), Find(Changes, TEXT("Platform/0/Size")));
		TestNotNull(TEXT("The number of mips"), Find(Changes, TEXT("Platform/0/MipCount")));
		if (const FAssetNativeDataChange* Added = Find(Changes, TEXT("Platform/0/Mip/3")))
		{
			TestEqual(TEXT("The mip that the new texture has beyond the old is added"), static_cast<uint8>(Added->State), static_cast<uint8>(FAssetNativeDataChange::EState::Added));
		}
		else
		{
			AddError(TEXT("The added mip is not reported"));
		}
	}

	// A mip that moved from the package to a sidecar file.
	{
		FAssetBulkDataExport Streamed = Base;
		Streamed.PlatformData[0].Mips[0] = MakeMip(8, TEXT("aaaa"), false);

		const TArray<FAssetNativeDataChange> Changes = AssetBulkDataExport::Compare(Base, Streamed);
		if (const FAssetNativeDataChange* Change = Find(Changes, TEXT("Platform/0/Mip/0")))
		{
			TestTrue(TEXT("The change says how the mip is stored"), Change->NewValue.Contains(TEXT("streamed")));
		}
		else
		{
			AddError(TEXT("The change of the storage is not reported"));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBulkDataExport_ReadsTheOlderBulkDataFormat, "AssetSerializationInspector.Serialization.AssetBulkDataExport.ReadsTheOlderBulkDataFormat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBulkDataExport_ReadsTheOlderBulkDataFormat::RunTest(const FString& Parameters)
{
	using namespace BulkDataTestUtils;

	// A package from before the editor bulk data: its source is a header of the older bulk data format.
	const FString File = FPaths::Combine(FPaths::EngineContentDir(), TEXT("WebBrowser"), TEXT("WebTexture_T.uasset"));
	if (!IFileManager::Get().FileExists(*File))
	{
		AddInfo(TEXT("The engine does not have the web browser texture, so there is nothing to read."));
		return true;
	}

	FText Error;
	const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(File, Error);
	if (!TestTrue(TEXT("It loads"), Document.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);
	bool bRead = false;
	for (const FAssetPackageExportEntry& Export : Document->ExportMap)
	{
		FAssetBulkDataExport Data;
		if (!DecodeData(*Document, *Traces, Export, Data))
		{
			continue;
		}

		bRead = true;
		TestTrue(FString::Printf(TEXT("It is read to its last byte (%s)"), *Data.Error), Data.bComplete);
		TestTrue(TEXT("It has the record of its source"), Data.bHasBulkData);
		TestTrue(TEXT("In the older format"), Data.Bulk.bLegacy);
		TestTrue(TEXT("Which the storage says"), Data.Bulk.DescribeStorage().Contains(TEXT("older bulk data format")));
	}
	TestTrue(TEXT("The package has a texture"), bRead);

	// A resave that upgraded the record to the current format did not change the image when the size is the same: the two hashes mean
	// different things, so only the storage differs.
	FAssetBulkDataExport Legacy;
	Legacy.Kind = TEXT("Texture");
	Legacy.bComplete = true;
	Legacy.bHasBulkData = true;
	Legacy.Bulk.bLegacy = true;
	Legacy.Bulk.ContentHash = TEXT("1111111111111111111111111111111111111111");
	Legacy.Bulk.PayloadSize = 64;

	FAssetBulkDataExport Current = Legacy;
	Current.Bulk.bLegacy = false;
	Current.Bulk.ContentHash = TEXT("2222222222222222222222222222222222222222");

	const TArray<FAssetNativeDataChange> Upgraded = AssetBulkDataExport::Compare(Legacy, Current);
	TestTrue(TEXT("The image is not reported as changed"), CookedTextureTestUtils::Find(Upgraded, TEXT("BulkData/Source")) == nullptr);
	TestNotNull(TEXT("The storage is"), CookedTextureTestUtils::Find(Upgraded, TEXT("BulkData/Storage")));

	Current.Bulk.PayloadSize = 128;
	TestNotNull(TEXT("A different size is a different image whatever the format"), CookedTextureTestUtils::Find(AssetBulkDataExport::Compare(Legacy, Current), TEXT("BulkData/Source")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
