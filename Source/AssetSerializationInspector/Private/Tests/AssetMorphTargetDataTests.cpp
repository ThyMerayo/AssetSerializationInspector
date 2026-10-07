// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Serialization/AssetMorphTargetData.h"
#include "Trace/AssetPackageFieldDecoder.h"
#include "Trace/AssetSerializationTrace.h"

namespace MorphTargetTestUtils
{
	/** The data a morph target writes after its tagged properties: the last range of its trace that no property accounts for. */
	static bool DecodeData(const FAssetPackageDocument& Document, const FAssetPackageTraceCollection& Traces, const FAssetPackageExportEntry& Export, FAssetMorphTargetData& Out)
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

		return Native != nullptr && AssetMorphTargetData::Decode(Document, Export, Export.SerialOffset + Native->Offset, Native->Size, Out);
	}

	static FAssetMorphTargetLod MakeLod(const int32 Deltas, const TCHAR* Hash)
	{
		FAssetMorphTargetLod Lod;
		Lod.DeltaCount = Deltas;
		Lod.DeltaHash = Hash;
		Lod.NumBaseMeshVerts = 1000;
		Lod.SectionCount = 2;
		return Lod;
	}

	static FAssetMorphTargetData MakeData(const TArray<FAssetMorphTargetLod>& Lods)
	{
		FAssetMorphTargetData Data;
		Data.bComplete = true;
		Data.Lods = Lods;
		return Data;
	}

	static const FAssetNativeDataChange* Find(const TArray<FAssetNativeDataChange>& Changes, const TCHAR* Key)
	{
		return Changes.FindByPredicate([Key](const FAssetNativeDataChange& Change) { return Change.Key == Key; });
	}
} // namespace MorphTargetTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetMorphTargetData_ReadsTheMorphTargetsOfAMesh, "AssetSerializationInspector.Serialization.AssetMorphTargetData.ReadsTheMorphTargetsOfAMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetMorphTargetData_ReadsTheMorphTargetsOfAMesh::RunTest(const FString& Parameters)
{
	using namespace MorphTargetTestUtils;

	// The groom mesh of the MetaHuman character: a skeletal mesh with hundreds of morph targets of its own.
	const FString File =
		FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("MetaHuman"), TEXT("MetaHumanCharacter"), TEXT("Content"), TEXT("Optional"), TEXT("Grooms"), TEXT("GroomMesh"), TEXT("MH_Groom_Head.uasset"));
	if (!IFileManager::Get().FileExists(*File))
	{
		AddInfo(TEXT("The engine does not have the MetaHuman groom mesh, so there is nothing to read."));
		return true;
	}

	FText Error;
	const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(File, Error);
	if (!TestTrue(TEXT("It loads"), Document.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);
	int32 Read = 0;
	int32 WithDeltas = 0;
	for (const FAssetPackageExportEntry& Export : Document->ExportMap)
	{
		FAssetMorphTargetData Data;
		if (!DecodeData(*Document, *Traces, Export, Data))
		{
			continue;
		}

		++Read;
		if (!TestTrue(FString::Printf(TEXT("%s is read to its last byte (%s)"), *Document->ResolveExportPath(Export.Index), *Data.Error), Data.bComplete))
		{
			break;
		}

		for (const FAssetMorphTargetLod& Lod : Data.Lods)
		{
			if (Lod.DeltaCount > 0 && !Lod.bDeltasStripped)
			{
				++WithDeltas;
				TestFalse(TEXT("A LOD that moves vertices has a hash of the deltas"), Lod.DeltaHash.IsEmpty());
			}
		}
	}

	TestTrue(TEXT("The mesh has morph targets"), Read > 0);
	TestTrue(TEXT("And some of them move vertices"), WithDeltas > 0);
	AddInfo(FString::Printf(TEXT("%d morph targets read, %d LODs with deltas"), Read, WithDeltas));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetMorphTargetData_ShowsAChangeOfTheDeltas, "AssetSerializationInspector.Serialization.AssetMorphTargetData.ShowsAChangeOfTheDeltas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetMorphTargetData_ShowsAChangeOfTheDeltas::RunTest(const FString& Parameters)
{
	using namespace MorphTargetTestUtils;

	const FAssetMorphTargetData Base = MakeData({ MakeLod(120, TEXT("aaaa")), MakeLod(60, TEXT("bbbb")) });
	TestTrue(TEXT("The same morph target is no change"), AssetMorphTargetData::Compare(Base, Base).IsEmpty());

	// A moved vertex: the same number of deltas, another hash.
	{
		FAssetMorphTargetData Moved = Base;
		Moved.Lods[1].DeltaHash = TEXT("cccc");

		const TArray<FAssetNativeDataChange> Changes = AssetMorphTargetData::Compare(Base, Moved);
		TestEqual(TEXT("One change"), Changes.Num(), 1);
		if (const FAssetNativeDataChange* Change = Find(Changes, TEXT("Lod/1/Deltas")))
		{
			TestTrue(TEXT("From the old hash"), Change->OldValue.Contains(TEXT("bbbb")));
			TestTrue(TEXT("To the new"), Change->NewValue.Contains(TEXT("cccc")));
		}
		else
		{
			AddError(TEXT("The change of the deltas is not reported"));
		}
	}

	// A LOD added, and another source file, base vertex count and section count on the first.
	{
		FAssetMorphTargetData Other = Base;
		Other.Lods[0].SourceFilename = TEXT("C:/art/face.fbx");
		Other.Lods[0].NumBaseMeshVerts = 1200;
		Other.Lods[0].SectionCount = 3;
		Other.Lods.Add(MakeLod(30, TEXT("dddd")));

		const TArray<FAssetNativeDataChange> Changes = AssetMorphTargetData::Compare(Base, Other);
		TestNotNull(TEXT("The source file"), Find(Changes, TEXT("Lod/0/Source")));
		TestNotNull(TEXT("The base vertices"), Find(Changes, TEXT("Lod/0/BaseVertices")));
		TestNotNull(TEXT("The sections"), Find(Changes, TEXT("Lod/0/Sections")));
		if (const FAssetNativeDataChange* Added = Find(Changes, TEXT("Lod/2")))
		{
			TestEqual(TEXT("The new LOD is added"), static_cast<uint8>(Added->State), static_cast<uint8>(FAssetNativeDataChange::EState::Added));
		}
		else
		{
			AddError(TEXT("The added LOD is not reported"));
		}
	}

	// Deltas a cook did not keep have no hash: only their number can differ.
	{
		FAssetMorphTargetData Cooked = Base;
		Cooked.Lods[0].DeltaHash.Empty();
		Cooked.Lods[0].bDeltasStripped = true;
		TestTrue(TEXT("The same number of deltas, one side without them, is no change"), AssetMorphTargetData::Compare(Base, Cooked).IsEmpty());
	}

	// A morph target that was not read says nothing.
	{
		FAssetMorphTargetData NotRead = Base;
		NotRead.bComplete = false;
		TestTrue(TEXT("Nothing is said of one that was not read"), AssetMorphTargetData::Compare(NotRead, Base).IsEmpty());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
