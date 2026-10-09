// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Report/AssetFolderComparisonReportWriter.h"

#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"

#include "Compare/AssetFolderComparison.h"
#include "Report/AssetAnalysisReport.h"
#include "Report/AssetHtmlReport.h"

#define LOCTEXT_NAMESPACE "AssetFolderComparisonReportWriter"

namespace
{
	using FJsonWriter = TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>;

	const TCHAR* StatusName(const EAssetFolderComparisonStatus Status)
	{
		switch (Status)
		{
			case EAssetFolderComparisonStatus::Identical:
				return TEXT("Identical");
			case EAssetFolderComparisonStatus::Changed:
				return TEXT("Changed");
			case EAssetFolderComparisonStatus::OnlyInOldFolder:
				return TEXT("OnlyInOldFolder");
			case EAssetFolderComparisonStatus::OnlyInNewFolder:
				return TEXT("OnlyInNewFolder");
			case EAssetFolderComparisonStatus::Failed:
				return TEXT("Failed");
		}

		return TEXT("Unknown");
	}

	FString VersionText(const FString& EngineVersion, const FString& FileVersion)
	{
		if (EngineVersion.IsEmpty() && FileVersion.IsEmpty())
		{
			return FString();
		}

		return FString::Printf(TEXT("%s (%s)"), EngineVersion.IsEmpty() ? TEXT("unknown engine") : *EngineVersion, *FileVersion);
	}

	void WriteOptionalString(FJsonWriter& Writer, const FString& Key, const FString& Value)
	{
		if (Value.IsEmpty())
		{
			Writer.WriteNull(Key);
		}
		else
		{
			Writer.WriteValue(Key, Value);
		}
	}

	void WriteOptionalSize(FJsonWriter& Writer, const FString& Key, const int64 Size)
	{
		if (Size == INDEX_NONE)
		{
			Writer.WriteNull(Key);
		}
		else
		{
			Writer.WriteValue(Key, Size);
		}
	}

	void AppendEntry(const FAssetFolderComparisonEntry& Entry, TArray<FString>& OutLines)
	{
		FString Line = FString::Printf(TEXT("  %s"), *Entry.RelativePath);

		if (Entry.Status == EAssetFolderComparisonStatus::Failed)
		{
			OutLines.Add(Line + TEXT(": ") + Entry.Message);
			return;
		}

		const FString OldVersion = VersionText(Entry.OldEngineVersion, Entry.OldFileVersion);
		const FString NewVersion = VersionText(Entry.NewEngineVersion, Entry.NewFileVersion);

		if (Entry.Status == EAssetFolderComparisonStatus::Changed)
		{
			Line += FString::Printf(TEXT(" (%lld -> %lld bytes)"), Entry.OldFileSize, Entry.NewFileSize);
			OutLines.Add(Line);

			if (Entry.bVersionsDiffer)
			{
				OutLines.Add(FString::Printf(TEXT("    saved by %s -> %s"), *OldVersion, *NewVersion));
			}

			for (const FAssetBatchResaveChange& Change : Entry.Changes)
			{
				OutLines.Add(FString::Printf(TEXT("    - [%s] %s%s%s"), *Change.Category, *Change.Name, Change.Detail.IsEmpty() ? TEXT("") : TEXT(": "), *AssetReportWriter::OneLine(Change.Detail)));
			}

			if (Entry.ChangesOmitted > 0)
			{
				OutLines.Add(FString::Printf(TEXT("    ... and %d more"), Entry.ChangesOmitted));
			}

			return;
		}

		const FString& Version = Entry.Status == EAssetFolderComparisonStatus::OnlyInOldFolder ? OldVersion : NewVersion;
		OutLines.Add(Version.IsEmpty() ? Line : FString::Printf(TEXT("%s (saved by %s)"), *Line, *Version));
	}
} // namespace

