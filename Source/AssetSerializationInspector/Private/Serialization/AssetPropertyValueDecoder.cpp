// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetPropertyValueDecoder.h"

#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackagePayloadReader.h"
#include "Readers/AssetPackageReader.h"
#include "Serialization/AssetSerializationPrimitives.h"
#include "Serialization/AssetSerializedPropertyTag.h"
#include "UObject/EditorObjectVersion.h"
#include "UObject/ObjectVersion.h"

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
 * Mirrors FText::SerializeText: uint32 Flags, int8 HistoryType, then a history-specific payload.
 * Only the history types that carry no arguments are decoded; formatted, numeric, date/time,
 * transform and generator histories are reported as unsupported.
 */
static bool DecodeTextFromReader(const FAssetPropertyDecodeContext& Context, FAssetPackagePayloadReader& Reader, const int64 ValueEnd, FAssetDecodedPropertyValue& OutValue)
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
		OutValue.Children.Reset();
		OutValue.Status = EAssetPropertyDecodeStatus::Unsupported;
		OutValue.Error = FString::Printf(TEXT("FText history type %d is not supported."), static_cast<int32>(HistoryType));
		return false;
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
