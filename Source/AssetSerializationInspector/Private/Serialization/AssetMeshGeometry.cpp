// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetMeshGeometry.h"

#include "MeshDescription.h"
#include "Serialization/BufferReader.h"
#include "StaticMeshAttributes.h"

#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetEditorPayload.h"

namespace
{
	/** A mesh description larger than this is not loaded. */
	constexpr int64 MaximumMeshBytes = 512ll * 1024 * 1024;

	/** The reader the editor bulk data uses for a mesh description: names are written as strings, and the versions are the package's. */
	class FMeshDescriptionPayloadReader : public FBufferReaderBase
	{
	public:
		FMeshDescriptionPayloadReader(void* Data, const int64 Size) : FBufferReaderBase(Data, Size, false, true) {}

		using FArchive::operator<<;

		virtual FArchive& operator<<(FName& Name) override
		{
			FString Text;
			*this << Text;
			Name = FName(*Text);
			return *this;
		}

		virtual FString GetArchiveName() const override { return TEXT("MeshDescriptionPayloadReader"); }
	};

	FString DescribeBounds(const FBox& Bounds)
	{
		return Bounds.IsValid ? FString::Printf(TEXT("(%.3f, %.3f, %.3f) to (%.3f, %.3f, %.3f)"), Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z, Bounds.Max.X, Bounds.Max.Y, Bounds.Max.Z)
							  : FString(TEXT("empty"));
	}

	FString DescribeCounts(const FAssetMeshGeometry& Geometry)
	{
		return FString::Printf(TEXT("%d vertices, %d triangles, %d polygons"), Geometry.VertexCount, Geometry.TriangleCount, Geometry.PolygonCount);
	}
} // namespace

