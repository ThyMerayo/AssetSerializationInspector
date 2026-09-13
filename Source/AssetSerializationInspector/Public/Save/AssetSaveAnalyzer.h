// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

struct FAssetPackageDiffResult;
struct FAssetPackageDocument;

enum class EAssetExplanationConfidence : uint8
{
	Certain,
	High,
	Inferred,
	Unknown
};

enum class EAssetSaveChangeClassification : uint8
{
	Unknown,

	PropertyValueChanged,
	PropertyBecameSerialized,
	PropertyBecameOmitted,
	ContainerChanged,

	ExportPayloadChanged,
	ExportRelocated,

	PackageMetadataChanged,
	TableChanged,

	NativeOrUndecodedChanged
};

enum class EAssetSaveResultKind : uint8
{
	Identical,
	LayoutOnly,
	MetadataOnly,
	SemanticChanges,
	SemanticAndNativeChanges,
	NativeOnlyChanges
};

struct FAssetSaveExplanationEntry
{
	EAssetSaveChangeClassification Classification = EAssetSaveChangeClassification::Unknown;
	EAssetExplanationConfidence Confidence = EAssetExplanationConfidence::Unknown;
	EAssetExplanationConfidence CauseConfidence = EAssetExplanationConfidence::Unknown;

	FString Key;
	FString SemanticPath;

	FText Title;
	FText Description;
	FText CauseDescription;

	int64 ChangedByteCount = 0;

	int64 OldOffset = INDEX_NONE;
	int64 NewOffset = INDEX_NONE;

	int64 OldSize = 0;
	int64 NewSize = 0;

	FString OldValue;
	FString NewValue;

	bool bHasOldValue = false;
	bool bHasNewValue = false;

	TArray<FAssetSaveExplanationEntry> Children;
};

struct FAssetSaveAnalysis
{
	EAssetSaveResultKind ResultKind = EAssetSaveResultKind::Identical;

	int32 PropertyChangeCount = 0;
	int32 RelocationCount = 0;

	int64 TotalChangedBytes = 0;
	int64 ExplainedChangedBytes = 0;
	int64 UnexplainedChangedBytes = 0;

	TArray<FAssetSaveExplanationEntry> SemanticChanges;
	TArray<FAssetSaveExplanationEntry> LayoutChanges;
	TArray<FAssetSaveExplanationEntry> UnexplainedChanges;
};

class FAssetSaveAnalyzer
{
public:
	static FAssetSaveAnalysis Analyze(const FAssetPackageDiffResult& Diff, const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument);
};