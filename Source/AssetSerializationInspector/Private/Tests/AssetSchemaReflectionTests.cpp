// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

#include "Serialization/AssetSchemaReflection.h"
#include "Serialization/AssetSerializedPropertyTag.h"

namespace
{
	FAssetSerializedPropertyType MakeNode(const TCHAR* Name, TArray<FAssetSerializedPropertyType> Parameters = {})
	{
		FAssetSerializedPropertyType Type;
		Type.Name = Name;
		Type.Parameters = MoveTemp(Parameters);
		return Type;
	}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSchemaReflection_FindsNativeStructsAndCompletesTheirFields,
	"AssetSerializationInspector.Serialization.AssetSchemaReflection.FindsNativeStructsAndCompletesTheirFields", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSchemaReflection_FindsNativeStructsAndCompletesTheirFields::RunTest(const FString& Parameters)
{
	TestNotNull(TEXT("A native struct is found by name"), AssetSchemaReflection::FindNativeStruct(TEXT("Guid")));
	TestNull(TEXT("An unknown struct is not found"), AssetSchemaReflection::FindNativeStruct(TEXT("NoSuchStructInAnyModule")));
	TestNull(TEXT("None is not a struct"), AssetSchemaReflection::FindNativeStruct(TEXT("None")));

	// A struct field the tag left unnamed takes its type from the reflected property.
	const UStruct* Transform = AssetSchemaReflection::FindNativeStruct(TEXT("Transform"));
	if (TestNotNull(TEXT("Transform is found"), Transform))
	{
		FAssetSerializedPropertyType Type = MakeNode(TEXT("StructProperty"));
		TestTrue(TEXT("The unnamed struct is completed"), AssetSchemaReflection::CompleteType(Transform, TEXT("Rotation"), Type));
		if (TestEqual(TEXT("It gains its struct"), Type.Parameters.Num(), 1))
		{
			TestEqual(TEXT("The struct is the rotation's type"), Type.Parameters[0].Name, FString(TEXT("Quat")));
		}

		TestFalse(TEXT("A type that already names its struct is left alone"), AssetSchemaReflection::CompleteType(Transform, TEXT("Rotation"), Type));

		FAssetSerializedPropertyType Mismatch = MakeNode(TEXT("IntProperty"));
		TestFalse(TEXT("A tag of another type than the property is left alone"), AssetSchemaReflection::CompleteType(Transform, TEXT("Rotation"), Mismatch));

		FAssetSerializedPropertyType Unknown = MakeNode(TEXT("StructProperty"));
		TestFalse(TEXT("A property the struct does not have is left alone"), AssetSchemaReflection::CompleteType(Transform, TEXT("NoSuchField"), Unknown));
		TestFalse(TEXT("Without an owner nothing changes"), AssetSchemaReflection::CompleteType(nullptr, TEXT("Rotation"), Unknown));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSchemaReflection_CompletesContainerElements, "AssetSerializationInspector.Serialization.AssetSchemaReflection.CompletesContainerElements",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSchemaReflection_CompletesContainerElements::RunTest(const FString& Parameters)
{
	// Any reflected map or set with struct elements will do: find one rather than depend on a particular class.
	const UStruct* MapOwner = nullptr;
	FString MapName;
	FString KeyStruct;
	FString ValueStruct;
	const UStruct* SetOwner = nullptr;
	FString SetName;
	FString SetStruct;

	for (TObjectIterator<UStruct> It; It; ++It)
	{
		for (TFieldIterator<FProperty> Field(*It, EFieldIterationFlags::None); Field; ++Field)
		{
			if (const FMapProperty* Map = CastField<FMapProperty>(*Field))
			{
				const FStructProperty* Key = CastField<FStructProperty>(Map->KeyProp);
				const FStructProperty* Value = CastField<FStructProperty>(Map->ValueProp);
				if (MapOwner == nullptr && Value != nullptr)
				{
					MapOwner = *It;
					MapName = Map->GetName();
					KeyStruct = Key != nullptr ? Key->Struct->GetName() : FString();
					ValueStruct = Value->Struct->GetName();
				}
			}
			else if (const FSetProperty* Set = CastField<FSetProperty>(*Field))
			{
				if (const FStructProperty* Element = CastField<FStructProperty>(Set->ElementProp); Element != nullptr && SetOwner == nullptr)
				{
					SetOwner = *It;
					SetName = Set->GetName();
					SetStruct = Element->Struct->GetName();
				}
			}
		}

		if (MapOwner != nullptr && SetOwner != nullptr)
		{
			break;
		}
	}

	if (TestNotNull(TEXT("The editor has a map with struct values"), MapOwner))
	{
		const FMapProperty* Map = CastField<FMapProperty>(FindFProperty<FProperty>(MapOwner, *MapName));
		FAssetSerializedPropertyType Key = MakeNode(*Map->KeyProp->GetClass()->GetName());
		FAssetSerializedPropertyType Value = MakeNode(TEXT("StructProperty"));
		FAssetSerializedPropertyType Type = MakeNode(TEXT("MapProperty"), { Key, Value });

		TestTrue(TEXT("The map is completed"), AssetSchemaReflection::CompleteType(MapOwner, MapName, Type));
		if (TestEqual(TEXT("It keeps its key and value"), Type.Parameters.Num(), 2) && TestEqual(TEXT("The value gains its struct"), Type.Parameters[1].Parameters.Num(), 1))
		{
			TestEqual(TEXT("The value struct is the property's"), Type.Parameters[1].Parameters[0].Name, ValueStruct);
		}
	}

	if (TestNotNull(TEXT("The editor has a set of structs"), SetOwner))
	{
		FAssetSerializedPropertyType Type = MakeNode(TEXT("SetProperty"), { MakeNode(TEXT("StructProperty")) });

		TestTrue(TEXT("The set is completed"), AssetSchemaReflection::CompleteType(SetOwner, SetName, Type));
		if (TestEqual(TEXT("It keeps its element"), Type.Parameters.Num(), 1) && TestEqual(TEXT("The element gains its struct"), Type.Parameters[0].Parameters.Num(), 1))
		{
			TestEqual(TEXT("The element struct is the property's"), Type.Parameters[0].Parameters[0].Name, SetStruct);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetSchemaReflection_ExportsStructFieldDefaults, "AssetSerializationInspector.Serialization.AssetSchemaReflection.ExportsStructFieldDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetSchemaReflection_ExportsStructFieldDefaults::RunTest(const FString& Parameters)
{
	// A default constructed transform has unit scale and the identity rotation.
	FString Text;
	if (TestTrue(TEXT("A field default is exported"), AssetSchemaReflection::ExportStructFieldDefault(TEXT("Transform"), { TEXT("Scale3D") }, Text)))
	{
		TestTrue(TEXT("Scale is one"), Text.Contains(TEXT("X=1.0")));
	}

	if (TestTrue(TEXT("A field of a nested struct is exported"), AssetSchemaReflection::ExportStructFieldDefault(TEXT("Transform"), { TEXT("Rotation"), TEXT("W") }, Text)))
	{
		TestEqual(TEXT("The identity rotation has W of one"), Text, FString(TEXT("1.000000")));
	}

	TestFalse(TEXT("An unknown field is refused"), AssetSchemaReflection::ExportStructFieldDefault(TEXT("Transform"), { TEXT("NoSuchField") }, Text));
	TestFalse(TEXT("An unknown struct is refused"), AssetSchemaReflection::ExportStructFieldDefault(TEXT("NoSuchStructInAnyModule"), { TEXT("X") }, Text));
	TestFalse(
		TEXT("A path through a field that is not a struct is refused"), AssetSchemaReflection::ExportStructFieldDefault(TEXT("Transform"), { TEXT("Rotation"), TEXT("W"), TEXT("Deeper") }, Text));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
