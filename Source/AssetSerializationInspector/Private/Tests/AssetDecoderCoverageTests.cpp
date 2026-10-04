// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Curves/CurveFloat.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "Coverage/AssetDecoderCoverage.h"
#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Trace/AssetPackageFieldDecoder.h"
#include "Trace/AssetSerializationTrace.h"

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

	TestEqual(TEXT("Numbers are replaced so similar failures group"), AssetDecoderCoverage::NormalizeMessage(TEXT("Could not decode array element 12 of 340.")),
		FString(TEXT("Could not decode array element # of #.")));
	TestEqual(TEXT("Text without numbers is unchanged"), AssetDecoderCoverage::NormalizeMessage(TEXT("Unsupported property type: LazyObjectProperty")),
		FString(TEXT("Unsupported property type: LazyObjectProperty")));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDecoderCoverage_ScansAFolder, "AssetSerializationInspector.Coverage.AssetDecoderCoverage.ScansAFolder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetDecoderCoverage_ReadsAPackageFromUE50, "AssetSerializationInspector.Coverage.AssetDecoderCoverage.ReadsAPackageFromUE50",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetDecoderCoverage_ReadsAPackageFromUE50::RunTest(const FString& Parameters)
{
	// BP_BOX50 is a small Blueprint migrated from Unreal 5.0 (file version 1004, tags without complete type names, no script
	// serialization offsets, no soft object path table). It is the fixture for the pre-5.4 package layouts.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
	if (!TestTrue(TEXT("The plugin is found"), Plugin.IsValid()))
	{
		return false;
	}

	const FString Folder = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("TestFixtures"));
	const FString File = FPaths::Combine(Folder, TEXT("BP_BOX50.uasset"));
	if (!TestTrue(TEXT("The fixture exists"), IFileManager::Get().FileExists(*File)))
	{
		return false;
	}

	FText Error;
	const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(File, Error);
	if (!TestTrue(TEXT("The package loads"), Document.IsValid()))
	{
		return false;
	}

	TestEqual(TEXT("It was saved by UE 5.0"), Document->PackageSummary.GetFileVersionUE().FileVersionUE5, 1004);
	TestTrue(TEXT("Its name map decodes"), Document->NameMapError.IsEmpty());
	TestTrue(TEXT("Its import map decodes"), Document->ImportMapError.IsEmpty());
	TestTrue(TEXT("Its export map decodes"), Document->ExportMapError.IsEmpty());
	TestEqual(TEXT("All its exports are read"), Document->ExportMap.Num(), 16);

	// The fixtures folder holds several packages; scan this one alone.
	const FString ScanFolder = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("AssetSerializationInspector"), TEXT("UE50Fixture"));
	IFileManager::Get().DeleteDirectory(*ScanFolder, false, true);
	IFileManager::Get().Copy(*FPaths::Combine(ScanFolder, TEXT("BP_BOX50.uasset")), *File);

	const FAssetDecoderCoverageResult Result = AssetDecoderCoverage::Run(ScanFolder, [](int32, int32, const FString&) { return true; });
	IFileManager::Get().DeleteDirectory(*ScanFolder, false, true);
	TestEqual(TEXT("It is scanned"), Result.AssetsScanned, 1);
	TestEqual(TEXT("Nothing is unreadable"), Result.AssetsUnreadable, 0);
	TestEqual(TEXT("Every export has a property stream"), Result.ExportsScanned, 16);
	TestEqual(TEXT("Its tagged properties are found"), Result.PropertiesScanned, 73);

	// The one property left is a Map<Name, Guid>: packages before UE 5.4 do not store the struct type of a map value.
	TestEqual(TEXT("All but one decode"), Result.PropertiesDecoded, 72);
	if (TestEqual(TEXT("One kind of failure"), Result.Issues.Num(), 1))
	{
		TestTrue(TEXT("It is the map"), Result.Issues[0].TypeName.StartsWith(TEXT("MapProperty")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetDecoderCoverage_ReadsAnArrayOfTextsFromAnEditorPackage, "AssetSerializationInspector.Coverage.AssetDecoderCoverage.ReadsAnArrayOfTextsFromAnEditorPackage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetDecoderCoverage_ReadsAnArrayOfTextsFromAnEditorPackage::RunTest(const FString& Parameters)
{
	// BP_Box1 has NewVar_11, an array of three texts: the first and third have values, the second is the default (empty).
	// Editor packages store developer notes after the source string of a text, which keeps the next element aligned.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
	if (!TestTrue(TEXT("The plugin is found"), Plugin.IsValid()))
	{
		return false;
	}

	FText Error;
	const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("TestFixtures"), TEXT("BP_Box1.uasset")), Error);
	if (!TestTrue(TEXT("The fixture loads"), Document.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);

	for (const FAssetPackageExportEntry& Export : Document->ExportMap)
	{
		const FAssetSerializationTrace* Trace = Traces->FindExportTrace(Export.Index);
		if (Trace == nullptr || !Trace->Root.IsValid())
		{
			continue;
		}

		for (const TSharedPtr<FAssetSerializationTraceNode>& Node : Trace->Root->Children)
		{
			if (!Node.IsValid() || Node->Name != TEXT("NewVar_11"))
			{
				continue;
			}

			const FAssetDecodedPropertyValue Texts = FAssetPropertyValueDecoder::Decode(*Document, *Node, Export.SerialOffset);
			TestTrue(TEXT("The array of texts decodes"), Texts.IsSuccess());
			if (TestEqual(TEXT("It has three elements"), Texts.Children.Num(), 3))
			{
				TestEqual(TEXT("The first has its value"), Texts.Children[0].Value, FString(TEXT("test111")));
				TestEqual(TEXT("The second is the default, an empty text"), Texts.Children[1].Value, FString());
				TestEqual(TEXT("The third has its value"), Texts.Children[2].Value, FString(TEXT("Other1")));
			}
			return true;
		}
	}

	AddError(TEXT("NewVar_11 was not found in the fixture."));
	return false;
}

#endif // WITH_DEV_AUTOMATION_TESTS
