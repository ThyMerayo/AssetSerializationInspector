// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetStaticMeshData.h"

#include "Engine/StaticMesh.h"
#include "UObject/EditorObjectVersion.h"
#include "UObject/EnterpriseObjectVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"
#include "UObject/ObjectVersion.h"
#include "UObject/RenderingObjectVersion.h"
#include "UObject/UE5MainStreamObjectVersion.h"
#include "UObject/UE5ReleaseStreamObjectVersion.h"

#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetCookedBulkData.h"
#include "Serialization/AssetLegacyBulkData.h"
#include "Serialization/AssetNativeReader.h"
#include "Serialization/AssetSchemaReflection.h"
#include "Trace/AssetSerializationTrace.h"

using namespace AssetCookedBulkData;

namespace
{
	constexpr int32 MaximumMeshEntries = 4096;

	/** How many elements the SourceModels property has, from the tagged properties of the export; INDEX_NONE when it cannot be told. */
	int32 CountSourceModels(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, const FAssetSerializationTrace* Trace)
	{
		if (Trace == nullptr || !Trace->Root.IsValid())
		{
			return INDEX_NONE;
		}

		for (const TSharedPtr<FAssetSerializationTraceNode>& Node : Trace->Root->Children)
		{
			if (Node.IsValid() && Node->Kind == EAssetSerializationTraceKind::Property && Node->Name == TEXT("SourceModels"))
			{
				// The elements are structs whose layout is the engine's own, so only their count is read: the first value of the array.
				if (Node->bIsZeroValue)
				{
					return 0;
				}

				FNativeReader Reader(Document, Export.SerialOffset + Node->Offset, Node->Size);
				const int32 Count = Reader.Read<int32>();
				return Reader.Ok() && Count >= 0 ? Count : INDEX_NONE;
			}
		}

		// A mesh without the property has no source model.
		return 0;
	}

	/**
	 * FStaticMeshSourceModel::SerializeBulkData of an editor from before the mesh description was an object: whether there is a mesh
	 * description, and then FMeshDescriptionBulkData, which is the bulk data, an identifier and whether the identifier is a hash.
	 */
	void ReadLegacySourceModel(FNativeReader& Reader, const FAssetPackageDocument& Document, FAssetStaticMeshSourceModel& Out)
	{
		Out.bHasMeshDescription = Reader.ReadBool();
		if (!Reader.Ok() || !Out.bHasMeshDescription)
		{
			return;
		}

		if (Reader.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::MeshDescriptionVirtualization)
		{
			Reader.Fail(TEXT("The source models hold a mesh description in a layout that is not read"));
			return;
		}

		const FAssetLegacyBulkData Bulk = AssetLegacyBulkData::Read(Reader, Document);
		Out.PayloadSize = Bulk.ElementCount;
		Out.PayloadHash = Bulk.PayloadHash;

		if (Reader.CustomVer(FEditorObjectVersion::GUID) >= FEditorObjectVersion::MeshDescriptionBulkDataGuid)
		{
			Out.MeshGuid = Reader.ReadGuid().ToString(EGuidFormats::DigitsWithHyphens);
		}
		if (Reader.CustomVer(FEnterpriseObjectVersion::GUID) >= FEnterpriseObjectVersion::MeshDescriptionBulkDataGuidIsHash)
		{
			Reader.ReadBool();
		}
	}

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

	/** How many bytes a material slot takes in the data (FStaticMaterial), by the versions of the package. */
	int64 MaterialSlotBytes(const FNativeReader& Reader, const bool bEditorOnlyStripped)
	{
		int64 Size = 4 + 8; // the material and the name of the slot
		if (!bEditorOnlyStripped)
		{
			Size += 8; // the name it was imported with
		}
		if (Reader.CustomVer(FRenderingObjectVersion::GUID) >= FRenderingObjectVersion::TextureStreamingMeshUVChannelData)
		{
			Size += 4 + 4 + 4 * 4; // FMeshUVChannelInfo
		}
		if (Reader.CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::MeshMaterialSlotOverlayMaterialAdded)
		{
			Size += 4; // the overlay material
		}
		return Size;
	}

	/** FBoxSphereBounds, in doubles: the center, the half extents and the radius of the sphere. */
	FBoxSphereBounds ReadRenderBounds(FNativeReader& Reader)
	{
		FBoxSphereBounds Bounds(ForceInit);
		Bounds.Origin.X = Reader.Read<double>();
		Bounds.Origin.Y = Reader.Read<double>();
		Bounds.Origin.Z = Reader.Read<double>();
		Bounds.BoxExtent.X = Reader.Read<double>();
		Bounds.BoxExtent.Y = Reader.Read<double>();
		Bounds.BoxExtent.Z = Reader.Read<double>();
		Bounds.SphereRadius = Reader.Read<double>();
		return Bounds;
	}

	bool RenderBoundsEqual(const FBoxSphereBounds& A, const FBoxSphereBounds& B)
	{
		return A.Origin.Equals(B.Origin, 1e-4) && A.BoxExtent.Equals(B.BoxExtent, 1e-4) && FMath::IsNearlyEqual(A.SphereRadius, B.SphereRadius, 1e-4);
	}

	FString DescribeRenderBounds(const FBoxSphereBounds& Bounds)
	{
		return FString::Printf(TEXT("center (%.2f, %.2f, %.2f), half extents (%.2f, %.2f, %.2f), radius %.2f"), Bounds.Origin.X, Bounds.Origin.Y, Bounds.Origin.Z, Bounds.BoxExtent.X,
			Bounds.BoxExtent.Y, Bounds.BoxExtent.Z, Bounds.SphereRadius);
	}

	/** FStaticMeshSection (StaticMesh.cpp). */
	void ReadRenderSection(FNativeReader& Reader, const bool bEditorOnlyStripped, FAssetStaticMeshRenderSection& Out)
	{
		Out.MaterialIndex = Reader.Read<int32>();
		Out.FirstIndex = Reader.Read<int32>();
		Out.NumTriangles = Reader.Read<uint32>();
		Out.MinVertexIndex = Reader.Read<uint32>();
		Out.MaxVertexIndex = Reader.Read<uint32>();
		Out.bEnableCollision = Reader.ReadBool();
		Out.bCastShadow = Reader.ReadBool();
		if (Reader.CustomVer(FRenderingObjectVersion::GUID) >= FRenderingObjectVersion::StaticMeshSectionForceOpaqueField)
		{
			Out.bForceOpaque = Reader.ReadBool();
		}
		if (!bEditorOnlyStripped)
		{
			Reader.Skip(8 * (4 + 4)); // the UV density and weight of each of the eight channels
		}
		Out.bVisibleInRayTracing = Reader.ReadBool();
		Out.bAffectDistanceFieldLighting = Reader.ReadBool();
	}

