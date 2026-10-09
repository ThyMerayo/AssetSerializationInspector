// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Serialization/AssetStaticMeshData.h"
#include "Serialization/AssetStructNativeData.h"

struct FAssetPackageDocument;
struct FAssetPackageExportEntry;

/** One bone of the reference skeleton of a skeletal mesh: its name, its parent and its pose. */
struct FAssetSkeletonBone
{
	FString Name;
	FString ParentName;

	/** The name the bone had in the file the mesh was imported from, when the package keeps it. */
	FString ExportName;

	/** The pose of the bone relative to its parent. */
	FString Pose;
};

/** One section of a LOD of a skeletal mesh (FSkelMeshSection): the triangles and vertices drawn with one material. */
struct FAssetSkeletalMeshSection
{
	int32 MaterialIndex = 0;
	uint32 BaseIndex = 0;
	uint32 NumTriangles = 0;
	uint32 BaseVertexIndex = 0;
	int32 NumVertices = 0;

	/** The bones the vertices of the section are skinned to, and the most bones that influence one vertex. */
	int32 BoneCount = 0;
	int32 MaxBoneInfluences = 0;

	bool bCastShadow = true;
	bool bVisibleInRayTracing = true;
	bool bRecomputeTangent = false;
	bool bDisabled = false;
	int32 GenerateUpToLodIndex = INDEX_NONE;

	/** How many vertices the editor model keeps for the section, and a hash of their bytes (positions, tangents, UVs, colors, skinning). */
	int32 VertexCount = 0;
	FString VertexHash;

	/** A hash of the bones of the section. */
	FString BoneMapHash;

	/** One line: material, triangles, vertices, bones, and the flags that are not the default. */
	FString Describe() const;
};

/** One level of detail of a skeletal mesh (FSkeletalMeshLODModel). */
struct FAssetSkeletalMeshLod
{
	TArray<FAssetSkeletalMeshSection> Sections;

	uint32 NumVertices = 0;
	uint32 NumTexCoords = 0;

	/** The index buffer of the LOD, as its number of indices and a hash of them. */
	int32 IndexCount = 0;
	FString IndexHash;

	int32 ActiveBoneCount = 0;
	int32 RequiredBoneCount = 0;

	/** The meshes the LOD was imported from: name and imported vertices. */
	TArray<FString> ImportedMeshes;

	/** One line: sections, vertices, triangles, UV channels and bones. */
	FString Describe() const;
};

/** One section of a LOD of the render data of a cooked skeletal mesh (FSkelMeshRenderSection). */
struct FAssetSkeletalRenderSection
{
	uint32 MaterialIndex = 0;
	uint32 BaseIndex = 0;
	uint32 NumTriangles = 0;
	uint32 BaseVertexIndex = 0;
	uint32 NumVertices = 0;
	uint32 MaxBoneInfluences = 0;
	bool bRecomputeTangent = false;
	bool bCastShadow = true;
	bool bVisibleInRayTracing = true;
	bool bUnifiedBoneMap = false;
	bool bDisabled = false;

	/** The bones the vertices are skinned to, as their number and a hash of their indices. */
	int32 BoneCount = 0;
	FString BoneMapHash;

	/** The cloth the section is mapped to: the asset index, and how many vertices of mapping data the LODs of the mapping have. */
	int32 ClothAssetIndex = INDEX_NONE;
	int64 ClothMappingVertices = 0;

	/** One line: material, triangles, vertices, bones and the flags that are not the default. */
	FString Describe() const;
};

/**
 * One LOD of the render data of a cooked skeletal mesh (FSkeletalMeshLODRenderData): its sections and bones, and the buffers (indices,
 * positions, tangents and UVs, colors, skin weights, cloth, morph targets...) that are kept as one block of the size the LOD records. The
 * block is hashed from the package or from the sidecar file it streams from; the counts come from the record of what the buffers hold.
 */
struct FAssetSkeletalRenderLod
{
	bool bCookedOut = false;
	bool bInlined = false;

	int32 RequiredBones = 0;
	int32 ActiveBones = 0;
	TArray<FAssetSkeletalRenderSection> Sections;

