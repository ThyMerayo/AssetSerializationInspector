// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "ISourceControlRevision.h"
#include "Misc/Paths.h"

#include "Compare/AssetSourceControlCompare.h"
#include "Diff/AssetPackageDiff.h"
#include "Save/AssetSaveAnalyzer.h"
#include "Widgets/SAssetSerializationDiff.h"

namespace
{
	/** A revision whose contents are a file on disk, standing in for what a provider downloads. */
	class FFakeRevision : public ISourceControlRevision
	{
	public:
		FFakeRevision(const FString& InSourceFile, const FString& InRevision, const FString& InDescription) : SourceFile(InSourceFile), Revision(InRevision), Description(InDescription) {}

		virtual bool Get(FString& InOutFilename, EConcurrency::Type InConcurrency = EConcurrency::Synchronous) const override
		{
			return !bFailToGet && IFileManager::Get().Copy(*InOutFilename, *SourceFile) == COPY_OK;
		}
		virtual bool GetAnnotated(TArray<FAnnotationLine>& OutLines) const override { return false; }
		virtual bool GetAnnotated(FString& InOutFilename) const override { return false; }
		virtual const FString& GetFilename() const override { return SourceFile; }
		virtual int32 GetRevisionNumber() const override { return 7; }
		virtual const FString& GetRevision() const override { return Revision; }
		virtual const FString& GetDescription() const override { return Description; }
		virtual const FString& GetUserName() const override { return UserName; }
		virtual const FString& GetClientSpec() const override { return UserName; }
		virtual const FString& GetAction() const override { return Action; }
		virtual TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> GetBranchSource() const override { return nullptr; }
		virtual const FDateTime& GetDate() const override { return Date; }
		virtual int32 GetCheckInIdentifier() const override { return 7; }
		virtual int32 GetFileSize() const override { return 0; }

		bool bFailToGet = false;
		FString UserName = TEXT("ana");
		FString Action = TEXT("edit");
		FDateTime Date = FDateTime(2026, 10, 1, 14, 7, 0);

	private:
		FString SourceFile;
		FString Revision;
		FString Description;
	};

	FString GetSourceControlFixture(const TCHAR* Name)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
		return Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("TestFixtures"), Name) : FString();
	}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSourceControlCompare_DescribesRevisions, "AssetSerializationInspector.Compare.AssetSourceControlCompare.DescribesRevisions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSourceControlCompare_DescribesRevisions::RunTest(const FString& Parameters)
{
	const TSharedRef<FFakeRevision, ESPMode::ThreadSafe> Newer = MakeShared<FFakeRevision, ESPMode::ThreadSafe>(FString(), TEXT("1234"), TEXT("Fix the door\n\nThe long explanation follows."));
	const TSharedRef<FFakeRevision, ESPMode::ThreadSafe> Older = MakeShared<FFakeRevision, ESPMode::ThreadSafe>(FString(), FString(), TEXT("  First version  "));

	const TArray<FAssetRevisionInfo> Infos = AssetSourceControlCompare::DescribeAll({ Newer, nullptr, Older });
	if (!TestEqual(TEXT("A missing revision is skipped"), Infos.Num(), 2))
	{
		return false;
	}

	TestEqual(TEXT("The provider's revision name is used"), Infos[0].Revision, FString(TEXT("1234")));
	TestEqual(TEXT("Only the first line of the description is kept"), Infos[0].Summary, FString(TEXT("Fix the door")));
	TestEqual(TEXT("The position in the history is kept"), Infos[1].Index, 2);
	TestEqual(TEXT("A revision without a name uses its number"), Infos[1].Revision, FString(TEXT("#7")));
	TestEqual(TEXT("The description is trimmed"), Infos[1].Summary, FString(TEXT("First version")));
	TestEqual(TEXT("The label has revision, user, date and summary"), Infos[0].ToLabel(), FString(TEXT("1234  ana  2026-10-01 14:07  Fix the door")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSourceControlCompare_ComparesADownloadedRevision, "AssetSerializationInspector.Compare.AssetSourceControlCompare.ComparesADownloadedRevision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSourceControlCompare_ComparesADownloadedRevision::RunTest(const FString& Parameters)
{
	// The revision is one package and the file on disk another, so there is something to explain.
	const TSharedRef<FFakeRevision, ESPMode::ThreadSafe> Revision =
		MakeShared<FFakeRevision, ESPMode::ThreadSafe>(GetSourceControlFixture(TEXT("BP_BOX50.uasset")), TEXT("#12 (head)"), TEXT("Old box"));
	const FString OnDisk = GetSourceControlFixture(TEXT("BP_Box1.uasset"));

	FString RevisionFile;
	FString Error;
	if (!TestTrue(TEXT("The revision is downloaded"), AssetSourceControlCompare::DownloadRevision(*Revision, OnDisk, RevisionFile, Error)))
	{
		return false;
	}

	TestEqual(TEXT("The download keeps the package extension"), FPaths::GetExtension(RevisionFile), FString(TEXT("uasset")));
	TestTrue(TEXT("The name has no characters a file cannot have"), !FPaths::GetCleanFilename(RevisionFile).Contains(TEXT("#")) && !FPaths::GetCleanFilename(RevisionFile).Contains(TEXT(" ")));

	FText ReadError;
	const TSharedPtr<FAssetSerializationDiffSession> Session = FAssetSerializationDiffSession::FromFiles(RevisionFile, OnDisk, TEXT("BP_Box1"), ReadError);
	if (TestTrue(TEXT("The two files are compared"), Session.IsValid()) && TestTrue(TEXT("There is a diff"), Session->DiffResult.IsSet() && Session->Analysis.IsSet()))
	{
		TestFalse(TEXT("The files differ"), Session->DiffResult->bFilesIdentical);
		TestEqual(TEXT("The session names the asset"), Session->PackageName, FName(TEXT("BP_Box1")));
	}

	// A revision of the file itself is identical to it.
	const TSharedRef<FFakeRevision, ESPMode::ThreadSafe> Same = MakeShared<FFakeRevision, ESPMode::ThreadSafe>(OnDisk, TEXT("5"), TEXT("Same"));
	FString SameFile;
	if (TestTrue(TEXT("The same revision is downloaded"), AssetSourceControlCompare::DownloadRevision(*Same, OnDisk, SameFile, Error)))
	{
		const TSharedPtr<FAssetSerializationDiffSession> SameSession = FAssetSerializationDiffSession::FromFiles(SameFile, OnDisk, TEXT("BP_Box1"), ReadError);
		if (TestTrue(TEXT("It is compared"), SameSession.IsValid()))
		{
			TestTrue(TEXT("Nothing differs"), SameSession->DiffResult->bFilesIdentical);
		}
		IFileManager::Get().Delete(*SameFile);
	}

	// A provider that cannot deliver the revision is reported, not ignored.
	Revision->bFailToGet = true;
	FString Missing;
	TestFalse(TEXT("A failed download is reported"), AssetSourceControlCompare::DownloadRevision(*Revision, OnDisk, Missing, Error));
	TestTrue(TEXT("The reason names the file"), Error.Contains(TEXT("BP_Box1")));

	// Files that are not packages are refused with a reason.
	FText BadError;
	TestFalse(TEXT("A missing file gives no session"), FAssetSerializationDiffSession::FromFiles(TEXT("Z:/NoSuchFolder/Missing.uasset"), OnDisk, TEXT("X"), BadError).IsValid());
	TestFalse(TEXT("And says why"), BadError.IsEmpty());

	IFileManager::Get().Delete(*RevisionFile);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
