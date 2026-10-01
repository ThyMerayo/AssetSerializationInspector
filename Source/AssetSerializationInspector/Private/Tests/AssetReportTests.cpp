// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "Diff/AssetDiffFilter.h"
#include "Report/AssetAnalysisReport.h"
#include "Report/AssetReportWriter.h"

namespace AssetReportTestUtils
{
	static FAssetPackageDiffEntry MakeProperty(const FString& Name, const EAssetPackageDiffState State, const FString& OldValue, const FString& NewValue)
	{
		FAssetPackageDiffEntry Entry;
		Entry.Kind = EAssetPackageDiffKind::Property;
		Entry.State = State;
		Entry.Key = Name;
		Entry.DisplayName = FText::FromString(Name);
		Entry.SemanticPath = TEXT("Export/") + Name;
		Entry.TypeName = TEXT("IntProperty");
		Entry.OldDecodedValue = OldValue;
		Entry.NewDecodedValue = NewValue;
		Entry.bHasOldDecodedValue = !OldValue.IsEmpty();
		Entry.bHasNewDecodedValue = !NewValue.IsEmpty();
		return Entry;
	}

	/** An export with a changed Health, an added Name and an unchanged Mana. */
	static FAssetPackageDiffResult MakeDiff()
	{
		FAssetPackageDiffEntry Export;
		Export.Kind = EAssetPackageDiffKind::ExportPayload;
		Export.State = EAssetPackageDiffState::Modified;
		Export.Key = TEXT("Export");
		Export.DisplayName = FText::FromString(TEXT("Export"));
		Export.SemanticPath = TEXT("Export");

		FAssetPackageDiffEntry Health = MakeProperty(TEXT("Health"), EAssetPackageDiffState::Modified, TEXT("50"), TEXT("75"));
		Health.NewFinalValue = TEXT("2 elements: a, b");
		Health.NewFinalValueNote = TEXT("Assumes an empty default.");
		Export.Children.Add(Health);
		Export.Children.Add(MakeProperty(TEXT("Name"), EAssetPackageDiffState::Added, FString(), TEXT("Bob\nSmith")));
		Export.Children.Add(MakeProperty(TEXT("Mana"), EAssetPackageDiffState::Unchanged, TEXT("10"), TEXT("10")));

		FAssetPackageDiffResult Diff;
		Diff.OldFilename = TEXT("D:/Old.uasset");
		Diff.NewFilename = TEXT("D:/New.uasset");
		Diff.OldFileHash = TEXT("aaaa");
		Diff.NewFileHash = TEXT("bbbb");
		Diff.Entries.Add(Export);
		return Diff;
	}

	static FAssetSaveAnalysis MakeAnalysis()
	{
		FAssetSaveExplanationEntry Entry;
		Entry.Classification = EAssetSaveChangeClassification::PropertyValueChanged;
		Entry.Confidence = EAssetExplanationConfidence::Certain;
		Entry.CauseConfidence = EAssetExplanationConfidence::Inferred;
		Entry.Title = FText::FromString(TEXT("Health changed"));
		Entry.Description = FText::FromString(TEXT("The value was edited."));
		Entry.CauseDescription = FText::FromString(TEXT("A user edit."));
		Entry.OldValue = TEXT("50");
		Entry.NewValue = TEXT("75");
		Entry.bHasOldValue = true;
		Entry.bHasNewValue = true;
		Entry.OldOffset = 16;

		FAssetSaveAnalysis Analysis;
		Analysis.ResultKind = EAssetSaveResultKind::SemanticChanges;
		Analysis.PropertyChangeCount = 1;
		Analysis.TotalChangedBytes = 8;
		Analysis.ExplainedChangedBytes = 4;
		Analysis.UnexplainedChangedBytes = 4;
		Analysis.SemanticChanges.Add(Entry);
		return Analysis;
	}

