// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetSkeletalMeshData.h"

#include "Engine/SkeletalMesh.h"
#include "UObject/CoreObjectVersion.h"
#include "UObject/EditorObjectVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"
#include "UObject/ObjectVersion.h"
#include "UObject/RenderingObjectVersion.h"

#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetNativeReader.h"
#include "Serialization/AssetNumberText.h"
#include "Serialization/AssetSchemaReflection.h"

namespace
{
	constexpr int32 MaximumSkeletalEntries = 20000;

	FString SkeletalOptionalName(const FString& Name)
	{
		return Name == TEXT("None") ? FString() : Name;
	}

	FString SkeletalOptionalObject(const FString& Object)
	{
		return Object == TEXT("None") ? FString() : Object;
	}

	/** A number the way the package writes it: a double since large world coordinates, a float before. */
	double ReadCoordinate(FNativeReader& Reader, const bool bDouble)
	{
		return bDouble ? Reader.Read<double>() : static_cast<double>(Reader.Read<float>());
	}

	FString ReadVector(FNativeReader& Reader, const bool bDouble)
	{
		const double X = ReadCoordinate(Reader, bDouble);
		const double Y = ReadCoordinate(Reader, bDouble);
		const double Z = ReadCoordinate(Reader, bDouble);
		return AssetNumberText::Text(FVector(X, Y, Z));
	}

	/** FSkeletalMaterial (SkinnedAssetCommon.cpp): the same slot a static mesh has, with the editor data kept only when a flag says so. */
	void ReadSkeletalMaterial(FNativeReader& Reader, FAssetMeshMaterialSlot& Out)
	{
		Out.Material = SkeletalOptionalObject(Reader.ReadObject());
		if (Reader.CustomVer(FEditorObjectVersion::GUID) >= FEditorObjectVersion::RefactorMeshEditorMaterials)
		{
			Out.SlotName = SkeletalOptionalName(Reader.ReadName());
			bool bImportedName = true;
			if (Reader.CustomVer(FCoreObjectVersion::GUID) >= FCoreObjectVersion::SkeletalMaterialEditorDataStripping)
			{
				bImportedName = Reader.ReadBool();
			}
			if (bImportedName)
			{
				Out.ImportedSlotName = SkeletalOptionalName(Reader.ReadName());
			}
		}
		else
		{
			Reader.Fail(TEXT("The material slots are stored in an older format"));
			return;
		}

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
			Out.OverlayMaterial = SkeletalOptionalObject(Reader.ReadObject());
		}
	}
} // namespace

FString FAssetSkeletalMeshData::Summarize() const
{
	return FString::Printf(TEXT("%d material slots, %d bones"), Materials.Num(), Bones.Num());
}

