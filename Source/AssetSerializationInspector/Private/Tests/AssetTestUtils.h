// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetStructNativeData.h"
#include "Trace/AssetSerializationTrace.h"

namespace AssetTestUtils
{
	/**
	 * Decodes what an export writes after its tagged properties: the last range of its trace that no property accounts for. Decode is
	 * called with the offset of that range in the document, its size and the trace of the export; the result is what Decode returns,
	 * and false for an export without such a range.
	 */
	template <typename TDecode> bool DecodeLastNative(const FAssetPackageTraceCollection& Traces, const FAssetPackageExportEntry& Export, TDecode&& Decode)
	{
		const FAssetSerializationTrace* Trace = Traces.FindExportTrace(Export.Index);
		if (Trace == nullptr || !Trace->Root.IsValid())
		{
			return false;
		}

		const FAssetSerializationTraceNode* Native = nullptr;
		for (const TSharedPtr<FAssetSerializationTraceNode>& Node : Trace->Root->Children)
		{
			if (Node.IsValid() && Node->Kind == EAssetSerializationTraceKind::Native)
			{
				Native = Node.Get();
			}
		}

		return Native != nullptr && Decode(Export.SerialOffset + Native->Offset, Native->Size, Trace);
	}

	/** The change of a native data comparison with this key, or null. */
	inline const FAssetNativeDataChange* FindChange(const TArray<FAssetNativeDataChange>& Changes, const TCHAR* Key)
	{
		return Changes.FindByPredicate([Key](const FAssetNativeDataChange& Change) { return Change.Key == Key; });
	}
} // namespace AssetTestUtils
