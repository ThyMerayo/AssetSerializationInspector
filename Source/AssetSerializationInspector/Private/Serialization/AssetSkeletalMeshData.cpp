// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetSkeletalMeshData.h"

#include "Engine/SkeletalMesh.h"
#include "GPUSkinPublicDefs.h"
#include "MeshUVChannelInfo.h"
#include "Misc/SecureHash.h"
#include "SkeletalMeshLegacyCustomVersions.h"
#include "UObject/AnimObjectVersion.h"
#include "UObject/CoreObjectVersion.h"
#include "UObject/EditorObjectVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"
#include "UObject/ObjectVersion.h"
#include "UObject/ReleaseObjectVersion.h"
#include "UObject/RenderingObjectVersion.h"
#include "UObject/UE5MainStreamObjectVersion.h"
#include "UObject/UE5ReleaseStreamObjectVersion.h"

#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetLegacyBulkData.h"
#include "Serialization/AssetNativeReader.h"
#include "Serialization/AssetNumberText.h"
#include "Serialization/AssetRenderDataReaders.h"
#include "Serialization/AssetSchemaReflection.h"

using namespace AssetCookedBulkData;
using namespace AssetRenderDataReaders;

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

	/** A hash of a range of the document, short enough to read: what tells that bytes changed without saying what they hold. */
	FString HashRange(const FAssetPackageDocument& Document, const int64 Offset, const int64 Size)
	{
		if (Size <= 0 || !Document.IsValidRange(Offset, Size))
		{
			return FString();
		}

		return FSHA1::HashBuffer(Document.FileData.GetData() + Offset, static_cast<uint64>(Size)).ToString().Left(16);
	}

	/**
	 * How many bytes a vertex of a section takes in the file (FSoftSkinVertex): the position, the three tangents (the last has a fourth
	 * component, the handedness), the UVs, the color, the bones of the influences, and their weights, which are 16 bits since
	 * IncreasedSkinWeightPrecision and 8 bits before.
	 */
	int64 SoftVertexSize(const FNativeReader& Reader)
	{
		const int64 WeightSize = Reader.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::IncreasedSkinWeightPrecision ? 2 : 1;
		return 12 + 12 + 12 + 16 + MAX_TEXCOORDS * 8 + 4 + MAX_TOTAL_INFLUENCES * 2 + MAX_TOTAL_INFLUENCES * WeightSize;
	}

	/** Reads a count that must fit what is left of the data, as many elements of ElementSize bytes. */
	int32 ReadCount(FNativeReader& Reader, const int64 ElementSize, const TCHAR* What)
	{
		const int32 Count = Reader.Read<int32>();
		if (Reader.Ok() && (Count < 0 || Count > Reader.Remaining() / FMath::Max<int64>(ElementSize, 1)))
		{
			Reader.Fail(FString::Printf(TEXT("The number of %s does not fit the data"), What));
			return 0;
		}
		return Reader.Ok() ? Count : 0;
	}

	/** An array of fixed size elements that is only counted and hashed: its elements are not interpreted. */
	void SkipHashedArray(FNativeReader& Reader, const FAssetPackageDocument& Document, const int64 ElementSize, const TCHAR* What, int32& OutCount, FString& OutHash)
	{
		OutCount = ReadCount(Reader, ElementSize, What);
		const int64 Start = Reader.Tell();
		const int64 Bytes = static_cast<int64>(OutCount) * ElementSize;
		OutHash = Reader.Ok() ? HashRange(Document, Start, Bytes) : FString();
		Reader.Skip(Bytes);
	}

	/** FClothingSectionData: the GUID of the clothing asset and the LOD of it that the section uses. */
	void SkipClothingSectionData(FNativeReader& Reader)
	{
		Reader.ReadGuid();
		Reader.Read<int32>();
	}

	/** FSkelMeshSection (SkeletalMeshLODModel.cpp), in the layout since the clothing and the build refactors, and the versions of it since the ray tracing flag; older layouts are not read. */
	void ReadSection(FNativeReader& Reader, const FAssetPackageDocument& Document, FAssetSkeletalMeshSection& Out)
	{
		const uint8 GlobalStripFlags = Reader.Read<uint8>();
		Reader.Read<uint8>();
		if ((GlobalStripFlags & 2) != 0)
		{
			Reader.Fail(TEXT("A section was stripped of its render data"));
			return;
		}

		Out.MaterialIndex = Reader.Read<uint16>();
		Out.BaseIndex = Reader.Read<uint32>();
		Out.NumTriangles = Reader.Read<uint32>();
		Out.bRecomputeTangent = Reader.ReadBool();
		Reader.Read<uint8>(); // the vertex color channel to recompute the tangents from
		Out.bCastShadow = Reader.ReadBool();
		// A section is visible in ray tracing unless it says otherwise; before the flag existed it was not written.
		Out.bVisibleInRayTracing = Reader.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::SkelMeshSectionVisibleInRayTracingFlagAdded ? Reader.ReadBool() : true;
		Out.BaseVertexIndex = Reader.Read<uint32>();

		if ((GlobalStripFlags & 1) == 0)
		{
			SkipHashedArray(Reader, Document, SoftVertexSize(Reader), TEXT("vertices of a section"), Out.VertexCount, Out.VertexHash);
		}

		Reader.ReadBool(); // whether the bone indices are 16 bits
		SkipHashedArray(Reader, Document, 2, TEXT("bones of a section"), Out.BoneCount, Out.BoneMapHash);
		Out.NumVertices = Reader.Read<int32>();
		Out.MaxBoneInfluences = Reader.Read<int32>();

		// The cloth mapping of each LOD of the section: not read, and an error when there is some.
		// Before the LOD bias of the cloth was added there was a single mapping instead of one for each LOD.
		const bool bClothLodBias = Reader.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) >= FUE5ReleaseStreamObjectVersion::AddClothMappingLODBias;
		const int32 ClothLods = bClothLodBias ? ReadCount(Reader, 4, TEXT("cloth mapping levels")) : 1;
		for (int32 Index = 0; Index < ClothLods && Reader.Ok(); ++Index)
		{
			if (Reader.Read<int32>() != 0)
			{
				Reader.Fail(TEXT("The cloth mapping data of a section is not read"));
			}
		}

		Reader.Read<int16>(); // the index of the clothing asset
		SkipClothingSectionData(Reader);

		// The vertices that sit at the same place: a map from a vertex to the others.
		const int32 Overlaps = ReadCount(Reader, 8, TEXT("overlapping vertex entries"));
		for (int32 Index = 0; Index < Overlaps && Reader.Ok(); ++Index)
		{
			Reader.Read<int32>();
			const int32 Others = ReadCount(Reader, 4, TEXT("overlapping vertices"));
			Reader.Skip(static_cast<int64>(Others) * 4);
		}

		Out.bDisabled = Reader.ReadBool();
		Out.GenerateUpToLodIndex = Reader.Read<int32>();
		Reader.Read<int32>(); // the section of the original data
		Reader.Read<int32>(); // and the parent it was chunked from
	}

	/** FSkelMeshSourceSectionUserData, the editor's settings for a section (a map from a section to them). */
	void SkipUserSectionData(FNativeReader& Reader)
	{
		const uint8 GlobalStripFlags = Reader.Read<uint8>();
		Reader.Read<uint8>();
		if ((GlobalStripFlags & 1) != 0)
		{
			return;
		}

		Reader.ReadBool();	  // recompute tangents
		Reader.Read<uint8>(); // and the color channel it uses
		Reader.ReadBool();	  // casts shadows
		if (Reader.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::SkelMeshSectionVisibleInRayTracingFlagAdded)
		{
			Reader.ReadBool(); // visible in ray tracing
		}
		Reader.ReadBool();	  // disabled
		Reader.Read<int32>(); // generated up to this LOD
		Reader.Read<int16>(); // the clothing asset
		SkipClothingSectionData(Reader);
	}

	/** FSkeletalMeshLODModel::Serialize, for the layout since the model and the render data were split. */
	void ReadLod(FNativeReader& Reader, const FAssetPackageDocument& Document, FAssetSkeletalMeshLod& Out)
	{
		const uint8 GlobalStripFlags = Reader.Read<uint8>();
		Reader.Read<uint8>();
		const bool bEditorStripped = (GlobalStripFlags & 1) != 0;
		if ((GlobalStripFlags & 2) != 0)
		{
			Reader.Fail(TEXT("A LOD was stripped of its render data (a cooked mesh)"));
			return;
		}

		const int32 SectionCount = ReadCount(Reader, 8, TEXT("sections"));
		for (int32 Index = 0; Index < SectionCount && Reader.Ok(); ++Index)
		{
			ReadSection(Reader, Document, Out.Sections.AddDefaulted_GetRef());
		}

		if (!bEditorStripped)
		{
			const int32 UserSections = ReadCount(Reader, 8, TEXT("section settings"));
			for (int32 Index = 0; Index < UserSections && Reader.Ok(); ++Index)
			{
				Reader.Read<int32>();
				SkipUserSectionData(Reader);
			}

			SkipHashedArray(Reader, Document, 4, TEXT("indices"), Out.IndexCount, Out.IndexHash);
		}

		int32 ActiveBones = 0;
		FString Unused;
		SkipHashedArray(Reader, Document, 2, TEXT("active bones"), ActiveBones, Unused);
		Out.ActiveBoneCount = ActiveBones;

		if (!bEditorStripped && Reader.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::SkeletalMeshLODModelMeshInfo)
		{
			const int32 Meshes = ReadCount(Reader, 16, TEXT("imported meshes"));
			for (int32 Index = 0; Index < Meshes && Reader.Ok(); ++Index)
			{
				const FString Name = Reader.ReadName();
				const int32 Vertices = Reader.Read<int32>();
				Reader.Read<int32>();
				Out.ImportedMeshes.Add(FString::Printf(TEXT("%s (%d vertices)"), *Name, Vertices));
			}
		}

		Reader.Read<uint32>(); // a size the engine keeps for compatibility
		Out.NumVertices = Reader.Read<uint32>();

		int32 RequiredBones = 0;
		SkipHashedArray(Reader, Document, 2, TEXT("required bones"), RequiredBones, Unused);
		Out.RequiredBoneCount = RequiredBones;

		if (!bEditorStripped)
		{
			if (Reader.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) < FUE5ReleaseStreamObjectVersion::RemoveSkeletalMeshLODModelBulkDatas)
			{
				// Before the indices moved into the model they were a block of bulk data of their own.
				AssetLegacyBulkData::Read(Reader, Document);
			}
			else
			{
				int32 PointIndices = 0;
				SkipHashedArray(Reader, Document, 4, TEXT("raw point indices"), PointIndices, Unused);
			}
			Reader.ReadString(); // the id of the source data
			Reader.ReadBool();	 // whether the build data is available
			Reader.ReadBool();	 // whether the source data is empty
		}

		int32 ImportMap = 0;
		SkipHashedArray(Reader, Document, 4, TEXT("imported vertices"), ImportMap, Unused);
		Reader.Read<int32>(); // the highest imported vertex
		Out.NumTexCoords = Reader.Read<uint32>();

		if (Reader.Read<int32>() != 0)
		{
			Reader.Fail(TEXT("The skin weight profiles of a LOD are not read"));
		}
	}

	// The identifiers of these two custom versions are defined in SkeletalMesh.cpp and not exported by the engine, so they are repeated here.
	const FGuid RecomputeTangentVersionGuid(0x5579F886, 0x933A4C1F, 0x83BA087B, 0x6361B92F);
	const FGuid OverlappingVerticesVersionGuid(0x612FBE52, 0xDA53400B, 0x910D4F91, 0x9FB1857C);

	/** The first custom version the package has older than the layout of the imported model this reading knows needs, as its name; empty when it is the layout. */
	FString FirstOlderVersion(const FNativeReader& Reader)
	{
		struct FGate
		{
			const TCHAR* Name;
			const FGuid& Guid;
			int32 Required;
		};

		const FGate Gates[] = {
			{ TEXT("SkeletalMeshCustomVersion.RemoveEnableClothLOD"), FSkeletalMeshCustomVersion::GUID, FSkeletalMeshCustomVersion::RemoveEnableClothLOD },
			{ TEXT("EditorObjectVersion.SkeletalMeshMoveEditorSourceDataToPrivateAsset"), FEditorObjectVersion::GUID, FEditorObjectVersion::SkeletalMeshMoveEditorSourceDataToPrivateAsset },
			{ TEXT("FortniteMainBranchObjectVersion.AllowSkeletalMeshToReduceTheBaseLOD"), FFortniteMainBranchObjectVersion::GUID,
				FFortniteMainBranchObjectVersion::AllowSkeletalMeshToReduceTheBaseLOD },
			{ TEXT("RecomputeTangentCustomVersion.RecomputeTangentVertexColorMask"), RecomputeTangentVersionGuid, FRecomputeTangentCustomVersion::RecomputeTangentVertexColorMask },
			{ TEXT("OverlappingVerticesCustomVersion.DetectOVerlappingVertices"), OverlappingVerticesVersionGuid, FOverlappingVerticesCustomVersion::DetectOVerlappingVertices },
			{ TEXT("ReleaseObjectVersion.AddSkeletalMeshSectionDisable"), FReleaseObjectVersion::GUID, FReleaseObjectVersion::AddSkeletalMeshSectionDisable },
			{ TEXT("AnimObjectVersion.UnlimitedBoneInfluences"), FAnimObjectVersion::GUID, FAnimObjectVersion::UnlimitedBoneInfluences },
			{ TEXT("RenderingObjectVersion.IncreaseNormalPrecision"), FRenderingObjectVersion::GUID, FRenderingObjectVersion::IncreaseNormalPrecision },
		};

		TArray<FString> Older;
		for (const FGate& Gate : Gates)
		{
			const int32 Version = Reader.CustomVer(Gate.Guid);
			if (Version < Gate.Required)
			{
				Older.Add(FString::Printf(TEXT("%s (the package has %d)"), Gate.Name, Version));
			}
		}

		return FString::Join(Older, TEXT(", "));
	}

	/** Reads the imported model, then the rest of the data of the mesh, and says whether the whole range was used. */
	void ReadModel(FNativeReader& Reader, const FAssetPackageDocument& Document, FAssetSkeletalMeshData& Out)
	{
		const FString Older = FirstOlderVersion(Reader);
		if (!Older.IsEmpty())
		{
			Out.ModelError = FString::Printf(TEXT("The imported model is stored in an older layout: %s"), *Older);
			return;
		}

		// FSkeletalMeshModel::Serialize: strip flags, the LODs, the identifier of the model, and the reduction caches.
		Reader.Read<uint8>();
		Reader.Read<uint8>();

		const int32 LodCount = ReadCount(Reader, 8, TEXT("LODs"));
		for (int32 Index = 0; Index < LodCount && Reader.Ok(); ++Index)
		{
			ReadLod(Reader, Document, Out.Lods.AddDefaulted_GetRef());
		}

		Out.ModelGuid = Reader.ReadGuid().ToString(EGuidFormats::DigitsWithHyphens);
		Reader.ReadBool(); // whether the identifier is a hash

		if (Reader.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::ConvertReductionBaseSkeletalMeshBulkDataToInlineReductionCacheData)
		{
			// A block of bulk data for each LOD, kept for a reduction to start from.
			const int32 Caches = ReadCount(Reader, 20, TEXT("reduction sources"));
			for (int32 Index = 0; Index < Caches && Reader.Ok(); ++Index)
			{
				AssetLegacyBulkData::Read(Reader, Document);
			}
		}
		else
		{
			const int32 Caches = ReadCount(Reader, 8, TEXT("reduction caches"));
			Reader.Skip(static_cast<int64>(Caches) * 8);
		}

		// USkeletalMesh::Serialize again: whether it is cooked (its render data follows), and the objects it keeps.
		const bool bCooked = Reader.ReadBool();
		if (Reader.Ok() && bCooked)
		{
			Reader.Fail(TEXT("The render data of a cooked skeletal mesh is not read"));
		}

		const int32 Objects = ReadCount(Reader, 4, TEXT("objects"));
		Reader.Skip(static_cast<int64>(Objects) * 4);

		// The collision for per-polygon queries, when the mesh has it on.
		if (Reader.Ok() && Reader.Remaining() == 4)
		{
			Reader.ReadObject();
		}

		if (!Reader.Ok())
		{
			Out.ModelError = Reader.GetError();
			Out.Lods.Reset();
		}
		else if (Reader.Remaining() != 0)
		{
			Out.ModelError = FString::Printf(TEXT("%lld bytes follow what this reading knows"), Reader.Remaining());
			Out.Lods.Reset();
		}
		else
		{
			Out.bComplete = true;
		}
	}
} // namespace

