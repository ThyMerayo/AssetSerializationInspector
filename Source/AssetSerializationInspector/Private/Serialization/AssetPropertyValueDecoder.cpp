// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetPropertyValueDecoder.h"

#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackagePayloadReader.h"
#include "Readers/AssetPackageReader.h"
#include "Serialization/AssetSerializationPrimitives.h"
#include "Serialization/AssetSerializedPropertyTag.h"
#include "UObject/EditorObjectVersion.h"
#include "UObject/ObjectVersion.h"
#include "UObject/UE5ReleaseStreamObjectVersion.h"

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
	OutValue.Value = LexToString(Value);
	OutValue.AbsoluteOffset = Start;
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
template <typename TVector>
static bool DecodeBoxFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
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
	OutValue.Value = FString::Printf(TEXT("Min=(%s) Max=(%s) IsValid=%d"), *Min.ToString(), *Max.ToString(), bIsValid != 0 ? 1 : 0);
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
	OutValue.Value = FString::Printf(TEXT("Rotation: %s\nTranslation: %s\nScale3D: %s"), *Rotation.ToString(), *Translation.ToString(), *Scale3D.ToString());
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

bool DecodeEdGraphPinTypeFromReader(FAssetPackagePayloadReader& Reader, FAssetDecodedPropertyValue& OutValue)
{
	const int64 Start = Reader.Tell();

	FEdGraphPinType Value{};
	Value.Serialize(Reader);

	OutValue.Status = EAssetPropertyDecodeStatus::Success;
	OutValue.Kind = EAssetDecodedValueKind::Struct;
	OutValue.Value = TEXT("EdGraphPinType value");
	OutValue.AbsoluteOffset = Start;
	OutValue.Size = Reader.Tell() - Start;
	return true;
}

static bool DecodeNameFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue);
static bool DecodeSoftObjectPathFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue);

static bool IsValidContainerCount(const FAssetPropertyDecodeContext& Context, const int32 Count);

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

	if (StructName == TEXT("Vector2f") || StructName == TEXT("DeprecateSlateVector2D"))
	{
		return DecodePodStructFromReader<FVector2f>(Context, Reader, ValueEnd, OutValue, [](const FVector2f& Value) { return Value.ToString(); });
	}

	if (StructName == TEXT("GameplayTag"))
	{
		return DecodeNameFromReader(Context, Reader, ValueEnd, OutValue);
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
		return DecodeEdGraphPinTypeFromReader(Reader, OutValue);
	}

	return false;
}

