// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "UObject/ObjectVersion.h"

#include "Compare/AssetEngineVersionInference.h"
#include "Compare/AssetFolderComparison.h"
#include "Model/AssetPackageDocument.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetEngineVersionInference_NamesTheReleases, "AssetSerializationInspector.Compare.AssetEngineVersionInference.NamesTheReleases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetEngineVersionInference_NamesTheReleases::RunTest(const FString& Parameters)
{
	using namespace AssetEngineVersionInference;

	TestEqual(TEXT("A UE5 file version names the releases that had it"), Infer(522, 1012), FString(TEXT("5.4.0 to 5.4.4 (inferred)")));
	TestEqual(TEXT("Several minor releases can share one"), Infer(522, 1009), FString(TEXT("5.2.0 to 5.3.2 (inferred)")));
	TestEqual(TEXT("The version of UE 5.0.3, the fixture's"), Infer(522, 1004), FString(TEXT("5.0.0 to 5.0.3 (inferred)")));
	TestEqual(TEXT("UE4 packages are told apart by their UE4 version"), Infer(516, 0), FString(TEXT("4.19.0 to 4.20.3 (inferred)")));
	TestEqual(TEXT("The last UE4 releases"), Infer(522, 0), FString(TEXT("4.26.0 to 4.27.2 (inferred)")));

	// A version no release had belongs to a build between two releases, or to none we know.
	TestEqual(TEXT("A build between two releases"), Infer(522, 1010), FString(TEXT("between 5.3.2 and 5.4.0 (inferred)")));
	TestEqual(TEXT("A version after the last release"), Infer(522, 5000), FString(TEXT("newer than 5.8.3 (inferred)")));
	TestEqual(TEXT("A version before the first"), Infer(300, 0), FString(TEXT("older than 4.0.1 (inferred)")));

	// The table follows the file versions of the engine this plugin is built for, and is in order.
	const TConstArrayView<FRelease> Releases = GetReleases();
	TestEqual(TEXT("The newest release has this engine's UE5 file version"), Releases.Last().FileVersionUE5, static_cast<int32>(EUnrealEngineObjectUE5Version::AUTOMATIC_VERSION));
	TestEqual(TEXT("And its UE4 file version"), Releases.Last().FileVersionUE4, static_cast<int32>(EUnrealEngineObjectUE4Version::VER_UE4_AUTOMATIC_VERSION));

	bool bInOrder = true;
	for (int32 Index = 1; Index < Releases.Num(); ++Index)
	{
		const bool bUE4Grows = Releases[Index].FileVersionUE4 > Releases[Index - 1].FileVersionUE4;
		const bool bUE5Grows = Releases[Index].FileVersionUE5 > Releases[Index - 1].FileVersionUE5;
		bInOrder &= bUE4Grows || (Releases[Index].FileVersionUE4 == Releases[Index - 1].FileVersionUE4 && bUE5Grows);
	}
	TestTrue(TEXT("Each row is newer than the one before"), bInOrder);

	// What the package says wins over what its file version suggests.
	FAssetPackageDocument Named;
	Named.PackageSummary.SetFileVersions(522, 1012, 0);
	Named.PackageSummary.SavedByEngineVersion.Set(5, 4, 2, 1000, TEXT("++UE5+Release-5.4"));
	TestEqual(TEXT("A named engine version is used"), AssetFolderComparison::DescribeEngineVersionOrInferred(Named), FString(TEXT("5.4.2")));

	FAssetPackageDocument Unnamed;
	Unnamed.PackageSummary.SetFileVersions(522, 1012, 0);
	TestEqual(TEXT("An unnamed package is placed by its file version"), AssetFolderComparison::DescribeEngineVersionOrInferred(Unnamed), FString(TEXT("5.4.0 to 5.4.4 (inferred)")));
	TestEqual(TEXT("The plain description still has no version for it"), AssetFolderComparison::DescribeEngineVersion(Unnamed), FString());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
