// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Serialization/AssetPropertyValueDecoder.h"
#include "Serialization/AssetSerializedPropertyTag.h"

struct FAssetPackageDocument;
class FProperty;
class UStruct;

/** One slot of an unversioned property stream: a property of the class (or struct) and what the stream holds for it. */
struct FAssetUnversionedValue
{
	const FProperty* Property = nullptr;

	/** The element of a fixed size array ("int Values[4]") this slot is, or 0. */
	int32 ArrayIndex = 0;

	/** The slot is zero (false, 0, None, empty): the zero mask says so and no bytes are stored. */
	bool bZero = false;

	/** The bytes of the value in the document. Empty for a zero slot. */
	int64 Offset = 0;
	int64 Size = 0;

	FAssetSerializedPropertyType Type;
	FAssetDecodedPropertyValue Decoded;
};

/**
 * Properties saved without tags (PKG_UnversionedProperties; cooked packages usually are). The stream is a header of 16 bit
 * fragments ("skip n properties, then m values follow", with a flag for fragments that hold zeroes and a last-fragment flag), a
 * bit mask of which of those values are zero, and then the non-zero values one after the other, in the order of the class's
 * property list. Nothing in the stream names a property or says how long a value is, so reading it needs the class: this uses the
 * classes and structs of the running editor, and so does not work for a class it does not have (such as a Blueprint class).
 */
namespace AssetUnversionedProperties
{
	/** Whether the package was saved with unversioned properties. */
	bool IsUsedBy(const FAssetPackageDocument& Document);

	/** The type a property has in a tagged package ("StructProperty(Vector)"), so that the value decoders can read its value. */
	FAssetSerializedPropertyType MakeType(const FProperty* Property);

	/** The name of an enum's value ("EAxis::X"), or the number when the editor has no such enum or value. */
	FString DescribeEnumValue(const FString& EnumName, int64 Value);

	/** What a zero slot of a type holds: 0, false, None, an empty string or container. */
	FAssetDecodedPropertyValue MakeZeroValue(const FAssetSerializedPropertyType& Type);

	/**
	 * Reads the properties of a struct or object that start at Offset and end at End, in order. Stops at the first value that cannot be
	 * decoded (Out holds the values read before it, OutEndOffset is where it starts; with an invalid header or the wrong class Out is empty and OutEndOffset is Offset), because without lengths
	 * nothing after it can be found.
	 *
	 * @param OutEndOffset Where reading stopped: after the last value read, or at the value that failed.
	 * @return false with OutError set when the header is invalid or a value could not be decoded.
	 */
	bool Read(const FAssetPackageDocument& Document, const UStruct* Struct, int64 Offset, int64 End, TArray<FAssetUnversionedValue>& Out, int64& OutEndOffset, FString& OutError);
} // namespace AssetUnversionedProperties