static bool DecodeStructFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const FAssetSerializedPropertyType& Type, const int64 ValueEnd,
	FAssetDecodedPropertyValue& OutValue, const int32 Depth)
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

	return DecodeTaggedStruct(Context, Reader, ValueEnd, OutValue, Depth);
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
	if (Reader.UEVer() < EUnrealEngineObjectUE5Version::PROPERTY_TAG_COMPLETE_TYPE_NAME && Reader.UEVer() >= VER_UE4_INNER_ARRAY_TAG_INFO && InnerType.Name == TEXT("StructProperty"))
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
			OutValue.Children.Add(MoveTemp(Element));
			OutValue.Status = EAssetPropertyDecodeStatus::InvalidData;
			OutValue.Error = FString::Printf(TEXT("Could not decode array element %d."), Index);
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
static bool DecodeTextFromReader(
	const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue, const int32 Depth = 0);

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
				OutArgument = MakeTextChild(Name, TEXT("Int64Property"), LexToString(Value), Start, State.Reader.Tell() - Start);
				return true;
			}
			case 1: // UInt
			{
				uint64 Value = 0;
				if (!ReadBounded(State.Reader, State.ValueEnd, Value))
				{
					break;
				}
				OutArgument = MakeTextChild(Name, TEXT("UInt64Property"), LexToString(Value), Start, State.Reader.Tell() - Start);
				return true;
			}
			case 2: // Float
			{
				float Value = 0.0f;
				if (!ReadBounded(State.Reader, State.ValueEnd, Value))
				{
					break;
				}
				OutArgument = MakeTextChild(Name, TEXT("FloatProperty"), LexToString(Value), Start, State.Reader.Tell() - Start);
				return true;
			}
			case 3: // Double
			{
				double Value = 0.0;
				if (!ReadBounded(State.Reader, State.ValueEnd, Value))
				{
					break;
				}
				OutArgument = MakeTextChild(Name, TEXT("DoubleProperty"), LexToString(Value), Start, State.Reader.Tell() - Start);
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
				OutArgument = MakeTextChild(Name, TEXT("UInt64Property"), LexToString(Value), Start, State.Reader.Tell() - Start);
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
				Value = MakeTextChild(Name, TEXT("Int64Property"), LexToString(b64 ? Number : static_cast<int64>(Small)), Start, State.Reader.Tell() - Start);
				break;
			}
			case 2:
			{
				float Number = 0.0f;
				bRead = ReadBounded(State.Reader, State.ValueEnd, Number);
				Value = MakeTextChild(Name, TEXT("FloatProperty"), LexToString(Number), Start, State.Reader.Tell() - Start);
				break;
			}
			case 3:
			{
				double Number = 0.0;
				bRead = ReadBounded(State.Reader, State.ValueEnd, Number);
				Value = MakeTextChild(Name, TEXT("DoubleProperty"), LexToString(Number), Start, State.Reader.Tell() - Start);
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
				Value = MakeTextChild(Name, TEXT("ByteProperty"), LexToString(static_cast<uint32>(Gender)), Start, State.Reader.Tell() - Start);
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

			if (State.Reader.CustomVer(FEditorObjectVersion::GUID) >= FEditorObjectVersion::AddedAlwaysSignNumberFormattingOption &&
				!ReadScalar<uint32>(State, TEXT("AlwaysSign"), TEXT("BoolProperty"), [](const uint32 Value) { return FString(Value != 0 ? TEXT("true") : TEXT("false")); }, Flag))
			{
				return false;
			}

			if (!ReadScalar<uint32>(State, TEXT("UseGrouping"), TEXT("BoolProperty"), [](const uint32 Value) { return FString(Value != 0 ? TEXT("true") : TEXT("false")); }, Flag) ||
				!ReadScalar<int8>(State, TEXT("RoundingMode"), TEXT("Int8Property"), [](const int8 Value) { return LexToString(static_cast<int32>(Value)); }, Rounding) ||
				!ReadScalar<int32>(State, TEXT("MinimumIntegralDigits"), TEXT("IntProperty"), [](const int32 Value) { return LexToString(Value); }, Digits) ||
				!ReadScalar<int32>(State, TEXT("MaximumIntegralDigits"), TEXT("IntProperty"), [](const int32 Value) { return LexToString(Value); }, Digits) ||
				!ReadScalar<int32>(State, TEXT("MinimumFractionalDigits"), TEXT("IntProperty"), [](const int32 Value) { return LexToString(Value); }, Digits) ||
				!ReadScalar<int32>(State, TEXT("MaximumFractionalDigits"), TEXT("IntProperty"), [](const int32 Value) { return LexToString(Value); }, Digits))
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
				if (!ReadDateTime(State) || !ReadStyle(State, TEXT("DateStyle"), Style) || (State.Reader.UEVer() >= VER_UE4_FTEXT_HISTORY_DATE_TIMEZONE && !ReadString(State, TEXT("TimeZone"), TimeZone)) ||
					!ReadCultureName(State))
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
				if (!ReadDateTime(State) || !ReadStyle(State, TEXT("DateStyle"), DateStyle) || !ReadStyle(State, TEXT("TimeStyle"), TimeStyle) || (DateStyle == 5 && !ReadString(State, TEXT("CustomPattern"), Pattern)) ||
					!ReadString(State, TEXT("TimeZone"), TimeZone) || !ReadCultureName(State))
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
				if (!ReadScalar<uint8>(State, TEXT("TransformType"), TEXT("ByteProperty"), [](const uint8 Value) { return FString(Value == 0 ? TEXT("ToLower") : (Value == 1 ? TEXT("ToUpper") : TEXT("Unknown"))); }, Transform))
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
	OutValue.Children.Add(MakeTextChild(TEXT("Flags"), TEXT("UInt32Property"), LexToString(Flags), Start, sizeof(uint32)));

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
			if (!DecodeNameFromReader(Context, Reader, ValueEnd, PackageName) || !DecodeNameFromReader(Context, Reader, ValueEnd, AssetName) ||
				!AssetSerializationPrimitives::ReadUtf8SerializedString(Reader, SubPath, Error))
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

	if (Type.Name == TEXT("MulticastInlineDelegateProperty"))
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
		return DecodeStructFromReader(Context, Reader, Type, ValueEnd, OutValue, Depth + 1);
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
		if (Child.Kind == EAssetDecodedValueKind::MapEntry && Child.Children.Num() == 2)
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
