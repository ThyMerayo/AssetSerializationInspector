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

	for (const FString& File : Meshes)
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
			TestTrue(TEXT("The imported model follows, and is not read"), Data.RemainingSize > 0);

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

#endif // WITH_DEV_AUTOMATION_TESTS
