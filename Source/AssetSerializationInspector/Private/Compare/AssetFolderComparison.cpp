// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Compare/AssetFolderComparison.h"

#include "HAL/FileManager.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "Serialization/CustomVersion.h"
#include "UObject/PackageFileSummary.h"

#include "Diff/AssetPackageDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Save/AssetSaveAnalyzer.h"
#include "Trace/AssetPackageFieldDecoder.h"

namespace
{
	FString NormalizeKey(const FString& RelativePath)
	{
		return RelativePath.ToLower();
	}

	FAssetFolderComparisonEntry MakeFailed(const FString& RelativePath, const FString& Message)
	{
		FAssetFolderComparisonEntry Entry;
		Entry.RelativePath = RelativePath;
		Entry.Status = EAssetFolderComparisonStatus::Failed;
		Entry.Message = Message;
		return Entry;
	}
} // namespace

FAssetFolderComparisonSummary FAssetFolderComparisonResult::Summarize() const
{
	FAssetFolderComparisonSummary Summary;

	for (const FAssetFolderComparisonEntry& Entry : Entries)
	{
		switch (Entry.Status)
		{
			case EAssetFolderComparisonStatus::Identical:
				++Summary.Identical;
				break;

			case EAssetFolderComparisonStatus::Changed:
				++Summary.Changed;
				Summary.ChangedWithDifferentVersions += Entry.bVersionsDiffer ? 1 : 0;
				break;

			case EAssetFolderComparisonStatus::OnlyInOldFolder:
				++Summary.OnlyInOldFolder;
				break;

			case EAssetFolderComparisonStatus::OnlyInNewFolder:
				++Summary.OnlyInNewFolder;
				break;

			case EAssetFolderComparisonStatus::Failed:
				++Summary.Failed;
				break;
		}
	}

	return Summary;
}

TArray<FAssetEngineVersionPair> FAssetFolderComparisonResult::FindEngineVersionPairs() const
{
	TMap<FString, FAssetEngineVersionPair> Pairs;

	for (const FAssetFolderComparisonEntry& Entry : Entries)
	{
		if (Entry.Status != EAssetFolderComparisonStatus::Identical && Entry.Status != EAssetFolderComparisonStatus::Changed)
		{
			continue;
		}

		const FString Key = Entry.OldEngineVersion + TEXT("|") + Entry.NewEngineVersion;
		FAssetEngineVersionPair& Pair = Pairs.FindOrAdd(Key);
		Pair.OldEngineVersion = Entry.OldEngineVersion;
		Pair.NewEngineVersion = Entry.NewEngineVersion;
		++Pair.AssetCount;
	}

	TArray<FAssetEngineVersionPair> Result;
	Pairs.GenerateValueArray(Result);

	Result.Sort([](const FAssetEngineVersionPair& Left, const FAssetEngineVersionPair& Right) {
		return Left.AssetCount != Right.AssetCount ? Left.AssetCount > Right.AssetCount : Left.OldEngineVersion + Left.NewEngineVersion < Right.OldEngineVersion + Right.NewEngineVersion;
	});

	return Result;
}

TArray<FAssetBatchRecurringChange> FAssetFolderComparisonResult::FindRecurringChanges(const int32 MinimumAssets) const
{
	// Each asset counts once per change, however many times the change appears inside it.
	TMap<FString, FAssetBatchRecurringChange> Changes;

	for (const FAssetFolderComparisonEntry& Entry : Entries)
	{
		TSet<FString> Seen;

		for (const FAssetBatchResaveChange& Change : Entry.Changes)
		{
			const FString Key = Change.Category + TEXT("|") + Change.Name;

			if (!Seen.Contains(Key))
			{
				Seen.Add(Key);

				FAssetBatchRecurringChange& Recurring = Changes.FindOrAdd(Key);
				Recurring.Category = Change.Category;
				Recurring.Name = Change.Name;
				++Recurring.AssetCount;
			}
		}
	}

	TArray<FAssetBatchRecurringChange> Result;

	for (const TPair<FString, FAssetBatchRecurringChange>& Pair : Changes)
	{
		if (Pair.Value.AssetCount >= MinimumAssets)
		{
			Result.Add(Pair.Value);
		}
	}

	Result.Sort([](const FAssetBatchRecurringChange& Left, const FAssetBatchRecurringChange& Right) {
		return Left.AssetCount != Right.AssetCount ? Left.AssetCount > Right.AssetCount : Left.Name < Right.Name;
	});

	return Result;
}