	/** An array written with BulkSerialize: the size of an element, the number of elements, then the elements. Returns how many there are. */
	int32 SkipRenderBulkArray(FNativeReader& Reader, const TCHAR* What)
	{
		const int32 ElementSize = Reader.Read<int32>();
		const int32 Count = Reader.Read<int32>();
		if (Reader.Ok() && (ElementSize <= 0 || Count < 0 || static_cast<int64>(ElementSize) * Count > Reader.Remaining()))
		{
			Reader.Fail(FString::Printf(TEXT("The %s of a LOD do not fit the data"), What));
			return 0;
		}
		Reader.Skip(static_cast<int64>(ElementSize) * Count);
		return Count;
	}

	/** FRawStaticIndexBuffer::Serialize: whether the indices are 32 bit, the indices as bytes, and whether they would be expanded to 32 bit. Returns the number of indices. */
	int32 SkipRenderIndexBuffer(FNativeReader& Reader, bool* bOut32Bit = nullptr)
	{
		const bool b32Bit = Reader.ReadBool();
		const int32 Bytes = SkipRenderBulkArray(Reader, TEXT("indices"));
		Reader.ReadBool();
		if (bOut32Bit != nullptr)
		{
			*bOut32Bit = b32Bit;
		}
		return Bytes / (b32Bit ? 4 : 2);
	}

	/** A weighted random sampler (FWeightedRandomSampler): the probabilities, the aliases and the total weight. */
	void SkipRenderSampler(FNativeReader& Reader)
	{
		for (int32 Array = 0; Array < 2; ++Array)
		{
			const int32 Count = Reader.Read<int32>();
			if (Reader.Ok() && (Count < 0 || static_cast<int64>(Count) * 4 > Reader.Remaining()))
			{
				Reader.Fail(TEXT("A sampler of a LOD does not fit the data"));
				return;
			}
			Reader.Skip(static_cast<int64>(Count) * 4);
		}
		Reader.Read<float>();
	}

	/**
	 * FStaticMeshLODResources::SerializeBuffers: the position, tangent and UV, and color vertex buffers, the index buffers (the LOD, reversed,
	 * depth only, reversed depth only and wireframe), the ray tracing geometry and the samplers that pick a point on the surface. The
	 * vertices and indices are counted and hashed, not listed.
	 */
	void ReadInlineBuffers(FNativeReader& Reader, const FAssetPackageDocument& Document, FAssetStaticMeshRenderLod& Out)
	{
		constexpr uint8 ReversedIndexBufferStripped = 4;
		constexpr uint8 RayTracingResourcesStripped = 8;
		constexpr uint8 EditorDataStripped = 1;
		constexpr uint8 AudioVisualStripped = 2;

		const int64 Start = Reader.Tell();
		const uint8 GlobalStripFlags = Reader.Read<uint8>();
		const uint8 ClassStripFlags = Reader.Read<uint8>();

		// The positions.
		Out.PositionStride = Reader.Read<uint32>();
		Reader.Read<uint32>();
		SkipRenderBulkArray(Reader, TEXT("positions"));

		// The tangents and the UVs.
		const uint8 VertexStrip = Reader.Read<uint8>();
		Reader.Read<uint8>();
		Out.NumTexCoords = Reader.Read<uint32>();
		Out.NumVertices = Reader.Read<uint32>();
		Out.bFullPrecisionUVs = Reader.ReadBool();
		Out.bHighPrecisionTangents = Reader.ReadBool();
		if ((VertexStrip & AudioVisualStripped) == 0)
		{
			SkipRenderBulkArray(Reader, TEXT("tangents"));
			SkipRenderBulkArray(Reader, TEXT("UVs"));
		}

		// The colors, when the mesh has any.
		const uint8 ColorStrip = Reader.Read<uint8>();
		Reader.Read<uint8>();
		Reader.Read<uint32>();
		Out.ColorVertices = Reader.Read<uint32>();
		if ((ColorStrip & AudioVisualStripped) == 0 && Out.ColorVertices > 0)
		{
			SkipRenderBulkArray(Reader, TEXT("colors"));
		}

		Out.NumIndices = SkipRenderIndexBuffer(Reader, &Out.b32BitIndices);
		if ((ClassStripFlags & ReversedIndexBufferStripped) == 0)
		{
			Out.ReversedIndices = SkipRenderIndexBuffer(Reader);
		}
		Out.DepthOnlyIndices = SkipRenderIndexBuffer(Reader);
		if ((ClassStripFlags & ReversedIndexBufferStripped) == 0)
		{
			Out.ReversedDepthOnlyIndices = SkipRenderIndexBuffer(Reader);
		}
		if ((GlobalStripFlags & EditorDataStripped) == 0)
		{
			Out.WireframeIndices = SkipRenderIndexBuffer(Reader);
		}

		if ((ClassStripFlags & RayTracingResourcesStripped) == 0)
		{
			Reader.Skip(6 * 4); // the header of the offline ray tracing data
			SkipRenderBulkArray(Reader, TEXT("ray tracing data"));
		}

		for (int32 Index = 0; Index < Out.Sections.Num() && Reader.Ok(); ++Index)
		{
			SkipRenderSampler(Reader);
		}
		SkipRenderSampler(Reader);

		if (Reader.Ok())
		{
			Out.BufferBytes = Reader.Tell() - Start;
			Out.BufferHash = HashBytes(Document.FileData.GetData() + Start, Out.BufferBytes);
		}

		Out.SerializedBuffersSize = Reader.Read<uint32>();
		Out.DepthOnlyIndexBytes = Reader.Read<uint32>();
		Out.ReversedIndexBytes = Reader.Read<uint32>();
	}

