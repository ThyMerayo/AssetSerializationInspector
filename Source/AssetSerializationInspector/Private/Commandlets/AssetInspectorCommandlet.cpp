// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Commandlets/AssetInspectorCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Compare/AssetFolderComparison.h"
#include "Coverage/AssetDecoderCoverage.h"
#include "Report/AssetBatchReportWriter.h"
#include "Report/AssetFolderComparisonReportWriter.h"
#include "Save/AssetBatchResave.h"

DEFINE_LOG_CATEGORY_STATIC(LogAssetInspectorCommandlet, Log, All);

namespace
{
	/** Logs a line about progress at most every few seconds, so a long run shows it is alive without flooding the log. */
	class FProgressLog
	{
	public:
		void Tick(const TCHAR* What, const int32 Index, const int32 Total, const FString& Item)
		{
			if (FPlatformTime::Seconds() - LastLog > 10.0)
			{
				LastLog = FPlatformTime::Seconds();
				UE_LOG(LogAssetInspectorCommandlet, Display, TEXT("%s: %d of %d (%s)"), What, Index + 1, Total, *Item);
			}
		}

	private:
		double LastLog = FPlatformTime::Seconds();
	};

	/** Relative paths are taken from the project folder: the working directory of a commandlet is the engine's binaries folder. */
	FString ResolvePath(const FString& Path)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
	}

	/** Makes the report path absolute and its folder exist. An empty path means no report. */
	bool PrepareReportFile(const FString& Params, FString& OutReport)
	{
		FParse::Value(*Params, TEXT("Report="), OutReport);
		if (OutReport.IsEmpty())
		{
			return true;
		}

		OutReport = ResolvePath(OutReport);
		if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutReport), true))
		{
			UE_LOG(LogAssetInspectorCommandlet, Error, TEXT("The folder of the report file could not be created: %s"), *OutReport);
			return false;
		}

		return true;
	}

	int32 FailedToWrite(const FText& Error)
	{
		UE_LOG(LogAssetInspectorCommandlet, Error, TEXT("%s"), *Error.ToString());
		return AssetInspectorCommandlet::ExitError;
	}

	int32 RunNoOpResave(const FString& Params)
	{
		FString PathList;
		if (!FParse::Value(*Params, TEXT("Path="), PathList, false) || PathList.IsEmpty())
		{
			UE_LOG(LogAssetInspectorCommandlet, Error, TEXT("-Mode=NoOpResave needs -Path=<content path>[,<content path>...], such as -Path=/Game/Characters."));
			return AssetInspectorCommandlet::ExitError;
		}

		FString Report;
		if (!PrepareReportFile(Params, Report))
		{
			return AssetInspectorCommandlet::ExitError;
		}

		TArray<FString> Paths;
		PathList.ParseIntoArray(Paths, TEXT(","), true);

		// Assets are found through the registry, which scans in the background unless it is asked to finish.
		FAssetRegistryModule::GetRegistry().SearchAllAssets(true);

		const TArray<FName> Packages = AssetBatchResave::CollectPackages(Paths, true);
		if (Packages.IsEmpty())
		{
			UE_LOG(LogAssetInspectorCommandlet, Error, TEXT("No assets were found under %s."), *PathList);
			return AssetInspectorCommandlet::ExitError;
		}

		UE_LOG(LogAssetInspectorCommandlet, Display, TEXT("Testing %d assets under %s."), Packages.Num(), *PathList);

		FProgressLog Progress;
		const FAssetBatchResaveResult Result = AssetBatchResave::Run(Packages, PathList, [&Progress](const int32 Index, const int32 Total, const FName PackageName) {
			Progress.Tick(TEXT("No-op resave test"), Index, Total, PackageName.ToString());
			return true;
		});

		const FAssetBatchResaveSummary Summary = Result.Summarize();
		UE_LOG(LogAssetInspectorCommandlet, Display, TEXT("No-op resave test of %s: %d stable, %d normalized on the first save, %d unstable, %d skipped, %d failed."), *PathList, Summary.Stable,
			Summary.NormalizedOnFirstSave, Summary.Unstable, Summary.Skipped, Summary.Failed);

		if (!Report.IsEmpty())
		{
			FText Error;
			if (!AssetBatchReportWriter::SaveToFile(Result, Report, Error))
			{
				return FailedToWrite(Error);
			}

			UE_LOG(LogAssetInspectorCommandlet, Display, TEXT("Report: %s"), *Report);
		}

		const bool bFailOnUnstable = FParse::Param(*Params, TEXT("FailOnUnstable"));
		return bFailOnUnstable && (Summary.Unstable > 0 || Summary.Failed > 0) ? AssetInspectorCommandlet::ExitFindings : AssetInspectorCommandlet::ExitOk;
	}

	int32 RunCompareFolders(const FString& Params)
	{
		FString OldFolder;
		FString NewFolder;
		if (!FParse::Value(*Params, TEXT("Old="), OldFolder) || !FParse::Value(*Params, TEXT("New="), NewFolder) || OldFolder.IsEmpty() || NewFolder.IsEmpty())
		{
			UE_LOG(LogAssetInspectorCommandlet, Error, TEXT("-Mode=CompareFolders needs -Old=<folder> and -New=<folder>."));
			return AssetInspectorCommandlet::ExitError;
		}

		OldFolder = ResolvePath(OldFolder);
		NewFolder = ResolvePath(NewFolder);
		for (const FString* Folder : { &OldFolder, &NewFolder })
		{
			if (!IFileManager::Get().DirectoryExists(**Folder))
			{
				UE_LOG(LogAssetInspectorCommandlet, Error, TEXT("The folder does not exist: %s"), **Folder);
				return AssetInspectorCommandlet::ExitError;
			}
		}

		FString Report;
		if (!PrepareReportFile(Params, Report))
		{
			return AssetInspectorCommandlet::ExitError;
		}

		FProgressLog Progress;
		const FAssetFolderComparisonResult Result = AssetFolderComparison::Run(OldFolder, NewFolder, [&Progress](const int32 Index, const int32 Total, const FString& RelativePath) {
			Progress.Tick(TEXT("Folder comparison"), Index, Total, RelativePath);
			return true;
		});

		const FAssetFolderComparisonSummary Summary = Result.Summarize();
		UE_LOG(LogAssetInspectorCommandlet, Display, TEXT("Folder comparison: %d identical, %d changed (%d with different versions), %d only in the old folder, %d only in the new folder, %d failed."),
			Summary.Identical, Summary.Changed, Summary.ChangedWithDifferentVersions, Summary.OnlyInOldFolder, Summary.OnlyInNewFolder, Summary.Failed);

		if (!Report.IsEmpty())
		{
			FText Error;
			if (!AssetFolderComparisonReportWriter::SaveToFile(Result, Report, Error))
			{
				return FailedToWrite(Error);
			}

			UE_LOG(LogAssetInspectorCommandlet, Display, TEXT("Report: %s"), *Report);
		}

		const bool bFailOnChanges = FParse::Param(*Params, TEXT("FailOnChanges"));
		const bool bFoundDifferences = Summary.Changed > 0 || Summary.OnlyInOldFolder > 0 || Summary.OnlyInNewFolder > 0 || Summary.Failed > 0;
		return bFailOnChanges && bFoundDifferences ? AssetInspectorCommandlet::ExitFindings : AssetInspectorCommandlet::ExitOk;
	}

	int32 RunDecodeCoverage(const FString& Params)
	{
		FString Folder;
		if (!FParse::Value(*Params, TEXT("Folder="), Folder) || Folder.IsEmpty())
		{
			UE_LOG(LogAssetInspectorCommandlet, Error, TEXT("-Mode=DecodeCoverage needs -Folder=<existing folder>."));
			return AssetInspectorCommandlet::ExitError;
		}

		Folder = ResolvePath(Folder);
		if (!IFileManager::Get().DirectoryExists(*Folder))
		{
			UE_LOG(LogAssetInspectorCommandlet, Error, TEXT("The folder does not exist: %s"), *Folder);
			return AssetInspectorCommandlet::ExitError;
		}

		FString Report;
		if (!PrepareReportFile(Params, Report))
		{
			return AssetInspectorCommandlet::ExitError;
		}

		FProgressLog Progress;
		const FAssetDecoderCoverageResult Result = AssetDecoderCoverage::Run(Folder, [&Progress](const int32 Index, const int32 Total, const FString& Item) {
			Progress.Tick(TEXT("Decode coverage"), Index, Total, Item);
			return true;
		});

		UE_LOG(LogAssetInspectorCommandlet, Display, TEXT("Decode coverage: %d assets, %d unreadable, %d of %d properties decoded, %d kinds of failure."), Result.AssetsScanned,
			Result.AssetsUnreadable, Result.PropertiesDecoded, Result.PropertiesScanned, Result.Issues.Num());

		if (!Report.IsEmpty())
		{
			FText Error;
			if (!AssetDecoderCoverage::SaveToFile(Result, Report, Error))
			{
				return FailedToWrite(Error);
			}

			UE_LOG(LogAssetInspectorCommandlet, Display, TEXT("Report: %s"), *Report);
		}

		return AssetInspectorCommandlet::ExitOk;
	}
} // namespace

UAssetSerializationInspectorCommandlet::UAssetSerializationInspectorCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UAssetSerializationInspectorCommandlet::Main(const FString& Params)
{
	return AssetInspectorCommandlet::Execute(Params);
}

int32 AssetInspectorCommandlet::Execute(const FString& Params)
{
	FString Mode;
	FParse::Value(*Params, TEXT("Mode="), Mode);

	if (Mode.Equals(TEXT("NoOpResave"), ESearchCase::IgnoreCase))
	{
		return RunNoOpResave(Params);
	}

	if (Mode.Equals(TEXT("CompareFolders"), ESearchCase::IgnoreCase))
	{
		return RunCompareFolders(Params);
	}

	if (Mode.Equals(TEXT("DecodeCoverage"), ESearchCase::IgnoreCase))
	{
		return RunDecodeCoverage(Params);
	}

	UE_LOG(LogAssetInspectorCommandlet, Error, TEXT("Unknown or missing -Mode. Use -Mode=NoOpResave, -Mode=CompareFolders or -Mode=DecodeCoverage."));
	return ExitError;
}
