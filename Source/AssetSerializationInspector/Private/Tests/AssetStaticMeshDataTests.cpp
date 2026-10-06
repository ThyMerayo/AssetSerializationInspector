// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
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

		return Native != nullptr && AssetStaticMeshData::Decode(Document, Export, Export.SerialOffset + Native->Offset, Native->Size, Out);
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

#endif // WITH_DEV_AUTOMATION_TESTS
