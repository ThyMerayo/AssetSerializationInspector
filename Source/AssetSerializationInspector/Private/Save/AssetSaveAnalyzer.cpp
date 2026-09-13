// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Save/AssetSaveAnalyzer.h"

#include "Diff/AssetPackageDiff.h"

static FString MakeSemanticPath(const FString& ParentPath, const FAssetPackageDiffEntry& Entry)
{
	if (ParentPath.IsEmpty())
	{
		return Entry.Key;
	}

	return ParentPath + TEXT("/") + Entry.Key;
}

static int32 CountChangedChildren(const FAssetPackageDiffEntry& Entry)
{
	int32 Count = 0;

	for (const FAssetPackageDiffEntry& Child : Entry.Children)
	{
		if (Child.State != EAssetPackageDiffState::Unchanged)
		{
			++Count;
		}
	}

	return Count;
}

static void AnalyzeProperty(const FAssetPackageDiffEntry& Entry, FAssetSaveAnalysis& OutAnalysis)
{
	if (Entry.State == EAssetPackageDiffState::Unchanged)
	{
		return;
	}

	FAssetSaveExplanationEntry Explanation;
	Explanation.Key = Entry.Key;
	Explanation.ChangedByteCount = Entry.ChangedByteCount;
	Explanation.OldOffset = Entry.OldOffset;
	Explanation.NewOffset = Entry.NewOffset;
	Explanation.OldSize = Entry.OldSize;
	Explanation.NewSize = Entry.NewSize;
	Explanation.bHasOldValue = Entry.bHasOldDecodedValue;
	Explanation.bHasNewValue = Entry.bHasNewDecodedValue;
	Explanation.OldValue = Entry.OldValue;
	Explanation.NewValue = Entry.NewValue;

	Explanation.SemanticPath = MakeSemanticPath(Entry.OldFieldPath, Entry);

	if (Entry.OldPresence == EAssetSerializedPropertyPresence::NotSerialized && Entry.NewPresence == EAssetSerializedPropertyPresence::Present)
	{
		Explanation.Classification = EAssetSaveChangeClassification::PropertyBecameSerialized;
		Explanation.Confidence = EAssetExplanationConfidence::High;
		Explanation.Title = FText::FromString(Entry.Key);
		Explanation.Description = FText::Format(NSLOCTEXT("AssetSaveAnalyzer", "PropertyBecameSerialized", "Property is now serialized. Old: {0}  New: {1}"),
			FText::FromString(Entry.bHasOldDecodedValue ? Entry.OldDecodedValue : TEXT("<not serialized; likely default>")),
			FText::FromString(Entry.bHasNewDecodedValue ? Entry.NewDecodedValue : TEXT("<serialized>")));
	}
	else if (Entry.OldPresence == EAssetSerializedPropertyPresence::Present && Entry.NewPresence == EAssetSerializedPropertyPresence::NotSerialized)
	{
		Explanation.Classification = EAssetSaveChangeClassification::PropertyBecameOmitted;
		Explanation.Confidence = EAssetExplanationConfidence::High;
		Explanation.Title = FText::FromString(Entry.Key);
		Explanation.Description = FText::Format(NSLOCTEXT("AssetSaveAnalyzer", "PropertyBecameOmitted", "Property is no longer serialized. Old: {0}  New: likely default/omitted"),
			FText::FromString(Entry.bHasOldDecodedValue ? Entry.OldDecodedValue : TEXT("<serialized>")));
	}
	else if (!Entry.Children.IsEmpty())
	{
		Explanation.Classification = EAssetSaveChangeClassification::ContainerChanged;
		Explanation.Confidence = EAssetExplanationConfidence::Certain;
		Explanation.Title = FText::FromString(Entry.Key);
		Explanation.Description = FText::Format(NSLOCTEXT("AssetSaveAnalyzer", "NestedPropertyChanged", "{0} nested value changes."), FText::AsNumber(CountChangedChildren(Entry)));
	}
	else
	{
		Explanation.Classification = EAssetSaveChangeClassification::PropertyValueChanged;
		Explanation.Confidence = Entry.bHasOldDecodedValue && Entry.bHasNewDecodedValue ? EAssetExplanationConfidence::Certain : EAssetExplanationConfidence::High;
		Explanation.Title = FText::FromString(Entry.Key);

		if (Entry.bHasOldDecodedValue && Entry.bHasNewDecodedValue)
		{
			Explanation.Description =
				FText::Format(NSLOCTEXT("AssetSaveAnalyzer", "PropertyValueChanged", "{0} -> {1}"), FText::FromString(Entry.OldDecodedValue), FText::FromString(Entry.NewDecodedValue));
		}
		else
		{
			Explanation.Description = FText::Format(NSLOCTEXT("AssetSaveAnalyzer", "PropertyBytesChanged", "{0} serialized bytes changed."), FText::AsNumber(Entry.ChangedByteCount));
		}
	}

	OutAnalysis.PropertyChangeCount++;
	OutAnalysis.SemanticChanges.Add(MoveTemp(Explanation));
}

