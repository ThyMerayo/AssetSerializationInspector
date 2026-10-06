// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetPropertyValueDecoder.h"

#include "UObject/CoreObjectVersion.h"
#include "UObject/EditorObjectVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"
#include "UObject/FrameworkObjectVersion.h"
#include "UObject/ObjectVersion.h"
#include "UObject/SequencerObjectVersion.h"
#include "UObject/UE5ReleaseStreamObjectVersion.h"

#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackagePayloadReader.h"
#include "Readers/AssetPackageReader.h"
#include "Serialization/AssetGraphNodePins.h"
#include "Serialization/AssetNumberText.h"
#include "Serialization/AssetPropertyTagDecoder.h"
#include "Serialization/AssetSchemaReflection.h"
#include "Serialization/AssetSerializationPrimitives.h"
#include "Serialization/AssetSerializedPropertyTag.h"
#include "Serialization/AssetUnversionedProperties.h"
#include "Trace/AssetSerializationTrace.h"

static FAssetDecodedPropertyValue DecodeBool(const FAssetPackageDocument& Document, const FAssetSerializationTraceNode& Node, const int64 AbsoluteOffset)
{
	FAssetDecodedPropertyValue Result;

	if (Node.bHasInlineBoolValue)
	{
		Result.Status = EAssetPropertyDecodeStatus::Success;
		Result.Kind = EAssetDecodedValueKind::Scalar;
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
		Result.Kind = EAssetDecodedValueKind::Scalar;
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
	OutValue.Kind = EAssetDecodedValueKind::Scalar;
	OutValue.Value = AssetNumberText::Text(Value);
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = Reader.Tell() - Start;

	return true;
}

struct FAssetPropertyDecodeContext
{
	const FAssetPackageDocument& Document;

	int32 MaximumDepth = 32;
	int32 MaximumContainerElements = 100000;

	/** Set by whoever read the property's tag for the next value decoded: the tag says its type writes the value itself. */
	mutable bool bNextValueIsNativelySerialized = false;

	/** The live struct whose tagged fields are being read, when the running editor has it; it names the struct elements older packages leave out. */
	const UStruct* OwnerStruct = nullptr;

	/** The package saved its properties without tags: enums are integers, bools are bytes, and structs have a header, not tags. */
	bool IsUnversioned() const { return AssetUnversionedProperties::IsUsedBy(Document); }
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
			OutValue.Kind = EAssetDecodedValueKind::Struct;
			OutValue.Value = FString::Printf(TEXT("%d fields"), OutValue.Children.Num());
			OutValue.AbsoluteOffset = Start;
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

		AssetSchemaReflection::CompleteType(Context.OwnerStruct, Tag.ResolvedName, Tag.Type);

		FAssetDecodedPropertyValue Child;
		Child.Name = Tag.ResolvedName;
		Child.TypeName = Tag.Type.ToString();

		if (Tag.Type.Name == TEXT("BoolProperty"))
		{
			Child.Status = EAssetPropertyDecodeStatus::Success;
			Child.Kind = EAssetDecodedValueKind::Scalar;
			Child.Value = Tag.bBoolValue ? TEXT("true") : TEXT("false");
		}
		else
		{
			Reader.Seek(Tag.ValueOffset);
			Context.bNextValueIsNativelySerialized = Tag.SerializeType == EAssetPropertyTagSerializeType::BinaryOrNative;
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
	OutValue.Kind = EAssetDecodedValueKind::Struct;
	OutValue.Value = Formatter(Value);
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = Reader.Tell() - Start;

	return true;
}

/** A box is its minimum and maximum corners followed by a one byte "is valid" flag. */
template <typename TVector> static bool DecodeBoxFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	const int64 Start = Reader.Tell();

	if (Start + static_cast<int64>(2 * sizeof(TVector) + sizeof(uint8)) > ValueEnd)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Not enough space in reader for decoding a box");
		return false;
	}

	TVector Min;
	TVector Max;
	uint8 bIsValid = 0;
	Reader << Min;
	Reader << Max;
	Reader << bIsValid;

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Kind = EAssetDecodedValueKind::Struct;
	OutValue.Value = FString::Printf(TEXT("Min=(%s) Max=(%s) IsValid=%d"), *AssetNumberText::Text(Min), *AssetNumberText::Text(Max), bIsValid != 0 ? 1 : 0);
	OutValue.AbsoluteOffset = Start;
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
	OutValue.Kind = EAssetDecodedValueKind::Struct;
	OutValue.Value = Value.ToString(EGuidFormats::DigitsWithHyphens);
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

// Currently not used, left in case there are scenarios in which using the DecodeTagStruct does not work for FTransform
static bool DecodeTransformFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	const int64 Start = Reader.Tell();

	if (Start + static_cast<int64>(sizeof(FQuat) + 2 * sizeof(FVector)) > ValueEnd)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Not enough space in reader for decoding FTransform");
		return false;
	}

	FQuat Rotation;
	Reader << Rotation;
	FVector Translation;
	Reader << Translation;
	FVector Scale3D;
	Reader << Scale3D;

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Kind = EAssetDecodedValueKind::Struct;
	OutValue.Value = FString::Printf(TEXT("Rotation: %s\nTranslation: %s\nScale3D: %s"), *AssetNumberText::Text(Rotation), *AssetNumberText::Text(Translation), *AssetNumberText::Text(Scale3D));
	OutValue.AbsoluteOffset = Start;
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
	OutValue.Kind = EAssetDecodedValueKind::Struct;
	OutValue.Value = FString::Printf(TEXT("R=%u G=%u B=%u A=%u"), static_cast<uint32>(Value.R), static_cast<uint32>(Value.G), static_cast<uint32>(Value.B), static_cast<uint32>(Value.A));
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

bool DecodeEdGraphPinTypeFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	const int64 Start = Reader.Tell();

	// The engine's own serializer finds where the value ends; the description is read separately because the engine's value holds
	// objects that were never resolved. The description is used only when it ends at the same byte.
	FEdGraphPinType Value{};
	Value.Serialize(Reader);
	const int64 End = Reader.Tell();

	FString Description;
	int64 DescribedEnd = 0;
	const bool bDescribed = AssetGraphNodePins::ReadPinType(Context.Document, Start, ValueEnd, Description, DescribedEnd) && DescribedEnd == End;

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Kind = EAssetDecodedValueKind::Struct;
	OutValue.Value = bDescribed ? Description : FString(TEXT("EdGraphPinType value"));
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = End - Start;
	return true;
}

static bool DecodeNameFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue);
static bool DecodeSoftObjectPathFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue);

static bool IsValidContainerCount(const FAssetPropertyDecodeContext& Context, const int32 Count);
static FAssetDecodedPropertyValue MakeTextChild(const FString& Name, const FString& TypeName, const FString& Value, const int64 Offset, const int64 Size);
static bool DecodePackageIndexFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue);

/** FGameplayTagContainer::Serialize writes its tags as an array of names, not as tagged properties. */
static bool DecodeNameArrayStructFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	const int64 Start = Reader.Tell();

	int32 Count = 0;
	if (!ReadBounded(Reader, ValueEnd, Count) || !IsValidContainerCount(Context, Count))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Could not read the tag count.");
		return false;
	}

	OutValue.Children.Reserve(Count);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		FAssetDecodedPropertyValue Element;
		Element.Name = FString::Printf(TEXT("[%d]"), Index);
		Element.TypeName = TEXT("NameProperty");

		if (!DecodeNameFromReader(Context, Reader, ValueEnd, Element))
		{
			OutValue.Children.Add(MoveTemp(Element));
			OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
			OutValue.Error = FString::Printf(TEXT("Could not decode tag %d."), Index);
			return false;
		}

		Element.SemanticKey = FAssetPropertyValueDecoder::BuildSemanticValueKey(Element);
		OutValue.Children.Add(MoveTemp(Element));
	}

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Kind = EAssetDecodedValueKind::Array;
	OutValue.Value = FString::Printf(TEXT("%d elements"), Count);
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

/** A struct made of named scalar fields, read one after the other from a native (binary) layout. */
/**
 * FPerPlatformInt / FPerPlatformFloat / FPerPlatformBool (PerPlatformPropertiesImpl.inl): a 32 bit "strip" flag, the default
 * value and, unless the flag is set, a map from platform name to value. Bools are 32 bit in a binary archive.
 */
template <typename TValue>
static bool DecodePerPlatformFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	const int64 Start = Reader.Tell();

	const auto Format = [](const TValue Value) { return AssetNumberText::Text(Value); };
	const auto Fail = [&OutValue](const TCHAR* Message) {
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = Message;
		return false;
	};

	uint32 bStripped = 0;
	TValue Default{};
	if (!ReadBounded(Reader, ValueEnd, bStripped) || !ReadBounded(Reader, ValueEnd, Default))
	{
		return Fail(TEXT("Not enough space in reader for decoding a per-platform value."));
	}

	OutValue.Kind = EAssetDecodedValueKind::Struct;
	OutValue.Children.Add(MakeTextChild(TEXT("Default"), TEXT("ScalarProperty"), Format(Default), Start + sizeof(uint32), sizeof(TValue)));
	FString Summary = Format(Default);

	if (bStripped == 0)
	{
		int32 Count = 0;
		if (!ReadBounded(Reader, ValueEnd, Count) || !IsValidContainerCount(Context, Count))
		{
			return Fail(TEXT("Could not read the per-platform value count."));
		}

		for (int32 Index = 0; Index < Count; ++Index)
		{
			FAssetDecodedPropertyValue Platform;
			TValue Value{};
			if (!DecodeNameFromReader(Context, Reader, ValueEnd, Platform) || !ReadBounded(Reader, ValueEnd, Value))
			{
				return Fail(TEXT("Could not read a per-platform value."));
			}

			OutValue.Children.Add(MakeTextChild(Platform.Value, TEXT("ScalarProperty"), Format(Value), Platform.AbsoluteOffset, Reader.Tell() - Platform.AbsoluteOffset));
			Summary += FString::Printf(TEXT(", %s=%s"), *Platform.Value, *Format(Value));
		}
	}

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Value = Summary;
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

/** FRichCurveKey::Serialize: three mode bytes, then Time, Value and the arrive and leave tangents with their weights. */
static bool DecodeRichCurveKeyFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	const int64 Start = Reader.Tell();

	uint8 Modes[3] = { 0, 0, 0 };
	float Numbers[6] = { 0, 0, 0, 0, 0, 0 };
	bool bRead = true;
	for (uint8& Mode : Modes)
	{
		bRead = bRead && ReadBounded(Reader, ValueEnd, Mode);
	}
	for (float& Number : Numbers)
	{
		bRead = bRead && ReadBounded(Reader, ValueEnd, Number);
	}

	if (!bRead)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Not enough space in reader for decoding a rich curve key.");
		return false;
	}

	static const TCHAR* const ModeNames[] = { TEXT("InterpMode"), TEXT("TangentMode"), TEXT("TangentWeightMode") };
	static const TCHAR* const NumberNames[] = { TEXT("Time"), TEXT("Value"), TEXT("ArriveTangent"), TEXT("ArriveTangentWeight"), TEXT("LeaveTangent"), TEXT("LeaveTangentWeight") };

	OutValue.Kind = EAssetDecodedValueKind::Struct;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		OutValue.Children.Add(MakeTextChild(ModeNames[Index], TEXT("ByteProperty"), AssetNumberText::Text(static_cast<uint32>(Modes[Index])), Start + Index, 1));
	}
	for (int32 Index = 0; Index < 6; ++Index)
	{
		OutValue.Children.Add(MakeTextChild(NumberNames[Index], TEXT("FloatProperty"), AssetNumberText::Text(Numbers[Index]), Start + 3 + Index * sizeof(float), sizeof(float)));
	}

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Value = FString::Printf(TEXT("(Time=%s, Value=%s)"), *AssetNumberText::Text(Numbers[0]), *AssetNumberText::Text(Numbers[1]));
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

/** The constants a material input stores after the expression it is connected to, by input kind. */
enum class EMaterialInputConstant : uint8
{
	None,
	LinearColor,
	Float,
	Vector3,
	Vector2,
	UInt32
};

