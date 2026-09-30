// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Diff/AssetPackageDiff.h"

#include "Algo/Unique.h"
#include "Misc/SecureHash.h"

#include "Diff/AssetByteDiff.h"
#include "Diff/AssetDecodedValueDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetArchetypeResolver.h"
#include "Serialization/AssetPropertyValueDecoder.h"
#include "Trace/AssetSerializationTrace.h"

namespace
{
	struct FPropertyDiffKey
	{
		FString OldPath;
		FString NewPath;

		bool operator==(const FPropertyDiffKey& Other) const { return OldPath == Other.OldPath && NewPath == Other.NewPath; }
	};
	uint32 GetTypeHash(const FPropertyDiffKey& Key)
	{
		return HashCombine(GetTypeHash(Key.OldPath), GetTypeHash(Key.NewPath));
	}

	FString HashRange(const FAssetPackageDocument& Document, const int64 Offset, const int64 Size)
	{
		if (Size < 0 || !Document.IsValidRange(Offset, Size))
		{
			return TEXT("<invalid>");
		}

		if (Size == 0)
		{
			return TEXT("");
		}

		const uint8* Data = Document.FileData.GetData() + Offset;
		const FSHAHash Hash = FSHA1::HashBuffer(Data, static_cast<uint64>(Size));

		return Hash.ToString();
	}

	FString HashDocument(const FAssetPackageDocument& Document)
	{
		if (Document.FileData.IsEmpty())
		{
			return TEXT("");
		}

		return FSHA1::HashBuffer(Document.FileData.GetData(), static_cast<uint64>(Document.FileData.Num())).ToString();
	}

	template <typename TValue> void AddSummaryValueDiff(FAssetPackageDiffResult& Result, const TCHAR* Key, const FText& DisplayName, const TValue& OldValue, const TValue& NewValue)
	{
		FAssetPackageDiffEntry Entry;

		Entry.Kind = EAssetPackageDiffKind::SummaryField;
		Entry.Key = Key;
		Entry.DisplayName = DisplayName;
		Entry.OldValue = LexToString(OldValue);
		Entry.NewValue = LexToString(NewValue);
		Entry.State = Entry.OldValue == Entry.NewValue ? EAssetPackageDiffState::Unchanged : EAssetPackageDiffState::Modified;

		Result.Entries.Add(MoveTemp(Entry));
	}

	void CompareSummary(const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument, FAssetPackageDiffResult& Result)
	{
		const FPackageFileSummary& A = OldDocument.PackageSummary;
		const FPackageFileSummary& B = NewDocument.PackageSummary;

		AddSummaryValueDiff(Result, TEXT("TotalHeaderSize"), NSLOCTEXT("AssetPackageDiff", "TotalHeaderSize", "Total header size"), A.TotalHeaderSize, B.TotalHeaderSize);
		AddSummaryValueDiff(Result, TEXT("NameCount"), NSLOCTEXT("AssetPackageDiff", "NameCount", "Name count"), A.NameCount, B.NameCount);
		AddSummaryValueDiff(Result, TEXT("NameOffset"), NSLOCTEXT("AssetPackageDiff", "NameOffset", "Name offset"), A.NameOffset, B.NameOffset);
		AddSummaryValueDiff(Result, TEXT("ImportCount"), NSLOCTEXT("AssetPackageDiff", "ImportCount", "Import count"), A.ImportCount, B.ImportCount);
		AddSummaryValueDiff(Result, TEXT("ImportOffset"), NSLOCTEXT("AssetPackageDiff", "ImportOffset", "Import offset"), A.ImportOffset, B.ImportOffset);
		AddSummaryValueDiff(Result, TEXT("ExportCount"), NSLOCTEXT("AssetPackageDiff", "ExportCount", "Export count"), A.ExportCount, B.ExportCount);
		AddSummaryValueDiff(Result, TEXT("ExportOffset"), NSLOCTEXT("AssetPackageDiff", "ExportOffset", "Export offset"), A.ExportOffset, B.ExportOffset);
		AddSummaryValueDiff(Result, TEXT("PackageFlags"), NSLOCTEXT("AssetPackageDiff", "PackageFlags", "Package flags"), A.GetPackageFlags(), B.GetPackageFlags());
	}

