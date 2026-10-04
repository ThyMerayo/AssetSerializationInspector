// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetContainerFinalValue.h"

namespace
{
	FString GetMapValueKey(const FAssetDecodedPropertyValue& Entry)
	{
		return Entry.Children.IsValidIndex(1) ? FAssetPropertyValueDecoder::BuildSemanticValueKey(Entry.Children[1]) : FString();
	}

	FAssetDecodedPropertyValue MakeFinalElement(const FAssetDecodedPropertyValue& Source, const FString& Name)
	{
		FAssetDecodedPropertyValue Element = Source;
		Element.Name = Name;
		Element.ContainerOperation = EAssetDecodedContainerOperation::None;
		return Element;
	}

	bool ValidateDefaults(const FAssetDecodedPropertyValue& Serialized, const FAssetDecodedPropertyValue& Defaults, FString& OutError)
	{
		if (Defaults.Kind != Serialized.Kind || !Defaults.IsSuccess())
		{
			OutError = TEXT("The default value is not a successfully decoded container of the same kind.");
			return false;
		}

		for (const FAssetDecodedPropertyValue& Child : Defaults.Children)
		{
			if (Child.ContainerOperation == EAssetDecodedContainerOperation::Remove)
			{
				OutError = TEXT("The default value is itself a delta; it must be resolved to its final value first.");
				return false;
			}
		}

		return true;
	}

	void Finish(FAssetContainerFinalValue& OutFinal, const FAssetDecodedPropertyValue& Serialized, const bool bIsSet, TArray<FAssetDecodedPropertyValue>&& Elements)
	{
		OutFinal.Value = Serialized;
		OutFinal.Value.Children = MoveTemp(Elements);
		OutFinal.Value.ContainerMode = EAssetDecodedContainerSerializationMode::Full;
		OutFinal.Value.ContainerOperation = EAssetDecodedContainerOperation::None;
		OutFinal.Value.Value = bIsSet ? FString::Printf(TEXT("%d elements"), OutFinal.Value.Children.Num()) : FString::Printf(TEXT("%d entries"), OutFinal.Value.Children.Num());
	}

	bool ComputeSet(const FAssetDecodedPropertyValue& Serialized, const FAssetDecodedPropertyValue* Defaults, FAssetContainerFinalValue& OutFinal, FString& OutError)
	{
		TArray<const FAssetDecodedPropertyValue*> Removed;
		TArray<const FAssetDecodedPropertyValue*> Added;
		for (const FAssetDecodedPropertyValue& Child : Serialized.Children)
		{
			(Child.ContainerOperation == EAssetDecodedContainerOperation::Remove ? Removed : Added).Add(&Child);
		}

		const auto MakeFullResult = [&](const bool bWasFull) {
			TArray<FAssetDecodedPropertyValue> Elements;
			for (const FAssetDecodedPropertyValue* Element : Added)
			{
				Elements.Add(MakeFinalElement(*Element, FString::Printf(TEXT("Element[%d]"), Elements.Num())));
			}
			Finish(OutFinal, Serialized, true, MoveTemp(Elements));
			OutFinal.bSerializedAsFullContainer = bWasFull;
			OutFinal.Confidence = EAssetContainerFinalValueConfidence::Certain;
			return true;
		};

		if (Defaults == nullptr)
		{
			OutError = TEXT("The default set is required to reconstruct the final value.");
			return false;
		}

		if (!ValidateDefaults(Serialized, *Defaults, OutError))
		{
			return false;
		}

		TSet<FString> DefaultKeys;
		for (const FAssetDecodedPropertyValue& Child : Defaults->Children)
		{
			DefaultKeys.Add(Child.SemanticKey);
		}

		TSet<FString> RemovedKeys;
		for (const FAssetDecodedPropertyValue* Element : Removed)
		{
			if (!DefaultKeys.Contains(Element->SemanticKey))
			{
				OutError = FString::Printf(TEXT("The set removes '%s', which is not in its defaults."), *Element->Value);
				return false;
			}
			RemovedKeys.Add(Element->SemanticKey);
		}

		// A delta identical to its defaults is never serialized, so a present-but-empty set is a complete, empty one.
		if (Removed.IsEmpty() && Added.IsEmpty())
		{
			MakeFullResult(true);
			OutFinal.Confidence = DefaultKeys.IsEmpty() ? EAssetContainerFinalValueConfidence::Certain : EAssetContainerFinalValueConfidence::Inferred;
			return true;
		}

		bool bAnyAddedIsDefault = false;
		for (const FAssetDecodedPropertyValue* Element : Added)
		{
			bAnyAddedIsDefault |= DefaultKeys.Contains(Element->SemanticKey);
		}

		if (bAnyAddedIsDefault)
		{
			// A delta never repeats an element the defaults already have, so this set was written without defaults.
			if (!Removed.IsEmpty())
			{
				OutError = TEXT("The set both removes default elements and repeats unchanged ones.");
				return false;
			}

			return MakeFullResult(true);
		}

		TArray<FAssetDecodedPropertyValue> Elements;
		for (const FAssetDecodedPropertyValue& Child : Defaults->Children)
		{
			if (!RemovedKeys.Contains(Child.SemanticKey))
			{
				Elements.Add(MakeFinalElement(Child, FString::Printf(TEXT("Element[%d]"), Elements.Num())));
			}
		}

		for (const FAssetDecodedPropertyValue* Element : Added)
		{
			Elements.Add(MakeFinalElement(*Element, FString::Printf(TEXT("Element[%d]"), Elements.Num())));
		}

		Finish(OutFinal, Serialized, true, MoveTemp(Elements));
		OutFinal.bSerializedAsFullContainer = false;

		// Removals prove a delta. Without them, additions disjoint from non-empty defaults could equally be a complete set.
		const bool bAmbiguous = Removed.IsEmpty() && !Added.IsEmpty() && !DefaultKeys.IsEmpty();
		OutFinal.Confidence = bAmbiguous ? EAssetContainerFinalValueConfidence::Inferred : EAssetContainerFinalValueConfidence::Certain;
		return true;
	}

