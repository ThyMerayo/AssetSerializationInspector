// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Serialization/ArchiveProxy.h"

class FProperty;

struct FAssetSerializationTraceEvent
{
	int64 Offset = 0;
	int64 Size = 0;

	FString PropertyPath;
	FString LeafPropertyName;
	FString PropertyType;

	bool bHasPropertyContext = false;
};

class FAssetSerializationTraceArchive final : public FArchiveProxy
{
public:
	explicit FAssetSerializationTraceArchive(FArchive& InInnerArchive, int64 InBaseOffset = 0);

	virtual void Serialize(void* Data, int64 Num) override;

	const TArray<FAssetSerializationTraceEvent>& GetEvents() const { return Events; }

private:
	FString BuildCurrentPropertyPath() const;

private:
	int64 BaseOffset = 0;

	TArray<FAssetSerializationTraceEvent> Events;
};