TArray<FString> AssetFolderComparison::FindPackageFiles(const FString& Folder)
{
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Folder, TEXT("*.uasset"), true, false);

	TArray<FString> Maps;
	IFileManager::Get().FindFilesRecursive(Maps, *Folder, TEXT("*.umap"), true, false);
	Files.Append(Maps);

	FString Root = FPaths::ConvertRelativePathToFull(Folder);
	FPaths::NormalizeFilename(Root);
	if (!Root.EndsWith(TEXT("/")))
	{
		Root += TEXT("/");
	}

	TArray<FString> Relative;
	for (const FString& File : Files)
	{
		FString Full = FPaths::ConvertRelativePathToFull(File);
		FPaths::NormalizeFilename(Full);

		if (Full.StartsWith(Root, ESearchCase::IgnoreCase))
		{
			Relative.Add(Full.RightChop(Root.Len()));
		}
	}

	Relative.Sort();
	return Relative;
}

FString AssetFolderComparison::DescribeEngineVersion(const FAssetPackageDocument& Document)
{
	const FEngineVersion& Version = Document.PackageSummary.SavedByEngineVersion;
	return Version.GetMajor() == 0 && Version.GetMinor() == 0 && Version.GetPatch() == 0 ? FString() : Version.ToString(EVersionComponent::Patch);
}

FString AssetFolderComparison::DescribeFileVersion(const FAssetPackageDocument& Document)
{
	const FPackageFileVersion Version = Document.PackageSummary.GetFileVersionUE();
	return FString::Printf(TEXT("UE4 %d / UE5 %d"), static_cast<int32>(Version.FileVersionUE4), static_cast<int32>(Version.FileVersionUE5));
}

bool AssetFolderComparison::HaveDifferentVersions(const FAssetPackageDocument& Old, const FAssetPackageDocument& New)
{
	const FPackageFileSummary& A = Old.PackageSummary;
	const FPackageFileSummary& B = New.PackageSummary;

	if (!(A.GetFileVersionUE() == B.GetFileVersionUE()) || A.GetFileVersionLicenseeUE() != B.GetFileVersionLicenseeUE() || A.SavedByEngineVersion.ToString() != B.SavedByEngineVersion.ToString())
	{
		return true;
	}

	TMap<FGuid, int32> OldVersions;
	for (const FCustomVersion& Version : A.GetCustomVersionContainer().GetAllVersions())
	{
		OldVersions.Add(Version.Key, Version.Version);
	}

	const FCustomVersionArray& NewVersions = B.GetCustomVersionContainer().GetAllVersions();
	if (NewVersions.Num() != OldVersions.Num())
	{
		return true;
	}

	for (const FCustomVersion& Version : NewVersions)
	{
		const int32* OldVersion = OldVersions.Find(Version.Key);

		if (OldVersion == nullptr || *OldVersion != Version.Version)
		{
			return true;
		}
	}

	return false;
}

