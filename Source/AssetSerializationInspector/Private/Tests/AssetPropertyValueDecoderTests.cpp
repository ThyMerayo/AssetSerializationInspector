// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "UObject/ObjectVersion.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetPropertyValueDecoder_DecodesNativelySerializedStructs, "AssetSerializationInspector.Serialization.AssetPropertyValueDecoder.DecodesNativelySerializedStructs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPropertyValueDecoder_DecodesNativelySerializedStructs::RunTest(const FString& Parameters)
{
	using namespace AssetPropertyValueDecoderTestUtils;

	const auto StructType = [](const TCHAR* StructName) {
		FAssetSerializedPropertyType Type = MakeType(TEXT("StructProperty"));
		Type.Parameters.Add(MakeType(StructName));
		return Type;
	};

	// FGameplayTagContainer::Serialize writes an array of names, not tagged properties.
	FAssetPackageDocument Container;
	AppendValue<int32>(Container, 2);
	AppendName(Container, AddName(Container, TEXT("Ability.Death")));
	AppendName(Container, AddName(Container, TEXT("Ability.Melee")));

	FAssetDecodedPropertyValue Result = DecodeWholeFile(Container, StructType(TEXT("GameplayTagContainer")));
	TestTrue(TEXT("A tag container decodes"), Result.IsSuccess());
	if (TestEqual(TEXT("Both tags are read"), Result.Children.Num(), 2))
	{
		TestEqual(TEXT("By name"), Result.Children[1].Value, FString(TEXT("Ability.Melee")));
	}
	TestEqual(TEXT("The whole value is consumed"), Result.Size, static_cast<int64>(Container.FileData.Num()));

	FAssetPackageDocument Tag;
	AppendName(Tag, AddName(Tag, TEXT("Ability.Death")));
	Result = DecodeWholeFile(Tag, StructType(TEXT("GameplayTag")));
	TestEqual(TEXT("A tag is its name"), Result.Value, FString(TEXT("Ability.Death")));

	FAssetPackageDocument Vector;
	AppendValue<float>(Vector, 1.5f);
	AppendValue<float>(Vector, -2.0f);
	Result = DecodeWholeFile(Vector, StructType(TEXT("DeprecateSlateVector2D")));
	TestTrue(TEXT("A single precision 2D vector decodes"), Result.IsSuccess());
	TestEqual(TEXT("It is two floats"), Result.Size, static_cast<int64>(Vector.FileData.Num()));

	FAssetPackageDocument Box;
	AppendValue<float>(Box, 0.0f);
	AppendValue<float>(Box, 1.0f);
	AppendValue<float>(Box, 2.0f);
	AppendValue<float>(Box, 3.0f);
	AppendValue<uint8>(Box, 1);
	Result = DecodeWholeFile(Box, StructType(TEXT("Box2f")));
	TestTrue(TEXT("A box decodes"), Result.IsSuccess());
	TestEqual(TEXT("It is two corners and a flag"), Result.Size, static_cast<int64>(Box.FileData.Num()));
	TestTrue(TEXT("The flag is shown"), Result.Value.Contains(TEXT("IsValid=1")));

	FAssetPackageDocument Version;
	AppendValue<uint8>(Version, 1);
	Result = DecodeWholeFile(Version, StructType(TEXT("GameplayEffectVersion")));
	TestTrue(TEXT("A gameplay effect version decodes"), Result.IsSuccess());
	TestEqual(TEXT("As its byte"), Result.Value, FString(TEXT("1")));

	FAssetPackageDocument Truncated;
	AppendValue<int32>(Truncated, 3);
	AppendName(Truncated, AddName(Truncated, TEXT("Ability.Death")));
	Result = DecodeWholeFile(Truncated, StructType(TEXT("GameplayTagContainer")));
	TestEqual(TEXT("A container cut short is invalid"), Result.Status, EAssetPropertyDecodeStatus::InvalidData);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetPropertyValueDecoder_DecodesDelegates, "AssetSerializationInspector.Serialization.AssetPropertyValueDecoder.DecodesDelegates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPropertyValueDecoder_DecodesDelegates::RunTest(const FString& Parameters)
{
	using namespace AssetPropertyValueDecoderTestUtils;

	FAssetPackageDocument Empty;
	AppendValue<int32>(Empty, 0);
	FAssetDecodedPropertyValue Result = DecodeWholeFile(Empty, MakeType(TEXT("MulticastInlineDelegateProperty")));
	TestTrue(TEXT("A multicast delegate with no bindings decodes"), Result.IsSuccess());
	TestEqual(TEXT("It has no bindings"), Result.Children.Num(), 0);

	FAssetPackageDocument Bound;
	AppendValue<int32>(Bound, 1);
	AppendValue<int32>(Bound, 0);
	AppendName(Bound, AddName(Bound, TEXT("OnChanged")));
	Result = DecodeWholeFile(Bound, MakeType(TEXT("MulticastInlineDelegateProperty")));
	if (TestTrue(TEXT("A bound delegate decodes"), Result.IsSuccess()) && TestEqual(TEXT("One binding"), Result.Children.Num(), 1))
	{
		TestTrue(TEXT("The function is named"), Result.Children[0].Value.Contains(TEXT("OnChanged")));
	}

	FAssetPackageDocument Single;
	AppendValue<int32>(Single, 0);
	AppendName(Single, AddName(Single, TEXT("None")));
	Result = DecodeWholeFile(Single, MakeType(TEXT("DelegateProperty")));
	TestEqual(TEXT("An unbound delegate is None"), Result.Value, FString(TEXT("None")));

	// An FFieldPath: the names below the owner, then the owner (0 is a null package index).
	FAssetPackageDocument Path;
	AppendValue<int32>(Path, 2);
	AppendName(Path, AddName(Path, TEXT("Inner")));
	AppendName(Path, AddName(Path, TEXT("Value")));
	AppendValue<int32>(Path, 0);
	Result = DecodeWholeFile(Path, MakeType(TEXT("FieldPathProperty")));
	TestTrue(TEXT("A field path decodes"), Result.IsSuccess());
	TestTrue(TEXT("Its names are joined"), Result.Value.Contains(TEXT("Inner.Value")));
	TestEqual(TEXT("The whole value is consumed"), Result.Size, static_cast<int64>(Path.FileData.Num()));

	FAssetPackageDocument EmptyPath;
	AppendValue<int32>(EmptyPath, 0);
	AppendValue<int32>(EmptyPath, 0);
	Result = DecodeWholeFile(EmptyPath, MakeType(TEXT("FieldPathProperty")));
	TestEqual(TEXT("An empty field path is None"), Result.Value, FString(TEXT("None")));

	FAssetPackageDocument Truncated;
	AppendValue<int32>(Truncated, 2);
	AppendValue<int32>(Truncated, 0);
	Result = DecodeWholeFile(Truncated, MakeType(TEXT("MulticastInlineDelegateProperty")));
	TestEqual(TEXT("Missing bindings are invalid"), Result.Status, EAssetPropertyDecodeStatus::InvalidData);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetPropertyValueDecoder_DecodesLegacyPropertyTags, "AssetSerializationInspector.Serialization.AssetPropertyValueDecoder.DecodesLegacyPropertyTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPropertyValueDecoder_DecodesLegacyPropertyTags::RunTest(const FString& Parameters)
{
	using namespace AssetPropertyValueDecoderTestUtils;

	// A package saved before PROPERTY_TAG_COMPLETE_TYPE_NAME: type name only, size, array index, the type's extra fields, then a property GUID flag.
	FAssetPackageDocument Document;
	Document.PackageSummary.SetFileVersions(522, static_cast<int32>(EUnrealEngineObjectUE5Version::INITIAL_VERSION), 0);

	const auto AddHeader = [&Document](const TCHAR* Name, const TCHAR* Type, const int32 Size) {
		AppendName(Document, AddName(Document, Name));
		AppendName(Document, AddName(Document, Type));
		AppendValue<int32>(Document, Size);
		AppendValue<int32>(Document, 0);
	};

	AddHeader(TEXT("Count"), TEXT("IntProperty"), 4);
	AppendValue<uint8>(Document, 0); // no property GUID
	AppendValue<int32>(Document, 7);

	AddHeader(TEXT("Enabled"), TEXT("BoolProperty"), 0);
	AppendValue<uint8>(Document, 1); // the value lives in the tag
	AppendValue<uint8>(Document, 0);

	AddHeader(TEXT("Items"), TEXT("ArrayProperty"), 12);
	AppendName(Document, AddName(Document, TEXT("IntProperty"))); // inner type
	AppendValue<uint8>(Document, 0);
	AppendValue<int32>(Document, 2);
	AppendValue<int32>(Document, 10);
	AppendValue<int32>(Document, 20);

	// An array of structs starts with a tag for its inner struct, which has no value of its own.
	AddHeader(TEXT("Points"), TEXT("ArrayProperty"), 0);
	const int32 PointsSizeOffset = Document.FileData.Num() - 8;
	AppendName(Document, AddName(Document, TEXT("StructProperty")));
	AppendValue<uint8>(Document, 0);
	const int32 PointsStart = Document.FileData.Num();
	AppendValue<int32>(Document, 1);
	AppendName(Document, AddName(Document, TEXT("Points")));
	AppendName(Document, AddName(Document, TEXT("StructProperty")));
	AppendValue<int32>(Document, 0); // the inner tag's size, only informative
	AppendValue<int32>(Document, 0);
	AppendName(Document, AddName(Document, TEXT("MyPoint")));
	AppendValue<FGuid>(Document, FGuid());
	AppendValue<uint8>(Document, 0);
	AddHeader(TEXT("X"), TEXT("IntProperty"), 4);
	AppendValue<uint8>(Document, 0);
	AppendValue<int32>(Document, 5);
	AppendName(Document, AddName(Document, TEXT("None")));
	const int32 PointsSize = Document.FileData.Num() - PointsStart;
	FMemory::Memcpy(Document.FileData.GetData() + PointsSizeOffset, &PointsSize, sizeof(int32));

	AppendName(Document, AddName(Document, TEXT("None")));

	FAssetSerializedPropertyType StructType = MakeType(TEXT("StructProperty"));
	StructType.Parameters.Add(MakeType(TEXT("MyStruct")));

	const FAssetDecodedPropertyValue Result = DecodeWholeFile(Document, StructType);
	if (TestTrue(TEXT("A struct with older tags decodes"), Result.IsSuccess()) && TestEqual(TEXT("Every field is found"), Result.Children.Num(), 4))
	{
		TestEqual(TEXT("An int"), Result.Children[0].Value, FString(TEXT("7")));
		TestEqual(TEXT("A bool held in the tag"), Result.Children[1].Value, FString(TEXT("true")));
		TestEqual(TEXT("An array of ints"), Result.Children[2].Children.Num(), 2);
		TestTrue(TEXT("An array of structs decodes"), Result.Children[3].IsSuccess());
		if (TestEqual(TEXT("With its element"), Result.Children[3].Children.Num(), 1))
		{
			TestEqual(TEXT("Holding its field"), Result.Children[3].Children[0].Children.Num(), 1);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetPropertyValueDecoder_FormatsValuesForDisplay, "AssetSerializationInspector.Serialization.AssetPropertyValueDecoder.FormatsValuesForDisplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetPropertyValueDecoder_FormatsValuesForDisplay::RunTest(const FString& Parameters)
{
	const auto MakeScalar = [](const FString& Name, const FString& Value) {
		FAssetDecodedPropertyValue Scalar;
		Scalar.Status = EAssetPropertyDecodeStatus::Success;
		Scalar.Kind = EAssetDecodedValueKind::Scalar;
		Scalar.Name = Name;
		Scalar.Value = Value;
		return Scalar;
	};

	TestEqual(TEXT("Scalars are shown as-is"), FAssetPropertyValueDecoder::FormatForDisplay(MakeScalar(TEXT("X"), TEXT("5"))), FString(TEXT("5")));

	FAssetDecodedPropertyValue Struct;
	Struct.Kind = EAssetDecodedValueKind::Struct;
	Struct.Value = TEXT("2 fields");
	Struct.Children = { MakeScalar(TEXT("X"), TEXT("1")), MakeScalar(TEXT("Y"), TEXT("2")) };
	TestEqual(TEXT("Structs list their fields by name"), FAssetPropertyValueDecoder::FormatForDisplay(Struct), FString(TEXT("{X=1, Y=2}")));

	FAssetDecodedPropertyValue Array;
	Array.Kind = EAssetDecodedValueKind::Array;
	Array.Value = TEXT("3 elements");
	Array.Children = { MakeScalar(TEXT("[0]"), TEXT("a")), MakeScalar(TEXT("[1]"), TEXT("b")), MakeScalar(TEXT("[2]"), TEXT("c")) };
	TestEqual(TEXT("Containers show their count and elements"), FAssetPropertyValueDecoder::FormatForDisplay(Array), FString(TEXT("3 elements: a, b, c")));
	TestEqual(TEXT("Long containers are cut"), FAssetPropertyValueDecoder::FormatForDisplay(Array, 2), FString(TEXT("3 elements: a, b, ...")));

	FAssetDecodedPropertyValue Entry;
	Entry.Kind = EAssetDecodedValueKind::MapEntry;
	Entry.Children = { MakeScalar(TEXT("Key"), TEXT("k")), MakeScalar(TEXT("Value"), TEXT("v")) };
	FAssetDecodedPropertyValue Map;
	Map.Kind = EAssetDecodedValueKind::Map;
	Map.Value = TEXT("1 entries");
	Map.Children = { Entry };
	TestEqual(TEXT("Map entries are shown as key=value"), FAssetPropertyValueDecoder::FormatForDisplay(Map), FString(TEXT("1 entries: k=v")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