FString FAssetSkeletalMeshSection::Describe() const
{
	FString Text = FString::Printf(
		TEXT("material %d, %u triangles, %d vertices, %d bones, vertex data %s"), MaterialIndex, NumTriangles, NumVertices, BoneCount, VertexHash.IsEmpty() ? TEXT("none") : *VertexHash);
	if (!bCastShadow)
	{
		Text += TEXT(", no shadow");
	}
	if (!bVisibleInRayTracing)
	{
		Text += TEXT(", not in ray tracing");
	}
	if (bRecomputeTangent)
	{
		Text += TEXT(", recomputes tangents");
	}
	if (bDisabled)
	{
		Text += TEXT(", disabled");
	}
	if (GenerateUpToLodIndex != INDEX_NONE)
	{
		Text += FString::Printf(TEXT(", generated up to LOD %d"), GenerateUpToLodIndex);
	}
	return Text;
}

FString FAssetSkeletalMeshLod::Describe() const
{
	return FString::Printf(TEXT("%d sections, %u vertices, %d indices, %u UV channels, %d bones"), Sections.Num(), NumVertices, IndexCount, NumTexCoords, RequiredBoneCount);
}

FString FAssetSkeletalRenderSection::Describe() const
{
	FString Text = FString::Printf(TEXT("material %u, %u triangles from index %u, %u vertices from %u, %d bones (%s), up to %u influences"), MaterialIndex, NumTriangles, BaseIndex, NumVertices,
		BaseVertexIndex, BoneCount, *BoneMapHash, MaxBoneInfluences);
	Text += bCastShadow ? FString() : FString(TEXT(", no shadow"));
	Text += bVisibleInRayTracing ? FString() : FString(TEXT(", hidden in ray tracing"));
	Text += bRecomputeTangent ? FString(TEXT(", recomputes tangents")) : FString();
	Text += bUnifiedBoneMap ? FString(TEXT(", unified bone map")) : FString();
	Text += bDisabled ? FString(TEXT(", disabled")) : FString();
	Text += ClothAssetIndex != INDEX_NONE ? FString::Printf(TEXT(", cloth asset %d (%lld mapping vertices)"), ClothAssetIndex, ClothMappingVertices) : FString();
	return Text;
}

