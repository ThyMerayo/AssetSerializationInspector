// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Curves/CurveFloat.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "Coverage/AssetDecoderCoverage.h"

namespace AssetDecoderCoverageTestUtils
{
	static FAssetDecodedPropertyValue MakeValue(const EAssetPropertyDecodeStatus Status, const TCHAR* TypeName, const TCHAR* Error = TEXT(""))
	{
		FAssetDecodedPropertyValue Value;
		Value.Status = Status;
		Value.TypeName = TypeName;
		Value.Error = Error;
		return Value;
	}
} // namespace AssetDecoderCoverageTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetDecoderCoverage_NormalizesAndCollectsFailures, "AssetSerializationInspector.Coverage.AssetDecoderCoverage.NormalizesAndCollectsFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetDecoderCoverage_NormalizesAndCollectsFailures::RunTest(const FString& Parameters)
{
	using namespace AssetDecoderCoverageTestUtils;

	TestEqual(TEXT("Numbers are replaced so similar failures group"), AssetDecoderCoverage::NormalizeMessage(TEXT("Could not decode array element 12 of 340.")), FString(TEXT("Could not decode array element # of #.")));
	TestEqual(TEXT("Text without numbers is unchanged"), AssetDecoderCoverage::NormalizeMessage(TEXT("Unsupported property type: LazyObjectProperty")), FString(TEXT("Unsupported property type: LazyObjectProperty")));

	TArray<FAssetDecoderCoverageFailure> Failures;
	AssetDecoderCoverage::CollectFailures(MakeValue(EAssetPropertyDecodeStatus::Success, TEXT("IntProperty")), Failures);
	TestTrue(TEXT("A decoded value has no failures"), Failures.IsEmpty());

	AssetDecoderCoverage::CollectFailures(MakeValue(EAssetPropertyDecodeStatus::Unsupported, TEXT("LazyObjectProperty"), TEXT("Unsupported property type: LazyObjectProperty")), Failures);
	if (TestEqual(TEXT("A value that did not decode is a failure"), Failures.Num(), 1))
	{
		TestEqual(TEXT("With its type"), Failures[0].TypeName, FString(TEXT("LazyObjectProperty")));
	}

	// A tagged struct succeeds but keeps a field it could not read; the failure is that field, not the struct.
	FAssetDecodedPropertyValue Struct = MakeValue(EAssetPropertyDecodeStatus::Success, TEXT("StructProperty(MyStruct)"));
	Struct.Children.Add(MakeValue(EAssetPropertyDecodeStatus::Success, TEXT("IntProperty")));
	Struct.Children.Add(MakeValue(EAssetPropertyDecodeStatus::Unsupported, TEXT("FieldPathProperty"), TEXT("Unsupported property type: FieldPathProperty")));

	Failures.Reset();
	AssetDecoderCoverage::CollectFailures(Struct, Failures);
	if (TestEqual(TEXT("Only the field is reported"), Failures.Num(), 1))
	{
		TestEqual(TEXT("It is the field"), Failures[0].TypeName, FString(TEXT("FieldPathProperty")));
	}

	// A container that failed because of an element is reported once, at the deepest level that says why.
	FAssetDecodedPropertyValue Array = MakeValue(EAssetPropertyDecodeStatus::InvalidData, TEXT("ArrayProperty(StructProperty(MyStruct))"), TEXT("Could not decode array element 3."));
	Failures.Reset();
	AssetDecoderCoverage::CollectFailures(Array, Failures);
	if (TestEqual(TEXT("A failed container is one failure"), Failures.Num(), 1))
	{
		TestEqual(TEXT("With a normalized message"), Failures[0].Message, FString(TEXT("Could not decode array element #.")));
	}

	Array.Children.Add(MakeValue(EAssetPropertyDecodeStatus::InvalidData, TEXT("StructProperty(MyStruct)"), TEXT("Tag extends beyond the range.")));
	Failures.Reset();
	AssetDecoderCoverage::CollectFailures(Array, Failures);
	if (TestEqual(TEXT("A failed child explains the container"), Failures.Num(), 1))
	{
		TestEqual(TEXT("So the child is reported"), Failures[0].TypeName, FString(TEXT("StructProperty(MyStruct)")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetDecoderCoverage_ScansAFolder, "AssetSerializationInspector.Coverage.AssetDecoderCoverage.ScansAFolder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetDecoderCoverage_ScansAFolder::RunTest(const FString& Parameters)
{
	const FString Root = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("AssetSerializationInspector"), TEXT("Coverage"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);

	const FString PackageName = TEXT("/Game/__AssetSerializationInspectorTests/CoverageCurve");
	UPackage* Package = CreatePackage(*PackageName);
	UCurveFloat* Curve = NewObject<UCurveFloat>(Package, TEXT("CoverageCurve"), RF_Public | RF_Standalone);
	Curve->FloatCurve.AddKey(0.0f, 1.0f);
	Package->MarkAsFullyLoaded();

	const FString File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	SaveArgs.bSlowTask = false;

	const bool bSaved = UPackage::SavePackage(Package, Curve, *File, SaveArgs);
	TestTrue(TEXT("The test asset is written"), bSaved);

	if (bSaved)
	{
		IFileManager::Get().Copy(*FPaths::Combine(Root, TEXT("Sub"), TEXT("Curve.uasset")), *File);

		// A file that is not a package.
		// The engine logs a warning when it opens it.
		AddExpectedMessage(TEXT("is too small"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
		FFileHelper::SaveStringToFile(TEXT("not a package"), *FPaths::Combine(Root, TEXT("Broken.uasset")));

		int32 Calls = 0;
		const FAssetDecoderCoverageResult Result = AssetDecoderCoverage::Run(Root, [&Calls](int32, int32, const FString&) {
			++Calls;
			return true;
		});

		TestEqual(TEXT("Every file is considered"), Calls, 2);
		TestFalse(TEXT("Not cancelled"), Result.bCancelled);
		TestEqual(TEXT("The package was scanned"), Result.AssetsScanned, 1);
		TestEqual(TEXT("The other file is unreadable"), Result.AssetsUnreadable, 1);
		TestEqual(TEXT("Its reason is recorded"), Result.UnreadableReasons.Num(), 1);
		TestTrue(TEXT("The export was scanned"), Result.ExportsScanned > 0);
		TestTrue(TEXT("Property sizes are consistent"), Result.PropertyBytesUndecoded <= Result.PropertyBytes);
		TestEqual(TEXT("A plain curve decodes completely"), Result.PropertiesDecoded, Result.PropertiesScanned);
		TestTrue(TEXT("So there is nothing to report"), Result.Issues.IsEmpty());

		const FString Text = AssetDecoderCoverage::ToText(Result);
		TestTrue(TEXT("The text report counts the assets"), Text.Contains(TEXT("1 assets scanned, 1 unreadable")));
		TestTrue(TEXT("It lists unreadable files"), Text.Contains(TEXT("Unreadable files")));

		TSharedPtr<FJsonObject> Root0;
		if (TestTrue(TEXT("The JSON report is valid"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(AssetDecoderCoverage::ToJson(Result)), Root0) && Root0.IsValid()))
		{
			TestEqual(TEXT("Its kind"), Root0->GetStringField(TEXT("kind")), FString(TEXT("decoderCoverage")));
			TestEqual(TEXT("Its totals"), static_cast<int32>(Root0->GetObjectField(TEXT("totals"))->GetNumberField(TEXT("assetsScanned"))), 1);
		}

		const FString Report = FPaths::Combine(Root, TEXT("report.json"));
		FText Error;
		TestTrue(TEXT("The report is saved"), AssetDecoderCoverage::SaveToFile(Result, Report, Error));
		TestTrue(TEXT("It exists"), IFileManager::Get().FileExists(*Report));

		const FAssetDecoderCoverageResult Cancelled = AssetDecoderCoverage::Run(Root, [](const int32 Index, int32, const FString&) { return Index < 1; });
		TestTrue(TEXT("A scan can be cancelled"), Cancelled.bCancelled);
	}

	Curve->ClearFlags(RF_Public | RF_Standalone);
	Package->SetDirtyFlag(false);
	IFileManager::Get().Delete(*File, false, true, true);
	IFileManager::Get().DeleteDirectory(*Root, false, true);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
