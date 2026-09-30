// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Trace/AssetSerializationTraceBuilder.h"

#include "Serialization/Archive.h"

FAssetSerializationTraceBuilder::FAssetSerializationTraceBuilder(FArchive& InArchive, const int64 InPayloadStart) : Archive(InArchive), PayloadStart(InPayloadStart) {}

void FAssetSerializationTraceBuilder::BeginNode(const FString& Name, const FString& TypeName, const EAssetSerializationTraceKind Kind)
{
	TSharedPtr<FAssetSerializationTraceNode> Node = MakeShared<FAssetSerializationTraceNode>();

	Node->Name = Name;
	Node->TypeName = TypeName;
	Node->Kind = Kind;

	Node->Offset = Archive.Tell() - PayloadStart;

	if (!Stack.IsEmpty())
	{
		Node->Parent = Stack.Last();
		Stack.Last()->Children.Add(Node);
	}
	else
	{
		Root = Node;
	}

	Stack.Add(Node);
}

void FAssetSerializationTraceBuilder::EndNode()
{
	if (Stack.IsEmpty())
	{
		return;
	}

	TSharedPtr<FAssetSerializationTraceNode> Node = Stack.Pop();

	const int64 End = Archive.Tell() - PayloadStart;

	Node->Size = FMath::Max<int64>(0, End - Node->Offset);
}