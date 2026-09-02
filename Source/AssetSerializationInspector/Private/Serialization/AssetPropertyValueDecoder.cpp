// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetPropertyValueDecoder.h"

#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Serialization/AssetSerializationPrimitives.h"

template <typename TValue> static FAssetDecodedPropertyValue DecodeIntegral(const FAssetPackageDocument& Document, const int64 AbsoluteOffset, const int64 Size)
{
	FAssetDecodedPropertyValue Result;

	if (Size != sizeof(TValue) || !Document.IsValidRange(AbsoluteOffset, sizeof(TValue)))
	{
		Result.Error = TEXT("Invalid integral value range.");

		return Result;
	}

	TValue Value{};

	FMemory::Memcpy(&Value, Document.FileData.GetData() + AbsoluteOffset, sizeof(TValue));

	Result.bSuccess = true;

	if constexpr (TIsSigned<TValue>::Value)
	{
		Result.Value = LexToString(static_cast<int64>(Value));
	}
	else
	{
		Result.Value = LexToString(static_cast<uint64>(Value));
	}

	return Result;
}

static FAssetDecodedPropertyValue DecodeBool(const FAssetPackageDocument& Document, const FAssetSerializationTraceNode& Node, const int64 AbsoluteOffset)
{
	FAssetDecodedPropertyValue Result;

	if (Node.bHasInlineBoolValue)
	{
		Result.bSuccess = true;
		Result.Value = Node.bInlineBoolValue ? TEXT("true") : TEXT("false");

		return Result;
	}

	/*
	 * Fallback for formats where the bool value
	 * is actually stored in the value payload.
	 */
	if (Node.Size == sizeof(uint8) && Document.IsValidRange(AbsoluteOffset, sizeof(uint8)))
	{
		const uint8 Value = Document.FileData[AbsoluteOffset];

		Result.bSuccess = true;
		Result.Value = Value != 0 ? TEXT("true") : TEXT("false");

		return Result;
	}

	Result.Error = TEXT("Bool value is not available.");

	return Result;
}

static FAssetDecodedPropertyValue DecodeFloat(const FAssetPackageDocument& Document, const int64 AbsoluteOffset, const int64 Size)
{
	FAssetDecodedPropertyValue Result;

	if (Size != sizeof(float) || !Document.IsValidRange(AbsoluteOffset, sizeof(float)))
	{
		Result.Error = TEXT("Invalid float range.");

		return Result;
	}

	float Value = 0.0f;

	FMemory::Memcpy(&Value, Document.FileData.GetData() + AbsoluteOffset, sizeof(float));

	Result.bSuccess = true;

	Result.Value = FString::Printf(TEXT("%.9g"), static_cast<double>(Value));

	return Result;
}

static FAssetDecodedPropertyValue DecodeDouble(const FAssetPackageDocument& Document, const int64 AbsoluteOffset, const int64 Size)
{
	FAssetDecodedPropertyValue Result;

	if (Size != sizeof(double) || !Document.IsValidRange(AbsoluteOffset, sizeof(double)))
	{
		Result.Error = TEXT("Invalid double range.");

		return Result;
	}

	double Value = 0.0;

	FMemory::Memcpy(&Value, Document.FileData.GetData() + AbsoluteOffset, sizeof(double));

	Result.bSuccess = true;

	Result.Value = FString::Printf(TEXT("%.17g"), Value);

	return Result;
}

static FAssetDecodedPropertyValue DecodeName(const FAssetPackageDocument& Document, const int64 AbsoluteOffset, const int64 Size)
{
	FAssetDecodedPropertyValue Result;

	if (Size != sizeof(int32) * 2 || !Document.IsValidRange(AbsoluteOffset, Size))
	{
		Result.Error = TEXT("Invalid FName value range.");

		return Result;
	}

	int32 NameIndex = INDEX_NONE;
	int32 Number = 0;

	FMemory::Memcpy(&NameIndex, Document.FileData.GetData() + AbsoluteOffset, sizeof(int32));
	FMemory::Memcpy(&Number, Document.FileData.GetData() + AbsoluteOffset + sizeof(int32), sizeof(int32));

	if (!Document.NameMap.IsValidIndex(NameIndex))
	{
		Result.Error = FString::Printf(TEXT("Invalid name index %d."), NameIndex);

		return Result;
	}

	FAssetPackageNameReference Reference;
	Reference.NameIndex = NameIndex;
	Reference.Number = Number;

	Result.bSuccess = true;

	Result.Value = Document.ResolveNameReference(Reference);

	return Result;
}

