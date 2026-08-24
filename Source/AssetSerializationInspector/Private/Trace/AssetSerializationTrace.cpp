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

	void FindOverlappingTraceNodes(const TSharedPtr<FAssetSerializationTraceNode>& Root, int64 Offset, int64 Size, TArray<const FAssetSerializationTraceNode*>& OutNodes)
	{
		if (!Root.IsValid() || !Root->Overlaps(Offset, Size))
		{
			return;
		}

		for (const TSharedPtr<FAssetSerializationTraceNode>& Child : Root->Children)
		{
			if (Child.IsValid() && Child->Overlaps(Offset, Size))
			{
				OutNodes.Add(Child.Get());
			}
		}
	}

	FAssetSerializationTrace BuildSerializationTrace(const UObject* Object, const int64 PayloadSize, const TArray<FAssetSerializationTraceEvent>& Events)
	{
		FAssetSerializationTrace Result;

		Result.ObjectPath = Object ? Object->GetPathName() : TEXT("<unknown>");
		Result.PayloadOffset = 0;
		Result.PayloadSize = PayloadSize;
		Result.Root = MakeShared<FAssetSerializationTraceNode>();
		Result.Root->Kind = EAssetSerializationTraceKind::Object;
		Result.Root->Name = Result.ObjectPath;
		Result.Root->TypeName = Object ? Object->GetClass()->GetName() : TEXT("UObject");
		Result.Root->Offset = 0;
		Result.Root->Size = PayloadSize;

		for (const FAssetSerializationTraceEvent& Event : Events)
		{
			TSharedPtr<FAssetSerializationTraceNode> Node = MakeShared<FAssetSerializationTraceNode>();
			Node->Kind = Event.bHasPropertyContext ? EAssetSerializationTraceKind::Property : EAssetSerializationTraceKind::Native;
			Node->Name = Event.PropertyPath;
			Node->TypeName = Event.PropertyType;
			Node->Offset = Event.Offset;
			Node->Size = Event.Size;
			Node->Parent = Result.Root;
			Result.Root->Children.Add(Node);
		}

		return Result;
	}
} // namespace AssetSerializationTrace