/**
 * FExpressionInput and the material inputs derived from it (SerializeExpressionInput in MaterialShared.cpp): the expression
 * (a package index), the output index, the input name and five mask ints, then for the typed inputs a 32 bit "use constant"
 * flag and the constant. Packages older than the native serialization store them as tagged properties, which is not handled.
 */
static bool DecodeExpressionInputFromReader(
	const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue, const EMaterialInputConstant Constant)
{
	const int64 Start = Reader.Tell();

	if (Reader.CustomVer(FCoreObjectVersion::GUID) < FCoreObjectVersion::MaterialInputNativeSerialize || Reader.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::PinsStoreFName)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::Unsupported;
		OutValue.Error = TEXT("This material input was saved before it had its own serialization.");
		return false;
	}

	const auto Fail = [&OutValue](const TCHAR* Message) {
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = Message;
		return false;
	};

	FAssetDecodedPropertyValue Expression;
	FAssetDecodedPropertyValue InputName;
	int32 OutputIndex = 0;
	int32 Masks[5] = { 0, 0, 0, 0, 0 };

	if (!DecodePackageIndexFromReader(Context, Reader, ValueEnd, Expression) || !ReadBounded(Reader, ValueEnd, OutputIndex) || !DecodeNameFromReader(Context, Reader, ValueEnd, InputName))
	{
		return Fail(TEXT("Could not read a material input."));
	}

	for (int32& Mask : Masks)
	{
		if (!ReadBounded(Reader, ValueEnd, Mask))
		{
			return Fail(TEXT("Could not read a material input."));
		}
	}

	OutValue.Kind = EAssetDecodedValueKind::Struct;
	OutValue.Children.Add(MakeTextChild(TEXT("Expression"), TEXT("ObjectProperty"), Expression.Value, Expression.AbsoluteOffset, Expression.Size));
	OutValue.Children.Add(MakeTextChild(TEXT("OutputIndex"), TEXT("IntProperty"), AssetNumberText::Text(OutputIndex), Start, sizeof(int32)));
	OutValue.Children.Add(MakeTextChild(TEXT("InputName"), TEXT("NameProperty"), InputName.Value, InputName.AbsoluteOffset, InputName.Size));

	static const TCHAR* const MaskNames[] = { TEXT("Mask"), TEXT("MaskR"), TEXT("MaskG"), TEXT("MaskB"), TEXT("MaskA") };
	for (int32 Index = 0; Index < 5; ++Index)
	{
		OutValue.Children.Add(MakeTextChild(MaskNames[Index], TEXT("IntProperty"), AssetNumberText::Text(Masks[Index]), INDEX_NONE, sizeof(int32)));
	}

	if (Constant != EMaterialInputConstant::None)
	{
		uint32 bUseConstant = 0;
		if (!ReadBounded(Reader, ValueEnd, bUseConstant))
		{
			return Fail(TEXT("Could not read a material input."));
		}

		FString ConstantText;
		bool bRead = true;

		const auto Floats = [&](const int32 Count) {
			TArray<FString> Parts;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				float Number = 0.0f;
				bRead = bRead && ReadBounded(Reader, ValueEnd, Number);
				Parts.Add(AssetNumberText::Text(Number));
			}
			return FString::Join(Parts, TEXT(", "));
		};

		switch (Constant)
		{
			case EMaterialInputConstant::LinearColor:
				// Inputs saved before MaterialInputUsesLinearColor hold an 8 bit color.
				if (Reader.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::MaterialInputUsesLinearColor)
				{
					uint32 Color = 0;
					bRead = ReadBounded(Reader, ValueEnd, Color);
					ConstantText = FString::Printf(TEXT("0x%08X"), Color);
				}
				else
				{
					ConstantText = Floats(4);
				}
				break;

			case EMaterialInputConstant::Float:
				ConstantText = Floats(1);
				break;

			case EMaterialInputConstant::Vector3:
				ConstantText = Floats(3);
				break;

			case EMaterialInputConstant::Vector2:
				ConstantText = Floats(2);
				break;

			default:
			{
				uint32 Value = 0;
				bRead = ReadBounded(Reader, ValueEnd, Value);
				ConstantText = AssetNumberText::Text(Value);
				break;
			}
		}

		if (!bRead)
		{
			return Fail(TEXT("Could not read a material input."));
		}

		OutValue.Children.Add(MakeTextChild(TEXT("UseConstant"), TEXT("BoolProperty"), bUseConstant != 0 ? TEXT("true") : TEXT("false"), INDEX_NONE, sizeof(uint32)));
		OutValue.Children.Add(MakeTextChild(TEXT("Constant"), TEXT("ScalarProperty"), ConstantText, INDEX_NONE, 0));
	}

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Value = Expression.Value == TEXT("None") ? FString(TEXT("(not connected)")) : FString::Printf(TEXT("%s [output %d]"), *Expression.Value, OutputIndex);
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

/** FMovieSceneFrameRange::Serialize writes a TRange<FFrameNumber>: each bound is a bound-type byte and a frame number. */
static bool DecodeFrameRangeFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	const int64 Start = Reader.Tell();

	uint8 LowerType = 0;
	int32 Lower = 0;
	uint8 UpperType = 0;
	int32 Upper = 0;
	if (!ReadBounded(Reader, ValueEnd, LowerType) || !ReadBounded(Reader, ValueEnd, Lower) || !ReadBounded(Reader, ValueEnd, UpperType) || !ReadBounded(Reader, ValueEnd, Upper))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Not enough space in reader for decoding a frame range.");
		return false;
	}

	// ERangeBoundTypes: 0 exclusive, 1 inclusive, 2 open.
	const FString LowerText = LowerType == 2 ? FString(TEXT("(-inf")) : FString::Printf(TEXT("%s%d"), LowerType == 1 ? TEXT("[") : TEXT("("), Lower);
	const FString UpperText = UpperType == 2 ? FString(TEXT("+inf)")) : FString::Printf(TEXT("%d%s"), Upper, UpperType == 1 ? TEXT("]") : TEXT(")"));

	OutValue.Kind = EAssetDecodedValueKind::Struct;
	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Value = FString::Printf(TEXT("%s, %s"), *LowerText, *UpperText);
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

/**
 * FMovieSceneFloatChannel / FMovieSceneDoubleChannel (TMovieSceneCurveChannelImpl::Serialize): the extrapolation modes, the key
 * times and key values as raw arrays (each prefixed with its element size), the default value and the tick resolution. Only
 * each key's own value is read; the tangents and modes that follow it in the element are skipped.
 */
template <typename TValue>
static bool DecodeMovieSceneChannelFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	const int64 Start = Reader.Tell();

	const auto Fail = [&OutValue](const TCHAR* Message) {
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = Message;
		return false;
	};

	if (Reader.CustomVer(FSequencerObjectVersion::GUID) < FSequencerObjectVersion::SerializeFloatChannelCompletely
		&& Reader.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::SerializeFloatChannelShowCurve)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::Unsupported;
		OutValue.Error = TEXT("This channel was saved before it was serialized completely.");
		return false;
	}

	uint8 PreExtrapolation = 0;
	uint8 PostExtrapolation = 0;
	int32 TimeSize = 0;
	int32 TimeCount = 0;
	if (!ReadBounded(Reader, ValueEnd, PreExtrapolation) || !ReadBounded(Reader, ValueEnd, PostExtrapolation) || !ReadBounded(Reader, ValueEnd, TimeSize) || !ReadBounded(Reader, ValueEnd, TimeCount)
		|| TimeSize != sizeof(int32) || !IsValidContainerCount(Context, TimeCount) || Reader.Tell() + static_cast<int64>(TimeCount) * TimeSize > ValueEnd)
	{
		return Fail(TEXT("Could not read the key times of a channel."));
	}

	TArray<int32> Times;
	Times.SetNumUninitialized(TimeCount);
	if (TimeCount > 0)
	{
		Reader.Serialize(Times.GetData(), static_cast<int64>(TimeCount) * TimeSize);
	}

	int32 ValueSize = 0;
	int32 ValueCount = 0;
	if (!ReadBounded(Reader, ValueEnd, ValueSize) || !ReadBounded(Reader, ValueEnd, ValueCount) || ValueSize < static_cast<int32>(sizeof(TValue)) || ValueCount != TimeCount
		|| Reader.Tell() + static_cast<int64>(ValueCount) * ValueSize > ValueEnd)
	{
		return Fail(TEXT("Could not read the key values of a channel."));
	}

	OutValue.Kind = EAssetDecodedValueKind::Struct;

	FAssetDecodedPropertyValue Keys;
	Keys.Status = EAssetPropertyDecodeStatus::Success;
	Keys.Kind = EAssetDecodedValueKind::Array;
	Keys.Name = TEXT("Keys");
	Keys.TypeName = TEXT("ArrayProperty");
	Keys.Value = FString::Printf(TEXT("%d keys"), TimeCount);
	Keys.AbsoluteOffset = Reader.Tell();

	for (int32 Index = 0; Index < TimeCount; ++Index)
	{
		TValue Value{};
		FMemory::Memcpy(&Value, Reader.Tell() + Context.Document.FileData.GetData(), sizeof(TValue));
		Reader.Seek(Reader.Tell() + ValueSize);

		FAssetDecodedPropertyValue Key = MakeTextChild(
			FString::Printf(TEXT("[%d]"), Index), TEXT("KeyProperty"), FString::Printf(TEXT("frame %d: %s"), Times[Index], *FString::SanitizeFloat(static_cast<double>(Value))), INDEX_NONE, ValueSize);
		Key.SemanticKey = FAssetPropertyValueDecoder::BuildSemanticValueKey(Key);
		Keys.Children.Add(MoveTemp(Key));
	}

	Keys.Size = Reader.Tell() - Keys.AbsoluteOffset;

	TValue Default{};
	uint32 bHasDefault = 0;
	int32 Numerator = 0;
	int32 Denominator = 0;
	if (!ReadBounded(Reader, ValueEnd, Default) || !ReadBounded(Reader, ValueEnd, bHasDefault) || !ReadBounded(Reader, ValueEnd, Numerator) || !ReadBounded(Reader, ValueEnd, Denominator))
	{
		return Fail(TEXT("Could not read the default value of a channel."));
	}

	if (Reader.CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::SerializeFloatChannelShowCurve)
	{
		uint32 bShowCurve = 0;
		if (!ReadBounded(Reader, ValueEnd, bShowCurve))
		{
			return Fail(TEXT("Could not read a channel."));
		}
	}

	OutValue.Children.Add(MakeTextChild(TEXT("PreInfinityExtrap"), TEXT("ByteProperty"), AssetNumberText::Text(static_cast<uint32>(PreExtrapolation)), INDEX_NONE, 1));
	OutValue.Children.Add(MakeTextChild(TEXT("PostInfinityExtrap"), TEXT("ByteProperty"), AssetNumberText::Text(static_cast<uint32>(PostExtrapolation)), INDEX_NONE, 1));
	OutValue.Children.Add(MoveTemp(Keys));
	OutValue.Children.Add(
		MakeTextChild(TEXT("DefaultValue"), TEXT("ScalarProperty"), bHasDefault != 0 ? FString::SanitizeFloat(static_cast<double>(Default)) : FString(TEXT("none")), INDEX_NONE, sizeof(TValue)));
	OutValue.Children.Add(MakeTextChild(TEXT("TickResolution"), TEXT("FrameRate"), FString::Printf(TEXT("%d/%d"), Numerator, Denominator), INDEX_NONE, 8));

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Value = FString::Printf(TEXT("%d keys"), TimeCount);
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

/**
 * FNiagaraVariableBase / FNiagaraVariable / FNiagaraVariableWithOffset: the name, the type definition (a stream of tagged
 * properties ending in "None"), then for a variable its data as a byte array, or for the one with an offset the offset.
 */
