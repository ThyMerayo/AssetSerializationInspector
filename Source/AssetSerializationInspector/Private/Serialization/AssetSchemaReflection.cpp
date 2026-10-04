// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetSchemaReflection.h"

#include "UObject/UnrealType.h"

#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetSerializedPropertyTag.h"

namespace
{
	constexpr int32 MaximumSuperDepth = 32;

	bool CompleteFromProperty(const FProperty* Property, FAssetSerializedPropertyType& Type)
	{
		if (Property == nullptr || Property->GetClass()->GetName() != Type.Name)
		{
			return false;
		}

		bool bChanged = false;

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (Type.Parameters.IsEmpty() && StructProperty->Struct != nullptr)
			{
				FAssetSerializedPropertyType& Struct = Type.Parameters.AddDefaulted_GetRef();
				Struct.Name = StructProperty->Struct->GetName();
				bChanged = true;
			}
		}
		else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			if (Type.Parameters.Num() == 1)
			{
				bChanged = CompleteFromProperty(ArrayProperty->Inner, Type.Parameters[0]);
			}
		}
		else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			if (Type.Parameters.Num() == 1)
			{
				bChanged = CompleteFromProperty(SetProperty->ElementProp, Type.Parameters[0]);
			}
		}
		else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			if (Type.Parameters.Num() == 2)
			{
				bChanged = CompleteFromProperty(MapProperty->KeyProp, Type.Parameters[0]);
				bChanged |= CompleteFromProperty(MapProperty->ValueProp, Type.Parameters[1]);
			}
		}

		return bChanged;
	}
} // namespace

UClass* AssetSchemaReflection::FindNativeClass(const FAssetPackageDocument& Document, const int32 ExportIndex)
{
	if (!Document.ExportMap.IsValidIndex(ExportIndex))
	{
		return nullptr;
	}

	FAssetPackageIndexReference Index = Document.ExportMap[ExportIndex].ClassIndex;

	for (int32 Guard = 0; Guard < MaximumSuperDepth; ++Guard)
	{
		if (Index.GetKind() == EAssetPackageIndexKind::Export)
		{
			if (!Document.ExportMap.IsValidIndex(Index.GetArrayIndex()))
			{
				return nullptr;
			}

			Index = Document.ExportMap[Index.GetArrayIndex()].SuperIndex;
			continue;
		}

		FString Path;
		if (Index.GetKind() != EAssetPackageIndexKind::Import || !Document.ResolvePackageIndexPath(Index, Path) || !Path.StartsWith(TEXT("/Script/")))
		{
			return nullptr;
		}

		return FindObject<UClass>(nullptr, *Path);
	}

	return nullptr;
}

const UStruct* AssetSchemaReflection::FindNativeStruct(const FString& StructName)
{
	if (StructName.IsEmpty() || StructName == TEXT("None"))
	{
		return nullptr;
	}

	return FindFirstObject<UScriptStruct>(*StructName, EFindFirstObjectOptions::NativeFirst);
}

bool AssetSchemaReflection::CompleteType(const UStruct* Owner, const FString& PropertyName, FAssetSerializedPropertyType& Type)
{
	if (Owner == nullptr)
	{
		return false;
	}

	return CompleteFromProperty(FindFProperty<FProperty>(Owner, *PropertyName), Type);
}

bool AssetSchemaReflection::ExportStructFieldDefault(const FString& StructName, const TArray<FString>& FieldPath, FString& OutText)
{
	const UScriptStruct* Struct = Cast<UScriptStruct>(FindNativeStruct(StructName));
	if (Struct == nullptr || FieldPath.IsEmpty())
	{
		return false;
	}

	FStructOnScope Instance(Struct);
	const UStruct* Owner = Struct;
	const void* Container = Instance.GetStructMemory();

	for (int32 Index = 0; Index < FieldPath.Num(); ++Index)
	{
		const FProperty* Property = FindFProperty<FProperty>(Owner, *FieldPath[Index]);
		if (Property == nullptr)
		{
			return false;
		}

		const void* Value = Property->ContainerPtrToValuePtr<void>(Container);
		if (Index == FieldPath.Num() - 1)
		{
			OutText.Reset();
			Property->ExportText_Direct(OutText, Value, Value, nullptr, PPF_None);
			return true;
		}

		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		if (StructProperty == nullptr || StructProperty->Struct == nullptr)
		{
			return false;
		}

		Owner = StructProperty->Struct;
		Container = Value;
	}

	return false;
}
