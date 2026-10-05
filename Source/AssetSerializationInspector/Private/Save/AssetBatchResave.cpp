// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Save/AssetBatchResave.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/World.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

#include "Save/AssetSaveObserver.h"

namespace
{
	/** How many packages a run tests before collecting the garbage left by the ones it had to load. */
	constexpr int32 PackagesPerGarbageCollection = 20;

	const TCHAR* ClassificationName(const EAssetSaveChangeClassification Classification)
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

	void AddChange(TArray<FAssetBatchResaveChange>& OutChanges, int32& OutOmitted, FAssetBatchResaveChange&& Change)
	{
		if (OutChanges.Num() < FAssetBatchResaveEntry::MaximumChangesPerResave)
		{
			OutChanges.Add(MoveTemp(Change));
		}
		else
		{
			++OutOmitted;
		}
	}

	void AddExplanations(const TArray<FAssetSaveExplanationEntry>& Entries, TArray<FAssetBatchResaveChange>& OutChanges, int32& OutOmitted)
	{
		for (const FAssetSaveExplanationEntry& Entry : Entries)
		{
			FAssetBatchResaveChange Change;
			Change.Category = ClassificationName(Entry.Classification);
			Change.Name = Entry.Title.ToString();

			if (Entry.bHasOldValue || Entry.bHasNewValue)
			{
				Change.Detail = FString::Printf(TEXT("%s => %s"), Entry.bHasOldValue ? *Entry.OldValue : TEXT("(none)"), Entry.bHasNewValue ? *Entry.NewValue : TEXT("(none)"));
			}
			else
			{
				Change.Detail = Entry.Description.ToString();
			}

			// Container changes are a heading for their children, which are listed on their own.
			if (Entry.Children.IsEmpty())
			{
				AddChange(OutChanges, OutOmitted, MoveTemp(Change));
			}

			AddExplanations(Entry.Children, OutChanges, OutOmitted);
		}
	}

	/** Lets go of a package the run loaded, so a project-wide run does not keep every asset in memory. */
	void ReleasePackage(UPackage* Package)
	{
		if (Package == nullptr)
		{
			return;
		}

		TArray<UObject*> Objects;
		GetObjectsWithPackage(Package, Objects);

		for (UObject* Object : Objects)
		{
			Object->ClearFlags(RF_Standalone);
		}

		Package->ClearFlags(RF_Standalone);
	}
} // namespace

FAssetBatchResaveSummary FAssetBatchResaveResult::Summarize() const
{
	FAssetBatchResaveSummary Summary;

	for (const FAssetBatchResaveEntry& Entry : Entries)
	{
		switch (Entry.Status)
		{
			case EAssetBatchResaveStatus::Skipped:
				++Summary.Skipped;
				break;

			case EAssetBatchResaveStatus::Failed:
				++Summary.Failed;
				break;

			case EAssetBatchResaveStatus::Tested:
				++Summary.Tested;

				switch (Entry.Verdict)
				{
					case ENoOpResaveVerdict::Stable:
						++Summary.Stable;
						break;

					case ENoOpResaveVerdict::NormalizedOnFirstSave:
						++Summary.NormalizedOnFirstSave;
						break;

					case ENoOpResaveVerdict::Unstable:
						++Summary.Unstable;
						break;
				}
				break;
		}
	}

	return Summary;
}

