// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Report/AssetReportWriter.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"

#include "Report/AssetAnalysisReport.h"

#define LOCTEXT_NAMESPACE "AssetReportWriter"

namespace
{
	using FJsonWriter = TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>;

	// ---- Names shared by both formats ----

	const TCHAR* NameOf(const EAssetPackageDiffKind Kind)
	{
		switch (Kind)
		{
			case EAssetPackageDiffKind::File:
				return TEXT("File");
			case EAssetPackageDiffKind::SummaryField:
				return TEXT("SummaryField");
			case EAssetPackageDiffKind::Header:
				return TEXT("Header");
			case EAssetPackageDiffKind::HeaderRegion:
				return TEXT("HeaderRegion");
			case EAssetPackageDiffKind::Name:
				return TEXT("Name");
			case EAssetPackageDiffKind::Import:
				return TEXT("Import");
			case EAssetPackageDiffKind::Export:
				return TEXT("Export");
			case EAssetPackageDiffKind::ExportPayload:
				return TEXT("ExportPayload");
			case EAssetPackageDiffKind::Property:
				return TEXT("Property");
			case EAssetPackageDiffKind::UnknownPayloadRange:
				return TEXT("UnknownPayloadRange");
		}

		return TEXT("Unknown");
	}

	const TCHAR* NameOf(const EAssetPackageDiffState State)
	{
		switch (State)
		{
			case EAssetPackageDiffState::Unchanged:
				return TEXT("Unchanged");
			case EAssetPackageDiffState::Added:
				return TEXT("Added");
			case EAssetPackageDiffState::Removed:
				return TEXT("Removed");
			case EAssetPackageDiffState::Modified:
				return TEXT("Modified");
			case EAssetPackageDiffState::Moved:
				return TEXT("Moved");
		}

		return TEXT("Unknown");
	}

