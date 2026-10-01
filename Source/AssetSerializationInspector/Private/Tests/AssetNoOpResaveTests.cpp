// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Curves/CurveFloat.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "Save/AssetNoOpResaveTest.h"
#include "Save/AssetSaveObserver.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetNoOpResave_ClassifiesVerdicts, "AssetSerializationInspector.Save.AssetNoOpResaveTest.ClassifiesVerdicts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetNoOpResave_ClassifiesVerdicts::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Both resaves identical is stable"), AssetNoOpResaveTest::ClassifyVerdict(true, true), ENoOpResaveVerdict::Stable);
	TestEqual(TEXT("A changed first resave that settles is normalization"), AssetNoOpResaveTest::ClassifyVerdict(false, true), ENoOpResaveVerdict::NormalizedOnFirstSave);
	TestEqual(TEXT("Changes on both resaves are unstable"), AssetNoOpResaveTest::ClassifyVerdict(false, false), ENoOpResaveVerdict::Unstable);
	TestEqual(TEXT("A second resave that changes after an identical first one is unstable"), AssetNoOpResaveTest::ClassifyVerdict(true, false), ENoOpResaveVerdict::Unstable);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetNoOpResave_ResavesARealAsset, "AssetSerializationInspector.Save.AssetNoOpResaveTest.ResavesARealAsset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetNoOpResave_ResavesARealAsset::RunTest(const FString& Parameters)
{
	const FString PackageName = TEXT("/Game/__AssetSerializationInspectorTests/NoOpResaveCurve");
	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

	UPackage* Package = CreatePackage(*PackageName);
	UCurveFloat* Curve = NewObject<UCurveFloat>(Package, TEXT("NoOpResaveCurve"), RF_Public | RF_Standalone);
	Curve->FloatCurve.AddKey(0.0f, 1.0f);
	Curve->FloatCurve.AddKey(1.0f, 2.0f);
	Package->MarkAsFullyLoaded();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	SaveArgs.bSlowTask = false;

	const bool bSaved = UPackage::SavePackage(Package, Curve, *Filename, SaveArgs);
	TestTrue(TEXT("The test asset is written to disk"), bSaved);

	if (bSaved)
	{
		const int64 OriginalSize = IFileManager::Get().FileSize(*Filename);
		const FDateTime OriginalTime = IFileManager::Get().GetTimeStamp(*Filename);

		const FGuid OriginalPersistentGuid = Package->GetPersistentGuid();

		const FNoOpResaveResult Result = AssetNoOpResaveTest::Run(Package);

		TestTrue(*FString::Printf(TEXT("The test runs: %s"), *Result.Error.ToString()), Result.bSucceeded);
		TestFalse(TEXT("The original file is never written"), Result.bOriginalFileModified);
		TestEqual(TEXT("The original file keeps its size"), IFileManager::Get().FileSize(*Filename), OriginalSize);
		TestEqual(TEXT("The original file keeps its timestamp"), IFileManager::Get().GetTimeStamp(*Filename), OriginalTime);
		TestFalse(TEXT("The package is left clean"), Package->IsDirty());
		TestEqual(TEXT("The package keeps its persistent GUID"), Package->GetPersistentGuid(), OriginalPersistentGuid);

		if (Result.bSucceeded)
		{
			TestTrue(TEXT("The first resave is compared with the original"), Result.FirstResave.IsValid() && Result.FirstResave->Before.IsValid() && Result.FirstResave->After.IsValid());
			TestTrue(TEXT("The second resave is compared with the first"), Result.SecondResave.IsValid() && Result.SecondResave->After.IsValid());
			TestEqual(TEXT("A freshly saved asset resaves identically"), Result.Verdict, ENoOpResaveVerdict::Stable);
		}

		const FNoOpResaveResult Second = AssetNoOpResaveTest::Run(Package);
		TestEqual(TEXT("Running the test again gives the same verdict"), Second.Verdict, Result.Verdict);

		Package->SetDirtyFlag(true);
		const FNoOpResaveResult Dirty = AssetNoOpResaveTest::Run(Package);
		TestFalse(TEXT("A package with unsaved changes is refused"), Dirty.bSucceeded);
		TestFalse(TEXT("The refusal explains why"), Dirty.Error.IsEmpty());
		Package->SetDirtyFlag(false);
	}

	TestFalse(TEXT("A missing package is refused"), AssetNoOpResaveTest::Run(nullptr).bSucceeded);

	// Remove the test asset again.
	Curve->ClearFlags(RF_Public | RF_Standalone);
	Package->SetDirtyFlag(false);
	IFileManager::Get().Delete(*Filename, false, true, true);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