	void CompareNames(const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument, FAssetPackageDiffResult& Result)
	{
		const int32 MaximumCount = FMath::Max(OldDocument.NameMap.Num(), NewDocument.NameMap.Num());

		FAssetPackageDiffEntry Root;
		Root.Kind = EAssetPackageDiffKind::Name;
		Root.DisplayName = NSLOCTEXT("AssetPackageDiff", "NameMap", "Name Map");

		for (int32 Index = 0; Index < MaximumCount; ++Index)
		{
			const bool bHasOld = OldDocument.NameMap.IsValidIndex(Index);
			const bool bHasNew = NewDocument.NameMap.IsValidIndex(Index);

			FAssetPackageDiffEntry Entry;
			Entry.Kind = EAssetPackageDiffKind::Name;
			Entry.Key = FString::Printf(TEXT("Name[%d]"), Index);
			Entry.DisplayName = FText::FromString(Entry.Key);

			if (!bHasOld)
			{
				Entry.State = EAssetPackageDiffState::Added;
				Entry.NewValue = NewDocument.NameMap[Index].Name;
				Entry.NewOffset = NewDocument.NameMap[Index].Offset;
				Entry.NewSize = NewDocument.NameMap[Index].Size;
			}
			else if (!bHasNew)
			{
				Entry.State = EAssetPackageDiffState::Removed;
				Entry.OldValue = OldDocument.NameMap[Index].Name;
				Entry.OldOffset = OldDocument.NameMap[Index].Offset;
				Entry.OldSize = OldDocument.NameMap[Index].Size;
			}
			else
			{
				const FAssetPackageNameEntry& A = OldDocument.NameMap[Index];
				const FAssetPackageNameEntry& B = NewDocument.NameMap[Index];

				Entry.OldValue = A.Name;
				Entry.NewValue = B.Name;
				Entry.OldOffset = A.Offset;
				Entry.NewOffset = B.Offset;
				Entry.OldSize = A.Size;
				Entry.NewSize = B.Size;

				if (A.Name != B.Name || A.NonCasePreservingHash != B.NonCasePreservingHash || A.CasePreservingHash != B.CasePreservingHash)
				{
					Entry.State = EAssetPackageDiffState::Modified;
				}
				else if (A.Offset != B.Offset)
				{
					Entry.State = EAssetPackageDiffState::Moved;
				}
				else
				{
					Entry.State = EAssetPackageDiffState::Unchanged;
				}
			}

			Root.Children.Add(MoveTemp(Entry));
		}

		Result.Entries.Add(MoveTemp(Root));
	}

	TMap<FString, const FAssetPackageImportEntry*> BuildImportPathMap(const FAssetPackageDocument& Document)
	{
		TMap<FString, const FAssetPackageImportEntry*> Result;

		for (const FAssetPackageImportEntry& Import : Document.ImportMap)
		{
			Result.Add(Document.ResolveImportPath(Import.Index), &Import);
		}

		return Result;
	}