	const TCHAR* NameOf(const EAssetSaveChangeClassification Classification)
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
		}

		return TEXT("Unknown");
	}

	const TCHAR* NameOf(const EAssetExplanationConfidence Confidence)
	{
		switch (Confidence)
		{
			case EAssetExplanationConfidence::Certain:
				return TEXT("Certain");
			case EAssetExplanationConfidence::High:
				return TEXT("High");
			case EAssetExplanationConfidence::Inferred:
				return TEXT("Inferred");
			case EAssetExplanationConfidence::Unknown:
				return TEXT("Unknown");
		}

		return TEXT("Unknown");
	}

	const TCHAR* NameOf(const EAssetSaveResultKind ResultKind)
	{
		switch (ResultKind)
		{
			case EAssetSaveResultKind::Identical:
				return TEXT("Identical");
			case EAssetSaveResultKind::LayoutOnly:
				return TEXT("LayoutOnly");
			case EAssetSaveResultKind::MetadataOnly:
				return TEXT("MetadataOnly");
			case EAssetSaveResultKind::SemanticChanges:
				return TEXT("SemanticChanges");
			case EAssetSaveResultKind::SemanticAndNativeChanges:
				return TEXT("SemanticAndNativeChanges");
			case EAssetSaveResultKind::NativeOnlyChanges:
				return TEXT("NativeOnlyChanges");
		}

		return TEXT("Unknown");
	}

	const TCHAR* NameOf(const EObservedValuePattern Pattern)
	{
		switch (Pattern)
		{
			case EObservedValuePattern::Unknown:
				return TEXT("Unknown");
			case EObservedValuePattern::Stable:
				return TEXT("Stable");
			case EObservedValuePattern::ChangedOnce:
				return TEXT("ChangedOnce");
			case EObservedValuePattern::Recurring:
				return TEXT("Recurring");
			case EObservedValuePattern::ChangedEverySave:
				return TEXT("ChangedEverySave");
			case EObservedValuePattern::ContinuouslyChanging:
				return TEXT("ContinuouslyChanging");
			case EObservedValuePattern::Alternating:
				return TEXT("Alternating");
		}

		return TEXT("Unknown");
	}

	/** The value shown for one side of a difference: the decoded value when there is one, otherwise the raw text. */
	const FString& SideValue(const FAssetPackageDiffEntry& Entry, const bool bOld)
	{
		const bool bHasDecoded = bOld ? Entry.bHasOldDecodedValue : Entry.bHasNewDecodedValue;
		return bHasDecoded ? (bOld ? Entry.OldDecodedValue : Entry.NewDecodedValue) : (bOld ? Entry.OldValue : Entry.NewValue);
	}

	// ---- Text ----

	FString OneLine(const FString& Value)
	{
		FString Result = Value;
		Result.ReplaceInline(TEXT("\r\n"), TEXT(" | "));
		Result.ReplaceInline(TEXT("\n"), TEXT(" | "));
		Result.ReplaceInline(TEXT("\r"), TEXT(" | "));
		return Result;
	}

	FString Indent(const int32 Depth)
	{
		return FString::ChrN(Depth * 2, TEXT(' '));
	}

	FString DescribeSide(const FAssetPackageDiffEntry& Entry, const bool bOld)
	{
		const FString& Value = SideValue(Entry, bOld);
		const FString& Final = bOld ? Entry.OldFinalValue : Entry.NewFinalValue;

		FString Text = OneLine(Value);
		if (!Final.IsEmpty())
		{
			Text += FString::Printf(TEXT(" -> %s"), *OneLine(Final));
		}

		return Text;
	}

	void AppendDifference(const FAssetPackageDiffEntry& Entry, const int32 Depth, TArray<FString>& OutLines)
	{
		FString Line = FString::Printf(TEXT("%s[%s] %s"), *Indent(Depth), NameOf(Entry.State), *Entry.DisplayName.ToString());

		if (!Entry.TypeName.IsEmpty())
		{
			Line += FString::Printf(TEXT(" (%s)"), *Entry.TypeName);
		}

		const FString Old = DescribeSide(Entry, true);
		const FString New = DescribeSide(Entry, false);
		if (!Old.IsEmpty() || !New.IsEmpty())
		{
			Line += FString::Printf(TEXT(": %s => %s"), Old.IsEmpty() ? TEXT("(none)") : *Old, New.IsEmpty() ? TEXT("(none)") : *New);
		}
		else if (Entry.ChangedByteCount > 0)
		{
			Line += FString::Printf(TEXT(": %lld changed bytes"), Entry.ChangedByteCount);
		}

		OutLines.Add(Line);

		if (!Entry.Explanation.IsEmpty())
		{
			OutLines.Add(FString::Printf(TEXT("%s    why: %s"), *Indent(Depth), *OneLine(Entry.Explanation.ToString())));
		}

		for (const FString& Note : { Entry.OldFinalValueNote, Entry.NewFinalValueNote })
		{
			if (!Note.IsEmpty())
			{
				OutLines.Add(FString::Printf(TEXT("%s    note: %s"), *Indent(Depth), *OneLine(Note)));
			}
		}

		for (const FAssetPackageDiffEntry& Child : Entry.Children)
		{
			AppendDifference(Child, Depth + 1, OutLines);
		}
	}

	void AppendExplanation(const FAssetSaveExplanationEntry& Entry, const int32 Depth, TArray<FString>& OutLines)
	{
		FString Line = FString::Printf(TEXT("%s- %s [%s]"), *Indent(Depth), *Entry.Title.ToString(), NameOf(Entry.Confidence));

		const FString Description = Entry.Description.ToString();
		if (!Description.IsEmpty())
		{
			Line += FString::Printf(TEXT(": %s"), *OneLine(Description));
		}

		if (Entry.bHasOldValue || Entry.bHasNewValue)
		{
			Line += FString::Printf(TEXT(" (%s => %s)"), Entry.bHasOldValue ? *OneLine(Entry.OldValue) : TEXT("(none)"), Entry.bHasNewValue ? *OneLine(Entry.NewValue) : TEXT("(none)"));
		}

		OutLines.Add(Line);

		const FString Cause = Entry.CauseDescription.ToString();
		if (!Cause.IsEmpty())
		{
			OutLines.Add(FString::Printf(TEXT("%s    cause [%s]: %s"), *Indent(Depth), NameOf(Entry.CauseConfidence), *OneLine(Cause)));
		}

		for (const FAssetSaveExplanationEntry& Child : Entry.Children)
		{
			AppendExplanation(Child, Depth + 1, OutLines);
		}
	}

	void AppendExplanationSection(const TCHAR* Title, const TArray<FAssetSaveExplanationEntry>& Entries, TArray<FString>& OutLines)
	{
		OutLines.Add(FString::Printf(TEXT("  %s (%d)"), Title, Entries.Num()));

		for (const FAssetSaveExplanationEntry& Entry : Entries)
		{
			AppendExplanation(Entry, 2, OutLines);
		}
	}

	// ---- JSON ----

	void WriteOptionalString(FJsonWriter& Writer, const FString& Key, const FString& Value, const bool bPresent)
	{
		if (bPresent)
		{
			Writer.WriteValue(Key, Value);
		}
		else
		{
			Writer.WriteNull(Key);
		}
	}

	void WriteSide(FJsonWriter& Writer, const FString& Key, const FAssetPackageDiffEntry& Entry, const bool bOld)
	{
		const FString& Value = SideValue(Entry, bOld);
		const bool bHasValue = (bOld ? Entry.bHasOldDecodedValue : Entry.bHasNewDecodedValue) || !Value.IsEmpty();
		const FString& Final = bOld ? Entry.OldFinalValue : Entry.NewFinalValue;
		const FString& Note = bOld ? Entry.OldFinalValueNote : Entry.NewFinalValueNote;
		const EAssetSerializedPropertyPresence Presence = bOld ? Entry.OldPresence : Entry.NewPresence;

		Writer.WriteObjectStart(Key);
		WriteOptionalString(Writer, TEXT("value"), Value, bHasValue);
		WriteOptionalString(Writer, TEXT("finalValue"), Final, !Final.IsEmpty());
		WriteOptionalString(Writer, TEXT("note"), Note, !Note.IsEmpty());
		Writer.WriteValue(TEXT("presence"), Presence == EAssetSerializedPropertyPresence::NotSerialized ? TEXT("NotSerialized") : TEXT("Present"));
		Writer.WriteObjectEnd();
	}

	void WriteOffset(FJsonWriter& Writer, const FString& Key, const int64 Value)
	{
		if (Value == INDEX_NONE)
		{
			Writer.WriteNull(Key);
		}
		else
		{
			Writer.WriteValue(Key, Value);
		}
	}

	void WriteDifference(FJsonWriter& Writer, const FAssetPackageDiffEntry& Entry)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("kind"), NameOf(Entry.Kind));
		Writer.WriteValue(TEXT("state"), NameOf(Entry.State));
		Writer.WriteValue(TEXT("key"), Entry.Key);
		Writer.WriteValue(TEXT("name"), Entry.DisplayName.ToString());
		Writer.WriteValue(TEXT("semanticPath"), Entry.SemanticPath);
		Writer.WriteValue(TEXT("type"), Entry.TypeName);
		WriteOptionalString(Writer, TEXT("explanation"), Entry.Explanation.ToString(), !Entry.Explanation.IsEmpty());
		WriteSide(Writer, TEXT("old"), Entry, true);
		WriteSide(Writer, TEXT("new"), Entry, false);
		Writer.WriteValue(TEXT("changedBytes"), Entry.ChangedByteCount);
		WriteOffset(Writer, TEXT("oldOffset"), Entry.OldOffset);
		WriteOffset(Writer, TEXT("newOffset"), Entry.NewOffset);
		Writer.WriteValue(TEXT("oldSize"), Entry.OldSize);
		Writer.WriteValue(TEXT("newSize"), Entry.NewSize);

		Writer.WriteArrayStart(TEXT("children"));
		for (const FAssetPackageDiffEntry& Child : Entry.Children)
		{
			WriteDifference(Writer, Child);
		}
		Writer.WriteArrayEnd();

		Writer.WriteObjectEnd();
	}

	void WriteExplanation(FJsonWriter& Writer, const FAssetSaveExplanationEntry& Entry)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("classification"), NameOf(Entry.Classification));
		Writer.WriteValue(TEXT("confidence"), NameOf(Entry.Confidence));
		Writer.WriteValue(TEXT("causeConfidence"), NameOf(Entry.CauseConfidence));
		Writer.WriteValue(TEXT("key"), Entry.Key);
		Writer.WriteValue(TEXT("semanticPath"), Entry.SemanticPath);
		Writer.WriteValue(TEXT("title"), Entry.Title.ToString());
		Writer.WriteValue(TEXT("description"), Entry.Description.ToString());
		Writer.WriteValue(TEXT("cause"), Entry.CauseDescription.ToString());
		Writer.WriteValue(TEXT("changedBytes"), Entry.ChangedByteCount);
		WriteOffset(Writer, TEXT("oldOffset"), Entry.OldOffset);
		WriteOffset(Writer, TEXT("newOffset"), Entry.NewOffset);
		WriteOptionalString(Writer, TEXT("oldValue"), Entry.OldValue, Entry.bHasOldValue);
		WriteOptionalString(Writer, TEXT("newValue"), Entry.NewValue, Entry.bHasNewValue);

		Writer.WriteArrayStart(TEXT("children"));
		for (const FAssetSaveExplanationEntry& Child : Entry.Children)
		{
			WriteExplanation(Writer, Child);
		}
		Writer.WriteArrayEnd();

		Writer.WriteObjectEnd();
	}

	void WriteExplanations(FJsonWriter& Writer, const FString& Key, const TArray<FAssetSaveExplanationEntry>& Entries)
	{
		Writer.WriteArrayStart(Key);
		for (const FAssetSaveExplanationEntry& Entry : Entries)
		{
			WriteExplanation(Writer, Entry);
		}
		Writer.WriteArrayEnd();
	}

	void WriteFile(FJsonWriter& Writer, const FString& Key, const FString& Path, const FString& Hash)
	{
		Writer.WriteObjectStart(Key);
		Writer.WriteValue(TEXT("path"), Path);
		Writer.WriteValue(TEXT("hash"), Hash);
		Writer.WriteObjectEnd();
	}
} // namespace

