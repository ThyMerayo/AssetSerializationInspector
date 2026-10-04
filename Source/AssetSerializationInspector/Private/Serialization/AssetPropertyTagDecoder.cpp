// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetPropertyTagDecoder.h"

#include "Readers/AssetPackagePayloadReader.h"
#include "Serialization/AssetSerializedPropertyTag.h"

constexpr int32 MaxPropertyTypeDepth = 16;

namespace
{
	// Replicated from PropertyTag.cpp
	enum class EAssetPropertyTagFlags : uint8
	{
		None = 0x00,
		HasArrayIndex = 0x01,
		HasPropertyGuid = 0x02,
		HasPropertyExtensions = 0x04,
		HasBinaryOrNativeSerialize = 0x08,
		BoolTrue = 0x10,
		SkippedSerialize = 0x20,
	};

	ENUM_CLASS_FLAGS(EAssetPropertyTagFlags);

	enum class EAssetPropertyTagExtension : uint8
	{
		NoExtension = 0x00,
		ReserveForFutureUse = 0x01, // Can be used to add a next group of extensions

		////////////////////////////////////////////////
		// First extension group
		OverridableInformation = 0x02,
		HasExternalsObjects = 0x04,
	};

	ENUM_CLASS_FLAGS(EAssetPropertyTagExtension);
} // namespace

/**
 * Reads a complete property type name: a pre-order list of nodes, each an FName and the number of nodes nested directly under
 * it (FPropertyTypeName's archive format). The engine's own reader cannot be used on data that may not be a property tag at
 * all: it stops the process (a fatal error) when the node count is absurd, so the sizes are bounded here instead.
 */
static bool ReadPropertyTypeNode(FAssetPackagePayloadReader& Reader, FAssetSerializedPropertyType& OutType, int32& InOutNodeBudget, const int32 Depth)
{
	if (Depth >= MaxPropertyTypeDepth || --InOutNodeBudget < 0)
	{
		return false;
	}

	FName Name;
	int32 InnerCount = 0;
	Reader << Name;
	Reader << InnerCount;

	if (Reader.IsError() || InnerCount < 0 || InnerCount > 64)
	{
		return false;
	}

	OutType = {};
	OutType.Name = Name.ToString();
	OutType.Parameters.Reserve(InnerCount);

	for (int32 Index = 0; Index < InnerCount; ++Index)
	{
		FAssetSerializedPropertyType Parameter;
		if (!ReadPropertyTypeNode(Reader, Parameter, InOutNodeBudget, Depth + 1))
		{
			return false;
		}

		OutType.Parameters.Add(MoveTemp(Parameter));
	}

	return true;
}

static bool ReadCompletePropertyType(const FAssetPackageDocument& Document, FAssetPackagePayloadReader& Reader, FAssetSerializedPropertyType& OutType, FText& OutError)
{
	int32 NodeBudget = 256;

	if (!ReadPropertyTypeNode(Reader, OutType, NodeBudget, 0))
	{
		OutType = {};
		OutError = NSLOCTEXT("AssetPropertyTagDecoder", "PropertyTypeNameReadFailed", "Could not deserialize the complete property type name.");
		return false;
	}

	// A type that names nothing is how the engine writes "no type".
	if (OutType.Name == TEXT("None") && OutType.Parameters.IsEmpty())
	{
		OutType = {};
	}

	return true;
}