static FAssetSaveExplanationEntry BuildPropertyExplanation(const FAssetPackageDiffEntry& Entry)
{
	FAssetSaveExplanationEntry Result;
	Result.Key = Entry.Key;
	Result.Title = Entry.DisplayName;
	Result.ChangedByteCount = Entry.ChangedByteCount;
	Result.OldOffset = Entry.OldOffset;
	Result.NewOffset = Entry.NewOffset;
	Result.OldSize = Entry.OldSize;
	Result.NewSize = Entry.NewSize;
	Result.SemanticPath = MakeSemanticPath(Entry.OldFieldPath, Entry);

	// classification logic...

	for (const FAssetPackageDiffEntry& Child : Entry.Children)
	{
		if (Child.Kind != EAssetPackageDiffKind::Property || Child.State == EAssetPackageDiffState::Unchanged)
		{
			continue;
		}

		Result.Children.Add(BuildPropertyExplanation(Child));
	}

	return Result;
}

static const FAssetPackageDiffEntry* FindPayloadChild(const FAssetPackageDiffEntry& Export)
{
	for (const FAssetPackageDiffEntry& Child : Export.Children)
	{
		if (Child.Kind == EAssetPackageDiffKind::ExportPayload)
		{
			return &Child;
		}
	}

	return nullptr;
}

static void AnalyzeExport(const FAssetPackageDiffEntry& Entry, FAssetSaveAnalysis& OutAnalysis)
{
	const FAssetPackageDiffEntry* Payload = FindPayloadChild(Entry);

	if (Payload == nullptr)
	{
		return;
	}

	const bool bPayloadIdentical = Payload->State == EAssetPackageDiffState::Unchanged;
	const bool bMoved = Entry.OldOffset != INDEX_NONE && Entry.NewOffset != INDEX_NONE && Entry.OldOffset != Entry.NewOffset;

	if (!bPayloadIdentical || !bMoved)
	{
		return;
	}

	FAssetSaveExplanationEntry Explanation;
	Explanation.Classification = EAssetSaveChangeClassification::ExportRelocated;
	Explanation.Confidence = EAssetExplanationConfidence::Certain;
	Explanation.Key = Entry.Key;
	Explanation.Title = Entry.DisplayName;
	const int64 Delta = Entry.NewOffset - Entry.OldOffset;
	Explanation.Description = FText::Format(NSLOCTEXT("AssetSaveAnalyzer", "ExportRelocated", "Payload is unchanged but moved by {0} bytes."), FText::AsNumber(Delta));
	Explanation.OldOffset = Entry.OldOffset;
	Explanation.NewOffset = Entry.NewOffset;
	Explanation.OldSize = Entry.OldSize;
	Explanation.NewSize = Entry.NewSize;
	Explanation.SemanticPath = FString::Printf(TEXT("Export:%s"), *Entry.Key);

	OutAnalysis.RelocationCount++;
	OutAnalysis.LayoutChanges.Add(MoveTemp(Explanation));
}

static void AnalyzeUnknownRange(const FAssetPackageDiffEntry& Entry, FAssetSaveAnalysis& OutAnalysis)
{
	if (Entry.State == EAssetPackageDiffState::Unchanged)
	{
		return;
	}

	FAssetSaveExplanationEntry Explanation;
	Explanation.Classification = EAssetSaveChangeClassification::NativeOrUndecodedChanged;
	Explanation.Confidence = EAssetExplanationConfidence::Unknown;
	Explanation.Key = Entry.Key;
	Explanation.Title = NSLOCTEXT("AssetSaveAnalyzer", "NativeUndecoded", "Native / undecoded serialization");
	Explanation.ChangedByteCount = Entry.ChangedByteCount;
	Explanation.Description =
		FText::Format(NSLOCTEXT("AssetSaveAnalyzer", "NativeBytesChanged", "{0} changed bytes could not be attributed to a decoded serialized property."), FText::AsNumber(Entry.ChangedByteCount));
	Explanation.OldOffset = Entry.OldOffset;
	Explanation.NewOffset = Entry.NewOffset;
	Explanation.OldSize = Entry.OldSize;
	Explanation.NewSize = Entry.NewSize;
	Explanation.SemanticPath = MakeSemanticPath(Entry.OldFieldPath, Entry);

	OutAnalysis.UnexplainedChanges.Add(MoveTemp(Explanation));
}

static void AnalyzeEntry(const FAssetPackageDiffEntry& Entry, const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument, FAssetSaveAnalysis& OutAnalysis)
{
	switch (Entry.Kind)
	{
		case EAssetPackageDiffKind::Property:
			AnalyzeProperty(Entry, OutAnalysis);
			break;

		case EAssetPackageDiffKind::Export:
			AnalyzeExport(Entry, OutAnalysis);
			break;

		case EAssetPackageDiffKind::UnknownPayloadRange:
			AnalyzeUnknownRange(Entry, OutAnalysis);
			break;

		default:
			break;
	}

	for (const FAssetPackageDiffEntry& Child : Entry.Children)
	{
		AnalyzeEntry(Child, OldDocument, NewDocument, OutAnalysis);
	}
}