static bool DecodeNiagaraVariableFromReader(
	const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue, const int32 Kind)
{
	const int64 Start = Reader.Tell();

	const auto Fail = [&OutValue](const TCHAR* Message) {
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = Message;
		return false;
	};

	FAssetDecodedPropertyValue Name;
	if (!DecodeNameFromReader(Context, Reader, ValueEnd, Name))
	{
		return Fail(TEXT("Could not read the name of a Niagara variable."));
	}

	FAssetDecodedPropertyValue TypeDefinition;
	TypeDefinition.Name = TEXT("TypeDefinition");
	TypeDefinition.TypeName = TEXT("StructProperty(NiagaraTypeDefinition)");
	if (!DecodeTaggedStruct(Context, Reader, ValueEnd, TypeDefinition, 1))
	{
		return Fail(TEXT("Could not read the type of a Niagara variable."));
	}

	OutValue.Kind = EAssetDecodedValueKind::Struct;
	OutValue.Children.Add(MakeTextChild(TEXT("Name"), TEXT("NameProperty"), Name.Value, Name.AbsoluteOffset, Name.Size));
	OutValue.Children.Add(MoveTemp(TypeDefinition));

	if (Kind == 1)
	{
		const int64 DataStart = Reader.Tell();
		int32 DataSize = 0;
		if (!ReadBounded(Reader, ValueEnd, DataSize) || DataSize < 0 || Reader.Tell() + DataSize > ValueEnd)
		{
			return Fail(TEXT("Could not read the data of a Niagara variable."));
		}

		Reader.Seek(Reader.Tell() + DataSize);
		OutValue.Children.Add(MakeTextChild(TEXT("VarData"), TEXT("ByteProperty"), FString::Printf(TEXT("%d bytes"), DataSize), DataStart, Reader.Tell() - DataStart));
	}
	else if (Kind == 2)
	{
		int32 Offset = 0;
		if (!ReadBounded(Reader, ValueEnd, Offset))
		{
			return Fail(TEXT("Could not read the offset of a Niagara variable."));
		}

		OutValue.Children.Add(MakeTextChild(TEXT("Offset"), TEXT("IntProperty"), AssetNumberText::Text(Offset), Reader.Tell() - sizeof(int32), sizeof(int32)));
	}

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Value = Name.Value;
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

static bool TryDecodeKnownStruct(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const FString& StructName, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	if (StructName == TEXT("Vector"))
	{
		return DecodePodStructFromReader<FVector>(Context, Reader, ValueEnd, OutValue, [](const FVector& Value) { return AssetNumberText::Text(Value); });
	}

	if (StructName == TEXT("Vector2D"))
	{
		return DecodePodStructFromReader<FVector2D>(Context, Reader, ValueEnd, OutValue, [](const FVector2D& Value) { return AssetNumberText::Text(Value); });
	}

	if (StructName == TEXT("Vector2f") || StructName == TEXT("DeprecateSlateVector2D"))
	{
		return DecodePodStructFromReader<FVector2f>(Context, Reader, ValueEnd, OutValue, [](const FVector2f& Value) { return AssetNumberText::Text(Value); });
	}

	if (StructName == TEXT("GameplayTag"))
	{
		return DecodeNameFromReader(Context, Reader, ValueEnd, OutValue);
	}

	if (StructName == TEXT("IntVector2") || StructName == TEXT("Int32Vector2"))
	{
		return DecodePodStructFromReader<FIntPoint>(Context, Reader, ValueEnd, OutValue, [](const FIntPoint& Value) { return FString::Printf(TEXT("X=%d Y=%d"), Value.X, Value.Y); });
	}

	if (StructName == TEXT("IntVector4") || StructName == TEXT("Int32Vector4"))
	{
		return DecodePodStructFromReader<FInt32Vector4>(
			Context, Reader, ValueEnd, OutValue, [](const FInt32Vector4& Value) { return FString::Printf(TEXT("X=%d Y=%d Z=%d W=%d"), Value.X, Value.Y, Value.Z, Value.W); });
	}

	if (StructName == TEXT("FrameNumber"))
	{
		return DecodePodStructFromReader<int32>(Context, Reader, ValueEnd, OutValue, [](const int32 Value) { return AssetNumberText::Text(Value); });
	}

	if (StructName == TEXT("DateTime"))
	{
		return DecodePodStructFromReader<int64>(Context, Reader, ValueEnd, OutValue, [](const int64 Ticks) { return FDateTime(Ticks).ToIso8601(); });
	}

	if (StructName == TEXT("Vector3f"))
	{
		return DecodePodStructFromReader<FVector3f>(Context, Reader, ValueEnd, OutValue, [](const FVector3f& Value) { return AssetNumberText::Text(Value); });
	}

	if (StructName == TEXT("Vector4f"))
	{
		return DecodePodStructFromReader<FVector4f>(Context, Reader, ValueEnd, OutValue, [](const FVector4f& Value) { return AssetNumberText::Text(Value); });
	}

	if (StructName == TEXT("Quat4f"))
	{
		return DecodePodStructFromReader<FQuat4f>(Context, Reader, ValueEnd, OutValue, [](const FQuat4f& Value) { return AssetNumberText::Text(Value); });
	}

	if (StructName == TEXT("Rotator3f"))
	{
		return DecodePodStructFromReader<FRotator3f>(Context, Reader, ValueEnd, OutValue, [](const FRotator3f& Value) { return AssetNumberText::Text(Value); });
	}

	if (StructName == TEXT("ExpressionInput") || StructName == TEXT("MaterialAttributesInput"))
	{
		return DecodeExpressionInputFromReader(Context, Reader, ValueEnd, OutValue, EMaterialInputConstant::None);
	}

	if (StructName == TEXT("ColorMaterialInput"))
	{
		return DecodeExpressionInputFromReader(Context, Reader, ValueEnd, OutValue, EMaterialInputConstant::LinearColor);
	}

	if (StructName == TEXT("ScalarMaterialInput"))
	{
		return DecodeExpressionInputFromReader(Context, Reader, ValueEnd, OutValue, EMaterialInputConstant::Float);
	}

	if (StructName == TEXT("VectorMaterialInput"))
	{
		return DecodeExpressionInputFromReader(Context, Reader, ValueEnd, OutValue, EMaterialInputConstant::Vector3);
	}

	if (StructName == TEXT("Vector2MaterialInput"))
	{
		return DecodeExpressionInputFromReader(Context, Reader, ValueEnd, OutValue, EMaterialInputConstant::Vector2);
	}

	if (StructName == TEXT("ShadingModelMaterialInput") || StructName == TEXT("SubstrateMaterialInput"))
	{
		return DecodeExpressionInputFromReader(Context, Reader, ValueEnd, OutValue, EMaterialInputConstant::UInt32);
	}

	if (StructName == TEXT("MovieSceneFrameRange"))
	{
		return DecodeFrameRangeFromReader(Context, Reader, ValueEnd, OutValue);
	}

	if (StructName == TEXT("MovieSceneFloatChannel"))
	{
		return DecodeMovieSceneChannelFromReader<float>(Context, Reader, ValueEnd, OutValue);
	}

	if (StructName == TEXT("MovieSceneDoubleChannel"))
	{
		return DecodeMovieSceneChannelFromReader<double>(Context, Reader, ValueEnd, OutValue);
	}

	if (StructName == TEXT("NiagaraVariableBase"))
	{
		return DecodeNiagaraVariableFromReader(Context, Reader, ValueEnd, OutValue, 0);
	}

	if (StructName == TEXT("NiagaraVariable"))
	{
		return DecodeNiagaraVariableFromReader(Context, Reader, ValueEnd, OutValue, 1);
	}

	if (StructName == TEXT("NiagaraVariableWithOffset"))
	{
		return DecodeNiagaraVariableFromReader(Context, Reader, ValueEnd, OutValue, 2);
	}

	if (StructName == TEXT("PerPlatformInt"))
	{
		return DecodePerPlatformFromReader<int32>(Context, Reader, ValueEnd, OutValue);
	}

	if (StructName == TEXT("PerPlatformFloat"))
	{
		return DecodePerPlatformFromReader<float>(Context, Reader, ValueEnd, OutValue);
	}

	if (StructName == TEXT("PerPlatformBool"))
	{
		// A bool is 32 bits in a binary archive.
		return DecodePerPlatformFromReader<uint32>(Context, Reader, ValueEnd, OutValue);
	}

	if (StructName == TEXT("RichCurveKey") && Reader.UEVer() >= VER_UE4_SERIALIZE_RICH_CURVE_KEY)
	{
		return DecodeRichCurveKeyFromReader(Context, Reader, ValueEnd, OutValue);
	}

	if (StructName == TEXT("GameplayTagContainer"))
	{
		return DecodeNameArrayStructFromReader(Context, Reader, ValueEnd, OutValue);
	}

	if (StructName == TEXT("SoftObjectPath") || StructName == TEXT("SoftClassPath"))
	{
		return DecodeSoftObjectPathFromReader(Context, Reader, ValueEnd, OutValue);
	}

	if (StructName == TEXT("Box2f"))
	{
		return DecodeBoxFromReader<FVector2f>(Context, Reader, ValueEnd, OutValue);
	}

	if (StructName == TEXT("Box2D"))
	{
		return DecodeBoxFromReader<FVector2D>(Context, Reader, ValueEnd, OutValue);
	}

	if (StructName == TEXT("Box3f"))
	{
		return DecodeBoxFromReader<FVector3f>(Context, Reader, ValueEnd, OutValue);
	}

	if (StructName == TEXT("Box"))
	{
		return DecodeBoxFromReader<FVector>(Context, Reader, ValueEnd, OutValue);
	}

	if (StructName == TEXT("GameplayEffectVersion"))
	{
		// FGameplayEffectVersion::Serialize writes only its enum, as a raw byte and without property tags.
		return DecodePrimitiveFromReader<uint8>(Reader, ValueEnd, OutValue);
	}

	if (StructName == TEXT("Vector4"))
	{
		return DecodePodStructFromReader<FVector4>(Context, Reader, ValueEnd, OutValue, [](const FVector4& Value) { return AssetNumberText::Text(Value); });
	}

	if (StructName == TEXT("Rotator"))
	{
		return DecodePodStructFromReader<FRotator>(Context, Reader, ValueEnd, OutValue, [](const FRotator& Value) { return AssetNumberText::Text(Value); });
	}

	if (StructName == TEXT("Quat"))
	{
		return DecodePodStructFromReader<FQuat>(Context, Reader, ValueEnd, OutValue, [](const FQuat& Value) { return AssetNumberText::Text(Value); });
	}

	if (StructName == TEXT("Guid"))
	{
		return DecodeGuidFromReader(Context, Reader, ValueEnd, OutValue);
	}

	if (StructName == TEXT("Transform"))
	{
		return DecodeTaggedStruct(Context, Reader, ValueEnd, OutValue, 0);
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

	if (StructName == TEXT("EdGraphPinType"))
	{
		return DecodeEdGraphPinTypeFromReader(Context, Reader, ValueEnd, OutValue);
	}

	return false;
}

/** A struct of a package saved without tags: a property header and the values, in the order of the struct's property list. */
static bool DecodeUnversionedStructFromReader(
	const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const FString& StructName, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	const int64 Start = Reader.Tell();

	const UScriptStruct* Struct = Cast<UScriptStruct>(AssetSchemaReflection::FindNativeStruct(StructName));
	if (Struct == nullptr)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::Unsupported;
		OutValue.Error = FString::Printf(TEXT("%s is not a struct of this editor, and a package saved without tags cannot be read without its definition."), *StructName);
		return false;
	}

	if ((Struct->StructFlags & STRUCT_SerializeNative) != 0)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::Unsupported;
		OutValue.Error = FString::Printf(TEXT("%s is serialized by its own native code, whose layout this inspector does not know."), *StructName);
		return false;
	}

	TArray<FAssetUnversionedValue> Values;
	int64 EndOffset = Start;
	FString Error;
	const bool bRead = AssetUnversionedProperties::Read(Context.Document, Struct, Start, ValueEnd, Values, EndOffset, Error);

	for (FAssetUnversionedValue& Slot : Values)
	{
		FAssetDecodedPropertyValue Child = MoveTemp(Slot.Decoded);
		Child.Name = Slot.Property->ArrayDim > 1 ? FString::Printf(TEXT("%s[%d]"), *Slot.Property->GetName(), Slot.ArrayIndex) : Slot.Property->GetName();
		Child.TypeName = Slot.Type.ToString();
		Child.AbsoluteOffset = Slot.Offset;
		Child.Size = Slot.Size;
		OutValue.Children.Add(MoveTemp(Child));
	}

	// The nested reading used a reader of its own, so this one moves on by what it used.
	Reader.Seek(EndOffset);

	OutValue.Kind = EAssetDecodedValueKind::Struct;
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = EndOffset - Start;

	if (!bRead)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = Error;
		return false;
	}

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Value = FString::Printf(TEXT("%d fields"), OutValue.Children.Num());
	return true;
}

