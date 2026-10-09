// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Trace/AssetSerializationTrace.h"

class FArchive;

class FAssetSerializationTraceBuilder
{
public:
	FAssetSerializationTraceBuilder(FArchive& InArchive, int64 InPayloadStart);

	void BeginNode(const FString& Name, const FString& TypeName, EAssetSerializationTraceKind Kind);

	void EndNode();


private:
	FArchive& Archive;

	int64 PayloadStart = 0;

	TSharedPtr<FAssetSerializationTraceNode> Root;

	TArray<TSharedPtr<FAssetSerializationTraceNode>> Stack;
};

class FAssetSerializationTraceScope
{
public:
	FAssetSerializationTraceScope(FAssetSerializationTraceBuilder& InBuilder, const FString& Name, const FString& TypeName, const EAssetSerializationTraceKind Kind) : Builder(InBuilder)
	{
		Builder.BeginNode(Name, TypeName, Kind);
	}

	~FAssetSerializationTraceScope() { Builder.EndNode(); }

private:
	FAssetSerializationTraceBuilder& Builder;
};