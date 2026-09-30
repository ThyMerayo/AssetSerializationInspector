// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetPropertyValueDecoder.h"
#include "Trace/AssetSerializationTrace.h"

namespace AssetPropertyValueDecoderTestUtils
{
	template <typename TValue> static void AppendValue(FAssetPackageDocument& Document, const TValue Value)
	{
		const int32 Start = Document.FileData.Num();
		Document.FileData.AddUninitialized(sizeof(TValue));
		FMemory::Memcpy(Document.FileData.GetData() + Start, &Value, sizeof(TValue));
	}

	static void AppendName(FAssetPackageDocument& Document, const int32 NameIndex, const int32 Number = 0)
	{
		AppendValue<int32>(Document, NameIndex);
		AppendValue<int32>(Document, Number);
	}

	static void AppendAnsiString(FAssetPackageDocument& Document, const FString& Value)
	{
		if (Value.IsEmpty())
		{
			AppendValue<int32>(Document, 0);
			return;
		}

		AppendValue<int32>(Document, Value.Len() + 1);
		for (int32 Index = 0; Index < Value.Len(); ++Index)
		{
			AppendValue<uint8>(Document, static_cast<uint8>(Value[Index]));
		}
		AppendValue<uint8>(Document, 0);
	}

	static int32 AddName(FAssetPackageDocument& Document, const FString& Name)
	{
		FAssetPackageNameEntry Entry;
		Entry.Name = Name;
		return Document.NameMap.Add(Entry);
	}

	static FAssetSerializedPropertyType MakeType(const FString& Name)
	{
		FAssetSerializedPropertyType Type;
		Type.Name = Name;
		return Type;
	}

	static FAssetDecodedPropertyValue DecodeRange(const FAssetPackageDocument& Document, const FAssetSerializedPropertyType& Type, const int64 Size)
	{
		FAssetSerializationTraceNode Node;
		Node.PropertyType = Type;
		Node.Offset = 0;
		Node.Size = Size;

		return FAssetPropertyValueDecoder::Decode(Document, Node, 0);
	}