FString FAssetSkeletalRenderLod::Describe() const
{
	if (bCookedOut)
	{
		return TEXT("left out by the cook");
	}

	FString Text = FString::Printf(TEXT("%d sections, %d required bones, %d active bones"), Sections.Num(), RequiredBones, ActiveBones);
	if (bHasCounts)
	{
		Text += FString::Printf(TEXT(", %u vertices, %d indices (%d bit), %u UV channels, up to %u bone influences%s"), NumVertices, NumIndices, IndexBytes * 8, NumTexCoords, MaxBoneInfluences,
			bVariableBonesPerVertex ? TEXT(" (a variable number per vertex)") : TEXT(""));
		Text += ColorVertices > 0 ? FString::Printf(TEXT(", %u vertex colors"), ColorVertices) : FString();
		Text += ClothVertices > 0 ? FString::Printf(TEXT(", %u cloth vertices"), ClothVertices) : FString();
		Text += SkinWeightProfiles > 0 ? FString::Printf(TEXT(", %d skin weight profiles"), SkinWeightProfiles) : FString();
	}
	Text += FString::Printf(TEXT(", %u bytes of buffers %s, %s"), BuffersSize, bInlined ? TEXT("in the export") : TEXT("streamed"), BufferHash.IsEmpty() ? TEXT("not read") : *BufferHash);
	return Text;
}

