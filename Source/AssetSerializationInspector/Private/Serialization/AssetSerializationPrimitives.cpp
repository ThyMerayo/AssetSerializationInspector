#include "Serialization/AssetSerializationPrimitives.h"

namespace
{
	constexpr int32 MaximumReasonableNameLength = 1024 * 1024;
}

bool AssetSerializationPrimitives::ReadSerializedString(FAssetPackageMemoryReader& Reader, FString& OutString, FText& OutError)
{
	OutString.Reset();

	int32 SerializedLength = 0;
	Reader << SerializedLength;

	if (Reader.IsError())
	{
		OutError = NSLOCTEXT("AssetPackageReader", "NameLengthReadFailed", "Could not read the serialized name length.");
		return false;
	}

	if (SerializedLength == 0)
	{
		return true;
	}

	if (SerializedLength == MIN_int32)
	{
		OutError = NSLOCTEXT("AssetPackageReader", "InvalidNameLength", "The serialized name length is invalid.");
		return false;
	}

	const bool bIsWide = SerializedLength < 0;
	const int64 CharacterCount = bIsWide ? -static_cast<int64>(SerializedLength) : static_cast<int64>(SerializedLength);

	if (CharacterCount <= 0 || CharacterCount > MaximumReasonableNameLength)
	{
		OutError = FText::Format(NSLOCTEXT("AssetPackageReader", "UnreasonableNameLength", "The name declares an unreasonable length: {0}."), FText::AsNumber(CharacterCount));
		return false;
	}

	if (bIsWide)
	{
		const int64 ByteCount = CharacterCount * sizeof(UTF16CHAR);

		if (!Reader.CanRead(ByteCount))
		{
			OutError = NSLOCTEXT("AssetPackageReader", "WideNameOutsideRegion", "The UTF-16 name extends beyond the Name Map.");
			return false;
		}

		TArray<UTF16CHAR> Characters;
		Characters.SetNumUninitialized(static_cast<int32>(CharacterCount));

		Reader.Serialize(Characters.GetData(), ByteCount);

		if (Reader.IsError())
		{
			OutError = NSLOCTEXT("AssetPackageReader", "WideNameReadFailed", "Could not read a UTF-16 name.");
			return false;
		}

		if (Characters.Last() != 0)
		{
			OutError = NSLOCTEXT("AssetPackageReader", "WideNameMissingTerminator", "A UTF-16 name has no null terminator.");
			return false;
		}

		const int32 SourceLength = static_cast<int32>(CharacterCount - 1);
		const auto Converted = StringCast<TCHAR>(Characters.GetData(), SourceLength);
		OutString = FString(Converted.Length(), Converted.Get());
	}
	else
	{
		if (!Reader.CanRead(CharacterCount))
		{
			OutError = NSLOCTEXT("AssetPackageReader", "AnsiNameOutsideRegion", "The narrow name extends beyond the Name Map.");
			return false;
		}

		TArray<ANSICHAR> Characters;
		Characters.SetNumUninitialized(static_cast<int32>(CharacterCount));

		Reader.Serialize(Characters.GetData(), CharacterCount);

		if (Reader.IsError())
		{
			OutError = NSLOCTEXT("AssetPackageReader", "AnsiNameReadFailed", "Could not read a narrow name.");
			return false;
		}

		if (Characters.Last() != 0)
		{
			OutError = NSLOCTEXT("AssetPackageReader", "AnsiNameMissingTerminator", "A narrow name has no null terminator.");
			return false;
		}

		const int32 SourceLength = static_cast<int32>(CharacterCount - 1);
		const auto Converted = StringCast<TCHAR>(Characters.GetData(), SourceLength);
		OutString = FString(Converted.Length(), Converted.Get());
	}

	return true;
}
