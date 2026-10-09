// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Save/AssetSaveAnalyzer.h"

#include "Diff/AssetPackageDiff.h"

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

	// Nothing differs in the value, only how it is stored: a layout change, not a change of the asset.
	if (Entry.bRepresentationOnly)
	{
		FAssetSaveExplanationEntry Stored;
		Stored.Classification = EAssetSaveChangeClassification::PropertyStoredDifferently;
		Stored.Confidence = EAssetExplanationConfidence::Certain;
		Stored.Key = Entry.Key;
		Stored.Title = FText::FromString(Entry.Key);
		Stored.Description = NSLOCTEXT("AssetSaveAnalyzer", "PropertyStoredDifferently", "Same entries, stored in another order (maps and sets are written in hash table order).");
		Stored.ChangedByteCount = Entry.ChangedByteCount;
		Stored.OldOffset = Entry.OldOffset;
		Stored.NewOffset = Entry.NewOffset;
		Stored.OldSize = Entry.OldSize;
		Stored.NewSize = Entry.NewSize;
		Stored.SemanticPath = Entry.SemanticPath;

		OutAnalysis.ExplainedChangedBytes += Entry.ChangedByteCount;
		OutAnalysis.LayoutChanges.Add(MoveTemp(Stored));
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

	Explanation.SemanticPath = Entry.SemanticPath;

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

	// A property that appeared or went accounts for all its bytes; a modified one for the bytes that differ.
	OutAnalysis.ExplainedChangedBytes += Entry.State == EAssetPackageDiffState::Added ? Entry.NewSize : (Entry.State == EAssetPackageDiffState::Removed ? Entry.OldSize : Entry.ChangedByteCount);

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
	Result.SemanticPath = AssetPackageDiff::AppendSemanticPath(Entry.OldFieldPath, Entry.Key);

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
	Explanation.SemanticPath = Entry.Key; // FString::Printf(TEXT("Export:%s"), *Entry.Key);

	OutAnalysis.RelocationCount++;
	OutAnalysis.LayoutChanges.Add(MoveTemp(Explanation));
}

