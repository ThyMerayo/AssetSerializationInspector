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