	void CompareImports(const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument, FAssetPackageDiffResult& Result)
	{
		const auto OldImports = BuildImportPathMap(OldDocument);
		const auto NewImports = BuildImportPathMap(NewDocument);

		TSet<FString> Keys;
		for (const auto& Pair : OldImports)
		{
			Keys.Add(Pair.Key);
		}
		for (const auto& Pair : NewImports)
		{
			Keys.Add(Pair.Key);
		}

		FAssetPackageDiffEntry Root;
		Root.Kind = EAssetPackageDiffKind::Import;
		Root.DisplayName = NSLOCTEXT("AssetPackageDiff", "ImportMap", "Import Map");

		for (const FString& Key : Keys)
		{
			const FAssetPackageImportEntry* const* OldFound = OldImports.Find(Key);
			const FAssetPackageImportEntry* const* NewFound = NewImports.Find(Key);

			FAssetPackageDiffEntry Entry;
			Entry.Kind = EAssetPackageDiffKind::Import;
			Entry.Key = Key;
			Entry.DisplayName = FText::FromString(Key);

			if (!OldFound)
			{
				Entry.State = EAssetPackageDiffState::Added;
			}
			else if (!NewFound)
			{
				Entry.State = EAssetPackageDiffState::Removed;
			}
			else
			{
				const FAssetPackageImportEntry& A = **OldFound;
				const FAssetPackageImportEntry& B = **NewFound;

				Entry.OldOffset = A.Offset;
				Entry.NewOffset = B.Offset;
				Entry.OldSize = A.Size;
				Entry.NewSize = B.Size;

				if (A.Size != B.Size)
				{
					Entry.State = EAssetPackageDiffState::Modified;
				}
				else if (A.Offset != B.Offset)
				{
					Entry.State = EAssetPackageDiffState::Moved;
				}
				else
				{
					Entry.State = EAssetPackageDiffState::Unchanged;
				}
			}

			Root.Children.Add(MoveTemp(Entry));
		}

		Result.Entries.Add(MoveTemp(Root));
	}

	TMap<FString, const FAssetPackageExportEntry*> BuildExportPathMap(const FAssetPackageDocument& Document)
	{
		TMap<FString, const FAssetPackageExportEntry*> Result;

		for (const FAssetPackageExportEntry& Export : Document.ExportMap)
		{
			Result.Add(Document.ResolveExportPath(Export.Index), &Export);
		}

		return Result;
	}

	const FAssetSerializationTrace* FindExportTrace(const FAssetPackageTraceCollection* Traces, const int32 ExportIndex)
	{
		if (Traces == nullptr || ExportIndex == INDEX_NONE)
		{
			return nullptr;
		}

		return Traces->FindExportTrace(ExportIndex);
	}

	struct FAssetSerializedPropertyIdentity
	{
		FString Path;
		FString TypeName;
		int32 ArrayIndex = 0;

		bool operator==(const FAssetSerializedPropertyIdentity& Other) const { return Path == Other.Path && TypeName == Other.TypeName && ArrayIndex == Other.ArrayIndex; }
	};

	uint32 GetTypeHash(const FAssetSerializedPropertyIdentity& Identity)
	{
		uint32 Hash = GetTypeHash(Identity.Path);
		Hash = HashCombine(Hash, GetTypeHash(Identity.TypeName));
		Hash = HashCombine(Hash, static_cast<uint32>(Identity.ArrayIndex));

		return Hash;
	}

	FAssetSerializedPropertyIdentity MakePropertyIdentity(const FAssetSerializationTraceNode& Node)
	{
		FAssetSerializedPropertyIdentity Result;
		Result.Path = AssetPackageDiff::BuildTracePath(&Node);
		Result.TypeName = Node.TypeName;
		Result.ArrayIndex = Node.ArrayIndex;

		return Result;
	}

	using FPropertyNodeMap = TMap<FAssetSerializedPropertyIdentity, const FAssetSerializationTraceNode*>;

	FPropertyNodeMap BuildPropertyNodeMap(const FAssetSerializationTrace* Trace)
	{
		FPropertyNodeMap Result;

		if (Trace == nullptr || !Trace->Root.IsValid())
		{
			return Result;
		}

		TFunction<void(const TSharedPtr<FAssetSerializationTraceNode>&)> Visit;
		Visit = [&](const TSharedPtr<FAssetSerializationTraceNode>& Node) {
			if (!Node.IsValid())
			{
				return;
			}

			if (Node->Kind == EAssetSerializationTraceKind::Property)
			{
				Result.Add(MakePropertyIdentity(*Node), Node.Get());
			}

			for (const auto& Child : Node->Children)
			{
				Visit(Child);
			}
		};

		Visit(Trace->Root);

		return Result;
	}