	/** The size of the buffers as the LOD records it, how they are stored (EBulkDataFlags) and a hash of them (empty when not read). */
	uint32 BuffersSize = 0;
	uint32 BulkFlags = 0;
	FString BufferHash;

	/** Where the buffers start in the document when they are inline, and INDEX_NONE when they stream from a sidecar file. */
	int64 BufferOffset = INDEX_NONE;

	/** What the buffers hold, from the record the LOD keeps. */
	bool bHasCounts = false;
	int32 NumIndices = 0;
	uint8 IndexBytes = 0;
	uint32 NumVertices = 0;
	uint32 NumTexCoords = 0;
	bool bFullPrecisionUVs = false;
	bool bHighPrecisionTangents = false;
	uint32 ColorVertices = 0;
	uint32 MaxBoneInfluences = 0;
	bool bVariableBonesPerVertex = false;
	bool b16BitBoneIndex = false;
	bool b16BitBoneWeight = false;
	uint32 ClothVertices = 0;
	int32 SkinWeightProfiles = 0;

	/** One line: sections, vertices, indices, UV channels, bone influences and the buffers. */
	FString Describe() const;
};

/** The render data of a cooked skeletal mesh (FSkeletalMeshRenderData): its LODs and its Nanite data. */
struct FAssetSkeletalRenderData
{
	bool bRead = false;
	TArray<FAssetSkeletalRenderLod> Lods;
	FAssetNaniteResources Nanite;
	uint8 NumInlinedLods = 0;
	uint8 NumNonOptionalLods = 0;
};

/**
 * What a skeletal mesh writes after its tagged properties, decoded (USkeletalMesh::Serialize): its bounds, its material slots, its
 * reference skeleton and its imported model (the LODs with their sections and vertices). The vertices are not interpreted; each
 * section keeps a hash of its vertex bytes, so a change of the geometry shows without reading it. The render data of a cooked mesh,
 * cloth data, skin weight profiles and the older layouts of the model are not read.
 */
struct FAssetSkeletalMeshData
{
	/** The bounds, the materials and the skeleton were read, and the counts of the skeleton agree. When false, Error says where it stopped. */
	bool bPrefixRead = false;

	/** The imported model was read too and the whole range was used. When false, ModelError says why not (or Error, if the start failed). */
	bool bComplete = false;
	FString Error;
	FString ModelError;

	/** The bytes the data covers in the document, and how many of them follow what was read. */
	int64 Offset = 0;
	int64 Size = 0;
	int64 RemainingSize = 0;

	FString ObjectGuid;

	/** The bounds the mesh had when it was imported, as text. */
	FString ImportedBounds;

	TArray<FAssetMeshMaterialSlot> Materials;
	TArray<FAssetSkeletonBone> Bones;

	TArray<FAssetSkeletalMeshLod> Lods;

	/** The render data of a cooked mesh (which has no imported model), when it was read. */
	FAssetSkeletalRenderData Render;

	/** The identifier of the model, and whether it is a hash of its content. */
	FString ModelGuid;

	/** A short account: how many material slots, bones and LODs. */
	FString Summarize() const;
};

namespace AssetSkeletalMeshData
{
	/**
	 * Decodes the native data of a skeletal mesh export, as far as it can be read: the start (bounds, materials, skeleton) and, when
	 * it is in a layout this reading knows, the imported model. Returns false, without touching Out, for any other export.
	 *
	 * @param NativeOffset Where the native data starts in the document (right after the tagged properties).
	 * @param NativeSize How many bytes it has.
	 */
	bool Decode(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, int64 NativeOffset, int64 NativeSize, FAssetSkeletalMeshData& Out);

	/**
	 * What differs between the material slots (matched by slot name), the bones (matched by name: added, removed, moved to another
	 * parent, or given another pose), the imported bounds, and when both sides have their model read, the LODs and their sections.
	 */
	TArray<FAssetNativeDataChange> Compare(const FAssetSkeletalMeshData& Old, const FAssetSkeletalMeshData& New);
} // namespace AssetSkeletalMeshData
