// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#include "Diff/AssetPackageDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Save/AssetSaveAnalyzer.h"
#include "Serialization/AssetSkeletalMeshData.h"
#include "Trace/AssetPackageFieldDecoder.h"
#include "Trace/AssetSerializationTrace.h"

namespace SkeletalMeshTestUtils
{
	/** The data a skeletal mesh writes after its tagged properties: the last range of its trace that no property accounts for. */
	static bool DecodeData(const FAssetPackageDocument& Document, const FAssetPackageTraceCollection& Traces, const FAssetPackageExportEntry& Export, FAssetSkeletalMeshData& Out)
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

		return Native != nullptr && AssetSkeletalMeshData::Decode(Document, Export, Export.SerialOffset + Native->Offset, Native->Size, Out);
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

	const auto Find = [](const TArray<FAssetNativeDataChange>& Changes, const TCHAR* Key) {
		return Changes.FindByPredicate([Key](const FAssetNativeDataChange& Change) { return Change.Key == Key; });
	};

	// A bone added at the end, and one that went away.
	{
		const TArray<FAssetNativeDataChange> Added = AssetSkeletalMeshData::Compare(Base,
			MakeMesh({ MakeBone(TEXT("root"), TEXT(""), TEXT("pose A")), MakeBone(TEXT("spine"), TEXT("root"), TEXT("pose B")), MakeBone(TEXT("head"), TEXT("spine"), TEXT("pose C")),
				MakeBone(TEXT("hat"), TEXT("head"), TEXT("pose D")) }));
		if (const FAssetNativeDataChange* Change = Find(Added, TEXT("Bone/hat")))
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
		if (const FAssetNativeDataChange* Change = Find(Removed, TEXT("Bone/head")))
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
		if (const FAssetNativeDataChange* Pose = Find(Changes, TEXT("Bone/spine")))
		{
			TestTrue(TEXT("The pose of the spine"), Pose->Title.Contains(TEXT("pose")));
		}
		if (const FAssetNativeDataChange* Parent = Find(Changes, TEXT("Bone/head")))
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

	const auto Find = [](const TArray<FAssetNativeDataChange>& Changes, const TCHAR* Key) {
		return Changes.FindByPredicate([Key](const FAssetNativeDataChange& Change) { return Change.Key == Key; });
	};

	// The vertices of a section moved (the hash changes, the counts do not): the geometry changed.
	{
		FAssetSkeletalMeshData Moved = Base;
		Moved.Lods[0].Sections[1].VertexHash = TEXT("cccc");
		Moved.ModelGuid = TEXT("22222222-2222-2222-2222-222222222222");

		const TArray<FAssetNativeDataChange> Changes = AssetSkeletalMeshData::Compare(Base, Moved);
		if (const FAssetNativeDataChange* Change = Find(Changes, TEXT("Lod/0/Section/1")))
		{
			TestEqual(TEXT("The section is modified"), static_cast<uint8>(Change->State), static_cast<uint8>(FAssetNativeDataChange::EState::Modified));
			TestTrue(TEXT("From the old vertex data"), Change->OldValue.Contains(TEXT("bbbb")));
			TestTrue(TEXT("To the new"), Change->NewValue.Contains(TEXT("cccc")));
		}
		else
		{
			AddError(TEXT("The change of the vertices is not reported"));
		}
		TestNull(TEXT("The other section is not"), Find(Changes, TEXT("Lod/0/Section/0")));
		TestNotNull(TEXT("The identifier of the model follows, since something changed"), Find(Changes, TEXT("ModelGuid")));
	}

	// A section added, a LOD added, and the index buffer of a LOD.
	{
		FAssetSkeletalMeshData Grown = Base;
		Grown.Lods[0].Sections.Add(MakeSection(2, 4, TEXT("dddd")));
		Grown.Lods[0].NumVertices += 4;
		Grown.Lods.Add(MakeLod({ MakeSection(6, 12, TEXT("eeee")) }, TEXT("jjjj")));

		const TArray<FAssetNativeDataChange> Changes = AssetSkeletalMeshData::Compare(Base, Grown);
		if (const FAssetNativeDataChange* Section = Find(Changes, TEXT("Lod/0/Section/2")))
		{
			TestEqual(TEXT("The new section is added"), static_cast<uint8>(Section->State), static_cast<uint8>(FAssetNativeDataChange::EState::Added));
		}
		else
		{
			AddError(TEXT("The added section is not reported"));
		}
		if (const FAssetNativeDataChange* Lod = Find(Changes, TEXT("Lod/1")))
		{
			TestEqual(TEXT("The new LOD is added"), static_cast<uint8>(Lod->State), static_cast<uint8>(FAssetNativeDataChange::EState::Added));
		}
		else
		{
			AddError(TEXT("The added LOD is not reported"));
		}
		TestNotNull(TEXT("The vertices of the LOD changed with the new section"), Find(Changes, TEXT("Lod/0/Whole")));
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

#endif // WITH_DEV_AUTOMATION_TESTS