FString AssetReportWriter::ToText(const FAssetAnalysisReport& Report)
{
	TArray<FString> Lines;

	Lines.Add(TEXT("Asset Serialization Inspector report"));
	Lines.Add(FString::Printf(TEXT("Tool version: %s"), Report.ToolVersion.IsEmpty() ? TEXT("unknown") : *Report.ToolVersion));
	Lines.Add(FString::Printf(TEXT("Generated: %s"), *Report.GeneratedAtUtc.ToIso8601()));
	Lines.Add(FString());
	Lines.Add(FString::Printf(TEXT("Old: %s"), *Report.OldFilename));
	Lines.Add(FString::Printf(TEXT("     hash %s"), *Report.OldFileHash));
	Lines.Add(FString::Printf(TEXT("New: %s"), *Report.NewFilename));
	Lines.Add(FString::Printf(TEXT("     hash %s"), *Report.NewFileHash));
	Lines.Add(FString());

	if (Report.bFilesIdentical)
	{
		Lines.Add(TEXT("The files are byte-identical."));
	}
	else
	{
		Lines.Add(FString::Printf(TEXT("Summary: %d modified, %d moved, %d added, %d removed"), Report.Summary.Modified, Report.Summary.Moved, Report.Summary.Added, Report.Summary.Removed));
	}

	if (Report.SaveAnalysis.IsSet())
	{
		const FAssetSaveAnalysis& Analysis = Report.SaveAnalysis.GetValue();

		Lines.Add(FString());
		Lines.Add(FString::Printf(TEXT("Save analysis: %s"), NameOf(Analysis.ResultKind)));
		Lines.Add(FString::Printf(TEXT("  %d property changes, %d relocations, %d header table changes"), Analysis.PropertyChangeCount, Analysis.RelocationCount, Analysis.HeaderChangeCount));
		Lines.Add(FString::Printf(TEXT("  %lld changed bytes: %lld explained, %lld unexplained"), Analysis.TotalChangedBytes, Analysis.ExplainedChangedBytes, Analysis.UnexplainedChangedBytes));
		AppendExplanationSection(TEXT("Semantic changes"), Analysis.SemanticChanges, Lines);
		AppendExplanationSection(TEXT("Layout changes"), Analysis.LayoutChanges, Lines);
		AppendExplanationSection(TEXT("Package header changes"), Analysis.HeaderChanges, Lines);
		AppendExplanationSection(TEXT("Unexplained changes"), Analysis.UnexplainedChanges, Lines);
	}

	Lines.Add(FString());
	Lines.Add(Report.FilterDescription.IsEmpty() ? FString(TEXT("Differences")) : FString::Printf(TEXT("Differences (filtered: %s)"), *Report.FilterDescription));

	if (Report.Differences.IsEmpty())
	{
		Lines.Add(TEXT("  (none)"));
	}

	for (const FAssetPackageDiffEntry& Entry : Report.Differences)
	{
		AppendDifference(Entry, 1, Lines);
	}

	if (!Report.RepeatedSavePatterns.IsEmpty())
	{
		Lines.Add(FString());
		Lines.Add(TEXT("Repeated saves"));

		for (const FRepeatedSavePattern& Pattern : Report.RepeatedSavePatterns)
		{
			Lines.Add(FString::Printf(TEXT("  %s (%s): %s, changed in %d of %d saves, %lld bytes"), *Pattern.DisplayName.ToString(), *Pattern.SemanticPath, NameOf(Pattern.ValuePattern),
				Pattern.ChangeCount, Pattern.ObservationCount, Pattern.TotalChangedBytes));

			for (const FObservedPropertySample& Sample : Pattern.Samples)
			{
				if (Sample.bChanged)
				{
					Lines.Add(FString::Printf(TEXT("    %s: %s => %s"), *Sample.Timestamp.ToIso8601(), Sample.bHasOldValue ? *OneLine(Sample.OldValue) : TEXT("(none)"),
						Sample.bHasNewValue ? *OneLine(Sample.NewValue) : TEXT("(none)")));
				}
			}
		}
	}

	return FString::Join(Lines, TEXT("\n")) + TEXT("\n");
}

