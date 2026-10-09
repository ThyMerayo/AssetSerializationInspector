// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Diff/AssetPackageDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Save/AssetSaveAnalyzer.h"
#include "Serialization/AssetSkeletalMeshData.h"
#include "Tests/AssetTestUtils.h"
#include "Trace/AssetPackageFieldDecoder.h"
#include "Trace/AssetSerializationTrace.h"

using AssetTestUtils::FindChange;

namespace SkeletalMeshTestUtils
{
	/** The data a skeletal mesh writes after its tagged properties: the last range of its trace that no property accounts for. */
	static bool DecodeData(const FAssetPackageDocument& Document, const FAssetPackageTraceCollection& Traces, const FAssetPackageExportEntry& Export, FAssetSkeletalMeshData& Out)
	{
		return AssetTestUtils::DecodeLastNative(
			Traces, Export, [&](const int64 Offset, const int64 Size, const FAssetSerializationTrace*) { return AssetSkeletalMeshData::Decode(Document, Export, Offset, Size, Out); });
	}

	static FAssetSkeletonBone MakeBone(const TCHAR* Name, const TCHAR* Parent, const TCHAR* Pose)
	{
		FAssetSkeletonBone Bone;
		Bone.Name = Name;
		Bone.ParentName = Parent;
		Bone.Pose = Pose;
		return Bone;
	}

