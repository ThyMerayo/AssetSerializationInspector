// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetPropertyValueDecoder.h"
#include "Trace/AssetSerializationTrace.h"
#include "UObject/EditorObjectVersion.h"
#include "UObject/ObjectVersion.h"

namespace AssetTextPropertyDecoderTestUtils
{
	template <typename TValue> static void AppendValue(FAssetPackageDocument& Document, const TValue Value)
	{
		const int32 Start = Document.FileData.Num();
		Document.FileData.AddUninitialized(sizeof(TValue));
		FMemory::Memcpy(Document.FileData.GetData() + Start, &Value, sizeof(TValue));
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

	static FAssetPackageDocument MakeDocument(const int32 EditorObjectVersion = FEditorObjectVersion::LatestVersion)
	{
		FAssetPackageDocument Document;
		Document.PackageSummary.SetFileVersions(VER_UE4_AUTOMATIC_VERSION, 0, 0);

		FCustomVersionContainer Versions;
		Versions.SetVersion(FEditorObjectVersion::GUID, EditorObjectVersion, TEXT("Dev-Editor"));
		Document.PackageSummary.SetCustomVersionContainer(Versions);

		return Document;
	}

	static FAssetDecodedPropertyValue DecodeText(const FAssetPackageDocument& Document)
	{
		FAssetSerializationTraceNode Node;
		Node.PropertyType.Name = TEXT("TextProperty");
		Node.Offset = 0;
		Node.Size = Document.FileData.Num();

		return FAssetPropertyValueDecoder::Decode(Document, Node, 0);
	}

	static const FAssetDecodedPropertyValue* FindChild(const FAssetDecodedPropertyValue& Value, const FString& Name)
	{
		return Value.Children.FindByPredicate([&Name](const FAssetDecodedPropertyValue& Child) { return Child.Name == Name; });
	}
} // namespace AssetTextPropertyDecoderTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetTextPropertyDecoder_DecodesBaseHistory, "AssetSerializationInspector.Serialization.AssetTextPropertyDecoder.DecodesBaseHistory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetTextPropertyDecoder_DecodesBaseHistory::RunTest(const FString& Parameters)
{
	using namespace AssetTextPropertyDecoderTestUtils;

	FAssetPackageDocument Document = MakeDocument();
	AppendValue<uint32>(Document, 0);	// Flags
	AppendValue<int8>(Document, 0);		// ETextHistoryType::Base
	AppendAnsiString(Document, TEXT("MyNamespace"));
	AppendAnsiString(Document, TEXT("0123ABCD"));
	AppendAnsiString(Document, TEXT("Hello"));

	const FAssetDecodedPropertyValue Result = DecodeText(Document);
	TestTrue(TEXT("A base-history text decodes"), Result.IsSuccess());
	TestEqual(TEXT("The value is the source string"), Result.Value, FString(TEXT("Hello")));
	TestEqual(TEXT("The text is reported as a struct"), Result.Kind, EAssetDecodedValueKind::Struct);
	TestEqual(TEXT("The whole value is consumed"), Result.Size, static_cast<int64>(Document.FileData.Num()));

	const FAssetDecodedPropertyValue* Namespace = FindChild(Result, TEXT("Namespace"));
	const FAssetDecodedPropertyValue* Key = FindChild(Result, TEXT("Key"));
	TestNotNull(TEXT("The namespace is exposed"), Namespace);
	TestNotNull(TEXT("The key is exposed"), Key);
	if (Namespace != nullptr && Key != nullptr)
	{
		TestEqual(TEXT("Namespace value"), Namespace->Value, FString(TEXT("MyNamespace")));
		TestEqual(TEXT("Key value"), Key->Value, FString(TEXT("0123ABCD")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetTextPropertyDecoder_DecodesNoneHistory, "AssetSerializationInspector.Serialization.AssetTextPropertyDecoder.DecodesNoneHistory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetTextPropertyDecoder_DecodesNoneHistory::RunTest(const FString& Parameters)
{
	using namespace AssetTextPropertyDecoderTestUtils;

	FAssetPackageDocument Invariant = MakeDocument();
	AppendValue<uint32>(Invariant, 0);
	AppendValue<int8>(Invariant, -1);
	AppendValue<uint32>(Invariant, 1);	// bHasCultureInvariantString
	AppendAnsiString(Invariant, TEXT("Invariant"));

	FAssetDecodedPropertyValue Result = DecodeText(Invariant);
	TestTrue(TEXT("A culture-invariant text decodes"), Result.IsSuccess());
	TestEqual(TEXT("The value is the invariant string"), Result.Value, FString(TEXT("Invariant")));

	FAssetPackageDocument Empty = MakeDocument();
	AppendValue<uint32>(Empty, 0);
	AppendValue<int8>(Empty, -1);
	AppendValue<uint32>(Empty, 0);
	Result = DecodeText(Empty);
	TestTrue(TEXT("An empty text decodes"), Result.IsSuccess());
	TestEqual(TEXT("An empty text has no value"), Result.Value, FString());
	TestEqual(TEXT("The whole value is consumed"), Result.Size, static_cast<int64>(Empty.FileData.Num()));

	// Before CultureInvariantTextSerializationKeyStability the None history has no payload.
	FAssetPackageDocument Legacy = MakeDocument(FEditorObjectVersion::CultureInvariantTextSerializationKeyStability - 1);
	AppendValue<uint32>(Legacy, 0);
	AppendValue<int8>(Legacy, -1);
	Result = DecodeText(Legacy);
	TestTrue(TEXT("A legacy empty text decodes"), Result.IsSuccess());
	TestEqual(TEXT("The whole value is consumed"), Result.Size, static_cast<int64>(Legacy.FileData.Num()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetTextPropertyDecoder_DecodesStringTableEntry, "AssetSerializationInspector.Serialization.AssetTextPropertyDecoder.DecodesStringTableEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetTextPropertyDecoder_DecodesStringTableEntry::RunTest(const FString& Parameters)
{
	using namespace AssetTextPropertyDecoderTestUtils;

	FAssetPackageDocument Document = MakeDocument();
	FAssetPackageNameEntry TableName;
	TableName.Name = TEXT("/Game/UI/ST_Menu");
	Document.NameMap.Add(TableName);

	AppendValue<uint32>(Document, 0);
	AppendValue<int8>(Document, 11);	// ETextHistoryType::StringTableEntry
	AppendValue<int32>(Document, 0);	// TableId name index
	AppendValue<int32>(Document, 0);	// TableId number
	AppendAnsiString(Document, TEXT("StartGame"));

	const FAssetDecodedPropertyValue Result = DecodeText(Document);
	TestTrue(TEXT("A string table text decodes"), Result.IsSuccess());
	TestEqual(TEXT("The value names the table and key"), Result.Value, FString(TEXT("/Game/UI/ST_Menu:StartGame")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetTextPropertyDecoder_RejectsUnsupportedAndTruncatedText, "AssetSerializationInspector.Serialization.AssetTextPropertyDecoder.RejectsUnsupportedAndTruncatedText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetTextPropertyDecoder_RejectsUnsupportedAndTruncatedText::RunTest(const FString& Parameters)
{
	using namespace AssetTextPropertyDecoderTestUtils;

	FAssetPackageDocument Formatted = MakeDocument();
	AppendValue<uint32>(Formatted, 0);
	AppendValue<int8>(Formatted, 1);	// ETextHistoryType::NamedFormat
	AppendValue<int32>(Formatted, 0);
	FAssetDecodedPropertyValue Result = DecodeText(Formatted);
	TestEqual(TEXT("Formatted text is reported as unsupported"), Result.Status, EAssetPropertyDecodeStatus::Unsupported);
	TestTrue(TEXT("An unsupported text exposes no partial children"), Result.Children.IsEmpty());

	FAssetPackageDocument Truncated = MakeDocument();
	AppendValue<uint32>(Truncated, 0);
	AppendValue<int8>(Truncated, 0);
	AppendAnsiString(Truncated, TEXT("Namespace"));
	Result = DecodeText(Truncated);
	TestEqual(TEXT("A text missing its key and source is invalid"), Result.Status, EAssetPropertyDecodeStatus::InvalidData);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