bool AssetSkeletalMeshData::Decode(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, const int64 NativeOffset, const int64 NativeSize, FAssetSkeletalMeshData& Out)
{
	const UClass* NativeClass = AssetSchemaReflection::FindNativeClass(Document, Export.Index);
	if (NativeClass == nullptr || !NativeClass->IsChildOf(USkeletalMesh::StaticClass()) || NativeSize <= 0 || !Document.IsValidRange(NativeOffset, NativeSize))
	{
		return false;
	}

	Out = FAssetSkeletalMeshData();
	Out.Offset = NativeOffset;
	Out.Size = NativeSize;

	FNativeReader Reader(Document, NativeOffset, NativeSize);

	if (Reader.ReadBool())
	{
		Out.ObjectGuid = Reader.ReadGuid().ToString(EGuidFormats::DigitsWithHyphens);
	}

	// USkeletalMesh::Serialize: strip flags, the imported bounds (box, sphere radius), the materials and the reference skeleton.
	Reader.Read<uint8>();
	Reader.Read<uint8>();

	const bool bDouble = Reader.UEVerAtLeast(EUnrealEngineObjectUE5Version::LARGE_WORLD_COORDINATES);
	const FString Origin = ReadVector(Reader, bDouble);
	const FString Extent = ReadVector(Reader, bDouble);
	const double Radius = ReadCoordinate(Reader, bDouble);
	Out.ImportedBounds = FString::Printf(TEXT("origin %s, extent %s, radius %s"), *Origin, *Extent, *AssetNumberText::Text(Radius));

	const int32 MaterialCount = Reader.Read<int32>();
	if (Reader.Ok() && (MaterialCount < 0 || MaterialCount > MaximumSkeletalEntries || MaterialCount > Reader.Remaining() / 8))
	{
		Reader.Fail(TEXT("The number of material slots does not fit the data"));
	}
	for (int32 Index = 0; Index < MaterialCount && Reader.Ok(); ++Index)
	{
		ReadSkeletalMaterial(Reader, Out.Materials.AddDefaulted_GetRef());
	}

	// FReferenceSkeleton: the bones (name, parent, and the name in the source file), their poses, and a map from name to bone.
	const bool bEditorOnlyStripped = (Document.PackageSummary.GetPackageFlags() & PKG_FilterEditorOnly) != 0;
	const int32 BoneCount = Reader.Ok() ? Reader.Read<int32>() : 0;
	if (Reader.Ok() && (BoneCount < 0 || BoneCount > MaximumSkeletalEntries || BoneCount > Reader.Remaining() / 12))
	{
		Reader.Fail(TEXT("The number of bones does not fit the data"));
	}

	TArray<int32> Parents;
	for (int32 Index = 0; Index < BoneCount && Reader.Ok(); ++Index)
	{
		FAssetSkeletonBone& Bone = Out.Bones.AddDefaulted_GetRef();
		Bone.Name = Reader.ReadName();
		Parents.Add(Reader.Read<int32>());
		if (!bEditorOnlyStripped && !Reader.UEVerBelow(VER_UE4_STORE_BONE_EXPORT_NAMES))
		{
			Bone.ExportName = Reader.ReadString();
		}
	}

	const int32 PoseCount = Reader.Ok() ? Reader.Read<int32>() : 0;
	if (Reader.Ok() && PoseCount != BoneCount)
	{
		Reader.Fail(TEXT("The reference skeleton has another number of poses than bones"));
	}
	for (int32 Index = 0; Index < PoseCount && Reader.Ok(); ++Index)
	{
		// FTransform: the rotation as a quaternion, the translation and the scale.
		const double Qx = ReadCoordinate(Reader, bDouble);
		const double Qy = ReadCoordinate(Reader, bDouble);
		const double Qz = ReadCoordinate(Reader, bDouble);
		const double Qw = ReadCoordinate(Reader, bDouble);
		const FString Translation = ReadVector(Reader, bDouble);
		const FString Scale = ReadVector(Reader, bDouble);
		if (Reader.Ok() && Out.Bones.IsValidIndex(Index))
		{
			Out.Bones[Index].Pose = FString::Printf(TEXT("translation %s, rotation (%s, %s, %s, %s), scale %s"), *Translation, *AssetNumberText::Text(Qx), *AssetNumberText::Text(Qy),
				*AssetNumberText::Text(Qz), *AssetNumberText::Text(Qw), *Scale);
		}
	}

	if (!Reader.UEVerBelow(VER_UE4_REFERENCE_SKELETON_REFACTOR))
	{
		const int32 MapCount = Reader.Ok() ? Reader.Read<int32>() : 0;
		if (Reader.Ok() && MapCount != BoneCount)
		{
			Reader.Fail(TEXT("The reference skeleton has another number of names than bones"));
		}
		for (int32 Index = 0; Index < MapCount && Reader.Ok(); ++Index)
		{
			Reader.ReadName();
			Reader.Read<int32>();
		}
	}

	if (!Reader.Ok())
	{
		Out.Error = Reader.GetError();
		return true;
	}

	// The bone a bone hangs from, by name.
	for (int32 Index = 0; Index < Out.Bones.Num(); ++Index)
	{
		const int32 Parent = Parents[Index];
		if (Parent >= Out.Bones.Num() || Parent < INDEX_NONE)
		{
			Out.Error = TEXT("A bone has a parent that is not a bone of the skeleton");
			return true;
		}
		Out.Bones[Index].ParentName = Parent == INDEX_NONE ? FString() : Out.Bones[Parent].Name;
	}

	Out.RemainingSize = Reader.Remaining();
	Out.bPrefixRead = true;
	return true;
}

