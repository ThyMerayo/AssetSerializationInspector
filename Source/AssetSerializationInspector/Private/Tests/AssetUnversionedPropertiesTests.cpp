// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Sound/SoundClass.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "Diff/AssetPackageDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Serialization/AssetPropertyValueDecoder.h"
#include "Serialization/AssetUnversionedProperties.h"
#include "Trace/AssetPackageFieldDecoder.h"
#include "Trace/AssetSerializationTrace.h"

namespace UnversionedPropertiesTestUtils
{
	static const TCHAR* const Folder = TEXT("/Game/__AssetSerializationInspectorTests/Unversioned");

	/** Saves a new object made by Make into a package, tagged or without tags, and returns the file. */
	static FString SaveObject(const FString& Name, const bool bUnversioned, const TFunctionRef<UObject*(UPackage*)> Make)
	{
		const FString PackageName = FString(Folder) / Name;
		UPackage* Package = CreatePackage(*PackageName);
		UObject* Object = Make(Package);
		Package->MarkAsFullyLoaded();

		const FString File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError | (bUnversioned ? SAVE_Unversioned_Properties : 0);
		SaveArgs.bSlowTask = false;
		return UPackage::SavePackage(Package, Object, *File, SaveArgs) ? File : FString();
	}

	struct FLoaded
	{
		TSharedPtr<FAssetPackageDocument> Document;
		TSharedPtr<FAssetPackageTraceCollection> Traces;
		const FAssetPackageExportEntry* Export = nullptr;
		const FAssetSerializationTrace* Trace = nullptr;
	};

	static bool Load(const FString& File, const TCHAR* ObjectName, FLoaded& Out)
	{
		FText Error;
		Out.Document = FAssetPackageReader::LoadFromFile(File, Error);
		if (!Out.Document.IsValid())
		{
			return false;
		}

		Out.Traces = FAssetPackageFieldDecoder::Decode(*Out.Document);
		for (const FAssetPackageExportEntry& Export : Out.Document->ExportMap)
		{
			if (Out.Document->ResolveExportPath(Export.Index).EndsWith(ObjectName))
			{
				Out.Export = &Export;
				Out.Trace = Out.Traces->FindExportTrace(Export.Index);
			}
		}

		return Out.Export != nullptr && Out.Trace != nullptr && Out.Trace->Root.IsValid();
	}

	/** "Name[ArrayIndex]" to its displayed value, for the properties of an export. */
	static TMap<FString, FString> ReadProperties(const FLoaded& Loaded)
	{
		TMap<FString, FString> Result;
		for (const TSharedPtr<FAssetSerializationTraceNode>& Node : Loaded.Trace->Root->Children)
		{
			if (Node.IsValid() && Node->Kind == EAssetSerializationTraceKind::Property)
			{
				const FAssetDecodedPropertyValue Value = FAssetPropertyValueDecoder::Decode(*Loaded.Document, *Node, Loaded.Export->SerialOffset);
				Result.Add(FString::Printf(TEXT("%s[%d]"), *Node->Name, Node->ArrayIndex),
					Value.IsSuccess() ? FAssetPropertyValueDecoder::FormatForDisplay(Value) : FString::Printf(TEXT("<%s>"), *Value.Error));
			}
		}
		return Result;
	}

	static void DeleteFolder()
	{
		IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(Folder), false, true);
	}
} // namespace UnversionedPropertiesTestUtils

/**
 * Saves the same object twice, with tags and without, and requires that the two streams say the same: every property of the
 * tagged package is found without tags with the same value, and no payload is left undecoded.
 */