static bool DecodeStructFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const FAssetSerializedPropertyType& Type, const int64 ValueEnd,
	FAssetDecodedPropertyValue& OutValue, const int32 Depth, const bool bNativelySerialized = false)
{
	if (Type.Parameters.IsEmpty())
	{
		// Older packages do not name the struct of a map or set element. Try it as a tagged struct; one saved natively (a GUID,
		// for example) cannot be told apart without the class, which this reader does not have.
		if (Reader.UEVer() < EUnrealEngineObjectUE5Version::PROPERTY_TAG_COMPLETE_TYPE_NAME)
		{
			const int64 StructStart = Reader.Tell();
			FAssetDecodedPropertyValue Tagged;
			Tagged.TypeName = OutValue.TypeName;

			if (DecodeTaggedStruct(Context, Reader, ValueEnd, Tagged, Depth))
			{
				OutValue = MoveTemp(Tagged);
				return true;
			}

			Reader.Seek(StructStart);
			OutValue.Status = EAssetPropertyDecodeStatus::Unsupported;
			OutValue.Error = TEXT("The struct type of this element is not stored in packages saved before UE 5.4, and it is not a tagged struct.");
			return false;
		}

		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("StructProperty has no struct type.");
		return false;
	}

	const FString StructName = Type.Parameters[0].Name;
	if (TryDecodeKnownStruct(Context, Reader, StructName, ValueEnd, OutValue))
	{
		return true;
	}

	if (Context.IsUnversioned())
	{
		return DecodeUnversionedStructFromReader(Context, Reader, StructName, ValueEnd, OutValue);
	}

	if (bNativelySerialized)
	{
		// The type writes itself with its own serializer. Many of those serializers write the struct's properties as tagged
		// properties after all (to add a version fix-up, for example), so try that first, and accept it only when it reads
		// cleanly: every field decodes and the stream ends exactly where the value does. Anything else would be garbage.
		const int64 NativeStart = Reader.Tell();
		FAssetDecodedPropertyValue Tagged;
		Tagged.TypeName = OutValue.TypeName;

		if (DecodeTaggedStruct(Context, Reader, ValueEnd, Tagged, Depth) && Reader.Tell() == ValueEnd && !Tagged.Children.IsEmpty()
			&& !Tagged.Children.ContainsByPredicate([](const FAssetDecodedPropertyValue& Child) { return !Child.IsSuccess(); }))
		{
			OutValue = MoveTemp(Tagged);
			return true;
		}

		Reader.Seek(NativeStart);

		// The type writes itself; reading that as tagged properties would only produce garbage.
		OutValue.Status = EAssetPropertyDecodeStatus::Unsupported;
		OutValue.Error = FString::Printf(TEXT("%s is serialized by its own native code, whose layout this inspector does not know."), *StructName);
		return false;
	}

	FAssetPropertyDecodeContext FieldContext = Context;
	FieldContext.OwnerStruct = AssetSchemaReflection::FindNativeStruct(StructName);
	return DecodeTaggedStruct(FieldContext, Reader, ValueEnd, OutValue, Depth);
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

	FAssetSerializedPropertyType InnerType = Type.Parameters[0];

	// Before PROPERTY_TAG_COMPLETE_TYPE_NAME an array of structs carries a tag for the inner struct right after the count.
	if (!Context.IsUnversioned() && Reader.UEVer() < EUnrealEngineObjectUE5Version::PROPERTY_TAG_COMPLETE_TYPE_NAME && Reader.UEVer() >= VER_UE4_INNER_ARRAY_TAG_INFO
		&& InnerType.Name == TEXT("StructProperty"))
	{
		FAssetSerializedPropertyTag InnerTag;
		FText InnerError;

		if (!FAssetPropertyTagDecoder::ReadTag(Context.Document, Reader, InnerTag, InnerError) || Reader.Tell() > ValueEnd)
		{
			OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
			OutValue.Error = TEXT("Could not read the array's inner struct tag.");
			return false;
		}

		// The array's own tag only says "StructProperty"; the inner tag names the struct.
		if (InnerType.Parameters.IsEmpty())
		{
			InnerType = InnerTag.Type;
		}
	}

	OutValue.Children.Reserve(Count);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		FAssetDecodedPropertyValue Element;
		Element.Name = FString::Printf(TEXT("[%d]"), Index);
		Element.TypeName = InnerType.ToString();

		if (!DecodeValueFromReader(Context, Reader, InnerType, ValueEnd, Element, Depth))
		{
			// Keep what did decode: the elements before this one are good, and this one carries its own error.
			OutValue.Children.Add(MoveTemp(Element));
			OutValue.Status = EAssetPropertyDecodeStatus::Partial;
			OutValue.Kind = EAssetDecodedValueKind::Array;
			OutValue.Value = FString::Printf(TEXT("%d of %d elements decoded"), Index, Count);
			OutValue.Error = FString::Printf(TEXT("Could not decode array element %d."), Index);
			OutValue.AbsoluteOffset = Start;
			OutValue.Size = ValueEnd - Start;
			return false;
		}

		Element.SemanticKey = FAssetPropertyValueDecoder::BuildSemanticValueKey(Element);

		OutValue.Children.Add(MoveTemp(Element));
	}

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Kind = EAssetDecodedValueKind::Array;
	OutValue.Value = FString::Printf(TEXT("%d elements"), Count);
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = Reader.Tell() - Start;

	return true;
}

static bool IsValidContainerCount(const FAssetPropertyDecodeContext& Context, const int32 Count)
{
	return Count >= 0 && Count <= Context.MaximumContainerElements;
}

static bool DecodeSetFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const FAssetSerializedPropertyType& Type, const int64 ValueEnd,
	FAssetDecodedPropertyValue& OutValue, const int32 Depth)
{
	if (Depth >= Context.MaximumDepth)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Maximum property nesting depth exceeded.");
		return false;
	}

	if (Type.Parameters.Num() != 1)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("SetProperty does not contain exactly one element type.");
		return false;
	}

	const int64 StartOffset = Reader.Tell();
	const FAssetSerializedPropertyType& ElementType = Type.Parameters[0];

	OutValue.Kind = EAssetDecodedValueKind::Set;

	/*
	 * First serialized array:
	 *
	 * ElementsToRemove
	 */
	int32 NumElementsToRemove = 0;

	if (!ReadBounded(Reader, ValueEnd, NumElementsToRemove))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Could not read the SetProperty removal count.");
		return false;
	}

	if (!IsValidContainerCount(Context, NumElementsToRemove))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = FString::Printf(TEXT("SetProperty contains an invalid removal count of %d."), NumElementsToRemove);
		return false;
	}

	for (int32 Index = 0; Index < NumElementsToRemove; ++Index)
	{
		FAssetDecodedPropertyValue Element;
		Element.Name = FString::Printf(TEXT("Remove[%d]"), Index);
		Element.TypeName = ElementType.ToString();
		Element.ContainerOperation = EAssetDecodedContainerOperation::Remove;

		const int64 ElementStart = Reader.Tell();
		if (!DecodeValueFromReader(Context, Reader, ElementType, ValueEnd, Element, Depth))
		{
			OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
			OutValue.Error = FString::Printf(TEXT("Could not decode SetProperty removed element %d."), Index);
			return false;
		}

		Element.AbsoluteOffset = ElementStart;
		Element.Size = Reader.Tell() - ElementStart;
		Element.SemanticKey = FAssetPropertyValueDecoder::BuildSemanticValueKey(Element);
		OutValue.Children.Add(MoveTemp(Element));
	}

	/*
	 * Second serialized array:
	 *
	 * Elements
	 */
	int32 NumElements = 0;

	if (!ReadBounded(Reader, ValueEnd, NumElements))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Could not read the SetProperty element count.");
		return false;
	}

	if (!IsValidContainerCount(Context, NumElements))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = FString::Printf(TEXT("SetProperty contains an invalid element count of %d."), NumElements);
		return false;
	}

	for (int32 Index = 0; Index < NumElements; ++Index)
	{
		FAssetDecodedPropertyValue Element;
		Element.Name = FString::Printf(TEXT("Element[%d]"), Index);
		Element.TypeName = ElementType.ToString();
		Element.ContainerOperation = EAssetDecodedContainerOperation::Add;

		const int64 ElementStart = Reader.Tell();
		if (!DecodeValueFromReader(Context, Reader, ElementType, ValueEnd, Element, Depth))
		{
			OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
			OutValue.Error = FString::Printf(TEXT("Could not decode SetProperty element %d."), Index);
			return false;
		}

		Element.AbsoluteOffset = ElementStart;
		Element.Size = Reader.Tell() - ElementStart;
		Element.SemanticKey = FAssetPropertyValueDecoder::BuildSemanticValueKey(Element);
		OutValue.Children.Add(MoveTemp(Element));
	}

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.AbsoluteOffset = StartOffset;
	OutValue.Size = Reader.Tell() - StartOffset;

	if (NumElementsToRemove > 0)
	{
		OutValue.ContainerMode = EAssetDecodedContainerSerializationMode::Delta;
		OutValue.Value = FString::Printf(TEXT("%d added, %d removed"), NumElements, NumElementsToRemove);
	}
	else
	{
		/*
		 * We still don't strictly know whether this is a
		 * complete set or additions relative to defaults.
		 *
		 * More on that below.
		 */
		OutValue.Value = FString::Printf(TEXT("%d serialized elements"), NumElements);
	}

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
	OutValue.Kind = EAssetDecodedValueKind::Scalar;
	OutValue.Value = Context.Document.ResolveNameReference(Reference);
	OutValue.AbsoluteOffset = Start;
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
	OutValue.Kind = EAssetDecodedValueKind::Scalar;
	OutValue.Value = MoveTemp(Value);
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

static FAssetDecodedPropertyValue MakeTextChild(const FString& Name, const FString& TypeName, const FString& Value, const int64 Offset, const int64 Size)
{
	FAssetDecodedPropertyValue Child;
	Child.Status = EAssetPropertyDecodeStatus::Success;
	Child.Kind = EAssetDecodedValueKind::Scalar;
	Child.Name = Name;
	Child.TypeName = TypeName;
	Child.Value = Value;
	Child.AbsoluteOffset = Offset;
	Child.Size = Size;
	return Child;
}

/*
 * The history types of FText that carry more than a string, in the layouts of FTextHistory_*::Serialize (TextHistory.cpp):
 * formatted texts with their arguments, numbers, percents and currencies, dates and times, case transforms and generators.
 * Bools in these records are written as 32 bit values, like every bool in a binary archive.
 */
static bool DecodeTextFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue, const int32 Depth = 0);

namespace TextHistoryDecoding
{
	struct FState
	{
		const FAssetPropertyDecodeContext& Context;
		FAssetPackagePayloadReader& Reader;
		const int64 ValueEnd;
		FAssetDecodedPropertyValue& Out;
		const int32 Depth;
		FString Error;
	};

	template <typename TValue> bool ReadScalar(FState& State, const TCHAR* Name, const TCHAR* TypeName, FString (*Format)(TValue), TValue& OutValue)
	{
		const int64 Start = State.Reader.Tell();
		if (!ReadBounded(State.Reader, State.ValueEnd, OutValue))
		{
			State.Error = FString::Printf(TEXT("Could not read %s."), Name);
			return false;
		}

		State.Out.Children.Add(MakeTextChild(Name, TypeName, Format(OutValue), Start, State.Reader.Tell() - Start));
		return true;
	}

	bool ReadString(FState& State, const TCHAR* Name, FString& OutText)
	{
		const int64 Start = State.Reader.Tell();
		FText Error;
		if (!AssetSerializationPrimitives::ReadSerializedString(State.Reader, OutText, Error) || State.Reader.Tell() > State.ValueEnd)
		{
			State.Error = FString::Printf(TEXT("Could not read %s."), Name);
			return false;
		}

		State.Out.Children.Add(MakeTextChild(Name, TEXT("StrProperty"), OutText, Start, State.Reader.Tell() - Start));
		return true;
	}

