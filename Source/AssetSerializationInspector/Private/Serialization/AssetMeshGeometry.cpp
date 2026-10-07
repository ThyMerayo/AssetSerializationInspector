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
}