static void AnalyzeUnknownRange(const FAssetPackageDiffEntry& Entry, FAssetSaveAnalysis& OutAnalysis)
{
	if (Entry.State == EAssetPackageDiffState::Unchanged)
	{
		return;
	}

	// A class or function whose native data was read on both sides: what changed in it is known, so it is a semantic change.
	if (Entry.bNativeDataDecoded)
	{
		FAssetSaveExplanationEntry Decoded;
		Decoded.Classification = EAssetSaveChangeClassification::ExportPayloadChanged;
		Decoded.Confidence = EAssetExplanationConfidence::High;
		Decoded.Key = Entry.Key;
		Decoded.Title = Entry.NativeDataTitle.IsEmpty() ? NSLOCTEXT("AssetSaveAnalyzer", "NativeDecoded", "Native data") : Entry.NativeDataTitle;
		Decoded.Description = FText::Format(NSLOCTEXT("AssetSaveAnalyzer", "NativeDecodedDescription", "{0} changes in the data written after the properties; {1} bytes changed."),
			FText::AsNumber(Entry.Children.Num()), FText::AsNumber(Entry.ChangedByteCount));
		Decoded.ChangedByteCount = Entry.ChangedByteCount;
		Decoded.OldOffset = Entry.OldOffset;
		Decoded.NewOffset = Entry.NewOffset;
		Decoded.OldSize = Entry.OldSize;
		Decoded.NewSize = Entry.NewSize;
		Decoded.SemanticPath = AssetPackageDiff::AppendSemanticPath(Entry.OldFieldPath, Entry.Key);

		for (const FAssetPackageDiffEntry& Child : Entry.Children)
		{
			FAssetSaveExplanationEntry Part;
			Part.Classification = EAssetSaveChangeClassification::ExportPayloadChanged;
			Part.Confidence = EAssetExplanationConfidence::High;
			Part.Key = Child.Key;
			Part.Title = Child.DisplayName;
			Part.Description = Child.State == EAssetPackageDiffState::Added ? NSLOCTEXT("AssetSaveAnalyzer", "NativePartAdded", "Added.")
				: Child.State == EAssetPackageDiffState::Removed			? NSLOCTEXT("AssetSaveAnalyzer", "NativePartRemoved", "Removed.")
																			: NSLOCTEXT("AssetSaveAnalyzer", "NativePartChanged", "Changed.");
			Part.bHasOldValue = Child.bHasOldDecodedValue;
			Part.bHasNewValue = Child.bHasNewDecodedValue;
			Part.OldValue = Child.OldValue;
			Part.NewValue = Child.NewValue;
			Part.SemanticPath = Child.SemanticPath;
			Decoded.Children.Add(MoveTemp(Part));
		}

		OutAnalysis.ExplainedChangedBytes += Entry.ChangedByteCount;
		OutAnalysis.PropertyChangeCount += Entry.Children.Num();
		OutAnalysis.SemanticChanges.Add(MoveTemp(Decoded));
		return;
	}

	// A range that was read on both sides and holds the same values: only how they are stored differs.
	if (Entry.bRepresentationOnly)
	{
		FAssetSaveExplanationEntry Stored;
		Stored.Classification = EAssetSaveChangeClassification::PropertyStoredDifferently;
		Stored.Confidence = EAssetExplanationConfidence::Certain;
		Stored.Key = Entry.Key;
		Stored.Title = Entry.NativeDataTitle.IsEmpty() ? FText::FromString(Entry.Key) : Entry.NativeDataTitle;
		Stored.Description = Entry.Explanation;
		Stored.ChangedByteCount = Entry.ChangedByteCount;
		Stored.OldOffset = Entry.OldOffset;
		Stored.NewOffset = Entry.NewOffset;
		Stored.OldSize = Entry.OldSize;
		Stored.NewSize = Entry.NewSize;
		Stored.SemanticPath = AssetPackageDiff::AppendSemanticPath(Entry.OldFieldPath, Entry.Key);

		OutAnalysis.ExplainedChangedBytes += Entry.ChangedByteCount;
		OutAnalysis.LayoutChanges.Add(MoveTemp(Stored));
		return;
	}

	FAssetSaveExplanationEntry Explanation;
	Explanation.Classification = EAssetSaveChangeClassification::NativeOrUndecodedChanged;
	Explanation.Confidence = EAssetExplanationConfidence::Unknown;
	Explanation.Key = Entry.Key;
	Explanation.Title = NSLOCTEXT("AssetSaveAnalyzer", "NativeUndecoded", "Native / undecoded serialization");
	Explanation.ChangedByteCount = Entry.ChangedByteCount;
	Explanation.Description = FText::Format(NSLOCTEXT("AssetSaveAnalyzer", "NativeBytesChanged", "{0} changed bytes of {1} could not be attributed to a decoded serialized property."),
		FText::AsNumber(Entry.ChangedByteCount), FText::AsNumber(FMath::Max(Entry.OldSize, Entry.NewSize)));
	Explanation.CauseDescription = Entry.Explanation;
	Explanation.OldOffset = Entry.OldOffset;
	Explanation.NewOffset = Entry.NewOffset;
	Explanation.OldSize = Entry.OldSize;
	Explanation.NewSize = Entry.NewSize;
	Explanation.SemanticPath = AssetPackageDiff::AppendSemanticPath(Entry.OldFieldPath, Entry.Key);

	OutAnalysis.UnexplainedChangedBytes += Entry.ChangedByteCount;
	OutAnalysis.UnexplainedChanges.Add(MoveTemp(Explanation));
}

static FText DescribeSizeChange(const int64 OldSize, const int64 NewSize)
{
	const int64 Delta = NewSize - OldSize;
	if (Delta == 0)
	{
		return FText::GetEmpty();
	}

	return FText::Format(NSLOCTEXT("AssetSaveAnalyzer", "SizeChange", "{0} by {1} bytes ({2} -> {3})"),
		Delta > 0 ? NSLOCTEXT("AssetSaveAnalyzer", "Grew", "grew") : NSLOCTEXT("AssetSaveAnalyzer", "Shrank", "shrank"), FText::AsNumber(FMath::Abs(Delta)), FText::AsNumber(OldSize),
		FText::AsNumber(NewSize));
}

