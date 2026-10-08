// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Serialization/AssetBulkDataExport.h"
#include "Serialization/AssetStructNativeData.h"

struct FAssetPackageDocument;

/**
 * The geometry of a mesh description (the source geometry of a static mesh, as the editor keeps it in the package trailer): how many
 * vertices, triangles and polygons it has, its bounds, the position of each vertex, the normals, tangents and UVs of its corners and the
 * material slot of each triangle, and the colors, binormal signs, edge hardness and polygon names. A mesh description has no smoothing groups:
 * those exist when a mesh is imported, and the hard edges they became are what it keeps.
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

	/** The normal and the tangent of each vertex instance (a corner of a polygon), by its index in the mesh description. */
	TArray<FVector3f> Normals;
	TArray<FVector3f> Tangents;

	/** The UVs of each channel for each vertex instance. */
	TArray<TArray<FVector2f>> UVs;

	/** The color and the sign of the binormal of each vertex instance, whether each edge is hard, and the object name of each polygon. */
	TArray<FVector4f> Colors;
	TArray<float> BinormalSigns;
	TArray<bool> EdgeHardness;
	TArray<FString> PolygonNames;

	/** The material slot name of each polygon group, and the slot name each triangle uses. */
	TArray<FString> MaterialSlots;
	TArray<FString> TriangleSlots;
};

namespace AssetMeshGeometry
{
	/** Loads the geometry that the record of a mesh description refers to, when the package holds the data in its trailer. */
	FAssetMeshGeometry Load(const FAssetPackageDocument& Document, const FAssetBulkDataInfo& Bulk);

	/**
	 * Appends the change of the geometry between two meshes to Changes: the counts of vertices, triangles and polygons, the bounds, and
	 * when the vertices are the same in number, how many moved, the largest move, and where they are; the same for the normals, the tangents and
	 * the UVs of the corners, and the material slots and which triangles use which. Appends a note instead when one of
	 * the geometries could not be loaded, and nothing when they are the same.
	 */
	void AppendGeometryChange(const FAssetMeshGeometry& Old, const FAssetMeshGeometry& New, TArray<FAssetNativeDataChange>& Changes);
} // namespace AssetMeshGeometry
