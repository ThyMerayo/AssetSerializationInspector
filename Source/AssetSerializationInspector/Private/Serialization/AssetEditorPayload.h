// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Serialization/AssetBulkDataExport.h"

struct FAssetPackageDocument;

namespace AssetEditorPayload
{
	/**
	 * Loads the bytes that the record of a block of editor data (FEditorBulkData) refers to, uncompressed, when the package holds them in
	 * its trailer, which is where a save writes them, or at the offset the record gives, as a save without a trailer does. The data of a package from the older format, a virtualized payload, one in a
	 * sidecar file, one larger than MaximumBytes and a package that is not on disk are not loaded, and Error says which.
	 *
	 * @param What What the data is, for the error messages ("The image").
	 */
	bool Load(const FAssetPackageDocument& Document, const FAssetBulkDataInfo& Bulk, const TCHAR* What, int64 MaximumBytes, TArray64<uint8>& Out, FString& Error);
} // namespace AssetEditorPayload