FString FAssetSkeletalMeshData::Summarize() const
{
	return FString::Printf(TEXT("%d material slots, %d bones, %d LODs"), Materials.Num(), Bones.Num(), Lods.Num());
}

namespace
{
	/** The custom version of the recomputed tangents (its GUID is not exported by the engine module). */
	const FGuid RecomputeTangentVersion(0x5579F886, 0x933A4C1F, 0x83BA087B, 0x6361B92F);

	/** FMeshToMeshVertData: three barycentric coordinates and distances, four source vertex indices, a weight and padding. */
	constexpr int64 ClothMappingBytes = 3 * 16 + 4 * 2 + 4 + 4;

	/** FSkelMeshRenderSection (operator<<), as a cooked package writes it. */
	void ReadSkeletalRenderSection(FNativeReader& Reader, const FAssetPackageDocument& Document, FAssetSkeletalRenderSection& Out, bool& bOutHasCloth)
	{
		constexpr uint8 DuplicatedVerticesStripped = 1;

		Reader.Read<uint8>(); // the strip flags of the section
		const uint8 ClassStrip = Reader.Read<uint8>();

		Out.MaterialIndex = Reader.Read<uint16>();
		Out.BaseIndex = Reader.Read<uint32>();
		Out.NumTriangles = Reader.Read<uint32>();
		Out.bRecomputeTangent = Reader.ReadBool();
		if (Reader.CustomVer(RecomputeTangentVersion) >= FRecomputeTangentCustomVersion::RecomputeTangentVertexColorMask)
		{
			Reader.Read<uint8>(); // which vertex color channel masks the recomputed tangents
		}
		Out.bCastShadow = Reader.ReadBool();
		if (Reader.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::SkelMeshSectionVisibleInRayTracingFlagAdded)
		{
			Out.bVisibleInRayTracing = Reader.ReadBool();
		}
		Out.BaseVertexIndex = Reader.Read<uint32>();

		// The mapping of the section to the cloth: one array before a LOD bias existed, an array of arrays after.
		bOutHasCloth = false;
		const int32 MappingArrays = Reader.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) < FUE5ReleaseStreamObjectVersion::AddClothMappingLODBias ? 1 : Reader.Read<int32>();
		if (Reader.Ok() && (MappingArrays < 0 || MappingArrays > 64))
		{
			Reader.Fail(TEXT("The number of cloth mappings of a section does not make sense"));
			return;
		}
		for (int32 Index = 0; Index < MappingArrays && Reader.Ok(); ++Index)
		{
			const int32 Vertices = SkipFixedArray(Reader, ClothMappingBytes, TEXT("cloth mapping vertices"));
			Out.ClothMappingVertices += Vertices;
			bOutHasCloth |= Index == 0 && Vertices > 0;
		}