	bool ComputeMap(const FAssetDecodedPropertyValue& Serialized, const FAssetDecodedPropertyValue* Defaults, FAssetContainerFinalValue& OutFinal, FString& OutError)
	{
		TArray<const FAssetDecodedPropertyValue*> Removed;
		TArray<const FAssetDecodedPropertyValue*> Entries;
		for (const FAssetDecodedPropertyValue& Child : Serialized.Children)
		{
			(Child.ContainerOperation == EAssetDecodedContainerOperation::Remove ? Removed : Entries).Add(&Child);
		}

		const auto MakeFullResult = [&](const bool bWasFull) {
			TArray<FAssetDecodedPropertyValue> Elements;
			for (const FAssetDecodedPropertyValue* Entry : Entries)
			{
				Elements.Add(MakeFinalElement(*Entry, Entry->Name));
			}
			Finish(OutFinal, Serialized, false, MoveTemp(Elements));
			OutFinal.bSerializedAsFullContainer = bWasFull;
			OutFinal.Confidence = EAssetContainerFinalValueConfidence::Certain;
			return true;
		};

		// The engine marks maps that replace their defaults outright with a removal count of -1.
		if (Serialized.ContainerMode == EAssetDecodedContainerSerializationMode::Full)
		{
			return MakeFullResult(true);
		}

		if (Defaults == nullptr)
		{
			OutError = TEXT("The default map is required to reconstruct the final value.");
			return false;
		}

		if (!ValidateDefaults(Serialized, *Defaults, OutError))
		{
			return false;
		}

		TMap<FString, const FAssetDecodedPropertyValue*> DefaultsByKey;
		for (const FAssetDecodedPropertyValue& Child : Defaults->Children)
		{
			DefaultsByKey.Add(Child.SemanticKey, &Child);
		}

		TSet<FString> RemovedKeys;
		for (const FAssetDecodedPropertyValue* Entry : Removed)
		{
			if (!DefaultsByKey.Contains(Entry->SemanticKey))
			{
				OutError = FString::Printf(TEXT("The map removes key '%s', which is not in its defaults."), *Entry->SemanticKey);
				return false;
			}
			RemovedKeys.Add(Entry->SemanticKey);
		}

		// A delta identical to its defaults is never serialized, so a present-but-empty map is a complete, empty one.
		if (Removed.IsEmpty() && Entries.IsEmpty())
		{
			MakeFullResult(true);
			OutFinal.Confidence = DefaultsByKey.IsEmpty() ? EAssetContainerFinalValueConfidence::Certain : EAssetContainerFinalValueConfidence::Inferred;
			return true;
		}

		bool bAnyEntryRepeatsDefault = false;
		for (const FAssetDecodedPropertyValue* Entry : Entries)
		{
			const FAssetDecodedPropertyValue* const* Default = DefaultsByKey.Find(Entry->SemanticKey);
			bAnyEntryRepeatsDefault |= Default != nullptr && GetMapValueKey(**Default) == GetMapValueKey(*Entry);
		}

		if (bAnyEntryRepeatsDefault)
		{
			// A delta never writes an entry that matches its default, so this map was written without defaults.
			if (!Removed.IsEmpty())
			{
				OutError = TEXT("The map both removes default keys and repeats unchanged entries.");
				return false;
			}

			return MakeFullResult(true);
		}

		TMap<FString, const FAssetDecodedPropertyValue*> EntriesByKey;
		for (const FAssetDecodedPropertyValue* Entry : Entries)
		{
			EntriesByKey.Add(Entry->SemanticKey, Entry);
		}

		TArray<FAssetDecodedPropertyValue> Elements;
		TSet<FString> Emitted;
		for (const FAssetDecodedPropertyValue& Child : Defaults->Children)
		{
			if (RemovedKeys.Contains(Child.SemanticKey))
			{
				continue;
			}

			const FAssetDecodedPropertyValue* const* Override = EntriesByKey.Find(Child.SemanticKey);
			Elements.Add(MakeFinalElement(Override != nullptr ? **Override : Child, Child.Name));
			Emitted.Add(Child.SemanticKey);
		}

		for (const FAssetDecodedPropertyValue* Entry : Entries)
		{
			if (!Emitted.Contains(Entry->SemanticKey))
			{
				Elements.Add(MakeFinalElement(*Entry, Entry->Name));
				Emitted.Add(Entry->SemanticKey);
			}
		}

		Finish(OutFinal, Serialized, false, MoveTemp(Elements));
		OutFinal.bSerializedAsFullContainer = false;

		const bool bAmbiguous = Removed.IsEmpty() && !Entries.IsEmpty() && !DefaultsByKey.IsEmpty();
		OutFinal.Confidence = bAmbiguous ? EAssetContainerFinalValueConfidence::Inferred : EAssetContainerFinalValueConfidence::Certain;
		return true;
	}
} // namespace

