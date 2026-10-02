// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Report/AssetBatchReportWriter.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"

#include "Report/AssetAnalysisReport.h"
#include "Save/AssetBatchResave.h"

#define LOCTEXT_NAMESPACE "AssetBatchReportWriter"

namespace
{
	using FJsonWriter = TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>;

	const TCHAR* NameOf(const EAssetBatchResaveStatus Status)
	{
		switch (Status)
		{
			case EAssetBatchResaveStatus::Tested:
				return TEXT("Tested");
			case EAssetBatchResaveStatus::Skipped:
				return TEXT("Skipped");
			case EAssetBatchResaveStatus::Failed:
				return TEXT("Failed");
		}

		return TEXT("Unknown");
	}

	const TCHAR* NameOf(const ENoOpResaveVerdict Verdict)
	{
		switch (Verdict)
		{
			case ENoOpResaveVerdict::Stable:
				return TEXT("Stable");
			case ENoOpResaveVerdict::NormalizedOnFirstSave:
				return TEXT("NormalizedOnFirstSave");
			case ENoOpResaveVerdict::Unstable:
				return TEXT("Unstable");
		}

		return TEXT("Unknown");
	}

	FString CollapseLines(const FString& Value)
	{
		FString Result = Value;
		Result.ReplaceInline(TEXT("\r\n"), TEXT(" | "));
		Result.ReplaceInline(TEXT("\n"), TEXT(" | "));
		Result.ReplaceInline(TEXT("\r"), TEXT(" | "));
		return Result;
	}

	void AppendChanges(const TCHAR* Heading, const int64 ChangedBytes, const TArray<FAssetBatchResaveChange>& Changes, const int32 Omitted, TArray<FString>& OutLines)
	{
		OutLines.Add(FString::Printf(TEXT("    %s: %lld changed bytes, %d changes"), Heading, ChangedBytes, Changes.Num() + Omitted));

		for (const FAssetBatchResaveChange& Change : Changes)
		{
			OutLines.Add(FString::Printf(TEXT("      - [%s] %s%s%s"), *Change.Category, *Change.Name, Change.Detail.IsEmpty() ? TEXT("") : TEXT(": "), *CollapseLines(Change.Detail)));
		}

		if (Omitted > 0)
		{
			OutLines.Add(FString::Printf(TEXT("      ... and %d more"), Omitted));
		}
	}

	void WriteChanges(FJsonWriter& Writer, const FString& Key, const int64 ChangedBytes, const TArray<FAssetBatchResaveChange>& Changes, const int32 Omitted)
	{
		Writer.WriteObjectStart(Key);
		Writer.WriteValue(TEXT("changedBytes"), ChangedBytes);

		Writer.WriteArrayStart(TEXT("changes"));
		for (const FAssetBatchResaveChange& Change : Changes)
		{
			Writer.WriteObjectStart();
			Writer.WriteValue(TEXT("category"), Change.Category);
			Writer.WriteValue(TEXT("name"), Change.Name);
			Writer.WriteValue(TEXT("detail"), Change.Detail);
			Writer.WriteObjectEnd();
		}
		Writer.WriteArrayEnd();

		Writer.WriteValue(TEXT("omitted"), static_cast<int64>(Omitted));
		Writer.WriteObjectEnd();
	}
} // namespace

