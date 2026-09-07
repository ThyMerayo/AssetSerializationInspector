// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetPropertyValueDecoder.h"

#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackagePayloadReader.h"
#include "Readers/AssetPackageReader.h"
#include "Serialization/AssetSerializationPrimitives.h"
#include "Serialization/AssetSerializedPropertyTag.h"

static FAssetDecodedPropertyValue DecodeBool(const FAssetPackageDocument& Document, const FAssetSerializationTraceNode& Node, const int64 AbsoluteOffset)
{
	FAssetDecodedPropertyValue Result;

	if (Node.bHasInlineBoolValue)
	{
		Result.Status = EAssetPropertyDecodeStatus::Success;
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

		Result.Status = EAssetPropertyDecodeStatus::Success;
		Result.Value = Value != 0 ? TEXT("true") : TEXT("false");
		return Result;
	}

	Result.Error = TEXT("Bool value is not available.");
	return Result;
}

template <typename TValue> static bool ReadBounded(FAssetPackagePayloadReader& Reader, const int64 ValueEnd, TValue& OutValue)
{
	if (Reader.Tell() + static_cast<int64>(sizeof(TValue)) > ValueEnd)
	{
		return false;
	}

	Reader << OutValue;

	return !Reader.IsError();
}

template <typename TValue> static bool DecodePrimitiveFromReader(FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	const int64 Start = Reader.Tell();

	TValue Value{};
	if (!ReadBounded(Reader, ValueEnd, Value))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Primitive value extends beyond its range.");
		return false;
	}

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Value = LexToString(Value);
	OutValue.RelativeOffset = Start;
	OutValue.Size = Reader.Tell() - Start;

	return true;
}

struct FAssetPropertyDecodeContext
{
	const FAssetPackageDocument& Document;

	int32 MaximumDepth = 32;
	int32 MaximumContainerElements = 100000;
};

static bool DecodeValueFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const FAssetSerializedPropertyType& Type, const int64 ValueEnd,
	FAssetDecodedPropertyValue& OutValue, const int32 Depth);

static bool DecodeTaggedStruct(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue, const int32 Depth)
{
	const int64 Start = Reader.Tell();

	while (!Reader.IsError() && Reader.Tell() < ValueEnd)
	{
		FAssetSerializedPropertyTag Tag;
		FText Error;

		if (!FAssetPropertyTagDecoder::ReadTag(Context.Document, Reader, Tag, Error))
		{
			OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
			OutValue.Error = Error.ToString();
			return false;
		}

		if (Tag.IsTerminator())
		{
			OutValue.Status = EAssetPropertyDecodeStatus::Success;
			OutValue.Value = FString::Printf(TEXT("%d fields"), OutValue.Children.Num());
			OutValue.RelativeOffset = Start;
			OutValue.Size = Reader.Tell() - Start;
			return true;
		}

		const int64 ChildValueEnd = Tag.ValueOffset + Tag.Size;

		if (ChildValueEnd > ValueEnd)
		{
			OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
			OutValue.Error = FString::Printf(TEXT("Struct field '%s' extends beyond the struct value range."), *Tag.ResolvedName);
			return false;
		}

		FAssetDecodedPropertyValue Child;
		Child.Name = Tag.ResolvedName;
		Child.TypeName = Tag.Type.ToString();

		if (Tag.Type.Name == TEXT("BoolProperty"))
		{
			Child.Status = EAssetPropertyDecodeStatus::Success;
			Child.Value = Tag.bBoolValue ? TEXT("true") : TEXT("false");
		}
		else
		{
			Reader.Seek(Tag.ValueOffset);
			DecodeValueFromReader(Context, Reader, Tag.Type, ChildValueEnd, Child, Depth);
		}

		OutValue.Children.Add(MoveTemp(Child));

		/*
		 * Always advance according to Tag.Size,
		 * regardless of how much our value decoder consumed.
		 */
		Reader.Seek(ChildValueEnd);
	}

	OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
	OutValue.Error = TEXT("Tagged struct ended without a None terminator.");
	return false;
}

