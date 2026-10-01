// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Save/AssetNoOpResaveTest.h"

#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "Save/AssetSaveObserver.h"

#define LOCTEXT_NAMESPACE "AssetNoOpResaveTest"

namespace
{
	struct FFileStamp
	{
		int64 Size = INDEX_NONE;
		FDateTime ModifiedTime;

		bool operator==(const FFileStamp& Other) const { return Size == Other.Size && ModifiedTime == Other.ModifiedTime; }
	};

	FFileStamp StampFile(const FString& Filename)
	{
		FFileStamp Stamp;
		Stamp.Size = IFileManager::Get().FileSize(*Filename);
		Stamp.ModifiedTime = IFileManager::Get().GetTimeStamp(*Filename);
		return Stamp;
	}

	/** Saves the package to a temporary file, leaving the package itself as it was. */
	bool SaveCopy(UPackage* Package, UObject* Asset, const FString& Filename)
	{
		IFileManager::Get().Delete(*Filename, false, true, true);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;

		// The copy must not leave a trace on the package: keep it dirty-neutral, and keep the progress dialog away.
		// The target is outside every mounted content root, so the package's loaded path is not updated either.
		// Saving to a path other than the loaded one would otherwise give the package a new persistent GUID, both in the copy
		// and in memory, which would make every resave look different.
		SaveArgs.SaveFlags = SAVE_NoError | SAVE_KeepDirty | SAVE_KeepPersistentGUID;
		SaveArgs.bSlowTask = false;
		SaveArgs.bWarnOfLongFilename = false;

		return UPackage::SavePackage(Package, Asset, *Filename, SaveArgs) && FPaths::FileExists(Filename);
	}

	FNoOpResaveResult Fail(const FName PackageName, const FText& Error)
	{
		FNoOpResaveResult Result;
		Result.PackageName = PackageName;
		Result.Error = Error;
		return Result;
	}
} // namespace

ENoOpResaveVerdict AssetNoOpResaveTest::ClassifyVerdict(const bool bFirstResaveIdentical, const bool bSecondResaveIdentical)
{
	if (bFirstResaveIdentical && bSecondResaveIdentical)
	{
		return ENoOpResaveVerdict::Stable;
	}

	// The first resave may rewrite the file once (for example to a newer format) and then settle.
	return bSecondResaveIdentical ? ENoOpResaveVerdict::NormalizedOnFirstSave : ENoOpResaveVerdict::Unstable;
}

FNoOpResaveResult AssetNoOpResaveTest::Run(UPackage* Package)
{
	if (Package == nullptr)
	{
		return Fail(NAME_None, LOCTEXT("NoPackage", "There is no package to test."));
	}

	const FName PackageName = Package->GetFName();

	if (Package->ContainsMap())
	{
		return Fail(PackageName, LOCTEXT("MapPackage", "Level packages are not supported."));
	}

	if (Package->IsDirty())
	{
		return Fail(PackageName, LOCTEXT("DirtyPackage", "The asset has unsaved changes. Save it first, so the test compares like with like."));
	}

	const FString OriginalFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());

	if (!FPaths::FileExists(OriginalFilename))
	{
		return Fail(PackageName, LOCTEXT("NoFileOnDisk", "The asset has not been saved yet, so there is no file to compare with."));
	}

	Package->FullyLoad();

	UObject* Asset = Package->FindAssetInPackage();

	if (Asset == nullptr)
	{
		return Fail(PackageName, LOCTEXT("NoAsset", "The package does not contain an asset."));
	}

	// The summary stores a checksum of the saved file's base name, so the copies keep the asset's own file name and
	// differ only by directory. Otherwise every copy would differ from the original in that field alone.
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AssetSerializationInspector"), TEXT("NoOpResave"), FPaths::GetBaseFilename(OriginalFilename));
	const FString FirstFilename = FPaths::Combine(Directory, TEXT("Resave1"), FPaths::GetCleanFilename(OriginalFilename));
	const FString SecondFilename = FPaths::Combine(Directory, TEXT("Resave2"), FPaths::GetCleanFilename(OriginalFilename));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FirstFilename), true);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SecondFilename), true);

	const FFileStamp OriginalStamp = StampFile(OriginalFilename);

	// Saving would otherwise be reported as an observed save of a monitored asset.
	FAssetSaveObserver::FScopedSuppression Suppression;

	if (!SaveCopy(Package, Asset, FirstFilename) || !SaveCopy(Package, Asset, SecondFilename))
	{
		return Fail(PackageName, LOCTEXT("SaveFailed", "The package could not be saved to a temporary file."));
	}

	FNoOpResaveResult Result;
	Result.PackageName = PackageName;
	Result.FirstResave = FAssetSaveObserver::Get().BuildObservedSave(PackageName, OriginalFilename, FirstFilename);
	Result.SecondResave = FAssetSaveObserver::Get().BuildObservedSave(PackageName, FirstFilename, SecondFilename);
	Result.bOriginalFileModified = !(StampFile(OriginalFilename) == OriginalStamp);

	if (!Result.FirstResave.IsValid() || !Result.FirstResave->Before.IsValid() || !Result.FirstResave->After.IsValid() || !Result.SecondResave.IsValid() || !Result.SecondResave->After.IsValid())
	{
		return Fail(PackageName, LOCTEXT("ReadFailed", "The saved copies could not be read back."));
	}

	Result.Verdict = ClassifyVerdict(!Result.FirstResave->HasChanges(), !Result.SecondResave->HasChanges());
	Result.bSucceeded = true;

	return Result;
}

#undef LOCTEXT_NAMESPACE