	struct FPropertyDiffNodeData
	{
		const FAssetPackageDocument& Document;
		const FAssetPackageExportEntry& Export;
		const FAssetSerializationTraceNode* Node;
		FAssetArchetypeResolver* ArchetypeResolver = nullptr;
	};

	FString FormatFinalValue(const FAssetDecodedPropertyValue& Final)
	{
		constexpr int32 MaximumShownElements = 8;

		TArray<FString> Parts;
		for (const FAssetDecodedPropertyValue& Child : Final.Children)
		{
			if (Parts.Num() == MaximumShownElements)
			{
				Parts.Add(TEXT("..."));
				break;
			}

			Parts.Add(Child.Kind == EAssetDecodedValueKind::MapEntry && Child.Children.Num() == 2 ? FString::Printf(TEXT("%s=%s"), *Child.Children[0].Value, *Child.Children[1].Value) : Child.Value);
		}

		return FString::Printf(TEXT("%s: %s"), *Final.Value, *FString::Join(Parts, TEXT(", ")));
	}

	/** Reconstructs the final contents of a set or map that may have been stored as a delta against its archetype. */
	void ResolveFinalValue(const FPropertyDiffNodeData& Data, const FAssetDecodedPropertyValue& Decoded, FString& OutFinalValue, FString& OutNote)
	{
		if (Data.ArchetypeResolver == nullptr || Data.Node == nullptr || (Decoded.Kind != EAssetDecodedValueKind::Set && Decoded.Kind != EAssetDecodedValueKind::Map))
		{
			return;
		}

		FAssetArchetypeValue Final;
		FString Message;
		if (!Data.ArchetypeResolver->ResolveFinalContainerValue(Data.Export.Index, Data.Node->Name, Data.Node->ArrayIndex, Decoded, Final, Message))
		{
			return;
		}

		OutFinalValue = FormatFinalValue(Final.Value);
		OutNote = Final.Note;
	}

	bool ArePropertyBytesIdentical(const FPropertyDiffNodeData& OldData, const FPropertyDiffNodeData& NewData)
	{
		if (OldData.Node->Size != NewData.Node->Size)
		{
			return false;
		}

		if (OldData.Node->Size == 0)
		{
			// Important for inline BoolProperty.
			return OldData.Node->bHasInlineBoolValue == NewData.Node->bHasInlineBoolValue && OldData.Node->bInlineBoolValue == NewData.Node->bInlineBoolValue;
		}

		const int64 OldAbsoluteOffset = OldData.Export.SerialOffset + OldData.Node->Offset;
		const int64 NewAbsoluteOffset = NewData.Export.SerialOffset + NewData.Node->Offset;

		if (!OldData.Document.IsValidRange(OldAbsoluteOffset, OldData.Node->Size) || !NewData.Document.IsValidRange(NewAbsoluteOffset, NewData.Node->Size))
		{
			return false;
		}

		return FMemory::Memcmp(OldData.Document.FileData.GetData() + OldAbsoluteOffset, NewData.Document.FileData.GetData() + NewAbsoluteOffset, OldData.Node->Size) == 0;
	}