template <typename TValue, typename TFormatter>
static bool DecodePodStructFromReader(
	const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue, TFormatter&& Formatter)
{
	const int64 Start = Reader.Tell();

	if (Start + static_cast<int64>(sizeof(TValue)) > ValueEnd)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Not enough space in reader for decoding Struct");
		return false;
	}

	TValue Value{};
	Reader << Value;

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Value = Formatter(Value);
	OutValue.RelativeOffset = Start;
	OutValue.Size = Reader.Tell() - Start;

	return true;
}

static bool DecodeGuidFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	const int64 Start = Reader.Tell();

	if (Start + static_cast<int64>(sizeof(FGuid)) > ValueEnd)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Not enough space in reader for decoding FGuid");
		return false;
	}

	FGuid Value;
	Reader << Value;

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Value = Value.ToString(EGuidFormats::DigitsWithHyphens);
	OutValue.RelativeOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

static bool DecodeTransformFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	// TODO: Not reading the payload properly, reads before the actual data

	const int64 Start = Reader.Tell();

	if (Start + static_cast<int64>(sizeof(FQuat) + 2 * sizeof(FVector)) > ValueEnd)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Not enough space in reader for decoding FTransform");
		return false;
	}

	Reader.Seek(Reader.Tell() + 6 * 8 + 1);
	FQuat Rotation;
	Reader << Rotation;

	Reader.Seek(Reader.Tell() + 6 * 8 + 1);
	FVector Translation;
	Reader << Translation;

	Reader.Seek(Reader.Tell() + 6 * 8 + 1);
	FVector Scale3D;
	Reader << Scale3D;

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Value = FString::Printf(TEXT("Rotation: %s\nTranslation: %s\nScale3D: %s"), *Rotation.ToString(), *Translation.ToString(), *Scale3D.ToString());
	OutValue.RelativeOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

static bool DecodeColorFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	const int64 Start = Reader.Tell();

	if (Start + static_cast<int64>(sizeof(FColor)) > ValueEnd)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Not enough space in reader for decoding FTransform");
		return false;
	}

	FColor Value;
	Reader << Value;

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Value = FString::Printf(TEXT("R=%u G=%u B=%u A=%u"), static_cast<uint32>(Value.R), static_cast<uint32>(Value.G), static_cast<uint32>(Value.B), static_cast<uint32>(Value.A));
	OutValue.RelativeOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

static bool TryDecodeKnownStruct(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const FString& StructName, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	if (StructName == TEXT("Vector"))
	{
		return DecodePodStructFromReader<FVector>(Context, Reader, ValueEnd, OutValue, [](const FVector& Value) { return Value.ToString(); });
	}

	if (StructName == TEXT("Vector2D"))
	{
		return DecodePodStructFromReader<FVector2D>(Context, Reader, ValueEnd, OutValue, [](const FVector2D& Value) { return Value.ToString(); });
	}

	if (StructName == TEXT("Vector4"))
	{
		return DecodePodStructFromReader<FVector4>(Context, Reader, ValueEnd, OutValue, [](const FVector4& Value) { return Value.ToString(); });
	}

	if (StructName == TEXT("Rotator"))
	{
		return DecodePodStructFromReader<FRotator>(Context, Reader, ValueEnd, OutValue, [](const FRotator& Value) { return Value.ToString(); });
	}

	if (StructName == TEXT("Quat"))
	{
		return DecodePodStructFromReader<FQuat>(Context, Reader, ValueEnd, OutValue, [](const FQuat& Value) { return Value.ToString(); });
	}

	if (StructName == TEXT("Guid"))
	{
		return DecodeGuidFromReader(Context, Reader, ValueEnd, OutValue);
	}

	if (StructName == TEXT("Transform"))
	{
		return DecodeTransformFromReader(Context, Reader, ValueEnd, OutValue);
	}

	if (StructName == TEXT("Color"))
	{
		return DecodeColorFromReader(Context, Reader, ValueEnd, OutValue);
	}

	if (StructName == TEXT("LinearColor"))
	{
		return DecodePodStructFromReader<FLinearColor>(Context, Reader, ValueEnd, OutValue, [](const FLinearColor& Value) {
			return FString::Printf(TEXT("R=%.9g G=%.9g B=%.9g A=%.9g"), static_cast<double>(Value.R), static_cast<double>(Value.G), static_cast<double>(Value.B), static_cast<double>(Value.A));
		});
	}

	if (StructName == TEXT("IntPoint"))
	{
		return DecodePodStructFromReader<FIntPoint>(Context, Reader, ValueEnd, OutValue, [](const FIntPoint& Value) { return FString::Printf(TEXT("X=%d Y=%d"), Value.X, Value.Y); });
	}

	if (StructName == TEXT("IntVector"))
	{
		return DecodePodStructFromReader<FIntVector>(Context, Reader, ValueEnd, OutValue, [](const FIntVector& Value) { return FString::Printf(TEXT("X=%d Y=%d Z=%d"), Value.X, Value.Y, Value.Z); });
	}

	return false;
}

