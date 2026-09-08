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

static bool AreDecodedValuesEquivalent(const FAssetDecodedPropertyValue& A, const FAssetDecodedPropertyValue& B)
{
	if (A.Kind != B.Kind)
	{
		return false;
	}

	if (A.TypeName != B.TypeName)
	{
		return false;
	}

	if (!A.SemanticKey.IsEmpty() && !B.SemanticKey.IsEmpty())
	{
		return A.TypeName == B.TypeName && A.SemanticKey == B.SemanticKey;
	}

	if (A.Kind == EAssetDecodedValueKind::Scalar)
	{
		return A.Value == B.Value;
	}

	if (A.Children.Num() != B.Children.Num())
	{
		return false;
	}

	for (int32 Index = 0; Index < A.Children.Num(); ++Index)
	{
		if (!AreDecodedValuesEquivalent(A.Children[Index], B.Children[Index]))
		{
			return false;
		}
	}

	return true;
}

static bool AreSameArrayElement(const FAssetDecodedPropertyValue& A, const FAssetDecodedPropertyValue& B)
{
	if (A.TypeName != B.TypeName)
	{
		return false;
	}

	if (!A.SemanticKey.IsEmpty() && !B.SemanticKey.IsEmpty())
	{
		return A.SemanticKey == B.SemanticKey;
	}

	/*
	 * For scalar arrays, value equality is the only
	 * identity information we have.
	 */
	return AreDecodedValuesEquivalent(A, B);
}

static TArray<int32> BuildLcsTable(const TArray<FAssetDecodedPropertyValue>& OldValues, const TArray<FAssetDecodedPropertyValue>& NewValues)
{
	const int32 OldCount = OldValues.Num();
	const int32 NewCount = NewValues.Num();

	TArray<int32> Table;
	Table.SetNumZeroed((OldCount + 1) * (NewCount + 1));

	auto At = [NewCount, &Table](const int32 OldIndex, const int32 NewIndex) -> int32& { return Table[OldIndex * (NewCount + 1) + NewIndex]; };

	for (int32 OldIndex = OldCount - 1; OldIndex >= 0; --OldIndex)
	{
		for (int32 NewIndex = NewCount - 1; NewIndex >= 0; --NewIndex)
		{
			if (AreSameArrayElement(OldValues[OldIndex], NewValues[NewIndex]))
			{
				At(OldIndex, NewIndex) = 1 + At(OldIndex + 1, NewIndex + 1);
			}
			else
			{
				At(OldIndex, NewIndex) = FMath::Max(At(OldIndex + 1, NewIndex), At(OldIndex, NewIndex + 1));
			}
		}
	}

	return Table;
}

static void CompareArrayChildrenByIndex(const FAssetDecodedPropertyValue& OldValue, const FAssetDecodedPropertyValue& NewValue, FAssetDecodedValueDiff& OutDiff)
{
	const TArray<FAssetDecodedPropertyValue>& OldChildren = OldValue.Children;
	const TArray<FAssetDecodedPropertyValue>& NewChildren = NewValue.Children;

	const int32 MinCount = FMath::Min(OldChildren.Num(), NewChildren.Num());

	for (int Index = 0; Index < MinCount; ++Index)
	{
		FAssetDecodedValueDiff ChildDiff = FAssetDecodedValueDiffer::Compare(&OldChildren[Index], &NewChildren[Index]);
		ChildDiff.Name = FString::Printf(TEXT("[%d]"), Index);
		OutDiff.Children.Add(MoveTemp(ChildDiff));
	}
	for (int Index = MinCount; Index < OldChildren.Num(); ++Index)
	{
		FAssetDecodedValueDiff ChildDiff = FAssetDecodedValueDiffer::Compare(&OldChildren[Index], nullptr);
		ChildDiff.Name = FString::Printf(TEXT("[%d]"), Index);
		OutDiff.Children.Add(MoveTemp(ChildDiff));
	}
	for (int Index = MinCount; Index < NewChildren.Num(); ++Index)
	{
		FAssetDecodedValueDiff ChildDiff = FAssetDecodedValueDiffer::Compare(nullptr, &NewChildren[Index]);
		ChildDiff.Name = FString::Printf(TEXT("[%d]"), Index);
		OutDiff.Children.Add(MoveTemp(ChildDiff));
	}
}

