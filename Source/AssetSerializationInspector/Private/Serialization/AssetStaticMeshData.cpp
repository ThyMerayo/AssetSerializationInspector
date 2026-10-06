// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetStaticMeshData.h"

#include "Engine/StaticMesh.h"
#include "UObject/EditorObjectVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"
#include "UObject/ObjectVersion.h"
#include "UObject/RenderingObjectVersion.h"
#include "UObject/UE5MainStreamObjectVersion.h"

#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetNativeReader.h"
#include "Serialization/AssetSchemaReflection.h"

namespace
{
	constexpr int32 MaximumMeshEntries = 4096;

	FString MeshOptionalName(const FString& Name)
	{
		return Name == TEXT("None") ? FString() : Name;
	}

	FString MeshOptionalObject(const FString& Object)
	{
		return Object == TEXT("None") ? FString() : Object;
	}

	void ReadMaterialSlot(FNativeReader& Reader, const bool bEditorOnlyStripped, FAssetMeshMaterialSlot& Out)
	{
		Out.Material = MeshOptionalObject(Reader.ReadObject());
		Out.SlotName = MeshOptionalName(Reader.ReadName());
		if (!bEditorOnlyStripped)
		{
			Out.ImportedSlotName = MeshOptionalName(Reader.ReadName());
		}

		// FMeshUVChannelInfo: whether it was set, whether the densities are overridden, and the density of each of the four channels.
		if (Reader.CustomVer(FRenderingObjectVersion::GUID) >= FRenderingObjectVersion::TextureStreamingMeshUVChannelData)
		{
			Out.bHasUVDensities = Reader.ReadBool();
			Out.bOverrideDensities = Reader.ReadBool();
			for (float& Density : Out.UVDensities)
			{
				Density = Reader.Read<float>();
			}
		}

		if (Reader.CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::MeshMaterialSlotOverlayMaterialAdded)
		{
			Out.OverlayMaterial = MeshOptionalObject(Reader.ReadObject());
		}
	}

	FString SlotLabel(const FAssetMeshMaterialSlot& Material)
	{
		return Material.SlotName.IsEmpty() ? FString(TEXT("(unnamed)")) : Material.SlotName;
	}
} // namespace

FString FAssetMeshMaterialSlot::Describe() const
{
	FString Text = Material.IsEmpty() ? FString(TEXT("no material")) : Material;
	if (!OverlayMaterial.IsEmpty())
	{
		Text += FString::Printf(TEXT(", overlay %s"), *OverlayMaterial);
	}
	if (!ImportedSlotName.IsEmpty() && ImportedSlotName != SlotName)
	{
		Text += FString::Printf(TEXT(", imported as %s"), *ImportedSlotName);
	}
	if (bHasUVDensities && bOverrideDensities)
	{
		Text += FString::Printf(TEXT(", UV densities %g %g %g %g"), UVDensities[0], UVDensities[1], UVDensities[2], UVDensities[3]);
	}
	return Text;
}

FString FAssetStaticMeshData::Summarize() const
{
	return FString::Printf(TEXT("%d material slots, %d sockets"), Materials.Num(), Sockets.Num());
}