FString AssetFolderComparisonReportWriter::ToText(const FAssetFolderComparisonResult& Result)
{
	const FAssetFolderComparisonSummary Summary = Result.Summarize();

	TArray<FString> Lines;

	Lines.Add(TEXT("Asset Serialization Inspector: folder comparison"));
	const FString ToolVersion = AssetAnalysisReport::GetToolVersion();
	Lines.Add(FString::Printf(TEXT("Tool version: %s"), ToolVersion.IsEmpty() ? TEXT("unknown") : *ToolVersion));
	Lines.Add(FString::Printf(TEXT("Old folder: %s"), *Result.OldFolder));
	Lines.Add(FString::Printf(TEXT("New folder: %s"), *Result.NewFolder));
	Lines.Add(FString::Printf(TEXT("Started: %s"), *Result.StartedAt.ToIso8601()));
	Lines.Add(FString::Printf(TEXT("Finished: %s%s"), *Result.FinishedAt.ToIso8601(), Result.bCancelled ? TEXT(" (cancelled before every file was compared)") : TEXT("")));
	Lines.Add(FString());

	Lines.Add(FString::Printf(TEXT("%d files: %d identical, %d changed (%d of them saved by different versions), %d only in the old folder, %d only in the new folder, %d failed"),
		Result.Entries.Num(), Summary.Identical, Summary.Changed, Summary.ChangedWithDifferentVersions, Summary.OnlyInOldFolder, Summary.OnlyInNewFolder, Summary.Failed));

	const TArray<FAssetEngineVersionPair> VersionPairs = Result.FindEngineVersionPairs();
	if (!VersionPairs.IsEmpty())
	{
		Lines.Add(FString());
		Lines.Add(TEXT("Engine versions of the files present in both folders"));

		for (const FAssetEngineVersionPair& Pair : VersionPairs)
		{
			Lines.Add(FString::Printf(TEXT("  %d files: %s -> %s"), Pair.AssetCount, Pair.OldEngineVersion.IsEmpty() ? TEXT("unknown") : *Pair.OldEngineVersion,
				Pair.NewEngineVersion.IsEmpty() ? TEXT("unknown") : *Pair.NewEngineVersion));
		}
	}

	const TArray<FAssetBatchRecurringChange> Recurring = Result.FindRecurringChanges();
	if (!Recurring.IsEmpty())
	{
		Lines.Add(FString());
		Lines.Add(TEXT("Changes found in several files"));

		for (const FAssetBatchRecurringChange& Change : Recurring)
		{
			Lines.Add(FString::Printf(TEXT("  %d files: [%s] %s"), Change.AssetCount, *Change.Category, *Change.Name));
		}
	}

	const auto AppendGroup = [&](const TCHAR* Heading, const EAssetFolderComparisonStatus Status) {
		TArray<const FAssetFolderComparisonEntry*> Group;
		for (const FAssetFolderComparisonEntry& Entry : Result.Entries)
		{
			if (Entry.Status == Status)
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

		for (const FAssetFolderComparisonEntry* Entry : Group)
		{
			AppendEntry(*Entry, Lines);
		}
	};

	AppendGroup(TEXT("Changed"), EAssetFolderComparisonStatus::Changed);
	AppendGroup(TEXT("Failed"), EAssetFolderComparisonStatus::Failed);
	AppendGroup(TEXT("Only in the old folder"), EAssetFolderComparisonStatus::OnlyInOldFolder);
	AppendGroup(TEXT("Only in the new folder"), EAssetFolderComparisonStatus::OnlyInNewFolder);

	return FString::Join(Lines, TEXT("\n")) + TEXT("\n");
}

FString AssetFolderComparisonReportWriter::ToHtml(const FAssetFolderComparisonResult& Result)
{
	using namespace AssetHtmlReport;

	const FAssetFolderComparisonSummary Summary = Result.Summarize();
	const FString ToolVersion = AssetAnalysisReport::GetToolVersion();

	FString Body = TEXT("<h1>Asset Serialization Inspector: folder comparison</h1>\n");
	Body += KeyValues({ { TEXT("Tool version"), ToolVersion.IsEmpty() ? FString(TEXT("unknown")) : ToolVersion }, { TEXT("Old folder"), Result.OldFolder }, { TEXT("New folder"), Result.NewFolder },
		{ TEXT("Started"), Result.StartedAt.ToIso8601() },
		{ TEXT("Finished"), Result.FinishedAt.ToIso8601() + (Result.bCancelled ? TEXT(" (cancelled before every file was compared)") : TEXT("")) } });

	Body += FString::Printf(TEXT("<p class=\"chips\">%s%s%s%s%s%s</p>\n"), *Chip(FString::Printf(TEXT("%d identical"), Summary.Identical), TEXT("ok")),
		*Chip(FString::Printf(TEXT("%d changed (%d with different versions)"), Summary.Changed, Summary.ChangedWithDifferentVersions), TEXT("modified")),
		*Chip(FString::Printf(TEXT("%d only in the old folder"), Summary.OnlyInOldFolder), TEXT("removed")),
		*Chip(FString::Printf(TEXT("%d only in the new folder"), Summary.OnlyInNewFolder), TEXT("added")), *Chip(FString::Printf(TEXT("%d failed"), Summary.Failed), TEXT("bad")),
		*Chip(FString::Printf(TEXT("%d files"), Result.Entries.Num()), TEXT("info")));

	const TArray<FAssetEngineVersionPair> VersionPairs = Result.FindEngineVersionPairs();
	if (!VersionPairs.IsEmpty())
	{
		TArray<TArray<FString>> Rows;
		for (const FAssetEngineVersionPair& Pair : VersionPairs)
		{
			Rows.Add({ Escape(FString::FromInt(Pair.AssetCount)), Escape(Pair.OldEngineVersion.IsEmpty() ? FString(TEXT("unknown")) : Pair.OldEngineVersion),
				Escape(Pair.NewEngineVersion.IsEmpty() ? FString(TEXT("unknown")) : Pair.NewEngineVersion) });
		}

		Body += TEXT("<h2>Engine versions of the files present in both folders</h2>\n") + Table({ TEXT("Files"), TEXT("Saved by"), TEXT("Now saved by") }, Rows);
	}

	const TArray<FAssetBatchRecurringChange> Recurring = Result.FindRecurringChanges();
	if (!Recurring.IsEmpty())
	{
		TArray<TArray<FString>> Rows;
		for (const FAssetBatchRecurringChange& Change : Recurring)
		{
			Rows.Add({ Escape(FString::FromInt(Change.AssetCount)), Escape(Change.Category), Escape(Change.Name) });
		}

		Body += TEXT("<h2>Changes found in several files</h2>\n") + Table({ TEXT("Files"), TEXT("Category"), TEXT("Name") }, Rows);
	}

	const auto AddGroup = [&](const TCHAR* Heading, const EAssetFolderComparisonStatus Status) {
		FString Group;
		int32 Count = 0;
		for (const FAssetFolderComparisonEntry& Entry : Result.Entries)
		{
			if (Entry.Status != Status)
			{
				continue;
			}

			++Count;
			const FString Name = FString::Printf(TEXT("<strong>%s</strong>"), *Escape(Entry.RelativePath));
			if (Entry.Status == EAssetFolderComparisonStatus::Failed)
			{
				Group += FString::Printf(TEXT("<div class=\"row\">%s <span class=\"muted\">%s</span></div>\n"), *Name, *Escape(Entry.Message));
				continue;
			}

			const FString OldVersion = VersionText(Entry.OldEngineVersion, Entry.OldFileVersion);
			const FString NewVersion = VersionText(Entry.NewEngineVersion, Entry.NewFileVersion);

			if (Entry.Status != EAssetFolderComparisonStatus::Changed)
			{
				const FString& Version = Entry.Status == EAssetFolderComparisonStatus::OnlyInOldFolder ? OldVersion : NewVersion;
				Group += FString::Printf(
					TEXT("<div class=\"row\">%s%s</div>\n"), *Name, Version.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" <span class=\"muted\">saved by %s</span>"), *Escape(Version)));
				continue;
			}

			FString Details;
			if (Entry.bVersionsDiffer)
			{
				Details += FString::Printf(TEXT("<p class=\"note\">saved by %s &rarr; %s</p>"), *Escape(OldVersion), *Escape(NewVersion));
			}

			TArray<TArray<FString>> Rows;
			for (const FAssetBatchResaveChange& Change : Entry.Changes)
			{
				Rows.Add({ Escape(Change.Category), Escape(Change.Name), Change.Detail.IsEmpty() ? FString() : Code(Change.Detail) });
			}

			if (!Rows.IsEmpty())
			{
				Details += Table({ TEXT("Category"), TEXT("Name"), TEXT("Detail") }, Rows);
			}

			if (Entry.ChangesOmitted > 0)
			{
				Details += FString::Printf(TEXT("<p class=\"muted\">... and %d more</p>"), Entry.ChangesOmitted);
			}

			const FString Head = FString::Printf(TEXT("%s <span class=\"muted\">%lld &rarr; %lld bytes</span>"), *Name, Entry.OldFileSize, Entry.NewFileSize);
			Group += Details.IsEmpty() ? FString::Printf(TEXT("<div class=\"row\">%s</div>\n"), *Head) : AssetHtmlReport::Details(Head, Details, false);
		}

		if (Count > 0)
		{
			Body += FString::Printf(TEXT("<h2>%s (%d)</h2>\n"), *Escape(Heading), Count) + Group;
		}
	};

	AddGroup(TEXT("Changed"), EAssetFolderComparisonStatus::Changed);
	AddGroup(TEXT("Failed"), EAssetFolderComparisonStatus::Failed);
	AddGroup(TEXT("Only in the old folder"), EAssetFolderComparisonStatus::OnlyInOldFolder);
	AddGroup(TEXT("Only in the new folder"), EAssetFolderComparisonStatus::OnlyInNewFolder);

	return Page(TEXT("Asset Serialization Inspector: folder comparison"), Body);
}

