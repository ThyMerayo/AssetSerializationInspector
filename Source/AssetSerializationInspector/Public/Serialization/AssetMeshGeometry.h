// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Serialization/AssetBulkDataExport.h"
#include "Serialization/AssetStructNativeData.h"

struct FAssetPackageDocument;

/**
 * The geometry of a mesh description (the source geometry of a static mesh, as the editor keeps it in the package trailer): how many
 * vertices, triangles and polygons it has, its bounds, and the position of each vertex. The attributes other than the positions (normals,
 * UVs, materials, groups) are not kept.
 */
struct FAssetMeshGeometry
{
	/** The mesh description was found and deserialized. When false, Error says why not. */
	bool bLoaded = false;
	FString Error;

	int32 VertexCount = 0;
	int32 VertexInstanceCount = 0;
	int32 TriangleCount = 0;
	int32 PolygonCount = 0;

	FBox Bounds = FBox(ForceInit);

	/** The position of each vertex, by the index of the vertex in the mesh description. */
	TArray<FVector3f> Positions;
};

namespace AssetMeshGeometry
{
	/** Loads the geometry that the record of a mesh description refers to, when the package holds the data in its trailer. */
	FAssetMeshGeometry Load(const FAssetPackageDocument& Document, const FAssetBulkDataInfo& Bulk);

	/**
	 * Appends the change of the geometry between two meshes to Changes: the counts of vertices, triangles and polygons, the bounds, and
	 * when the vertices are the same in number, how many moved, the largest move, and where they are. Appends a note instead when one of
	 * the geometries could not be loaded, and nothing when they are the same.
	 */
	void AppendGeometryChange(const FAssetMeshGeometry& Old, const FAssetMeshGeometry& New, TArray<FAssetNativeDataChange>& Changes);
} // namespace AssetMeshGeometry