static bool ReadPropertyExtensions(const FAssetPackageDocument& Document, FAssetPackagePayloadReader& Reader, FAssetSerializedPropertyTag& OutTag, FText& OutError)
{
	using FExtensionStorage = uint8;

	FExtensionStorage RawExtensions = 0;
	Reader << RawExtensions;

	if (Reader.IsError())
	{
		OutError = NSLOCTEXT("AssetPropertyTagDecoder", "PropertyExtensionsReadFailed", "Could not read property tag extensions.");
		return false;
	}

	OutTag.RawExtensions = static_cast<uint32>(RawExtensions);

	const EAssetPropertyTagExtension Extensions = static_cast<EAssetPropertyTagExtension>(RawExtensions);

	if (EnumHasAnyFlags(Extensions, EAssetPropertyTagExtension::OverridableInformation))
	{
		/*
		 * Use the exact underlying type from
		 * EOverriddenPropertyOperation in your 5.8.1 headers.
		 */
		uint8 OverrideOperation = 0;
		Reader << OverrideOperation;

		if (Reader.IsError())
		{
			OutError = NSLOCTEXT("AssetPropertyTagDecoder", "OverrideOperationReadFailed", "Could not read the overridden-property operation.");
			return false;
		}

		OutTag.OverrideOperation = OverrideOperation;

		bool bExperimentalLogic = false;
		Reader << bExperimentalLogic;

		if (Reader.IsError())
		{
			OutError = NSLOCTEXT("AssetPropertyTagDecoder", "OverridableLogicReadFailed", "Could not read the overridable-property logic flag.");
			return false;
		}

		OutTag.bExperimentalOverridableLogic = bExperimentalLogic;
	}

	if (EnumHasAnyFlags(Extensions, EAssetPropertyTagExtension::HasExternalsObjects))
	{
		bool bExternalObjects = false;
		Reader << bExternalObjects;

		if (Reader.IsError())
		{
			OutError = NSLOCTEXT("AssetPropertyTagDecoder", "ExternalObjectsFlagReadFailed", "Could not read the external-objects property flag.");
			return false;
		}

		OutTag.bExperimentalExternalObjects = bExternalObjects;
	}

	return true;
}

static bool ReadModernTagRemainder(const FAssetPackageDocument& Document, FAssetPackagePayloadReader& Reader, FAssetSerializedPropertyTag& OutTag, FText& OutError)
{
	using FFlagsStorage = uint8;

	FFlagsStorage RawFlags = 0;

	if (!Reader.CanRead(sizeof(RawFlags)))
	{
		OutError = NSLOCTEXT("AssetPropertyTagDecoder", "PropertyFlagsOutOfRange", "The property tag flags extend beyond the property stream.");
		return false;
	}

	Reader << RawFlags;

	if (Reader.IsError())
	{
		OutError = NSLOCTEXT("AssetPropertyTagDecoder", "PropertyFlagsReadFailed", "Could not read property tag flags.");
		return false;
	}

	OutTag.RawFlags = static_cast<uint32>(RawFlags);

	const EAssetPropertyTagFlags Flags = static_cast<EAssetPropertyTagFlags>(RawFlags);

	if (EnumHasAnyFlags(Flags, EAssetPropertyTagFlags::HasArrayIndex))
	{
		Reader << OutTag.ArrayIndex;

		if (Reader.IsError())
		{
			OutError = NSLOCTEXT("AssetPropertyTagDecoder", "PropertyArrayIndexReadFailed", "Could not read the property array index.");
			return false;
		}
	}
	else
	{
		OutTag.ArrayIndex = 0;
	}

	if (EnumHasAnyFlags(Flags, EAssetPropertyTagFlags::HasPropertyGuid))
	{
		OutTag.bHasPropertyGuid = true;
		Reader << OutTag.PropertyGuid;

		if (Reader.IsError())
		{
			OutError = NSLOCTEXT("AssetPropertyTagDecoder", "PropertyGuidReadFailed", "Could not read the property GUID.");
			return false;
		}
	}

	if (EnumHasAnyFlags(Flags, EAssetPropertyTagFlags::SkippedSerialize))
	{
		OutTag.SerializeType = EAssetPropertyTagSerializeType::Skipped;
	}
	else if (EnumHasAnyFlags(Flags, EAssetPropertyTagFlags::HasBinaryOrNativeSerialize))
	{
		OutTag.SerializeType = EAssetPropertyTagSerializeType::BinaryOrNative;
	}
	else
	{
		OutTag.SerializeType = EAssetPropertyTagSerializeType::Property;
	}

	OutTag.bBoolValue = EnumHasAnyFlags(Flags, EAssetPropertyTagFlags::BoolTrue);

	if (EnumHasAnyFlags(Flags, EAssetPropertyTagFlags::HasPropertyExtensions))
	{
		if (!ReadPropertyExtensions(Document, Reader, OutTag, OutError))
		{
			return false;
		}
	}

	OutTag.ValueOffset = Reader.Tell();

	OutTag.TagSize = OutTag.ValueOffset - OutTag.TagOffset;

	if (OutTag.Size < 0)
	{
		OutError = FText::Format(NSLOCTEXT("AssetPropertyTagDecoder", "NegativePropertySize", "Property '{0}' declares a negative serialized size."), FText::FromString(OutTag.ResolvedName));
		return false;
	}

	if (OutTag.Size > Reader.GetRegionEnd() - OutTag.ValueOffset)
	{
		OutError = FText::Format(NSLOCTEXT("AssetPropertyTagDecoder", "PropertyValueOutOfRange", "Property '{0}' declares {1} value bytes, which extend beyond the script serialization range."),
			FText::FromString(OutTag.ResolvedName), FText::AsNumber(OutTag.Size));
		return false;
	}

	return true;
}

