// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Serialization/Archive.h"

/**
 * Read-only archive over a verified subsection of a file buffer.
 *
 * Tell() returns an absolute file offset, making parser errors and tree nodes
 * easier to relate to the hex display.
 */
class FAssetPackageMemoryReader final : public FArchive
{
public:
	FAssetPackageMemoryReader(const TArray64<uint8>& InFileData, int64 InStartOffset, int64 InSize);

	virtual void Serialize(void* Data, int64 Num) override;
	virtual int64 Tell() override;
	virtual int64 TotalSize() override;
	virtual void Seek(int64 InPos) override;

	int64 GetRegionStart() const { return RegionStart; }

	int64 GetRegionEnd() const { return RegionEnd; }

	int64 Remaining() const { return FMath::Max<int64>(0, RegionEnd - Position); }

	bool CanRead(const int64 NumBytes) const { return NumBytes >= 0 && Position >= RegionStart && Position <= RegionEnd && NumBytes <= RegionEnd - Position; }

private:
	const TArray64<uint8>& FileData;

	int64 RegionStart = 0;
	int64 RegionEnd = 0;
	int64 Position = 0;
};