		SkipHashedArray(Reader, Document, 2, TEXT("bones of a section"), Out.BoneCount, Out.BoneMapHash);

		Out.NumVertices = Reader.Read<uint32>();
		if (Reader.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::SkeletalMeshUnifiedBoneMap)
		{
			const uint32 Packed = Reader.Read<uint32>();
			Out.bUnifiedBoneMap = (Packed & (1u << 31)) != 0;
			Out.MaxBoneInfluences = Packed & ~(1u << 31);
		}
		else
		{
			Out.MaxBoneInfluences = Reader.Read<uint32>();
		}

		Out.ClothAssetIndex = Reader.Read<int16>();
		Reader.Skip(16 + 4); // the clothing data: the asset and the LOD of it
		if ((ClassStrip & DuplicatedVerticesStripped) == 0)
		{
			Reader.Fail(TEXT("The duplicated vertices of a section are not read"));
			return;
		}
		Out.bDisabled = Reader.ReadBool();
	}

	/** FSkeletalMeshLODRenderData::Serialize of a cooked LOD: the sections and bones, the block of buffers, and the record of what the buffers hold. */
	void ReadSkeletalRenderLod(FNativeReader& Reader, const FAssetPackageDocument& Document, const TArray<FDataResource>& Resources, FAssetSkeletalRenderLod& Out)
	{
		constexpr uint8 AudioVisualStripped = 2;

		const uint8 GlobalStrip = Reader.Read<uint8>();
		Reader.Read<uint8>();
		Out.bCookedOut = Reader.ReadBool();
		Out.bInlined = Reader.ReadBool();
		Out.RequiredBones = SkipFixedArray(Reader, 2, TEXT("required bones"));
		if (!Reader.Ok() || (GlobalStrip & AudioVisualStripped) != 0 || Out.bCookedOut)
		{
			return;
		}

		const int32 SectionCount = Reader.Read<int32>();
		if (Reader.Ok() && (SectionCount < 0 || SectionCount > 4096 || SectionCount > Reader.Remaining() / 20))
		{
			Reader.Fail(TEXT("The number of sections of a LOD does not fit the data"));
			return;
		}
		bool bHasCloth = false;
		for (int32 Index = 0; Index < SectionCount && Reader.Ok(); ++Index)
		{
			bool bSectionCloth = false;
			ReadSkeletalRenderSection(Reader, Document, Out.Sections.AddDefaulted_GetRef(), bSectionCloth);
			bHasCloth |= bSectionCloth;
		}
		Out.ActiveBones = SkipFixedArray(Reader, 2, TEXT("active bones"));
		Out.BuffersSize = Reader.Read<uint32>();
		if (!Reader.Ok())
		{
			return;
		}

		// The buffers are one block of bulk data: in the package, or in a sidecar file for a LOD that streams.
		FBulkReference Buffers;
		if (!ReadBulkReference(Reader, Document, Resources, TEXT("The buffers of a LOD"), Buffers))
		{
			return;
		}
		Out.BulkFlags = Buffers.Flags;
		Out.BufferHash = Buffers.PayloadHash;
		Out.BufferOffset = Out.bInlined ? Buffers.DataOffset : INDEX_NONE;
		if (Buffers.RawSize == 0)
		{
			return; // a LOD with no buffers keeps no record of them either
		}

		// The record of what the buffers hold (SerializeAvailabilityInfo).
		if (Reader.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) < FUE5ReleaseStreamObjectVersion::RemovingTessellation
			|| Reader.CustomVer(FAnimObjectVersion::GUID) < FAnimObjectVersion::UnlimitedBoneInfluences
			|| Reader.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::IncreasedSkinWeightPrecision)
		{
			Reader.Fail(TEXT("The record of the buffers of a LOD is in a layout older than unlimited bone influences"));
			return;
		}

		Out.bHasCounts = true;
		Out.IndexBytes = Reader.Read<uint8>();
		Out.NumIndices = Reader.Read<int32>();
		Out.NumTexCoords = Reader.Read<uint32>();
		Out.NumVertices = Reader.Read<uint32>();
		Out.bFullPrecisionUVs = Reader.ReadBool();
		Out.bHighPrecisionTangents = Reader.ReadBool();
		Reader.Read<uint32>(); // the stride of the positions
		Reader.Read<uint32>(); // and how many there are
		Reader.Read<uint32>(); // the stride of the colors
		Out.ColorVertices = Reader.Read<uint32>();
		Out.bVariableBonesPerVertex = Reader.ReadBool();
		Out.MaxBoneInfluences = Reader.Read<uint32>();
		Reader.Read<uint32>(); // the number of bone weights
		Reader.Read<uint32>(); // the vertices of the weights
		Out.b16BitBoneIndex = Reader.ReadBool();
		Out.b16BitBoneWeight = Reader.ReadBool();
		Reader.Read<uint32>(); // the vertices of the lookup of the weights
		if (bHasCloth)
		{
			SkipFixedArray(Reader, 12, TEXT("cloth index mappings"));
			Reader.Read<uint32>(); // the stride
			Out.ClothVertices = Reader.Read<uint32>();
		}
		const int32 Profiles = Reader.Read<int32>();
		if (Reader.Ok() && (Profiles < 0 || static_cast<int64>(Profiles) * 8 > Reader.Remaining()))
		{
			Reader.Fail(TEXT("The number of skin weight profiles does not fit the data"));
			return;
		}
		Out.SkinWeightProfiles = Profiles;
		Reader.Skip(static_cast<int64>(Profiles) * 8);
		Reader.Skip(6 * 4); // the header of the ray tracing geometry
	}

	/** FSkeletalMeshRenderData::Serialize of a cooked mesh: the LODs, the Nanite data and how many LODs are inlined. */
	void ReadSkeletalRenderData(FNativeReader& Reader, const FAssetPackageDocument& Document, FAssetSkeletalRenderData& Out, FString& OutError)
	{
		TArray<FDataResource> Resources;
		FString TableError;
		ReadDataResources(Document, Resources, TableError);
		if (!TableError.IsEmpty())
		{
			OutError = TableError;
			return;
		}

		const int32 LodCount = Reader.Read<int32>();
		if (Reader.Ok() && (LodCount < 0 || LodCount > 16))
		{
			Reader.Fail(TEXT("The number of LODs of the render data does not make sense"));
		}
		for (int32 Index = 0; Index < LodCount && Reader.Ok(); ++Index)
		{
			ReadSkeletalRenderLod(Reader, Document, Resources, Out.Lods.AddDefaulted_GetRef());
		}
		if (Reader.Ok())
		{
			ReadNaniteResources(Reader, Document, Resources, Out.Nanite);
		}
		Out.NumInlinedLods = Reader.Read<uint8>();
		Out.NumNonOptionalLods = Reader.Read<uint8>();
		if (!Reader.Ok())
		{
			OutError = Reader.GetError();
			return;
		}
		Out.bRead = true;
	}
} // namespace

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
	const uint8 GlobalStripFlags = Reader.Read<uint8>();
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

	// The imported model follows (and then the rest of the data of the mesh); the start above stays read when this cannot be.
	if ((GlobalStripFlags & 1) == 0)
	{
		ReadModel(Reader, Document, Out);
	}
	else if ((Document.PackageSummary.GetPackageFlags() & PKG_FilterEditorOnly) == 0 || Reader.CustomVer(FSkeletalMeshCustomVersion::GUID) < FSkeletalMeshCustomVersion::SplitModelAndRenderData)
	{
		Out.ModelError = TEXT("The editor data (the imported model) was stripped, and the render data is not in a layout that is read");
	}
	else
	{
		// A cooked mesh has no imported model: it writes whether it is cooked, and then its render data.
		const bool bCooked = Reader.ReadBool();
		if (!bCooked)
		{
			Out.ModelError = TEXT("The editor data (the imported model) was stripped and the mesh is not cooked");
		}
		else
		{
			FString RenderError;
			ReadSkeletalRenderData(Reader, Document, Out.Render, RenderError);
			if (!RenderError.IsEmpty())
			{
				Out.ModelError = RenderError;
			}
			else
			{
				// What follows is the list of objects a legacy mesh wrote (empty), and the end of the data.
				const int32 LegacyObjects = Reader.Read<int32>();
				Reader.Skip(static_cast<int64>(FMath::Max(LegacyObjects, 0)) * 4);
				if (!Reader.Ok())
				{
					Out.ModelError = Reader.GetError();
				}
				else if (Reader.Remaining() != 0)
				{
					Out.ModelError = FString::Printf(TEXT("%lld bytes follow what this reading knows"), Reader.Remaining());
				}
				else
				{
					Out.bComplete = true;
				}
			}
		}
	}
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

	// The LODs of the imported model, when both sides have it read: each LOD and its sections, matched by their place.
	if (Old.bComplete && New.bComplete)
	{
		const int32 LodCount = FMath::Max(Old.Lods.Num(), New.Lods.Num());
		for (int32 LodIndex = 0; LodIndex < LodCount; ++LodIndex)
		{
			const FString LodKey = FString::Printf(TEXT("Lod/%d"), LodIndex);
			const FString LodTitle = FString::Printf(TEXT("LOD %d"), LodIndex);
			if (!Old.Lods.IsValidIndex(LodIndex))
			{
				Add(LodKey, LodTitle, FAssetNativeDataChange::EState::Added, FString(), New.Lods[LodIndex].Describe());
				continue;
			}
			if (!New.Lods.IsValidIndex(LodIndex))
			{
				Add(LodKey, LodTitle, FAssetNativeDataChange::EState::Removed, Old.Lods[LodIndex].Describe(), FString());
				continue;
			}

			const FAssetSkeletalMeshLod& OldLod = Old.Lods[LodIndex];
			const FAssetSkeletalMeshLod& NewLod = New.Lods[LodIndex];
			const int32 SectionCount = FMath::Max(OldLod.Sections.Num(), NewLod.Sections.Num());
			for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
			{
				const FString SectionKey = FString::Printf(TEXT("Lod/%d/Section/%d"), LodIndex, SectionIndex);
				const FString SectionTitle = FString::Printf(TEXT("LOD %d, section %d"), LodIndex, SectionIndex);
				if (!OldLod.Sections.IsValidIndex(SectionIndex))
				{
					Add(SectionKey, SectionTitle, FAssetNativeDataChange::EState::Added, FString(), NewLod.Sections[SectionIndex].Describe());
				}
				else if (!NewLod.Sections.IsValidIndex(SectionIndex))
				{
					Add(SectionKey, SectionTitle, FAssetNativeDataChange::EState::Removed, OldLod.Sections[SectionIndex].Describe(), FString());
				}
				else if (!OldLod.Sections[SectionIndex].Describe().Equals(NewLod.Sections[SectionIndex].Describe(), ESearchCase::CaseSensitive))
				{
					Add(SectionKey, SectionTitle, FAssetNativeDataChange::EState::Modified, OldLod.Sections[SectionIndex].Describe(), NewLod.Sections[SectionIndex].Describe());
				}
			}

			// What the LOD holds as a whole: the vertices and UV channels, the index buffer and the bones.
			const FString OldWhole = FString::Printf(TEXT("%u vertices, %u UV channels, indices %s, %d active bones, %d required bones"), OldLod.NumVertices, OldLod.NumTexCoords, *OldLod.IndexHash,
				OldLod.ActiveBoneCount, OldLod.RequiredBoneCount);
			const FString NewWhole = FString::Printf(TEXT("%u vertices, %u UV channels, indices %s, %d active bones, %d required bones"), NewLod.NumVertices, NewLod.NumTexCoords, *NewLod.IndexHash,
				NewLod.ActiveBoneCount, NewLod.RequiredBoneCount);
			if (!OldWhole.Equals(NewWhole, ESearchCase::CaseSensitive))
			{
				Add(FString::Printf(TEXT("Lod/%d/Whole"), LodIndex), FString::Printf(TEXT("%s (vertices, indices, bones)"), *LodTitle), FAssetNativeDataChange::EState::Modified, OldWhole, NewWhole);
			}
		}

		if (!Old.ModelGuid.Equals(New.ModelGuid, ESearchCase::CaseSensitive) && !Changes.IsEmpty())
		{
			Add(TEXT("ModelGuid"), TEXT("Model identifier"), FAssetNativeDataChange::EState::Modified, Old.ModelGuid, New.ModelGuid);
		}
	}

	// The render data of a cooked mesh: each LOD with its sections and the block of its buffers, the Nanite data, and how many LODs are inlined.
	if (Old.Render.bRead && New.Render.bRead)
	{
		const FAssetSkeletalRenderData& OldRender = Old.Render;
		const FAssetSkeletalRenderData& NewRender = New.Render;
		const int32 LodCount = FMath::Max(OldRender.Lods.Num(), NewRender.Lods.Num());
		for (int32 LodIndex = 0; LodIndex < LodCount; ++LodIndex)
		{
			const FString LodKey = FString::Printf(TEXT("Render/Lod/%d"), LodIndex);
			const FString LodTitle = FString::Printf(TEXT("Render LOD %d"), LodIndex);
			if (!OldRender.Lods.IsValidIndex(LodIndex))
			{
				Add(LodKey, LodTitle, FAssetNativeDataChange::EState::Added, FString(), NewRender.Lods[LodIndex].Describe());
				continue;
			}
			if (!NewRender.Lods.IsValidIndex(LodIndex))
			{
				Add(LodKey, LodTitle, FAssetNativeDataChange::EState::Removed, OldRender.Lods[LodIndex].Describe(), FString());
				continue;
			}

			const FAssetSkeletalRenderLod& OldLod = OldRender.Lods[LodIndex];
			const FAssetSkeletalRenderLod& NewLod = NewRender.Lods[LodIndex];
			const int32 SectionCount = FMath::Max(OldLod.Sections.Num(), NewLod.Sections.Num());
			for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
			{
				const FString SectionKey = FString::Printf(TEXT("%s/Section/%d"), *LodKey, SectionIndex);
				const FString SectionTitle = FString::Printf(TEXT("%s, section %d"), *LodTitle, SectionIndex);
				if (!OldLod.Sections.IsValidIndex(SectionIndex))
				{
					Add(SectionKey, SectionTitle, FAssetNativeDataChange::EState::Added, FString(), NewLod.Sections[SectionIndex].Describe());
				}
				else if (!NewLod.Sections.IsValidIndex(SectionIndex))
				{
					Add(SectionKey, SectionTitle, FAssetNativeDataChange::EState::Removed, OldLod.Sections[SectionIndex].Describe(), FString());
				}
				else if (!OldLod.Sections[SectionIndex].Describe().Equals(NewLod.Sections[SectionIndex].Describe(), ESearchCase::CaseSensitive))
				{
					Add(SectionKey, SectionTitle, FAssetNativeDataChange::EState::Modified, OldLod.Sections[SectionIndex].Describe(), NewLod.Sections[SectionIndex].Describe());
				}
			}

			// The LOD as a whole: bones, the counts the buffers hold and the hash of the block. A hash that could not be read says nothing.
			const bool bSameHash = OldLod.BufferHash.IsEmpty() || NewLod.BufferHash.IsEmpty() || OldLod.BufferHash.Equals(NewLod.BufferHash, ESearchCase::CaseSensitive);
			const bool bSameBuffers = OldLod.bInlined == NewLod.bInlined && OldLod.BuffersSize == NewLod.BuffersSize && bSameHash;
			const auto Counts = [](const FAssetSkeletalRenderLod& Lod) {
				return FString::Printf(TEXT("%d/%d/%u/%d/%u/%u/%u/%d%d%d/%u/%d/%d"), Lod.RequiredBones, Lod.ActiveBones, Lod.NumVertices, Lod.NumIndices, Lod.NumTexCoords, Lod.MaxBoneInfluences,
					Lod.ColorVertices, Lod.bFullPrecisionUVs, Lod.bHighPrecisionTangents, Lod.bVariableBonesPerVertex, Lod.ClothVertices, Lod.bCookedOut, Lod.SkinWeightProfiles);
			};
			if (!bSameBuffers || !Counts(OldLod).Equals(Counts(NewLod)))
			{
				Add(FString::Printf(TEXT("%s/Buffers"), *LodKey), FString::Printf(TEXT("%s: bones and buffers"), *LodTitle), FAssetNativeDataChange::EState::Modified, OldLod.Describe(),
					NewLod.Describe());
			}
		}

		if (OldRender.Nanite.Part.Bytes != NewRender.Nanite.Part.Bytes || !OldRender.Nanite.Part.Hash.Equals(NewRender.Nanite.Part.Hash, ESearchCase::CaseSensitive))
		{
			Add(TEXT("Render/Nanite"), TEXT("Nanite resources"), FAssetNativeDataChange::EState::Modified, OldRender.Nanite.Describe(), NewRender.Nanite.Describe());
		}
		if (OldRender.NumInlinedLods != NewRender.NumInlinedLods || OldRender.NumNonOptionalLods != NewRender.NumNonOptionalLods)
		{
			Add(TEXT("Render/InlinedLods"), TEXT("LODs kept in the export"), FAssetNativeDataChange::EState::Modified,
				FString::Printf(TEXT("%d inlined, %d not optional"), OldRender.NumInlinedLods, OldRender.NumNonOptionalLods),
				FString::Printf(TEXT("%d inlined, %d not optional"), NewRender.NumInlinedLods, NewRender.NumNonOptionalLods));
		}
	}

	return Changes;
}
