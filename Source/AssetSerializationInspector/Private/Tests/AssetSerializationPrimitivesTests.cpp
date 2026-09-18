// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Readers/AssetPackageMemoryReader.h"
#include "Serialization/AssetSerializationPrimitives.h"

namespace AssetSerializationPrimitivesTestUtils
{
	static void AppendInt32(TArray64<uint8>& Buffer, int32 Value)
	{
		const int32 Start = Buffer.Num();
		Buffer.AddUninitialized(sizeof(int32));
		FMemory::Memcpy(Buffer.GetData() + Start, &Value, sizeof(int32));
	}

	// Ansi strings are serialized with a positive length that includes the
	// null terminator.
	static void AppendAnsiString(TArray64<uint8>& Buffer, const FString& Value)
	{
		AppendInt32(Buffer, Value.Len() + 1);

		for (const TCHAR Character : Value)
		{
			Buffer.Add(static_cast<uint8>(static_cast<ANSICHAR>(Character)));
		}

		Buffer.Add(0);
	}

	// Wide strings use a negative length (character count, including the
	// terminator) to distinguish them from the ANSI case.
	static void AppendWideString(TArray64<uint8>& Buffer, const FString& Value)
	{
		AppendInt32(Buffer, -(Value.Len() + 1));

		for (const TCHAR Character : Value)
		{
			const UTF16CHAR Wide = static_cast<UTF16CHAR>(Character);
			Buffer.Append(reinterpret_cast<const uint8*>(&Wide), sizeof(UTF16CHAR));
		}

		const UTF16CHAR Terminator = 0;
		Buffer.Append(reinterpret_cast<const uint8*>(&Terminator), sizeof(UTF16CHAR));
	}
} // namespace AssetSerializationPrimitivesTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSerializationPrimitives_ReadsAnsiString, "AssetSerializationInspector.Serialization.Primitives.ReadsAnsiString",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSerializationPrimitives_ReadsAnsiString::RunTest(const FString& Parameters)
{
	using namespace AssetSerializationPrimitivesTestUtils;

	TArray64<uint8> Buffer;
	AppendAnsiString(Buffer, TEXT("Hello"));

	FAssetPackageMemoryReader Reader(Buffer, 0, Buffer.Num());

	FString Result;
	FText Error;
	const bool bSuccess = AssetSerializationPrimitives::ReadSerializedString(Reader, Result, Error);

	TestTrue(TEXT("ReadSerializedString should succeed for a well-formed ANSI string"), bSuccess);
	TestEqual(TEXT("Decoded string should match the source"), Result, FString(TEXT("Hello")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSerializationPrimitives_ReadsWideString, "AssetSerializationInspector.Serialization.Primitives.ReadsWideString",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSerializationPrimitives_ReadsWideString::RunTest(const FString& Parameters)
{
	using namespace AssetSerializationPrimitivesTestUtils;

	TArray64<uint8> Buffer;
	AppendWideString(Buffer, TEXT("H\u00e9llo")); // "Héllo" - forces the UTF-16 path.

	FAssetPackageMemoryReader Reader(Buffer, 0, Buffer.Num());

	FString Result;
	FText Error;
	const bool bSuccess = AssetSerializationPrimitives::ReadSerializedString(Reader, Result, Error);

	TestTrue(TEXT("ReadSerializedString should succeed for a well-formed UTF-16 string"), bSuccess);
	TestEqual(TEXT("Decoded string should match the source"), Result, FString(TEXT("H\u00e9llo")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSerializationPrimitives_EmptyStringIsValid, "AssetSerializationInspector.Serialization.Primitives.EmptyStringIsValid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSerializationPrimitives_EmptyStringIsValid::RunTest(const FString& Parameters)
{
	using namespace AssetSerializationPrimitivesTestUtils;

	TArray64<uint8> Buffer;
	AppendInt32(Buffer, 0);

	FAssetPackageMemoryReader Reader(Buffer, 0, Buffer.Num());

	FString Result = TEXT("untouched");
	FText Error;
	const bool bSuccess = AssetSerializationPrimitives::ReadSerializedString(Reader, Result, Error);

	TestTrue(TEXT("A zero-length string is a valid, empty string"), bSuccess);
	TestTrue(TEXT("Result should be reset to empty"), Result.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSerializationPrimitives_RejectsMissingTerminator, "AssetSerializationInspector.Serialization.Primitives.RejectsMissingTerminator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSerializationPrimitives_RejectsMissingTerminator::RunTest(const FString& Parameters)
{
	using namespace AssetSerializationPrimitivesTestUtils;

	TArray64<uint8> Buffer;

	// Declare 3 ANSI characters but only write 3 non-null bytes: no terminator.
	AppendInt32(Buffer, 3);
	Buffer.Add('A');
	Buffer.Add('B');
	Buffer.Add('C');

	FAssetPackageMemoryReader Reader(Buffer, 0, Buffer.Num());

	FString Result;
	FText Error;
	const bool bSuccess = AssetSerializationPrimitives::ReadSerializedString(Reader, Result, Error);

	TestFalse(TEXT("A string missing its null terminator must be rejected"), bSuccess);
	TestFalse(TEXT("An error message should be produced"), Error.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSerializationPrimitives_RejectsUnreasonableLength, "AssetSerializationInspector.Serialization.Primitives.RejectsUnreasonableLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSerializationPrimitives_RejectsUnreasonableLength::RunTest(const FString& Parameters)
{
	using namespace AssetSerializationPrimitivesTestUtils;

	TArray64<uint8> Buffer;
	AppendInt32(Buffer, 64 * 1024 * 1024); // Far beyond MaximumReasonableNameLength.

	FAssetPackageMemoryReader Reader(Buffer, 0, Buffer.Num());

	FString Result;
	FText Error;
	const bool bSuccess = AssetSerializationPrimitives::ReadSerializedString(Reader, Result, Error);

	TestFalse(TEXT("An unreasonably large declared length must be rejected before allocating"), bSuccess);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSerializationPrimitives_RejectsMinInt32Length, "AssetSerializationInspector.Serialization.Primitives.RejectsMinInt32Length",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSerializationPrimitives_RejectsMinInt32Length::RunTest(const FString& Parameters)
{
	using namespace AssetSerializationPrimitivesTestUtils;

	TArray64<uint8> Buffer;
	AppendInt32(Buffer, MIN_int32); // Cannot be negated safely - must be rejected explicitly.

	FAssetPackageMemoryReader Reader(Buffer, 0, Buffer.Num());

	FString Result;
	FText Error;
	const bool bSuccess = AssetSerializationPrimitives::ReadSerializedString(Reader, Result, Error);

	TestFalse(TEXT("MIN_int32 is not a representable length and must be rejected"), bSuccess);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