	void AppendDecodedValueDiffChildren(const FAssetDecodedValueDiff& ValueDiff, FAssetPackageDiffEntry& Parent)
	{
		for (const FAssetDecodedValueDiff& Child : ValueDiff.Children)
		{
			if (Child.State == EAssetDecodedValueDiffState::Unchanged)
			{
				continue;
			}

			FAssetPackageDiffEntry Entry;
			Entry.Kind = EAssetPackageDiffKind::Property;
			Entry.Key = Child.Name;
			Entry.SemanticPath = AssetPackageDiff::AppendSemanticPath(Parent.SemanticPath, Child.Name);
			Entry.DisplayName = FText::FromString(Child.Name);
			Entry.TypeName = Child.TypeName;

			switch (Child.State)
			{
				case EAssetDecodedValueDiffState::Added:
					Entry.State = EAssetPackageDiffState::Added;
					break;

				case EAssetDecodedValueDiffState::Removed:
					Entry.State = EAssetPackageDiffState::Removed;
					break;

				case EAssetDecodedValueDiffState::Modified:
					Entry.State = EAssetPackageDiffState::Modified;
					break;

				case EAssetDecodedValueDiffState::Unchanged:
					Entry.State = EAssetPackageDiffState::Unchanged;
					break;
			}

			if (Child.bHasOldValue)
			{
				Entry.bHasOldDecodedValue = true;
				Entry.OldDecodedValue = Child.OldValue;
				Entry.OldValue = Child.OldValue;
			}

			if (Child.bHasNewValue)
			{
				Entry.bHasNewDecodedValue = true;
				Entry.NewDecodedValue = Child.NewValue;
				Entry.NewValue = Child.NewValue;
			}

			AppendDecodedValueDiffChildren(Child, Entry);

			Parent.Children.Add(MoveTemp(Entry));
		}
	}

	void BuildOnePropertyDiff(const FPropertyDiffNodeData& OldData, const FPropertyDiffNodeData& NewData, const FAssetSerializedPropertyIdentity& Identity, FAssetPackageDiffEntry& PayloadEntry)
	{
		FAssetPackageDiffEntry Entry;

		Entry.Kind = EAssetPackageDiffKind::Property;
		Entry.Key = Identity.Path;
		Entry.DisplayName = FText::FromString(Identity.Path);
		Entry.TypeName = Identity.TypeName;

		if (OldData.Node == nullptr && NewData.Node != nullptr)
		{
			Entry.State = EAssetPackageDiffState::Modified;
			Entry.OldPresence = EAssetSerializedPropertyPresence::NotSerialized;
			Entry.NewPresence = EAssetSerializedPropertyPresence::Present;
			Entry.bHasOldDecodedValue = true;
			Entry.OldDecodedValue = TEXT("<not serialized; likely default>");

			const FAssetDecodedPropertyValue NewDecoded = FAssetPropertyValueDecoder::Decode(NewData.Document, *NewData.Node, NewData.Export.SerialOffset);
			if (NewDecoded.IsSuccess())
			{
				Entry.bHasOldDecodedValue = true;
				Entry.NewDecodedValue = NewDecoded.Value;
				Entry.NewValue = NewDecoded.Value;
			}

			Entry.SemanticPath = AssetPackageDiff::AppendSemanticPath(PayloadEntry.SemanticPath, Entry.Key);
			PayloadEntry.Children.Add(MoveTemp(Entry));

			return;
		}

		if (OldData.Node != nullptr && NewData.Node == nullptr)
		{
			Entry.State = EAssetPackageDiffState::Modified;
			Entry.OldPresence = EAssetSerializedPropertyPresence::Present;
			Entry.NewPresence = EAssetSerializedPropertyPresence::NotSerialized;
			Entry.bHasNewDecodedValue = true;
			Entry.NewDecodedValue = TEXT("<not serialized; likely default>");

			const FAssetDecodedPropertyValue OldDecoded = FAssetPropertyValueDecoder::Decode(OldData.Document, *OldData.Node, OldData.Export.SerialOffset);
			if (OldDecoded.IsSuccess())
			{
				Entry.bHasOldDecodedValue = true;
				Entry.OldDecodedValue = OldDecoded.Value;
				Entry.OldValue = OldDecoded.Value;
			}

			Entry.SemanticPath = AssetPackageDiff::AppendSemanticPath(PayloadEntry.SemanticPath, Entry.Key);
			PayloadEntry.Children.Add(MoveTemp(Entry));

			return;
		}

		if (OldData.Node == nullptr || NewData.Node == nullptr)
		{
			return;
		}

		if (ArePropertyBytesIdentical(OldData, NewData))
		{
			return;
		}

		Entry.State = EAssetPackageDiffState::Modified;
		Entry.OldOffset = OldData.Export.SerialOffset + OldData.Node->Offset;
		Entry.NewOffset = NewData.Export.SerialOffset + NewData.Node->Offset;
		Entry.OldSize = OldData.Node->Size;
		Entry.NewSize = NewData.Node->Size;
		Entry.SemanticPath = AssetPackageDiff::AppendSemanticPath(PayloadEntry.SemanticPath, Entry.Key);

		const FAssetDecodedPropertyValue OldDecoded = FAssetPropertyValueDecoder::Decode(OldData.Document, *OldData.Node, OldData.Export.SerialOffset);
		if (OldDecoded.IsSuccess())
		{
			Entry.bHasOldDecodedValue = true;
			Entry.OldDecodedValue = OldDecoded.Value;
			Entry.OldValue = OldDecoded.Value;
			ResolveFinalValue(OldData, OldDecoded, Entry.OldFinalValue, Entry.OldFinalValueNote);
		}
		const FAssetDecodedPropertyValue NewDecoded = FAssetPropertyValueDecoder::Decode(NewData.Document, *NewData.Node, NewData.Export.SerialOffset);
		if (NewDecoded.IsSuccess())
		{
			Entry.bHasNewDecodedValue = true;
			Entry.NewDecodedValue = NewDecoded.Value;
			Entry.NewValue = NewDecoded.Value;
			ResolveFinalValue(NewData, NewDecoded, Entry.NewFinalValue, Entry.NewFinalValueNote);
		}

		const FAssetDecodedPropertyValue* OldPtr = OldDecoded.IsSuccess() ? &OldDecoded : nullptr;
		const FAssetDecodedPropertyValue* NewPtr = NewDecoded.IsSuccess() ? &NewDecoded : nullptr;
		if (OldPtr != nullptr || NewPtr != nullptr)
		{
			const FAssetDecodedValueDiff ValueDiff = FAssetDecodedValueDiffer::Compare(OldPtr, NewPtr);
			AppendDecodedValueDiffChildren(ValueDiff, Entry);
		}

		PayloadEntry.Children.Add(MoveTemp(Entry));
	}

