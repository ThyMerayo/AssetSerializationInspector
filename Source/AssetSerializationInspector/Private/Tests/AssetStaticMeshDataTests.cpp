// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Serialization/AssetStaticMeshData.h"
#include "Trace/AssetPackageFieldDecoder.h"
#include "Trace/AssetSerializationTrace.h"

namespace StaticMeshTestUtils
{
	/** The data a static mesh writes after its tagged properties: the last range of its trace that no property accounts for. */
	static bool DecodeData(const FAssetPackageDocument& Document, const FAssetPackageTraceCollection& Traces, const FAssetPackageExportEntry& Export, FAssetStaticMeshData& Out)
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

		return Native != nullptr && AssetStaticMeshData::Decode(Document, Export, Export.SerialOffset + Native->Offset, Native->Size, Out, Trace);
	}

	static FAssetMeshMaterialSlot MakeSlot(const TCHAR* SlotName, const TCHAR* Material)
	{
		FAssetMeshMaterialSlot Slot;
		Slot.SlotName = SlotName;
		Slot.ImportedSlotName = SlotName;
		Slot.Material = Material;
		return Slot;
	}

	static FAssetStaticMeshData MakeMesh(const TArray<FAssetMeshMaterialSlot>& Slots)
	{
		FAssetStaticMeshData Mesh;
		Mesh.bComplete = true;
		Mesh.BodySetup = TEXT("/Game/Mesh.BodySetup_1");
		Mesh.NavCollision = TEXT("/Game/Mesh.NavCollision_1");
		Mesh.LightingGuid = TEXT("11111111-2222-3333-4444-555555555555");
		Mesh.Materials = Slots;
		return Mesh;
	}
} // namespace StaticMeshTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetStaticMeshData_ReadsTheStaticMeshesOfTheEngineContent, "AssetSerializationInspector.Serialization.AssetStaticMeshData.ReadsTheStaticMeshesOfTheEngineContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetStaticMeshData_ReadsTheStaticMeshesOfTheEngineContent::RunTest(const FString& Parameters)
{
	using namespace StaticMeshTestUtils;

	const FString Folder = FPaths::Combine(FPaths::EngineContentDir(), TEXT("BasicShapes"));
	const TArray<FString> Meshes = { TEXT("Cube.uasset"), TEXT("Cone.uasset"), TEXT("Sphere.uasset"), TEXT("Cylinder.uasset"), TEXT("Plane.uasset") };
	if (!IFileManager::Get().FileExists(*FPaths::Combine(Folder, Meshes[0])))
	{
		AddInfo(TEXT("The engine does not have the basic shapes, so there is nothing to read."));
		return true;
	}

	for (const FString& Name : Meshes)
	{
		FText Error;
		const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(FPaths::Combine(Folder, Name), Error);
		if (!TestTrue(FString::Printf(TEXT("%s loads"), *Name), Document.IsValid()))
		{
			continue;
		}

		const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);

		int32 Read = 0;
		for (const FAssetPackageExportEntry& Export : Document->ExportMap)
		{
			FAssetStaticMeshData Data;
			if (!DecodeData(*Document, *Traces, Export, Data))
			{
				continue;
			}

			++Read;
			TestTrue(FString::Printf(TEXT("%s: read to the last byte (%s)"), *Name, *Data.Error), Data.bComplete);
			TestFalse(TEXT("It is not cooked"), Data.bCooked);
			TestTrue(TEXT("It has a collision object"), !Data.BodySetup.IsEmpty());
			TestEqual(TEXT("Its lighting GUID is written with hyphens"), Data.LightingGuid.Len(), 36);
			TestTrue(TEXT("It has a material slot"), !Data.Materials.IsEmpty());
			if (!Data.Materials.IsEmpty())
			{
				TestFalse(TEXT("With a name"), Data.Materials[0].SlotName.IsEmpty());
			}

			AddInfo(FString::Printf(TEXT("%s: %s; first slot %s"), *Name, *Data.Summarize(), Data.Materials.IsEmpty() ? TEXT("none") : *Data.Materials[0].Describe()));
		}

		TestEqual(FString::Printf(TEXT("%s has one static mesh"), *Name), Read, 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetStaticMeshData_ComparesTheMaterialSlotsAndSockets, "AssetSerializationInspector.Serialization.AssetStaticMeshData.ComparesTheMaterialSlotsAndSockets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetStaticMeshData_ComparesTheMaterialSlotsAndSockets::RunTest(const FString& Parameters)
{
	using namespace StaticMeshTestUtils;

	const FAssetStaticMeshData Base = MakeMesh({ MakeSlot(TEXT("Body"), TEXT("/Game/M_Body")), MakeSlot(TEXT("Glass"), TEXT("/Game/M_Glass")) });
	TestTrue(TEXT("The same mesh is no change"), AssetStaticMeshData::Compare(Base, Base).IsEmpty());

	const auto Find = [](const TArray<FAssetNativeDataChange>& Changes, const TCHAR* Key) {
		return Changes.FindByPredicate([Key](const FAssetNativeDataChange& Change) { return Change.Key == Key; });
	};

	// A material assigned to a slot another: the slot is modified, with both materials.
	{
		const TArray<FAssetNativeDataChange> Changes = AssetStaticMeshData::Compare(Base, MakeMesh({ MakeSlot(TEXT("Body"), TEXT("/Game/M_Body")), MakeSlot(TEXT("Glass"), TEXT("/Game/M_Tinted")) }));
		if (TestEqual(TEXT("One slot changed"), Changes.Num(), 1))
		{
			TestEqual(TEXT("It is the slot of the glass"), Changes[0].Key, FString(TEXT("Material/Glass")));
			TestEqual(TEXT("It was modified"), static_cast<uint8>(Changes[0].State), static_cast<uint8>(FAssetNativeDataChange::EState::Modified));
			TestTrue(TEXT("From the old material"), Changes[0].OldValue.Contains(TEXT("M_Glass")));
			TestTrue(TEXT("To the new one"), Changes[0].NewValue.Contains(TEXT("M_Tinted")));
		}
	}

	// A slot added and a slot removed.
	{
		const TArray<FAssetNativeDataChange> Added = AssetStaticMeshData::Compare(
			Base, MakeMesh({ MakeSlot(TEXT("Body"), TEXT("/Game/M_Body")), MakeSlot(TEXT("Glass"), TEXT("/Game/M_Glass")), MakeSlot(TEXT("Trim"), TEXT("/Game/M_Trim")) }));
		if (TestEqual(TEXT("A slot was added"), Added.Num(), 1))
		{
			TestEqual(TEXT("It is added"), static_cast<uint8>(Added[0].State), static_cast<uint8>(FAssetNativeDataChange::EState::Added));
			TestEqual(TEXT("It is the trim"), Added[0].Key, FString(TEXT("Material/Trim")));
		}

		const TArray<FAssetNativeDataChange> Removed = AssetStaticMeshData::Compare(Base, MakeMesh({ MakeSlot(TEXT("Body"), TEXT("/Game/M_Body")) }));
		if (TestEqual(TEXT("A slot was removed"), Removed.Num(), 1))
		{
			TestEqual(TEXT("It is removed"), static_cast<uint8>(Removed[0].State), static_cast<uint8>(FAssetNativeDataChange::EState::Removed));
		}
	}

	// A socket, the collision and the lighting GUID.
	{
		FAssetStaticMeshData Changed = Base;
		Changed.Sockets.Add(TEXT("/Game/Mesh.Socket_Hand"));
		Changed.LightingGuid = TEXT("99999999-2222-3333-4444-555555555555");
		Changed.BodySetup = TEXT("/Game/Mesh.BodySetup_2");

		const TArray<FAssetNativeDataChange> Changes = AssetStaticMeshData::Compare(Base, Changed);
		TestNotNull(TEXT("The new socket is reported"), Find(Changes, TEXT("Socket//Game/Mesh.Socket_Hand")));
		TestNotNull(TEXT("The lighting GUID is reported"), Find(Changes, TEXT("LightingGuid")));
		TestNotNull(TEXT("The collision is reported"), Find(Changes, TEXT("BodySetup")));
		TestEqual(TEXT("And nothing else"), Changes.Num(), 3);
	}

	return true;
}

namespace CookedStaticMeshTestUtils
{
	/** Reads the static mesh of a cooked package kept with the plugin (cooked for Windows by this engine version), with its sidecar file next to it. */
	static bool ReadFixture(FAutomationTestBase& Test, const TCHAR* Fixture, FAssetStaticMeshData& Out)
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
			if (StaticMeshTestUtils::DecodeData(*Document, *Traces, Export, Out))
			{
				return true;
			}
		}

		Test.AddError(FString::Printf(TEXT("%s has no static mesh"), Fixture));
		return false;
	}

	static const FAssetNativeDataChange* Find(const TArray<FAssetNativeDataChange>& Changes, const TCHAR* Key)
	{
		return Changes.FindByPredicate([Key](const FAssetNativeDataChange& Change) { return Change.Key == Key; });
	}
	/** Reads the static mesh of a package at a path. */
	static bool ReadFixtureAt(FAutomationTestBase& Test, const FString& Path, FAssetStaticMeshData& Out)
	{
		FText Error;
		const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(Path, Error);
		if (!Test.TestTrue(FString::Printf(TEXT("%s loads"), *Path), Document.IsValid()))
		{
			return false;
		}

		const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);
		for (const FAssetPackageExportEntry& Export : Document->ExportMap)
		{
			if (StaticMeshTestUtils::DecodeData(*Document, *Traces, Export, Out))
			{
				return true;
			}
		}
		return false;
	}
} // namespace CookedStaticMeshTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetStaticMeshData_ReadsTheRenderDataOfACookedMesh, "AssetSerializationInspector.Serialization.AssetStaticMeshData.ReadsTheRenderDataOfACookedMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetStaticMeshData_ReadsTheRenderDataOfACookedMesh::RunTest(const FString& Parameters)
{
	using namespace CookedStaticMeshTestUtils;

	// A plane and a cube of the engine, cooked for Windows: one LOD each, with its buffers in the export.
	for (const TCHAR* Fixture : { TEXT("Plane.uasset"), TEXT("Cube.uasset") })
	{
		FAssetStaticMeshData Data;
		if (!ReadFixture(*this, Fixture, Data))
		{
			continue;
		}

		if (!TestTrue(FString::Printf(TEXT("%s is read to its last byte (%s)"), Fixture, *Data.Error), Data.bComplete))
		{
			continue;
		}

		TestTrue(TEXT("It is cooked"), Data.bCooked);
		if (!TestTrue(TEXT("Its render data is read"), Data.RenderData.bRead))
		{
			continue;
		}

		const FAssetStaticMeshRenderData& Render = Data.RenderData;
		if (!TestFalse(TEXT("It has a LOD"), Render.Lods.IsEmpty()))
		{
			continue;
		}

		for (const FAssetStaticMeshRenderLod& Lod : Render.Lods)
		{
			AddInfo(FString::Printf(TEXT("%s: %s"), Fixture, *Lod.Describe()));
			if (Lod.bCookedOut)
			{
				continue;
			}

			TestFalse(TEXT("The LOD has sections"), Lod.Sections.IsEmpty());
			TestTrue(TEXT("And vertices"), Lod.NumVertices > 0);
			TestTrue(TEXT("And a hash of its buffers"), !Lod.BufferHash.IsEmpty() && Lod.BufferBytes > 0);

			// What the sections say adds up to the index buffer: three indices for each triangle.
			int64 Triangles = 0;
			for (const FAssetStaticMeshRenderSection& Section : Lod.Sections)
			{
				Triangles += Section.NumTriangles;
				TestTrue(TEXT("A section uses a slot of the mesh"), Section.MaterialIndex >= 0 && Section.MaterialIndex < Data.Materials.Num());
				TestTrue(TEXT("In a range of vertices the LOD has"), Section.MinVertexIndex <= Section.MaxVertexIndex && Section.MaxVertexIndex < Lod.NumVertices);
			}
			TestEqual(TEXT("The index buffer has three indices for each triangle"), static_cast<int64>(Lod.NumIndices), Triangles * 3);
		}

		TestTrue(TEXT("The first LOD has a screen size"), Render.ScreenSize[0] > 0.0f);
		TestTrue(TEXT("The bounds are not empty"), Render.Bounds.SphereRadius > 0.0);
		TestTrue(TEXT("The same mesh is no change"), AssetStaticMeshData::Compare(Data, Data).IsEmpty());
	}

	// The cube of the engine has 54 vertices and 48 triangles (its edges are beveled), and a bounding box of 100 units.
	FAssetStaticMeshData Cube;
	if (ReadFixture(*this, TEXT("Cube.uasset"), Cube) && Cube.RenderData.bRead && !Cube.RenderData.Lods.IsEmpty())
	{
		TestEqual(TEXT("The cube has 54 vertices"), static_cast<int32>(Cube.RenderData.Lods[0].NumVertices), 54);
		TestEqual(TEXT("And 144 indices"), Cube.RenderData.Lods[0].NumIndices, 144);
		TestTrue(TEXT("Its box is 100 units on each side"), Cube.RenderData.Bounds.BoxExtent.Equals(FVector(50, 50, 50), 1e-3));
		TestTrue(TEXT("In a sphere of radius 50 times the square root of three"), FMath::IsNearlyEqual(Cube.RenderData.Bounds.SphereRadius, 50.0 * FMath::Sqrt(3.0), 1e-2));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetStaticMeshData_ShowsAChangeOfTheRenderData, "AssetSerializationInspector.Serialization.AssetStaticMeshData.ShowsAChangeOfTheRenderData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetStaticMeshData_ShowsAChangeOfTheRenderData::RunTest(const FString& Parameters)
{
	using namespace CookedStaticMeshTestUtils;

	// Two different meshes: the LOD, its sections and its buffers differ.
	FAssetStaticMeshData Plane;
	FAssetStaticMeshData Cube;
	if (ReadFixture(*this, TEXT("Plane.uasset"), Plane) && ReadFixture(*this, TEXT("Cube.uasset"), Cube) && Plane.RenderData.bRead && Cube.RenderData.bRead)
	{
		const TArray<FAssetNativeDataChange> Changes = AssetStaticMeshData::Compare(Plane, Cube);
		TestNotNull(TEXT("The buffers of the LOD"), Find(Changes, TEXT("Render/Lod/0/Buffers")));
		TestNotNull(TEXT("The section of the LOD"), Find(Changes, TEXT("Render/Lod/0/Section/0")));
		TestNotNull(TEXT("The bounds"), Find(Changes, TEXT("Render/Bounds")));
	}

	// One thing at a time, on a copy of the cube.
	FAssetStaticMeshData Base;
	if (!ReadFixture(*this, TEXT("Cube.uasset"), Base) || !Base.RenderData.bRead || Base.RenderData.Lods.IsEmpty())
	{
		return false;
	}

	{
		FAssetStaticMeshData Repainted = Base;
		Repainted.RenderData.Lods[0].BufferHash = TEXT("zzzzzzzzzzzzzzzz");
		const TArray<FAssetNativeDataChange> Changes = AssetStaticMeshData::Compare(Base, Repainted);
		TestEqual(TEXT("Other buffers are one change"), Changes.Num(), 1);
		TestNotNull(TEXT("Named by its LOD"), Find(Changes, TEXT("Render/Lod/0/Buffers")));
	}
	{
		FAssetStaticMeshData Hidden = Base;
		Hidden.RenderData.Lods[0].BufferHash.Empty();
		TestTrue(TEXT("A hash that could not be read says nothing"), AssetStaticMeshData::Compare(Base, Hidden).IsEmpty());
	}
	{
		FAssetStaticMeshData Other = Base;
		Other.RenderData.Lods[0].Sections[0].MaterialIndex += 1;
		Other.RenderData.OtherHash = TEXT("0000000000000000");
		Other.RenderData.Middle.bDecoded = false; // not decoded: what lies between the LODs and the bounds is compared by its hash
		Other.RenderData.ScreenSize[1] = 0.25f;
		Other.RenderData.Lods.Add(Base.RenderData.Lods[0]);
		const TArray<FAssetNativeDataChange> Changes = AssetStaticMeshData::Compare(Base, Other);
		TestNotNull(TEXT("A section"), Find(Changes, TEXT("Render/Lod/0/Section/0")));
		TestNotNull(TEXT("What lies between the LODs and the bounds"), Find(Changes, TEXT("Render/Other")));
		TestNotNull(TEXT("The screen size"), Find(Changes, TEXT("Render/ScreenSize")));
		if (const FAssetNativeDataChange* Added = Find(Changes, TEXT("Render/Lod/1")))
		{
			TestEqual(TEXT("A LOD added"), static_cast<uint8>(Added->State), static_cast<uint8>(FAssetNativeDataChange::EState::Added));
		}
		else
		{
			AddError(TEXT("The added LOD is not reported"));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetStaticMeshData_ReadsTheSourceModelsOfAnOlderEditor, "AssetSerializationInspector.Serialization.AssetStaticMeshData.ReadsTheSourceModelsOfAnOlderEditor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetStaticMeshData_ReadsTheSourceModelsOfAnOlderEditor::RunTest(const FString& Parameters)
{
	using namespace StaticMeshTestUtils;

	// The meshes of the Concert plugin were saved before the mesh description was an object of its own: each source model has its mesh description inline.
	const FString Path = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Developer/Concert/ConcertSync/ConcertSyncClient/Content/MonitorMesh.uasset"));
	if (!IFileManager::Get().FileExists(*Path))
	{
		AddInfo(TEXT("The engine does not have the Concert plugin, so there is nothing to read."));
		return true;
	}

	FText Error;
	const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(Path, Error);
	if (!TestTrue(TEXT("The mesh loads"), Document.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);
	int32 Read = 0;
	for (const FAssetPackageExportEntry& Export : Document->ExportMap)
	{
		FAssetStaticMeshData Data;
		if (!DecodeData(*Document, *Traces, Export, Data))
		{
			continue;
		}

		++Read;
		TestTrue(FString::Printf(TEXT("Read to the last byte (%s)"), *Data.Error), Data.bComplete);
		if (TestFalse(TEXT("It has its source models inline"), Data.SourceModels.IsEmpty()))
		{
			for (const FAssetStaticMeshSourceModel& Model : Data.SourceModels)
			{
				TestTrue(TEXT("Each has a mesh description"), Model.bHasMeshDescription && Model.PayloadSize > 0 && !Model.MeshGuid.IsEmpty());
			}
			AddInfo(FString::Printf(TEXT("%d source models; the first is %s"), Data.SourceModels.Num(), *Data.SourceModels[0].Describe()));
		}

		// Without the properties the number of source models is not known, so the mesh is not read rather than guessed.
		FAssetStaticMeshData Guessed;
		const FAssetSerializationTraceNode* Native = nullptr;
		for (const TSharedPtr<FAssetSerializationTraceNode>& Node : Traces->FindExportTrace(Export.Index)->Root->Children)
		{
			Native = Node.IsValid() && Node->Kind == EAssetSerializationTraceKind::Native ? Node.Get() : Native;
		}
		if (Native != nullptr && AssetStaticMeshData::Decode(*Document, Export, Export.SerialOffset + Native->Offset, Native->Size, Guessed))
		{
			TestFalse(TEXT("Not read without the properties"), Guessed.bComplete);
		}
	}

	TestEqual(TEXT("It has one static mesh"), Read, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetStaticMeshData_ComparesTheSourceModels, "AssetSerializationInspector.Serialization.AssetStaticMeshData.ComparesTheSourceModels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetStaticMeshData_ComparesTheSourceModels::RunTest(const FString& Parameters)
{
	using namespace StaticMeshTestUtils;

	const auto MakeModel = [](const TCHAR* Hash, const int64 Size) {
		FAssetStaticMeshSourceModel Model;
		Model.bHasMeshDescription = true;
		Model.PayloadHash = Hash;
		Model.PayloadSize = Size;
		Model.MeshGuid = TEXT("11111111-2222-3333-4444-555555555555");
		return Model;
	};

	FAssetStaticMeshData Base = MakeMesh({ MakeSlot(TEXT("Body"), TEXT("/Game/M_Body")) });
	Base.SourceModels = { MakeModel(TEXT("AAAAAAAAAAAAAAAAAAAA"), 100), MakeModel(TEXT("BBBBBBBBBBBBBBBBBBBB"), 50) };
	TestTrue(TEXT("The same mesh is no change"), AssetStaticMeshData::Compare(Base, Base).IsEmpty());

	// The second model has other content, and a third is added.
	FAssetStaticMeshData Edited = Base;
	Edited.SourceModels[1] = MakeModel(TEXT("CCCCCCCCCCCCCCCCCCCC"), 60);
	Edited.SourceModels.Add(MakeModel(TEXT("DDDDDDDDDDDDDDDDDDDD"), 10));
	const TArray<FAssetNativeDataChange> Changes = AssetStaticMeshData::Compare(Base, Edited);
	if (TestEqual(TEXT("Two changes"), Changes.Num(), 2))
	{
		TestEqual(TEXT("The second model changed"), Changes[0].Key, FString(TEXT("SourceModel/1")));
		TestTrue(TEXT("Modified, from one content to the other"),
			Changes[0].State == FAssetNativeDataChange::EState::Modified && Changes[0].OldValue.Contains(TEXT("BBBB")) && Changes[0].NewValue.Contains(TEXT("CCCC")));
		TestEqual(TEXT("The third was added"), Changes[1].Key, FString(TEXT("SourceModel/2")));
		TestTrue(TEXT("Added"), Changes[1].State == FAssetNativeDataChange::EState::Added);
	}

	// A new identifier for the same content is not a change of the content.
	FAssetStaticMeshData Renamed = Base;
	Renamed.SourceModels[0].MeshGuid = TEXT("99999999-2222-3333-4444-555555555555");
	TestTrue(TEXT("A new identifier alone is no change"), AssetStaticMeshData::Compare(Base, Renamed).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetStaticMeshData_ReadsWhatLiesBetweenTheLodsAndTheBounds, "AssetSerializationInspector.Serialization.AssetStaticMeshData.ReadsWhatLiesBetweenTheLodsAndTheBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetStaticMeshData_ReadsWhatLiesBetweenTheLodsAndTheBounds::RunTest(const FString& Parameters)
{
	using namespace CookedStaticMeshTestUtils;

	// The parts add up to what lies between the LODs and the bounds, but for the strip flags in front of the cards and the distance fields.
	const auto TestParts = [this](const TCHAR* Name, const FAssetStaticMeshRenderData& Render) {
		const FAssetStaticMeshRenderMiddle& Middle = Render.Middle;
		int64 Bytes = Middle.Nanite.Part.Bytes + Middle.RayTracing.Part.Bytes + 2 + 2;
		for (const FAssetCardRepresentation& Cards : Middle.Cards)
		{
			Bytes += Cards.Part.Bytes;
		}
		for (const FAssetDistanceField& Field : Middle.DistanceFields)
		{
			Bytes += Field.Part.Bytes;
		}
		TestEqual(FString::Printf(TEXT("%s: the parts are all of it"), Name), Bytes, Render.OtherBytes);
		TestEqual(FString::Printf(TEXT("%s: a card representation and a distance field for each LOD"), Name), Middle.Cards.Num() + Middle.DistanceFields.Num(), 2 * Render.Lods.Num());
	};

	// A cube and a plane of the engine, cooked for Windows: no Nanite data, a ray tracing proxy that shares the buffers of the LOD, cards and a distance field.
	FAssetStaticMeshData Cube;
	if (ReadFixture(*this, TEXT("Cube.uasset"), Cube) && TestTrue(TEXT("The cube's render data is read"), Cube.RenderData.bRead))
	{
		const FAssetStaticMeshRenderMiddle& Middle = Cube.RenderData.Middle;
		if (TestTrue(FString::Printf(TEXT("The middle of the cube is decoded (%s)"), *Middle.Error), Middle.bDecoded))
		{
			TestEqual(TEXT("No Nanite clusters"), Middle.Nanite.Clusters, 0u);
			TestTrue(TEXT("A ray tracing proxy of one LOD that shares the buffers"), Middle.RayTracing.bPresent && Middle.RayTracing.bUsingRenderingLods && Middle.RayTracing.Lods == 1);
			if (TestEqual(TEXT("Cards for the LOD"), Middle.Cards.Num(), 1) && TestTrue(TEXT("That are valid"), Middle.Cards[0].bValid))
			{
				TestEqual(TEXT("Six cards, one for each side"), Middle.Cards[0].Cards, 6);
				// Each side of the cube has a card that faces it and sits at the distance of the side from the center.
				if (TestEqual(TEXT("The list has the six cards"), Middle.Cards[0].CardList.Num(), 6))
				{
					TSet<int32> Directions;
					for (const FAssetCard& Card : Middle.Cards[0].CardList)
					{
						Directions.Add(Card.DirectionIndex);
						TestTrue(TEXT("Its axes are unit vectors"),
							FMath::IsNearlyEqual(Card.AxisX.Size(), 1.0, 1e-3) && FMath::IsNearlyEqual(Card.AxisY.Size(), 1.0, 1e-3) && FMath::IsNearlyEqual(Card.AxisZ.Size(), 1.0, 1e-3));
						TestTrue(TEXT("Its origin is 45 units from the center, in the direction it faces"),
							FMath::IsNearlyEqual(Card.Origin.GetAbsMax(), 45.0, 1e-3) && (Card.Origin.GetSafeNormal() | Card.AxisZ) > 0.99);
						TestTrue(TEXT("It is a slab of 100 by 100 by 20 units"), Card.Extent.Equals(FVector(50, 50, 10), 1e-3));
					}
					TestEqual(TEXT("The six cards face six directions"), Directions.Num(), 6);
				}
				TestTrue(TEXT("In a box a little larger than the cube"), Middle.Cards[0].BoundsMin.Equals(FVector(-51, -51, -51), 1e-3) && Middle.Cards[0].BoundsMax.Equals(FVector(51, 51, 51), 1e-3));
			}
			if (TestEqual(TEXT("A distance field for the LOD"), Middle.DistanceFields.Num(), 1) && TestTrue(TEXT("That is valid"), Middle.DistanceFields[0].bValid))
			{
				const FAssetDistanceField& Field = Middle.DistanceFields[0];
				TestTrue(TEXT("The bricks of its three mips"), Field.Mips[0].Bricks == 208 && Field.Mips[1].Bricks == 27 && Field.Mips[2].Bricks == 8);
				TestTrue(TEXT("In the box of the cube"), Field.BoundsMin.Equals(FVector(-50, -50, -50), 1e-3) && Field.BoundsMax.Equals(FVector(50, 50, 50), 1e-3));
				TestTrue(TEXT("With streamed mips hashed from the sidecar file"), Field.StreamableBytes > 0 && !Field.StreamableHash.IsEmpty());
			}
			TestParts(TEXT("Cube"), Cube.RenderData);
		}
	}

	FAssetStaticMeshData Plane;
	if (ReadFixture(*this, TEXT("Plane.uasset"), Plane) && Plane.RenderData.bRead && TestTrue(TEXT("The middle of the plane is decoded"), Plane.RenderData.Middle.bDecoded))
	{
		const FAssetStaticMeshRenderMiddle& Middle = Plane.RenderData.Middle;
		TestTrue(TEXT("One card and fewer bricks"), Middle.Cards.Num() == 1 && Middle.Cards[0].Cards == 1 && Middle.DistanceFields.Num() == 1 && Middle.DistanceFields[0].Mips[0].Bricks == 9);
		TestParts(TEXT("Plane"), Plane.RenderData);
	}

	// A sphere that has Nanite data and a ray tracing proxy with buffers of its own, from the engine's editor meshes.
	FAssetStaticMeshData Sphere;
	if (ReadFixture(*this, TEXT("SM_Dataflow_Sphere.uasset"), Sphere) && Sphere.RenderData.bRead
		&& TestTrue(FString::Printf(TEXT("The middle of the Nanite sphere is decoded (%s)"), *Sphere.RenderData.Middle.Error), Sphere.RenderData.Middle.bDecoded))
	{
		const FAssetStaticMeshRenderMiddle& Middle = Sphere.RenderData.Middle;
		TestTrue(TEXT("It has Nanite data"), Middle.Nanite.bPresent);
		TestEqual(TEXT("Six clusters"), Middle.Nanite.Clusters, 6u);
		TestEqual(TEXT("In one page, which is a root page"), Middle.Nanite.Pages, 1);
		TestEqual(TEXT("That is a root page"), Middle.Nanite.RootPages, 1u);
		TestEqual(TEXT("Of 320 triangles"), Middle.Nanite.InputTriangles, 320u);
		TestEqual(TEXT("And 232 vertices"), Middle.Nanite.InputVertices, 232u);
		TestTrue(TEXT("A ray tracing proxy of two LODs that are its own"), Middle.RayTracing.bPresent && !Middle.RayTracing.bUsingRenderingLods && Middle.RayTracing.Lods == 2);
		TestParts(TEXT("Sphere"), Sphere.RenderData);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetStaticMeshData_ShowsAChangeOfTheCardsAndTheDistanceField,
	"AssetSerializationInspector.Serialization.AssetStaticMeshData.ShowsAChangeOfTheCardsAndTheDistanceField", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetStaticMeshData_ShowsAChangeOfTheCardsAndTheDistanceField::RunTest(const FString& Parameters)
{
	using namespace CookedStaticMeshTestUtils;

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
	if (!TestTrue(TEXT("The plugin is found"), Plugin.IsValid()))
	{
		return false;
	}

	const auto Find = [](const TArray<FAssetNativeDataChange>& Changes, const TCHAR* Key) {
		return Changes.FindByPredicate([Key](const FAssetNativeDataChange& Change) { return Change.Key == Key; });
	};

	FAssetStaticMeshData Cube;
	FAssetStaticMeshData Plane;
	FAssetStaticMeshData Sphere;
	if (!ReadFixture(*this, TEXT("Cube.uasset"), Cube) || !ReadFixture(*this, TEXT("Plane.uasset"), Plane) || !ReadFixture(*this, TEXT("SM_Dataflow_Sphere.uasset"), Sphere)
		|| !Cube.RenderData.Middle.bDecoded || !Plane.RenderData.Middle.bDecoded || !Sphere.RenderData.Middle.bDecoded)
	{
		AddError(TEXT("The fixtures are not read"));
		return false;
	}

	TestTrue(TEXT("The same mesh is no change"), AssetStaticMeshData::Compare(Cube, Cube).IsEmpty());

	// Two meshes: the cards and the distance field name the LOD; the hash of all of it is not reported when the parts are.
	{
		const TArray<FAssetNativeDataChange> Changes = AssetStaticMeshData::Compare(Plane, Cube);
		if (const FAssetNativeDataChange* Cards = Find(Changes, TEXT("Render/Lod/0/Cards")))
		{
			TestTrue(TEXT("From one card to six"), Cards->OldValue.StartsWith(TEXT("1 cards")) && Cards->NewValue.StartsWith(TEXT("6 cards")));
		}
		else
		{
			AddError(TEXT("The cards are not in the changes"));
		}
		if (const FAssetNativeDataChange* Field = Find(Changes, TEXT("Render/Lod/0/DistanceField")))
		{
			TestTrue(TEXT("From 9 bricks to 208"), Field->OldValue.StartsWith(TEXT("9, 4 and 1 bricks")) && Field->NewValue.StartsWith(TEXT("208, 27 and 8 bricks")));
		}
		else
		{
			AddError(TEXT("The distance field is not in the changes"));
		}
		TestNull(TEXT("What lies between the LODs and the bounds is not reported as a whole"), Find(Changes, TEXT("Render/Other")));
		TestNull(TEXT("Neither has Nanite clusters"), Find(Changes, TEXT("Render/Nanite")));
	}

	// The cards of a LOD: the cards that changed are named, and the others are not.
	{
		FAssetStaticMeshData Moved = Cube;
		Moved.RenderData.Middle.Cards[0].CardList[2].Origin += FVector(0, 0, 5);
		Moved.RenderData.Middle.Cards[0].Part.Hash = TEXT("changed");
		FAssetStaticMeshData Fewer = Cube;
		Fewer.RenderData.Middle.Cards[0].CardList.Pop();
		Fewer.RenderData.Middle.Cards[0].Cards = 5;
		Fewer.RenderData.Middle.Cards[0].Part.Hash = TEXT("fewer");

		const TArray<FAssetNativeDataChange> MovedChanges = AssetStaticMeshData::Compare(Cube, Moved);
		TestNotNull(TEXT("The cards of the LOD changed"), Find(MovedChanges, TEXT("Render/Lod/0/Cards")));
		if (const FAssetNativeDataChange* Card = Find(MovedChanges, TEXT("Render/Lod/0/Card/2")))
		{
			TestTrue(TEXT("The third card moved 5 units up"), Card->State == FAssetNativeDataChange::EState::Modified && Card->OldValue != Card->NewValue);
		}
		else
		{
			AddError(TEXT("The card that moved is not in the changes"));
		}
		TestNull(TEXT("The others did not"), Find(MovedChanges, TEXT("Render/Lod/0/Card/1")));

		const TArray<FAssetNativeDataChange> FewerChanges = AssetStaticMeshData::Compare(Cube, Fewer);
		if (const FAssetNativeDataChange* Removed = Find(FewerChanges, TEXT("Render/Lod/0/Card/5")))
		{
			TestTrue(TEXT("The sixth card was removed"), Removed->State == FAssetNativeDataChange::EState::Removed);
		}
		else
		{
			AddError(TEXT("The removed card is not in the changes"));
		}
	}

	// A mesh with Nanite data and a proxy of its own.
	{
		const TArray<FAssetNativeDataChange> Changes = AssetStaticMeshData::Compare(Cube, Sphere);
		if (const FAssetNativeDataChange* Nanite = Find(Changes, TEXT("Render/Nanite")))
		{
			TestTrue(TEXT("To 6 clusters"), Nanite->OldValue.StartsWith(TEXT("0 clusters")) && Nanite->NewValue.StartsWith(TEXT("6 clusters")));
		}
		else
		{
			AddError(TEXT("The Nanite resources are not in the changes"));
		}
		TestNotNull(TEXT("The ray tracing proxy"), Find(Changes, TEXT("Render/RayTracing")));
	}

	// One byte of the distance field of a copy of the cube: nothing else is reported.
	const FString Fixtures = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("CookedTestFixtures"));
	const FString Folder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("__AssetSerializationInspectorTests"), TEXT("CookedMiddle"));
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	IFileManager::Get().MakeDirectory(*Folder, true);
	for (const TCHAR* Extension : { TEXT("uasset"), TEXT("uexp"), TEXT("ubulk") })
	{
		IFileManager::Get().Copy(*FPaths::Combine(Folder, FString::Printf(TEXT("Cube.%s"), Extension)), *FPaths::Combine(Fixtures, FString::Printf(TEXT("Cube.%s"), Extension)));
	}

	const int64 HeaderSize = IFileManager::Get().FileSize(*FPaths::Combine(Fixtures, TEXT("Cube.uasset")));
	TArray<uint8> Exports;
	if (!TestTrue(TEXT("The copy is there"), FFileHelper::LoadFileToArray(Exports, *FPaths::Combine(Folder, TEXT("Cube.uexp")))))
	{
		return false;
	}

	const auto Damaged = [&](const FAssetRenderPart& Part, const int64 Skip, FAssetStaticMeshData& Out) {
		TArray<uint8> Copy = Exports;
		const int64 Position = Part.Offset + Skip - HeaderSize;
		if (Position < 0 || Position >= Copy.Num())
		{
			return false;
		}
		Copy[Position] ^= 0x55;
		FFileHelper::SaveArrayToFile(Copy, *FPaths::Combine(Folder, TEXT("Cube.uexp")));
		return ReadFixtureAt(*this, FPaths::Combine(Folder, TEXT("Cube.uasset")), Out);
	};

	// The first mip of the distance field: its indirection grid starts 33 bytes in (a flag, the box and a flag).
	FAssetStaticMeshData WithField;
	if (Damaged(Cube.RenderData.Middle.DistanceFields[0].Part, 33, WithField) && WithField.RenderData.Middle.bDecoded)
	{
		const TArray<FAssetNativeDataChange> Changes = AssetStaticMeshData::Compare(Cube, WithField);
		TestEqual(TEXT("One byte of the distance field is one change"), Changes.Num(), 1);
		TestNotNull(TEXT("Named by the LOD"), Find(Changes, TEXT("Render/Lod/0/DistanceField")));
	}
	else
	{
		AddError(TEXT("The copy with a changed distance field is not read"));
	}

	// The same for the cards: the box of the first card starts 4 bytes in.
	FAssetStaticMeshData WithCards;
	if (Damaged(Cube.RenderData.Middle.Cards[0].Part, 8, WithCards) && WithCards.RenderData.Middle.bDecoded)
	{
		const TArray<FAssetNativeDataChange> Changes = AssetStaticMeshData::Compare(Cube, WithCards);
		TestEqual(TEXT("One byte of the cards is one change"), Changes.Num(), 1);
		TestNotNull(TEXT("Named by the LOD"), Find(Changes, TEXT("Render/Lod/0/Cards")));
	}
	else
	{
		AddError(TEXT("The copy with changed cards is not read"));
	}

	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetStaticMeshData_ReadsTheRawMeshOfAnOlderStill, "AssetSerializationInspector.Serialization.AssetStaticMeshData.ReadsTheRawMeshOfAnOlderStill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetStaticMeshData_ReadsTheRawMeshOfAnOlderStill::RunTest(const FString& Parameters)
{
	using namespace StaticMeshTestUtils;

	// The garbage matte plane of the mixed reality capture plugin was saved before the raw mesh was replaced by the mesh description.
	const FString Path = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Runtime/MixedRealityCaptureFramework/Content/SM_GarbageMattePlane.uasset"));
	if (!IFileManager::Get().FileExists(*Path))
	{
		AddInfo(TEXT("The engine does not have the mixed reality capture plugin, so there is nothing to read."));
		return true;
	}

	FText Error;
	const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(Path, Error);
	if (!TestTrue(TEXT("The mesh loads"), Document.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);
	int32 Read = 0;
	for (const FAssetPackageExportEntry& Export : Document->ExportMap)
	{
		FAssetStaticMeshData Data;
		if (!DecodeData(*Document, *Traces, Export, Data))
		{
			continue;
		}

		++Read;
		TestTrue(FString::Printf(TEXT("Read to the last byte (%s)"), *Data.Error), Data.bComplete);
		if (TestEqual(TEXT("One source model"), Data.SourceModels.Num(), 1))
		{
			TestTrue(TEXT("It is a raw mesh"), Data.SourceModels[0].bRawMesh && Data.SourceModels[0].bHasMeshDescription);
			TestEqual(TEXT("Of 284 bytes"), Data.SourceModels[0].PayloadSize, static_cast<int64>(284));
			TestFalse(TEXT("With an identifier"), Data.SourceModels[0].MeshGuid.IsEmpty());
		}
		TestEqual(TEXT("And one material slot"), Data.Materials.Num(), 1);
	}

	TestEqual(TEXT("It has one static mesh"), Read, 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