static FAssetSerializedPropertyType MakeNamedType(const FString& Name)
{
	FAssetSerializedPropertyType Type;
	Type.Name = Name;
	return Type;
}

/**
 * Packages saved before PROPERTY_TAG_COMPLETE_TYPE_NAME (UE4 and UE5 up to 5.3) write a different tag: the type name alone, the
 * size and array index, then only the extra fields that type needs (struct name and GUID, bool value, enum name, inner types),
 * a property GUID, and, in later versions, tag extensions. This builds the same complete type the newer format stores. The layout
 * is that of LoadPropertyTagNoFullType in the engine's PropertyTag.cpp; the engine's own FPropertyTag is not exported to plugins.
 */
static bool ReadLegacyTagRemainder(const FAssetPackageDocument& Document, FAssetPackagePayloadReader& Reader, FAssetSerializedPropertyTag& OutTag, FText& OutError)
{
	const FPackageFileVersion Version = Reader.UEVer();

	FName TypeName;
	Reader << TypeName;
	Reader << OutTag.Size;
	Reader << OutTag.ArrayIndex;

	if (Reader.IsError())
	{
		OutError = FText::Format(NSLOCTEXT("AssetPropertyTagDecoder", "LegacyPropertyHeaderReadFailed", "Could not read the tag of property '{0}' (older package format)."), FText::FromString(OutTag.ResolvedName));
		return false;
	}

	const auto ReadName = [&Reader](FString& OutName) {
		FName Name;
		Reader << Name;
		OutName = Name.ToString();
		return !Reader.IsError();
	};

	OutTag.Type = MakeNamedType(TypeName.ToString());

	bool bTypeFieldsRead = true;
	FString Text;

	if (TypeName == NAME_StructProperty)
	{
		FGuid StructGuid;
		bTypeFieldsRead = ReadName(Text);
		OutTag.Type.Parameters.Add(MakeNamedType(Text));

		if (bTypeFieldsRead && Version >= VER_UE4_STRUCT_GUID_IN_PROPERTY_TAG)
		{
			Reader << StructGuid;
			bTypeFieldsRead = !Reader.IsError();
			if (StructGuid.IsValid())
			{
				OutTag.Type.Parameters.Add(MakeNamedType(StructGuid.ToString(EGuidFormats::DigitsWithHyphens)));
			}
		}
	}
	else if (TypeName == NAME_BoolProperty)
	{
		uint8 BoolValue = 0;
		Reader << BoolValue;
		bTypeFieldsRead = !Reader.IsError();
		OutTag.bBoolValue = BoolValue != 0;
	}
	else if (TypeName == NAME_ByteProperty)
	{
		bTypeFieldsRead = ReadName(Text);
		if (Text != TEXT("None"))
		{
			OutTag.Type.Parameters.Add(MakeNamedType(Text));
		}
	}
	else if (TypeName == NAME_EnumProperty)
	{
		bTypeFieldsRead = ReadName(Text);
		OutTag.Type.Parameters.Add(MakeNamedType(Text));
		OutTag.Type.Parameters.Add(MakeNamedType(TEXT("ByteProperty")));
	}
	else if (TypeName == NAME_ArrayProperty || TypeName == NAME_OptionalProperty || (TypeName == NAME_SetProperty && Version >= VER_UE4_PROPERTY_TAG_SET_MAP_SUPPORT))
	{
		if (TypeName != NAME_ArrayProperty || Version >= VAR_UE4_ARRAY_PROPERTY_INNER_TAGS)
		{
			bTypeFieldsRead = ReadName(Text);
			OutTag.Type.Parameters.Add(MakeNamedType(Text));
		}
		else
		{
			OutTag.Type.Parameters.Add(MakeNamedType(TEXT("None")));
		}
	}
	else if (TypeName == NAME_MapProperty && Version >= VER_UE4_PROPERTY_TAG_SET_MAP_SUPPORT)
	{
		bTypeFieldsRead = ReadName(Text);
		OutTag.Type.Parameters.Add(MakeNamedType(Text));
		bTypeFieldsRead = bTypeFieldsRead && ReadName(Text);
		OutTag.Type.Parameters.Add(MakeNamedType(Text));
	}

	if (!bTypeFieldsRead)
	{
		OutError = FText::Format(NSLOCTEXT("AssetPropertyTagDecoder", "LegacyPropertyTypeReadFailed", "Could not read the type of property '{0}' (older package format)."), FText::FromString(OutTag.ResolvedName));
		return false;
	}

	if (Version >= VER_UE4_PROPERTY_GUID_IN_PROPERTY_TAG)
	{
		uint8 bHasPropertyGuid = 0;
		Reader << bHasPropertyGuid;
		OutTag.bHasPropertyGuid = bHasPropertyGuid != 0;

		if (OutTag.bHasPropertyGuid)
		{
			Reader << OutTag.PropertyGuid;
		}

		if (Reader.IsError())
		{
			OutError = NSLOCTEXT("AssetPropertyTagDecoder", "LegacyPropertyGuidReadFailed", "Could not read the property GUID (older package format).");
			return false;
		}
	}

	if (Version >= EUnrealEngineObjectUE5Version::PROPERTY_TAG_EXTENSION_AND_OVERRIDABLE_SERIALIZATION && !ReadPropertyExtensions(Document, Reader, OutTag, OutError))
	{
		return false;
	}

	OutTag.SerializeType = EAssetPropertyTagSerializeType::Property;
	OutTag.ValueOffset = Reader.Tell();
	OutTag.TagSize = OutTag.ValueOffset - OutTag.TagOffset;

	if (OutTag.Size < 0 || OutTag.Size > Reader.GetRegionEnd() - OutTag.ValueOffset)
	{
		OutError = FText::Format(NSLOCTEXT("AssetPropertyTagDecoder", "LegacyPropertyValueOutOfRange", "Property '{0}' declares {1} value bytes, which extend beyond the script serialization range."),
			FText::FromString(OutTag.ResolvedName), FText::AsNumber(OutTag.Size));
		return false;
	}

	return true;
}