	void BuildSemanticPropertyDiffs(const FPropertyDiffData& OldData, const FPropertyDiffData& NewData, FAssetPackageDiffEntry& PayloadEntry)
	{
		const FPropertyNodeMap OldProperties = BuildPropertyNodeMap(OldData.Trace);
		const FPropertyNodeMap NewProperties = BuildPropertyNodeMap(NewData.Trace);

		TSet<FAssetSerializedPropertyIdentity> Keys;

		for (const auto& Pair : OldProperties)
		{
			Keys.Add(Pair.Key);
		}

		for (const auto& Pair : NewProperties)
		{
			Keys.Add(Pair.Key);
		}

		for (const FAssetSerializedPropertyIdentity& Key : Keys)
		{
			const FAssetSerializationTraceNode* const* OldFound = OldProperties.Find(Key);
			const FAssetSerializationTraceNode* const* NewFound = NewProperties.Find(Key);
			const FAssetSerializationTraceNode* OldNode = OldFound != nullptr ? *OldFound : nullptr;
			const FAssetSerializationTraceNode* NewNode = NewFound != nullptr ? *NewFound : nullptr;

			BuildOnePropertyDiff({ OldData.Document, OldData.Export, OldNode, OldData.ArchetypeResolver }, { NewData.Document, NewData.Export, NewNode, NewData.ArchetypeResolver }, Key, PayloadEntry);
		}
	}