static FAssetSaveExplanationEntry BuildHeaderFieldExplanation(const FAssetPackageDiffEntry& Field)
{
	FAssetSaveExplanationEntry Result;
	Result.Classification = EAssetSaveChangeClassification::PackageMetadataChanged;
	Result.Confidence = EAssetExplanationConfidence::Certain;
	Result.Key = Field.Key;
	Result.SemanticPath = Field.SemanticPath;
	Result.Title = Field.DisplayName;
	Result.OldValue = Field.OldValue;
	Result.NewValue = Field.NewValue;
	Result.bHasOldValue = Field.State != EAssetPackageDiffState::Added;
	Result.bHasNewValue = Field.State != EAssetPackageDiffState::Removed;
	Result.Description = FText::Format(NSLOCTEXT("AssetSaveAnalyzer", "HeaderFieldChanged", "{0} -> {1}"), FText::FromString(Field.OldValue), FText::FromString(Field.NewValue));

	if (!Field.Explanation.IsEmpty())
	{
		Result.CauseDescription = Field.Explanation;
		Result.CauseConfidence = EAssetExplanationConfidence::High;
	}

	return Result;
}

static FAssetSaveExplanationEntry BuildHeaderRegionExplanation(const FAssetPackageDiffEntry& Region)
{
	FAssetSaveExplanationEntry Result;
	Result.Classification = EAssetSaveChangeClassification::TableChanged;
	Result.Confidence = EAssetExplanationConfidence::Certain;
	Result.Key = Region.Key;
	Result.SemanticPath = Region.SemanticPath;
	Result.Title = Region.DisplayName;
	Result.OldOffset = Region.OldOffset;
	Result.NewOffset = Region.NewOffset;
	Result.OldSize = Region.OldSize;
	Result.NewSize = Region.NewSize;
	Result.ChangedByteCount = Region.ChangedByteCount;
	Result.OldValue = Region.OldValue;
	Result.NewValue = Region.NewValue;
	Result.bHasOldValue = Region.State != EAssetPackageDiffState::Added;
	Result.bHasNewValue = Region.State != EAssetPackageDiffState::Removed;

	switch (Region.State)
	{
		case EAssetPackageDiffState::Added:
			Result.Description = FText::Format(NSLOCTEXT("AssetSaveAnalyzer", "HeaderTableAdded", "New table, {0} bytes."), FText::AsNumber(Region.NewSize));
			break;

		case EAssetPackageDiffState::Removed:
			Result.Description = FText::Format(NSLOCTEXT("AssetSaveAnalyzer", "HeaderTableRemoved", "Table removed; it held {0} bytes."), FText::AsNumber(Region.OldSize));
			break;

		default:
		{
			const FText SizeChange = DescribeSizeChange(Region.OldSize, Region.NewSize);
			Result.Description = !SizeChange.IsEmpty()
				? SizeChange
				: FText::Format(NSLOCTEXT("AssetSaveAnalyzer", "HeaderTableBytesChanged", "{0} of {1} bytes changed."), FText::AsNumber(Region.ChangedByteCount), FText::AsNumber(Region.OldSize));
			break;
		}
	}

	if (!Region.Explanation.IsEmpty())
	{
		Result.CauseDescription = Region.Explanation;
		Result.CauseConfidence = EAssetExplanationConfidence::High;
	}

	// The summary holds the individual fields.
	for (const FAssetPackageDiffEntry& Field : Region.Children)
	{
		if (Field.Kind == EAssetPackageDiffKind::SummaryField && Field.State != EAssetPackageDiffState::Unchanged)
		{
			Result.Classification = EAssetSaveChangeClassification::PackageMetadataChanged;
			Result.Children.Add(BuildHeaderFieldExplanation(Field));
		}
	}

	return Result;
}