static FAssetSaveExplanationEntry* FindLayoutExplanation(FAssetSaveAnalysis& Analysis, const FString& Key)
{
	for (FAssetSaveExplanationEntry& Entry : Analysis.LayoutChanges)
	{
		if (Entry.Key == Key)
		{
			return &Entry;
		}
	}
	return nullptr;
}

struct FMatchedExportLayout
{
	FString Name;

	int64 OldOffset = 0;
	int64 OldSize = 0;

	int64 NewOffset = 0;
	int64 NewSize = 0;

	bool bPayloadIdentical = false;
};

static void AnalyzeExportLayout(const FAssetPackageDiffResult& Diff, FAssetSaveAnalysis& OutAnalysis)
{
	TArray<FMatchedExportLayout> Exports;

	for (const FAssetPackageDiffEntry& Entry : Diff.Entries)
	{
		Exports.Emplace(Entry.Key, Entry.OldOffset, Entry.OldSize, Entry.NewOffset, Entry.NewSize, Entry.ChangedSpans.IsEmpty());
	}

	Exports.Sort([](const FMatchedExportLayout& A, const FMatchedExportLayout& B) { return A.OldOffset < B.OldOffset; });

	int64 CumulativeDelta = 0;
	FString LastSizeChangingExport;

	for (const FMatchedExportLayout& Export : Exports)
	{
		const int64 ExpectedOffset = Export.OldOffset + CumulativeDelta;

		if (Export.bPayloadIdentical && Export.NewOffset == ExpectedOffset && CumulativeDelta != 0)
		{
			FAssetSaveExplanationEntry* Explanation = FindLayoutExplanation(OutAnalysis, Export.Name);
			if (Explanation != nullptr)
			{
				Explanation->CauseConfidence = EAssetExplanationConfidence::Inferred;
				Explanation->CauseDescription =
					FText::Format(NSLOCTEXT("AssetSaveAnalyzer", "PrecedingDataChanged", "Preceding serialized data changed size by {0} bytes."), FText::AsNumber(CumulativeDelta));
			}
		}

		const int64 SizeDelta = Export.NewSize - Export.OldSize;
		if (SizeDelta != 0)
		{
			CumulativeDelta += SizeDelta;
			LastSizeChangingExport = Export.Name;
		}
	}
}

struct FAssetChangedRange
{
	int64 Offset = 0;
	int64 Size = 0;

	int64 End() const { return Offset + Size; }
};

static int64 CalculateCoveredBytes(TArray<FAssetChangedRange> Ranges)
{
	if (Ranges.IsEmpty())
	{
		return 0;
	}

	Ranges.Sort([](const FAssetChangedRange& A, const FAssetChangedRange& B) { return A.Offset < B.Offset; });

	int64 Total = 0;
	int64 CurrentStart = Ranges[0].Offset;
	int64 CurrentEnd = Ranges[0].End();

	for (int32 Index = 1; Index < Ranges.Num(); ++Index)
	{
		const FAssetChangedRange& Range = Ranges[Index];
		if (Range.Offset <= CurrentEnd)
		{
			CurrentEnd = FMath::Max(CurrentEnd, Range.End());
			continue;
		}

		Total += CurrentEnd - CurrentStart;
		CurrentStart = Range.Offset;
		CurrentEnd = Range.End();
	}

	Total += CurrentEnd - CurrentStart;
	return Total;
}

static void FinalizeResult(FAssetSaveAnalysis& Analysis)
{
	if (Analysis.PropertyChangeCount == 0 && Analysis.UnexplainedChangedBytes == 0)
	{
		if (Analysis.RelocationCount > 0 || !Analysis.LayoutChanges.IsEmpty())
		{
			Analysis.ResultKind = EAssetSaveResultKind::LayoutOnly;
		}
		else
		{
			Analysis.ResultKind = EAssetSaveResultKind::MetadataOnly;
		}

		return;
	}

	if (Analysis.PropertyChangeCount > 0 && Analysis.UnexplainedChangedBytes > 0)
	{
		Analysis.ResultKind = EAssetSaveResultKind::SemanticAndNativeChanges;
		return;
	}

	if (Analysis.PropertyChangeCount > 0)
	{
		Analysis.ResultKind = EAssetSaveResultKind::SemanticChanges;
		return;
	}

	if (Analysis.UnexplainedChangedBytes > 0)
	{
		Analysis.ResultKind = EAssetSaveResultKind::NativeOnlyChanges;
		return;
	}

	Analysis.ResultKind = EAssetSaveResultKind::MetadataOnly;
}

FAssetSaveAnalysis FAssetSaveAnalyzer::Analyze(const FAssetPackageDiffResult& Diff, const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument)
{
	FAssetSaveAnalysis Result;

	if (Diff.bFilesIdentical)
	{
		Result.ResultKind = EAssetSaveResultKind::Identical;
		return Result;
	}

	for (const FAssetPackageDiffEntry& Entry : Diff.Entries)
	{
		AnalyzeEntry(Entry, OldDocument, NewDocument, Result);
	}

	AnalyzeExportLayout(Diff, Result);

	FinalizeResult(Result);

	return Result;
}