static bool DecodeStructFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const FAssetSerializedPropertyType& Type, const int64 ValueEnd,
	FAssetDecodedPropertyValue& OutValue, const int32 Depth)
{
	if (Type.Parameters.IsEmpty())
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("StructProperty has no struct type.");
		return false;
	}

	const FString StructName = Type.Parameters[0].Name;
	if (TryDecodeKnownStruct(Context, Reader, StructName, ValueEnd, OutValue))
	{
		return true;
	}

	// TODO: Not working currently, needs to be fixed
	// return DecodeTaggedStruct(Context, Reader, ValueEnd, OutValue, Depth);

	OutValue.Status = EAssetPropertyDecodeStatus::Unsupported;
	OutValue.Error = TEXT("Generic StructProperty is not supported yet.");
	return false;
}

static bool DecodeArrayFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const FAssetSerializedPropertyType& Type, const int64 ValueEnd,
	FAssetDecodedPropertyValue& OutValue, const int32 Depth)
{
	if (Type.Parameters.Num() != 1)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("ArrayProperty does not contain exactly one inner type.");
		return false;
	}

	const int64 Start = Reader.Tell();
	int32 Count = 0;

	if (!ReadBounded(Reader, ValueEnd, Count))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Could not read array element count.");
		return false;
	}

	if (Count < 0 || Count > Context.MaximumContainerElements)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = FString::Printf(TEXT("Invalid array element count: %d."), Count);
		return false;
	}

	const FAssetSerializedPropertyType& InnerType = Type.Parameters[0];

	OutValue.Children.Reserve(Count);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		FAssetDecodedPropertyValue Element;
		Element.Name = FString::Printf(TEXT("[%d]"), Index);
		Element.TypeName = InnerType.ToString();

		if (!DecodeValueFromReader(Context, Reader, InnerType, ValueEnd, Element, Depth))
		{
			OutValue.Children.Add(MoveTemp(Element));
			OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
			OutValue.Error = FString::Printf(TEXT("Could not decode array element %d."), Index);
			return false;
		}

		OutValue.Children.Add(MoveTemp(Element));
	}

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Value = FString::Printf(TEXT("%d elements"), Count);
	OutValue.RelativeOffset = Start;
	OutValue.Size = Reader.Tell() - Start;

	return true;
}

static bool DecodeNameFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	const int64 Start = Reader.Tell();
	if (Start + static_cast<int64>(2 * sizeof(int32)) > ValueEnd)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Not enough space in reader for decoding FName");
		return false;
	}

	int32 NameIndex = INDEX_NONE;
	int32 Number = 0;

	Reader << NameIndex;
	Reader << Number;

	if (!Context.Document.NameMap.IsValidIndex(NameIndex))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = FString::Printf(TEXT("Could not find Name %d in NameMap"), NameIndex);
		return false;
	}

	FAssetPackageNameReference Reference;
	Reference.NameIndex = NameIndex;
	Reference.Number = Number;

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Value = Context.Document.ResolveNameReference(Reference);
	OutValue.RelativeOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