static FAssetDecodedPropertyValue DecodeString(const FAssetPackageDocument& Document, const int64 AbsoluteOffset, const int64 Size)
{
	FAssetDecodedPropertyValue Result;

	if (Size <= 0 || !Document.IsValidRange(AbsoluteOffset, Size))
	{
		Result.Error = TEXT("Invalid FString value range.");

		return Result;
	}

	FAssetPackageMemoryReader Reader(Document.FileData, AbsoluteOffset, Size);

	FString Value;
	FText Error;

	if (!AssetSerializationPrimitives::ReadSerializedString(Reader, Value, Error))
	{
		Result.Error = Error.ToString();

		return Result;
	}

	Result.bSuccess = true;
	Result.Value = MoveTemp(Value);

	return Result;
}

static FAssetDecodedPropertyValue DecodeGuid(const FAssetPackageDocument& Document, const int64 AbsoluteOffset, const int64 Size)
{
	FAssetDecodedPropertyValue Result;

	if (Size != sizeof(FGuid) || !Document.IsValidRange(AbsoluteOffset, sizeof(FGuid)))
	{
		Result.Error = TEXT("Invalid FGuid range.");

		return Result;
	}

	FGuid Value;
	FMemory::Memcpy(&Value, Document.FileData.GetData() + AbsoluteOffset, sizeof(FGuid));
	Result.bSuccess = true;
	Result.Value = Value.ToString(EGuidFormats::DigitsWithHyphens);

	return Result;
}

FAssetDecodedPropertyValue FAssetPropertyValueDecoder::Decode(const FAssetPackageDocument& Document, const FAssetSerializationTraceNode& Node, const int64 ExportSerialOffset)
{
	const int64 AbsoluteOffset = ExportSerialOffset + Node.Offset;

	if (Node.TypeName == TEXT("BoolProperty"))
	{
		return DecodeBool(Document, Node, AbsoluteOffset);
	}

	if (Node.TypeName == TEXT("Int8Property"))
	{
		return DecodeIntegral<int8>(Document, AbsoluteOffset, Node.Size);
	}

	if (Node.TypeName == TEXT("Int16Property"))
	{
		return DecodeIntegral<int16>(Document, AbsoluteOffset, Node.Size);
	}

	if (Node.TypeName == TEXT("IntProperty"))
	{
		return DecodeIntegral<int32>(Document, AbsoluteOffset, Node.Size);
	}

	if (Node.TypeName == TEXT("Int64Property"))
	{
		return DecodeIntegral<int64>(Document, AbsoluteOffset, Node.Size);
	}

	if (Node.TypeName == TEXT("UInt16Property"))
	{
		return DecodeIntegral<uint16>(Document, AbsoluteOffset, Node.Size);
	}

	if (Node.TypeName == TEXT("UInt32Property"))
	{
		return DecodeIntegral<uint32>(Document, AbsoluteOffset, Node.Size);
	}

	if (Node.TypeName == TEXT("UInt64Property"))
	{
		return DecodeIntegral<uint64>(Document, AbsoluteOffset, Node.Size);
	}

	if (Node.TypeName == TEXT("FloatProperty"))
	{
		return DecodeFloat(Document, AbsoluteOffset, Node.Size);
	}

	if (Node.TypeName == TEXT("DoubleProperty"))
	{
		return DecodeDouble(Document, AbsoluteOffset, Node.Size);
	}

	if (Node.TypeName == TEXT("NameProperty"))
	{
		return DecodeName(Document, AbsoluteOffset, Node.Size);
	}

	if (Node.TypeName == TEXT("StrProperty"))
	{
		return DecodeString(Document, AbsoluteOffset, Node.Size);
	}

	if (Node.TypeName.StartsWith(TEXT("StructProperty(Guid")))
	{
		return DecodeGuid(Document, AbsoluteOffset, Node.Size);
	}

	FAssetDecodedPropertyValue Result;

	Result.Error = FString::Printf(TEXT("Unsupported type: %s"), *Node.TypeName);

	return Result;
}