bool AssetStaticMeshData::Decode(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, const int64 NativeOffset, const int64 NativeSize, FAssetStaticMeshData& Out)
{
	const UClass* NativeClass = AssetSchemaReflection::FindNativeClass(Document, Export.Index);
	if (NativeClass == nullptr || !NativeClass->IsChildOf(UStaticMesh::StaticClass()) || NativeSize <= 0 || !Document.IsValidRange(NativeOffset, NativeSize))
	{
		return false;
	}

	Out = FAssetStaticMeshData();
	Out.Offset = NativeOffset;
	Out.Size = NativeSize;

	FNativeReader Reader(Document, NativeOffset, NativeSize);

	if (Reader.ReadBool())
	{
		Out.ObjectGuid = Reader.ReadGuid().ToString(EGuidFormats::DigitsWithHyphens);
	}

	// UStaticMesh::Serialize: strip flags, whether it is cooked, the collision and the navigation collision.
	const uint8 GlobalStripFlags = Reader.Read<uint8>();
	Reader.Read<uint8>();
	const bool bEditorDataStripped = (GlobalStripFlags & 1) != 0;
	Out.bCooked = Reader.ReadBool();
	Out.BodySetup = MeshOptionalObject(Reader.ReadObject());
	if (!Reader.UEVerBelow(VER_UE4_STATIC_MESH_STORE_NAV_COLLISION))
	{
		Out.NavCollision = MeshOptionalObject(Reader.ReadObject());
	}

	// What older editors wrote and newer ones only read and drop: a package saved by one still has it.
	if (!bEditorDataStripped)
	{
		if (Reader.UEVerBelow(VER_UE4_DEPRECATED_STATIC_MESH_THUMBNAIL_PROPERTIES_REMOVED))
		{
			Reader.Skip(3 * sizeof(float) + sizeof(float)); // the thumbnail angle and distance
		}

		if (Reader.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::DeprecatedHighResSourceMesh)
		{
			Reader.ReadString();   // the name of the high resolution source mesh
			Reader.Read<uint32>(); // and its CRC
		}
	}

	Out.LightingGuid = Reader.ReadGuid().ToString(EGuidFormats::DigitsWithHyphens);

	const int32 SocketCount = Reader.Read<int32>();
	if (Reader.Ok() && (SocketCount < 0 || SocketCount > MaximumMeshEntries || SocketCount > Reader.Remaining() / 4))
	{
		Reader.Fail(TEXT("The number of sockets does not fit the data"));
	}
	for (int32 Index = 0; Index < SocketCount && Reader.Ok(); ++Index)
	{
		Out.Sockets.Add(MeshOptionalObject(Reader.ReadObject()));
	}

	// The source models write nothing here once the mesh description is an object of its own; older layouts are not read.
	if (!bEditorDataStripped)
	{
		if (Reader.CustomVer(FEditorObjectVersion::GUID) < FEditorObjectVersion::StaticMeshDeprecatedRawMesh
			|| Reader.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::SerializeMeshDescriptionBase
			|| Reader.CustomVer(FEditorObjectVersion::GUID) < FEditorObjectVersion::UPropertryForMeshSection)
		{
			Reader.Fail(TEXT("The source models are stored in an older format"));
		}
	}

	if (Reader.Ok() && Out.bCooked)
	{
		Reader.Fail(TEXT("The render data of a cooked static mesh is not read"));
	}

	if (Reader.Ok() && !Reader.UEVerBelow(VER_UE4_SPEEDTREE_STATICMESH) && Reader.ReadBool())
	{
		Reader.Fail(TEXT("The SpeedTree wind data is not read"));
	}

	if (Reader.Ok() && Reader.CustomVer(FEditorObjectVersion::GUID) < FEditorObjectVersion::RefactorMeshEditorMaterials)
	{
		Reader.Fail(TEXT("The materials are stored in an older format"));
	}

	const bool bEditorOnlyStripped = (Document.PackageSummary.GetPackageFlags() & PKG_FilterEditorOnly) != 0;
	const int32 MaterialCount = Reader.Ok() ? Reader.Read<int32>() : 0;
	if (Reader.Ok() && (MaterialCount < 0 || MaterialCount > MaximumMeshEntries || MaterialCount > Reader.Remaining() / 8))
	{
		Reader.Fail(TEXT("The number of material slots does not fit the data"));
	}
	for (int32 Index = 0; Index < MaterialCount && Reader.Ok(); ++Index)
	{
		ReadMaterialSlot(Reader, bEditorOnlyStripped, Out.Materials.AddDefaulted_GetRef());
	}

	if (!Reader.Ok())
	{
		Out.Error = Reader.GetError();
	}
	else if (Reader.Remaining() != 0)
	{
		Out.Error = FString::Printf(TEXT("%lld bytes follow what this reading knows"), Reader.Remaining());
	}
	else
	{
		Out.bComplete = true;
	}

	return true;
}

