// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/ArchiveSerializedPropertyChain.h"
#include "UObject/UnrealType.h"

#include "Trace/AssetSerializationTraceArchive.h"

FAssetSerializationTraceArchive::FAssetSerializationTraceArchive(FArchive& InInnerArchive, const int64 InBaseOffset /* = 0 */) : InnerArchive(InInnerArchive), BaseOffset(InBaseOffset)
{
	SetIsLoading(InnerArchive.IsLoading());
	SetIsSaving(InnerArchive.IsSaving());
	SetIsPersistent(InnerArchive.IsPersistent());

	SetUEVer(InnerArchive.UEVer());
	SetLicenseeUEVer(InnerArchive.LicenseeUEVer());

	SetEngineVer(InnerArchive.EngineVer());

	SetCustomVersions(InnerArchive.GetCustomVersions());
}

void FAssetSerializationTraceArchive::Serialize(void* Data, const int64 Num)
{
	if (Num <= 0)
	{
		InnerArchive.Serialize(Data, Num);
		return;
	}

	const int64 StartOffset = InnerArchive.Tell();

	FAssetSerializationTraceEvent Event;
	Event.Offset = StartOffset - BaseOffset;
	if (FProperty* Property = GetSerializedProperty())
	{
		Event.bHasPropertyContext = true;
		Event.LeafPropertyName = Property->GetName();
		Event.PropertyType = Property->GetClass()->GetName();
		Event.PropertyPath = BuildCurrentPropertyPath();
	}
	else
	{
		Event.bHasPropertyContext = false;
		Event.PropertyPath = TEXT("<native/unclassified>");
	}

	// Forward the actual byte operation.
	InnerArchive.Serialize(Data, Num);

	const int64 EndOffset = InnerArchive.Tell();
	Event.Size = FMath::Max<int64>(0, EndOffset - StartOffset);
	if (Event.Size == 0)
	{
		return;
	}

	// Merge consecutive writes that belong to the same property.
	if (!Events.IsEmpty())
	{
		FAssetSerializationTraceEvent& Previous = Events.Last();
		const bool bAdjacent = Previous.Offset + Previous.Size == Event.Offset;
		const bool bSameContext = Previous.bHasPropertyContext == Event.bHasPropertyContext && Previous.PropertyPath == Event.PropertyPath && Previous.PropertyType == Event.PropertyType;

		if (bAdjacent && bSameContext)
		{
			Previous.Size += Event.Size;
			return;
		}
	}

	Events.Add(MoveTemp(Event));
}

FString FAssetSerializationTraceArchive::BuildCurrentPropertyPath() const
{
	const FArchiveSerializedPropertyChain* Chain = GetSerializedPropertyChain();

	if (Chain == nullptr || Chain->GetNumProperties() == 0)
	{
		if (FProperty* Property = GetSerializedProperty())
		{
			return Property->GetName();
		}

		return TEXT("<native/unclassified>");
	}

	TArray<FString> Parts;

	const int32 NumProperties = Chain->GetNumProperties();

	Parts.Reserve(NumProperties);

	for (int32 Index = 0; Index < NumProperties; ++Index)
	{
		FProperty* Property = Chain->GetPropertyFromRoot(Index);

		if (Property != nullptr)
		{
			Parts.Add(Property->GetName());
		}
	}

	return Parts.IsEmpty() ? TEXT("<native/unclassified>") : FString::Join(Parts, TEXT("."));
}

FArchive& FAssetSerializationTraceArchive::operator<<(FObjectPtr& Value)
{
	InnerArchive << Value;
	return *this;
}