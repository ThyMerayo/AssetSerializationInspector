// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetUnversionedProperties.h"

#include "UObject/Class.h"
#include "UObject/EnumProperty.h"
#include "UObject/PropertyOptional.h"
#include "UObject/UnrealType.h"

#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackagePayloadReader.h"

namespace
{
	struct FFragment
	{
		uint8 SkipNum = 0;
		bool bHasAnyZeroes = false;
		uint8 ValueNum = 0;
		bool bIsLast = false;

		static FFragment Unpack(const uint16 Packed)
		{
			FFragment Fragment;
			Fragment.SkipNum = static_cast<uint8>(Packed & 0x007fu);
			Fragment.bHasAnyZeroes = (Packed & 0x0080u) != 0;
			Fragment.ValueNum = static_cast<uint8>(Packed >> 9u);
			Fragment.bIsLast = (Packed & 0x0100u) != 0;
			return Fragment;
		}
	};

	/** The slots of the struct's schema, in the order the stream refers to them: its property list with fixed size arrays spread out. */
	void BuildSchema(const UStruct* Struct, const bool bSkipEditorOnly, TArray<TPair<const FProperty*, int32>>& OutSchema)
	{
		for (const FProperty* Property = Struct->PropertyLink; Property != nullptr; Property = Property->PropertyLinkNext)
		{
			if (bSkipEditorOnly && Property->IsEditorOnlyProperty())
			{
				continue;
			}

			for (int32 ArrayIndex = 0; ArrayIndex < Property->ArrayDim; ++ArrayIndex)
			{
				OutSchema.Emplace(Property, ArrayIndex);
			}
		}
	}

	FString UnderlyingTypeName(const FProperty* Underlying)
	{
		return Underlying != nullptr ? Underlying->GetID().ToString() : FString(TEXT("ByteProperty"));
	}
} // namespace

bool AssetUnversionedProperties::IsUsedBy(const FAssetPackageDocument& Document)
{
	return (Document.PackageSummary.GetPackageFlags() & PKG_UnversionedProperties) != 0;
}

FString AssetUnversionedProperties::DescribeEnumValue(const FString& EnumName, const int64 Value)
{
	if (const UEnum* Enum = FindFirstObject<UEnum>(*EnumName, EFindFirstObjectOptions::NativeFirst))
	{
		const FString Name = Enum->GetNameStringByValue(Value);
		if (!Name.IsEmpty())
		{
			return FString::Printf(TEXT("%s::%s"), *EnumName, *Name);
		}
	}

	return LexToString(Value);
}

FAssetSerializedPropertyType AssetUnversionedProperties::MakeType(const FProperty* Property)
{
	FAssetSerializedPropertyType Type;
	Type.Name = Property->GetID().ToString();

	const auto Add = [&Type](const FProperty* Inner) {
		if (Inner != nullptr)
		{
			Type.Parameters.Add(MakeType(Inner));
		}
	};

	if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
	{
		FAssetSerializedPropertyType StructType;
		StructType.Name = Struct->Struct != nullptr ? Struct->Struct->GetName() : FString(TEXT("None"));
		Type.Parameters.Add(MoveTemp(StructType));
	}
	else if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
	{
		Add(Array->Inner);
	}
	else if (const FSetProperty* Set = CastField<FSetProperty>(Property))
	{
		Add(Set->ElementProp);
	}
	else if (const FMapProperty* Map = CastField<FMapProperty>(Property))
	{
		Add(Map->KeyProp);
		Add(Map->ValueProp);
	}
	else if (const FOptionalProperty* Optional = CastField<FOptionalProperty>(Property))
	{
		Add(Optional->GetValueProperty());
	}
	else if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
	{
		// The enum, then the integer type the value is stored as.
		FAssetSerializedPropertyType EnumType;
		EnumType.Name = Enum->GetEnum() != nullptr ? Enum->GetEnum()->GetName() : FString(TEXT("None"));
		Type.Parameters.Add(MoveTemp(EnumType));

		FAssetSerializedPropertyType Underlying;
		Underlying.Name = UnderlyingTypeName(Enum->GetUnderlyingProperty());
		Type.Parameters.Add(MoveTemp(Underlying));
	}
	else if (const FByteProperty* Byte = CastField<FByteProperty>(Property))
	{
		if (Byte->Enum != nullptr)
		{
			FAssetSerializedPropertyType EnumType;
			EnumType.Name = Byte->Enum->GetName();
			Type.Parameters.Add(MoveTemp(EnumType));
		}
	}

	return Type;
}

