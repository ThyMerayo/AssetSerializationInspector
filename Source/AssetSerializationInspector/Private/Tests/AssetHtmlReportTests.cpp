// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Compare/AssetFolderComparison.h"
#include "Report/AssetAnalysisReport.h"
#include "Report/AssetBatchReportWriter.h"
#include "Report/AssetFolderComparisonReportWriter.h"
#include "Report/AssetHtmlReport.h"
#include "Report/AssetReportWriter.h"
#include "Save/AssetBatchResave.h"
#include "Widgets/SAssetSerializationDiff.h"

namespace
{
	FString GetHtmlFixture(const TCHAR* Name)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
		return Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("TestFixtures"), Name) : FString();
	}

	int32 CountOf(const FString& Text, const TCHAR* Needle)
	{
		int32 Count = 0;
		int32 From = 0;
		while ((From = Text.Find(Needle, ESearchCase::CaseSensitive, ESearchDir::FromStart, From)) != INDEX_NONE)
		{
			++Count;
			From += FCString::Strlen(Needle);
		}
		return Count;
	}

	/** The page is one document that opens and closes the folding blocks evenly, and never carries markup from the data. */
	void CheckPage(FAutomationTestBase& Test, const FString& Html, const TCHAR* Name)
	{
		Test.TestTrue(FString::Printf(TEXT("%s is a whole page"), Name), Html.StartsWith(TEXT("<!DOCTYPE html>")) && Html.Contains(TEXT("</html>")));
		Test.TestEqual(FString::Printf(TEXT("%s closes every folding block it opens"), Name), CountOf(Html, TEXT("<details")), CountOf(Html, TEXT("</details>")));
		Test.TestFalse(FString::Printf(TEXT("%s has no script"), Name), Html.Contains(TEXT("<script")));
	}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetHtmlReport_EscapesAndPicksTheFormat, "AssetSerializationInspector.Report.AssetHtmlReport.EscapesAndPicksTheFormat", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetHtmlReport_EscapesAndPicksTheFormat::RunTest(const FString& Parameters)
{
	TestEqual(
		TEXT("Markup characters are escaped"), AssetHtmlReport::Escape(TEXT("<a href=\"x\">Tom & 'Jerry'</a>")), FString(TEXT("&lt;a href=&quot;x&quot;&gt;Tom &amp; &#39;Jerry&#39;&lt;/a&gt;")));
	TestEqual(TEXT("Plain text is unchanged"), AssetHtmlReport::Escape(TEXT("Static Mesh 1.5")), FString(TEXT("Static Mesh 1.5")));

	TestEqual(TEXT(".html is HTML"), AssetReportWriter::GetFormatForFilename(TEXT("Report.html")), EAssetReportFormat::Html);
	TestEqual(TEXT(".HTM is HTML"), AssetReportWriter::GetFormatForFilename(TEXT("Report.HTM")), EAssetReportFormat::Html);
	TestEqual(TEXT(".json is JSON"), AssetReportWriter::GetFormatForFilename(TEXT("Report.json")), EAssetReportFormat::Json);
	TestEqual(TEXT("Anything else is text"), AssetReportWriter::GetFormatForFilename(TEXT("Report.log")), EAssetReportFormat::Text);
	TestEqual(TEXT("The extension of HTML"), FString(AssetReportWriter::GetFileExtension(EAssetReportFormat::Html)), FString(TEXT("html")));
	TestTrue(TEXT("A default name uses it"),
		AssetReportWriter::MakeDefaultFilename(TEXT("Hero.uasset"), FDateTime(2026, 10, 1, 14, 7, 43), EAssetReportFormat::Html).EndsWith(TEXT("_20261001-140743.html")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetHtmlReport_WritesAnAssetReport, "AssetSerializationInspector.Report.AssetHtmlReport.WritesAnAssetReport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetHtmlReport_WritesAnAssetReport::RunTest(const FString& Parameters)
{
	// A value that tries to inject markup stays text.
	FAssetAnalysisReport Report;
	Report.OldFilename = TEXT("C:/Old/<b>Hero</b>.uasset");
	Report.NewFilename = TEXT("C:/New/Hero.uasset");
	Report.GeneratedAtUtc = FDateTime(2026, 10, 1);

	FAssetPackageDiffEntry Parent;
	Parent.Kind = EAssetPackageDiffKind::Property;
	Parent.State = EAssetPackageDiffState::Modified;
	Parent.DisplayName = FText::FromString(TEXT("Label"));
	Parent.TypeName = TEXT("TextProperty");
	Parent.bHasOldDecodedValue = true;
	Parent.OldDecodedValue = TEXT("<script>alert(1)</script>");
	Parent.bHasNewDecodedValue = true;
	Parent.NewDecodedValue = TEXT("two\nlines");
	Parent.Explanation = FText::FromString(TEXT("because <i>reasons</i>"));

	FAssetPackageDiffEntry Child = Parent;
	Child.DisplayName = FText::FromString(TEXT("Inner"));
	Parent.Children.Add(Child);
	Report.Differences.Add(Parent);
	Report.Summary = AssetAnalysisReport::Summarize(Report.Differences);

	const FString Html = AssetReportWriter::ToHtml(Report);
	CheckPage(*this, Html, TEXT("The asset report"));
	TestTrue(TEXT("The value is escaped"), Html.Contains(TEXT("&lt;script&gt;alert(1)&lt;/script&gt;")));
	TestTrue(TEXT("So is the file name"), Html.Contains(TEXT("&lt;b&gt;Hero&lt;/b&gt;")));
	TestTrue(TEXT("So is the explanation"), Html.Contains(TEXT("because &lt;i&gt;reasons&lt;/i&gt;")));
	TestTrue(TEXT("The state is shown"), Html.Contains(TEXT("chip modified")));
	TestTrue(TEXT("Line breaks of a value are kept"), Html.Contains(TEXT("two\nlines")));
	TestEqual(TEXT("Write picks the page for HTML"), AssetReportWriter::Write(Report, EAssetReportFormat::Html), Html);

	// A real comparison of two packages.
	FText Error;
	const TSharedPtr<FAssetSerializationDiffSession> Session =
		FAssetSerializationDiffSession::FromFiles(GetHtmlFixture(TEXT("BP_BOX50.uasset")), GetHtmlFixture(TEXT("BP_Box1.uasset")), TEXT("BP_Box1"), Error);
	if (TestTrue(TEXT("The fixtures are compared"), Session.IsValid()))
	{
		const FAssetAnalysisReport Real = AssetAnalysisReport::Build(Session->DiffResult.GetValue(), Session->Analysis.GetPtrOrNull(), {}, nullptr);
		const FString RealHtml = AssetReportWriter::ToHtml(Real);
		CheckPage(*this, RealHtml, TEXT("The report of two packages"));
		TestTrue(TEXT("It has the save analysis"), RealHtml.Contains(TEXT("Save analysis")));
		TestTrue(TEXT("It has differences"), RealHtml.Contains(TEXT("class=\"chip ")));
	}

	// Saving by extension.
	const FString File = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("AssetSerializationInspector"), TEXT("Html"), TEXT("Report.html")));
	FText SaveError;
	if (TestTrue(TEXT("The report is saved"), AssetReportWriter::SaveToFile(Report, File, SaveError)))
	{
		FString Saved;
		FFileHelper::LoadFileToString(Saved, *File);
		TestEqual(TEXT("A .html file holds the page"), Saved, Html);
	}
	IFileManager::Get().DeleteDirectory(*FPaths::GetPath(File), false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetHtmlReport_WritesBatchAndFolderReports, "AssetSerializationInspector.Report.AssetHtmlReport.WritesBatchAndFolderReports",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetHtmlReport_WritesBatchAndFolderReports::RunTest(const FString& Parameters)
{
	// A batch with one asset of each kind.
	FAssetBatchResaveResult Batch;
	Batch.Scope = TEXT("/Game/<Characters>");
	Batch.StartedAt = FDateTime(2026, 10, 1);
	Batch.FinishedAt = FDateTime(2026, 10, 1, 0, 5);

	FAssetBatchResaveEntry Unstable;
	Unstable.PackageName = TEXT("/Game/Hero");
	Unstable.Status = EAssetBatchResaveStatus::Tested;
	Unstable.Verdict = ENoOpResaveVerdict::Unstable;
	Unstable.FirstResaveChangedBytes = 12;
	Unstable.FirstResaveChanges.Add({ TEXT("PropertyValueChanged"), TEXT("Speed"), TEXT("1 => 2 <b>") });
	Unstable.SecondResaveChanges.Add({ TEXT("HeaderRegion"), TEXT("Thumbnails"), FString() });
	Batch.Entries.Add(Unstable);

	FAssetBatchResaveEntry Stable;
	Stable.PackageName = TEXT("/Game/Rock");
	Stable.Status = EAssetBatchResaveStatus::Tested;
	Stable.Verdict = ENoOpResaveVerdict::Stable;
	Batch.Entries.Add(Stable);

	FAssetBatchResaveEntry Failed;
	Failed.PackageName = TEXT("/Game/Broken");
	Failed.Status = EAssetBatchResaveStatus::Failed;
	Failed.Message = TEXT("Could not <load>");
	Batch.Entries.Add(Failed);

	const FString BatchHtml = AssetBatchReportWriter::ToHtml(Batch);
	CheckPage(*this, BatchHtml, TEXT("The batch report"));
	TestTrue(TEXT("The scope is escaped"), BatchHtml.Contains(TEXT("/Game/&lt;Characters&gt;")));
	TestTrue(TEXT("The unstable asset is listed with its change"), BatchHtml.Contains(TEXT("/Game/Hero")) && BatchHtml.Contains(TEXT("Speed")) && BatchHtml.Contains(TEXT("1 =&gt; 2 &lt;b&gt;")));
	TestTrue(TEXT("The failure message is escaped"), BatchHtml.Contains(TEXT("Could not &lt;load&gt;")));
	TestTrue(TEXT("The groups are named"), BatchHtml.Contains(TEXT("Unstable: resaving keeps changing the file")) && BatchHtml.Contains(TEXT("Stable (1)")));
	TestEqual(TEXT("Write picks the page for HTML"), AssetBatchReportWriter::Write(Batch, EAssetReportFormat::Html), BatchHtml);

	// A folder comparison with a changed and a one-sided file.
	FAssetFolderComparisonResult Folders;
	Folders.OldFolder = TEXT("D:/Before");
	Folders.NewFolder = TEXT("D:/After");

	FAssetFolderComparisonEntry Changed;
	Changed.RelativePath = TEXT("Chars/Hero.uasset");
	Changed.Status = EAssetFolderComparisonStatus::Changed;
	Changed.bVersionsDiffer = true;
	Changed.OldEngineVersion = TEXT("5.5.1");
	Changed.NewEngineVersion = TEXT("5.8.2");
	Changed.OldFileVersion = TEXT("UE4 522 / UE5 1012");
	Changed.NewFileVersion = TEXT("UE4 522 / UE5 1018");
	Changed.OldFileSize = 100;
	Changed.NewFileSize = 120;
	Changed.Changes.Add({ TEXT("HeaderRegion"), TEXT("Names"), TEXT("moved") });
	Folders.Entries.Add(Changed);

	FAssetFolderComparisonEntry Added;
	Added.RelativePath = TEXT("Chars/New.uasset");
	Added.Status = EAssetFolderComparisonStatus::OnlyInNewFolder;
	Folders.Entries.Add(Added);

	FAssetFolderComparisonEntry Same;
	Same.RelativePath = TEXT("Chars/Same.uasset");
	Same.Status = EAssetFolderComparisonStatus::Identical;
	Folders.Entries.Add(Same);

	const FString FolderHtml = AssetFolderComparisonReportWriter::ToHtml(Folders);
	CheckPage(*this, FolderHtml, TEXT("The folder report"));
	TestTrue(TEXT("The changed file is listed with its versions"), FolderHtml.Contains(TEXT("Chars/Hero.uasset")) && FolderHtml.Contains(TEXT("5.5.1 (UE4 522 / UE5 1012)")));
	TestTrue(TEXT("The one-sided file is listed"), FolderHtml.Contains(TEXT("Chars/New.uasset")));
	TestFalse(TEXT("An identical file is only counted"), FolderHtml.Contains(TEXT("Chars/Same.uasset")));
	TestTrue(TEXT("The counts are shown"), FolderHtml.Contains(TEXT("1 identical")));
	TestEqual(TEXT("Write picks the page for HTML"), AssetFolderComparisonReportWriter::Write(Folders, EAssetReportFormat::Html), FolderHtml);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
