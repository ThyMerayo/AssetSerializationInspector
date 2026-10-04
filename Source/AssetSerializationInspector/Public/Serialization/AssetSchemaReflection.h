// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

struct FAssetPackageDocument;
struct FAssetSerializedPropertyType;
class UClass;
class UStruct;

/**
 * What the running editor knows about native classes and structs. Packages saved before UE 5.4 do not name the struct of a map,
 * set or array element, and a native struct cannot be told from a tagged one without its definition, so the missing names are
 * read from the live reflection data here.
 */
namespace AssetSchemaReflection
{
	/**
	 * Follows an export's class (through the Blueprint classes generated in the package) to the native class it derives from and
	 * returns the class when the running editor has it loaded.
	 */
	UClass* FindNativeClass(const FAssetPackageDocument& Document, int32 ExportIndex);

	/** A native struct by its name without prefix ("Guid"), or null when no loaded module declares it. */
	const UStruct* FindNativeStruct(const FString& StructName);

	/**
	 * Fills in the struct names a tag left out, from the property of the same name on Owner. Nothing is changed when the owner
	 * has no such property or when its type does not match the tag's. Returns true when a name was added.
	 */
	bool CompleteType(const UStruct* Owner, const FString& PropertyName, FAssetSerializedPropertyType& Type);
} // namespace AssetSchemaReflection
