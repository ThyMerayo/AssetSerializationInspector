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

/**
 * What a static mesh writes after its tagged properties, decoded (UStaticMesh::Serialize): its collision and navigation objects, its
 * lighting GUID, its sockets and its material slots. The geometry is not here: it is the mesh description a source model refers to, and
 * the render data of a cooked mesh.
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