	bool ReadNestedText(FState& State, const TCHAR* Name, FAssetDecodedPropertyValue& OutText)
	{
		OutText.Name = Name;
		OutText.TypeName = TEXT("TextProperty");

		if (State.Depth + 1 >= State.Context.MaximumDepth || !DecodeTextFromReader(State.Context, State.Reader, State.ValueEnd, OutText, State.Depth + 1))
		{
			State.Error = OutText.Error.IsEmpty() ? FString::Printf(TEXT("Could not decode %s."), Name) : FString::Printf(TEXT("Could not decode %s: %s"), Name, *OutText.Error);
			return false;
		}

		return true;
	}

	/** FFormatArgumentValue: an int8 type then the value. Text values are nested FTexts. */
	bool ReadArgumentValue(FState& State, const FString& Name, FAssetDecodedPropertyValue& OutArgument)
	{
		const int64 Start = State.Reader.Tell();
		int8 Type = 0;
		if (!ReadBounded(State.Reader, State.ValueEnd, Type))
		{
			State.Error = TEXT("Could not read a format argument type.");
			return false;
		}

		OutArgument.Name = Name;

		switch (Type)
		{
			case 0: // Int
			{
				int64 Value = 0;
				if (!ReadBounded(State.Reader, State.ValueEnd, Value))
				{
					break;
				}
				OutArgument = MakeTextChild(Name, TEXT("Int64Property"), AssetNumberText::Text(Value), Start, State.Reader.Tell() - Start);
				return true;
			}
			case 1: // UInt
			{
				uint64 Value = 0;
				if (!ReadBounded(State.Reader, State.ValueEnd, Value))
				{
					break;
				}
				OutArgument = MakeTextChild(Name, TEXT("UInt64Property"), AssetNumberText::Text(Value), Start, State.Reader.Tell() - Start);
				return true;
			}
			case 2: // Float
			{
				float Value = 0.0f;
				if (!ReadBounded(State.Reader, State.ValueEnd, Value))
				{
					break;
				}
				OutArgument = MakeTextChild(Name, TEXT("FloatProperty"), AssetNumberText::Text(Value), Start, State.Reader.Tell() - Start);
				return true;
			}
			case 3: // Double
			{
				double Value = 0.0;
				if (!ReadBounded(State.Reader, State.ValueEnd, Value))
				{
					break;
				}
				OutArgument = MakeTextChild(Name, TEXT("DoubleProperty"), AssetNumberText::Text(Value), Start, State.Reader.Tell() - Start);
				return true;
			}
			case 4: // Text
			{
				return ReadNestedText(State, *Name, OutArgument) && (OutArgument.AbsoluteOffset = Start, OutArgument.Size = State.Reader.Tell() - Start, true);
			}
			case 5: // Gender, stored as a UInt
			{
				uint64 Value = 0;
				if (!ReadBounded(State.Reader, State.ValueEnd, Value))
				{
					break;
				}
				OutArgument = MakeTextChild(Name, TEXT("UInt64Property"), AssetNumberText::Text(Value), Start, State.Reader.Tell() - Start);
				return true;
			}
			default:
				State.Error = FString::Printf(TEXT("Unknown format argument type %d."), static_cast<int32>(Type));
				return false;
		}

		State.Error = TEXT("Could not read a format argument value.");
		return false;
	}

	/** FFormatArgumentData (arguments of a text formatted from Blueprints): name, type byte, value. */
	bool ReadArgumentData(FState& State, const int32 Index, FAssetDecodedPropertyValue& OutArgument)
	{
		const int64 Start = State.Reader.Tell();
		const FString Label = FString::Printf(TEXT("[%d]"), Index);

		FString ArgumentName;
		FText StringError;
		if (State.Reader.UEVer() < VER_UE4_K2NODE_VAR_REFERENCEGUIDS)
		{
			FAssetDecodedPropertyValue OldName;
			if (!DecodeTextFromReader(State.Context, State.Reader, State.ValueEnd, OldName, State.Depth + 1))
			{
				State.Error = TEXT("Could not read a format argument name.");
				return false;
			}
			ArgumentName = OldName.Value;
		}
		else if (!AssetSerializationPrimitives::ReadSerializedString(State.Reader, ArgumentName, StringError))
		{
			State.Error = TEXT("Could not read a format argument name.");
			return false;
		}

		uint8 Type = 4; // Data saved before TextFormatArgumentDataIsVariant was always text.
		if (State.Reader.CustomVer(FEditorObjectVersion::GUID) >= FEditorObjectVersion::TextFormatArgumentDataIsVariant && !ReadBounded(State.Reader, State.ValueEnd, Type))
		{
			State.Error = TEXT("Could not read a format argument type.");
			return false;
		}

		FAssetDecodedPropertyValue Value;
		const FString Name = FString::Printf(TEXT("%s %s"), *Label, *ArgumentName);
		bool bRead = false;

		switch (Type)
		{
			case 0:
			{
				const bool b64 = State.Reader.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) >= FUE5ReleaseStreamObjectVersion::TextFormatArgumentData64bitSupport;
				int64 Number = 0;
				int32 Small = 0;
				bRead = b64 ? ReadBounded(State.Reader, State.ValueEnd, Number) : ReadBounded(State.Reader, State.ValueEnd, Small);
				Value = MakeTextChild(Name, TEXT("Int64Property"), AssetNumberText::Text(b64 ? Number : static_cast<int64>(Small)), Start, State.Reader.Tell() - Start);
				break;
			}
			case 2:
			{
				float Number = 0.0f;
				bRead = ReadBounded(State.Reader, State.ValueEnd, Number);
				Value = MakeTextChild(Name, TEXT("FloatProperty"), AssetNumberText::Text(Number), Start, State.Reader.Tell() - Start);
				break;
			}
			case 3:
			{
				double Number = 0.0;
				bRead = ReadBounded(State.Reader, State.ValueEnd, Number);
				Value = MakeTextChild(Name, TEXT("DoubleProperty"), AssetNumberText::Text(Number), Start, State.Reader.Tell() - Start);
				break;
			}
			case 4:
			{
				bRead = ReadNestedText(State, *Name, Value);
				break;
			}
			case 5:
			{
				uint8 Gender = 0;
				bRead = ReadBounded(State.Reader, State.ValueEnd, Gender);
				Value = MakeTextChild(Name, TEXT("ByteProperty"), AssetNumberText::Text(static_cast<uint32>(Gender)), Start, State.Reader.Tell() - Start);
				break;
			}
			default:
				State.Error = FString::Printf(TEXT("Unknown format argument type %d."), static_cast<int32>(Type));
				return false;
		}

		if (!bRead)
		{
			State.Error = State.Error.IsEmpty() ? FString(TEXT("Could not read a format argument value.")) : State.Error;
			return false;
		}

		Value.Name = Name;
		OutArgument = MoveTemp(Value);
		return true;
	}

	bool ReadCultureName(FState& State)
	{
		FString Culture;
		return ReadString(State, TEXT("CultureName"), Culture);
	}

	FString DateTimeStyleName(const int8 Style)
	{
		static const TCHAR* const Names[] = { TEXT("Default"), TEXT("Short"), TEXT("Medium"), TEXT("Long"), TEXT("Full"), TEXT("Custom") };
		return Style >= 0 && Style < UE_ARRAY_COUNT(Names) ? FString(Names[Style]) : FString::Printf(TEXT("%d"), static_cast<int32>(Style));
	}

	bool ReadDateTime(FState& State)
	{
		int64 Ticks = 0;
		return ReadScalar<int64>(State, TEXT("SourceDateTime"), TEXT("Int64Property"), [](const int64 Value) { return FDateTime(Value).ToIso8601(); }, Ticks);
	}

	bool ReadStyle(FState& State, const TCHAR* Name, int8& OutStyle)
	{
		return ReadScalar<int8>(State, Name, TEXT("Int8Property"), [](const int8 Value) { return DateTimeStyleName(Value); }, OutStyle);
	}

	bool ReadFormatNumberBody(FState& State)
	{
		FAssetDecodedPropertyValue Source;
		if (!ReadArgumentValue(State, TEXT("SourceValue"), Source))
		{
			return false;
		}

		State.Out.Children.Add(Source);
		State.Out.Value = Source.Value;

		uint32 bHasOptions = 0;
		if (!ReadScalar<uint32>(State, TEXT("bHasFormatOptions"), TEXT("BoolProperty"), [](const uint32 Value) { return FString(Value != 0 ? TEXT("true") : TEXT("false")); }, bHasOptions))
		{
			return false;
		}

		if (bHasOptions != 0)
		{
			uint32 Flag = 0;
			int8 Rounding = 0;
			int32 Digits = 0;

			if (State.Reader.CustomVer(FEditorObjectVersion::GUID) >= FEditorObjectVersion::AddedAlwaysSignNumberFormattingOption
				&& !ReadScalar<uint32>(State, TEXT("AlwaysSign"), TEXT("BoolProperty"), [](const uint32 Value) { return FString(Value != 0 ? TEXT("true") : TEXT("false")); }, Flag))
			{
				return false;
			}

			if (!ReadScalar<uint32>(
					State, TEXT("UseGrouping"), TEXT("BoolProperty"), [](const uint32 Value) { return FString(Value != 0 ? TEXT("true") : TEXT("false")); }, Flag)
				|| !ReadScalar<int8>(
					State, TEXT("RoundingMode"), TEXT("Int8Property"), [](const int8 Value) { return AssetNumberText::Text(static_cast<int32>(Value)); }, Rounding)
				|| !ReadScalar<int32>(
					State, TEXT("MinimumIntegralDigits"), TEXT("IntProperty"), [](const int32 Value) { return AssetNumberText::Text(Value); }, Digits)
				|| !ReadScalar<int32>(
					State, TEXT("MaximumIntegralDigits"), TEXT("IntProperty"), [](const int32 Value) { return AssetNumberText::Text(Value); }, Digits)
				|| !ReadScalar<int32>(
					State, TEXT("MinimumFractionalDigits"), TEXT("IntProperty"), [](const int32 Value) { return AssetNumberText::Text(Value); }, Digits)
				|| !ReadScalar<int32>(State, TEXT("MaximumFractionalDigits"), TEXT("IntProperty"), [](const int32 Value) { return AssetNumberText::Text(Value); }, Digits))
			{
				return false;
			}
		}

		return ReadCultureName(State);
	}

	/** Returns false with State.Error set when the payload cannot be read; Unsupported is set for history types not handled. */
	bool ReadHistory(FState& State, const int8 HistoryType, bool& bOutUnsupported)
	{
		FAssetDecodedPropertyValue& Out = State.Out;

		switch (HistoryType)
		{
			case 1: // NamedFormat: FormatText, then TMap<FString, FFormatArgumentValue>.
			case 2: // OrderedFormat: FormatText, then TArray<FFormatArgumentValue>.
			case 3: // ArgumentFormat: FormatText, then TArray<FFormatArgumentData>.
			{
				FAssetDecodedPropertyValue Format;
				if (!ReadNestedText(State, TEXT("FormatText"), Format))
				{
					return false;
				}

				const FString FormatString = Format.Value;
				Out.Children.Add(MoveTemp(Format));

				const int64 ArgumentsStart = State.Reader.Tell();
				int32 Count = 0;
				if (!ReadBounded(State.Reader, State.ValueEnd, Count) || !IsValidContainerCount(State.Context, Count))
				{
					State.Error = TEXT("Could not read the format argument count.");
					return false;
				}

				FAssetDecodedPropertyValue Arguments;
				Arguments.Status = EAssetPropertyDecodeStatus::Success;
				Arguments.Kind = EAssetDecodedValueKind::Array;
				Arguments.Name = TEXT("Arguments");
				Arguments.TypeName = HistoryType == 1 ? TEXT("MapProperty") : TEXT("ArrayProperty");
				Arguments.Value = FString::Printf(TEXT("%d arguments"), Count);

				for (int32 Index = 0; Index < Count; ++Index)
				{
					FAssetDecodedPropertyValue Argument;
					bool bOk = false;

					if (HistoryType == 1)
					{
						FString Key;
						FText StringError;
						bOk = AssetSerializationPrimitives::ReadSerializedString(State.Reader, Key, StringError) && ReadArgumentValue(State, Key, Argument);
					}
					else if (HistoryType == 2)
					{
						bOk = ReadArgumentValue(State, FString::Printf(TEXT("{%d}"), Index), Argument);
					}
					else
					{
						bOk = ReadArgumentData(State, Index, Argument);
					}

					if (!bOk)
					{
						State.Error = State.Error.IsEmpty() ? FString::Printf(TEXT("Could not read format argument %d."), Index) : State.Error;
						return false;
					}

					Argument.SemanticKey = FAssetPropertyValueDecoder::BuildSemanticValueKey(Argument);
					Arguments.Children.Add(MoveTemp(Argument));
				}

				Arguments.AbsoluteOffset = ArgumentsStart;
				Arguments.Size = State.Reader.Tell() - ArgumentsStart;
				Out.Children.Add(MoveTemp(Arguments));
				Out.Value = FString::Printf(TEXT("Format(%s) with %d arguments"), *FormatString, Count);
				return true;
			}

			case 4: // AsNumber
			case 5: // AsPercent
				if (!ReadFormatNumberBody(State))
				{
					return false;
				}
				Out.Value = FString::Printf(TEXT("%s(%s)"), HistoryType == 4 ? TEXT("AsNumber") : TEXT("AsPercent"), *Out.Value);
				return true;

			case 6: // AsCurrency: currency code first, then the number body.
			{
				FString Currency;
				if (State.Reader.UEVer() >= VER_UE4_ADDED_CURRENCY_CODE_TO_FTEXT && !ReadString(State, TEXT("CurrencyCode"), Currency))
				{
					return false;
				}

				if (!ReadFormatNumberBody(State))
				{
					return false;
				}

				Out.Value = FString::Printf(TEXT("AsCurrency(%s %s)"), *Out.Value, *Currency);
				return true;
			}

			case 7: // AsDate
			{
				int8 Style = 0;
				FString TimeZone;
				if (!ReadDateTime(State) || !ReadStyle(State, TEXT("DateStyle"), Style)
					|| (State.Reader.UEVer() >= VER_UE4_FTEXT_HISTORY_DATE_TIMEZONE && !ReadString(State, TEXT("TimeZone"), TimeZone)) || !ReadCultureName(State))
				{
					return false;
				}

				Out.Value = FString::Printf(TEXT("AsDate(%s, %s)"), *Out.Children[1].Value, *DateTimeStyleName(Style));
				return true;
			}

			case 8: // AsTime
			{
				int8 Style = 0;
				FString TimeZone;
				if (!ReadDateTime(State) || !ReadStyle(State, TEXT("TimeStyle"), Style) || !ReadString(State, TEXT("TimeZone"), TimeZone) || !ReadCultureName(State))
				{
					return false;
				}

				Out.Value = FString::Printf(TEXT("AsTime(%s, %s)"), *Out.Children[1].Value, *DateTimeStyleName(Style));
				return true;
			}

			case 9: // AsDateTime
			{
				int8 DateStyle = 0;
				int8 TimeStyle = 0;
				FString Pattern;
				FString TimeZone;
				if (!ReadDateTime(State) || !ReadStyle(State, TEXT("DateStyle"), DateStyle) || !ReadStyle(State, TEXT("TimeStyle"), TimeStyle)
					|| (DateStyle == 5 && !ReadString(State, TEXT("CustomPattern"), Pattern)) || !ReadString(State, TEXT("TimeZone"), TimeZone) || !ReadCultureName(State))
				{
					return false;
				}

				Out.Value = FString::Printf(TEXT("AsDateTime(%s)"), *Out.Children[1].Value);
				return true;
			}

			case 10: // Transform: source text, then 0 = ToLower, 1 = ToUpper.
			{
				FAssetDecodedPropertyValue Source;
				if (!ReadNestedText(State, TEXT("SourceText"), Source))
				{
					return false;
				}

				const FString SourceString = Source.Value;
				Out.Children.Add(MoveTemp(Source));

				uint8 Transform = 0;
				if (!ReadScalar<uint8>(
						State, TEXT("TransformType"), TEXT("ByteProperty"), [](const uint8 Value) { return FString(Value == 0 ? TEXT("ToLower") : (Value == 1 ? TEXT("ToUpper") : TEXT("Unknown"))); },
						Transform))
				{
					return false;
				}

				Out.Value = FString::Printf(TEXT("%s(%s)"), Transform == 0 ? TEXT("ToLower") : (Transform == 1 ? TEXT("ToUpper") : TEXT("Transform")), *SourceString);
				return true;
			}

			case 12: // TextGenerator: the generator's type name and its opaque contents.
			{
				FAssetDecodedPropertyValue Generator;
				if (!DecodeNameFromReader(State.Context, State.Reader, State.ValueEnd, Generator))
				{
					State.Error = TEXT("Could not read the text generator type.");
					return false;
				}

				Out.Children.Add(MakeTextChild(TEXT("GeneratorTypeID"), TEXT("NameProperty"), Generator.Value, Generator.AbsoluteOffset, Generator.Size));

				int32 ContentSize = 0;
				if (Generator.Value != TEXT("None"))
				{
					const int64 ContentStart = State.Reader.Tell();
					if (!ReadBounded(State.Reader, State.ValueEnd, ContentSize) || ContentSize < 0 || State.Reader.Tell() + ContentSize > State.ValueEnd)
					{
						State.Error = TEXT("Could not read the text generator contents.");
						return false;
					}

					State.Reader.Seek(State.Reader.Tell() + ContentSize);
					Out.Children.Add(MakeTextChild(TEXT("GeneratorContents"), TEXT("ByteProperty"), FString::Printf(TEXT("%d bytes"), ContentSize), ContentStart, State.Reader.Tell() - ContentStart));
				}

				Out.Value = FString::Printf(TEXT("TextGenerator(%s)"), *Generator.Value);
				return true;
			}

			default:
				bOutUnsupported = true;
				State.Error = FString::Printf(TEXT("FText history type %d is not supported."), static_cast<int32>(HistoryType));
				return false;
		}
	}
} // namespace TextHistoryDecoding

