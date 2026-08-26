// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Model/AssetPackageDocument.h"

enum class EAssetPropertyTagSerializeType : uint8
{
	Property,
	BinaryOrNative,
	Skipped
};

struct FAssetSerializedPropertyType
{
	FString Name;

	TArray<FAssetSerializedPropertyType> Parameters;

	FString ToString() const
	{
		if (Parameters.IsEmpty())
		{
			return Name;
		}

		TArray<FString> Parts;
		Parts.Reserve(Parameters.Num());

		for (const FAssetSerializedPropertyType& Parameter : Parameters)
		{
			Parts.Add(Parameter.ToString());
		}

		return FString::Printf(TEXT("%s(%s)"), *Name, *FString::Join(Parts, TEXT(", ")));
	}
};

struct FAssetSerializedPropertyTag
{
	FAssetPackageNameReference Name;
	FString ResolvedName;

	FAssetSerializedPropertyType Type;

	int32 Size = 0;
	int32 ArrayIndex = 0;

	uint8 RawFlags = 0;

	bool bHasPropertyGuid = false;
	FGuid PropertyGuid;

	bool bBoolValue = false;

	EAssetPropertyTagSerializeType SerializeType = EAssetPropertyTagSerializeType::Property;

	uint32 RawExtensions = 0;

	uint8 OverrideOperation = 0;
	bool bExperimentalOverridableLogic = false;
	bool bExperimentalExternalObjects = false;

	int64 TagOffset = 0;
	int64 TagSize = 0;
	int64 ValueOffset = 0;

	bool IsTerminator() const { return ResolvedName == TEXT("None"); }
};