FAssetFolderComparisonEntry AssetFolderComparison::ComparePair(const FString& RelativePath, const FString& OldFilename, const FString& NewFilename)
{
	FText Error;

	const TSharedPtr<FAssetPackageDocument> OldDocument = FAssetPackageReader::LoadFromFile(OldFilename, Error);
	if (!OldDocument.IsValid())
	{
		return MakeFailed(RelativePath, FString::Printf(TEXT("Old file: %s"), *Error.ToString().Replace(TEXT("\n"), TEXT(" "))));
	}

	const TSharedPtr<FAssetPackageDocument> NewDocument = FAssetPackageReader::LoadFromFile(NewFilename, Error);
	if (!NewDocument.IsValid())
	{
		return MakeFailed(RelativePath, FString::Printf(TEXT("New file: %s"), *Error.ToString().Replace(TEXT("\n"), TEXT(" "))));
	}

	FAssetFolderComparisonEntry Entry;
	Entry.RelativePath = RelativePath;
	Entry.OldEngineVersion = DescribeEngineVersion(*OldDocument);
	Entry.NewEngineVersion = DescribeEngineVersion(*NewDocument);
	Entry.OldFileVersion = DescribeFileVersion(*OldDocument);
	Entry.NewFileVersion = DescribeFileVersion(*NewDocument);
	Entry.bVersionsDiffer = HaveDifferentVersions(*OldDocument, *NewDocument);
	Entry.OldFileSize = OldDocument->FileData.Num();
	Entry.NewFileSize = NewDocument->FileData.Num();

	// Identical bytes need no decoding.
	if (OldDocument->FileData.Num() == NewDocument->FileData.Num() && FMemory::Memcmp(OldDocument->FileData.GetData(), NewDocument->FileData.GetData(), OldDocument->FileData.Num()) == 0)
	{
		Entry.Status = EAssetFolderComparisonStatus::Identical;
		return Entry;
	}

	const TSharedPtr<FAssetPackageTraceCollection> OldTraces = FAssetPackageFieldDecoder::Decode(*OldDocument);
	const TSharedPtr<FAssetPackageTraceCollection> NewTraces = FAssetPackageFieldDecoder::Decode(*NewDocument);

	const FAssetPackageDiffResult Diff = AssetPackageDiff::Compare(*OldDocument, *NewDocument, OldTraces.Get(), NewTraces.Get());
	const FAssetSaveAnalysis Analysis = FAssetSaveAnalyzer::Analyze(Diff, *OldDocument, *NewDocument);

	Entry.Status = EAssetFolderComparisonStatus::Changed;
	AssetBatchResave::ExtractChanges(Diff, Analysis, Entry.Changes, Entry.ChangesOmitted);

	return Entry;
}

FAssetFolderComparisonResult AssetFolderComparison::Run(const FString& OldFolder, const FString& NewFolder, TFunctionRef<bool(int32 Index, int32 Total, const FString& RelativePath)> ShouldContinue)
{
	FAssetFolderComparisonResult Result;
	Result.OldFolder = OldFolder;
	Result.NewFolder = NewFolder;
	Result.StartedAt = FDateTime::Now();

	// Pair the files by relative path. Windows paths are case-insensitive, and a package keeps its name when copied.
	TMap<FString, FString> OldFiles;
	TMap<FString, FString> NewFiles;
	for (const FString& File : FindPackageFiles(OldFolder))
	{
		OldFiles.Add(NormalizeKey(File), File);
	}
	for (const FString& File : FindPackageFiles(NewFolder))
	{
		NewFiles.Add(NormalizeKey(File), File);
	}

	TSet<FString> KeySet;
	for (const TPair<FString, FString>& Pair : OldFiles)
	{
		KeySet.Add(Pair.Key);
	}
	for (const TPair<FString, FString>& Pair : NewFiles)
	{
		KeySet.Add(Pair.Key);
	}

	TArray<FString> Keys = KeySet.Array();
	Keys.Sort();

	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		const FString& Key = Keys[Index];
		const FString* OldFile = OldFiles.Find(Key);
		const FString* NewFile = NewFiles.Find(Key);
		const FString& RelativePath = NewFile != nullptr ? *NewFile : *OldFile;

		if (!ShouldContinue(Index, Keys.Num(), RelativePath))
		{
			Result.bCancelled = true;
			break;
		}

		if (OldFile != nullptr && NewFile != nullptr)
		{
			Result.Entries.Add(ComparePair(RelativePath, FPaths::Combine(OldFolder, *OldFile), FPaths::Combine(NewFolder, *NewFile)));
			continue;
		}

		// Present in one folder only. Read its versions so the report can still say which engine saved it.
		FAssetFolderComparisonEntry Entry;
		Entry.RelativePath = RelativePath;
		Entry.Status = OldFile != nullptr ? EAssetFolderComparisonStatus::OnlyInOldFolder : EAssetFolderComparisonStatus::OnlyInNewFolder;

		FText Error;
		const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(FPaths::Combine(OldFile != nullptr ? OldFolder : NewFolder, RelativePath), Error);
		if (Document.IsValid())
		{
			(OldFile != nullptr ? Entry.OldEngineVersion : Entry.NewEngineVersion) = DescribeEngineVersion(*Document);
			(OldFile != nullptr ? Entry.OldFileVersion : Entry.NewFileVersion) = DescribeFileVersion(*Document);
			(OldFile != nullptr ? Entry.OldFileSize : Entry.NewFileSize) = Document->FileData.Num();
		}

		Result.Entries.Add(MoveTemp(Entry));
	}

	Result.FinishedAt = FDateTime::Now();
	return Result;
}