TArray<FAssetNativeDataChange> AssetStaticMeshData::Compare(const FAssetStaticMeshData& Old, const FAssetStaticMeshData& New)
{
	TArray<FAssetNativeDataChange> Changes;

	const auto Add = [&Changes](const FString& Key, const FString& Title, const FAssetNativeDataChange::EState State, const FString& OldValue, const FString& NewValue) {
		FAssetNativeDataChange& Change = Changes.AddDefaulted_GetRef();
		Change.Key = Key;
		Change.Title = Title;
		Change.State = State;
		Change.OldValue = OldValue;
		Change.NewValue = NewValue;
	};

	// The material slots, matched by the name of the slot (a name that appears twice is matched in order).
	TArray<bool> Matched;
	Matched.Init(false, Old.Materials.Num());
	for (const FAssetMeshMaterialSlot& Slot : New.Materials)
	{
		int32 Before = INDEX_NONE;
		for (int32 Index = 0; Index < Old.Materials.Num(); ++Index)
		{
			if (!Matched[Index] && Old.Materials[Index].SlotName.Equals(Slot.SlotName, ESearchCase::CaseSensitive))
			{
				Before = Index;
				break;
			}
		}

		if (Before == INDEX_NONE)
		{
			Add(FString::Printf(TEXT("Material/%s"), *Slot.SlotName), FString::Printf(TEXT("Material slot %s"), *SlotLabel(Slot)), FAssetNativeDataChange::EState::Added, FString(), Slot.Describe());
			continue;
		}

		Matched[Before] = true;
		if (!Old.Materials[Before].Describe().Equals(Slot.Describe(), ESearchCase::CaseSensitive))
		{
			Add(FString::Printf(TEXT("Material/%s"), *Slot.SlotName), FString::Printf(TEXT("Material slot %s"), *SlotLabel(Slot)), FAssetNativeDataChange::EState::Modified,
				Old.Materials[Before].Describe(), Slot.Describe());
		}
	}

	for (int32 Index = 0; Index < Old.Materials.Num(); ++Index)
	{
		if (!Matched[Index])
		{
			Add(FString::Printf(TEXT("Material/%s"), *Old.Materials[Index].SlotName), FString::Printf(TEXT("Material slot %s"), *SlotLabel(Old.Materials[Index])),
				FAssetNativeDataChange::EState::Removed, Old.Materials[Index].Describe(), FString());
		}
	}

	// The sockets, the collision objects and the lighting GUID.
	for (const FString& Socket : New.Sockets)
	{
		if (!Old.Sockets.Contains(Socket))
		{
			Add(FString::Printf(TEXT("Socket/%s"), *Socket), FString::Printf(TEXT("Socket %s"), *Socket), FAssetNativeDataChange::EState::Added, FString(), Socket);
		}
	}
	for (const FString& Socket : Old.Sockets)
	{
		if (!New.Sockets.Contains(Socket))
		{
			Add(FString::Printf(TEXT("Socket/%s"), *Socket), FString::Printf(TEXT("Socket %s"), *Socket), FAssetNativeDataChange::EState::Removed, Socket, FString());
		}
	}

	if (!Old.BodySetup.Equals(New.BodySetup, ESearchCase::CaseSensitive))
	{
		Add(TEXT("BodySetup"), TEXT("Collision (body setup)"), FAssetNativeDataChange::EState::Modified, Old.BodySetup, New.BodySetup);
	}
	if (!Old.NavCollision.Equals(New.NavCollision, ESearchCase::CaseSensitive))
	{
		Add(TEXT("NavCollision"), TEXT("Navigation collision"), FAssetNativeDataChange::EState::Modified, Old.NavCollision, New.NavCollision);
	}
	if (!Old.LightingGuid.Equals(New.LightingGuid, ESearchCase::CaseSensitive))
	{
		Add(TEXT("LightingGuid"), TEXT("Lighting GUID"), FAssetNativeDataChange::EState::Modified, Old.LightingGuid, New.LightingGuid);
	}
	if (Old.bCooked != New.bCooked)
	{
		Add(TEXT("Cooked"), TEXT("Cooked"), FAssetNativeDataChange::EState::Modified, Old.bCooked ? TEXT("yes") : TEXT("no"), New.bCooked ? TEXT("yes") : TEXT("no"));
	}

	return Changes;
}