FString AssetReportWriter::ToJson(const FAssetAnalysisReport& Report)
{
	FString Output;
	const TSharedRef<FJsonWriter> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);

	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("schemaVersion"), static_cast<int64>(FAssetAnalysisReport::SchemaVersion));

	Writer->WriteObjectStart(TEXT("tool"));
	Writer->WriteValue(TEXT("name"), TEXT("AssetSerializationInspector"));
	WriteOptionalString(*Writer, TEXT("version"), Report.ToolVersion, !Report.ToolVersion.IsEmpty());
	Writer->WriteObjectEnd();

	Writer->WriteValue(TEXT("generatedAt"), Report.GeneratedAtUtc.ToIso8601());

	Writer->WriteObjectStart(TEXT("files"));
	WriteFile(*Writer, TEXT("old"), Report.OldFilename, Report.OldFileHash);
	WriteFile(*Writer, TEXT("new"), Report.NewFilename, Report.NewFileHash);
	Writer->WriteValue(TEXT("identical"), Report.bFilesIdentical);
	Writer->WriteObjectEnd();

	Writer->WriteObjectStart(TEXT("summary"));
	Writer->WriteValue(TEXT("added"), static_cast<int64>(Report.Summary.Added));
	Writer->WriteValue(TEXT("removed"), static_cast<int64>(Report.Summary.Removed));
	Writer->WriteValue(TEXT("modified"), static_cast<int64>(Report.Summary.Modified));
	Writer->WriteValue(TEXT("moved"), static_cast<int64>(Report.Summary.Moved));
	Writer->WriteObjectEnd();

	WriteOptionalString(*Writer, TEXT("filter"), Report.FilterDescription, !Report.FilterDescription.IsEmpty());

	Writer->WriteArrayStart(TEXT("differences"));
	for (const FAssetPackageDiffEntry& Entry : Report.Differences)
	{
		WriteDifference(*Writer, Entry);
	}
	Writer->WriteArrayEnd();

	if (Report.SaveAnalysis.IsSet())
	{
		const FAssetSaveAnalysis& Analysis = Report.SaveAnalysis.GetValue();

		Writer->WriteObjectStart(TEXT("saveAnalysis"));
		Writer->WriteValue(TEXT("result"), NameOf(Analysis.ResultKind));
		Writer->WriteValue(TEXT("propertyChanges"), static_cast<int64>(Analysis.PropertyChangeCount));
		Writer->WriteValue(TEXT("relocations"), static_cast<int64>(Analysis.RelocationCount));
		Writer->WriteValue(TEXT("headerChanges"), static_cast<int64>(Analysis.HeaderChangeCount));
		Writer->WriteValue(TEXT("totalChangedBytes"), Analysis.TotalChangedBytes);
		Writer->WriteValue(TEXT("explainedChangedBytes"), Analysis.ExplainedChangedBytes);
		Writer->WriteValue(TEXT("unexplainedChangedBytes"), Analysis.UnexplainedChangedBytes);
		WriteExplanations(*Writer, TEXT("semanticChanges"), Analysis.SemanticChanges);
		WriteExplanations(*Writer, TEXT("layoutChanges"), Analysis.LayoutChanges);
		WriteExplanations(*Writer, TEXT("headerChangeDetails"), Analysis.HeaderChanges);
		WriteExplanations(*Writer, TEXT("unexplainedChanges"), Analysis.UnexplainedChanges);
		Writer->WriteObjectEnd();
	}
	else
	{
		Writer->WriteNull(TEXT("saveAnalysis"));
	}

	Writer->WriteArrayStart(TEXT("repeatedSaves"));
	for (const FRepeatedSavePattern& Pattern : Report.RepeatedSavePatterns)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("semanticPath"), Pattern.SemanticPath);
		Writer->WriteValue(TEXT("name"), Pattern.DisplayName.ToString());
		Writer->WriteValue(TEXT("observations"), static_cast<int64>(Pattern.ObservationCount));
		Writer->WriteValue(TEXT("changes"), static_cast<int64>(Pattern.ChangeCount));
		Writer->WriteValue(TEXT("totalChangedBytes"), Pattern.TotalChangedBytes);
		Writer->WriteValue(TEXT("pattern"), NameOf(Pattern.ValuePattern));

		Writer->WriteArrayStart(TEXT("samples"));
		for (const FObservedPropertySample& Sample : Pattern.Samples)
		{
			Writer->WriteObjectStart();
			Writer->WriteValue(TEXT("saveId"), FString::Printf(TEXT("%llu"), Sample.SaveId));
			Writer->WriteValue(TEXT("timestamp"), Sample.Timestamp.ToIso8601());
			Writer->WriteValue(TEXT("changed"), Sample.bChanged);
			WriteOptionalString(*Writer, TEXT("oldValue"), Sample.OldValue, Sample.bHasOldValue);
			WriteOptionalString(*Writer, TEXT("newValue"), Sample.NewValue, Sample.bHasNewValue);
			Writer->WriteValue(TEXT("changedBytes"), Sample.ChangedByteCount);
			Writer->WriteValue(TEXT("classification"), NameOf(Sample.Classification));
			Writer->WriteObjectEnd();
		}
		Writer->WriteArrayEnd();

		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

	Writer->WriteObjectEnd();
	Writer->Close();

	return Output;
}