/*
 * Mirrors FText::SerializeText: uint32 Flags, int8 HistoryType, then a history-specific payload. The simple histories
 * (none, base, string table) are read here; the others by TextHistoryDecoding::ReadHistory.
 */
static bool DecodeTextFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue, const int32 Depth)
{
	const int64 Start = Reader.Tell();

	const auto Fail = [&OutValue](const FString& Error) {
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = Error;
		return false;
	};

	if (Reader.UEVer() < VER_UE4_FTEXT_HISTORY)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::Unsupported;
		OutValue.Error = TEXT("FText written before the text history format is not supported.");
		return false;
	}

	uint32 Flags = 0;
	int8 HistoryType = 0;
	if (!ReadBounded(Reader, ValueEnd, Flags) || !ReadBounded(Reader, ValueEnd, HistoryType))
	{
		return Fail(TEXT("Not enough space in reader for decoding FText"));
	}

	OutValue.Kind = EAssetDecodedValueKind::Struct;
	OutValue.Children.Add(MakeTextChild(TEXT("Flags"), TEXT("UInt32Property"), AssetNumberText::Text(Flags), Start, sizeof(uint32)));

	const auto ReadTextString = [&](const TCHAR* Name) {
		const int64 StringStart = Reader.Tell();
		FString Text;
		FText Error;
		if (!AssetSerializationPrimitives::ReadSerializedString(Reader, Text, Error) || Reader.Tell() > ValueEnd)
		{
			return false;
		}
		OutValue.Children.Add(MakeTextChild(Name, TEXT("StrProperty"), Text, StringStart, Reader.Tell() - StringStart));
		return true;
	};

	if (HistoryType == -1)
	{
		// ETextHistoryType::None: optionally a culture-invariant string.
		if (Reader.CustomVer(FEditorObjectVersion::GUID) >= FEditorObjectVersion::CultureInvariantTextSerializationKeyStability)
		{
			uint32 bHasCultureInvariantString = 0;
			if (!ReadBounded(Reader, ValueEnd, bHasCultureInvariantString))
			{
				return Fail(TEXT("Could not read FText culture-invariant flag"));
			}

			if (bHasCultureInvariantString != 0 && !ReadTextString(TEXT("CultureInvariantString")))
			{
				return Fail(TEXT("Could not decode FText culture-invariant string"));
			}
		}

		OutValue.Value = OutValue.Children.Num() > 1 ? OutValue.Children.Last().Value : FString();
	}
	else if (HistoryType == 0)
	{
		// ETextHistoryType::Base: Namespace, Key, SourceString.
		if (!ReadTextString(TEXT("Namespace")) || !ReadTextString(TEXT("Key")) || !ReadTextString(TEXT("SourceString")))
		{
			return Fail(TEXT("Could not decode FText base history"));
		}

		OutValue.Value = OutValue.Children.Last().Value;

		// Editor packages also store the developer notes of the text: a string, written after the source string. It is what
		// keeps the next element of an array of texts aligned.
		if (Reader.CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::AddDevNotesToFText
			&& (Context.Document.PackageSummary.GetPackageFlags() & PKG_FilterEditorOnly) == 0 && !ReadTextString(TEXT("DevNotes")))
		{
			return Fail(TEXT("Could not decode FText developer notes"));
		}
	}
	else if (HistoryType == 11)
	{
		// ETextHistoryType::StringTableEntry: TableId (FName), Key.
		FAssetDecodedPropertyValue TableId;
		if (!DecodeNameFromReader(Context, Reader, ValueEnd, TableId))
		{
			return Fail(TEXT("Could not decode FText string table id"));
		}

		OutValue.Children.Add(MakeTextChild(TEXT("TableId"), TEXT("NameProperty"), TableId.Value, TableId.AbsoluteOffset, TableId.Size));

		if (!ReadTextString(TEXT("Key")))
		{
			return Fail(TEXT("Could not decode FText string table key"));
		}

		OutValue.Value = FString::Printf(TEXT("%s:%s"), *OutValue.Children[1].Value, *OutValue.Children[2].Value);
	}
	else
	{
		TextHistoryDecoding::FState State{ Context, Reader, ValueEnd, OutValue, Depth, FString() };
		bool bUnsupported = false;

		if (!TextHistoryDecoding::ReadHistory(State, HistoryType, bUnsupported))
		{
			OutValue.Children.Reset();
			OutValue.Status = bUnsupported ? EAssetPropertyDecodeStatus::Unsupported : EAssetPropertyDecodeStatus::InvalidData;
			OutValue.Error = State.Error;
			return false;
		}
	}

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

static bool DecodeMapEntry(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const FAssetSerializedPropertyType& KeyType, const FAssetSerializedPropertyType& ValueType,
	const int64 ValueEnd, const EAssetDecodedContainerOperation Operation, FAssetDecodedPropertyValue& OutEntry, const int32 Depth)
{
	const int64 EntryStart = Reader.Tell();

	OutEntry.Kind = EAssetDecodedValueKind::MapEntry;
	OutEntry.ContainerOperation = Operation;

	FAssetDecodedPropertyValue Key;
	Key.Name = TEXT("Key");
	Key.TypeName = KeyType.ToString();

	const int64 KeyStart = Reader.Tell();
	if (!DecodeValueFromReader(Context, Reader, KeyType, ValueEnd, Key, Depth))
	{
		OutEntry.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutEntry.Error = TEXT("Could not decode map key.");
		return false;
	}

	Key.AbsoluteOffset = KeyStart;
	Key.Size = Reader.Tell() - KeyStart;
	Key.SemanticKey = FAssetPropertyValueDecoder::BuildSemanticValueKey(Key);

	FAssetDecodedPropertyValue Value;
	Value.Name = TEXT("Value");
	Value.TypeName = ValueType.ToString();

	const int64 ValueStart = Reader.Tell();
	if (!DecodeValueFromReader(Context, Reader, ValueType, ValueEnd, Value, Depth))
	{
		OutEntry.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutEntry.Error = TEXT("Could not decode map value.");
		return false;
	}

	Value.AbsoluteOffset = ValueStart;
	Value.Size = Reader.Tell() - ValueStart;

	OutEntry.Children.Add(MoveTemp(Key));
	OutEntry.Children.Add(MoveTemp(Value));
	OutEntry.SemanticKey = OutEntry.Children[0].SemanticKey;
	OutEntry.Name = FString::Printf(TEXT("[%s]"), *OutEntry.SemanticKey);
	OutEntry.AbsoluteOffset = EntryStart;
	OutEntry.Size = Reader.Tell() - EntryStart;
	OutEntry.Status = EAssetPropertyDecodeStatus::Success;

	return true;
}