TArray<FAssetBatchRecurringChange> FAssetBatchResaveResult::FindRecurringChanges(const int32 MinimumAssets) const
{
	// Each asset counts once per change, however many times the change appears inside it.
	TMap<FString, FAssetBatchRecurringChange> Changes;

	for (const FAssetBatchResaveEntry& Entry : Entries)
	{
		TSet<FString> Seen;

		for (const FAssetBatchResaveChange& Change : Entry.FirstResaveChanges)
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

TArray<FName> AssetBatchResave::CollectPackages(const TArray<FString>& PackagePaths, const bool bRecursive)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	const FTopLevelAssetPath WorldClass = UWorld::StaticClass()->GetClassPathName();

	TSet<FName> Packages;

	for (const FString& Path : PackagePaths)
	{
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByPath(FName(*Path), Assets, bRecursive);

		for (const FAssetData& Asset : Assets)
		{
			if (!Asset.IsRedirector() && Asset.AssetClassPath != WorldClass)
			{
				Packages.Add(Asset.PackageName);
			}
		}
	}

	TArray<FName> Result = Packages.Array();
	Result.Sort([](const FName& Left, const FName& Right) { return Left.LexicalLess(Right); });
	return Result;
}

void AssetBatchResave::ExtractChanges(const FObservedAssetSave& Save, TArray<FAssetBatchResaveChange>& OutChanges, int32& OutOmitted)
{
	ExtractChanges(Save.Diff, Save.Analysis, OutChanges, OutOmitted);
}

void AssetBatchResave::ExtractChanges(const FAssetPackageDiffResult& Diff, const FAssetSaveAnalysis& Analysis, TArray<FAssetBatchResaveChange>& OutChanges, int32& OutOmitted)
{
	// The header is not covered by the save analysis, so its regions are read from the comparison itself.
	for (const FAssetPackageDiffEntry& Entry : Diff.Entries)
	{
		if (Entry.Kind != EAssetPackageDiffKind::Header)
		{
			continue;
		}

		for (const FAssetPackageDiffEntry& Region : Entry.Children)
		{
			if (Region.State == EAssetPackageDiffState::Unchanged)
			{
				continue;
			}

			FAssetBatchResaveChange Change;
			Change.Category = TEXT("HeaderRegion");
			Change.Name = Region.DisplayName.ToString();
			Change.Detail = Region.Explanation.IsEmpty() ? FString::Printf(TEXT("%s => %s"), *Region.OldValue, *Region.NewValue) : Region.Explanation.ToString();
			AddChange(OutChanges, OutOmitted, MoveTemp(Change));
		}
	}

	AddExplanations(Analysis.SemanticChanges, OutChanges, OutOmitted);
	AddExplanations(Analysis.LayoutChanges, OutChanges, OutOmitted);
	AddExplanations(Analysis.UnexplainedChanges, OutChanges, OutOmitted);
}

FAssetBatchResaveEntry AssetBatchResave::Condense(const FNoOpResaveResult& Result)
{
	FAssetBatchResaveEntry Entry;
	Entry.PackageName = Result.PackageName;

	if (!Result.bSucceeded)
	{
		Entry.Status = Result.bSkipped ? EAssetBatchResaveStatus::Skipped : EAssetBatchResaveStatus::Failed;
		Entry.Message = Result.Error.ToString();
		return Entry;
	}

	Entry.Status = EAssetBatchResaveStatus::Tested;
	Entry.Verdict = Result.Verdict;

	if (Result.bOriginalFileModified)
	{
		// Something wrote to the file under test, so nothing about it can be trusted.
		Entry.Status = EAssetBatchResaveStatus::Failed;
		Entry.Message = TEXT("The original file changed during the test, so the result cannot be trusted.");
		return Entry;
	}

	if (Result.FirstResave.IsValid())
	{
		Entry.FirstResaveChangedBytes = Result.FirstResave->Analysis.TotalChangedBytes;
		ExtractChanges(*Result.FirstResave, Entry.FirstResaveChanges, Entry.FirstResaveChangesOmitted);
	}

	if (Result.SecondResave.IsValid())
	{
		Entry.SecondResaveChangedBytes = Result.SecondResave->Analysis.TotalChangedBytes;
		ExtractChanges(*Result.SecondResave, Entry.SecondResaveChanges, Entry.SecondResaveChangesOmitted);
	}

	return Entry;
}

FAssetBatchResaveResult AssetBatchResave::Run(const TArray<FName>& PackageNames, const FString& Scope, TFunctionRef<bool(int32 Index, int32 Total, FName PackageName)> ShouldContinue)
{
	FAssetBatchResaveResult Result;
	Result.Scope = Scope;
	Result.StartedAt = FDateTime::Now();

	for (int32 Index = 0; Index < PackageNames.Num(); ++Index)
	{
		const FName PackageName = PackageNames[Index];

		if (!ShouldContinue(Index, PackageNames.Num(), PackageName))
		{
			Result.bCancelled = true;
			break;
		}

		const double StartTime = FPlatformTime::Seconds();

		// Leave alone anything that was already open: the user may be working with it.
		const bool bWasLoaded = FindPackage(nullptr, *PackageName.ToString()) != nullptr;
		UPackage* Package = LoadPackage(nullptr, *PackageName.ToString(), LOAD_None);

		FAssetBatchResaveEntry Entry;
		if (Package == nullptr)
		{
			Entry.PackageName = PackageName;
			Entry.Status = EAssetBatchResaveStatus::Failed;
			Entry.Message = TEXT("The package could not be loaded.");
		}
		else
		{
			Entry = Condense(AssetNoOpResaveTest::Run(Package));

			if (!bWasLoaded)
			{
				ReleasePackage(Package);
			}
		}

		Entry.Seconds = FPlatformTime::Seconds() - StartTime;
		Result.Entries.Add(MoveTemp(Entry));

		if ((Index + 1) % PackagesPerGarbageCollection == 0)
		{
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
	}

	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	Result.FinishedAt = FDateTime::Now();
	return Result;
}