	static FAssetDecodedPropertyValue DecodeWholeFile(const FAssetPackageDocument& Document, const FAssetSerializedPropertyType& Type)
	{
		return DecodeRange(Document, Type, Document.FileData.Num());
	}
} // namespace AssetPropertyValueDecoderTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetPropertyValueDecoder_DecodesFixedWidthIntegers, "AssetSerializationInspector.Serialization.AssetPropertyValueDecoder.DecodesFixedWidthIntegers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPropertyValueDecoder_DecodesFixedWidthIntegers::RunTest(const FString& Parameters)
{
	using namespace AssetPropertyValueDecoderTestUtils;

	const auto DecodeOne = [](const FString& TypeName, auto Value) {
		FAssetPackageDocument Document;
		AppendValue(Document, Value);
		return DecodeWholeFile(Document, MakeType(TypeName));
	};

	FAssetDecodedPropertyValue Result = DecodeOne(TEXT("Int8Property"), static_cast<int8>(-5));
	TestTrue(TEXT("Int8 decodes"), Result.IsSuccess());
	TestEqual(TEXT("Int8 sign is preserved"), Result.Value, FString(TEXT("-5")));

	Result = DecodeOne(TEXT("Int16Property"), static_cast<int16>(-300));
	TestEqual(TEXT("Int16 sign is preserved"), Result.Value, FString(TEXT("-300")));

	Result = DecodeOne(TEXT("Int64Property"), static_cast<int64>(-5000000000));
	TestEqual(TEXT("Int64 does not truncate"), Result.Value, FString(TEXT("-5000000000")));

	Result = DecodeOne(TEXT("UInt16Property"), static_cast<uint16>(65535));
	TestEqual(TEXT("UInt16 is unsigned"), Result.Value, FString(TEXT("65535")));

	Result = DecodeOne(TEXT("UInt32Property"), static_cast<uint32>(4000000000u));
	TestEqual(TEXT("UInt32 is unsigned"), Result.Value, FString(TEXT("4000000000")));

	Result = DecodeOne(TEXT("UInt64Property"), static_cast<uint64>(18000000000000000000ull));
	TestEqual(TEXT("UInt64 is unsigned"), Result.Value, FString(TEXT("18000000000000000000")));

	FAssetPackageDocument Truncated;
	AppendValue<uint8>(Truncated, 1);
	Result = DecodeWholeFile(Truncated, MakeType(TEXT("Int64Property")));
	TestEqual(TEXT("A value that does not fit its range is invalid"), Result.Status, EAssetPropertyDecodeStatus::InvalidData);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetPropertyValueDecoder_DecodesByteAndEnumValues, "AssetSerializationInspector.Serialization.AssetPropertyValueDecoder.DecodesByteAndEnumValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPropertyValueDecoder_DecodesByteAndEnumValues::RunTest(const FString& Parameters)
{
	using namespace AssetPropertyValueDecoderTestUtils;

	FAssetPackageDocument RawByte;
	AppendValue<uint8>(RawByte, 200);
	FAssetDecodedPropertyValue Result = DecodeWholeFile(RawByte, MakeType(TEXT("ByteProperty")));
	TestEqual(TEXT("A ByteProperty without an enum is a raw byte"), Result.Value, FString(TEXT("200")));

	FAssetPackageDocument EnumByte;
	AppendName(EnumByte, AddName(EnumByte, TEXT("EMyEnum::Second")));
	FAssetSerializedPropertyType ByteWithEnum = MakeType(TEXT("ByteProperty"));
	ByteWithEnum.Parameters.Add(MakeType(TEXT("/Script/Test.EMyEnum")));
	Result = DecodeWholeFile(EnumByte, ByteWithEnum);
	TestTrue(TEXT("A ByteProperty with an enum decodes"), Result.IsSuccess());
	TestEqual(TEXT("A ByteProperty with an enum is stored by name"), Result.Value, FString(TEXT("EMyEnum::Second")));

	FAssetPackageDocument EnumProperty;
	AppendName(EnumProperty, AddName(EnumProperty, TEXT("EMyEnum::Third")));
	FAssetSerializedPropertyType EnumType = MakeType(TEXT("EnumProperty"));
	EnumType.Parameters.Add(MakeType(TEXT("/Script/Test.EMyEnum")));
	EnumType.Parameters.Add(MakeType(TEXT("ByteProperty")));
	Result = DecodeWholeFile(EnumProperty, EnumType);
	TestTrue(TEXT("An EnumProperty decodes"), Result.IsSuccess());
	TestEqual(TEXT("An EnumProperty is stored by name"), Result.Value, FString(TEXT("EMyEnum::Third")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetPropertyValueDecoder_DecodesObjectReferences, "AssetSerializationInspector.Serialization.AssetPropertyValueDecoder.DecodesObjectReferences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPropertyValueDecoder_DecodesObjectReferences::RunTest(const FString& Parameters)
{
	using namespace AssetPropertyValueDecoderTestUtils;

	FAssetPackageDocument Base;
	Base.PackageSummary.PackageName = TEXT("/Game/Test");

	FAssetPackageImportEntry Import;
	Import.ObjectName.NameIndex = AddName(Base, TEXT("StaticMesh"));
	Base.ImportMap.Add(Import);

	FAssetPackageExportEntry Export;
	Export.ObjectName.NameIndex = AddName(Base, TEXT("MyObject"));
	Base.ExportMap.Add(Export);

	const auto DecodeIndex = [&Base](const int32 RawIndex, const FString& TypeName) {
		FAssetPackageDocument Document = Base;
		AppendValue<int32>(Document, RawIndex);
		return DecodeWholeFile(Document, MakeType(TypeName));
	};

	FAssetDecodedPropertyValue Result = DecodeIndex(0, TEXT("ObjectProperty"));
	TestEqual(TEXT("A null reference is None"), Result.Value, FString(TEXT("None")));

	Result = DecodeIndex(-1, TEXT("ObjectProperty"));
	TestEqual(TEXT("Negative indices resolve through the import map"), Result.Value, FString(TEXT("StaticMesh")));

	Result = DecodeIndex(1, TEXT("ObjectProperty"));
	TestEqual(TEXT("Positive indices resolve through the export map"), Result.Value, FString(TEXT("/Game/Test.MyObject")));

	Result = DecodeIndex(2, TEXT("ClassProperty"));
	TestEqual(TEXT("An out-of-range export index is invalid"), Result.Status, EAssetPropertyDecodeStatus::InvalidData);

	Result = DecodeIndex(-2, TEXT("ObjectProperty"));
	TestEqual(TEXT("An out-of-range import index is invalid"), Result.Status, EAssetPropertyDecodeStatus::InvalidData);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetPropertyValueDecoder_DecodesSoftObjectPaths, "AssetSerializationInspector.Serialization.AssetPropertyValueDecoder.DecodesSoftObjectPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPropertyValueDecoder_DecodesSoftObjectPaths::RunTest(const FString& Parameters)
{
	using namespace AssetPropertyValueDecoderTestUtils;

	FAssetPackageDocument Document;
	const int32 NoneName = AddName(Document, TEXT("None"));
	const int32 PackageName = AddName(Document, TEXT("/Game/Meshes/Rock"));
	const int32 AssetName = AddName(Document, TEXT("Rock"));

	// Two property values (index 1 and index 5) followed by the header table.
	AppendValue<int32>(Document, 1);
	AppendValue<int32>(Document, 5);
	const int64 ValueSize = sizeof(int32);

	Document.PackageSummary.SoftObjectPathsOffset = Document.FileData.Num();
	Document.PackageSummary.SoftObjectPathsCount = 2;

	// Entry 0: null path.
	AppendName(Document, NoneName);
	AppendName(Document, NoneName);
	AppendAnsiString(Document, FString());

	// Entry 1: /Game/Meshes/Rock.Rock:SubObject
	AppendName(Document, PackageName);
	AppendName(Document, AssetName);
	AppendAnsiString(Document, TEXT("SubObject"));

	FAssetSerializationTraceNode Node;
	Node.PropertyType = MakeType(TEXT("SoftObjectProperty"));
	Node.Offset = 0;
	Node.Size = ValueSize;

	FAssetDecodedPropertyValue Result = FAssetPropertyValueDecoder::Decode(Document, Node, 0);
	TestTrue(TEXT("A soft object path decodes"), Result.IsSuccess());
	TestEqual(TEXT("The path combines package, asset and sub-path"), Result.Value, FString(TEXT("/Game/Meshes/Rock.Rock:SubObject")));

	Node.Offset = sizeof(int32);
	Result = FAssetPropertyValueDecoder::Decode(Document, Node, 0);
	TestEqual(TEXT("An index outside the table is invalid"), Result.Status, EAssetPropertyDecodeStatus::InvalidData);

	FAssetPackageDocument NoTable = Document;
	NoTable.PackageSummary.SoftObjectPathsCount = 0;
	Node.Offset = 0;
	Result = FAssetPropertyValueDecoder::Decode(NoTable, Node, 0);
	TestEqual(TEXT("Inline soft object paths are reported as unsupported"), Result.Status, EAssetPropertyDecodeStatus::Unsupported);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetPropertyValueDecoder_DecodesMapReplaceMarker, "AssetSerializationInspector.Serialization.AssetPropertyValueDecoder.DecodesMapReplaceMarker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPropertyValueDecoder_DecodesMapReplaceMarker::RunTest(const FString& Parameters)
{
	using namespace AssetPropertyValueDecoderTestUtils;

	// FMapProperty writes a KeysToRemove count of -1 (INDEX_NONE) when the map replaces its defaults, followed by every entry.
	FAssetPackageDocument Document;
	AppendValue<int32>(Document, -1);
	AppendValue<int32>(Document, 2);
	AppendValue<int32>(Document, 1);
	AppendValue<int32>(Document, 10);
	AppendValue<int32>(Document, 2);
	AppendValue<int32>(Document, 20);

	FAssetSerializedPropertyType MapType = MakeType(TEXT("MapProperty"));
	MapType.Parameters.Add(MakeType(TEXT("IntProperty")));
	MapType.Parameters.Add(MakeType(TEXT("IntProperty")));

	const FAssetDecodedPropertyValue Result = DecodeWholeFile(Document, MapType);
	TestTrue(TEXT("A replace-marker map decodes"), Result.IsSuccess());
	TestEqual(TEXT("It is a complete map"), Result.ContainerMode, EAssetDecodedContainerSerializationMode::Full);
	TestEqual(TEXT("Both entries are read"), Result.Children.Num(), 2);
	TestEqual(TEXT("Entries replace the defaults"), Result.Children[0].ContainerOperation, EAssetDecodedContainerOperation::Replace);
	TestEqual(TEXT("The whole value is consumed"), Result.Size, static_cast<int64>(Document.FileData.Num()));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
