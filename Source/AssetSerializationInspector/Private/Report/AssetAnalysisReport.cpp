// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Report/AssetAnalysisReport.h"

#include "Interfaces/IPluginManager.h"

#include "Diff/AssetDiffFilter.h"

namespace
{
	void Accumulate(const FAssetPackageDiffEntry& Entry, FAssetReportSummary& Summary)
	{
		switch (Entry.State)
		{
			case EAssetPackageDiffState::Added:
				++Summary.Added;
				break;

			case EAssetPackageDiffState::Removed:
				++Summary.Removed;
				break;

			case EAssetPackageDiffState::Modified:
				++Summary.Modified;
				break;

			case EAssetPackageDiffState::Moved:
				++Summary.Moved;
				break;

			case EAssetPackageDiffState::Unchanged:
				break;
		}

		for (const FAssetPackageDiffEntry& Child : Entry.Children)
		{
			Accumulate(Child, Summary);
		}
	}

} // namespace

FString AssetAnalysisReport::GetToolVersion()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
	return Plugin.IsValid() ? Plugin->GetDescriptor().VersionName : FString();
}

FAssetReportSummary AssetAnalysisReport::Summarize(const TArray<FAssetPackageDiffEntry>& Entries)
{
	FAssetReportSummary Summary;

	for (const FAssetPackageDiffEntry& Entry : Entries)
	{
		Accumulate(Entry, Summary);
	}

	return Summary;
}

FAssetAnalysisReport AssetAnalysisReport::Build(
	const FAssetPackageDiffResult& Diff, const FAssetSaveAnalysis* SaveAnalysis, const TArray<FRepeatedSavePattern>& RepeatedSavePatterns, const FAssetDiffFilter* Filter)
{
	FAssetAnalysisReport Report;

	Report.ToolVersion = AssetAnalysisReport::GetToolVersion();
	Report.GeneratedAtUtc = FDateTime::UtcNow();

	Report.OldFilename = Diff.OldFilename;
	Report.NewFilename = Diff.NewFilename;
	Report.OldFileHash = Diff.OldFileHash;
	Report.NewFileHash = Diff.NewFileHash;
	Report.bFilesIdentical = Diff.bFilesIdentical;

	Report.Summary = Summarize(Diff.Entries);

	if (Filter != nullptr)
	{
		Report.Differences = Filter->FilterEntries(Diff.Entries);
		Report.FilterDescription = Filter->Describe();
	}
	else
	{
		Report.Differences = Diff.Entries;
	}

	if (SaveAnalysis != nullptr)
	{
		Report.SaveAnalysis = *SaveAnalysis;
	}

	Report.RepeatedSavePatterns = RepeatedSavePatterns;

	return Report;
}