	void CompareExports(const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument, const FAssetPackageTraceCollection* OldTraces,
		const FAssetPackageTraceCollection* NewTraces, FAssetPackageDiffResult& Result)
	{
		const auto OldExports = BuildExportPathMap(OldDocument);
		const auto NewExports = BuildExportPathMap(NewDocument);

		// Resolvers cache the archetype packages they load, so they live for the whole comparison.
		const TUniquePtr<FAssetArchetypeResolver> OldResolver = OldTraces != nullptr ? MakeUnique<FAssetArchetypeResolver>(OldDocument, *OldTraces) : nullptr;
		const TUniquePtr<FAssetArchetypeResolver> NewResolver = NewTraces != nullptr ? MakeUnique<FAssetArchetypeResolver>(NewDocument, *NewTraces) : nullptr;

		TSet<FString> Keys;
		for (const auto& Pair : OldExports)
		{
			Keys.Add(Pair.Key);
		}
		for (const auto& Pair : NewExports)
		{
			Keys.Add(Pair.Key);
		}

		FAssetPackageDiffEntry Root;
		Root.Kind = EAssetPackageDiffKind::Export;
		Root.DisplayName = NSLOCTEXT("AssetPackageDiff", "ExportMap", "Export Map");

		for (const FString& Key : Keys)
		{
			const FAssetPackageExportEntry* const* OldFound = OldExports.Find(Key);
			const FAssetPackageExportEntry* const* NewFound = NewExports.Find(Key);

			FAssetPackageDiffEntry Entry;
			Entry.Kind = EAssetPackageDiffKind::Export;
			Entry.Key = Key;
			Entry.DisplayName = FText::FromString(Key);
			Entry.SemanticPath = Entry.Key; // FString::Printf(TEXT("Export:%s"), *Entry.Key);

			if (!OldFound)
			{
				Entry.State = EAssetPackageDiffState::Added;
			}
			else if (!NewFound)
			{
				Entry.State = EAssetPackageDiffState::Removed;
			}
			else
			{
				const FAssetPackageExportEntry& A = **OldFound;
				const FAssetPackageExportEntry& B = **NewFound;

				Entry.OldExportIndex = A.Index;
				Entry.NewExportIndex = B.Index;

				const FString OldPayloadHash = HashRange(OldDocument, A.SerialOffset, A.SerialSize);
				const FString NewPayloadHash = HashRange(NewDocument, B.SerialOffset, B.SerialSize);
				const bool bPayloadIdentical = A.SerialSize == B.SerialSize && OldPayloadHash == NewPayloadHash;
				const bool bPayloadMoved = A.SerialOffset != B.SerialOffset;
				if (!bPayloadIdentical)
				{
					Entry.State = EAssetPackageDiffState::Modified;
				}
				else if (bPayloadMoved)
				{
					Entry.State = EAssetPackageDiffState::Moved;
				}
				else
				{
					Entry.State = EAssetPackageDiffState::Unchanged;
				}

				FAssetPackageDiffEntry Payload;
				Payload.Kind = EAssetPackageDiffKind::ExportPayload;
				Payload.DisplayName = NSLOCTEXT("AssetPackageDiff", "Payload", "Serialized Payload");
				Payload.OldOffset = A.SerialOffset;
				Payload.NewOffset = B.SerialOffset;
				Payload.OldSize = A.SerialSize;
				Payload.NewSize = B.SerialSize;
				Payload.OldValue = OldPayloadHash;
				Payload.NewValue = NewPayloadHash;
				Payload.OldExportIndex = A.Index;
				Payload.NewExportIndex = B.Index;
				Payload.SemanticPath = Entry.SemanticPath;

				if (OldPayloadHash != NewPayloadHash || A.SerialSize != B.SerialSize)
				{
					Payload.State = EAssetPackageDiffState::Modified;
					Payload.ChangedSpans = FAssetByteDiff::Compare(OldDocument, A.SerialOffset, A.SerialSize, NewDocument, B.SerialOffset, B.SerialSize);
					for (const FAssetByteDiffSpan& Span : Payload.ChangedSpans)
					{
						Payload.ChangedByteCount += Span.Size;
					}

					const FAssetSerializationTrace* OldTrace = FindExportTrace(OldTraces, A.Index);
					const FAssetSerializationTrace* NewTrace = FindExportTrace(NewTraces, B.Index);
					BuildSemanticPropertyDiffs({ OldDocument, A, OldTrace, OldResolver.Get() }, { NewDocument, B, NewTrace, NewResolver.Get() }, Payload);
				}
				else if (A.SerialOffset != B.SerialOffset)
				{
					Payload.State = EAssetPackageDiffState::Moved;
				}
				else
				{
					Payload.State = EAssetPackageDiffState::Unchanged;
				}

				Entry.Children.Add(MoveTemp(Payload));
			}

			Root.Children.Add(MoveTemp(Entry));
		}

		Result.Entries.Add(MoveTemp(Root));
	}