bool AssetContainerFinalValue::Compute(const FAssetDecodedPropertyValue& Serialized, const FAssetDecodedPropertyValue* Defaults, FAssetContainerFinalValue& OutFinal, FString& OutError)
{
	OutFinal = FAssetContainerFinalValue();

	if (!Serialized.IsSuccess())
	{
		OutError = TEXT("The serialized container did not decode successfully.");
		return false;
	}

	switch (Serialized.Kind)
	{
		case EAssetDecodedValueKind::Set:
			return ComputeSet(Serialized, Defaults, OutFinal, OutError);

		case EAssetDecodedValueKind::Map:
			return ComputeMap(Serialized, Defaults, OutFinal, OutError);

		default:
			OutError = TEXT("Only sets and maps are serialized as deltas.");
			return false;
	}
}

bool AssetContainerFinalValue::CanAssumeEmptyDefaults(const FAssetDecodedPropertyValue& Serialized)
{
	for (const FAssetDecodedPropertyValue& Child : Serialized.Children)
	{
		if (Child.ContainerOperation == EAssetDecodedContainerOperation::Remove)
		{
			return false;
		}
	}

	return Serialized.Kind == EAssetDecodedValueKind::Set || Serialized.Kind == EAssetDecodedValueKind::Map;
}

bool AssetContainerFinalValue::ResolveContainersInArrayElements(FAssetDecodedPropertyValue& Value, FString& InOutNote, const bool bInsideElement)
{
	bool bReplaced = false;

	for (FAssetDecodedPropertyValue& Child : Value.Children)
	{
		const bool bContainer = Child.Kind == EAssetDecodedValueKind::Set || Child.Kind == EAssetDecodedValueKind::Map;
		if (bContainer)
		{
			FAssetDecodedPropertyValue EmptyDefaults;
			EmptyDefaults.Status = EAssetPropertyDecodeStatus::Success;
			EmptyDefaults.Kind = Child.Kind;

			FAssetContainerFinalValue Final;
			FString Error;
			if (bInsideElement && CanAssumeEmptyDefaults(Child) && Compute(Child, &EmptyDefaults, Final, Error))
			{
				const FString Name = Child.Name;
				Child = MoveTemp(Final.Value);
				Child.Name = Name;
				bReplaced = true;

				const FString Note = TEXT("Containers inside array elements are shown against the empty defaults of their struct.");
				if (!InOutNote.Contains(Note))
				{
					InOutNote += (InOutNote.IsEmpty() ? TEXT("") : TEXT(" ")) + Note;
				}
			}
		}
		else if (Child.Kind == EAssetDecodedValueKind::Array || Child.Kind == EAssetDecodedValueKind::Struct)
		{
			bReplaced |= ResolveContainersInArrayElements(Child, InOutNote, bInsideElement || Value.Kind == EAssetDecodedValueKind::Array);
		}
	}

	return bReplaced;
}