FAssetMeshGeometry AssetMeshGeometry::Load(const FAssetPackageDocument& Document, const FAssetBulkDataInfo& Bulk)
{
	FAssetMeshGeometry Geometry;

	TArray64<uint8> Payload;
	if (!AssetEditorPayload::Load(Document, Bulk, TEXT("The mesh description"), MaximumMeshBytes, Payload, Geometry.Error))
	{
		return Geometry;
	}

	// The mesh description is written with the versions of the package that saved it.
	FMeshDescriptionPayloadReader Reader(Payload.GetData(), Payload.Num());
	Reader.SetUEVer(Document.PackageSummary.GetFileVersionUE());
	Reader.SetLicenseeUEVer(Document.PackageSummary.GetFileVersionLicenseeUE());
	Reader.SetCustomVersions(Document.PackageSummary.GetCustomVersionContainer());

	FMeshDescription MeshDescription;
	MeshDescription.Empty();
	Reader << MeshDescription;
	if (Reader.IsError() || Reader.Tell() != Reader.TotalSize())
	{
		Geometry.Error = TEXT("The mesh description is not in a layout this reading knows");
		return Geometry;
	}

	Geometry.VertexCount = MeshDescription.Vertices().Num();
	Geometry.VertexInstanceCount = MeshDescription.VertexInstances().Num();
	Geometry.TriangleCount = MeshDescription.Triangles().Num();
	Geometry.PolygonCount = MeshDescription.Polygons().Num();

	const FStaticMeshConstAttributes Attributes(MeshDescription);
	const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
	if (Positions.IsValid())
	{
		Geometry.Positions.Reserve(Geometry.VertexCount);
		for (const FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
		{
			const FVector3f Position = Positions[VertexID];
			Geometry.Positions.Add(Position);
			Geometry.Bounds += FVector(Position);
		}
	}

	// The attributes of the corners: normals, tangents and the UVs of each channel.
	const TVertexInstanceAttributesConstRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
	const TVertexInstanceAttributesConstRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
	const TVertexInstanceAttributesConstRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
	Geometry.UVs.SetNum(UVs.IsValid() ? UVs.GetNumChannels() : 0);
	for (const FVertexInstanceID Instance : MeshDescription.VertexInstances().GetElementIDs())
	{
		if (Normals.IsValid())
		{
			Geometry.Normals.Add(Normals[Instance]);
		}
		if (Tangents.IsValid())
		{
			Geometry.Tangents.Add(Tangents[Instance]);
		}
		for (int32 Channel = 0; Channel < Geometry.UVs.Num(); ++Channel)
		{
			Geometry.UVs[Channel].Add(UVs.Get(Instance, Channel));
		}
	}

	// The material slot of each polygon group, and the slot each triangle belongs to.
	const TPolygonGroupAttributesConstRef<FName> Slots = Attributes.GetPolygonGroupMaterialSlotNames();
	if (Slots.IsValid())
	{
		for (const FPolygonGroupID Group : MeshDescription.PolygonGroups().GetElementIDs())
		{
			Geometry.MaterialSlots.Add(Slots[Group].ToString());
		}
		for (const FTriangleID Triangle : MeshDescription.Triangles().GetElementIDs())
		{
			Geometry.TriangleSlots.Add(Slots[MeshDescription.GetTrianglePolygonGroup(Triangle)].ToString());
		}
	}

	Geometry.bLoaded = true;
	return Geometry;
}

void AssetMeshGeometry::AppendGeometryChange(const FAssetMeshGeometry& Old, const FAssetMeshGeometry& New, TArray<FAssetNativeDataChange>& Changes)
{
	const auto Add = [&Changes](const TCHAR* Key, const FString& Title, const FString& OldValue, const FString& NewValue) {
		FAssetNativeDataChange& Change = Changes.AddDefaulted_GetRef();
		Change.Key = Key;
		Change.Title = Title;
		Change.State = FAssetNativeDataChange::EState::Modified;
		Change.OldValue = OldValue;
		Change.NewValue = NewValue;
	};

	if (!Old.bLoaded || !New.bLoaded)
	{
		Add(TEXT("BulkData/Geometry"), TEXT("Mesh geometry"), TEXT("not compared"), !Old.bLoaded ? Old.Error : New.Error);
		return;
	}

	if (Old.VertexCount != New.VertexCount || Old.TriangleCount != New.TriangleCount || Old.PolygonCount != New.PolygonCount)
	{
		Add(TEXT("BulkData/Geometry"), TEXT("Mesh geometry"), DescribeCounts(Old), DescribeCounts(New));
	}

	// The bounds, when they moved a little: one tolerance for the float noise of a resave.
	if (!Old.Bounds.Min.Equals(New.Bounds.Min, 1e-4) || !Old.Bounds.Max.Equals(New.Bounds.Max, 1e-4) || Old.Bounds.IsValid != New.Bounds.IsValid)
	{
		Add(TEXT("BulkData/Bounds"), TEXT("Mesh bounds"), DescribeBounds(Old.Bounds), DescribeBounds(New.Bounds));
	}

	// With the same number of vertices they can be compared one by one: how many moved, and by how much.
	if (Old.Positions.Num() == New.Positions.Num() && !Old.Positions.IsEmpty())
	{
		int32 Moved = 0;
		double Largest = 0.0;
		FBox Where(ForceInit);
		for (int32 Index = 0; Index < Old.Positions.Num(); ++Index)
		{
			if (Old.Positions[Index] != New.Positions[Index])
			{
				++Moved;
				Largest = FMath::Max(Largest, static_cast<double>(FVector3f::Distance(Old.Positions[Index], New.Positions[Index])));
				Where += FVector(New.Positions[Index]);
			}
		}

		if (Moved > 0)
		{
			Add(TEXT("BulkData/Vertices"), FString::Printf(TEXT("Mesh vertices: %d of %d moved, the largest by %.4f"), Moved, Old.Positions.Num(), Largest), TEXT("positions before"),
				FString::Printf(TEXT("moved vertices within %s"), *DescribeBounds(Where)));
		}
	}

	// The normals and the tangents of the corners: how many changed, and the largest turn in degrees.
	const auto CompareDirections = [&](const TCHAR* Key, const TCHAR* Name, const TArray<FVector3f>& OldValues, const TArray<FVector3f>& NewValues) {
		if (OldValues.Num() != NewValues.Num() || OldValues.IsEmpty())
		{
			return;
		}

		int32 Changed = 0;
		double Largest = 0.0;
		for (int32 Index = 0; Index < OldValues.Num(); ++Index)
		{
			if (!OldValues[Index].Equals(NewValues[Index], 1e-4f))
			{
				++Changed;
				const double Cosine = FMath::Clamp(static_cast<double>(OldValues[Index].GetSafeNormal() | NewValues[Index].GetSafeNormal()), -1.0, 1.0);
				Largest = FMath::Max(Largest, FMath::RadiansToDegrees(FMath::Acos(Cosine)));
			}
		}

		if (Changed > 0)
		{
			Add(Key, FString::Printf(TEXT("Mesh %s: %d of %d changed, the largest by %.2f degrees"), Name, Changed, OldValues.Num(), Largest), TEXT("directions before"), TEXT("directions after"));
		}
	};
	CompareDirections(TEXT("BulkData/Normals"), TEXT("normals"), Old.Normals, New.Normals);
	CompareDirections(TEXT("BulkData/Tangents"), TEXT("tangents"), Old.Tangents, New.Tangents);

	// The UV channels: a channel added or removed, and for the channels both have, how many corners moved.
	if (Old.UVs.Num() != New.UVs.Num())
	{
		Add(TEXT("BulkData/UVChannels"), TEXT("Mesh UV channels"), FString::FromInt(Old.UVs.Num()), FString::FromInt(New.UVs.Num()));
	}
	for (int32 Channel = 0; Channel < FMath::Min(Old.UVs.Num(), New.UVs.Num()); ++Channel)
	{
		if (Old.UVs[Channel].Num() != New.UVs[Channel].Num())
		{
			continue;
		}

		int32 Changed = 0;
		double Largest = 0.0;
		for (int32 Index = 0; Index < Old.UVs[Channel].Num(); ++Index)
		{
			if (!Old.UVs[Channel][Index].Equals(New.UVs[Channel][Index], 1e-5f))
			{
				++Changed;
				Largest = FMath::Max(Largest, static_cast<double>(FVector2f::Distance(Old.UVs[Channel][Index], New.UVs[Channel][Index])));
			}
		}

		if (Changed > 0)
		{
			Add(*FString::Printf(TEXT("BulkData/UV/%d"), Channel),
				FString::Printf(TEXT("Mesh UV channel %d: %d of %d corners moved, the largest by %.4f"), Channel, Changed, Old.UVs[Channel].Num(), Largest), TEXT("UVs before"), TEXT("UVs after"));
		}
	}

	// The material slots, and which triangles use which.
	if (Old.MaterialSlots != New.MaterialSlots)
	{
		Add(TEXT("BulkData/MaterialSlots"), TEXT("Mesh material slots"), FString::Join(Old.MaterialSlots, TEXT(", ")), FString::Join(New.MaterialSlots, TEXT(", ")));
	}
	else if (Old.TriangleSlots.Num() == New.TriangleSlots.Num())
	{
		int32 Reassigned = 0;
		for (int32 Index = 0; Index < Old.TriangleSlots.Num(); ++Index)
		{
			Reassigned += Old.TriangleSlots[Index].Equals(New.TriangleSlots[Index], ESearchCase::CaseSensitive) ? 0 : 1;
		}

		if (Reassigned > 0)
		{
			Add(TEXT("BulkData/MaterialAssignment"), FString::Printf(TEXT("Mesh material assignment: %d of %d triangles use another slot"), Reassigned, Old.TriangleSlots.Num()), TEXT("slots before"),
				TEXT("slots after"));
		}
	}
}