	/**
	 * FStaticMeshLODResources::Serialize for a cooked mesh: the sections and bounds, then, when the buffers stream from a sidecar file,
	 * the reference to them and the record of what they hold (SerializeAvailabilityInfo); when they are kept in the export, the buffers themselves.
	 */
	void ReadRenderLod(FNativeReader& Reader, const FAssetPackageDocument& Document, const TArray<FDataResource>& Resources, const bool bEditorOnlyStripped, FAssetStaticMeshRenderLod& Out)
	{
		constexpr uint8 AudioVisualStripped = 2;

		const uint8 GlobalStripFlags = Reader.Read<uint8>();
		Reader.Read<uint8>();

		const int32 SectionCount = Reader.Read<int32>();
		if (Reader.Ok() && (SectionCount < 0 || SectionCount > MaximumMeshEntries || SectionCount > Reader.Remaining() / 20))
		{
			Reader.Fail(TEXT("The number of sections of a LOD does not fit the data"));
			return;
		}
		for (int32 Index = 0; Index < SectionCount && Reader.Ok(); ++Index)
		{
			ReadRenderSection(Reader, bEditorOnlyStripped, Out.Sections.AddDefaulted_GetRef());
		}

		Out.SourceMeshBounds = ReadRenderBounds(Reader);
		Out.MaxDeviation = Reader.Read<float>();
		if (!bEditorOnlyStripped)
		{
			Reader.Fail(TEXT("The editor data of a LOD of the render data is not read"));
			return;
		}

		Out.bCookedOut = Reader.ReadBool();
		Out.bInlined = Reader.ReadBool();
		if ((GlobalStripFlags & AudioVisualStripped) != 0 || Out.bCookedOut || !Reader.Ok())
		{
			return;
		}

		Out.bHasRayTracingGeometry = Reader.ReadBool();
		if (Out.bInlined)
		{
			ReadInlineBuffers(Reader, Document, Out);
			return;
		}

		// The buffers stream from a sidecar file; the export keeps the reference to them.
		FBulkReference Buffers;
		if (!ReadBulkReference(Reader, Document, Resources, TEXT("The buffers of a LOD"), Buffers))
		{
			return;
		}
		Out.BulkFlags = Buffers.Flags;
		Out.BufferBytes = Buffers.RawSize;
		Out.BufferHash = Buffers.PayloadHash;

		if (Reader.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) < FUE5ReleaseStreamObjectVersion::RemovingTessellation)
		{
			Reader.Fail(TEXT("The availability record of a LOD is in a layout older than the removal of tessellation"));
			return;
		}

		// SerializeAvailabilityInfo: the number of depth only triangles, what the LOD has, and the metadata of each buffer.
		Reader.Read<uint32>();
		Reader.Read<uint32>();
		Out.NumTexCoords = Reader.Read<uint32>();
		Out.NumVertices = Reader.Read<uint32>();
		Out.bFullPrecisionUVs = Reader.ReadBool();
		Out.bHighPrecisionTangents = Reader.ReadBool();
		Out.PositionStride = Reader.Read<uint32>();
		Reader.Read<uint32>(); // the vertices of the position buffer
		Reader.Read<uint32>(); // the stride of the color buffer
		Out.ColorVertices = Reader.Read<uint32>();
		Out.NumIndices = Reader.Read<int32>();
		Out.b32BitIndices = Reader.ReadBool();
		Out.ReversedIndices = Reader.Read<int32>();
		Reader.ReadBool();
		Out.DepthOnlyIndices = Reader.Read<int32>();
		Reader.ReadBool();
		Out.ReversedDepthOnlyIndices = Reader.Read<int32>();
		Reader.ReadBool();
		Out.WireframeIndices = Reader.Read<int32>();
		Reader.ReadBool();
		Reader.Skip(6 * 4); // the header of the ray tracing geometry