/** Describes what changed in the package header: its size, which tables grew, shrank, appeared or went, and which summary fields changed. */
static void AnalyzeHeader(const FAssetPackageDiffEntry& Header, FAssetSaveAnalysis& OutAnalysis)
{
	if (Header.State == EAssetPackageDiffState::Unchanged)
	{
		return;
	}

	FAssetSaveExplanationEntry Result;
	Result.Classification = EAssetSaveChangeClassification::TableChanged;
	Result.Confidence = EAssetExplanationConfidence::Certain;
	Result.Key = Header.Key;
	Result.SemanticPath = Header.SemanticPath;
	Result.Title = Header.DisplayName;
	Result.OldSize = Header.OldSize;
	Result.NewSize = Header.NewSize;
	Result.OldValue = Header.OldValue;
	Result.NewValue = Header.NewValue;
	Result.bHasOldValue = true;
	Result.bHasNewValue = true;

	int32 MovedTables = 0;
	for (const FAssetPackageDiffEntry& Region : Header.Children)
	{
		if (Region.State == EAssetPackageDiffState::Unchanged)
		{
			continue;
		}

		if (Region.State == EAssetPackageDiffState::Moved)
		{
			++MovedTables;
			continue;
		}

		Result.Children.Add(BuildHeaderRegionExplanation(Region));
		++OutAnalysis.HeaderChangeCount;

		// The header's changes are understood (they are tables with a stated reason), so their bytes count as explained.
		OutAnalysis.ExplainedChangedBytes +=
			Region.State == EAssetPackageDiffState::Added ? Region.NewSize : (Region.State == EAssetPackageDiffState::Removed ? Region.OldSize : Region.ChangedByteCount);
	}

	const FText SizeChange = DescribeSizeChange(Header.OldSize, Header.NewSize);
	FText Description = SizeChange.IsEmpty() ? NSLOCTEXT("AssetSaveAnalyzer", "HeaderSameSize", "The header kept its size.")
											 : FText::Format(NSLOCTEXT("AssetSaveAnalyzer", "HeaderSizeChanged", "The header {0}."), SizeChange);
	if (MovedTables > 0)
	{
		Description =
			FText::Format(NSLOCTEXT("AssetSaveAnalyzer", "HeaderMovedTables", "{0} {1} other tables were only moved (their stored offsets shifted)."), Description, FText::AsNumber(MovedTables));
	}

	Result.Description = Description;
	if (!Header.Explanation.IsEmpty())
	{
		Result.CauseDescription = Header.Explanation;
		Result.CauseConfidence = EAssetExplanationConfidence::High;
	}

	OutAnalysis.HeaderChanges.Add(MoveTemp(Result));
}

static void AnalyzeEntry(const FAssetPackageDiffEntry& Entry, const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument, FAssetSaveAnalysis& OutAnalysis)
{
	switch (Entry.Kind)
	{
		case EAssetPackageDiffKind::Header:
			AnalyzeHeader(Entry, OutAnalysis);
			return;

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

	Result.TotalChangedBytes = Result.ExplainedChangedBytes + Result.UnexplainedChangedBytes;

	FinalizeResult(Result);

	return Result;
}

const TCHAR* LexToString(const EAssetSaveChangeClassification Classification)
{
	switch (Classification)
	{
		case EAssetSaveChangeClassification::Unknown:
			return TEXT("Unknown");
		case EAssetSaveChangeClassification::PropertyValueChanged:
			return TEXT("PropertyValueChanged");
		case EAssetSaveChangeClassification::PropertyBecameSerialized:
			return TEXT("PropertyBecameSerialized");
		case EAssetSaveChangeClassification::PropertyBecameOmitted:
			return TEXT("PropertyBecameOmitted");
		case EAssetSaveChangeClassification::ContainerChanged:
			return TEXT("ContainerChanged");
		case EAssetSaveChangeClassification::ExportPayloadChanged:
			return TEXT("ExportPayloadChanged");
		case EAssetSaveChangeClassification::ExportRelocated:
			return TEXT("ExportRelocated");
		case EAssetSaveChangeClassification::PackageMetadataChanged:
			return TEXT("PackageMetadataChanged");
		case EAssetSaveChangeClassification::TableChanged:
			return TEXT("TableChanged");
		case EAssetSaveChangeClassification::NativeOrUndecodedChanged:
			return TEXT("NativeOrUndecodedChanged");
		case EAssetSaveChangeClassification::PropertyStoredDifferently:
			return TEXT("PropertyStoredDifferently");
	}

	return TEXT("Unknown");
}
