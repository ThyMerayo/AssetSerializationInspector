// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Trace/AssetSerializationTrace.h"

namespace AssetSerializationTrace
{
	const FAssetSerializationTraceNode* FindDeepestTraceNode(const TSharedPtr<FAssetSerializationTraceNode>& Root, const int64 Offset, const int64 Size)
	{
		if (!Root.IsValid() || !Root->Overlaps(Offset, Size))
		{
			return nullptr;
		}

		for (const TSharedPtr<FAssetSerializationTraceNode>& Child : Root->Children)
		{
			if (const FAssetSerializationTraceNode* Found = FindDeepestTraceNode(Child, Offset, Size))
			{
				return Found;
			}
		}

		return Root.Get();
	}
} // namespace AssetSerializationTrace
