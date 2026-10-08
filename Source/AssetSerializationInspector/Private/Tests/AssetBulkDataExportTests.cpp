// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Interfaces/IPluginManager.h"
#include "MeshDescription.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PixelFormat.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "Diff/AssetPackageDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Save/AssetSaveAnalyzer.h"
#include "Serialization/AssetBlockDecoder.h"
#include "Serialization/AssetBulkDataExport.h"
#include "Serialization/AssetCookedBulkData.h"
#include "Serialization/AssetMeshGeometry.h"
#include "Serialization/AssetMipBlocks.h"
#include "Serialization/AssetSourceImage.h"
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

	static const TCHAR* const MeshPackage = TEXT("/Game/__AssetSerializationInspectorTests/BulkDataMesh/SM_BulkData");
	static const TCHAR* const MeshFolder = TEXT("/Game/__AssetSerializationInspectorTests/BulkDataMesh");

	/** A static mesh of one quad of 100 units, in which the corner at (100, 100) can be lifted. */
	struct FTestMesh
	{
		UPackage* Package = nullptr;
		UStaticMesh* Mesh = nullptr;

		/** What the corners and the polygon carry besides the positions; SetQuad writes them. */
		FVector3f Normal = FVector3f(0, 0, 1);
		float UVShift = 0.0f;
		FName SlotName = FName(TEXT("None"));
		FVector4f Color = FVector4f(1, 1, 1, 1);
		float BinormalSign = 1.0f;
		bool bHardEdges = false;
		FName ObjectName = FName(TEXT("Quad"));

		FTestMesh()
		{
			Package = CreatePackage(*AssetTestPackages::Unique(MeshPackage));
			Mesh = NewObject<UStaticMesh>(Package, TEXT("SM_BulkData"), RF_Public | RF_Standalone);
			Mesh->GetStaticMaterials().Add(FStaticMaterial());
			Mesh->SetNumSourceModels(1);
			SetQuad(0.0f);
		}

		void SetQuad(const float LiftedCorner)
		{
			FMeshDescription* Description = Mesh->GetMeshDescription(0) != nullptr ? Mesh->GetMeshDescription(0) : Mesh->CreateMeshDescription(0);
			if (Description == nullptr)
			{
				return;
			}
			Description->Empty();
			FStaticMeshAttributes Attributes(*Description);
			Attributes.Register();
			Attributes.RegisterPolygonObjectNameAttribute();

			const FVector3f Positions[4] = { FVector3f(0, 0, 0), FVector3f(100, 0, 0), FVector3f(100, 100, LiftedCorner), FVector3f(0, 100, 0) };
			TArray<FVertexInstanceID> Instances;
			for (const FVector3f& Position : Positions)
			{
				const FVertexID Vertex = Description->CreateVertex();
				Attributes.GetVertexPositions()[Vertex] = Position;
				const FVertexInstanceID Instance = Description->CreateVertexInstance(Vertex);
				Attributes.GetVertexInstanceNormals()[Instance] = Normal;
				Attributes.GetVertexInstanceColors()[Instance] = Color;
				Attributes.GetVertexInstanceBinormalSigns()[Instance] = BinormalSign;
				Attributes.GetVertexInstanceUVs().Set(Instance, 0, FVector2f(Position.X / 100.0f + UVShift, Position.Y / 100.0f));
				Instances.Add(Instance);
			}

			const FPolygonGroupID Group = Description->CreatePolygonGroup();
			Attributes.GetPolygonGroupMaterialSlotNames()[Group] = SlotName;
			const FPolygonID Polygon = Description->CreatePolygon(Group, Instances);
			Attributes.GetPolygonObjectNames()[Polygon] = ObjectName;
			for (const FEdgeID Edge : Description->Edges().GetElementIDs())
			{
				Attributes.GetEdgeHardnesses()[Edge] = bHardEdges;
			}
			Mesh->CommitMeshDescription(0);
			Mesh->Build(true);
		}

		FString Save(const TCHAR* CopyExtension) const
		{
			const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			SaveArgs.bSlowTask = false;
			if (!UPackage::SavePackage(Package, Mesh, *File, SaveArgs))
			{
				return FString();
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

namespace SourceImageTestUtils
{
	/** A 2 by 2 BGRA8 image of the given pixels (four bytes each, blue first). */
	static FAssetSourceImage MakeImage(const TArray<uint8>& Bytes)
	{
		FAssetSourceImage Image;
		Image.bLoaded = true;
		Image.Width = 2;
		Image.Height = 2;
		Image.NumSlices = 1;
		Image.Format = TEXT("TSF_BGRA8");
		Image.Pixels.Append(Bytes.GetData(), Bytes.Num());
		return Image;
	}
} // namespace SourceImageTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBulkDataExport_ReadsTheGeometryOfAMeshDescription, "AssetSerializationInspector.Serialization.AssetBulkDataExport.ReadsTheGeometryOfAMeshDescription",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBulkDataExport_ReadsTheGeometryOfAMeshDescription::RunTest(const FString& Parameters)
{
	using namespace BulkDataTestUtils;

	// A static mesh of one quad, saved twice: once flat, once with a corner lifted. The package keeps the mesh description in its trailer.
	IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(MeshFolder), false, true);
	{
		FTestMesh Test;
		const FString Flat = Test.Save(TEXT("flat.uasset"));
		Test.SetQuad(50.0f);
		const FString Lifted = Test.Save(TEXT("lifted.uasset"));

		// The same flat quad with a tilted normal, shifted UVs and another material slot.
		Test.Normal = FVector3f(0.6f, 0.0f, 0.8f);
		Test.UVShift = 0.25f;
		Test.SlotName = FName(TEXT("Metal"));
		Test.SetQuad(0.0f);
		const FString Reshaded = Test.Save(TEXT("reshaded.uasset"));

		// The same flat quad, painted red, with a flipped binormal, hard edges and another object name.
		Test.Normal = FVector3f(0, 0, 1);
		Test.UVShift = 0.0f;
		Test.SlotName = FName(TEXT("None"));
		Test.Color = FVector4f(1, 0, 0, 1);
		Test.BinormalSign = -1.0f;
		Test.bHardEdges = true;
		Test.ObjectName = FName(TEXT("Panel"));
		Test.SetQuad(0.0f);
		const FString Painted = Test.Save(TEXT("painted.uasset"));

		FText Error;
		const TSharedPtr<FAssetPackageDocument> FlatDocument = FAssetPackageReader::LoadFromFile(Flat, Error);
		const TSharedPtr<FAssetPackageDocument> LiftedDocument = FAssetPackageReader::LoadFromFile(Lifted, Error);
		const TSharedPtr<FAssetPackageDocument> ReshadedDocument = FAssetPackageReader::LoadFromFile(Reshaded, Error);
		const TSharedPtr<FAssetPackageDocument> PaintedDocument = FAssetPackageReader::LoadFromFile(Painted, Error);
		if (TestTrue(TEXT("The three versions are written and load"), FlatDocument.IsValid() && LiftedDocument.IsValid() && ReshadedDocument.IsValid() && PaintedDocument.IsValid()))
		{
			const TSharedPtr<FAssetPackageTraceCollection> FlatTraces = FAssetPackageFieldDecoder::Decode(*FlatDocument);
			const TSharedPtr<FAssetPackageTraceCollection> LiftedTraces = FAssetPackageFieldDecoder::Decode(*LiftedDocument);

			bool bRead = false;
			for (const FAssetPackageExportEntry& Export : FlatDocument->ExportMap)
			{
				FAssetBulkDataExport Data;
				if (!DecodeData(*FlatDocument, *FlatTraces, Export, Data) || Data.Kind != TEXT("Mesh description") || Data.Bulk.PayloadSize == 0)
				{
					// The mesh also has a slot for a high resolution description, which this one leaves empty.
					continue;
				}

				bRead = true;
				AddInfo(FString::Printf(TEXT("Mesh description record: %s, %lld bytes, hash %s"), *Data.Bulk.DescribeStorage(), Data.Bulk.PayloadSize, *Data.Bulk.ContentHash));
				const FAssetMeshGeometry Geometry = AssetMeshGeometry::Load(*FlatDocument, Data.Bulk);
				if (TestTrue(FString::Printf(TEXT("The geometry is loaded (%s)"), *Geometry.Error), Geometry.bLoaded))
				{
					TestEqual(TEXT("Four vertices"), Geometry.VertexCount, 4);
					TestEqual(TEXT("Two triangles"), Geometry.TriangleCount, 2);
					TestEqual(TEXT("One polygon"), Geometry.PolygonCount, 1);
					TestTrue(TEXT("In the bounds of the quad"), Geometry.Bounds.Min.Equals(FVector(0, 0, 0), 1e-3) && Geometry.Bounds.Max.Equals(FVector(100, 100, 0), 1e-3));
				}
			}
			TestTrue(TEXT("The package has a mesh description"), bRead);

			// The diff says that one vertex moved, and by how much.
			const FAssetPackageDiffResult Diff = AssetPackageDiff::Compare(*FlatDocument, *LiftedDocument, FlatTraces.Get(), LiftedTraces.Get());
			if (const FAssetPackageDiffEntry* Vertices = FindByKey(Diff, TEXT("BulkData/Vertices")))
			{
				TestTrue(FString::Printf(TEXT("One of the four vertices moved by 50 (%s)"), *Vertices->DisplayName.ToString()),
					Vertices->DisplayName.ToString().Contains(TEXT("1 of 4")) && Vertices->DisplayName.ToString().Contains(TEXT("50.0000")));
			}
			else
			{
				AddError(TEXT("The move of the vertex is not in the diff"));
			}

			// Another normal, UVs and material slot: each is named, and the vertices are not reported as moved.
			const TSharedPtr<FAssetPackageTraceCollection> ReshadedTraces = FAssetPackageFieldDecoder::Decode(*ReshadedDocument);
			const FAssetPackageDiffResult Reshading = AssetPackageDiff::Compare(*FlatDocument, *ReshadedDocument, FlatTraces.Get(), ReshadedTraces.Get());
			if (const FAssetPackageDiffEntry* Normals = FindByKey(Reshading, TEXT("BulkData/Normals")))
			{
				TestTrue(FString::Printf(TEXT("All four normals turned by about 36.87 degrees (%s)"), *Normals->DisplayName.ToString()),
					Normals->DisplayName.ToString().Contains(TEXT("4 of 4")) && Normals->DisplayName.ToString().Contains(TEXT("36.8")));
			}
			else
			{
				AddError(TEXT("The change of the normals is not in the diff"));
			}
			if (const FAssetPackageDiffEntry* Uvs = FindByKey(Reshading, TEXT("BulkData/UV/0")))
			{
				TestTrue(FString::Printf(TEXT("Four corners moved by 0.25 (%s)"), *Uvs->DisplayName.ToString()),
					Uvs->DisplayName.ToString().Contains(TEXT("4 of 4")) && Uvs->DisplayName.ToString().Contains(TEXT("0.25")));
			}
			else
			{
				AddError(TEXT("The change of the UVs is not in the diff"));
			}
			if (const FAssetPackageDiffEntry* Slots = FindByKey(Reshading, TEXT("BulkData/MaterialSlots")))
			{
				TestTrue(TEXT("From None to Metal"), Slots->OldValue.Contains(TEXT("None")) && Slots->NewValue.Contains(TEXT("Metal")));
			}
			else
			{
				AddError(TEXT("The change of the material slot is not in the diff"));
			}
			TestNull(TEXT("The vertices did not move"), FindByKey(Reshading, TEXT("BulkData/Vertices")));

			// Colors, binormals, edges and the names of the polygons.
			const TSharedPtr<FAssetPackageTraceCollection> PaintedTraces = FAssetPackageFieldDecoder::Decode(*PaintedDocument);
			const FAssetPackageDiffResult Painting = AssetPackageDiff::Compare(*FlatDocument, *PaintedDocument, FlatTraces.Get(), PaintedTraces.Get());
			if (const FAssetPackageDiffEntry* Colors = FindByKey(Painting, TEXT("BulkData/Colors")))
			{
				TestTrue(FString::Printf(TEXT("All four colors changed by the whole range (%s)"), *Colors->DisplayName.ToString()),
					Colors->DisplayName.ToString().Contains(TEXT("4 of 4")) && Colors->DisplayName.ToString().Contains(TEXT("1.000")));
			}
			else
			{
				AddError(TEXT("The change of the colors is not in the diff"));
			}
			TestNotNull(TEXT("The binormal signs"), FindByKey(Painting, TEXT("BulkData/BinormalSigns")));
			if (const FAssetPackageDiffEntry* Edges = FindByKey(Painting, TEXT("BulkData/EdgeHardness")))
			{
				TestTrue(FString::Printf(TEXT("From no hard edge to all five (the four sides and the diagonal) (%s: %s to %s)"), *Edges->DisplayName.ToString(), *Edges->OldValue, *Edges->NewValue),
					Edges->OldValue.Contains(TEXT("0 hard")) && Edges->DisplayName.ToString().Contains(TEXT("5 of 5")) && Edges->NewValue.Contains(TEXT("5 hard")));
			}
			else
			{
				AddError(TEXT("The change of the edge hardness is not in the diff"));
			}
			TestNotNull(TEXT("The polygon names"), FindByKey(Painting, TEXT("BulkData/PolygonNames")));
			TestNull(TEXT("The normals did not change"), FindByKey(Painting, TEXT("BulkData/Normals")));
		}
	}
	IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(MeshFolder), false, true);

	// A change of the geometry on synthetic meshes.
	const auto MakeGeometry = [](const TArray<FVector3f>& Positions, const int32 Triangles) {
		FAssetMeshGeometry Geometry;
		Geometry.bLoaded = true;
		Geometry.VertexCount = Positions.Num();
		Geometry.TriangleCount = Triangles;
		Geometry.PolygonCount = Triangles;
		Geometry.Positions = Positions;
		for (const FVector3f& Position : Positions)
		{
			Geometry.Bounds += FVector(Position);
		}
		return Geometry;
	};

	const TArray<FVector3f> Quad = { FVector3f(0, 0, 0), FVector3f(100, 0, 0), FVector3f(100, 100, 0), FVector3f(0, 100, 0) };
	{
		TArray<FAssetNativeDataChange> Changes;
		AssetMeshGeometry::AppendGeometryChange(MakeGeometry(Quad, 2), MakeGeometry(Quad, 2), Changes);
		TestTrue(TEXT("The same geometry is no change"), Changes.IsEmpty());
	}
	{
		TArray<FVector3f> Moved = Quad;
		Moved[2] = FVector3f(100, 100, 50);
		TArray<FAssetNativeDataChange> Changes;
		AssetMeshGeometry::AppendGeometryChange(MakeGeometry(Quad, 2), MakeGeometry(Moved, 2), Changes);
		const FAssetNativeDataChange* Vertices = Changes.FindByPredicate([](const FAssetNativeDataChange& Change) { return Change.Key == TEXT("BulkData/Vertices"); });
		if (TestNotNull(TEXT("A moved vertex is reported"), Vertices))
		{
			TestTrue(TEXT("One of four moved"), Vertices->Title.Contains(TEXT("1 of 4")));
			TestTrue(TEXT("By 50"), Vertices->Title.Contains(TEXT("50.0000")));
		}
		TestTrue(TEXT("The bounds changed with it"), Changes.ContainsByPredicate([](const FAssetNativeDataChange& Change) { return Change.Key == TEXT("BulkData/Bounds"); }));
	}
	{
		TArray<FVector3f> Bigger = Quad;
		Bigger.Add(FVector3f(50, 150, 0));
		TArray<FAssetNativeDataChange> Changes;
		AssetMeshGeometry::AppendGeometryChange(MakeGeometry(Quad, 2), MakeGeometry(Bigger, 3), Changes);
		const FAssetNativeDataChange* Counts = Changes.FindByPredicate([](const FAssetNativeDataChange& Change) { return Change.Key == TEXT("BulkData/Geometry"); });
		if (TestNotNull(TEXT("A change of the counts is reported"), Counts))
		{
			TestTrue(TEXT("From 4 vertices"), Counts->OldValue.Contains(TEXT("4 vertices")));
			TestTrue(TEXT("To 5"), Counts->NewValue.Contains(TEXT("5 vertices")));
		}
		TestFalse(
			TEXT("Vertices of another number are not compared one by one"), Changes.ContainsByPredicate([](const FAssetNativeDataChange& Change) { return Change.Key == TEXT("BulkData/Vertices"); }));
	}
	{
		FAssetMeshGeometry NotLoaded;
		NotLoaded.Error = TEXT("The mesh description is virtualized, outside the package");
		TArray<FAssetNativeDataChange> Changes;
		AssetMeshGeometry::AppendGeometryChange(NotLoaded, MakeGeometry(Quad, 2), Changes);
		if (TestEqual(TEXT("A note"), Changes.Num(), 1))
		{
			TestTrue(TEXT("That says why"), Changes[0].NewValue.Contains(TEXT("virtualized")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBulkDataExport_ComparesThePixelsOfTwoImages, "AssetSerializationInspector.Serialization.AssetBulkDataExport.ComparesThePixelsOfTwoImages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBulkDataExport_ComparesThePixelsOfTwoImages::RunTest(const FString& Parameters)
{
	using namespace SourceImageTestUtils;

	const TArray<uint8> Black = { 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255 };
	TArray<uint8> OneRed = Black;
	OneRed[2 * 4 + 2] = 255; // the red of the pixel at x 0, y 1

	// The same image: no change.
	{
		TArray<FAssetNativeDataChange> Changes;
		AssetSourceImage::AppendPixelChange(MakeImage(Black), MakeImage(Black), Changes);
		TestTrue(TEXT("The same pixels are no change"), Changes.IsEmpty());
	}

	// One pixel: counted, and placed.
	{
		TArray<FAssetNativeDataChange> Changes;
		AssetSourceImage::AppendPixelChange(MakeImage(Black), MakeImage(OneRed), Changes);
		if (TestEqual(TEXT("One change"), Changes.Num(), 1))
		{
			TestTrue(TEXT("It says 1 of the 4 pixels differs"), Changes[0].Title.Contains(TEXT("1 of 4")));
			TestTrue(TEXT("At x 0 to 0"), Changes[0].Title.Contains(TEXT("x 0 to 0")));
			TestTrue(TEXT("And y 1 to 1"), Changes[0].Title.Contains(TEXT("y 1 to 1")));
			TestTrue(TEXT("The largest change is the whole range of the channel"), Changes[0].Title.Contains(TEXT("1.000")));
			TestTrue(TEXT("The average red of the new image is a quarter"), Changes[0].NewValue.Contains(TEXT("0.250")));
			TestTrue(TEXT("The old one is black with full alpha"), Changes[0].OldValue.Contains(TEXT("(0.000, 0.000, 0.000, 1.000)")));
		}
	}

	// Another size or a pixel that could not be loaded is a note, not a guess.
	{
		FAssetSourceImage Wider = MakeImage(Black);
		Wider.Width = 4;
		Wider.Height = 1;
		TArray<FAssetNativeDataChange> Changes;
		AssetSourceImage::AppendPixelChange(MakeImage(Black), Wider, Changes);
		if (TestEqual(TEXT("One note"), Changes.Num(), 1))
		{
			TestTrue(TEXT("It says the size changed"), Changes[0].NewValue.Contains(TEXT("size")));
		}

		FAssetSourceImage NotLoaded;
		NotLoaded.Error = TEXT("The image is virtualized, outside the package");
		Changes.Reset();
		AssetSourceImage::AppendPixelChange(NotLoaded, MakeImage(Black), Changes);
		if (TestEqual(TEXT("One note for the image that is not there"), Changes.Num(), 1))
		{
			TestTrue(TEXT("It says why"), Changes[0].NewValue.Contains(TEXT("virtualized")));
		}
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

	// And what changed in the pixels: all 16 of the 4 by 4 image went from one grey to another.
	if (const FAssetPackageDiffEntry* Pixels = FindByKey(ToLighter, TEXT("BulkData/Pixels")))
	{
		TestTrue(TEXT("Every pixel differs"), Pixels->DisplayName.ToString().Contains(TEXT("16 of 16")));
		TestTrue(TEXT("With the average color of each"), Pixels->OldValue.Contains(TEXT("average color")) && Pixels->NewValue.Contains(TEXT("average color")));
		AddInfo(FString::Printf(TEXT("Pixels: %s | %s | %s"), *Pixels->DisplayName.ToString(), *Pixels->OldValue, *Pixels->NewValue));
	}
	else
	{
		AddError(TEXT("The change of the pixels is not in the diff"));
	}

	// The same content saved again: nothing of the texture changed, whatever the identifier of the save and the place of the data.
	const FAssetPackageDiffResult Same = AssetPackageDiff::Compare(*GreyDocument, *AgainDocument, GreyTraces.Get(), AgainTraces.Get());
	TestNull(TEXT("A resave of the same image reports no change of the source image"), FindByKey(Same, TEXT("BulkData/Source")));

	IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(TextureFolder), false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBulkDataExport_ComparesThePixelsOfAPngSourceImage, "AssetSerializationInspector.Serialization.AssetBulkDataExport.ComparesThePixelsOfAPngSourceImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBulkDataExport_ComparesThePixelsOfAPngSourceImage::RunTest(const FString& Parameters)
{
	using namespace BulkDataTestUtils;

	IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(TextureFolder), false, true);

	// The source image compressed as PNG, the way the editor keeps the art of an imported texture. A BGRA image is written to the PNG with its
	// red and blue swapped (a legacy of the engine that is undone when it is read), so the pixels given to the encoder are red first.
	FTestTexture Test;
	const auto SetPng = [&Test](const uint8 Blue, const uint8 Green, const uint8 Red) {
		TArray<uint8> Pixels;
		for (int32 Index = 0; Index < 16; ++Index)
		{
			Pixels.Append({ Red, Green, Blue, static_cast<uint8>(255) });
		}
		const FImageView View(Pixels.GetData(), 4, 4, ERawImageFormat::BGRA8);
		TArray64<uint8> Png;
		if (FImageUtils::CompressImage(Png, TEXT("png"), View))
		{
			Test.Texture->Source.InitWithCompressedSourceData(4, 4, 1, TSF_BGRA8, TArrayView64<uint8>(Png.GetData(), Png.Num()), TSCF_PNG);
		}
	};

	SetPng(0x10, 0x20, 0x30);
	if (!TestTrue(TEXT("The source is compressed as PNG"), Test.Texture->Source.GetSourceCompression() == TSCF_PNG))
	{
		return false;
	}
	const FString Dark = Test.Save(TEXT("dark.uasset"));

	SetPng(0x50, 0x60, 0x70);
	const FString Light = Test.Save(TEXT("light.uasset"));

	FText Error;
	const TSharedPtr<FAssetPackageDocument> GreyDocument = FAssetPackageReader::LoadFromFile(Dark, Error);
	const TSharedPtr<FAssetPackageDocument> LighterDocument = FAssetPackageReader::LoadFromFile(Light, Error);
	if (!TestTrue(TEXT("The two versions are written and load"), GreyDocument.IsValid() && LighterDocument.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FAssetPackageTraceCollection> GreyTraces = FAssetPackageFieldDecoder::Decode(*GreyDocument);
	const TSharedPtr<FAssetPackageTraceCollection> LighterTraces = FAssetPackageFieldDecoder::Decode(*LighterDocument);
	const FAssetPackageDiffResult Diff = AssetPackageDiff::Compare(*GreyDocument, *LighterDocument, GreyTraces.Get(), LighterTraces.Get());
	if (const FAssetPackageDiffEntry* Pixels = FindByKey(Diff, TEXT("BulkData/Pixels")))
	{
		const FString Title = Pixels->DisplayName.ToString();
		AddInfo(FString::Printf(TEXT("Pixels: %s | %s | %s"), *Title, *Pixels->OldValue, *Pixels->NewValue));
		TestTrue(TEXT("Every pixel differs"), Title.Contains(TEXT("16 of 16")));
		TestTrue(TEXT("The first went from (0.188, 0.125, 0.063) with red and blue as the source has them"), Pixels->OldValue.Contains(TEXT("(0.188, 0.125, 0.063, 1.000)")));
		TestTrue(TEXT("To (0.439, 0.376, 0.314)"), Pixels->NewValue.Contains(TEXT("(0.439, 0.376, 0.314, 1.000)")));
	}
	else
	{
		AddError(TEXT("The change of the pixels is not in the diff"));
	}

	IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(TextureFolder), false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBulkDataExport_DecodesTheBlocksOfTheBcFormats, "AssetSerializationInspector.Serialization.AssetBulkDataExport.DecodesTheBlocksOfTheBcFormats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBulkDataExport_DecodesTheBlocksOfTheBcFormats::RunTest(const FString& Parameters)
{
	const auto Near = [](const FVector4f& A, const FVector4f& B) { return A.Equals(B, 1e-3f); };

	// BC1: red and blue as the end colors; the indices pick the end colors and the two between them.
	{
		uint8 Block[8] = { 0x00, 0xF8, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00 };
		FVector4f Pixels[16];
		AssetBlockDecoder::DecodeBlock(EAssetBlockCodec::BC1, Block, Pixels);
		TestTrue(TEXT("Index 0 is the first color"), Near(Pixels[0], FVector4f(1, 0, 0, 1)));

		Block[4] = 0b11100100; // the first four pixels use the indices 0, 1, 2 and 3
		AssetBlockDecoder::DecodeBlock(EAssetBlockCodec::BC1, Block, Pixels);
		TestTrue(TEXT("Index 1 is the second color"), Near(Pixels[1], FVector4f(0, 0, 1, 1)));
		TestTrue(TEXT("Index 2 is two thirds of the first and a third of the second"), Near(Pixels[2], FVector4f(2.0f / 3.0f, 0, 1.0f / 3.0f, 1)));
		TestTrue(TEXT("Index 3 is a third and two thirds"), Near(Pixels[3], FVector4f(1.0f / 3.0f, 0, 2.0f / 3.0f, 1)));

		// The first color not above the second: three colors, and the last is transparent.
		uint8 ThreeColors[8] = { 0x1F, 0x00, 0x00, 0xF8, 0b11100100, 0x00, 0x00, 0x00 };
		AssetBlockDecoder::DecodeBlock(EAssetBlockCodec::BC1, ThreeColors, Pixels);
		TestTrue(TEXT("Index 2 is the middle of the two"), Near(Pixels[2], FVector4f(0.5f, 0, 0.5f, 1)));
		TestTrue(TEXT("Index 3 is transparent black"), Near(Pixels[3], FVector4f(0, 0, 0, 0)));
	}

	// BC4 (and BC3 alpha, BC5 channels): 255 and 0 as the ends.
	{
		uint8 Block[8] = { 255, 0, 0b00001000, 0b00000000, 0, 0, 0, 0 }; // the indices 0, 1 and then 0s
		FVector4f Pixels[16];
		AssetBlockDecoder::DecodeBlock(EAssetBlockCodec::BC4, Block, Pixels);
		TestTrue(TEXT("Index 0 is the first end"), Near(Pixels[0], FVector4f(1, 1, 1, 1)));
		TestTrue(TEXT("Index 1 is the second end"), Near(Pixels[1], FVector4f(0, 0, 0, 1)));

		Block[2] = 0b00010010; // 2 then 2 then 0: the indices are 3 bits each, from the lowest
		AssetBlockDecoder::DecodeBlock(EAssetBlockCodec::BC4, Block, Pixels);
		TestTrue(TEXT("Index 2 is six sevenths of the way to the first end"), Near(Pixels[0], FVector4f(6.0f / 7.0f, 6.0f / 7.0f, 6.0f / 7.0f, 1)));

		// With the ends the other way round there are six steps and the values 0 and 1.
		uint8 Six[8] = { 0, 255, 0b11110110, 0b00000001, 0, 0, 0, 0 }; // the indices 6, 6, 7, 0
		AssetBlockDecoder::DecodeBlock(EAssetBlockCodec::BC4, Six, Pixels);
		TestTrue(TEXT("Index 6 is 0 and 7 is 1"), Near(Pixels[0], FVector4f(0, 0, 0, 1)) && Near(Pixels[2], FVector4f(1, 1, 1, 1)));

		// BC5: the first block is red and the second is green.
		uint8 Two[16] = { 255, 0, 0, 0, 0, 0, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0 };
		AssetBlockDecoder::DecodeBlock(EAssetBlockCodec::BC5, Two, Pixels);
		TestTrue(TEXT("Red from the first block, green from the second"), Near(Pixels[5], FVector4f(1, 1, 0, 1)));
	}

	// BC3: the alpha from a block of its own, then the colors.
	{
		uint8 Block[16] = { 0, 255, 0, 0, 0, 0, 0, 0, 0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0 };
		FVector4f Pixels[16];
		AssetBlockDecoder::DecodeBlock(EAssetBlockCodec::BC3, Block, Pixels);
		TestTrue(TEXT("Red with the alpha of the first end, which is 0"), Near(Pixels[0], FVector4f(1, 0, 0, 0)));
	}

	// The comparison: the colors of a mip of red where one block turned blue.
	{
		FAssetBlockFormat Dxt1;
		AssetMipBlocks::FindBlockFormat(TEXT("PF_DXT1"), Dxt1);
		TestTrue(TEXT("DXT1 is decoded"), Dxt1.Codec == EAssetBlockCodec::BC1);

		const uint8 RedBlock[8] = { 0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0 };
		const uint8 BlueBlock[8] = { 0x1F, 0x00, 0x1F, 0x00, 0, 0, 0, 0 };
		TArray<uint8> Old;
		for (int32 Block = 0; Block < 16; ++Block)
		{
			Old.Append(RedBlock, 8);
		}
		TArray<uint8> New = Old;
		FMemory::Memcpy(New.GetData() + 5 * 8, BlueBlock, 8);

		const FAssetMipBlockDiff Diff = AssetMipBlocks::CompareBytes(Old.GetData(), New.GetData(), Old.Num(), 16, 16, Dxt1);
		if (TestTrue(TEXT("The colors are compared"), Diff.bComparable && Diff.bColors && Diff.DifferingBlocks == 1))
		{
			TestEqual(TEXT("The largest change is the whole range"), Diff.LargestChange, 1.0);
			TestTrue(TEXT("The average of the old mip is red"), Diff.OldAverage.Equals(FVector4d(1, 0, 0, 1), 1e-3));
			TestTrue(TEXT("The new one has a sixteenth of blue"), Diff.NewAverage.Equals(FVector4d(15.0 / 16.0, 0, 1.0 / 16.0, 1), 1e-3));
		}

		// A format without a codec is still compared by blocks.
		FAssetBlockFormat Bc7;
		AssetMipBlocks::FindBlockFormat(TEXT("PF_BC6H"), Bc7);
		TArray<uint8> Seven;
		Seven.Init(1, 16);
		TArray<uint8> Changed = Seven;
		Changed[3] = 2;
		const FAssetMipBlockDiff SevenDiff = AssetMipBlocks::CompareBytes(Seven.GetData(), Changed.GetData(), 16, 4, 4, Bc7);
		TestTrue(TEXT("BC6H is compared by blocks, without colors"), SevenDiff.bComparable && SevenDiff.DifferingBlocks == 1 && !SevenDiff.bColors);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBulkDataExport_DecodesBc7AndTheOtherFormatsAgainstTheEngineEncoder,
	"AssetSerializationInspector.Serialization.AssetBulkDataExport.DecodesBc7AndTheOtherFormatsAgainstTheEngineEncoder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBulkDataExport_DecodesBc7AndTheOtherFormatsAgainstTheEngineEncoder::RunTest(const FString& Parameters)
{
	// A BC7 block of mode 6 packed by hand: two endpoints of 7 bits a channel and a P-bit each, and 4 bit indices (3 bits for the anchor pixel 0).
	{
		uint8 Block[16] = {};
		int32 Position = 0;
		const auto Put = [&Block, &Position](const uint32 Value, const int32 Count) {
			for (int32 Bit = 0; Bit < Count; ++Bit, ++Position)
			{
				Block[Position >> 3] |= ((Value >> Bit) & 1) << (Position & 7);
			}
		};
		Put(1 << 6, 7); // mode 6: six zeros and a one
		Put(127, 7);	// red of the first endpoint
		Put(0, 7);		// red of the second
		Put(0, 7);		// green of the first
		Put(127, 7);	// green of the second
		Put(0, 14);		// blue of both
		Put(127, 7);	// alpha of the first
		Put(127, 7);	// alpha of the second
		Put(1, 1);		// P-bit of the first endpoint
		Put(0, 1);		// P-bit of the second
		Put(0, 3);		// pixel 0, the anchor
		Put(15, 4);		// pixel 1
		Put(0, 56);		// the other fourteen

		FVector4f Pixels[16];
		AssetBlockDecoder::DecodeBlock(EAssetBlockCodec::BC7, Block, Pixels);
		TestTrue(TEXT("Index 0 is the first endpoint, with its P-bit in every channel"), Pixels[0].Equals(FVector4f(255.0f, 1.0f, 1.0f, 255.0f) / 255.0f, 1e-4f));
		TestTrue(TEXT("Index 15 is the second endpoint"), Pixels[1].Equals(FVector4f(0.0f, 254.0f, 0.0f, 254.0f) / 255.0f, 1e-4f));

		uint8 Invalid[16] = {};
		AssetBlockDecoder::DecodeBlock(EAssetBlockCodec::BC7, Invalid, Pixels);
		TestTrue(TEXT("A block with no mode is transparent black"), Pixels[7].Equals(FVector4f(0, 0, 0, 0)));
	}

	// The real thing: textures compressed by the engine's own encoder from a smooth image, decoded and compared with the image. The engine
	// only builds the data of a texture when it can render, which a run without a renderer cannot.
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("The engine does not build textures without a renderer, so the decoders are not compared with its encoder in this run."));
		return true;
	}

	struct FCase
	{
		const TCHAR* Name;
		TextureCompressionSettings Compression;
		bool bAlpha;
		const TCHAR* ExpectedFormat;
		double Tolerance;

		/** 0: gradients. 1: two colors in diagonal stripes, 2: three colors, 4: a ramp of sixteen steps in the four channels, 5: two halves of every block that each have a ramp. The last ones have
		 * blocks that BC7 stores with its subsets or its larger modes. */
		int32 Pattern = 0;
	};
	const FCase Cases[] = {
		{ TEXT("BC7 with alpha"), TC_BC7, true, TEXT("PF_BC7"), 0.02 },
		{ TEXT("BC7 opaque"), TC_BC7, false, TEXT("PF_BC7"), 0.02 },
		{ TEXT("DXT1"), TC_Default, false, TEXT("PF_DXT1"), 0.06 },
		{ TEXT("DXT5"), TC_Default, true, TEXT("PF_DXT5"), 0.06 },
		{ TEXT("BC7 stripes"), TC_BC7, false, TEXT("PF_BC7"), 0.03, 1 },
		{ TEXT("BC7 stripes with alpha"), TC_BC7, true, TEXT("PF_BC7"), 0.03, 1 },
		{ TEXT("BC7 three colors"), TC_BC7, false, TEXT("PF_BC7"), 0.03, 2 },
		{ TEXT("BC7 ramp in four channels"), TC_BC7, true, TEXT("PF_BC7"), 0.02, 4 },
		{ TEXT("BC7 two regions with ramps"), TC_BC7, true, TEXT("PF_BC7"), 0.03, 5 },
		{ TEXT("BC7 two regions with ramps, opaque"), TC_BC7, false, TEXT("PF_BC7"), 0.03, 5 },
	};

	constexpr int32 Size = 64;
	for (const FCase& Case : Cases)
	{
		TArray<uint8> Source;
		Source.SetNumUninitialized(Size * Size * 4);
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				uint8* Pixel = Source.GetData() + (Y * Size + X) * 4; // BGRA
				Pixel[2] = static_cast<uint8>(X * 4);
				Pixel[1] = static_cast<uint8>(Y * 4);
				Pixel[0] = static_cast<uint8>((X + Y) * 2);
				Pixel[3] = Case.bAlpha ? static_cast<uint8>(255 - X * 3) : 255;
				if (Case.Pattern == 1)
				{
					const bool bFirst = ((X * 3 + Y * 2) / 5) % 2 == 0;
					Pixel[2] = bFirst ? 255 : 20;
					Pixel[1] = bFirst ? 40 : 200;
					Pixel[0] = bFirst ? 20 : 90;
					Pixel[3] = Case.bAlpha ? (bFirst ? 255 : 100) : 255;
				}
				else if (Case.Pattern == 2)
				{
					static const uint8 Colors[3][4] = { { 255, 40, 20, 255 }, { 20, 200, 90, 160 }, { 30, 60, 240, 60 } };
					const uint8* Color = Colors[((X * 3 + Y * 2) / 3) % 3];
					Pixel[2] = Color[0];
					Pixel[1] = Color[1];
					Pixel[0] = Color[2];
					Pixel[3] = Case.bAlpha ? Color[3] : 255;
				}
				else if (Case.Pattern == 4)
				{
					const int32 Step = ((X & 3) + 4 * (Y & 3)) * 16;
					Pixel[2] = static_cast<uint8>(Step);
					Pixel[1] = static_cast<uint8>(255 - Step);
					Pixel[0] = static_cast<uint8>(Step / 2);
					Pixel[3] = Case.bAlpha ? static_cast<uint8>(255 - Step / 2) : 255;
				}
				else if (Case.Pattern == 5)
				{
					// The left and the right half of every block each have a ramp of four steps down the block.
					const bool bFirst = (X & 3) < 2;
					const int32 T = (Y & 3) * 85;
					Pixel[2] = static_cast<uint8>(bFirst ? T : 255 - T);
					Pixel[1] = static_cast<uint8>(bFirst ? 255 - T : 40 + T / 3);
					Pixel[0] = static_cast<uint8>(bFirst ? 30 : 200 - T / 2);
					Pixel[3] = Case.bAlpha ? static_cast<uint8>(bFirst ? 255 - T / 2 : 50 + T / 2) : 255;
				}
			}
		}

		UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Texture->Source.Init(Size, Size, 1, 1, TSF_BGRA8, Source.GetData());
		Texture->CompressionSettings = Case.Compression;
		Texture->MipGenSettings = TMGS_NoMipmaps;
		Texture->SRGB = false;
		Texture->PostEditChange();
		Texture->FinishCachePlatformData();

		FTexturePlatformData* Data = Texture->GetPlatformData();
		if (Data == nullptr || Data->Mips.IsEmpty())
		{
			AddInfo(FString::Printf(TEXT("%s: platform data %s"), Case.Name, Data != nullptr ? TEXT("exists but has no mips") : TEXT("is null")));
		}
		if (!TestTrue(FString::Printf(TEXT("%s: the engine builds the texture"), Case.Name), Data != nullptr && !Data->Mips.IsEmpty()))
		{
			continue;
		}

		const FString Format = GetPixelFormatString(Data->PixelFormat);
		if (!TestEqual(FString::Printf(TEXT("%s: the format"), Case.Name), Format, FString(Case.ExpectedFormat)))
		{
			continue;
		}

		FAssetBlockFormat BlockFormat;
		if (!TestTrue(TEXT("The format has a codec"), AssetMipBlocks::FindBlockFormat(Format, BlockFormat) && BlockFormat.Codec != EAssetBlockCodec::None))
		{
			continue;
		}

		Data->TryInlineMipData(0, TEXT("AssetSerializationInspector test"));
		FByteBulkData& Bulk = Data->Mips[0].BulkData;
		const uint8* Bytes = static_cast<const uint8*>(Bulk.LockReadOnly());
		if (!TestTrue(FString::Printf(TEXT("%s: the pixels are there and are the blocks of the format"), Case.Name),
				Bytes != nullptr && Bulk.GetBulkDataSize() == (Size / 4) * (Size / 4) * BlockFormat.BytesPerBlock))
		{
			Bulk.Unlock();
			continue;
		}

		double Total = 0.0;
		double Largest = 0.0;
		int32 ModeCounts[9] = {};
		for (int32 BlockIndex = 0; BlockIndex < (Size / 4) * (Size / 4); ++BlockIndex)
		{
			FVector4f Pixels[16];
			AssetBlockDecoder::DecodeBlock(BlockFormat.Codec, Bytes + BlockIndex * BlockFormat.BytesPerBlock, Pixels);
			const uint8 First = Bytes[BlockIndex * BlockFormat.BytesPerBlock];
			++ModeCounts[First == 0 ? 8 : FMath::CountTrailingZeros(static_cast<uint32>(First))];
			for (int32 Pixel = 0; Pixel < 16; ++Pixel)
			{
				const int32 X = (BlockIndex % (Size / 4)) * 4 + (Pixel & 3);
				const int32 Y = (BlockIndex / (Size / 4)) * 4 + (Pixel >> 2);
				const uint8* Expected = Source.GetData() + (Y * Size + X) * 4;
				const FVector4f Want(Expected[2] / 255.0f, Expected[1] / 255.0f, Expected[0] / 255.0f, Expected[3] / 255.0f);
				const FVector4f Delta = Pixels[Pixel] - Want;
				const double Error = FMath::Max(FMath::Max(FMath::Abs(Delta.X), FMath::Abs(Delta.Y)), FMath::Max(FMath::Abs(Delta.Z), FMath::Abs(Delta.W)));
				Total += Error;
				Largest = FMath::Max(Largest, Error);
			}
		}
		Bulk.Unlock();

		const double Mean = Total / (Size * Size);
		AddInfo(FString::Printf(TEXT("%s: mean error %.4f, largest %.4f"), Case.Name, Mean, Largest));
		if (BlockFormat.Codec == EAssetBlockCodec::BC7)
		{
			AddInfo(FString::Printf(TEXT("%s: blocks by BC7 mode 0 to 7: %d %d %d %d %d %d %d %d"), Case.Name, ModeCounts[0], ModeCounts[1], ModeCounts[2], ModeCounts[3], ModeCounts[4], ModeCounts[5],
				ModeCounts[6], ModeCounts[7]));
		}
		TestTrue(FString::Printf(TEXT("%s: the decoded colors are those of the image (mean error %.4f)"), Case.Name, Mean), Mean < Case.Tolerance);
	}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBulkDataExport_ReadsTheFlagsOfALightmapTexture, "AssetSerializationInspector.Serialization.AssetBulkDataExport.ReadsTheFlagsOfALightmapTexture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBulkDataExport_ReadsTheFlagsOfALightmapTexture::RunTest(const FString& Parameters)
{
	using namespace BulkDataTestUtils;

	// The built data of a map holds lightmap textures, which write their flags after the data of a texture.
	const FString File = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Experimental"), TEXT("ImpostorBaker"), TEXT("Content"), TEXT("Maps"), TEXT("Generate_Impostor_Map_BuiltData.uasset"));
	if (!IFileManager::Get().FileExists(*File))
	{
		AddInfo(TEXT("The engine does not have the impostor baker map, so there is nothing to read."));
	}
	else
	{
		FText Error;
		const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(File, Error);
		if (TestTrue(TEXT("It loads"), Document.IsValid()))
		{
			const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);
			int32 Lightmaps = 0;
			for (const FAssetPackageExportEntry& Export : Document->ExportMap)
			{
				FAssetBulkDataExport Data;
				if (DecodeData(*Document, *Traces, Export, Data) && Data.bHasLightmapFlags)
				{
					++Lightmaps;
					TestTrue(FString::Printf(TEXT("%s is read to its last byte (%s)"), *Document->ResolveExportPath(Export.Index), *Data.Error), Data.bComplete);
				}
			}
			TestTrue(TEXT("The package has lightmap textures"), Lightmaps > 0);
		}
	}

	FAssetBulkDataExport Old;
	Old.Kind = TEXT("Texture");
	Old.bComplete = true;
	Old.bHasLightmapFlags = true;
	Old.LightmapFlags = 0x1;
	FAssetBulkDataExport New = Old;
	TestTrue(TEXT("The same flags are no change"), AssetBulkDataExport::Compare(Old, New).IsEmpty());

	New.LightmapFlags = 0x3;
	const TArray<FAssetNativeDataChange> Changes = AssetBulkDataExport::Compare(Old, New);
	if (TestEqual(TEXT("Other flags are one change"), Changes.Num(), 1))
	{
		TestEqual(TEXT("Named by its key"), Changes[0].Key, FString(TEXT("BulkData/LightmapFlags")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBulkDataExport_ReadsTheTilesOfAVirtualTexture, "AssetSerializationInspector.Serialization.AssetBulkDataExport.ReadsTheTilesOfAVirtualTexture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBulkDataExport_ReadsTheTilesOfAVirtualTexture::RunTest(const FString& Parameters)
{
	using namespace CookedTextureTestUtils;

	FAssetBulkDataExport Data;
	if (ReadFixture(*this, TEXT("BaseFlattenDiffuseMap_VT.uasset"), Data))
	{
		if (TestTrue(FString::Printf(TEXT("It is read to its last byte (%s)"), *Data.Error), Data.bComplete) && TestEqual(TEXT("It has the data of one platform"), Data.PlatformData.Num(), 1))
		{
			const FAssetTexturePlatformData& Platform = Data.PlatformData[0];
			AddInfo(Data.Summarize());
			TestTrue(TEXT("It is virtual"), Platform.bVirtual);
			TestTrue(TEXT("With no mips of the usual kind"), Platform.Mips.IsEmpty());
			TestTrue(TEXT("A layer"), Platform.Virtual.NumLayers > 0 && Platform.Virtual.LayerFormats.Num() == static_cast<int32>(Platform.Virtual.NumLayers));
			TestTrue(TEXT("Tiles"), Platform.Virtual.TileSize > 0);
			TestTrue(TEXT("A size"), Platform.Virtual.Width > 0 && Platform.Virtual.Height > 0);
			if (TestFalse(TEXT("And chunks"), Platform.Virtual.Chunks.IsEmpty()))
			{
				for (const FAssetVirtualTextureChunk& Chunk : Platform.Virtual.Chunks)
				{
					TestEqual(TEXT("The hash of a chunk is 20 bytes"), Chunk.ContentHash.Len(), 40);
					TestTrue(TEXT("A chunk has a size"), Chunk.SizeInBytes > 0);
				}
			}
			TestTrue(TEXT("The same texture is no change"), AssetBulkDataExport::Compare(Data, Data).IsEmpty());
		}
	}

	// A change of a chunk, the layers and the tile size.
	const auto MakeVirtual = [](const TCHAR* Hash) {
		FAssetBulkDataExport Texture = MakeTexture({});
		FAssetTexturePlatformData& Platform = Texture.PlatformData[0];
		Platform.bVirtual = true;
		Platform.Virtual.NumLayers = 1;
		Platform.Virtual.LayerFormats = { TEXT("PF_DXT1") };
		Platform.Virtual.TileSize = 128;
		Platform.Virtual.TileBorderSize = 4;
		Platform.Virtual.Width = 256;
		Platform.Virtual.Height = 256;
		Platform.Virtual.NumMips = 3;
		FAssetVirtualTextureChunk& Chunk = Platform.Virtual.Chunks.AddDefaulted_GetRef();
		Chunk.ContentHash = Hash;
		Chunk.SizeInBytes = 1000;
		return Texture;
	};

	const FAssetBulkDataExport Base = MakeVirtual(TEXT("aaaa"));
	FAssetBulkDataExport Changed = MakeVirtual(TEXT("bbbb"));
	Changed.PlatformData[0].Virtual.LayerFormats = { TEXT("PF_BC7") };
	Changed.PlatformData[0].Virtual.TileSize = 64;

	const TArray<FAssetNativeDataChange> Changes = AssetBulkDataExport::Compare(Base, Changed);
	TestNotNull(TEXT("The chunk"), Find(Changes, TEXT("Platform/0/VirtualChunk/0")));
	TestNotNull(TEXT("The layers"), Find(Changes, TEXT("Platform/0/VirtualLayers")));
	TestNotNull(TEXT("The tile size"), Find(Changes, TEXT("Platform/0/VirtualTiles")));
	TestTrue(TEXT("The same virtual texture is no change"), AssetBulkDataExport::Compare(Base, MakeVirtual(TEXT("aaaa"))).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBulkDataExport_ComparesTheBlocksOfACookedMip, "AssetSerializationInspector.Serialization.AssetBulkDataExport.ComparesTheBlocksOfACookedMip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBulkDataExport_ComparesTheBlocksOfACookedMip::RunTest(const FString& Parameters)
{
	// The layouts of the formats.
	FAssetBlockFormat Dxt1;
	FAssetBlockFormat Bc7;
	FAssetBlockFormat Astc;
	FAssetBlockFormat Bgra;
	TestTrue(TEXT("DXT1"), AssetMipBlocks::FindBlockFormat(TEXT("PF_DXT1"), Dxt1) && Dxt1.BlockWidth == 4 && Dxt1.BytesPerBlock == 8);
	TestTrue(TEXT("BC7"), AssetMipBlocks::FindBlockFormat(TEXT("PF_BC7"), Bc7) && Bc7.BytesPerBlock == 16);
	TestTrue(TEXT("ASTC 6x6"), AssetMipBlocks::FindBlockFormat(TEXT("PF_ASTC_6x6"), Astc) && Astc.BlockWidth == 6 && Astc.BlockHeight == 6 && Astc.BytesPerBlock == 16);
	TestTrue(TEXT("BGRA8"), AssetMipBlocks::FindBlockFormat(TEXT("PF_B8G8R8A8"), Bgra) && Bgra.BlockWidth == 1 && Bgra.BytesPerBlock == 4);
	FAssetBlockFormat Unknown;
	TestFalse(TEXT("A format that is not known"), AssetMipBlocks::FindBlockFormat(TEXT("PF_Made_Up"), Unknown));

	// 16 by 16 pixels of DXT1: 4 by 4 blocks of 8 bytes. Two blocks change: block (2, 1) and block (3, 3).
	TArray<uint8> Old;
	Old.Init(0x11, 16 * 8);
	TArray<uint8> New = Old;
	New[(1 * 4 + 2) * 8 + 3] = 0x99;
	New[(3 * 4 + 3) * 8] = 0x99;
	{
		const FAssetMipBlockDiff Diff = AssetMipBlocks::CompareBytes(Old.GetData(), New.GetData(), Old.Num(), 16, 16, Dxt1);
		if (TestTrue(TEXT("They can be compared"), Diff.bComparable))
		{
			TestEqual(TEXT("Two blocks differ"), Diff.DifferingBlocks, static_cast<int64>(2));
			TestEqual(TEXT("Of sixteen"), Diff.TotalBlocks, static_cast<int64>(16));
			TestEqual(TEXT("The box starts at x 8"), Diff.MinX, 8);
			TestEqual(TEXT("And ends at x 15"), Diff.MaxX, 15);
			TestEqual(TEXT("It starts at y 4"), Diff.MinY, 4);
			TestEqual(TEXT("And ends at y 15"), Diff.MaxY, 15);
		}
	}
	{
		const FAssetMipBlockDiff Same = AssetMipBlocks::CompareBytes(Old.GetData(), Old.GetData(), Old.Num(), 16, 16, Dxt1);
		TestTrue(TEXT("The same mip has no block that differs"), Same.bComparable && Same.DifferingBlocks == 0);
	}

	// A mip whose size is not a number of blocks (a layout this does not know) is not compared.
	TestFalse(TEXT("A size that is not the blocks of the format"), AssetMipBlocks::CompareBytes(Old.GetData(), New.GetData(), Old.Num() - 1, 16, 16, Dxt1).bComparable);

	// A mip whose size is not a multiple of the block: the blocks at the edge cover less, and the box is cut at the image.
	{
		TArray<uint8> Small;
		Small.Init(0, 3 * 2 * 8); // 10 by 6 pixels: 3 by 2 blocks
		TArray<uint8> Changed = Small;
		Changed[(1 * 3 + 2) * 8] = 1; // the block at the right edge of the second row
		const FAssetMipBlockDiff Diff = AssetMipBlocks::CompareBytes(Small.GetData(), Changed.GetData(), Small.Num(), 10, 6, Dxt1);
		TestTrue(TEXT("It is compared"), Diff.bComparable && Diff.DifferingBlocks == 1);
		TestEqual(TEXT("The box ends at the edge of the image in x"), Diff.MaxX, 9);
		TestEqual(TEXT("And in y"), Diff.MaxY, 5);
	}

	// Uncompressed pixels are blocks of one pixel.
	{
		TArray<uint8> Pixels;
		Pixels.Init(0, 4 * 3 * 4);
		TArray<uint8> Changed = Pixels;
		Changed[(2 * 4 + 1) * 4 + 1] = 7; // the pixel at x 1, y 2
		const FAssetMipBlockDiff Diff = AssetMipBlocks::CompareBytes(Pixels.GetData(), Changed.GetData(), Pixels.Num(), 4, 3, Bgra);
		TestTrue(TEXT("One pixel"), Diff.bComparable && Diff.DifferingBlocks == 1 && Diff.MinX == 1 && Diff.MaxX == 1 && Diff.MinY == 2 && Diff.MaxY == 2);
	}

	// The real thing: a BC5 texture cooked by the engine, with some mips streamed from its sidecar file. Each mip is the blocks of its format.
	FAssetBulkDataExport Texture;
	if (CookedTextureTestUtils::ReadFixture(*this, TEXT("T_Default_Material_Grid_N.uasset"), Texture) && Texture.bComplete && !Texture.PlatformData.IsEmpty())
	{
		const FAssetTexturePlatformData& Platform = Texture.PlatformData[0];
		FAssetBlockFormat Bc5;
		if (TestTrue(TEXT("Its format is known"), AssetMipBlocks::FindBlockFormat(Platform.PixelFormat, Bc5)))
		{
			const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
			FText Error;
			const TSharedPtr<FAssetPackageDocument> Document =
				FAssetPackageReader::LoadFromFile(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("CookedTestFixtures"), TEXT("T_Default_Material_Grid_N.uasset")), Error);
			if (TestTrue(TEXT("The package loads"), Document.IsValid()))
			{
				int32 Streamed = 0;
				for (int32 Index = 0; Index < Platform.Mips.Num(); ++Index)
				{
					const FAssetTextureMip& Mip = Platform.Mips[Index];
					Streamed += Mip.IsInline() ? 0 : 1;

					TArray64<uint8> Bytes;
					if (!TestTrue(FString::Printf(TEXT("Mip %d can be read"), Index), AssetCookedBulkData::LoadBytes(*Document, Mip.BulkFlags, Mip.DataOffset, Mip.StoredSize, Bytes)))
					{
						continue;
					}

					TestEqual(TEXT("It has as many bytes as the mip says"), static_cast<int64>(Bytes.Num()), Mip.PayloadSize);
					const FAssetMipBlockDiff Same = AssetMipBlocks::CompareBytes(Bytes.GetData(), Bytes.GetData(), Bytes.Num(), Mip.SizeX, Mip.SizeY, Bc5);
					TestTrue(FString::Printf(TEXT("Mip %d is the blocks of its format"), Index), Same.bComparable && Same.DifferingBlocks == 0);

					// One block changed in the middle of the mip.
					TArray64<uint8> Changed = Bytes;
					const int64 BlocksX = (Mip.SizeX + 3) / 4;
					const int64 BlockY = ((Mip.SizeY + 3) / 4) / 2;
					const int64 BlockX = BlocksX / 2;
					Changed[(BlockY * BlocksX + BlockX) * Bc5.BytesPerBlock] ^= 0xFF;
					const FAssetMipBlockDiff One = AssetMipBlocks::CompareBytes(Bytes.GetData(), Changed.GetData(), Bytes.Num(), Mip.SizeX, Mip.SizeY, Bc5);
					TestTrue(TEXT("One block differs"), One.DifferingBlocks == 1);
					TestEqual(TEXT("At the place of the block in x"), One.MinX, static_cast<int32>(BlockX * 4));
					TestEqual(TEXT("And in y"), One.MinY, static_cast<int32>(BlockY * 4));
				}
				TestTrue(TEXT("Some of the mips stream from the sidecar file"), Streamed > 0);
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBulkDataExport_ShowsTheBlocksThatChangedInACookedTexture,
	"AssetSerializationInspector.Serialization.AssetBulkDataExport.ShowsTheBlocksThatChangedInACookedTexture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBulkDataExport_ShowsTheBlocksThatChangedInACookedTexture::RunTest(const FString& Parameters)
{
	using namespace BulkDataTestUtils;

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
	if (!TestTrue(TEXT("The plugin is found"), Plugin.IsValid()))
	{
		return false;
	}

	// A copy of the cooked texture next to the original, in which one block of an inline mip is changed.
	const FString Fixtures = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("CookedTestFixtures"));
	const FString Folder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("__AssetSerializationInspectorTests"), TEXT("CookedBlocks"));
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	IFileManager::Get().MakeDirectory(*Folder, true);
	for (const TCHAR* Extension : { TEXT("uasset"), TEXT("uexp"), TEXT("ubulk") })
	{
		IFileManager::Get().Copy(
			*FPaths::Combine(Folder, FString::Printf(TEXT("T_Changed.%s"), Extension)), *FPaths::Combine(Fixtures, FString::Printf(TEXT("T_Default_Material_Grid_N.%s"), Extension)));
	}

	FText Error;
	const TSharedPtr<FAssetPackageDocument> OldDocument = FAssetPackageReader::LoadFromFile(FPaths::Combine(Fixtures, TEXT("T_Default_Material_Grid_N.uasset")), Error);
	if (!TestTrue(TEXT("The original loads"), OldDocument.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FAssetPackageTraceCollection> OldTraces = FAssetPackageFieldDecoder::Decode(*OldDocument);
	FAssetBulkDataExport OldData;
	for (const FAssetPackageExportEntry& Export : OldDocument->ExportMap)
	{
		if (DecodeData(*OldDocument, *OldTraces, Export, OldData))
		{
			break;
		}
	}
	if (!TestTrue(TEXT("Its texture is read"), OldData.bComplete && !OldData.PlatformData.IsEmpty()))
	{
		return false;
	}

	// The last inline mip: the position of its bytes in the document is the size of the .uasset plus a position in the .uexp.
	const FAssetTexturePlatformData& Platform = OldData.PlatformData[0];
	int32 InlineMip = INDEX_NONE;
	for (int32 Index = 0; Index < Platform.Mips.Num(); ++Index)
	{
		InlineMip = Platform.Mips[Index].IsInline() && Platform.Mips[Index].PayloadSize >= 32 ? Index : InlineMip;
	}
	if (!TestTrue(TEXT("It has an inline mip with at least two blocks"), InlineMip != INDEX_NONE))
	{
		return false;
	}

	const FAssetTextureMip& Mip = Platform.Mips[InlineMip];
	const int64 HeaderSize = IFileManager::Get().FileSize(*FPaths::Combine(Fixtures, TEXT("T_Default_Material_Grid_N.uasset")));
	TArray<uint8> Exports;
	FFileHelper::LoadFileToArray(Exports, *FPaths::Combine(Folder, TEXT("T_Changed.uexp")));
	const int64 Position = Mip.DataOffset - HeaderSize + 16; // the second block of the mip (BC5 blocks are 16 bytes)
	if (!TestTrue(TEXT("The block is in the .uexp"), Position >= 0 && Position < Exports.Num()))
	{
		return false;
	}
	Exports[Position] ^= 0xFF;
	FFileHelper::SaveArrayToFile(Exports, *FPaths::Combine(Folder, TEXT("T_Changed.uexp")));

	const TSharedPtr<FAssetPackageDocument> NewDocument = FAssetPackageReader::LoadFromFile(FPaths::Combine(Folder, TEXT("T_Changed.uasset")), Error);
	if (TestTrue(TEXT("The changed copy loads"), NewDocument.IsValid()))
	{
		const TSharedPtr<FAssetPackageTraceCollection> NewTraces = FAssetPackageFieldDecoder::Decode(*NewDocument);
		const FAssetPackageDiffResult Diff = AssetPackageDiff::Compare(*OldDocument, *NewDocument, OldTraces.Get(), NewTraces.Get());
		if (const FAssetPackageDiffEntry* Blocks = FindByKey(Diff, FString::Printf(TEXT("Platform/0/Mip/%d/Blocks"), InlineMip)))
		{
			const FString Title = Blocks->DisplayName.ToString();
			AddInfo(Title);
			TestTrue(TEXT("One block differs"), Title.Contains(TEXT("1 of")));
			TestTrue(TEXT("The colors are decoded: the largest change and the average color of both"),
				Title.Contains(TEXT("largest change of a channel")) && Blocks->OldValue.Contains(TEXT("average color")) && Blocks->NewValue.Contains(TEXT("average color")));
			AddInfo(FString::Printf(TEXT("%s | %s | %s"), *Title, *Blocks->OldValue, *Blocks->NewValue));
			TestTrue(TEXT("The second block of the first row: pixels x 4 to 7, y 0 to 3"), Title.Contains(TEXT("x 4 to 7")) && Title.Contains(TEXT("y 0 to 3")));
		}
		else
		{
			AddError(TEXT("The change of the block is not in the diff"));
		}
	}

	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