FString AssetBatchReportWriter::ToText(const FAssetBatchResaveResult& Result)
{
	const FAssetBatchResaveSummary Summary = Result.Summarize();

	TArray<FString> Lines;

	Lines.Add(TEXT("Asset Serialization Inspector: no-op resave test"));
	const FString ToolVersion = AssetAnalysisReport::GetToolVersion();
	Lines.Add(FString::Printf(TEXT("Tool version: %s"), ToolVersion.IsEmpty() ? TEXT("unknown") : *ToolVersion));
	Lines.Add(FString::Printf(TEXT("Scope: %s"), *Result.Scope));
	Lines.Add(FString::Printf(TEXT("Started: %s"), *Result.StartedAt.ToIso8601()));
	Lines.Add(FString::Printf(TEXT("Finished: %s%s"), *Result.FinishedAt.ToIso8601(), Result.bCancelled ? TEXT(" (cancelled before every asset was tested)") : TEXT("")));
	Lines.Add(FString());

	Lines.Add(FString::Printf(TEXT("%d assets: %d tested, %d skipped, %d failed"), Result.Entries.Num(), Summary.Tested, Summary.Skipped, Summary.Failed));
	Lines.Add(FString::Printf(TEXT("Tested: %d stable, %d normalized on the first save, %d unstable"), Summary.Stable, Summary.NormalizedOnFirstSave, Summary.Unstable));

	const TArray<FAssetBatchRecurringChange> Recurring = Result.FindRecurringChanges();
	if (!Recurring.IsEmpty())
	{
		Lines.Add(FString());
		Lines.Add(TEXT("Changes made in several assets by the first resave"));

		for (const FAssetBatchRecurringChange& Change : Recurring)
		{
			Lines.Add(FString::Printf(TEXT("  %d assets: [%s] %s"), Change.AssetCount, *Change.Category, *Change.Name));
		}
	}

	const auto AppendGroup = [&](const TCHAR* Heading, const TFunctionRef<bool(const FAssetBatchResaveEntry&)> Predicate) {
		TArray<const FAssetBatchResaveEntry*> Group;
		for (const FAssetBatchResaveEntry& Entry : Result.Entries)
		{
			if (Predicate(Entry))
			{
				Group.Add(&Entry);
			}
		}

		if (Group.IsEmpty())
		{
			return;
		}

		Lines.Add(FString());
		Lines.Add(FString::Printf(TEXT("%s (%d)"), Heading, Group.Num()));

		for (const FAssetBatchResaveEntry* Entry : Group)
		{
			if (Entry->Status != EAssetBatchResaveStatus::Tested)
			{
				Lines.Add(FString::Printf(TEXT("  %s: %s"), *Entry->PackageName.ToString(), *Entry->Message));
				continue;
			}

			Lines.Add(FString::Printf(TEXT("  %s (%.2f s)"), *Entry->PackageName.ToString(), Entry->Seconds));

			if (Entry->Verdict != ENoOpResaveVerdict::Stable)
			{
				AppendChanges(TEXT("First resave"), Entry->FirstResaveChangedBytes, Entry->FirstResaveChanges, Entry->FirstResaveChangesOmitted, Lines);
			}

			if (Entry->Verdict == ENoOpResaveVerdict::Unstable)
			{
				AppendChanges(TEXT("Second resave"), Entry->SecondResaveChangedBytes, Entry->SecondResaveChanges, Entry->SecondResaveChangesOmitted, Lines);
			}
		}
	};

	AppendGroup(TEXT("Unstable: resaving keeps changing the file"),
		[](const FAssetBatchResaveEntry& Entry) { return Entry.Status == EAssetBatchResaveStatus::Tested && Entry.Verdict == ENoOpResaveVerdict::Unstable; });
	AppendGroup(TEXT("Normalized on the first save: the first resave changes the file, later ones do not"),
		[](const FAssetBatchResaveEntry& Entry) { return Entry.Status == EAssetBatchResaveStatus::Tested && Entry.Verdict == ENoOpResaveVerdict::NormalizedOnFirstSave; });
	AppendGroup(TEXT("Failed"), [](const FAssetBatchResaveEntry& Entry) { return Entry.Status == EAssetBatchResaveStatus::Failed; });
	AppendGroup(TEXT("Skipped"), [](const FAssetBatchResaveEntry& Entry) { return Entry.Status == EAssetBatchResaveStatus::Skipped; });
	AppendGroup(TEXT("Stable"), [](const FAssetBatchResaveEntry& Entry) { return Entry.Status == EAssetBatchResaveStatus::Tested && Entry.Verdict == ENoOpResaveVerdict::Stable; });

	return FString::Join(Lines, TEXT("\n")) + TEXT("\n");
}

