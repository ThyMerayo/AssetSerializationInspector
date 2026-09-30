// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Diff/AssetByteDiff.h"

#include "Model/AssetPackageDocument.h"

TArray<FAssetByteDiffSpan> FAssetByteDiff::Compare(
	const FAssetPackageDocument& OldDoc, const int64 OldOffset, const int64 OldSize, const FAssetPackageDocument& NewDoc, const int64 NewOffset, const int64 NewSize)
{
	TArray<FAssetByteDiffSpan> Result;

	if (OldOffset == INDEX_NONE || NewOffset == INDEX_NONE || OldSize < 0 || NewSize < 0 || !OldDoc.IsValidRange(OldOffset, OldSize) || !NewDoc.IsValidRange(NewOffset, NewSize))
	{
		return Result;
	}

	const int64 CommonSize = FMath::Min(OldSize, NewSize);
	int64 SpanStart = INDEX_NONE;

	for (int64 Index = 0; Index < CommonSize; ++Index)
	{
		const uint8 OldByte = OldDoc.FileData[OldOffset + Index];
		const uint8 NewByte = NewDoc.FileData[NewOffset + Index];
		const bool bDifferent = OldByte != NewByte;

		if (bDifferent && SpanStart == INDEX_NONE)
		{
			SpanStart = Index;
		}
		else if (!bDifferent && SpanStart != INDEX_NONE)
		{
			Result.Add({ SpanStart, Index - SpanStart });

			SpanStart = INDEX_NONE;
		}
	}

	if (SpanStart != INDEX_NONE)
	{
		Result.Add({ SpanStart, CommonSize - SpanStart });
	}

	// Anything beyond the common length only exists on one side,
	// so treat that region as changed too.
	if (OldSize != NewSize)
	{
		Result.Add({ CommonSize, FMath::Max(OldSize, NewSize) - CommonSize });
	}

	return Result;
}
