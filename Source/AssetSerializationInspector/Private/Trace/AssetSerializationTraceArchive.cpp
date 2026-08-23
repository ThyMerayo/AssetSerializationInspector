// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Trace/AssetSerializationTraceArchive.h"

#include "Serialization/ArchiveSerializedPropertyChain.h"
#include "UObject/UnrealType.h"

FAssetSerializationTraceArchive::FAssetSerializationTraceArchive(FArchive& InInnerArchive, const int64 InBaseOffset) : FArchiveProxy(InInnerArchive), BaseOffset(InBaseOffset) {}

void FAssetSerializationTraceArchive::Serialize(void* Data, const int64 Num)
{
	if (Num <= 0)
	{
		FArchiveProxy::Serialize(Data, Num);
		return;
	}

	const int64 StartOffset = InnerArchive.Tell();

	FAssetSerializationTraceEvent Event;
	Event.Offset = StartOffset - BaseOffset;

	Event.Size = Num;

	if (FProperty* Property = GetSerializedProperty())
	{
		Event.bHasPropertyContext = true;
		Event.LeafPropertyName = Property->GetName();
		Event.PropertyType = Property->GetClass()->GetName();
		Event.PropertyPath = BuildCurrentPropertyPath();
	}
	else
	{
		Event.PropertyPath = TEXT("<native/unclassified>");
	}

	FArchiveProxy::Serialize(Data, Num);

	if (!Events.IsEmpty())
	{
		FAssetSerializationTraceEvent& Previous = Events.Last();

		const bool bAdjacent = Previous.Offset + Previous.Size == Event.Offset;

		const bool bSameContext = Previous.PropertyPath == Event.PropertyPath && Previous.PropertyType == Event.PropertyType && Previous.bHasPropertyContext == Event.bHasPropertyContext;

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