// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Model/AssetPackageDocument.h"

namespace AssetPackageDocumentTestUtils
{
	static FAssetPackageNameEntry MakeNameEntry(const FString& Name)
	{
		FAssetPackageNameEntry Entry;
		Entry.Name = Name;
		return Entry;
	}
} // namespace AssetPackageDocumentTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetPackageDocument_IsValidRangeRejectsOutOfBounds, "AssetSerializationInspector.Model.AssetPackageDocument.IsValidRangeRejectsOutOfBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPackageDocument_IsValidRangeRejectsOutOfBounds::RunTest(const FString& Parameters)
{
	FAssetPackageDocument Document;
	Document.FileData.SetNumZeroed(16);

	TestTrue(TEXT("A range fully inside the file is valid"), Document.IsValidRange(0, 16));
	TestTrue(TEXT("A zero-size range at the end of the file is valid"), Document.IsValidRange(16, 0));
	TestFalse(TEXT("A negative offset is invalid"), Document.IsValidRange(-1, 4));
	TestFalse(TEXT("A negative size is invalid"), Document.IsValidRange(0, -1));
	TestFalse(TEXT("A range extending past the end of the file is invalid"), Document.IsValidRange(10, 10));
	TestFalse(TEXT("An offset past the end of the file is invalid"), Document.IsValidRange(17, 0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetPackageDocument_ResolveNameReferenceAppliesNumberSuffix, "AssetSerializationInspector.Model.AssetPackageDocument.ResolveNameReferenceAppliesNumberSuffix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPackageDocument_ResolveNameReferenceAppliesNumberSuffix::RunTest(const FString& Parameters)
{
	using namespace AssetPackageDocumentTestUtils;

	FAssetPackageDocument Document;
	Document.NameMap.Add(MakeNameEntry(TEXT("StaticMeshComponent")));

	FAssetPackageNameReference PlainReference;
	PlainReference.NameIndex = 0;
	PlainReference.Number = 0;
	TestEqual(TEXT("Number 0 means no suffix"), Document.ResolveNameReference(PlainReference), FString(TEXT("StaticMeshComponent")));

	FAssetPackageNameReference SuffixedReference;
	SuffixedReference.NameIndex = 0;
	SuffixedReference.Number = 1;
	TestEqual(TEXT("FName's internal Number is offset by one"), Document.ResolveNameReference(SuffixedReference), FString(TEXT("StaticMeshComponent_0")));

	FAssetPackageNameReference InvalidReference;
	InvalidReference.NameIndex = 5;
	TestTrue(TEXT("An invalid name index should be reported rather than crash"), Document.ResolveNameReference(InvalidReference).Contains(TEXT("invalid")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetPackageDocument_ResolveImportPathDetectsCycles, "AssetSerializationInspector.Model.AssetPackageDocument.ResolveImportPathDetectsCycles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPackageDocument_ResolveImportPathDetectsCycles::RunTest(const FString& Parameters)
{
	using namespace AssetPackageDocumentTestUtils;

	// A well-formed package can never contain a cyclic import chain, but a
	// corrupted or hand-crafted file could. ResolveImportPath must degrade
	// gracefully (no infinite recursion / stack overflow) instead of crashing
	// the editor while a user is just trying to inspect a file.
	FAssetPackageDocument Document;
	Document.NameMap.Add(MakeNameEntry(TEXT("A")));
	Document.NameMap.Add(MakeNameEntry(TEXT("B")));

	FAssetPackageImportEntry ImportA;
	ImportA.Index = 0;
	ImportA.ObjectName.NameIndex = 0;
	ImportA.OuterIndex.RawIndex = -2; // -> Import[1]

	FAssetPackageImportEntry ImportB;
	ImportB.Index = 1;
	ImportB.ObjectName.NameIndex = 1;
	ImportB.OuterIndex.RawIndex = -1; // -> Import[0], forming a cycle A -> B -> A.

	Document.ImportMap.Add(ImportA);
	Document.ImportMap.Add(ImportB);

	const FString Path = Document.ResolveImportPath(0);

	TestTrue(TEXT("A cyclic import chain should be reported rather than recursing forever"), Path.Contains(TEXT("cyclic")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetPackageDocument_ResolveExportPathBuildsDottedPath, "AssetSerializationInspector.Model.AssetPackageDocument.ResolveExportPathBuildsDottedPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPackageDocument_ResolveExportPathBuildsDottedPath::RunTest(const FString& Parameters)
{
	using namespace AssetPackageDocumentTestUtils;

	FAssetPackageDocument Document;
	Document.PackageSummary.PackageName = TEXT("/Game/Blueprints/BP_Box");
	Document.NameMap.Add(MakeNameEntry(TEXT("BP_Box_C")));

	FAssetPackageExportEntry ClassExport;
	ClassExport.Index = 0;
	ClassExport.ObjectName.NameIndex = 0;
	ClassExport.OuterIndex.RawIndex = 0; // Null outer -> parented directly to the package.

	Document.ExportMap.Add(ClassExport);

	TestEqual(TEXT("An export parented to the package should be prefixed with the package name"), Document.ResolveExportPath(0),
		FString(TEXT("/Game/Blueprints/BP_Box.BP_Box_C")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
