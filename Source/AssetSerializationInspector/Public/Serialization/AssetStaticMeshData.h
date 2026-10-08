// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Serialization/AssetStructNativeData.h"

struct FAssetPackageDocument;
struct FAssetPackageExportEntry;
struct FAssetSerializationTrace;

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

/** A part of the render data that is kept as bytes: where it is in the document, and a hash of them. */
struct FAssetRenderPart
{
	int64 Offset = 0;
	int64 Bytes = 0;
	FString Hash;
};

/** The Nanite resources of a mesh (Nanite::FResources), without the pages themselves. */
struct FAssetNaniteResources
{
	/** The mesh has Nanite data; false when the cook stripped it or the mesh has none. */
	bool bPresent = false;

	uint32 ResourceFlags = 0;
	int64 RootDataBytes = 0;
	int32 Pages = 0;
	int32 HierarchyNodes = 0;
	uint32 RootPages = 0;
	uint32 Clusters = 0;
	uint32 InputTriangles = 0;
	uint32 InputVertices = 0;
	uint32 InputCurves = 0;

	/** The pages that stream, in a sidecar file: their size and a hash of them when the file is there. */
	int64 StreamableBytes = 0;
	FString StreamableHash;

	FAssetRenderPart Part;

	FString Describe() const;
};

/** The ray tracing proxy of a mesh: whether it has one, and how many LODs it describes. */
struct FAssetRayTracingProxy
{
	bool bPresent = false;
	bool bUsingRenderingLods = false;
	int32 Lods = 0;
	FAssetRenderPart Part;

	FString Describe() const;
};

/** One card of a card representation: an oriented box that Lumen captures the surface of the mesh into (FLumenCardBuildData). */
struct FAssetCard
{
	FVector Origin = FVector::ZeroVector;
	FVector Extent = FVector::ZeroVector;
	FVector AxisX = FVector::ZeroVector;
	FVector AxisY = FVector::ZeroVector;
	FVector AxisZ = FVector::ZeroVector;

	/** The direction of the box when it is aligned to an axis of the mesh (0 to 5). */
	uint8 DirectionIndex = 0;

	bool Equals(const FAssetCard& Other) const;
	FString Describe() const;
};

/** The card representation Lumen uses for one LOD (FCardRepresentationData). */
struct FAssetCardRepresentation
{
	bool bValid = false;
	FVector BoundsMin = FVector::ZeroVector;
	FVector BoundsMax = FVector::ZeroVector;
	bool bMostlyTwoSided = false;
	int32 Cards = 0;
	TArray<FAssetCard> CardList;
	FAssetRenderPart Part;

	FString Describe() const;
};

/** The signed distance field of one LOD (FDistanceFieldVolumeData): its bounds, the bricks of each mip and the data that streams. */
struct FAssetDistanceField
{
	bool bValid = false;
	FVector BoundsMin = FVector::ZeroVector;
	FVector BoundsMax = FVector::ZeroVector;
	bool bMostlyTwoSided = false;

	struct FMip
	{
		FIntVector Indirection = FIntVector::ZeroValue;
		int32 Bricks = 0;
		uint32 BulkSize = 0;
	};
	FMip Mips[3];

	int32 AlwaysLoadedBytes = 0;
	int64 StreamableBytes = 0;
	FString StreamableHash;
	FAssetRenderPart Part;

	FString Describe() const;
};

/**
 * What lies between the LODs of the render data and its bounds, decoded where the layout is known: the Nanite resources, the ray tracing
 * proxy, then the card representation and the distance field of each LOD. When it cannot be decoded (a layout that is not read) bDecoded
 * is false with the reason, and the render data keeps a hash of all of it.
 */
struct FAssetStaticMeshRenderMiddle
{
	bool bDecoded = false;
	FString Error;

	FAssetNaniteResources Nanite;
	FAssetRayTracingProxy RayTracing;

	/** The cook left the cards or the distance fields out; the arrays are empty then. */
	bool bCardsStripped = false;
	bool bDistanceFieldsStripped = false;
	TArray<FAssetCardRepresentation> Cards;
	TArray<FAssetDistanceField> DistanceFields;
};

/**
 * The render data of a cooked static mesh (FStaticMeshRenderData): its LODs, then what follows them (Nanite resources, the ray tracing
 * proxy, the card representation and the distance fields of each LOD), which is decoded when its layout is known and hashed otherwise,
 * and the bounds and screen sizes.
 */
struct FAssetStaticMeshRenderData
{
	bool bRead = false;

	TArray<FAssetStaticMeshRenderLod> Lods;
	int32 NumInlinedLODs = 0;

	/** What lies between the LODs and the bounds: how many bytes, and a hash of them. */
	int64 OtherBytes = 0;
	FString OtherHash;

	/** The same bytes, decoded part by part. */
	FAssetStaticMeshRenderMiddle Middle;

	FBoxSphereBounds Bounds = FBoxSphereBounds(ForceInit);
	bool bLodsShareStaticLighting = false;
	bool bHasNaniteFallbackMesh = false;
	float ScreenSize[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
};

/**
 * A source model of a mesh saved by an editor from before the mesh description was an object of its own: the mesh description is
 * written inline, in the source model's slot of the static mesh.
 */
struct FAssetStaticMeshSourceModel
{
	bool bHasMeshDescription = false;

	/** The size of the mesh description, and a hash of the bytes as stored (comparable between two saves in the same format). */
	int64 PayloadSize = 0;
	FString PayloadHash;

	/** The identifier the mesh description was saved with; empty when the package does not keep one. */
	FString MeshGuid;

	FString Describe() const;
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

	/** The source models written inline by an older editor; empty for a package whose source models are objects of their own. */
	TArray<FAssetStaticMeshSourceModel> SourceModels;

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
	 * @param Trace The tagged properties of the export. A mesh saved by an older editor writes its source models inline, as many as the
	 *        SourceModels property has; without the trace such a mesh is not read.
	 */
	bool Decode(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, int64 NativeOffset, int64 NativeSize, FAssetStaticMeshData& Out,
		const FAssetSerializationTrace* Trace = nullptr);

	/**
	 * What differs between two versions: material slots added, removed or changed (matched by their slot name), the sockets, the
	 * collision and navigation objects, and the lighting GUID.
	 */
	TArray<FAssetNativeDataChange> Compare(const FAssetStaticMeshData& Old, const FAssetStaticMeshData& New);
} // namespace AssetStaticMeshData