	static FAssetSkeletalMeshData MakeMesh(const TArray<FAssetSkeletonBone>& Bones)
	{
		FAssetSkeletalMeshData Mesh;
		Mesh.bPrefixRead = true;
		Mesh.ImportedBounds = TEXT("origin X=0.000 Y=0.000 Z=0.000, extent X=50.000 Y=50.000 Z=50.000, radius 86.6");
		Mesh.Bones = Bones;
		return Mesh;
	}
	/** Reads the skeletal mesh of a package at a path. */
	static bool ReadMeshAt(FAutomationTestBase& Test, const FString& Path, FAssetSkeletalMeshData& Out)
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
			if (DecodeData(*Document, *Traces, Export, Out))
			{
				return true;
			}
		}
		return false;
	}

	/** Reads the skeletal mesh of a cooked package kept with the plugin (cooked for Windows by this engine version). */
	static bool ReadFixture(FAutomationTestBase& Test, const TCHAR* Fixture, FAssetSkeletalMeshData& Out)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
		return Test.TestTrue(TEXT("The plugin is found"), Plugin.IsValid()) && ReadMeshAt(Test, FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("CookedTestFixtures"), Fixture), Out);
	}
} // namespace SkeletalMeshTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSkeletalMeshData_ReadsTheSkeletalMeshesOfTheEngineContent,
	"AssetSerializationInspector.Serialization.AssetSkeletalMeshData.ReadsTheSkeletalMeshesOfTheEngineContent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSkeletalMeshData_ReadsTheSkeletalMeshesOfTheEngineContent::RunTest(const FString& Parameters)
{
	using namespace SkeletalMeshTestUtils;

	const TArray<FString> Meshes = { FPaths::Combine(FPaths::EngineContentDir(), TEXT("EngineMeshes"), TEXT("SkeletalCube.uasset")),
		FPaths::Combine(FPaths::EngineContentDir(), TEXT("EditorMeshes"), TEXT("SkeletalMesh"), TEXT("DefaultSkeletalMesh.uasset")) };
	if (!IFileManager::Get().FileExists(*Meshes[0]))
	{
		AddInfo(TEXT("The engine does not have its skeletal meshes, so there is nothing to read."));
		return true;
	}

	// Two meshes the engine's plugins keep from before 5.0 (the mannequins), whose model is stored in the layouts of those versions: the
	// raw point indices and the reduction sources are blocks of bulk data of their own, and the sections have no ray tracing flag.
	TArray<FString> AllMeshes = Meshes;
	for (const FString& Older : { FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Runtime"), TEXT("NetworkPredictionExtras"), TEXT("Content"), TEXT("Animation"), TEXT("Characters"), TEXT("UE4_Guy"),
									  TEXT("Mesh"), TEXT("SK_Mannequin.uasset")),
			 FPaths::Combine(
				 FPaths::EnginePluginsDir(), TEXT("Experimental"), TEXT("AnimToTexture"), TEXT("Content"), TEXT("Characters"), TEXT("Mannequin"), TEXT("Meshes"), TEXT("SKM_Mannequin.uasset")) })
	{
		if (IFileManager::Get().FileExists(*Older))
		{
			AllMeshes.Add(Older);
		}
	}

	for (const FString& File : AllMeshes)
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
			FAssetSkeletalMeshData Data;
			if (!DecodeData(*Document, *Traces, Export, Data))
			{
				continue;
			}

			++Read;
			if (!TestTrue(FString::Printf(TEXT("%s: the start of the data is read (%s)"), *File, *Data.Error), Data.bPrefixRead))
			{
				continue;
			}

			TestFalse(TEXT("It has bones"), Data.Bones.IsEmpty());
			TestFalse(TEXT("And material slots"), Data.Materials.IsEmpty());
			TestTrue(TEXT("The imported model follows the skeleton"), Data.RemainingSize > 0);

			// The imported model is read to the last byte, and what it says adds up: the triangles of the sections are the indices of the
			// LOD, and the vertices of the sections are its vertices.
			if (TestTrue(FString::Printf(TEXT("%s: the imported model is read to the last byte (%s)"), *File, *Data.ModelError), Data.bComplete))
			{
				TestFalse(TEXT("It has a LOD"), Data.Lods.IsEmpty());
				for (const FAssetSkeletalMeshLod& Lod : Data.Lods)
				{
					TestFalse(TEXT("With sections"), Lod.Sections.IsEmpty());

					uint32 Triangles = 0;
					int64 Vertices = 0;
					for (const FAssetSkeletalMeshSection& Section : Lod.Sections)
					{
						Triangles += Section.NumTriangles;
						Vertices += Section.NumVertices;
						TestEqual(TEXT("A section keeps as many vertices as it says"), Section.VertexCount, Section.NumVertices);
						TestFalse(TEXT("And a hash of them"), Section.VertexHash.IsEmpty());
						TestTrue(TEXT("Its material is a slot of the mesh"), Section.MaterialIndex < Data.Materials.Num());
					}

					TestEqual(TEXT("The index buffer has three indices for each triangle"), Lod.IndexCount, static_cast<int32>(Triangles * 3));
					TestEqual(TEXT("The LOD has the vertices of its sections"), static_cast<int64>(Lod.NumVertices), Vertices);
					AddInfo(FString::Printf(TEXT("%s: %s"), *FPaths::GetBaseFilename(File), *Lod.Describe()));
				}
			}

			// A skeleton is a tree: one root, and every other bone hangs from a bone of the skeleton.
			int32 Roots = 0;
			TSet<FString> Names;
			for (const FAssetSkeletonBone& Bone : Data.Bones)
			{
				Names.Add(Bone.Name);
			}
			for (const FAssetSkeletonBone& Bone : Data.Bones)
			{
				if (Bone.ParentName.IsEmpty())
				{
					++Roots;
				}
				else
				{
					TestTrue(FString::Printf(TEXT("%s hangs from a bone of the skeleton"), *Bone.Name), Names.Contains(Bone.ParentName));
				}
			}
			TestEqual(TEXT("There is one root bone"), Roots, 1);

			AddInfo(FString::Printf(TEXT("%s: %s; root %s; first slot %s"), *FPaths::GetBaseFilename(File), *Data.Summarize(), *Data.Bones[0].Name,
				Data.Materials.IsEmpty() ? TEXT("none") : *Data.Materials[0].Describe()));
		}

		TestEqual(FString::Printf(TEXT("%s has one skeletal mesh"), *File), Read, 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSkeletalMeshData_ComparesBonesAndMaterialSlots, "AssetSerializationInspector.Serialization.AssetSkeletalMeshData.ComparesBonesAndMaterialSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSkeletalMeshData_ComparesBonesAndMaterialSlots::RunTest(const FString& Parameters)
{
	using namespace SkeletalMeshTestUtils;

	const FAssetSkeletalMeshData Base =
		MakeMesh({ MakeBone(TEXT("root"), TEXT(""), TEXT("pose A")), MakeBone(TEXT("spine"), TEXT("root"), TEXT("pose B")), MakeBone(TEXT("head"), TEXT("spine"), TEXT("pose C")) });
	TestTrue(TEXT("The same skeleton is no change"), AssetSkeletalMeshData::Compare(Base, Base).IsEmpty());

	// A bone added at the end, and one that went away.
	{
		const TArray<FAssetNativeDataChange> Added = AssetSkeletalMeshData::Compare(Base,
			MakeMesh({ MakeBone(TEXT("root"), TEXT(""), TEXT("pose A")), MakeBone(TEXT("spine"), TEXT("root"), TEXT("pose B")), MakeBone(TEXT("head"), TEXT("spine"), TEXT("pose C")),
				MakeBone(TEXT("hat"), TEXT("head"), TEXT("pose D")) }));
		if (const FAssetNativeDataChange* Change = FindChange(Added, TEXT("Bone/hat")))
		{
			TestEqual(TEXT("The new bone is added"), static_cast<uint8>(Change->State), static_cast<uint8>(FAssetNativeDataChange::EState::Added));
			TestTrue(TEXT("With its parent"), Change->NewValue.Contains(TEXT("parent head")));
		}
		else
		{
			AddError(TEXT("The added bone is not reported"));
		}

		const TArray<FAssetNativeDataChange> Removed =
			AssetSkeletalMeshData::Compare(Base, MakeMesh({ MakeBone(TEXT("root"), TEXT(""), TEXT("pose A")), MakeBone(TEXT("spine"), TEXT("root"), TEXT("pose B")) }));
		if (const FAssetNativeDataChange* Change = FindChange(Removed, TEXT("Bone/head")))
		{
			TestEqual(TEXT("The bone that went away is removed"), static_cast<uint8>(Change->State), static_cast<uint8>(FAssetNativeDataChange::EState::Removed));
		}
		else
		{
			AddError(TEXT("The removed bone is not reported"));
		}
	}

	// A bone moved to another parent, and a bone with another pose: each says which of the two it is.
	{
		const TArray<FAssetNativeDataChange> Changes = AssetSkeletalMeshData::Compare(
			Base, MakeMesh({ MakeBone(TEXT("root"), TEXT(""), TEXT("pose A")), MakeBone(TEXT("spine"), TEXT("root"), TEXT("pose B2")), MakeBone(TEXT("head"), TEXT("root"), TEXT("pose C")) }));
		TestEqual(TEXT("Two bones changed"), Changes.Num(), 2);
		if (const FAssetNativeDataChange* Pose = FindChange(Changes, TEXT("Bone/spine")))
		{
			TestTrue(TEXT("The pose of the spine"), Pose->Title.Contains(TEXT("pose")));
		}
		if (const FAssetNativeDataChange* Parent = FindChange(Changes, TEXT("Bone/head")))
		{
			TestTrue(TEXT("The parent of the head"), Parent->Title.Contains(TEXT("parent")));
			TestTrue(TEXT("From the spine"), Parent->OldValue.Contains(TEXT("parent spine")));
			TestTrue(TEXT("To the root"), Parent->NewValue.Contains(TEXT("parent root")));
		}
	}

	// A material slot given another material.
	{
		FAssetSkeletalMeshData Old = Base;
		FAssetMeshMaterialSlot Slot;
		Slot.SlotName = TEXT("Skin");
		Slot.Material = TEXT("/Game/M_Skin");
		Old.Materials.Add(Slot);

		FAssetSkeletalMeshData New = Old;
		New.Materials[0].Material = TEXT("/Game/M_Skin2");

		const TArray<FAssetNativeDataChange> Changes = AssetSkeletalMeshData::Compare(Old, New);
		if (TestEqual(TEXT("One slot changed"), Changes.Num(), 1))
		{
			TestEqual(TEXT("It is the skin"), Changes[0].Key, FString(TEXT("Material/Skin")));
			TestTrue(TEXT("With the new material"), Changes[0].NewValue.Contains(TEXT("M_Skin2")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSkeletalMeshData_ComparesTheLodsAndSections, "AssetSerializationInspector.Serialization.AssetSkeletalMeshData.ComparesTheLodsAndSections",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSkeletalMeshData_ComparesTheLodsAndSections::RunTest(const FString& Parameters)
{
	using namespace SkeletalMeshTestUtils;

	const auto MakeSection = [](const uint32 Triangles, const int32 Vertices, const TCHAR* Hash) {
		FAssetSkeletalMeshSection Section;
		Section.NumTriangles = Triangles;
		Section.NumVertices = Vertices;
		Section.VertexCount = Vertices;
		Section.VertexHash = Hash;
		Section.BoneCount = 2;
		return Section;
	};

	const auto MakeLod = [](const TArray<FAssetSkeletalMeshSection>& Sections, const TCHAR* IndexHash) {
		FAssetSkeletalMeshLod Lod;
		Lod.Sections = Sections;
		Lod.IndexHash = IndexHash;
		Lod.IndexCount = 36;
		Lod.NumTexCoords = 1;
		Lod.RequiredBoneCount = 2;
		Lod.ActiveBoneCount = 2;
		for (const FAssetSkeletalMeshSection& Section : Sections)
		{
			Lod.NumVertices += Section.NumVertices;
		}
		return Lod;
	};

	FAssetSkeletalMeshData Base = MakeMesh({});
	Base.bComplete = true;
	Base.ModelGuid = TEXT("11111111-1111-1111-1111-111111111111");
	Base.Lods.Add(MakeLod({ MakeSection(12, 24, TEXT("aaaa")), MakeSection(4, 8, TEXT("bbbb")) }, TEXT("iiii")));

	TestTrue(TEXT("The same model is no change"), AssetSkeletalMeshData::Compare(Base, Base).IsEmpty());

	// The vertices of a section moved (the hash changes, the counts do not): the geometry changed.
	{
		FAssetSkeletalMeshData Moved = Base;
		Moved.Lods[0].Sections[1].VertexHash = TEXT("cccc");
		Moved.ModelGuid = TEXT("22222222-2222-2222-2222-222222222222");

		const TArray<FAssetNativeDataChange> Changes = AssetSkeletalMeshData::Compare(Base, Moved);
		if (const FAssetNativeDataChange* Change = FindChange(Changes, TEXT("Lod/0/Section/1")))
		{
			TestEqual(TEXT("The section is modified"), static_cast<uint8>(Change->State), static_cast<uint8>(FAssetNativeDataChange::EState::Modified));
			TestTrue(TEXT("From the old vertex data"), Change->OldValue.Contains(TEXT("bbbb")));
			TestTrue(TEXT("To the new"), Change->NewValue.Contains(TEXT("cccc")));
		}
		else
		{
			AddError(TEXT("The change of the vertices is not reported"));
		}
		TestNull(TEXT("The other section is not"), FindChange(Changes, TEXT("Lod/0/Section/0")));
		TestNotNull(TEXT("The identifier of the model follows, since something changed"), FindChange(Changes, TEXT("ModelGuid")));
	}

	// A section added, a LOD added, and the index buffer of a LOD.
	{
		FAssetSkeletalMeshData Grown = Base;
		Grown.Lods[0].Sections.Add(MakeSection(2, 4, TEXT("dddd")));
		Grown.Lods[0].NumVertices += 4;
		Grown.Lods.Add(MakeLod({ MakeSection(6, 12, TEXT("eeee")) }, TEXT("jjjj")));

		const TArray<FAssetNativeDataChange> Changes = AssetSkeletalMeshData::Compare(Base, Grown);
		if (const FAssetNativeDataChange* Section = FindChange(Changes, TEXT("Lod/0/Section/2")))
		{
			TestEqual(TEXT("The new section is added"), static_cast<uint8>(Section->State), static_cast<uint8>(FAssetNativeDataChange::EState::Added));
		}
		else
		{
			AddError(TEXT("The added section is not reported"));
		}
		if (const FAssetNativeDataChange* Lod = FindChange(Changes, TEXT("Lod/1")))
		{
			TestEqual(TEXT("The new LOD is added"), static_cast<uint8>(Lod->State), static_cast<uint8>(FAssetNativeDataChange::EState::Added));
		}
		else
		{
			AddError(TEXT("The added LOD is not reported"));
		}
		TestNotNull(TEXT("The vertices of the LOD changed with the new section"), FindChange(Changes, TEXT("Lod/0/Whole")));
	}

	// A model that is not read on one side is not compared.
	{
		FAssetSkeletalMeshData NotRead = Base;
		NotRead.bComplete = false;
		NotRead.Lods.Reset();
		TestTrue(TEXT("Nothing is said of a model that was not read"), AssetSkeletalMeshData::Compare(NotRead, Base).IsEmpty());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSkeletalMeshData_ReadsTheRenderDataOfACookedMesh, "AssetSerializationInspector.Serialization.AssetSkeletalMeshData.ReadsTheRenderDataOfACookedMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSkeletalMeshData_ReadsTheRenderDataOfACookedMesh::RunTest(const FString& Parameters)
{
	using namespace SkeletalMeshTestUtils;

	// A cube with three bones, cooked for Windows: one LOD with its buffers in the export.
	FAssetSkeletalMeshData Cube;
	if (ReadFixture(*this, TEXT("SKM_PhysCube.uasset"), Cube))
	{
		TestTrue(FString::Printf(TEXT("It is read to the last byte (%s)"), *Cube.ModelError), Cube.bComplete);
		TestTrue(TEXT("Its render data is read"), Cube.Render.bRead);
		TestEqual(TEXT("It has three bones"), Cube.Bones.Num(), 3);
		if (TestEqual(TEXT("One LOD"), Cube.Render.Lods.Num(), 1))
		{
			const FAssetSkeletalRenderLod& Lod = Cube.Render.Lods[0];
			TestTrue(TEXT("Its buffers are in the export"), Lod.bInlined && !Lod.bCookedOut && Lod.BufferOffset != INDEX_NONE && !Lod.BufferHash.IsEmpty());
			TestEqual(TEXT("Three required bones and three active"), Lod.RequiredBones * 10 + Lod.ActiveBones, 33);
			TestTrue(TEXT("The record of the buffers: 54 vertices, 144 indices of 16 bits, a UV channel, 54 colors"),
				Lod.bHasCounts && Lod.NumVertices == 54 && Lod.NumIndices == 144 && Lod.IndexBytes == 2 && Lod.NumTexCoords == 1 && Lod.ColorVertices == 54);
			if (TestEqual(TEXT("One section"), Lod.Sections.Num(), 1))
			{
				// 144 indices are 48 triangles, which is what the section draws, over the 54 vertices.
				TestTrue(TEXT("Of 48 triangles and the 54 vertices"), Lod.Sections[0].NumTriangles == 48 && Lod.Sections[0].NumVertices == 54);
			}
		}
		TestTrue(TEXT("The same mesh is no change"), AssetSkeletalMeshData::Compare(Cube, Cube).IsEmpty());
	}

	// A chain with three LODs, the last two smaller.
	FAssetSkeletalMeshData Chain;
	if (ReadFixture(*this, TEXT("SKM_Chain_Template.uasset"), Chain))
	{
		TestTrue(FString::Printf(TEXT("The chain is read to the last byte (%s)"), *Chain.ModelError), Chain.bComplete && Chain.Render.bRead);
		if (TestEqual(TEXT("Three LODs"), Chain.Render.Lods.Num(), 3))
		{
			TestEqual(TEXT("The first has 8 vertices"), static_cast<int32>(Chain.Render.Lods[0].NumVertices), 8);
			TestEqual(TEXT("The others have 6"), static_cast<int32>(Chain.Render.Lods[1].NumVertices + Chain.Render.Lods[2].NumVertices), 12);
			TestEqual(TEXT("Two sections in each"), Chain.Render.Lods[0].Sections.Num() + Chain.Render.Lods[1].Sections.Num() + Chain.Render.Lods[2].Sections.Num(), 6);
		}
		TestEqual(TEXT("Of the three, all are inlined"), static_cast<int32>(Chain.Render.NumInlinedLods), 3);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSkeletalMeshData_ShowsAChangeOfTheRenderData, "AssetSerializationInspector.Serialization.AssetSkeletalMeshData.ShowsAChangeOfTheRenderData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSkeletalMeshData_ShowsAChangeOfTheRenderData::RunTest(const FString& Parameters)
{
	using namespace SkeletalMeshTestUtils;

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
	if (!TestTrue(TEXT("The plugin is found"), Plugin.IsValid()))
	{
		return false;
	}

	FAssetSkeletalMeshData Cube;
	FAssetSkeletalMeshData Chain;
	if (!ReadFixture(*this, TEXT("SKM_PhysCube.uasset"), Cube) || !ReadFixture(*this, TEXT("SKM_Chain_Template.uasset"), Chain) || !Cube.Render.bRead || !Chain.Render.bRead)
	{
		AddError(TEXT("The fixtures are not read"));
		return false;
	}

	// Two meshes: LODs added, and the first one differs in its buffers and its sections.
	{
		const TArray<FAssetNativeDataChange> Changes = AssetSkeletalMeshData::Compare(Cube, Chain);
		TestNotNull(TEXT("A LOD was added"), FindChange(Changes, TEXT("Render/Lod/1")));
		TestNotNull(TEXT("The buffers of the first LOD"), FindChange(Changes, TEXT("Render/Lod/0/Buffers")));
		TestNotNull(TEXT("A section was added to it"), FindChange(Changes, TEXT("Render/Lod/0/Section/1")));
	}

	// One byte of the vertices of a copy of the cube: only the buffers of that LOD change.
	const FString Fixtures = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("CookedTestFixtures"));
	const FString Folder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("__AssetSerializationInspectorTests"), TEXT("CookedSkeletal"));
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	IFileManager::Get().MakeDirectory(*Folder, true);
	for (const TCHAR* Extension : { TEXT("uasset"), TEXT("uexp") })
	{
		IFileManager::Get().Copy(*FPaths::Combine(Folder, FString::Printf(TEXT("SKM_PhysCube.%s"), Extension)), *FPaths::Combine(Fixtures, FString::Printf(TEXT("SKM_PhysCube.%s"), Extension)));
	}

	const int64 HeaderSize = IFileManager::Get().FileSize(*FPaths::Combine(Fixtures, TEXT("SKM_PhysCube.uasset")));
	TArray<uint8> Exports;
	if (!TestTrue(TEXT("The copy is there"), FFileHelper::LoadFileToArray(Exports, *FPaths::Combine(Folder, TEXT("SKM_PhysCube.uexp")))))
	{
		return false;
	}

	// The positions start 30 bytes into the buffers (the strip flags, the index buffer and the header of the positions).
	const int64 Position = Cube.Render.Lods[0].BufferOffset + 60 - HeaderSize;
	if (TestTrue(TEXT("The byte is in the .uexp"), Position >= 0 && Position < Exports.Num()))
	{
		Exports[Position] ^= 0x55;
		FFileHelper::SaveArrayToFile(Exports, *FPaths::Combine(Folder, TEXT("SKM_PhysCube.uexp")));

		FAssetSkeletalMeshData Changed;
		if (ReadMeshAt(*this, FPaths::Combine(Folder, TEXT("SKM_PhysCube.uasset")), Changed) && Changed.Render.bRead)
		{
			const TArray<FAssetNativeDataChange> Changes = AssetSkeletalMeshData::Compare(Cube, Changed);
			TestEqual(TEXT("One byte of the vertices is one change"), Changes.Num(), 1);
			TestNotNull(TEXT("Named by the LOD"), FindChange(Changes, TEXT("Render/Lod/0/Buffers")));
		}
		else
		{
			AddError(TEXT("The changed copy is not read"));
		}
	}

	// One thing at a time on a copy in memory.
	{
		FAssetSkeletalMeshData Other = Cube;
		Other.Render.Lods[0].Sections[0].MaxBoneInfluences += 1;
		const TArray<FAssetNativeDataChange> Changes = AssetSkeletalMeshData::Compare(Cube, Other);
		TestEqual(TEXT("A section is one change"), Changes.Num(), 1);
		TestNotNull(TEXT("Named by its LOD and place"), FindChange(Changes, TEXT("Render/Lod/0/Section/0")));
	}

	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
