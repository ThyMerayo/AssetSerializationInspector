// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Serialization/AssetCookedBulkData.h"
#include "Serialization/AssetStaticMeshData.h"

class FNativeReader;
struct FAssetPackageDocument;

/** Pieces of the render data of a cooked mesh that static and skeletal meshes share. */
namespace AssetRenderDataReaders
{
	/** The bytes from Start to End of the document as a part: where they are, and a hash of them. */
	FAssetRenderPart MakePart(const FAssetPackageDocument& Document, int64 Start, int64 End);

	/** An array of elements of a fixed size that is skipped: its count must fit what is left. Returns the count. */
	int32 SkipFixedArray(FNativeReader& Reader, int64 ElementBytes, const TCHAR* What);

	/** Nanite::FResources::Serialize: the counts and the streamed pages of the Nanite data of a mesh. */
	void ReadNaniteResources(FNativeReader& Reader, const FAssetPackageDocument& Document, const TArray<AssetCookedBulkData::FDataResource>& Resources, FAssetNaniteResources& Out);
} // namespace AssetRenderDataReaders
