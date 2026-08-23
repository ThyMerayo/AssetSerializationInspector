// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Serialization/ArchiveUObject.h"

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

class FAssetSerializationTraceArchive : public FArchiveUObject
{
public:
	explicit FAssetSerializationTraceArchive(FArchive& InInnerArchive, int64 InBaseOffset = 0);

	virtual void Serialize(void* Data, int64 Num) override;

	virtual int64 Tell() override { return InnerArchive.Tell(); }
	virtual int64 TotalSize() override { return InnerArchive.TotalSize(); }
	virtual void Seek(const int64 InPos) override { InnerArchive.Seek(InPos); }
	virtual bool AtEnd() override { return InnerArchive.AtEnd(); }

	TArray<FAssetSerializationTraceEvent> GetEvents() const { return Events; }

	FArchive& operator<<(FObjectPtr& Value);

private:
	FString BuildCurrentPropertyPath() const;

private:
	FArchive& InnerArchive;
	int64 BaseOffset = 0;

	TArray<FAssetSerializationTraceEvent> Events;
};