TArray<FAssetNativeDataChange> AssetSkeletalMeshData::Compare(const FAssetSkeletalMeshData& Old, const FAssetSkeletalMeshData& New)
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

	// The material slots: the same comparison as a static mesh's, matched by the name of the slot.
	TArray<bool> MatchedSlot;
	MatchedSlot.Init(false, Old.Materials.Num());
	for (const FAssetMeshMaterialSlot& Slot : New.Materials)
	{
		int32 Before = INDEX_NONE;
		for (int32 Index = 0; Index < Old.Materials.Num(); ++Index)
		{
			if (!MatchedSlot[Index] && Old.Materials[Index].SlotName.Equals(Slot.SlotName, ESearchCase::CaseSensitive))
			{
				Before = Index;
				break;
			}
		}

		const FString Title = FString::Printf(TEXT("Material slot %s"), Slot.SlotName.IsEmpty() ? TEXT("(unnamed)") : *Slot.SlotName);
		if (Before == INDEX_NONE)
		{
			Add(FString::Printf(TEXT("Material/%s"), *Slot.SlotName), Title, FAssetNativeDataChange::EState::Added, FString(), Slot.Describe());
			continue;
		}

		MatchedSlot[Before] = true;
		if (!Old.Materials[Before].Describe().Equals(Slot.Describe(), ESearchCase::CaseSensitive))
		{
			Add(FString::Printf(TEXT("Material/%s"), *Slot.SlotName), Title, FAssetNativeDataChange::EState::Modified, Old.Materials[Before].Describe(), Slot.Describe());
		}
	}

	for (int32 Index = 0; Index < Old.Materials.Num(); ++Index)
	{
		if (!MatchedSlot[Index])
		{
			Add(FString::Printf(TEXT("Material/%s"), *Old.Materials[Index].SlotName), FString::Printf(TEXT("Material slot %s"), *Old.Materials[Index].SlotName),
				FAssetNativeDataChange::EState::Removed, Old.Materials[Index].Describe(), FString());
		}
	}

	// The bones, matched by name.
	TMap<FString, const FAssetSkeletonBone*> OldBones;
	for (const FAssetSkeletonBone& Bone : Old.Bones)
	{
		OldBones.Add(Bone.Name, &Bone);
	}

	const auto DescribeBone = [](const FAssetSkeletonBone& Bone) { return FString::Printf(TEXT("parent %s, %s"), Bone.ParentName.IsEmpty() ? TEXT("none") : *Bone.ParentName, *Bone.Pose); };

	TSet<FString> NewNames;
	for (const FAssetSkeletonBone& Bone : New.Bones)
	{
		NewNames.Add(Bone.Name);
		const FAssetSkeletonBone* const* Before = OldBones.Find(Bone.Name);
		if (Before == nullptr)
		{
			Add(FString::Printf(TEXT("Bone/%s"), *Bone.Name), FString::Printf(TEXT("Bone %s"), *Bone.Name), FAssetNativeDataChange::EState::Added, FString(), DescribeBone(Bone));
		}
		else if (!DescribeBone(**Before).Equals(DescribeBone(Bone), ESearchCase::CaseSensitive))
		{
			const bool bReparented = !(*Before)->ParentName.Equals(Bone.ParentName, ESearchCase::CaseSensitive);
			Add(FString::Printf(TEXT("Bone/%s"), *Bone.Name), FString::Printf(TEXT("Bone %s (%s)"), *Bone.Name, bReparented ? TEXT("parent") : TEXT("pose")), FAssetNativeDataChange::EState::Modified,
				DescribeBone(**Before), DescribeBone(Bone));
		}
	}

	for (const FAssetSkeletonBone& Bone : Old.Bones)
	{
		if (!NewNames.Contains(Bone.Name))
		{
			Add(FString::Printf(TEXT("Bone/%s"), *Bone.Name), FString::Printf(TEXT("Bone %s"), *Bone.Name), FAssetNativeDataChange::EState::Removed, DescribeBone(Bone), FString());
		}
	}

	if (!Old.ImportedBounds.Equals(New.ImportedBounds, ESearchCase::CaseSensitive))
	{
		Add(TEXT("ImportedBounds"), TEXT("Imported bounds"), FAssetNativeDataChange::EState::Modified, Old.ImportedBounds, New.ImportedBounds);
	}

	return Changes;
}