bool FAssetPropertyTagDecoder::ReadTag(const FAssetPackageDocument& Document, FAssetPackagePayloadReader& Reader, FAssetSerializedPropertyTag& OutTag, FText& OutError)
{
	OutTag = {};
	OutError = FText::GetEmpty();

	const int64 TagStart = Reader.Tell();

	OutTag.TagOffset = TagStart;

	FName PropertyName;
	Reader << PropertyName;

	if (Reader.IsError())
	{
		OutError = NSLOCTEXT("AssetPropertyTagDecoder", "PropertyNameReadFailed", "Could not read property name.");
		return false;
	}

	OutTag.ResolvedName = PropertyName.ToString();

	if (PropertyName.IsNone())
	{
		OutTag.ValueOffset = Reader.Tell();
		OutTag.TagSize = Reader.Tell() - OutTag.TagOffset;
		return true;
	}

	// 	if (!Reader.ReadResolvedName(OutTag.ResolvedName, &OutTag.Name))
	// 	{
	// 		OutError = NSLOCTEXT("AssetPropertyTagDecoder", "PropertyNameReadFailed", "Could not read property name.");
	// 		return false;
	// 	}
	//
	if (OutTag.IsTerminator())
	{
		OutTag.ValueOffset = Reader.Tell();
		OutTag.TagSize = OutTag.ValueOffset - OutTag.TagOffset;
		return true;
	}

	if (Reader.UEVer() < EUnrealEngineObjectUE5Version::PROPERTY_TAG_COMPLETE_TYPE_NAME)
	{
		return ReadLegacyTagRemainder(Document, Reader, OutTag, OutError);
	}

	if (!ReadCompletePropertyType(Document, Reader, OutTag.Type, OutError))
	{
		return false;
	}

	Reader << OutTag.Size;

	if (Reader.IsError())
	{
		OutError = FText::Format(NSLOCTEXT("AssetPropertyTagDecoder", "ModernPropertySizeReadFailed", "Could not read size for property '{0}'."), FText::FromString(OutTag.ResolvedName));
		return false;
	}

	return ReadModernTagRemainder(Document, Reader, OutTag, OutError);
}
