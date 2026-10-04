// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "Commandlets/AssetInspectorCommandlet.h"

namespace
{
	FString GetCommandletFixture(const TCHAR* Name)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
		return Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("TestFixtures"), Name) : FString();
	}

	FString GetCommandletScratchFolder(const TCHAR* Name)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("AssetSerializationInspector"), TEXT("Commandlet"), Name));
	}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetInspectorCommandlet_RejectsBadArguments, "AssetSerializationInspector.Commandlet.RejectsBadArguments", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetInspectorCommandlet_RejectsBadArguments::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("Unknown or missing -Mode"), EAutomationExpectedErrorFlags::Contains, 2);
	TestEqual(TEXT("No mode is an error"), AssetInspectorCommandlet::Execute(TEXT("")), AssetInspectorCommandlet::ExitError);
	TestEqual(TEXT("An unknown mode is an error"), AssetInspectorCommandlet::Execute(TEXT("-Mode=Frobnicate")), AssetInspectorCommandlet::ExitError);

	AddExpectedError(TEXT("needs -Path"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("A resave without a path is an error"), AssetInspectorCommandlet::Execute(TEXT("-Mode=NoOpResave")), AssetInspectorCommandlet::ExitError);

	AddExpectedError(TEXT("needs -Old"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("A comparison without folders is an error"), AssetInspectorCommandlet::Execute(TEXT("-Mode=CompareFolders -Old=Somewhere")), AssetInspectorCommandlet::ExitError);

	AddExpectedError(TEXT("The folder does not exist"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(
		TEXT("A missing folder is an error"), AssetInspectorCommandlet::Execute(TEXT("-Mode=CompareFolders -Old=Z:/NoSuchFolder -New=Z:/NoSuchFolderEither")), AssetInspectorCommandlet::ExitError);

	AddExpectedError(TEXT("needs -Folder"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("A coverage run without a folder is an error"), AssetInspectorCommandlet::Execute(TEXT("-Mode=DecodeCoverage")), AssetInspectorCommandlet::ExitError);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetInspectorCommandlet_ComparesFoldersAndSignalsChanges, "AssetSerializationInspector.Commandlet.ComparesFoldersAndSignalsChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetInspectorCommandlet_ComparesFoldersAndSignalsChanges::RunTest(const FString& Parameters)
{
	const FString Root = GetCommandletScratchFolder(TEXT("Compare"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	const FString OldFolder = FPaths::Combine(Root, TEXT("Old"));
	const FString NewFolder = FPaths::Combine(Root, TEXT("New"));
	const FString Report = FPaths::Combine(Root, TEXT("Reports"), TEXT("Compare.json"));

	IFileManager::Get().Copy(*FPaths::Combine(OldFolder, TEXT("BP_Box1.uasset")), *GetCommandletFixture(TEXT("BP_Box1.uasset")));
	IFileManager::Get().Copy(*FPaths::Combine(NewFolder, TEXT("BP_Box1.uasset")), *GetCommandletFixture(TEXT("BP_Box1.uasset")));

	const FString Arguments = FString::Printf(TEXT("-Mode=CompareFolders -Old=\"%s\" -New=\"%s\" -Report=\"%s\" -FailOnChanges"), *OldFolder, *NewFolder, *Report);
	TestEqual(TEXT("Identical folders pass"), AssetInspectorCommandlet::Execute(Arguments), AssetInspectorCommandlet::ExitOk);
	TestTrue(TEXT("The report is written, in a folder that did not exist"), IFileManager::Get().FileExists(*Report));

	FString Json;
	FFileHelper::LoadFileToString(Json, *Report);
	TSharedPtr<FJsonObject> Object;
	if (TestTrue(TEXT("The report is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) && Object.IsValid()))
	{
		TestEqual(TEXT("It is a folder comparison"), Object->GetStringField(TEXT("kind")), FString(TEXT("folderComparison")));
	}

	// A file that exists on one side only is a difference.
	IFileManager::Get().Copy(*FPaths::Combine(NewFolder, TEXT("BP_BOX50.uasset")), *GetCommandletFixture(TEXT("BP_BOX50.uasset")));
	TestEqual(TEXT("A difference fails when asked to"), AssetInspectorCommandlet::Execute(Arguments), AssetInspectorCommandlet::ExitFindings);

	const FString WithoutFail = Arguments.Replace(TEXT(" -FailOnChanges"), TEXT(""));
	TestEqual(TEXT("A difference passes when not asked to fail"), AssetInspectorCommandlet::Execute(WithoutFail), AssetInspectorCommandlet::ExitOk);

	// The text format is used for other extensions.
	const FString TextReport = FPaths::Combine(Root, TEXT("Compare.txt"));
	AssetInspectorCommandlet::Execute(WithoutFail.Replace(*Report, *TextReport));
	FString Text;
	FFileHelper::LoadFileToString(Text, *TextReport);
	TestTrue(TEXT("A .txt report is text, not JSON"), !Text.IsEmpty() && !Text.StartsWith(TEXT("{")));

	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetInspectorCommandlet_ReportsDecodeCoverage, "AssetSerializationInspector.Commandlet.ReportsDecodeCoverage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetInspectorCommandlet_ReportsDecodeCoverage::RunTest(const FString& Parameters)
{
	const FString Root = GetCommandletScratchFolder(TEXT("Coverage"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	IFileManager::Get().Copy(*FPaths::Combine(Root, TEXT("Assets"), TEXT("BP_Box1.uasset")), *GetCommandletFixture(TEXT("BP_Box1.uasset")));
	const FString Report = FPaths::Combine(Root, TEXT("Coverage.json"));

	TestEqual(TEXT("A coverage run succeeds"),
		AssetInspectorCommandlet::Execute(FString::Printf(TEXT("-Mode=DecodeCoverage -Folder=\"%s\" -Report=\"%s\""), *FPaths::Combine(Root, TEXT("Assets")), *Report)),
		AssetInspectorCommandlet::ExitOk);
	TestTrue(TEXT("Its report is written"), IFileManager::Get().FileExists(*Report));

	// Relative paths are taken from the project folder, whatever the working directory is.
	const FString RelativeReport = TEXT("Saved/Automation/Tmp/AssetSerializationInspector/Commandlet/Relative.txt");
	TestEqual(TEXT("A relative report path is accepted"),
		AssetInspectorCommandlet::Execute(FString::Printf(TEXT("-Mode=DecodeCoverage -Folder=\"%s\" -Report=\"%s\""), *FPaths::Combine(Root, TEXT("Assets")), *RelativeReport)),
		AssetInspectorCommandlet::ExitOk);
	TestTrue(TEXT("It is written under the project folder"), IFileManager::Get().FileExists(*FPaths::Combine(FPaths::ProjectDir(), RelativeReport)));
	IFileManager::Get().Delete(*FPaths::Combine(FPaths::ProjectDir(), RelativeReport));

	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