FString AssetReportWriter::Write(const FAssetAnalysisReport& Report, const EAssetReportFormat Format)
{
	return Format == EAssetReportFormat::Json ? ToJson(Report) : ToText(Report);
}

const TCHAR* AssetReportWriter::GetFileExtension(const EAssetReportFormat Format)
{
	return Format == EAssetReportFormat::Json ? TEXT("json") : TEXT("txt");
}

FString AssetReportWriter::MakeDefaultFilename(const FString& AssetFilename, const FDateTime& Time, const EAssetReportFormat Format)
{
	FString AssetName = FPaths::GetBaseFilename(AssetFilename);

	if (AssetName.IsEmpty())
	{
		AssetName = TEXT("Asset");
	}

	return FPaths::MakeValidFileName(FString::Printf(TEXT("%s_Report_%s.%s"), *AssetName, *Time.ToString(TEXT("%Y%m%d-%H%M%S")), GetFileExtension(Format)));
}

EAssetReportFormat AssetReportWriter::GetFormatForFilename(const FString& Filename)
{
	return FPaths::GetExtension(Filename).Equals(TEXT("json"), ESearchCase::IgnoreCase) ? EAssetReportFormat::Json : EAssetReportFormat::Text;
}

bool AssetReportWriter::SaveToFile(const FAssetAnalysisReport& Report, const FString& Filename, FText& OutError)
{
	if (Filename.IsEmpty())
	{
		OutError = LOCTEXT("NoReportFilename", "No filename was provided for the report.");
		return false;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);

	if (!FFileHelper::SaveStringToFile(Write(Report, GetFormatForFilename(Filename)), *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FText::Format(LOCTEXT("ReportWriteFailed", "The report could not be written to:\n{0}"), FText::FromString(Filename));
		return false;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
