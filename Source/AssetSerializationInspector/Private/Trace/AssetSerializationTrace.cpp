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

	const FAssetSerializationTraceNode* FindDeepestFieldTraceNode(const TSharedPtr<FAssetSerializationTraceNode>& Root, const int64 Offset, const int64 Size)
	{
		if (!Root.IsValid())
		{
			return nullptr;
		}

		for (const TSharedPtr<FAssetSerializationTraceNode>& Child : Root->Children)
		{
			if (!Child.IsValid() || !Child->Overlaps(Offset, Size))
			{
				continue;
			}

			const FAssetSerializationTraceNode* Deeper = FindDeepestFieldTraceNode(Child, Offset, Size);

			return Deeper != nullptr ? Deeper : Child.Get();
		}

		return nullptr;
	}

	bool IntersectRanges(const int64 AOffset, const int64 ASize, const int64 BOffset, const int64 BSize, int64& OutOffset, int64& OutSize)
	{
		if (ASize <= 0 || BSize <= 0)
		{
			return false;
		}

		const int64 Start = FMath::Max(AOffset, BOffset);

		const int64 End = FMath::Min(AOffset + ASize, BOffset + BSize);

		if (End <= Start)
		{
			return false;
		}

		OutOffset = Start;
		OutSize = End - Start;

		return true;
	}

	void FindDeepestOverlappingFieldNodes(const TSharedPtr<FAssetSerializationTraceNode>& Node, const int64 Offset, const int64 Size, TArray<const FAssetSerializationTraceNode*>& OutNodes)
	{
		if (!Node.IsValid() || !Node->Overlaps(Offset, Size))
		{
			return;
		}

		bool bFoundChild = false;

		for (const TSharedPtr<FAssetSerializationTraceNode>& Child : Node->Children)
		{
			if (!Child.IsValid() || !Child->Overlaps(Offset, Size))
			{
				continue;
			}

			bFoundChild = true;

			FindDeepestOverlappingFieldNodes(Child, Offset, Size, OutNodes);
		}

		if (!bFoundChild && Node->Kind != EAssetSerializationTraceKind::Object)
		{
			OutNodes.Add(Node.Get());
		}
	}
} // namespace AssetSerializationTrace