static bool DecodeStringFromReader(FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	const int64 Start = Reader.Tell();

	FString Value;
	FText Error;
	if (!AssetSerializationPrimitives::ReadSerializedString(Reader, Value, Error))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Could not decode FString");
		return false;
	}

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Value = MoveTemp(Value);
	OutValue.RelativeOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

static bool DecodeValueFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const FAssetSerializedPropertyType& Type, const int64 ValueEnd,
	FAssetDecodedPropertyValue& OutValue, const int32 Depth)
{
	if (Depth >= Context.MaximumDepth)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Maximum property nesting depth exceeded.");
		return false;
	}

	const int64 Start = Reader.Tell();

	if (Start > ValueEnd)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Property reader moved beyond its value range.");
		return false;
	}

	OutValue.TypeName = Type.ToString();

	if (Type.Name == TEXT("BoolProperty"))
	{
		return DecodePrimitiveFromReader<uint8>(Reader, ValueEnd, OutValue);
	}

	if (Type.Name == TEXT("IntProperty"))
	{
		return DecodePrimitiveFromReader<int32>(Reader, ValueEnd, OutValue);
	}

	if (Type.Name == TEXT("FloatProperty"))
	{
		return DecodePrimitiveFromReader<float>(Reader, ValueEnd, OutValue);
	}

	if (Type.Name == TEXT("DoubleProperty"))
	{
		return DecodePrimitiveFromReader<double>(Reader, ValueEnd, OutValue);
	}

	if (Type.Name == TEXT("NameProperty"))
	{
		return DecodeNameFromReader(Context, Reader, ValueEnd, OutValue);
	}

	if (Type.Name == TEXT("StrProperty"))
	{
		return DecodeStringFromReader(Reader, ValueEnd, OutValue);
	}

	if (Type.Name == TEXT("StructProperty"))
	{
		return DecodeStructFromReader(Context, Reader, Type, ValueEnd, OutValue, Depth + 1);
	}

	if (Type.Name == TEXT("ArrayProperty"))
	{
		return DecodeArrayFromReader(Context, Reader, Type, ValueEnd, OutValue, Depth + 1);
	}

	OutValue.Status = EAssetPropertyDecodeStatus::Unsupported;
	OutValue.Error = FString::Printf(TEXT("Unsupported property type: %s"), *Type.ToString());

	return false;
}

FAssetDecodedPropertyValue FAssetPropertyValueDecoder::Decode(const FAssetPackageDocument& Document, const FAssetSerializationTraceNode& Node, const int64 ExportSerialOffset)
{
	FAssetDecodedPropertyValue Result;

	const int64 AbsoluteOffset = ExportSerialOffset + Node.Offset;

	if (!Document.IsValidRange(AbsoluteOffset, Node.Size))
	{
		Result.Status = EAssetPropertyDecodeStatus::InvalidData;
		Result.Error = TEXT("The property value range is invalid.");
		return Result;
	}

	FAssetPackagePayloadReader Reader(Document, AbsoluteOffset, Node.Size);
	FAssetPropertyDecodeContext Context{ Document };

	Result.Name = Node.Name;
	Result.TypeName = Node.PropertyType.ToString();

	const int64 ValueEnd = AbsoluteOffset + Node.Size;
	if (Node.PropertyType.Name == TEXT("BoolProperty"))
	{
		return DecodeBool(Document, Node, AbsoluteOffset);
	}
	else if (!DecodeValueFromReader(Context, Reader, Node.PropertyType, ValueEnd, Result, 0))
	{
		return Result;
	}

	return Result;
}
