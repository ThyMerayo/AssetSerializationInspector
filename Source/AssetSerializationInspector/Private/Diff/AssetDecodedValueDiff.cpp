// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Diff/AssetDecodedValueDiff.h"

#include "Serialization/AssetPropertyValueDecoder.h"

static bool AreLeafValuesEqual(const FAssetDecodedPropertyValue& OldValue, const FAssetDecodedPropertyValue& NewValue)
{
	if (OldValue.TypeName != NewValue.TypeName)
	{
		return false;
	}

	if (!OldValue.Children.IsEmpty() || !NewValue.Children.IsEmpty())
	{
		/*
		 * For containers/structs, children are authoritative.
		 * The Value may merely say "3 elements".
		 */
		return true;
	}

	return OldValue.Value == NewValue.Value;
}

struct FDecodedChildKey
{
	FString Name;
	FString TypeName;

	bool operator==(const FDecodedChildKey& Other) const { return Name == Other.Name && TypeName == Other.TypeName; }
};

uint32 GetTypeHash(const FDecodedChildKey& Key)
{
	return HashCombine(GetTypeHash(Key.Name), GetTypeHash(Key.TypeName));
}

using FDecodedChildMap = TMap<FDecodedChildKey, const FAssetDecodedPropertyValue*>;

static FDecodedChildMap BuildChildMap(const FAssetDecodedPropertyValue& Value)
{
	FDecodedChildMap Result;

	for (const FAssetDecodedPropertyValue& Child : Value.Children)
	{
		FDecodedChildKey Key;
		Key.Name = Child.Name;
		Key.TypeName = Child.TypeName;
		Result.Add(MoveTemp(Key), &Child);
	}

	return Result;
}

static void CompareNamedChildren(const FAssetDecodedPropertyValue& OldValue, const FAssetDecodedPropertyValue& NewValue, FAssetDecodedValueDiff& OutDiff)
{
	const FDecodedChildMap OldChildren = BuildChildMap(OldValue);
	const FDecodedChildMap NewChildren = BuildChildMap(NewValue);
	TSet<FDecodedChildKey> Keys;

	for (const auto& Pair : OldChildren)
	{
		Keys.Add(Pair.Key);
	}

	for (const auto& Pair : NewChildren)
	{
		Keys.Add(Pair.Key);
	}

	for (const FDecodedChildKey& Key : Keys)
	{
		const FAssetDecodedPropertyValue* const* OldFound = OldChildren.Find(Key);
		const FAssetDecodedPropertyValue* const* NewFound = NewChildren.Find(Key);
		const FAssetDecodedPropertyValue* OldChild = OldFound != nullptr ? *OldFound : nullptr;
		const FAssetDecodedPropertyValue* NewChild = NewFound != nullptr ? *NewFound : nullptr;
		OutDiff.Children.Add(FAssetDecodedValueDiffer::Compare(OldChild, NewChild));
	}
}

static void CompareArrayChildren(const FAssetDecodedPropertyValue& OldValue, const FAssetDecodedPropertyValue& NewValue, FAssetDecodedValueDiff& OutDiff)
{
	const int32 CommonCount = FMath::Min(OldValue.Children.Num(), NewValue.Children.Num());

	for (int32 Index = 0; Index < CommonCount; ++Index)
	{
		OutDiff.Children.Add(FAssetDecodedValueDiffer::Compare(&OldValue.Children[Index], &NewValue.Children[Index]));
	}

	for (int32 Index = CommonCount; Index < OldValue.Children.Num(); ++Index)
	{
		OutDiff.Children.Add(FAssetDecodedValueDiffer::Compare(&OldValue.Children[Index], nullptr));
	}

	for (int32 Index = CommonCount; Index < NewValue.Children.Num(); ++Index)
	{
		OutDiff.Children.Add(FAssetDecodedValueDiffer::Compare(nullptr, &NewValue.Children[Index]));
	}
}

static void CompareChildren(const FAssetDecodedPropertyValue& OldValue, const FAssetDecodedPropertyValue& NewValue, FAssetDecodedValueDiff& OutDiff)
{
	if (OldValue.Kind == EAssetDecodedValueKind::Array && NewValue.Kind == EAssetDecodedValueKind::Array)
	{
		CompareArrayChildren(OldValue, NewValue, OutDiff);

		return;
	}

	CompareNamedChildren(OldValue, NewValue, OutDiff);
}

FAssetDecodedValueDiff FAssetDecodedValueDiffer::Compare(const FAssetDecodedPropertyValue* OldValue, const FAssetDecodedPropertyValue* NewValue)
{
	FAssetDecodedValueDiff Result;

	if (OldValue == nullptr && NewValue == nullptr)
	{
		return Result;
	}

	if (OldValue == nullptr)
	{
		Result.State = EAssetDecodedValueDiffState::Added;
		Result.Name = NewValue->Name;
		Result.TypeName = NewValue->TypeName;
		Result.NewValue = NewValue->Value;
		Result.bHasNewValue = true;

		for (const FAssetDecodedPropertyValue& Child : NewValue->Children)
		{
			Result.Children.Add(Compare(nullptr, &Child));
		}

		return Result;
	}

	if (NewValue == nullptr)
	{
		Result.State = EAssetDecodedValueDiffState::Removed;
		Result.Name = OldValue->Name;
		Result.TypeName = OldValue->TypeName;
		Result.OldValue = OldValue->Value;
		Result.bHasOldValue = true;

		for (const FAssetDecodedPropertyValue& Child : OldValue->Children)
		{
			Result.Children.Add(Compare(&Child, nullptr));
		}

		return Result;
	}

	Result.Name = !NewValue->Name.IsEmpty() ? NewValue->Name : OldValue->Name;
	Result.TypeName = !NewValue->TypeName.IsEmpty() ? NewValue->TypeName : OldValue->TypeName;
	Result.OldValue = OldValue->Value;
	Result.NewValue = NewValue->Value;
	Result.bHasOldValue = true;
	Result.bHasNewValue = true;

	if (OldValue != nullptr)
	{
		Result.OldOffset = OldValue->RelativeOffset;
		Result.OldSize = OldValue->Size;
	}

	if (NewValue != nullptr)
	{
		Result.NewOffset = NewValue->RelativeOffset;
		Result.NewSize = NewValue->Size;
	}

	CompareChildren(*OldValue, *NewValue, Result);

	const bool bLeafChanged = !AreLeafValuesEqual(*OldValue, *NewValue);
	bool bChildChanged = false;

	for (const FAssetDecodedValueDiff& Child : Result.Children)
	{
		if (Child.State != EAssetDecodedValueDiffState::Unchanged)
		{
			bChildChanged = true;
			break;
		}
	}

	Result.State = bLeafChanged || bChildChanged ? EAssetDecodedValueDiffState::Modified : EAssetDecodedValueDiffState::Unchanged;
	return Result;
}