	static FRepeatedSavePattern MakePattern()
	{
		FObservedPropertySample Sample;
		Sample.SaveId = 7;
		Sample.Timestamp = FDateTime(2026, 1, 2, 3, 4, 5);
		Sample.bChanged = true;
		Sample.bHasOldValue = true;
		Sample.bHasNewValue = true;
		Sample.OldValue = TEXT("1");
		Sample.NewValue = TEXT("2");

		FRepeatedSavePattern Pattern;
		Pattern.SemanticPath = TEXT("Export/Counter");
		Pattern.DisplayName = FText::FromString(TEXT("Counter"));
		Pattern.ObservationCount = 3;
		Pattern.ChangeCount = 2;
		Pattern.TotalChangedBytes = 8;
		Pattern.ValuePattern = EObservedValuePattern::ChangedEverySave;
		Pattern.Samples.Add(Sample);
		return Pattern;
	}

	static FAssetAnalysisReport MakeReport()
	{
		const FAssetPackageDiffResult Diff = MakeDiff();
		const FAssetSaveAnalysis Analysis = MakeAnalysis();
		// The default filter is the view's default: changed entries only.
		const FAssetDiffFilter DefaultFilter;
		FAssetAnalysisReport Report = AssetAnalysisReport::Build(Diff, &Analysis, { MakePattern() }, &DefaultFilter);
		Report.GeneratedAtUtc = FDateTime(2026, 1, 2, 3, 4, 5);
		Report.ToolVersion = TEXT("0.1");
		return Report;
	}
} // namespace AssetReportTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetReport_BuildSummarizesAndFilters, "AssetSerializationInspector.Report.AssetAnalysisReport.BuildSummarizesAndFilters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetReport_BuildSummarizesAndFilters::RunTest(const FString& Parameters)
{
	using namespace AssetReportTestUtils;

	const FAssetPackageDiffResult Diff = MakeDiff();

	const FAssetAnalysisReport Full = AssetAnalysisReport::Build(Diff, nullptr, {}, nullptr);
	TestEqual(TEXT("Modified entries are counted at every depth"), Full.Summary.Modified, 2);
	TestEqual(TEXT("Added entries are counted"), Full.Summary.Added, 1);
	TestEqual(TEXT("Unchanged entries are not counted"), Full.Summary.Removed + Full.Summary.Moved, 0);
	TestEqual(TEXT("Without a filter the full diff is listed"), Full.Differences[0].Children.Num(), 3);
	TestTrue(TEXT("Without a filter there is no filter description"), Full.FilterDescription.IsEmpty());
	TestFalse(TEXT("No analysis was supplied"), Full.SaveAnalysis.IsSet());

	FAssetDiffFilter Filter;
	Filter.States = EAssetDiffStateFilter::Added;
	const FAssetAnalysisReport Filtered = AssetAnalysisReport::Build(Diff, nullptr, {}, &Filter);
	TestEqual(TEXT("A filter prunes the listed differences"), Filtered.Differences[0].Children.Num(), 1);
	TestEqual(TEXT("The summary still describes the whole comparison"), Filtered.Summary.Modified, 2);
	TestEqual(TEXT("The filter is described"), Filtered.FilterDescription, FString(TEXT("states: added")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetReport_WritesText, "AssetSerializationInspector.Report.AssetReportWriter.WritesText", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetReport_WritesText::RunTest(const FString& Parameters)
{
	using namespace AssetReportTestUtils;

	const FString Text = AssetReportWriter::ToText(MakeReport());

	TestTrue(TEXT("The header names the files"), Text.Contains(TEXT("Old: D:/Old.uasset")) && Text.Contains(TEXT("New: D:/New.uasset")));
	TestTrue(TEXT("The generation time is included"), Text.Contains(TEXT("Generated: 2026-01-02T03:04:05")));
	TestTrue(TEXT("The summary counts the states"), Text.Contains(TEXT("Summary: 2 modified, 0 moved, 1 added, 0 removed")));
	TestTrue(TEXT("Changed properties show old and new values"), Text.Contains(TEXT("[Modified] Health (IntProperty): 50 => 75 -> 2 elements: a, b")));
	TestTrue(TEXT("Multi-line values are collapsed"), Text.Contains(TEXT("(none) => Bob | Smith")));
	TestTrue(TEXT("Notes are included"), Text.Contains(TEXT("note: Assumes an empty default.")));
	TestFalse(TEXT("Unchanged entries are not listed"), Text.Contains(TEXT("Mana")));
	TestTrue(TEXT("The save analysis is included"), Text.Contains(TEXT("Save analysis: SemanticChanges")) && Text.Contains(TEXT("- Health changed [Certain]: The value was edited. (50 => 75)")));
	TestTrue(TEXT("The cause is included"), Text.Contains(TEXT("cause [Inferred]: A user edit.")));
	TestTrue(TEXT("Empty sections are listed as empty"), Text.Contains(TEXT("Layout changes (0)")));
	TestTrue(TEXT("Repeated-save patterns are included"), Text.Contains(TEXT("Counter (Export/Counter): ChangedEverySave, changed in 2 of 3 saves")));

	FAssetAnalysisReport Identical;
	Identical.bFilesIdentical = true;
	const FString IdenticalText = AssetReportWriter::ToText(Identical);
	TestTrue(TEXT("Identical files are stated"), IdenticalText.Contains(TEXT("byte-identical")));
	TestTrue(TEXT("An empty differences list says so"), IdenticalText.Contains(TEXT("(none)")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetReport_WritesJson, "AssetSerializationInspector.Report.AssetReportWriter.WritesJson", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetReport_WritesJson::RunTest(const FString& Parameters)
{
	using namespace AssetReportTestUtils;

	const FString Json = AssetReportWriter::ToJson(MakeReport());

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!TestTrue(TEXT("The output is valid JSON"), FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid()))
	{
		return false;
	}

	TestEqual(TEXT("The schema is versioned"), static_cast<int32>(Root->GetNumberField(TEXT("schemaVersion"))), FAssetAnalysisReport::SchemaVersion);
	TestEqual(TEXT("The tool version is recorded"), Root->GetObjectField(TEXT("tool"))->GetStringField(TEXT("version")), FString(TEXT("0.1")));
	TestEqual(TEXT("Files are recorded"), Root->GetObjectField(TEXT("files"))->GetObjectField(TEXT("new"))->GetStringField(TEXT("path")), FString(TEXT("D:/New.uasset")));
	TestEqual(TEXT("The summary counts added entries"), static_cast<int32>(Root->GetObjectField(TEXT("summary"))->GetNumberField(TEXT("added"))), 1);
	TestTrue(TEXT("An unfiltered report has a null filter"), Root->HasTypedField<EJson::Null>(TEXT("filter")));

	const TArray<TSharedPtr<FJsonValue>>& Differences = Root->GetArrayField(TEXT("differences"));
	if (!TestEqual(TEXT("One top-level difference"), Differences.Num(), 1))
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Export = Differences[0]->AsObject();
	TestEqual(TEXT("The state is named"), Export->GetStringField(TEXT("state")), FString(TEXT("Modified")));

	const TArray<TSharedPtr<FJsonValue>>& Children = Export->GetArrayField(TEXT("children"));
	TestEqual(TEXT("Children are nested, without the unchanged one"), Children.Num(), 2);

	const TSharedPtr<FJsonObject> Health = Children[0]->AsObject();
	TestEqual(TEXT("The property name"), Health->GetStringField(TEXT("name")), FString(TEXT("Health")));
	TestEqual(TEXT("The old value"), Health->GetObjectField(TEXT("old"))->GetStringField(TEXT("value")), FString(TEXT("50")));
	TestEqual(TEXT("The final value"), Health->GetObjectField(TEXT("new"))->GetStringField(TEXT("finalValue")), FString(TEXT("2 elements: a, b")));
	TestTrue(TEXT("Absent final values are null"), Health->GetObjectField(TEXT("old"))->HasTypedField<EJson::Null>(TEXT("finalValue")));
	TestTrue(TEXT("Absent offsets are null"), Health->HasTypedField<EJson::Null>(TEXT("oldOffset")));

	const TSharedPtr<FJsonObject> Name = Children[1]->AsObject();
	TestEqual(TEXT("Multi-line values are kept intact"), Name->GetObjectField(TEXT("new"))->GetStringField(TEXT("value")), FString(TEXT("Bob\nSmith")));
	TestTrue(TEXT("An absent old value is null"), Name->GetObjectField(TEXT("old"))->HasTypedField<EJson::Null>(TEXT("value")));

	const TSharedPtr<FJsonObject> Analysis = Root->GetObjectField(TEXT("saveAnalysis"));
	TestEqual(TEXT("The analysis result"), Analysis->GetStringField(TEXT("result")), FString(TEXT("SemanticChanges")));
	const TSharedPtr<FJsonObject> Explanation = Analysis->GetArrayField(TEXT("semanticChanges"))[0]->AsObject();
	TestEqual(TEXT("The explanation classification"), Explanation->GetStringField(TEXT("classification")), FString(TEXT("PropertyValueChanged")));
	TestEqual(TEXT("The cause confidence"), Explanation->GetStringField(TEXT("causeConfidence")), FString(TEXT("Inferred")));
	TestEqual(TEXT("The explanation offset"), static_cast<int32>(Explanation->GetNumberField(TEXT("oldOffset"))), 16);
	TestTrue(TEXT("An unknown offset is null"), Explanation->HasTypedField<EJson::Null>(TEXT("newOffset")));

	const TSharedPtr<FJsonObject> Pattern = Root->GetArrayField(TEXT("repeatedSaves"))[0]->AsObject();
	TestEqual(TEXT("The pattern"), Pattern->GetStringField(TEXT("pattern")), FString(TEXT("ChangedEverySave")));
	TestEqual(TEXT("Sample ids are strings"), Pattern->GetArrayField(TEXT("samples"))[0]->AsObject()->GetStringField(TEXT("saveId")), FString(TEXT("7")));

	FAssetAnalysisReport NoAnalysis = MakeReport();
	NoAnalysis.SaveAnalysis.Reset();
	TSharedPtr<FJsonObject> NoAnalysisRoot;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(AssetReportWriter::ToJson(NoAnalysis)), NoAnalysisRoot);
	TestTrue(TEXT("A missing analysis is null"), NoAnalysisRoot.IsValid() && NoAnalysisRoot->HasTypedField<EJson::Null>(TEXT("saveAnalysis")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetReport_SavesInTheFormatOfTheFilename, "AssetSerializationInspector.Report.AssetReportWriter.SavesInTheFormatOfTheFilename",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetReport_SavesInTheFormatOfTheFilename::RunTest(const FString& Parameters)
{
	using namespace AssetReportTestUtils;

	TestEqual(TEXT(".json selects JSON"), AssetReportWriter::GetFormatForFilename(TEXT("report.JSON")), EAssetReportFormat::Json);
	TestEqual(TEXT(".txt selects text"), AssetReportWriter::GetFormatForFilename(TEXT("report.txt")), EAssetReportFormat::Text);
	TestEqual(TEXT("An unknown extension falls back to text"), AssetReportWriter::GetFormatForFilename(TEXT("report")), EAssetReportFormat::Text);

	const FAssetAnalysisReport Report = MakeReport();
	const FString Directory = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("AssetSerializationInspector"));
	FText Error;

	const FString JsonFile = FPaths::Combine(Directory, TEXT("report.json"));
	TestTrue(TEXT("A JSON report is written"), AssetReportWriter::SaveToFile(Report, JsonFile, Error));
	FString Loaded;
	TestTrue(TEXT("It can be read back"), FFileHelper::LoadFileToString(Loaded, *JsonFile));
	TestEqual(TEXT("It holds the JSON form"), Loaded, AssetReportWriter::ToJson(Report));

	const FString TextFile = FPaths::Combine(Directory, TEXT("report.txt"));
	TestTrue(TEXT("A text report is written"), AssetReportWriter::SaveToFile(Report, TextFile, Error));
	TestTrue(TEXT("It can be read back"), FFileHelper::LoadFileToString(Loaded, *TextFile));
	TestEqual(TEXT("It holds the text form"), Loaded, AssetReportWriter::ToText(Report));

	TestFalse(TEXT("An empty filename is rejected"), AssetReportWriter::SaveToFile(Report, FString(), Error));
	TestFalse(TEXT("The reason is reported"), Error.IsEmpty());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