FString AssetBatchReportWriter::ToJson(const FAssetBatchResaveResult& Result)
{
	const FAssetBatchResaveSummary Summary = Result.Summarize();

	FString Output;
	const TSharedRef<FJsonWriter> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);

	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("schemaVersion"), static_cast<int64>(FAssetAnalysisReport::SchemaVersion));
	Writer->WriteValue(TEXT("kind"), TEXT("batchNoOpResave"));

	Writer->WriteObjectStart(TEXT("tool"));
	Writer->WriteValue(TEXT("name"), TEXT("AssetSerializationInspector"));
	const FString ToolVersion = AssetAnalysisReport::GetToolVersion();
	if (ToolVersion.IsEmpty())
	{
		Writer->WriteNull(TEXT("version"));
	}
	else
	{
		Writer->WriteValue(TEXT("version"), ToolVersion);
	}
	Writer->WriteObjectEnd();

	Writer->WriteValue(TEXT("scope"), Result.Scope);
	Writer->WriteValue(TEXT("startedAt"), Result.StartedAt.ToIso8601());
	Writer->WriteValue(TEXT("finishedAt"), Result.FinishedAt.ToIso8601());
	Writer->WriteValue(TEXT("cancelled"), Result.bCancelled);

	Writer->WriteObjectStart(TEXT("summary"));
	Writer->WriteValue(TEXT("tested"), static_cast<int64>(Summary.Tested));
	Writer->WriteValue(TEXT("stable"), static_cast<int64>(Summary.Stable));
	Writer->WriteValue(TEXT("normalizedOnFirstSave"), static_cast<int64>(Summary.NormalizedOnFirstSave));
	Writer->WriteValue(TEXT("unstable"), static_cast<int64>(Summary.Unstable));
	Writer->WriteValue(TEXT("skipped"), static_cast<int64>(Summary.Skipped));
	Writer->WriteValue(TEXT("failed"), static_cast<int64>(Summary.Failed));
	Writer->WriteObjectEnd();

	Writer->WriteArrayStart(TEXT("recurringChanges"));
	for (const FAssetBatchRecurringChange& Change : Result.FindRecurringChanges())
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("category"), Change.Category);
		Writer->WriteValue(TEXT("name"), Change.Name);
		Writer->WriteValue(TEXT("assets"), static_cast<int64>(Change.AssetCount));
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

	Writer->WriteArrayStart(TEXT("assets"));
	for (const FAssetBatchResaveEntry& Entry : Result.Entries)
	{
		const bool bTested = Entry.Status == EAssetBatchResaveStatus::Tested;

		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("package"), Entry.PackageName.ToString());
		Writer->WriteValue(TEXT("status"), NameOf(Entry.Status));

		if (Entry.Message.IsEmpty())
		{
			Writer->WriteNull(TEXT("message"));
		}
		else
		{
			Writer->WriteValue(TEXT("message"), Entry.Message);
		}

		if (bTested)
		{
			Writer->WriteValue(TEXT("verdict"), NameOf(Entry.Verdict));
		}
		else
		{
			Writer->WriteNull(TEXT("verdict"));
		}

		Writer->WriteValue(TEXT("seconds"), Entry.Seconds);

		if (bTested)
		{
			WriteChanges(*Writer, TEXT("firstResave"), Entry.FirstResaveChangedBytes, Entry.FirstResaveChanges, Entry.FirstResaveChangesOmitted);
			WriteChanges(*Writer, TEXT("secondResave"), Entry.SecondResaveChangedBytes, Entry.SecondResaveChanges, Entry.SecondResaveChangesOmitted);
		}
		else
		{
			Writer->WriteNull(TEXT("firstResave"));
			Writer->WriteNull(TEXT("secondResave"));
		}

		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

	Writer->WriteObjectEnd();
	Writer->Close();

	return Output;
}

FString AssetBatchReportWriter::Write(const FAssetBatchResaveResult& Result, const EAssetReportFormat Format)
{
	return Format == EAssetReportFormat::Json ? ToJson(Result) : ToText(Result);
}

FString AssetBatchReportWriter::MakeDefaultFilename(const FString& Scope, const FDateTime& Time, const EAssetReportFormat Format)
{
	// The scope is a content path or a description, so keep only its last path segment.
	FString Name = FPaths::GetCleanFilename(Scope);

	if (Name.IsEmpty())
	{
		Name = Scope.IsEmpty() ? FString(TEXT("Assets")) : Scope;
	}

	return FPaths::MakeValidFileName(
		FString::Printf(TEXT("NoOpResave_%s_%s.%s"), *Name.Replace(TEXT(" "), TEXT("")), *Time.ToString(TEXT("%Y%m%d-%H%M%S")), AssetReportWriter::GetFileExtension(Format)));
}

bool AssetBatchReportWriter::SaveToFile(const FAssetBatchResaveResult& Result, const FString& Filename, FText& OutError)
{
	if (Filename.IsEmpty())
	{
		OutError = LOCTEXT("NoReportFilename", "No filename was provided for the report.");
		return false;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);

	if (!FFileHelper::SaveStringToFile(Write(Result, AssetReportWriter::GetFormatForFilename(Filename)), *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FText::Format(LOCTEXT("ReportWriteFailed", "The report could not be written to:\n{0}"), FText::FromString(Filename));
		return false;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