static bool DecodeRemovedMapKey(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const FAssetSerializedPropertyType& KeyType, const int64 ValueEnd,
	FAssetDecodedPropertyValue& OutEntry, const int32 Depth)
{
	const int64 EntryStart = Reader.Tell();
	OutEntry.Kind = EAssetDecodedValueKind::MapEntry;
	OutEntry.ContainerOperation = EAssetDecodedContainerOperation::Remove;

	FAssetDecodedPropertyValue Key;
	Key.Name = TEXT("Key");
	Key.TypeName = KeyType.ToString();

	const int64 KeyStart = Reader.Tell();

	if (!DecodeValueFromReader(Context, Reader, KeyType, ValueEnd, Key, Depth))
	{
		OutEntry.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutEntry.Error = TEXT("Could not decode removed map key.");
		return false;
	}

	Key.AbsoluteOffset = KeyStart;
	Key.Size = Reader.Tell() - KeyStart;
	Key.SemanticKey = FAssetPropertyValueDecoder::BuildSemanticValueKey(Key);

	OutEntry.SemanticKey = Key.SemanticKey;
	OutEntry.Name = FString::Printf(TEXT("Remove [%s]"), *OutEntry.SemanticKey);
	OutEntry.Children.Add(MoveTemp(Key));
	OutEntry.AbsoluteOffset = EntryStart;
	OutEntry.Size = Reader.Tell() - EntryStart;
	OutEntry.Status = EAssetPropertyDecodeStatus::Success;

	return true;
}

static bool DecodeMapFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const FAssetSerializedPropertyType& Type, const int64 ValueEnd,
	FAssetDecodedPropertyValue& OutValue, const int32 Depth)
{
	if (Depth >= Context.MaximumDepth)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Maximum property nesting depth exceeded.");
		return false;
	}

	if (Type.Parameters.Num() != 2)
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("MapProperty does not contain exactly two parameter types.");
		return false;
	}

	const FAssetSerializedPropertyType& KeyType = Type.Parameters[0];
	const FAssetSerializedPropertyType& ValueType = Type.Parameters[1];
	const int64 StartOffset = Reader.Tell();

	OutValue.Kind = EAssetDecodedValueKind::Map;

	int32 NumKeysToRemove = 0;
	if (!ReadBounded(Reader, ValueEnd, NumKeysToRemove))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Could not read MapProperty removal count.");
		return false;
	}

	const bool bReplaceMap = NumKeysToRemove == INDEX_NONE;
	if (bReplaceMap)
	{
		OutValue.ContainerMode = EAssetDecodedContainerSerializationMode::Full;
	}
	else
	{
		OutValue.ContainerMode = NumKeysToRemove > 0 ? EAssetDecodedContainerSerializationMode::Delta : EAssetDecodedContainerSerializationMode::Unknown;
	}

	// A removal count of -1 is the replace marker: no keys follow, and the entries that do are the whole map.
	if (!bReplaceMap)
	{
		if (!IsValidContainerCount(Context, NumKeysToRemove))
		{
			OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
			OutValue.Error = FString::Printf(TEXT("MapProperty has invalid removal count %d."), NumKeysToRemove);
			return false;
		}

		for (int32 Index = 0; Index < NumKeysToRemove; ++Index)
		{
			FAssetDecodedPropertyValue Removed;
			if (!DecodeRemovedMapKey(Context, Reader, KeyType, ValueEnd, Removed, Depth))
			{
				OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
				OutValue.Error = FString::Printf(TEXT("Could not decode removed map key %d."), Index);
				return false;
			}

			OutValue.Children.Add(MoveTemp(Removed));
		}
	}

	int32 NumEntries = 0;
	if (!ReadBounded(Reader, ValueEnd, NumEntries))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Could not read MapProperty entry count.");
		return false;
	}

	if (!IsValidContainerCount(Context, NumEntries))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = FString::Printf(TEXT("MapProperty has invalid entry count %d."), NumEntries);
		return false;
	}

	for (int32 Index = 0; Index < NumEntries; ++Index)
	{
		FAssetDecodedPropertyValue Entry;
		const EAssetDecodedContainerOperation Operation = bReplaceMap ? EAssetDecodedContainerOperation::Replace : EAssetDecodedContainerOperation::AddOrModify;

		if (!DecodeMapEntry(Context, Reader, KeyType, ValueType, ValueEnd, Operation, Entry, Depth))
		{
			OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
			OutValue.Error = FString::Printf(TEXT("Could not decode MapProperty entry %d."), Index);
			return false;
		}

		OutValue.Children.Add(MoveTemp(Entry));
	}

	OutValue.AbsoluteOffset = StartOffset;
	OutValue.Size = Reader.Tell() - StartOffset;
	OutValue.Status = EAssetPropertyDecodeStatus::Success;

	if (bReplaceMap)
	{
		OutValue.Value = FString::Printf(TEXT("%d entries (replace)"), NumEntries);
	}
	else
	{
		OutValue.Value = FString::Printf(TEXT("%d serialized entries, %d removals"), NumEntries, NumKeysToRemove);
	}

	return true;
}

static bool DecodePackageIndexFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	const int64 Start = Reader.Tell();

	int32 RawIndex = 0;
	if (!ReadBounded(Reader, ValueEnd, RawIndex))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Not enough space in reader for decoding an object reference");
		return false;
	}

	FString Path;
	if (!Context.Document.ResolvePackageIndexPath(FAssetPackageIndexReference{ RawIndex }, Path))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = FString::Printf(TEXT("Object reference %d does not point into the import or export map."), RawIndex);
		return false;
	}

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Kind = EAssetDecodedValueKind::Scalar;
	OutValue.Value = MoveTemp(Path);
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

static bool DecodeSoftObjectPathFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	const int64 Start = Reader.Tell();

	/*
	 * FLinkerLoad::operator<<(FSoftObjectPath&) reads an index into the header table only when the
	 * table is non-empty; otherwise the path is written inline, in the layout of FSoftObjectPath::SerializePathWithoutFixup.
	 */
	if (Context.Document.PackageSummary.SoftObjectPathsCount <= 0)
	{
		const FPackageFileVersion Version = Reader.UEVer();
		FString Path;
		FString SubPath;
		FText Error;

		if (Version < VER_UE4_ADDED_SOFT_OBJECT_PATH)
		{
			// The whole path is a single string.
			if (!AssetSerializationPrimitives::ReadSerializedString(Reader, Path, Error))
			{
				OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
				OutValue.Error = TEXT("Could not read an inline soft object path.");
				return false;
			}
		}
		else if (Version < EUnrealEngineObjectUE5Version::FSOFTOBJECTPATH_REMOVE_ASSET_PATH_FNAMES)
		{
			// An asset path name, then the sub path as a string.
			FAssetDecodedPropertyValue AssetPath;
			if (!DecodeNameFromReader(Context, Reader, ValueEnd, AssetPath) || !AssetSerializationPrimitives::ReadSerializedString(Reader, SubPath, Error))
			{
				OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
				OutValue.Error = TEXT("Could not read an inline soft object path.");
				return false;
			}

			Path = AssetPath.Value;
		}
		else
		{
			// The package and asset names of the top level asset, then the sub path as a UTF-8 string.
			FAssetDecodedPropertyValue PackageName;
			FAssetDecodedPropertyValue AssetName;
			if (!DecodeNameFromReader(Context, Reader, ValueEnd, PackageName) || !DecodeNameFromReader(Context, Reader, ValueEnd, AssetName)
				|| !AssetSerializationPrimitives::ReadUtf8SerializedString(Reader, SubPath, Error))
			{
				OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
				OutValue.Error = TEXT("Could not read an inline soft object path.");
				return false;
			}

			Path = PackageName.Value == TEXT("None") && AssetName.Value == TEXT("None") ? FString() : FString::Printf(TEXT("%s.%s"), *PackageName.Value, *AssetName.Value);
		}

		if (!SubPath.IsEmpty())
		{
			Path += TEXT(":") + SubPath;
		}

		OutValue.Status = EAssetPropertyDecodeStatus::Success;
		OutValue.Kind = EAssetDecodedValueKind::Scalar;
		OutValue.Value = Path.IsEmpty() ? FString(TEXT("None")) : Path;
		OutValue.AbsoluteOffset = Start;
		OutValue.Size = Reader.Tell() - Start;
		return true;
	}

	int32 PathIndex = INDEX_NONE;
	if (!ReadBounded(Reader, ValueEnd, PathIndex))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Not enough space in reader for decoding a soft object path");
		return false;
	}

	FString Path;
	if (!Context.Document.ResolveSoftObjectPath(PathIndex, Path))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = FString::Printf(TEXT("Soft object path index %d is outside the soft object path table."), PathIndex);
		return false;
	}

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Kind = EAssetDecodedValueKind::Scalar;
	OutValue.Value = MoveTemp(Path);
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

/** A bound delegate is the object it is bound to (a package index) followed by the name of the function. Delegates carrying a payload are not decoded. */
static bool DecodeBoundDelegateFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	const int64 Start = Reader.Tell();

	FAssetDecodedPropertyValue Object;
	FAssetDecodedPropertyValue Function;
	if (!DecodePackageIndexFromReader(Context, Reader, ValueEnd, Object) || !DecodeNameFromReader(Context, Reader, ValueEnd, Function))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Could not read a bound delegate.");
		return false;
	}

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Kind = EAssetDecodedValueKind::Scalar;
	OutValue.Value = Function.Value.IsEmpty() || Function.Value == TEXT("None") ? FString(TEXT("None")) : FString::Printf(TEXT("%s::%s"), *Object.Value, *Function.Value);
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

static bool DecodeMulticastDelegateFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	const int64 Start = Reader.Tell();

	int32 Count = 0;
	if (!ReadBounded(Reader, ValueEnd, Count) || !IsValidContainerCount(Context, Count))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Could not read the delegate count.");
		return false;
	}

	for (int32 Index = 0; Index < Count; ++Index)
	{
		FAssetDecodedPropertyValue Element;
		Element.Name = FString::Printf(TEXT("[%d]"), Index);
		Element.TypeName = TEXT("DelegateProperty");

		if (!DecodeBoundDelegateFromReader(Context, Reader, ValueEnd, Element))
		{
			OutValue.Children.Add(MoveTemp(Element));
			OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
			OutValue.Error = FString::Printf(TEXT("Could not decode delegate %d."), Index);
			return false;
		}

		Element.SemanticKey = FAssetPropertyValueDecoder::BuildSemanticValueKey(Element);
		OutValue.Children.Add(MoveTemp(Element));
	}

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Kind = EAssetDecodedValueKind::Array;
	OutValue.Value = FString::Printf(TEXT("%d bound"), Count);
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

/** An FFieldPath is the names of the path down from its owner, followed by the owner (a package index). */
static bool DecodeFieldPathFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	const int64 Start = Reader.Tell();

	int32 Count = 0;
	if (!ReadBounded(Reader, ValueEnd, Count) || !IsValidContainerCount(Context, Count))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Could not read the field path length.");
		return false;
	}

	TArray<FString> Names;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FAssetDecodedPropertyValue Element;
		if (!DecodeNameFromReader(Context, Reader, ValueEnd, Element))
		{
			OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
			OutValue.Error = FString::Printf(TEXT("Could not decode field path name %d."), Index);
			return false;
		}

		Names.Add(Element.Value);
	}

	FAssetDecodedPropertyValue Owner;
	if (!DecodePackageIndexFromReader(Context, Reader, ValueEnd, Owner))
	{
		OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
		OutValue.Error = TEXT("Could not decode the field path owner.");
		return false;
	}

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Kind = EAssetDecodedValueKind::Scalar;
	OutValue.Value = Names.IsEmpty() ? FString(TEXT("None")) : FString::Printf(TEXT("%s:%s"), *Owner.Value, *FString::Join(Names, TEXT(".")));
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