	void AddRelevantTraceBoundaries(const TSharedPtr<FAssetSerializationTraceNode>& Root, const int64 SpanOffset, const int64 SpanSize, TArray<int64>& InOutBoundaries)
	{
		if (!Root.IsValid())
		{
			return;
		}

		TArray<const FAssetSerializationTraceNode*> Nodes;
		AssetSerializationTrace::FindDeepestOverlappingFieldNodes(Root, SpanOffset, SpanSize, Nodes);

		const int64 SpanEnd = SpanOffset + SpanSize;

		for (const FAssetSerializationTraceNode* Node : Nodes)
		{
			const int64 Start = FMath::Max(Node->Offset, SpanOffset);
			const int64 End = FMath::Min(Node->Offset + Node->Size, SpanEnd);

			if (Start < End)
			{
				InOutBoundaries.Add(Start);
				InOutBoundaries.Add(End);
			}
		}
	}

} // namespace

namespace AssetPackageDiff
{
	FAssetPackageDiffResult Compare(const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument, const FAssetPackageTraceCollection* OldTraces /*= nullptr*/,
		const FAssetPackageTraceCollection* NewTraces /*= nullptr*/)
	{
		FAssetPackageDiffResult Result;

		Result.OldFilename = OldDocument.Filename;
		Result.NewFilename = NewDocument.Filename;
		Result.OldFileHash = HashDocument(OldDocument);
		Result.NewFileHash = HashDocument(NewDocument);
		Result.bFilesIdentical = OldDocument.GetFileSize() == NewDocument.GetFileSize() && Result.OldFileHash == Result.NewFileHash;

		if (Result.bFilesIdentical)
		{
			return Result;
		}

		CompareSummary(OldDocument, NewDocument, Result);
		CompareNames(OldDocument, NewDocument, Result);
		CompareImports(OldDocument, NewDocument, Result);
		CompareExports(OldDocument, NewDocument, OldTraces, NewTraces, Result);

		return Result;
	}

	FString BuildTracePath(const FAssetSerializationTraceNode* Node)
	{
		if (Node == nullptr)
		{
			return FString();
		}

		TArray<FString> Parts;

		const FAssetSerializationTraceNode* Current = Node;

		while (Current != nullptr && Current->Kind != EAssetSerializationTraceKind::Object)
		{
			if (!Current->Name.IsEmpty())
			{
				Parts.Add(Current->Name);
			}

			const TSharedPtr<FAssetSerializationTraceNode> Parent = Current->Parent.Pin();
			Current = Parent.Get();
		}

		Algo::Reverse(Parts);

		return FString::Join(Parts, TEXT("."));
	}

	FString AppendSemanticPath(const FString& ParentPath, const FString& Segment)
	{
		if (ParentPath.IsEmpty())
		{
			return Segment;
		}

		return FString::Printf(TEXT("%s/%s"), *ParentPath, *Segment);
	}
} // namespace AssetPackageDiff