static void ExpectSameValues(FAutomationTestBase& Test, const TCHAR* Name, const TCHAR* ObjectName, const TFunctionRef<UObject*(UPackage*)> Make)
{
	using namespace UnversionedPropertiesTestUtils;

	const FString Tagged = SaveObject(FString(Name) + TEXT("Tagged"), false, Make);
	const FString Untagged = SaveObject(FString(Name) + TEXT("Untagged"), true, Make);
	if (!Test.TestTrue(FString::Printf(TEXT("%s: both packages are written"), Name), !Tagged.IsEmpty() && !Untagged.IsEmpty()))
	{
		return;
	}

	FLoaded TaggedPackage;
	FLoaded UntaggedPackage;
	if (!Test.TestTrue(FString::Printf(TEXT("%s: both load"), Name), Load(Tagged, ObjectName, TaggedPackage) && Load(Untagged, ObjectName, UntaggedPackage)))
	{
		return;
	}

	Test.TestFalse(FString::Printf(TEXT("%s: the first package has tags"), Name), AssetUnversionedProperties::IsUsedBy(*TaggedPackage.Document));
	Test.TestTrue(FString::Printf(TEXT("%s: the second was saved without them"), Name), AssetUnversionedProperties::IsUsedBy(*UntaggedPackage.Document));

	// References name the package they are in, and the two packages have different names.
	const auto Normalize = [Name](TMap<FString, FString> Properties) {
		for (TPair<FString, FString>& Property : Properties)
		{
			Property.Value = Property.Value.Replace(*(FString(Name) + TEXT("Untagged")), TEXT("Package")).Replace(*(FString(Name) + TEXT("Tagged")), TEXT("Package"));
		}
		return Properties;
	};

	const TMap<FString, FString> WithTags = Normalize(ReadProperties(TaggedPackage));
	const TMap<FString, FString> WithoutTags = Normalize(ReadProperties(UntaggedPackage));

	Test.TestTrue(FString::Printf(TEXT("%s: the object has properties"), Name), WithTags.Num() > 0);
	for (const TPair<FString, FString>& Property : WithTags)
	{
		const FString* Other = WithoutTags.Find(Property.Key);
		if (Test.TestNotNull(*FString::Printf(TEXT("%s: %s is found without tags"), Name, *Property.Key), Other))
		{
			Test.TestEqual(*FString::Printf(TEXT("%s: %s has the same value"), Name, *Property.Key), *Other, Property.Value);
		}
	}

	for (const TPair<FString, FString>& Property : WithoutTags)
	{
		// Zero values are stored as such, so the stream can list properties the tagged one left out.
		if (!WithTags.Contains(Property.Key))
		{
			Test.AddInfo(FString::Printf(TEXT("%s, only without tags: %s = %s"), Name, *Property.Key, *Property.Value));
		}
	}

	int32 Undecoded = 0;
	for (const TSharedPtr<FAssetSerializationTraceNode>& Node : UntaggedPackage.Trace->Root->Children)
	{
		Undecoded += Node.IsValid() && Node->TypeName.Contains(TEXT("Undecoded")) ? 1 : 0;
	}
	Test.TestEqual(*FString::Printf(TEXT("%s: no payload is left undecoded"), Name), Undecoded, 0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetUnversionedProperties_ReadsTheSameValuesAsTags, "AssetSerializationInspector.Serialization.AssetUnversionedProperties.ReadsTheSameValuesAsTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetUnversionedProperties_ReadsTheSameValuesAsTags::RunTest(const FString& Parameters)
{
	using namespace UnversionedPropertiesTestUtils;

	DeleteFolder();

	// Booleans, floats, an enum and a struct of them.
	ExpectSameValues(*this, TEXT("SoundClass"), TEXT("TestSoundClass"), [](UPackage* Package) -> UObject* {
		USoundClass* SoundClass = NewObject<USoundClass>(Package, TEXT("TestSoundClass"), RF_Public | RF_Standalone);
		SoundClass->Properties.Volume = 0.5f;
		SoundClass->Properties.Pitch = 2.0f;
		SoundClass->Properties.bIsUISound = true;
		SoundClass->Properties.bIsMusic = true;
		SoundClass->Properties.OutputTarget = EAudioOutputTarget::Controller;
		SoundClass->Properties.AttenuationDistanceScale = 3.0f;
		return SoundClass;
	});

	// Object references and an array of them.
	ExpectSameValues(*this, TEXT("Hierarchy"), TEXT("ParentClass"), [](UPackage* Package) -> UObject* {
		USoundClass* Parent = NewObject<USoundClass>(Package, TEXT("ParentClass"), RF_Public | RF_Standalone);
		USoundClass* ChildA = NewObject<USoundClass>(Package, TEXT("ChildA"), RF_Public);
		USoundClass* ChildB = NewObject<USoundClass>(Package, TEXT("ChildB"), RF_Public);
		ChildA->ParentClass = Parent;
		ChildB->ParentClass = Parent;
		Parent->ChildClasses = { ChildA, ChildB };
		Parent->Properties.Volume = 0.25f;
		return Parent;
	});

	// A curve: an array of structs (keys) inside a struct.
	ExpectSameValues(*this, TEXT("Curve"), TEXT("TestCurve"), [](UPackage* Package) -> UObject* {
		UCurveFloat* Curve = NewObject<UCurveFloat>(Package, TEXT("TestCurve"), RF_Public | RF_Standalone);
		Curve->FloatCurve.AddKey(0.0f, 1.0f);
		Curve->FloatCurve.AddKey(2.0f, 5.0f);
		Curve->bIsEventCurve = true;
		return Curve;
	});

	// Three curves of keys.
	ExpectSameValues(*this, TEXT("VectorCurve"), TEXT("TestVectorCurve"), [](UPackage* Package) -> UObject* {
		UCurveVector* Curve = NewObject<UCurveVector>(Package, TEXT("TestVectorCurve"), RF_Public | RF_Standalone);
		Curve->FloatCurves[0].AddKey(0.0f, 1.0f);
		Curve->FloatCurves[1].AddKey(1.0f, 2.0f);
		Curve->FloatCurves[1].AddKey(3.0f, 4.0f);
		Curve->FloatCurves[2].AddKey(5.0f, 6.0f);
		return Curve;
	});

	DeleteFolder();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetUnversionedProperties_DiffsTwoSaves, "AssetSerializationInspector.Serialization.AssetUnversionedProperties.DiffsTwoSaves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetUnversionedProperties_DiffsTwoSaves::RunTest(const FString& Parameters)
{
	using namespace UnversionedPropertiesTestUtils;

	DeleteFolder();

	// One object saved twice to the same package, with another volume the second time: the file is copied in between.
	USoundClass* SoundClass = nullptr;
	const FString First = SaveObject(TEXT("TwoSaves"), true, [&SoundClass](UPackage* Package) -> UObject* {
		SoundClass = NewObject<USoundClass>(Package, TEXT("TestSoundClass"), RF_Public | RF_Standalone);
		SoundClass->Properties.Volume = 0.5f;
		SoundClass->Properties.Pitch = 2.0f;
		return SoundClass;
	});
	if (!TestTrue(TEXT("The first save is written"), !First.IsEmpty() && SoundClass != nullptr))
	{
		DeleteFolder();
		return false;
	}

	const FString Before = FPaths::ChangeExtension(First, TEXT("before.uasset"));
	IFileManager::Get().Copy(*Before, *First);

	SoundClass->Properties.Volume = 0.75f;
	const FString Second = SaveObject(TEXT("TwoSaves"), true, [SoundClass](UPackage*) -> UObject* { return SoundClass; });

	FLoaded OldPackage;
	FLoaded NewPackage;
	if (TestTrue(TEXT("Both load"), !Second.IsEmpty() && Load(Before, TEXT("TestSoundClass"), OldPackage) && Load(Second, TEXT("TestSoundClass"), NewPackage)))
	{
		const FAssetPackageDiffResult Diff = AssetPackageDiff::Compare(*OldPackage.Document, *NewPackage.Document, OldPackage.Traces.Get(), NewPackage.Traces.Get());

		// The volume is a field of the Properties struct: the diff must name it, with both values.
		bool bFoundVolume = false;
		TFunction<void(const FAssetPackageDiffEntry&)> Visit = [&](const FAssetPackageDiffEntry& Entry) {
			if (Entry.Kind == EAssetPackageDiffKind::Property && Entry.DisplayName.ToString() == TEXT("Volume") && Entry.OldValue.StartsWith(TEXT("0.5")) && Entry.NewValue.StartsWith(TEXT("0.75")))
			{
				bFoundVolume = true;
			}

			for (const FAssetPackageDiffEntry& Child : Entry.Children)
			{
				Visit(Child);
			}
		};

		for (const FAssetPackageDiffEntry& Entry : Diff.Entries)
		{
			Visit(Entry);
		}

		TestTrue(TEXT("The change of volume is found in packages saved without tags"), bFoundVolume);
	}

	DeleteFolder();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