		Out.SerializedBuffersSize = Reader.Read<uint32>();
		Out.DepthOnlyIndexBytes = Reader.Read<uint32>();
		Out.ReversedIndexBytes = Reader.Read<uint32>();
	}

	FAssetRenderPart MakePart(const FAssetPackageDocument& Document, const int64 Start, const int64 End)
	{
		FAssetRenderPart Part;
		Part.Offset = Start;
		Part.Bytes = End - Start;
		Part.Hash = HashBytes(Document.FileData.GetData() + Start, Part.Bytes);
		return Part;
	}

	/** An array of elements of a fixed size that is skipped: its count must fit what is left. */
	int32 SkipFixedArray(FNativeReader& Reader, const int64 ElementBytes, const TCHAR* What)
	{
		const int32 Count = Reader.Read<int32>();
		if (Reader.Ok() && (Count < 0 || static_cast<int64>(Count) * ElementBytes > Reader.Remaining()))
		{
			Reader.Fail(FString::Printf(TEXT("The number of %s does not fit the data"), What));
			return 0;
		}
		Reader.Skip(static_cast<int64>(Count) * ElementBytes);
		return Count;
	}

	/** Nanite::FResources::Serialize of a cooked mesh. */
	void ReadNaniteResources(FNativeReader& Reader, const FAssetPackageDocument& Document, const TArray<FDataResource>& Resources, FAssetNaniteResources& Out)
	{
		constexpr uint8 AudioVisualStripped = 2;
		const int64 Start = Reader.Tell();

		const uint8 GlobalStrip = Reader.Read<uint8>();
		Reader.Read<uint8>();
		if (Reader.Ok() && (GlobalStrip & AudioVisualStripped) == 0)
		{
			Out.bPresent = true;
			Out.ResourceFlags = Reader.Read<uint32>();

			FBulkReference Pages;
			if (!ReadBulkReference(Reader, Document, Resources, TEXT("The streamable pages of Nanite"), Pages))
			{
				return;
			}
			Out.StreamableBytes = Pages.RawSize;
			Out.StreamableHash = Pages.PayloadHash;

			Out.RootDataBytes = SkipFixedArray(Reader, 1, TEXT("bytes of Nanite root data"));
			Out.Pages = SkipFixedArray(Reader, 20, TEXT("Nanite page streaming states"));
			Out.HierarchyNodes = SkipFixedArray(Reader, 240, TEXT("Nanite hierarchy nodes"));
			SkipFixedArray(Reader, 4, TEXT("Nanite hierarchy roots"));
			SkipFixedArray(Reader, 2, TEXT("Nanite page dependencies"));
			SkipFixedArray(Reader, 48, TEXT("Nanite assembly transforms"));
			SkipFixedArray(Reader, 4, TEXT("Nanite bone attachments"));
			SkipFixedArray(Reader, 4, TEXT("Nanite bone indices"));
			SkipFixedArray(Reader, 4, TEXT("Nanite page ranges"));
			Reader.Skip(28); // the bounds of the mesh
			Out.RootPages = Reader.Read<uint32>();
			Reader.Skip(8); // the position and normal precision
			Out.InputTriangles = Reader.Read<uint32>();
			Out.InputVertices = Reader.Read<uint32>();
			Out.Clusters = Reader.Read<uint32>();
			Reader.Skip(8); // the mask of voxel materials
			Out.InputCurves = Reader.Read<uint32>();
		}

		Out.Part = MakePart(Document, Start, Reader.Tell());
	}

	/** FStaticMeshRayTracingProxy::Serialize of a cooked mesh whose proxy shares the buffers of the render LODs. */
	void ReadRayTracingProxy(FNativeReader& Reader, const FAssetPackageDocument& Document, const TArray<FDataResource>& Resources, const bool bEditorOnlyStripped, FAssetRayTracingProxy& Out)
	{
		constexpr uint8 AudioVisualStripped = 2;
		const int64 Start = Reader.Tell();

		Out.bPresent = Reader.ReadBool();
		if (!Reader.Ok() || !Out.bPresent)
		{
			Out.Part = MakePart(Document, Start, Reader.Tell());
			return;
		}

		const uint8 GlobalStrip = Reader.Read<uint8>();
		Reader.Read<uint8>();
		Out.bUsingRenderingLods = Reader.ReadBool();
		if (Reader.Ok() && (GlobalStrip & AudioVisualStripped) == 0)
		{
			Out.Lods = Reader.Read<int32>();
			if (Reader.Ok() && (Out.Lods < 0 || Out.Lods > 8))
			{
				Reader.Fail(TEXT("The number of LODs of the ray tracing proxy does not make sense"));
				return;
			}

			for (int32 Index = 0; Index < Out.Lods && Reader.Ok(); ++Index)
			{
				// A proxy that has buffers of its own (when the render LODs do not have them) lists its sections first.
				const bool bOwnsBuffers = Reader.ReadBool();
				if (bOwnsBuffers)
				{
					const int32 SectionCount = Reader.Read<int32>();
					if (Reader.Ok() && (SectionCount < 0 || SectionCount > MaximumMeshEntries || SectionCount > Reader.Remaining() / 20))
					{
						Reader.Fail(TEXT("The number of sections of the ray tracing proxy does not fit the data"));
						return;
					}
					FAssetStaticMeshRenderSection Section;
					for (int32 SectionIndex = 0; SectionIndex < SectionCount && Reader.Ok(); ++SectionIndex)
					{
						ReadRenderSection(Reader, bEditorOnlyStripped, Section);
					}
				}

				const bool bOwnsGeometry = Reader.ReadBool();
				const bool bInlined = Reader.ReadBool();
				if (bInlined)
				{
					Reader.Fail(TEXT("The ray tracing proxy is inline, which a cook does not write"));
					return;
				}

				FBulkReference Streamable;
				if (!ReadBulkReference(Reader, Document, Resources, TEXT("The data of a LOD of the ray tracing proxy"), Streamable))
				{
					return;
				}
				if (bOwnsBuffers)
				{
					// The size of the buffers, then the metadata of the positions, the tangents and UVs, the colors and the indices.
					Reader.Skip(4 + 2 * 4 + (2 * 4 + 2 * 4) + 2 * 4 + (4 + 4));
				}
				if (bOwnsGeometry)
				{
					Reader.Skip(8 + 6 * 4); // where the acceleration structure is, how big, and its header
				}
			}
		}

		Out.Part = MakePart(Document, Start, Reader.Tell());
	}

	/** The cards of every LOD: FStaticMeshRenderData::SerializeInlineDataRepresentations. */
	void ReadCardRepresentations(FNativeReader& Reader, const FAssetPackageDocument& Document, const int32 LodCount, FAssetStaticMeshRenderMiddle& Out)
	{
		constexpr uint8 AudioVisualStripped = 2;
		constexpr uint8 CardsStripped = 2;

		const uint8 GlobalStrip = Reader.Read<uint8>();
		const uint8 ClassStrip = Reader.Read<uint8>();
		if (!Reader.Ok() || (GlobalStrip & AudioVisualStripped) != 0 || (ClassStrip & CardsStripped) != 0)
		{
			Out.bCardsStripped = true;
			return;
		}

		for (int32 Lod = 0; Lod < LodCount && Reader.Ok(); ++Lod)
		{
			FAssetCardRepresentation& Cards = Out.Cards.AddDefaulted_GetRef();
			const int64 Start = Reader.Tell();
			Cards.bValid = Reader.ReadBool();
			if (Cards.bValid)
			{
				const double MinX = Reader.Read<double>();
				const double MinY = Reader.Read<double>();
				const double MinZ = Reader.Read<double>();
				const double MaxX = Reader.Read<double>();
				const double MaxY = Reader.Read<double>();
				const double MaxZ = Reader.Read<double>();
				Reader.Read<uint8>(); // whether the box is valid
				Cards.BoundsMin = FVector(MinX, MinY, MinZ);
				Cards.BoundsMax = FVector(MaxX, MaxY, MaxZ);
				Cards.bMostlyTwoSided = Reader.ReadBool();

				// Each card: three axes, the origin and the extent of its box, and the direction it faces.
				constexpr int64 CardBytes = 5 * 12 + 1;
				const int32 CardCount = Reader.Read<int32>();
				if (Reader.Ok() && (CardCount < 0 || static_cast<int64>(CardCount) * CardBytes > Reader.Remaining()))
				{
					Reader.Fail(TEXT("The number of cards does not fit the data"));
					return;
				}
				for (int32 CardIndex = 0; CardIndex < CardCount && Reader.Ok(); ++CardIndex)
				{
					float Values[15];
					for (float& Value : Values)
					{
						Value = Reader.Read<float>();
					}
					FAssetCard& Card = Cards.CardList.AddDefaulted_GetRef();
					Card.AxisX = FVector(Values[0], Values[1], Values[2]);
					Card.AxisY = FVector(Values[3], Values[4], Values[5]);
					Card.AxisZ = FVector(Values[6], Values[7], Values[8]);
					Card.Origin = FVector(Values[9], Values[10], Values[11]);
					Card.Extent = FVector(Values[12], Values[13], Values[14]);
					Card.DirectionIndex = Reader.Read<uint8>();
				}
				Cards.Cards = Cards.CardList.Num();
			}
			Cards.Part = MakePart(Document, Start, Reader.Tell());
		}
	}

	/** The distance field of every LOD: FDistanceFieldVolumeData::Serialize behind the strip flags of the render data. */
	void ReadDistanceFields(FNativeReader& Reader, const FAssetPackageDocument& Document, const TArray<FDataResource>& Resources, const int32 LodCount, FAssetStaticMeshRenderMiddle& Out)
	{
		constexpr uint8 AudioVisualStripped = 2;
		constexpr uint8 DistanceFieldStripped = 1;

		const uint8 GlobalStrip = Reader.Read<uint8>();
		const uint8 ClassStrip = Reader.Read<uint8>();
		if (!Reader.Ok() || (GlobalStrip & AudioVisualStripped) != 0 || (ClassStrip & DistanceFieldStripped) != 0)
		{
			Out.bDistanceFieldsStripped = true;
			return;
		}

		for (int32 Lod = 0; Lod < LodCount && Reader.Ok(); ++Lod)
		{
			FAssetDistanceField& Field = Out.DistanceFields.AddDefaulted_GetRef();
			const int64 Start = Reader.Tell();
			Field.bValid = Reader.ReadBool();
			if (Field.bValid)
			{
				float Corner[6];
				for (float& Value : Corner)
				{
					Value = Reader.Read<float>();
				}
				const FVector3f Min(Corner[0], Corner[1], Corner[2]);
				const FVector3f Max(Corner[3], Corner[4], Corner[5]);
				Reader.Read<uint8>(); // whether the box is valid
				Field.BoundsMin = FVector(Min);
				Field.BoundsMax = FVector(Max);
				Field.bMostlyTwoSided = Reader.ReadBool();

				for (FAssetDistanceField::FMip& Mip : Field.Mips)
				{
					const int32 IndirectionX = Reader.Read<int32>();
					const int32 IndirectionY = Reader.Read<int32>();
					const int32 IndirectionZ = Reader.Read<int32>();
					Mip.Indirection = FIntVector(IndirectionX, IndirectionY, IndirectionZ);
					Mip.Bricks = Reader.Read<int32>();
					Reader.Skip(3 * 4 + 3 * 4 + 2 * 4); // the scale and offset of the volume and the scale and bias of the distances
					Reader.Read<uint32>();				// where the mip is in the streamed data
					Mip.BulkSize = Reader.Read<uint32>();
				}

				Field.AlwaysLoadedBytes = SkipFixedArray(Reader, 1, TEXT("bytes of the always loaded mip of a distance field"));

				FBulkReference Streamable;
				if (!ReadBulkReference(Reader, Document, Resources, TEXT("The streamed mips of a distance field"), Streamable))
				{
					return;
				}
				Field.StreamableBytes = Streamable.RawSize;
				Field.StreamableHash = Streamable.PayloadHash;
			}
			Field.Part = MakePart(Document, Start, Reader.Tell());
		}
	}

	/**
	 * Decodes what lies between the LODs and the bounds. It never fails the render data: when the layout is not the one read here, or the
	 * bytes do not end where the bounds start, the reason is kept and the caller falls back to the hash of all of it.
	 */
	void ReadRenderMiddle(const FAssetPackageDocument& Document, const TArray<FDataResource>& Resources, const bool bEditorOnlyStripped, const int64 Start, const int64 End, const int32 LodCount,
		FAssetStaticMeshRenderMiddle& Out)
	{
		FNativeReader Reader(Document, Start, End - Start);
		ReadNaniteResources(Reader, Document, Resources, Out.Nanite);
		if (Reader.Ok())
		{
			ReadRayTracingProxy(Reader, Document, Resources, bEditorOnlyStripped, Out.RayTracing);
		}
		if (Reader.Ok())
		{
			ReadCardRepresentations(Reader, Document, LodCount, Out);
		}
		if (Reader.Ok())
		{
			ReadDistanceFields(Reader, Document, Resources, LodCount, Out);
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
			Out.bDecoded = true;
		}
	}

	/**
	 * FStaticMeshRenderData::Serialize for a cooked mesh. The LODs are read from the start. The end is found from the other side: what
	 * follows the render data is the SpeedTree flag and the material slots, whose size is known, so the render data ends where a flag
	 * and a count that matches the slots that follow it start. Between the LODs and the bounds lie the Nanite resources, the ray tracing
	 * proxy, the card representation and the distance fields; they are hashed, not decoded.
	 */
	void ReadRenderData(FNativeReader& Reader, const FAssetPackageDocument& Document, const int64 NativeEnd, const bool bEditorOnlyStripped, FAssetStaticMeshRenderData& Out)
	{
		constexpr int64 TailBytes =
			56 + 1 + 8 * 8 + 2; // the bounds, the flags, the screen size of each of the eight LODs (a flag and a float), and the flags of the data a cook keeps for a cooked cooker

		TArray<FDataResource> Resources;
		FString TableError;
		ReadDataResources(Document, Resources, TableError);
		if (!TableError.IsEmpty())
		{
			Reader.Fail(TableError);
			return;
		}

		// Where the render data ends: the largest number of slots whose flag and count are where the slots would put them.
		const int64 Start = Reader.Tell();
		const int64 SlotBytes = MaterialSlotBytes(Reader, bEditorOnlyStripped);
		int64 RenderEnd = INDEX_NONE;
		int32 SlotCount = 0;
		for (int32 Count = 0; Count <= MaximumMeshEntries; ++Count)
		{
			const int64 Position = NativeEnd - (8 + Count * SlotBytes);
			if (Position < Start + TailBytes)
			{
				break;
			}

			FNativeReader Probe(Document, Position, 8);
			const bool bSpeedTree = Probe.ReadBool();
			const int32 Slots = Probe.Read<int32>();
			if (Probe.Ok() && !bSpeedTree && Slots == Count)
			{
				RenderEnd = Position;
				SlotCount = Count;
			}
		}
		if (RenderEnd == INDEX_NONE)
		{
			Reader.Fail(TEXT("The end of the render data is not where the material slots would start"));
			return;
		}

		const int32 LodCount = Reader.Read<int32>();
		if (Reader.Ok() && (LodCount < 0 || LodCount > 8))
		{
			Reader.Fail(TEXT("The number of LODs of the render data does not make sense"));
		}
		for (int32 Index = 0; Index < LodCount && Reader.Ok(); ++Index)
		{
			ReadRenderLod(Reader, Document, Resources, bEditorOnlyStripped, Out.Lods.AddDefaulted_GetRef());
		}
		Out.NumInlinedLODs = Reader.Read<uint8>();
		if (!Reader.Ok())
		{
			return;
		}

		for (const FAssetStaticMeshRenderLod& Lod : Out.Lods)
		{
			for (const FAssetStaticMeshRenderSection& Section : Lod.Sections)
			{
				if (Section.MaterialIndex < 0 || Section.MaterialIndex >= SlotCount)
				{
					Reader.Fail(TEXT("A section uses a material slot that the mesh does not have"));
					return;
				}
			}
		}

		const int64 OtherStart = Reader.Tell();
		const int64 TailStart = RenderEnd - TailBytes;
		if (TailStart < OtherStart)
		{
			Reader.Fail(TEXT("The LODs of the render data end after its bounds start"));
			return;
		}

		Out.OtherBytes = TailStart - OtherStart;
		Out.OtherHash = HashBytes(Document.FileData.GetData() + OtherStart, Out.OtherBytes);
		ReadRenderMiddle(Document, Resources, bEditorOnlyStripped, OtherStart, TailStart, LodCount, Out.Middle);

		Reader.Seek(TailStart);
		Out.Bounds = ReadRenderBounds(Reader);
		const uint8 Flags = Reader.Read<uint8>();
		Out.bLodsShareStaticLighting = (Flags & 1) != 0;
		Out.bHasNaniteFallbackMesh = (Flags & 2) != 0;
		for (float& Size : Out.ScreenSize)
		{
			// A per platform value: whether it is the cooked one, and the value. Only the cooked form is read.
			if (!Reader.ReadBool() && Reader.Ok())
			{
				Reader.Fail(TEXT("A screen size is stored for each platform, which is not read"));
				return;
			}
			Size = Reader.Read<float>();
		}

		// A cook ends the render data with flags for the collision data a cooked cooker needs; the data itself is only there when they say so.
		constexpr uint8 NeededForCookingStripped = 4;
		const uint8 CookerFlags = Reader.Read<uint8>();
		Reader.Read<uint8>();
		if (Reader.Ok() && (CookerFlags & NeededForCookingStripped) == 0)
		{
			Reader.Fail(TEXT("The collision data kept for a cooked cooker is not read"));
			return;
		}

		if (Reader.Ok() && Reader.Tell() != RenderEnd)
		{
			Reader.Fail(TEXT("The bounds of the render data do not end where its material slots start"));
		}
		Out.bRead = Reader.Ok();
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

FString FAssetStaticMeshRenderSection::Describe() const
{
	FString Text = FString::Printf(TEXT("material %d, %u triangles from index %d, vertices %u to %u"), MaterialIndex, NumTriangles, FirstIndex, MinVertexIndex, MaxVertexIndex);
	Text += bEnableCollision ? TEXT(", collision") : TEXT("");
	Text += bCastShadow ? TEXT(", shadow") : TEXT("");
	Text += bForceOpaque ? TEXT(", forced opaque") : TEXT("");
	Text += bVisibleInRayTracing ? TEXT(", ray tracing") : TEXT("");
	Text += bAffectDistanceFieldLighting ? TEXT(", distance field lighting") : TEXT("");
	return Text;
}

FString FAssetStaticMeshRenderLod::Describe() const
{
	if (bCookedOut)
	{
		return TEXT("left out by the cook");
	}

	FString Text =
		FString::Printf(TEXT("%d sections, %u vertices, %d indices (%s), %u UV channels"), Sections.Num(), NumVertices, NumIndices, b32BitIndices ? TEXT("32 bit") : TEXT("16 bit"), NumTexCoords);
	Text += FString::Printf(TEXT(", %lld bytes of buffers %s, %s"), BufferBytes, bInlined ? TEXT("in the export") : TEXT("streamed"), BufferHash.IsEmpty() ? TEXT("not read") : *BufferHash);
	return Text;
}

FString FAssetNaniteResources::Describe() const
{
	if (!bPresent)
	{
		return TEXT("none");
	}
	return FString::Printf(TEXT("%u clusters in %d pages (%u root), %u input triangles and %u vertices, %lld bytes streamed %s, %s"), Clusters, Pages, RootPages, InputTriangles, InputVertices,
		StreamableBytes, StreamableHash.IsEmpty() ? TEXT("not read") : *StreamableHash.Left(16), *Part.Hash.Left(16));
}

FString FAssetRayTracingProxy::Describe() const
{
	return bPresent ? FString::Printf(TEXT("%d LODs%s, %s"), Lods, bUsingRenderingLods ? TEXT(" sharing the render buffers") : TEXT(""), *Part.Hash.Left(16)) : FString(TEXT("none"));
}

bool FAssetCard::Equals(const FAssetCard& Other) const
{
	return DirectionIndex == Other.DirectionIndex && Origin.Equals(Other.Origin, 1e-4) && Extent.Equals(Other.Extent, 1e-4) && AxisX.Equals(Other.AxisX, 1e-5) && AxisY.Equals(Other.AxisY, 1e-5)
		&& AxisZ.Equals(Other.AxisZ, 1e-5);
}

FString FAssetCard::Describe() const
{
	// The directions are the six sides of the box of the mesh, in the order -X, +X, -Y, +Y, -Z, +Z.
	static const TCHAR* const Directions[6] = { TEXT("-X"), TEXT("+X"), TEXT("-Y"), TEXT("+Y"), TEXT("-Z"), TEXT("+Z") };
	return FString::Printf(TEXT("faces %s, origin (%s), extent (%s)"), DirectionIndex < 6 ? Directions[DirectionIndex] : TEXT("no side"), *Origin.ToCompactString(), *Extent.ToCompactString());
}

FString FAssetCardRepresentation::Describe() const
{
	return bValid ? FString::Printf(TEXT("%d cards in (%s) to (%s)%s, %s"), Cards, *BoundsMin.ToCompactString(), *BoundsMax.ToCompactString(), bMostlyTwoSided ? TEXT(", mostly two sided") : TEXT(""),
						*Part.Hash.Left(16))
				  : FString(TEXT("none"));
}

FString FAssetDistanceField::Describe() const
{
	if (!bValid)
	{
		return TEXT("none");
	}
	return FString::Printf(TEXT("%d, %d and %d bricks, %d bytes always loaded, %lld bytes streamed %s, bounds (%s) to (%s), %s"), Mips[0].Bricks, Mips[1].Bricks, Mips[2].Bricks, AlwaysLoadedBytes,
		StreamableBytes, StreamableHash.IsEmpty() ? TEXT("not read") : *StreamableHash.Left(16), *BoundsMin.ToCompactString(), *BoundsMax.ToCompactString(), *Part.Hash.Left(16));
}

FString FAssetStaticMeshSourceModel::Describe() const
{
	return bHasMeshDescription ? FString::Printf(TEXT("%s (%lld bytes)"), PayloadHash.IsEmpty() ? TEXT("none") : *PayloadHash.Left(16), PayloadSize) : FString(TEXT("no mesh description"));
}

FString FAssetStaticMeshData::Summarize() const
{
	return FString::Printf(TEXT("%d material slots, %d sockets"), Materials.Num(), Sockets.Num());
}

bool AssetStaticMeshData::Decode(
	const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, const int64 NativeOffset, const int64 NativeSize, FAssetStaticMeshData& Out, const FAssetSerializationTrace* Trace)
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

	// The source models write nothing here once the mesh description is an object of its own. Before that each wrote its mesh description
	// inline, and before the raw mesh went away a raw mesh instead; the latter and the section info map of the oldest packages are not read.
	if (!bEditorDataStripped)
	{
		if (Reader.CustomVer(FEditorObjectVersion::GUID) < FEditorObjectVersion::StaticMeshDeprecatedRawMesh
			|| Reader.CustomVer(FEditorObjectVersion::GUID) < FEditorObjectVersion::UPropertryForMeshSection)
		{
			Reader.Fail(TEXT("The source models are stored in an older format"));
		}
		else if (Reader.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::SerializeMeshDescriptionBase)
		{
			const int32 SourceModelCount = CountSourceModels(Document, Export, Trace);
			if (SourceModelCount == INDEX_NONE || SourceModelCount > MaximumMeshEntries)
			{
				Reader.Fail(TEXT("The number of source models is not known"));
			}
			for (int32 Index = 0; Index < SourceModelCount && Reader.Ok(); ++Index)
			{
				ReadLegacySourceModel(Reader, Document, Out.SourceModels.AddDefaulted_GetRef());
			}
		}
	}

	const bool bEditorOnlyStripped = (Document.PackageSummary.GetPackageFlags() & PKG_FilterEditorOnly) != 0;
	if (Reader.Ok() && Out.bCooked)
	{
		ReadRenderData(Reader, Document, NativeOffset + NativeSize, bEditorOnlyStripped, Out.RenderData);
	}

	if (Reader.Ok() && !Reader.UEVerBelow(VER_UE4_SPEEDTREE_STATICMESH) && Reader.ReadBool())
	{
		Reader.Fail(TEXT("The SpeedTree wind data is not read"));
	}

	if (Reader.Ok() && Reader.CustomVer(FEditorObjectVersion::GUID) < FEditorObjectVersion::RefactorMeshEditorMaterials)
	{
		Reader.Fail(TEXT("The materials are stored in an older format"));
	}

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

	// The mesh descriptions an older editor kept inline in the source models.
	const int32 SourceModelCount = FMath::Max(Old.SourceModels.Num(), New.SourceModels.Num());
	for (int32 Index = 0; Index < SourceModelCount; ++Index)
	{
		const FString Key = FString::Printf(TEXT("SourceModel/%d"), Index);
		const FString Title = FString::Printf(TEXT("Source model %d mesh description"), Index);
		const FAssetStaticMeshSourceModel* Before = Old.SourceModels.IsValidIndex(Index) && Old.SourceModels[Index].bHasMeshDescription ? &Old.SourceModels[Index] : nullptr;
		const FAssetStaticMeshSourceModel* After = New.SourceModels.IsValidIndex(Index) && New.SourceModels[Index].bHasMeshDescription ? &New.SourceModels[Index] : nullptr;
		if (Before == nullptr && After != nullptr)
		{
			Add(Key, Title, FAssetNativeDataChange::EState::Added, FString(), After->Describe());
		}
		else if (Before != nullptr && After == nullptr)
		{
			Add(Key, Title, FAssetNativeDataChange::EState::Removed, Before->Describe(), FString());
		}
		else if (Before != nullptr && After != nullptr && (Before->PayloadSize != After->PayloadSize || !Before->PayloadHash.Equals(After->PayloadHash, ESearchCase::CaseSensitive)))
		{
			Add(Key, Title, FAssetNativeDataChange::EState::Modified, Before->Describe(), After->Describe());
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

	// The render data of a cooked mesh: each LOD, then what lies between the LODs and the bounds, the bounds and the screen sizes.
	if (Old.RenderData.bRead && New.RenderData.bRead)
	{
		const FAssetStaticMeshRenderData& OldRender = Old.RenderData;
		const FAssetStaticMeshRenderData& NewRender = New.RenderData;
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

			const FAssetStaticMeshRenderLod& OldLod = OldRender.Lods[LodIndex];
			const FAssetStaticMeshRenderLod& NewLod = NewRender.Lods[LodIndex];
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

			// The buffers: what they hold, and when both hashes could be read whether the bytes are the same.
			const bool bSameHash = OldLod.BufferHash.IsEmpty() || NewLod.BufferHash.IsEmpty() || OldLod.BufferHash.Equals(NewLod.BufferHash, ESearchCase::CaseSensitive);
			const bool bSameBuffers = OldLod.bCookedOut == NewLod.bCookedOut && OldLod.bInlined == NewLod.bInlined && OldLod.NumVertices == NewLod.NumVertices
				&& OldLod.NumTexCoords == NewLod.NumTexCoords && OldLod.NumIndices == NewLod.NumIndices && OldLod.b32BitIndices == NewLod.b32BitIndices && OldLod.BufferBytes == NewLod.BufferBytes
				&& OldLod.ColorVertices == NewLod.ColorVertices && OldLod.bFullPrecisionUVs == NewLod.bFullPrecisionUVs && OldLod.bHighPrecisionTangents == NewLod.bHighPrecisionTangents
				&& OldLod.ReversedIndices == NewLod.ReversedIndices && OldLod.DepthOnlyIndices == NewLod.DepthOnlyIndices && OldLod.WireframeIndices == NewLod.WireframeIndices && bSameHash;
			if (!bSameBuffers)
			{
				Add(LodKey + TEXT("/Buffers"), LodTitle + TEXT(": vertex and index buffers"), FAssetNativeDataChange::EState::Modified, OldLod.Describe(), NewLod.Describe());
			}
			if (!RenderBoundsEqual(OldLod.SourceMeshBounds, NewLod.SourceMeshBounds) || !FMath::IsNearlyEqual(OldLod.MaxDeviation, NewLod.MaxDeviation, 1e-6f))
			{
				Add(LodKey + TEXT("/Bounds"), LodTitle + TEXT(": bounds"), FAssetNativeDataChange::EState::Modified, DescribeRenderBounds(OldLod.SourceMeshBounds),
					DescribeRenderBounds(NewLod.SourceMeshBounds));
			}
		}

		if (OldRender.NumInlinedLODs != NewRender.NumInlinedLODs)
		{
			Add(TEXT("Render/InlinedLods"), TEXT("LODs kept in the export"), FAssetNativeDataChange::EState::Modified, FString::FromInt(OldRender.NumInlinedLODs),
				FString::FromInt(NewRender.NumInlinedLODs));
		}
		if (OldRender.Middle.bDecoded && NewRender.Middle.bDecoded)
		{
			const FAssetStaticMeshRenderMiddle& OldMiddle = OldRender.Middle;
			const FAssetStaticMeshRenderMiddle& NewMiddle = NewRender.Middle;
			const auto Differs = [](const FAssetRenderPart& A, const FAssetRenderPart& B) { return A.Bytes != B.Bytes || !A.Hash.Equals(B.Hash, ESearchCase::CaseSensitive); };

			if (Differs(OldMiddle.Nanite.Part, NewMiddle.Nanite.Part))
			{
				Add(TEXT("Render/Nanite"), TEXT("Nanite resources"), FAssetNativeDataChange::EState::Modified, OldMiddle.Nanite.Describe(), NewMiddle.Nanite.Describe());
			}
			if (Differs(OldMiddle.RayTracing.Part, NewMiddle.RayTracing.Part))
			{
				Add(TEXT("Render/RayTracing"), TEXT("Ray tracing proxy"), FAssetNativeDataChange::EState::Modified, OldMiddle.RayTracing.Describe(), NewMiddle.RayTracing.Describe());
			}

			const int32 CardLods = FMath::Max(OldMiddle.Cards.Num(), NewMiddle.Cards.Num());
			for (int32 Lod = 0; Lod < CardLods; ++Lod)
			{
				const FAssetCardRepresentation Empty;
				const FAssetCardRepresentation& OldCards = OldMiddle.Cards.IsValidIndex(Lod) ? OldMiddle.Cards[Lod] : Empty;
				const FAssetCardRepresentation& NewCards = NewMiddle.Cards.IsValidIndex(Lod) ? NewMiddle.Cards[Lod] : Empty;
				if (OldCards.bValid != NewCards.bValid || (OldCards.bValid && Differs(OldCards.Part, NewCards.Part)))
				{
					Add(FString::Printf(TEXT("Render/Lod/%d/Cards"), Lod), FString::Printf(TEXT("Render LOD %d: card representation"), Lod),
						!OldCards.bValid	   ? FAssetNativeDataChange::EState::Added
							: !NewCards.bValid ? FAssetNativeDataChange::EState::Removed
											   : FAssetNativeDataChange::EState::Modified,
						OldCards.Describe(), NewCards.Describe());

					// The cards that changed, were added or were removed, by their place in the list.
					if (OldCards.bValid && NewCards.bValid)
					{
						const int32 CardCount = FMath::Max(OldCards.CardList.Num(), NewCards.CardList.Num());
						for (int32 CardIndex = 0; CardIndex < CardCount; ++CardIndex)
						{
							const FString CardKey = FString::Printf(TEXT("Render/Lod/%d/Card/%d"), Lod, CardIndex);
							const FString CardTitle = FString::Printf(TEXT("Render LOD %d: card %d"), Lod, CardIndex);
							if (!OldCards.CardList.IsValidIndex(CardIndex))
							{
								Add(CardKey, CardTitle, FAssetNativeDataChange::EState::Added, FString(), NewCards.CardList[CardIndex].Describe());
							}
							else if (!NewCards.CardList.IsValidIndex(CardIndex))
							{
								Add(CardKey, CardTitle, FAssetNativeDataChange::EState::Removed, OldCards.CardList[CardIndex].Describe(), FString());
							}
							else if (!OldCards.CardList[CardIndex].Equals(NewCards.CardList[CardIndex]))
							{
								Add(CardKey, CardTitle, FAssetNativeDataChange::EState::Modified, OldCards.CardList[CardIndex].Describe(), NewCards.CardList[CardIndex].Describe());
							}
						}
					}
				}
			}
			if (OldMiddle.bCardsStripped != NewMiddle.bCardsStripped)
			{
				Add(TEXT("Render/CardsStripped"), TEXT("Card representation kept by the cook"), FAssetNativeDataChange::EState::Modified, OldMiddle.bCardsStripped ? TEXT("left out") : TEXT("kept"),
					NewMiddle.bCardsStripped ? TEXT("left out") : TEXT("kept"));
			}

			const int32 FieldLods = FMath::Max(OldMiddle.DistanceFields.Num(), NewMiddle.DistanceFields.Num());
			for (int32 Lod = 0; Lod < FieldLods; ++Lod)
			{
				const FAssetDistanceField Empty;
				const FAssetDistanceField& OldField = OldMiddle.DistanceFields.IsValidIndex(Lod) ? OldMiddle.DistanceFields[Lod] : Empty;
				const FAssetDistanceField& NewField = NewMiddle.DistanceFields.IsValidIndex(Lod) ? NewMiddle.DistanceFields[Lod] : Empty;
				const bool bSameStream = OldField.StreamableHash.IsEmpty() || NewField.StreamableHash.IsEmpty() || OldField.StreamableHash.Equals(NewField.StreamableHash, ESearchCase::CaseSensitive);
				if (OldField.bValid != NewField.bValid || (OldField.bValid && (Differs(OldField.Part, NewField.Part) || !bSameStream)))
				{
					Add(FString::Printf(TEXT("Render/Lod/%d/DistanceField"), Lod), FString::Printf(TEXT("Render LOD %d: distance field"), Lod),
						!OldField.bValid	   ? FAssetNativeDataChange::EState::Added
							: !NewField.bValid ? FAssetNativeDataChange::EState::Removed
											   : FAssetNativeDataChange::EState::Modified,
						OldField.Describe(), NewField.Describe());
				}
			}
			if (OldMiddle.bDistanceFieldsStripped != NewMiddle.bDistanceFieldsStripped)
			{
				Add(TEXT("Render/DistanceFieldsStripped"), TEXT("Distance fields kept by the cook"), FAssetNativeDataChange::EState::Modified,
					OldMiddle.bDistanceFieldsStripped ? TEXT("left out") : TEXT("kept"), NewMiddle.bDistanceFieldsStripped ? TEXT("left out") : TEXT("kept"));
			}
		}
		else if (OldRender.OtherBytes != NewRender.OtherBytes || !OldRender.OtherHash.Equals(NewRender.OtherHash, ESearchCase::CaseSensitive))
		{
			Add(TEXT("Render/Other"), TEXT("Nanite, ray tracing and distance field data"), FAssetNativeDataChange::EState::Modified,
				FString::Printf(TEXT("%lld bytes, %s"), OldRender.OtherBytes, *OldRender.OtherHash), FString::Printf(TEXT("%lld bytes, %s"), NewRender.OtherBytes, *NewRender.OtherHash));
		}
		if (!RenderBoundsEqual(OldRender.Bounds, NewRender.Bounds))
		{
			Add(TEXT("Render/Bounds"), TEXT("Render bounds"), FAssetNativeDataChange::EState::Modified, DescribeRenderBounds(OldRender.Bounds), DescribeRenderBounds(NewRender.Bounds));
		}
		bool bScreenSizeChanged = false;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			bScreenSizeChanged |= !FMath::IsNearlyEqual(OldRender.ScreenSize[Index], NewRender.ScreenSize[Index], 1e-6f);
		}
		if (bScreenSizeChanged)
		{
			const auto Sizes = [](const FAssetStaticMeshRenderData& Render) {
				TArray<FString> Parts;
				for (int32 Index = 0; Index < FMath::Max(Render.Lods.Num(), 1); ++Index)
				{
					Parts.Add(FString::Printf(TEXT("%.4f"), Render.ScreenSize[Index]));
				}
				return FString::Join(Parts, TEXT(", "));
			};
			Add(TEXT("Render/ScreenSize"), TEXT("Screen size of each LOD"), FAssetNativeDataChange::EState::Modified, Sizes(OldRender), Sizes(NewRender));
		}
	}

	return Changes;
}