FString AssetFolderComparisonReportWriter::ToJson(const FAssetFolderComparisonResult& Result)
{
	const FAssetFolderComparisonSummary Summary = Result.Summarize();

	FString Output;
	const TSharedRef<FJsonWriter> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);

	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("schemaVersion"), static_cast<int64>(FAssetAnalysisReport::SchemaVersion));
	Writer->WriteValue(TEXT("kind"), TEXT("folderComparison"));

	Writer->WriteObjectStart(TEXT("tool"));
	Writer->WriteValue(TEXT("name"), TEXT("AssetSerializationInspector"));
	WriteOptionalString(*Writer, TEXT("version"), AssetAnalysisReport::GetToolVersion());
	Writer->WriteObjectEnd();

	Writer->WriteValue(TEXT("oldFolder"), Result.OldFolder);
	Writer->WriteValue(TEXT("newFolder"), Result.NewFolder);
	Writer->WriteValue(TEXT("startedAt"), Result.StartedAt.ToIso8601());
	Writer->WriteValue(TEXT("finishedAt"), Result.FinishedAt.ToIso8601());
	Writer->WriteValue(TEXT("cancelled"), Result.bCancelled);

	Writer->WriteObjectStart(TEXT("summary"));
	Writer->WriteValue(TEXT("identical"), static_cast<int64>(Summary.Identical));
	Writer->WriteValue(TEXT("changed"), static_cast<int64>(Summary.Changed));
	Writer->WriteValue(TEXT("onlyInOldFolder"), static_cast<int64>(Summary.OnlyInOldFolder));
	Writer->WriteValue(TEXT("onlyInNewFolder"), static_cast<int64>(Summary.OnlyInNewFolder));
	Writer->WriteValue(TEXT("failed"), static_cast<int64>(Summary.Failed));
	Writer->WriteValue(TEXT("changedWithDifferentVersions"), static_cast<int64>(Summary.ChangedWithDifferentVersions));
	Writer->WriteObjectEnd();

	Writer->WriteArrayStart(TEXT("engineVersions"));
	for (const FAssetEngineVersionPair& Pair : Result.FindEngineVersionPairs())
	{
		Writer->WriteObjectStart();
		WriteOptionalString(*Writer, TEXT("old"), Pair.OldEngineVersion);
		WriteOptionalString(*Writer, TEXT("new"), Pair.NewEngineVersion);
		Writer->WriteValue(TEXT("assets"), static_cast<int64>(Pair.AssetCount));
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

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
	for (const FAssetFolderComparisonEntry& Entry : Result.Entries)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("path"), Entry.RelativePath);
		Writer->WriteValue(TEXT("status"), StatusName(Entry.Status));
		WriteOptionalString(*Writer, TEXT("message"), Entry.Message);
		WriteOptionalString(*Writer, TEXT("oldEngineVersion"), Entry.OldEngineVersion);
		WriteOptionalString(*Writer, TEXT("newEngineVersion"), Entry.NewEngineVersion);
		WriteOptionalString(*Writer, TEXT("oldFileVersion"), Entry.OldFileVersion);
		WriteOptionalString(*Writer, TEXT("newFileVersion"), Entry.NewFileVersion);
		Writer->WriteValue(TEXT("versionsDiffer"), Entry.bVersionsDiffer);
		WriteOptionalSize(*Writer, TEXT("oldFileSize"), Entry.OldFileSize);
		WriteOptionalSize(*Writer, TEXT("newFileSize"), Entry.NewFileSize);

		Writer->WriteArrayStart(TEXT("changes"));
		for (const FAssetBatchResaveChange& Change : Entry.Changes)
		{
			Writer->WriteObjectStart();
			Writer->WriteValue(TEXT("category"), Change.Category);
			Writer->WriteValue(TEXT("name"), Change.Name);
			Writer->WriteValue(TEXT("detail"), Change.Detail);
			Writer->WriteObjectEnd();
		}
		Writer->WriteArrayEnd();

		Writer->WriteValue(TEXT("omitted"), static_cast<int64>(Entry.ChangesOmitted));
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

	Writer->WriteObjectEnd();
	Writer->Close();

	return Output;
}

