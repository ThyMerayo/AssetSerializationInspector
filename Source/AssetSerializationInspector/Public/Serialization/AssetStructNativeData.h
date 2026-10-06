// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Serialization/AssetBytecode.h"

struct FAssetPackageDocument;
struct FAssetPackageExportEntry;

/** One property a class or function declares (an FProperty of a UStruct): what its type and flags are. */
struct FAssetFieldDefinition
{
	FString Name;

	/** The type, with what it refers to: "IntProperty", "ObjectProperty (/Script/Engine.Actor)", "ArrayProperty<StrProperty>". */
	FString Type;

	int32 ArrayDim = 1;
	int32 ElementSize = 0;
	uint64 PropertyFlags = 0;

	/** The function called when a replicated property changes, when it has one. */
	FString RepNotifyFunction;

	/** Metadata stored with the property (the editor keeps categories, tooltips and the like here). */
	TArray<TPair<FString, FString>> MetaData;

	/** The bytes of the definition in the document. */
	int64 Offset = 0;
	int64 Size = 0;

	/** One line: type, then the flags that are set. */
	FString Describe() const;
};

/**
 * What a class or a function writes after its tagged properties, decoded (UStruct::Serialize, then UClass::Serialize or
 * UFunction::Serialize): the parent, the child functions, the properties it declares, the size of its bytecode, and for a class its
 * function map, flags, interfaces and default object. This is the part of a Blueprint that changes when a variable is added.
 */
struct FAssetStructNativeData
{
	/** The whole range was read and nothing was left over. When false, Error says where it stopped and the rest is what was read. */
	bool bComplete = false;
	FString Error;

	/** The bytes the data covers in the document. */
	int64 Offset = 0;
	int64 Size = 0;

	bool bIsClass = false;
	bool bIsFunction = false;

	/** The object GUID the engine can store with any object (empty when there is none). */
	FString ObjectGuid;

	FString SuperStruct;

	/** The functions the struct owns, as export or import paths. */
	TArray<FString> Children;

	/** The properties it declares, in the order they are stored. */
	TArray<FAssetFieldDefinition> Fields;

	/** The size of the bytecode in memory and on disk, and where its bytes start in the document. */
	int32 BytecodeSize = 0;
	int32 BytecodeStorageSize = 0;
	int64 BytecodeOffset = 0;

	/** The bytecode read statement by statement; Bytecode.bComplete is false when it could not be (Bytecode.Error says why). */
	FAssetBytecode Bytecode;

	// ---- a class ----
	TArray<TPair<FString, FString>> FunctionMap;
	uint32 ClassFlags = 0;
	FString ClassWithin;
	FString ClassConfigName;
	FString GeneratedBy;
	TArray<FString> Interfaces;
	bool bDeprecatedForceScriptOrder = false;
	bool bCooked = false;
	FString DefaultObject;

	// ---- a function ----
	uint32 FunctionFlags = 0;
	FString EventGraphFunction;
	int32 EventGraphCallOffset = 0;

	/** A short account: how many properties, children, bytes of bytecode. */
	FString Summarize() const;
};

/** One difference between the native data of two versions of a class or function. */
struct FAssetNativeDataChange
{
	enum class EState : uint8
	{
		Added,
		Removed,
		Modified
	};

	/** A stable name for matching and paths ("Property/NewVar_12"). */
	FString Key;

	/** What changed, for people ("Variable NewVar_12"). */
	FString Title;

	EState State = EState::Modified;
	FString OldValue;
	FString NewValue;
};

namespace AssetStructNativeData
{
	/**
	 * Decodes the native data of a class or function export. Returns false, without touching Out, for any other kind of export: the
	 * export's class is followed through the Blueprint classes of the package to the native class it derives from, which must be a
	 * UClass or a UFunction of the running editor.
	 *
	 * @param NativeOffset Where the native data starts in the document (right after the tagged properties).
	 * @param NativeSize How many bytes it has.
	 */
	bool Decode(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, int64 NativeOffset, int64 NativeSize, FAssetStructNativeData& Out);

	/**
	 * What differs between the native data of two versions: properties, child functions, function map, interfaces, parent, flags,
	 * default object and bytecode, each as one change. Empty when nothing the data holds differs.
	 */
	TArray<FAssetNativeDataChange> Compare(const FAssetPackageDocument& OldDocument, const FAssetStructNativeData& Old, const FAssetPackageDocument& NewDocument, const FAssetStructNativeData& New);

	/** The names of the property flags that are set ("Edit, BlueprintVisible, ..."), and the remaining bits in hex. */
	FString DescribePropertyFlags(uint64 Flags);
	FString DescribeFunctionFlags(uint32 Flags);
	FString DescribeClassFlags(uint32 Flags);
} // namespace AssetStructNativeData