FAssetDecodedPropertyValue AssetUnversionedProperties::MakeZeroValue(const FAssetSerializedPropertyType& Type)
{
	FAssetDecodedPropertyValue Value;
	Value.Status = EAssetPropertyDecodeStatus::Success;
	Value.TypeName = Type.ToString();
	Value.Kind = EAssetDecodedValueKind::Scalar;

	const FString& Name = Type.Name;

	if (Name == TEXT("BoolProperty"))
	{
		Value.Value = TEXT("false");
	}
	else if (Name == TEXT("ArrayProperty") || Name == TEXT("SetProperty") || Name == TEXT("MapProperty"))
	{
		Value.Kind = Name == TEXT("ArrayProperty") ? EAssetDecodedValueKind::Array : (Name == TEXT("SetProperty") ? EAssetDecodedValueKind::Set : EAssetDecodedValueKind::Map);
		Value.Value = TEXT("0 elements");
	}
	else if (Name == TEXT("StructProperty"))
	{
		Value.Kind = EAssetDecodedValueKind::Struct;
		Value.Value = TEXT("(zero)");
	}
	else if (Name == TEXT("StrProperty") || Name == TEXT("TextProperty"))
	{
		Value.Value = FString();
	}
	else if (Name == TEXT("NameProperty") || Name.EndsWith(TEXT("ObjectProperty")) || Name == TEXT("ClassProperty") || Name == TEXT("InterfaceProperty"))
	{
		Value.Value = TEXT("None");
	}
	else if (Name == TEXT("OptionalProperty"))
	{
		Value.Value = TEXT("(not set)");
	}
	else
	{
		// Numbers, and enum values: the enum's name for 0 is not looked up here, 0 is what the stream says.
		Value.Value = TEXT("0");
	}

	return Value;
}

bool AssetUnversionedProperties::Read(
	const FAssetPackageDocument& Document, const UStruct* Struct, const int64 Offset, const int64 End, TArray<FAssetUnversionedValue>& Out, int64& OutEndOffset, FString& OutError)
{
	OutEndOffset = Offset;

	if (Struct == nullptr || End <= Offset || !Document.IsValidRange(Offset, End - Offset))
	{
		OutError = TEXT("There is no class or no data to read the properties with.");
		return false;
	}

	FAssetPackagePayloadReader Reader(Document, Offset, End - Offset);

	// The header: fragments until the last one, then the zero mask of the fragments that hold zeroes.
	TArray<FFragment> Fragments;
	uint32 ZeroMaskBits = 0;
	for (;;)
	{
		uint16 Packed = 0;
		if (Reader.Tell() + static_cast<int64>(sizeof(Packed)) > End)
		{
			OutError = TEXT("The property header is cut short.");
			return false;
		}

		Reader << Packed;
		const FFragment Fragment = FFragment::Unpack(Packed);
		Fragments.Add(Fragment);

		if (Fragment.bHasAnyZeroes)
		{
			ZeroMaskBits += Fragment.ValueNum;
		}

		if (Fragment.bIsLast)
		{
			break;
		}

		if (Fragments.Num() > 4096)
		{
			OutError = TEXT("The property header has no last fragment.");
			return false;
		}
	}

	TBitArray<> ZeroMask;
	if (ZeroMaskBits > 0)
	{
		const int64 MaskBytes = ZeroMaskBits <= 8 ? 1 : (ZeroMaskBits <= 16 ? 2 : static_cast<int64>(FMath::DivideAndRoundUp(ZeroMaskBits, 32u)) * 4);
		if (Reader.Tell() + MaskBytes > End)
		{
			OutError = TEXT("The zero mask of the property header is cut short.");
			return false;
		}

		ZeroMask.Init(false, ZeroMaskBits);
		for (int64 Byte = 0; Byte < MaskBytes; ++Byte)
		{
			uint8 Value = 0;
			Reader << Value;
			for (int32 Bit = 0; Bit < 8; ++Bit)
			{
				const int64 Index = Byte * 8 + Bit;
				if (Index < static_cast<int64>(ZeroMaskBits))
				{
					ZeroMask[static_cast<int32>(Index)] = (Value & (1 << Bit)) != 0;
				}
			}
		}
	}

	TArray<TPair<const FProperty*, int32>> Schema;
	BuildSchema(Struct, (Document.PackageSummary.GetPackageFlags() & PKG_FilterEditorOnly) != 0, Schema);

	int32 SchemaIndex = 0;
	int32 ZeroIndex = 0;

	for (const FFragment& Fragment : Fragments)
	{
		SchemaIndex += Fragment.SkipNum;

		for (int32 Index = 0; Index < Fragment.ValueNum; ++Index, ++SchemaIndex)
		{
			if (!Schema.IsValidIndex(SchemaIndex))
			{
				OutError = FString::Printf(TEXT("The stream refers to property %d but %s has %d: the class is not the one the package was saved with."), SchemaIndex, *Struct->GetName(), Schema.Num());
				// Nothing read so far can be trusted with the wrong class.
				Out.Reset();
				OutEndOffset = Offset;
				return false;
			}

			FAssetUnversionedValue& Slot = Out.AddDefaulted_GetRef();
			Slot.Property = Schema[SchemaIndex].Key;
			Slot.ArrayIndex = Schema[SchemaIndex].Value;
			Slot.Type = MakeType(Slot.Property);

			const bool bZero = Fragment.bHasAnyZeroes && ZeroMask[ZeroIndex++];
			if (bZero)
			{
				Slot.bZero = true;
				Slot.Offset = Reader.Tell();
				Slot.Decoded = MakeZeroValue(Slot.Type);
				continue;
			}

			Slot.Offset = Reader.Tell();
			if (!FAssetPropertyValueDecoder::DecodeTypeAt(Document, Slot.Type, Slot.Offset, End - Slot.Offset, Slot.Decoded))
			{
				OutError = FString::Printf(TEXT("Could not decode %s: %s"), *Slot.Property->GetName(), *Slot.Decoded.Error);
				OutEndOffset = Slot.Offset;
				Out.Pop();
				return false;
			}

			Slot.Size = Slot.Decoded.Size;
			Reader.Seek(Slot.Offset + Slot.Size);
		}
	}

	OutEndOffset = Reader.Tell();
	return true;
}
