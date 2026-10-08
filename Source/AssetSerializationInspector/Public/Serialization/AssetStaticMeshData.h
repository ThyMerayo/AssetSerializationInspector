// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Serialization/AssetStructNativeData.h"

struct FAssetPackageDocument;
struct FAssetPackageExportEntry;

/** One material slot of a static mesh (FStaticMaterial): which material it holds, and how the slot is called. */
struct FAssetMeshMaterialSlot
{
	FString Material;
	FString SlotName;

	/** The name the slot had in the source file the mesh was imported from; empty when the package does not keep it. */
	FString ImportedSlotName;

	/** The overlay material of the slot, when the package has it. */
	FString OverlayMaterial;

	/** The texture streaming density of each UV channel, when the slot has them. */
	bool bHasUVDensities = false;
	bool bOverrideDensities = false;
	float UVDensities[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	/** One line: the material, then the imported name and the densities when they say something. */
	FString Describe() const;
};

/** One section of a LOD of the render data of a cooked static mesh (FStaticMeshSection): a range of triangles drawn with one material. */
struct FAssetStaticMeshRenderSection
{
	int32 MaterialIndex = 0;
	int32 FirstIndex = 0;
	uint32 NumTriangles = 0;
	uint32 MinVertexIndex = 0;
	uint32 MaxVertexIndex = 0;
	bool bEnableCollision = false;
	bool bCastShadow = false;
	bool bForceOpaque = false;
	bool bVisibleInRayTracing = false;
	bool bAffectDistanceFieldLighting = false;

	/** "material 0, 12 triangles from index 0, vertices 0 to 23, collision, shadow". */
	FString Describe() const;
};

/** One LOD of the render data of a cooked static mesh (FStaticMeshLODResources): its sections, and the vertex and index buffers it keeps. */
struct FAssetStaticMeshRenderLod
{
	TArray<FAssetStaticMeshRenderSection> Sections;

	/** The bounds of the LOD (center, half extents, radius), and how far it deviates from the source LOD. */
	FBoxSphereBounds SourceMeshBounds = FBoxSphereBounds(ForceInit);
	float MaxDeviation = 0.0f;

	/** The LOD was left out by the cook (below the minimum LOD of the platform), and whether its buffers are in the export or stream from a sidecar file. */
	bool bCookedOut = false;
	bool bInlined = false;
	bool bHasRayTracingGeometry = false;

	/** The streamed buffers: how the block is stored (EBulkDataFlags), its size, and a hash of it when the sidecar file is next to the package. */
	uint32 BulkFlags = 0;
	int64 BufferBytes = 0;
	FString BufferHash;

	/** What the buffers hold, from the availability record: vertices, UV channels, positions and index counts. */
	uint32 NumVertices = 0;
	uint32 NumTexCoords = 0;
	bool bFullPrecisionUVs = false;
	bool bHighPrecisionTangents = false;
	uint32 PositionStride = 0;
	uint32 ColorVertices = 0;
	int32 NumIndices = 0;
	bool b32BitIndices = false;
	int32 ReversedIndices = 0;
	int32 DepthOnlyIndices = 0;
	int32 ReversedDepthOnlyIndices = 0;
	int32 WireframeIndices = 0;

	/** The bytes the cook says the serialized buffers, the depth only indices and the reversed indices take. */
	uint32 SerializedBuffersSize = 0;
	uint32 DepthOnlyIndexBytes = 0;
	uint32 ReversedIndexBytes = 0;

	FString Describe() const;
};

/**
 * The render data of a cooked static mesh (FStaticMeshRenderData): its LODs, then what follows them (Nanite resources, the ray tracing
 * proxy, the card representation and the distance fields of each LOD), which is hashed and not decoded, and the bounds and screen sizes.
 */
struct FAssetStaticMeshRenderData
{
	bool bRead = false;

	TArray<FAssetStaticMeshRenderLod> Lods;
	int32 NumInlinedLODs = 0;

	/** What lies between the LODs and the bounds: how many bytes, and a hash of them. */
	int64 OtherBytes = 0;
	FString OtherHash;

	FBoxSphereBounds Bounds = FBoxSphereBounds(ForceInit);
	bool bLodsShareStaticLighting = false;
	bool bHasNaniteFallbackMesh = false;
	float ScreenSize[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
};

/**
 * What a static mesh writes after its tagged properties, decoded (UStaticMesh::Serialize): its collision and navigation objects, its
 * lighting GUID, its sockets and its material slots, and for a cooked mesh its render data. The geometry of an uncooked mesh is not here:
 * it is the mesh description a source model refers to.
 */
struct FAssetStaticMeshData
{
	/** The whole range was read and nothing was left over. When false, Error says where it stopped. */
	bool bComplete = false;
	FString Error;

	/** The bytes the data covers in the document. */
	int64 Offset = 0;
	int64 Size = 0;

	FString ObjectGuid;
	bool bCooked = false;

	/** The collision (UBodySetup) and navigation collision objects of the mesh, as paths. */
	FString BodySetup;
	FString NavCollision;

	FString LightingGuid;
	TArray<FString> Sockets;
	TArray<FAssetMeshMaterialSlot> Materials;

	/** The render data of a cooked mesh, when it was read. */
	FAssetStaticMeshRenderData RenderData;

	/** A short account: how many slots and sockets. */
	FString Summarize() const;
};

namespace AssetStaticMeshData
{
	/**
	 * Decodes the native data of a static mesh export. Returns false, without touching Out, for any other export.
	 *
	 * @param NativeOffset Where the native data starts in the document (right after the tagged properties).
	 * @param NativeSize How many bytes it has.
	 */
	bool Decode(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, int64 NativeOffset, int64 NativeSize, FAssetStaticMeshData& Out);

	/**
	 * What differs between two versions: material slots added, removed or changed (matched by their slot name), the sockets, the
	 * collision and navigation objects, and the lighting GUID.
	 */
	TArray<FAssetNativeDataChange> Compare(const FAssetStaticMeshData& Old, const FAssetStaticMeshData& New);
} // namespace AssetStaticMeshData