FString AssetFolderComparisonReportWriter::Write(const FAssetFolderComparisonResult& Result, const EAssetReportFormat Format)
{
	switch (Format)
	{
		case EAssetReportFormat::Json:
			return ToJson(Result);
		case EAssetReportFormat::Html:
			return ToHtml(Result);
		default:
			return ToText(Result);
	}
}

FString AssetFolderComparisonReportWriter::MakeDefaultFilename(const FString& OldFolder, const FString& NewFolder, const FDateTime& Time, const EAssetReportFormat Format)
{
	const auto Name = [](const FString& Folder) {
		FString Trimmed = Folder.Replace(TEXT("\\"), TEXT("/"));
		Trimmed.RemoveFromEnd(TEXT("/"));

		FString Result = FPaths::GetCleanFilename(Trimmed).Replace(TEXT(" "), TEXT(""));
		return Result.IsEmpty() ? FString(TEXT("Folder")) : Result;
	};

	return FPaths::MakeValidFileName(
		FString::Printf(TEXT("FolderComparison_%s_vs_%s_%s.%s"), *Name(OldFolder), *Name(NewFolder), *Time.ToString(TEXT("%Y%m%d-%H%M%S")), AssetReportWriter::GetFileExtension(Format)));
}

bool AssetFolderComparisonReportWriter::SaveToFile(const FAssetFolderComparisonResult& Result, const FString& Filename, FText& OutError)
{
	return AssetReportWriter::SaveTextToFile(Filename, [&] { return Write(Result, AssetReportWriter::GetFormatForFilename(Filename)); }, OutError);
}

#undef LOCTEXT_NAMESPACE
