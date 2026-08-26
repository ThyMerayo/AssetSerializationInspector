// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetPropertyTagDecoder.h"

#include "UObject/PropertyTypeName.h"

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

static bool ReadLegacyPropertyType(const FAssetPackageDocument& Document, FAssetPackagePayloadReader& Reader, FAssetSerializedPropertyTag& OutTag, FText& OutError)
{
	FString TypeName;
	if (!Reader.ReadResolvedName(TypeName))
	{
		OutError = NSLOCTEXT("AssetPropertyTagDecoder", "PropertyTypeReadFailed", "Could not read property type.");
		return false;
	}

	return true;
}

static bool ConvertPropertyTypeName(const UE::FPropertyTypeName& InType, FAssetSerializedPropertyType& OutType, FText& OutError, const int32 Depth = 0)
{
	constexpr int32 MaxDepth = 32;

	if (Depth >= MaxDepth)
	{
		OutError = NSLOCTEXT("AssetPropertyTagDecoder", "PropertyTypeDepthExceeded", "Property type nesting exceeds the supported depth.");
	}

	OutType = {};

	if (InType.IsEmpty())
	{
		return true;
	}

	OutType.Name = InType.GetName().ToString();

	const int32 ParameterCount = InType.GetParameterCount();

	if (ParameterCount < 0 || ParameterCount > 64)
	{
		OutError = FText::Format(NSLOCTEXT("AssetPropertyTagDecoder", "PropertyTypeParameterCountInvalid", "Property type '{0}' has an invalid parameter count of {1}."),
			FText::FromString(OutType.Name), FText::AsNumber(ParameterCount));
		return false;
	}

	OutType.Parameters.Reserve(ParameterCount);

	for (int32 Index = 0; Index < ParameterCount; ++Index)
	{
		FAssetSerializedPropertyType Parameter;

		if (!ConvertPropertyTypeName(InType.GetParameter(Index), Parameter, OutError, Depth + 1))
		{
			return false;
		}

		OutType.Parameters.Add(MoveTemp(Parameter));
	}

	return true;
}

static bool ReadCompletePropertyType(const FAssetPackageDocument& Document, FAssetPackagePayloadReader& Reader, FAssetSerializedPropertyType& OutType, FText& OutError)
{
	UE::FPropertyTypeName EngineType;
	Reader << EngineType;

	if (Reader.IsError())
	{
		OutError = NSLOCTEXT("AssetPropertyTagDecoder", "PropertyTypeNameReadFailed", "Could not deserialize the complete property type name.");
		return false;
	}

	return ConvertPropertyTypeName(EngineType, OutType, OutError);
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

	const FPackageFileVersion Version = Reader.UEVer();

	if (Version < EUnrealEngineObjectUE5Version::PROPERTY_TAG_COMPLETE_TYPE_NAME)
	{
		return ReadLegacyPropertyType(Document, Reader, OutTag, OutError);
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