static void CompareArrayChildren(const FAssetDecodedPropertyValue& OldValue, const FAssetDecodedPropertyValue& NewValue, FAssetDecodedValueDiff& OutDiff)
{
	static constexpr int64 MaximumLcsCells = 4000000;
	const int64 CellCount = static_cast<int64>(OldValue.Children.Num() + 1) * static_cast<int64>(NewValue.Children.Num() + 1);

	if (CellCount > MaximumLcsCells)
	{
		CompareArrayChildrenByIndex(OldValue, NewValue, OutDiff);
		return;
	}

	const TArray<FAssetDecodedPropertyValue>& OldChildren = OldValue.Children;
	const TArray<FAssetDecodedPropertyValue>& NewChildren = NewValue.Children;
	const int32 OldCount = OldChildren.Num();
	const int32 NewCount = NewChildren.Num();
	const TArray<int32> Table = BuildLcsTable(OldChildren, NewChildren);
	auto At = [NewCount, &Table](const int32 OldIndex, const int32 NewIndex) { return Table[OldIndex * (NewCount + 1) + NewIndex]; };

	int32 OldIndex = 0;
	int32 NewIndex = 0;

	while (OldIndex < OldCount || NewIndex < NewCount)
	{
		if (OldIndex < OldCount && NewIndex < NewCount && AreSameArrayElement(OldChildren[OldIndex], NewChildren[NewIndex]))
		{
			FAssetDecodedValueDiff ChildDiff = FAssetDecodedValueDiffer::Compare(&OldChildren[OldIndex], &NewChildren[NewIndex]);
			ChildDiff.Name = FString::Printf(TEXT("[%d]"), NewIndex);
			OutDiff.Children.Add(MoveTemp(ChildDiff));
			++OldIndex;
			++NewIndex;
			continue;
		}

		const bool bPreferRemove = OldIndex < OldCount && (NewIndex >= NewCount || At(OldIndex + 1, NewIndex) >= At(OldIndex, NewIndex + 1));
		if (bPreferRemove)
		{
			FAssetDecodedValueDiff ChildDiff = FAssetDecodedValueDiffer::Compare(&OldChildren[OldIndex], nullptr);
			ChildDiff.Name = FString::Printf(TEXT("[%d]"), OldIndex);
			OutDiff.Children.Add(MoveTemp(ChildDiff));
			++OldIndex;
		}
		else
		{
			FAssetDecodedValueDiff ChildDiff = FAssetDecodedValueDiffer::Compare(nullptr, &NewChildren[NewIndex]);
			ChildDiff.Name = FString::Printf(TEXT("[%d]"), NewIndex);
			OutDiff.Children.Add(MoveTemp(ChildDiff));
			++NewIndex;
		}
	}
}

static void CompareChildren(const FAssetDecodedPropertyValue& OldValue, const FAssetDecodedPropertyValue& NewValue, FAssetDecodedValueDiff& OutDiff)
{
	if (OldValue.Kind == EAssetDecodedValueKind::Array && NewValue.Kind == EAssetDecodedValueKind::Array)
	{
		CompareArrayChildren(OldValue, NewValue, OutDiff);
	}
	else
	{
		CompareNamedChildren(OldValue, NewValue, OutDiff);
	}
}

static void CollapseArrayReplacements(TArray<FAssetDecodedValueDiff>& Children)
{
	for (int32 Index = 0; Index + 1 < Children.Num(); ++Index)
	{
		FAssetDecodedValueDiff& First = Children[Index];
		FAssetDecodedValueDiff& Second = Children[Index + 1];

		if (First.State == EAssetDecodedValueDiffState::Removed && Second.State == EAssetDecodedValueDiffState::Added && First.TypeName == Second.TypeName)
		{
			FAssetDecodedValueDiff Replacement;
			Replacement.State = EAssetDecodedValueDiffState::Modified;
			Replacement.Name = Second.Name;
			Replacement.TypeName = Second.TypeName;
			Replacement.bHasOldValue = First.bHasOldValue;
			Replacement.OldValue = First.OldValue;
			Replacement.bHasNewValue = Second.bHasNewValue;
			Replacement.NewValue = Second.NewValue;
			Replacement.OldOffset = First.OldOffset;
			Replacement.OldSize = First.OldSize;
			Replacement.NewOffset = Second.NewOffset;
			Replacement.NewSize = Second.NewSize;
			Children[Index] = MoveTemp(Replacement);
			Children.RemoveAt(Index + 1);

			--Index;
		}
	}
}

static bool CanTreatAsReplacement(const FAssetDecodedValueDiff& Removed, const FAssetDecodedValueDiff& Added)
{
	if (Removed.TypeName != Added.TypeName)
	{
		return false;
	}

	return true;
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

	CollapseArrayReplacements(Result.Children);

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