static bool DecodeByteFromReader(
	const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const FAssetSerializedPropertyType& Type, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
{
	/*
	 * A ByteProperty that carries an enum parameter is written as the enum value's FName
	 * in tagged serialization; a plain ByteProperty is a single raw byte.
	 */
	if (Context.IsUnversioned())
	{
		// Unversioned streams store the byte itself, enum or not.
		const bool bDecoded = DecodePrimitiveFromReader<uint8>(Reader, ValueEnd, OutValue);
		if (bDecoded && !Type.Parameters.IsEmpty())
		{
			OutValue.Value = AssetUnversionedProperties::DescribeEnumValue(Type.Parameters[0].Name, FCString::Atoi64(*OutValue.Value));
		}
		return bDecoded;
	}

	if (!Type.Parameters.IsEmpty())
	{
		return DecodeNameFromReader(Context, Reader, ValueEnd, OutValue);
	}

	return DecodePrimitiveFromReader<uint8>(Reader, ValueEnd, OutValue);
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

	// The flag is about this value only; nested values read their own tags.
	const bool bNativelySerialized = Context.bNextValueIsNativelySerialized;
	Context.bNextValueIsNativelySerialized = false;

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

	if (Type.Name == TEXT("Int8Property"))
	{
		return DecodePrimitiveFromReader<int8>(Reader, ValueEnd, OutValue);
	}

	if (Type.Name == TEXT("Int16Property"))
	{
		return DecodePrimitiveFromReader<int16>(Reader, ValueEnd, OutValue);
	}

	if (Type.Name == TEXT("Int64Property"))
	{
		return DecodePrimitiveFromReader<int64>(Reader, ValueEnd, OutValue);
	}

	if (Type.Name == TEXT("UInt16Property"))
	{
		return DecodePrimitiveFromReader<uint16>(Reader, ValueEnd, OutValue);
	}

	if (Type.Name == TEXT("UInt32Property"))
	{
		return DecodePrimitiveFromReader<uint32>(Reader, ValueEnd, OutValue);
	}

	if (Type.Name == TEXT("UInt64Property"))
	{
		return DecodePrimitiveFromReader<uint64>(Reader, ValueEnd, OutValue);
	}

	if (Type.Name == TEXT("ByteProperty"))
	{
		return DecodeByteFromReader(Context, Reader, Type, ValueEnd, OutValue);
	}

	if (Type.Name == TEXT("EnumProperty"))
	{
		if (Context.IsUnversioned() && Type.Parameters.Num() == 2)
		{
			// Unversioned streams store the integer the enum is made of, not its name.
			const FString& Underlying = Type.Parameters[1].Name;
			int64 Number = 0;
			bool bDecoded = false;

			if (Underlying == TEXT("Int8Property") || Underlying == TEXT("ByteProperty"))
			{
				bDecoded = DecodePrimitiveFromReader<uint8>(Reader, ValueEnd, OutValue);
			}
			else if (Underlying == TEXT("Int16Property") || Underlying == TEXT("UInt16Property"))
			{
				bDecoded = DecodePrimitiveFromReader<uint16>(Reader, ValueEnd, OutValue);
			}
			else if (Underlying == TEXT("Int64Property") || Underlying == TEXT("UInt64Property"))
			{
				bDecoded = DecodePrimitiveFromReader<int64>(Reader, ValueEnd, OutValue);
			}
			else
			{
				bDecoded = DecodePrimitiveFromReader<int32>(Reader, ValueEnd, OutValue);
			}

			if (bDecoded)
			{
				Number = FCString::Atoi64(*OutValue.Value);
				OutValue.Value = AssetUnversionedProperties::DescribeEnumValue(Type.Parameters[0].Name, Number);
			}

			return bDecoded;
		}

		// Enum values are stored by name, regardless of the underlying integer type.
		return DecodeNameFromReader(Context, Reader, ValueEnd, OutValue);
	}

	if (Type.Name == TEXT("ObjectProperty") || Type.Name == TEXT("ClassProperty") || Type.Name == TEXT("WeakObjectProperty") || Type.Name == TEXT("InterfaceProperty"))
	{
		return DecodePackageIndexFromReader(Context, Reader, ValueEnd, OutValue);
	}

	if (Type.Name == TEXT("SoftObjectProperty") || Type.Name == TEXT("SoftClassProperty"))
	{
		return DecodeSoftObjectPathFromReader(Context, Reader, ValueEnd, OutValue);
	}

	if (Type.Name == TEXT("FieldPathProperty"))
	{
		return DecodeFieldPathFromReader(Context, Reader, ValueEnd, OutValue);
	}

	if (Type.Name == TEXT("DelegateProperty"))
	{
		return DecodeBoundDelegateFromReader(Context, Reader, ValueEnd, OutValue);
	}

	if (Type.Name == TEXT("OptionalProperty") && Type.Parameters.Num() == 1)
	{
		// A 32 bit "is set" flag, then the value when it is.
		uint32 bIsSet = 0;
		if (!ReadBounded(Reader, ValueEnd, bIsSet))
		{
			OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
			OutValue.Error = TEXT("Could not read an optional value.");
			return false;
		}

		if (bIsSet == 0)
		{
			OutValue.Status = EAssetPropertyDecodeStatus::Success;
			OutValue.Kind = EAssetDecodedValueKind::Scalar;
			OutValue.Value = TEXT("(not set)");
			OutValue.AbsoluteOffset = Start;
			OutValue.Size = Reader.Tell() - Start;
			return true;
		}

		FAssetDecodedPropertyValue Inner;
		Inner.TypeName = Type.Parameters[0].ToString();
		if (!DecodeValueFromReader(Context, Reader, Type.Parameters[0], ValueEnd, Inner, Depth + 1))
		{
			OutValue.Status = Inner.Status == EAssetPropertyDecodeStatus::Unsupported ? EAssetPropertyDecodeStatus::Unsupported : EAssetPropertyDecodeStatus::InvalidData;
			OutValue.Error = Inner.Error;
			return false;
		}

		OutValue = MoveTemp(Inner);
		OutValue.TypeName = Type.ToString();
		OutValue.AbsoluteOffset = Start;
		OutValue.Size = Reader.Tell() - Start;
		return true;
	}

	if (Type.Name == TEXT("MulticastInlineDelegateProperty") || Type.Name == TEXT("MulticastSparseDelegateProperty"))
	{
		return DecodeMulticastDelegateFromReader(Context, Reader, ValueEnd, OutValue);
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

	if (Type.Name == TEXT("TextProperty"))
	{
		return DecodeTextFromReader(Context, Reader, ValueEnd, OutValue);
	}

	if (Type.Name == TEXT("StructProperty"))
	{
		return DecodeStructFromReader(Context, Reader, Type, ValueEnd, OutValue, Depth + 1, bNativelySerialized);
	}

	if (Type.Name == TEXT("ArrayProperty"))
	{
		return DecodeArrayFromReader(Context, Reader, Type, ValueEnd, OutValue, Depth + 1);
	}

	if (Type.Name == TEXT("SetProperty"))
	{
		return DecodeSetFromReader(Context, Reader, Type, ValueEnd, OutValue, Depth + 1);
	}

	if (Type.Name == TEXT("MapProperty"))
	{
		return DecodeMapFromReader(Context, Reader, Type, ValueEnd, OutValue, Depth + 1);
	}

	OutValue.Status = EAssetPropertyDecodeStatus::Unsupported;
	OutValue.Error = FString::Printf(TEXT("Unsupported property type: %s"), *Type.ToString());

	return false;
}

static FString MakeSetElementDisplayName(const FAssetDecodedPropertyValue& Element)
{
	const FString Value = !Element.Value.IsEmpty() ? Element.Value : Element.SemanticKey;

	switch (Element.ContainerOperation)
	{
		case EAssetDecodedContainerOperation::Add:
			return FString::Printf(TEXT("+ %s"), *Value);

		case EAssetDecodedContainerOperation::Remove:
			return FString::Printf(TEXT("- %s"), *Value);

		default:
			return Value;
	}
}

FAssetDecodedPropertyValue FAssetPropertyValueDecoder::Decode(const FAssetPackageDocument& Document, const FAssetSerializationTraceNode& Node, const int64 ExportSerialOffset)
{
	FAssetDecodedPropertyValue Result;

	if (Node.bIsZeroValue)
	{
		Result = AssetUnversionedProperties::MakeZeroValue(Node.PropertyType);
		Result.Name = Node.Name;
		Result.AbsoluteOffset = ExportSerialOffset + Node.Offset;
		return Result;
	}

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

	Context.bNextValueIsNativelySerialized = Node.bBinaryOrNative;
	if (!DecodeValueFromReader(Context, Reader, Node.PropertyType, ValueEnd, Result, 0))
	{
		return Result;
	}

	return Result;
}

bool FAssetPropertyValueDecoder::DecodeTypeAt(
	const FAssetPackageDocument& Document, const FAssetSerializedPropertyType& Type, const int64 AbsoluteOffset, const int64 AvailableSize, FAssetDecodedPropertyValue& Out)
{
	if (AvailableSize <= 0 || !Document.IsValidRange(AbsoluteOffset, AvailableSize))
	{
		Out.Status = EAssetPropertyDecodeStatus::InvalidData;
		Out.Error = TEXT("The property value range is invalid.");
		return false;
	}

	FAssetPackagePayloadReader Reader(Document, AbsoluteOffset, AvailableSize);
	FAssetPropertyDecodeContext Context{ Document };

	Out.TypeName = Type.ToString();
	const bool bDecoded = DecodeValueFromReader(Context, Reader, Type, AbsoluteOffset + AvailableSize, Out, 0);

	// The caller moves on by what the value used, so it is set whatever the decoder reported.
	Out.AbsoluteOffset = AbsoluteOffset;
	Out.Size = Reader.Tell() - AbsoluteOffset;

	if (bDecoded && Type.Name == TEXT("BoolProperty"))
	{
		Out.Value = Out.Value == TEXT("0") ? TEXT("false") : TEXT("true");
	}

	return bDecoded;
}

FString FAssetPropertyValueDecoder::BuildSemanticValueKey(const FAssetDecodedPropertyValue& Value)
{
	if (!Value.SemanticKey.IsEmpty())
	{
		return Value.SemanticKey;
	}

	if (Value.Kind == EAssetDecodedValueKind::Scalar)
	{
		return FString::Printf(TEXT("%s:%s"), *Value.TypeName, *Value.Value);
	}

	FString Result = Value.TypeName;

	Result += TEXT("{");

	for (const FAssetDecodedPropertyValue& Child : Value.Children)
	{
		Result += Child.Name;
		Result += TEXT("=");
		Result += BuildSemanticValueKey(Child);
		Result += TEXT(";");
	}

	Result += TEXT("}");

	return Result;
}

static FString FormatForDisplayInternal(const FAssetDecodedPropertyValue& Value, const int32 MaximumElements, const int32 Depth)
{
	constexpr int32 MaximumDepth = 3;

	if (Value.Children.IsEmpty() || Depth >= MaximumDepth)
	{
		return Value.Value;
	}

	TArray<FString> Parts;
	for (const FAssetDecodedPropertyValue& Child : Value.Children)
	{
		if (Parts.Num() == MaximumElements)
		{
			Parts.Add(TEXT("..."));
			break;
		}

		FString Text;
		if (!Child.IsSuccess() && Child.Value.IsEmpty() && Child.Children.IsEmpty())
		{
			Text = TEXT("(could not be decoded)");
		}
		else if (Child.Kind == EAssetDecodedValueKind::MapEntry && Child.Children.Num() == 2)
		{
			Text = FString::Printf(TEXT("%s=%s"), *FormatForDisplayInternal(Child.Children[0], MaximumElements, Depth + 1), *FormatForDisplayInternal(Child.Children[1], MaximumElements, Depth + 1));
		}
		else
		{
			Text = FormatForDisplayInternal(Child, MaximumElements, Depth + 1);
		}

		if (Value.Kind == EAssetDecodedValueKind::Struct && !Child.Name.IsEmpty())
		{
			Text = FString::Printf(TEXT("%s=%s"), *Child.Name, *Text);
		}

		Parts.Add(MoveTemp(Text));
	}

	const FString Joined = FString::Join(Parts, TEXT(", "));
	return Value.Kind == EAssetDecodedValueKind::Struct ? FString::Printf(TEXT("{%s}"), *Joined) : FString::Printf(TEXT("%s: %s"), *Value.Value, *Joined);
}

FString FAssetPropertyValueDecoder::FormatForDisplay(const FAssetDecodedPropertyValue& Value, const int32 MaximumElements)
{
	return FormatForDisplayInternal(Value, MaximumElements, 0);
}
