// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Diff/AssetPackageDiff.h"

#include "Algo/Unique.h"
#include "Misc/SecureHash.h"

#include "Diff/AssetByteDiff.h"
#include "Diff/AssetCaseSensitiveKeys.h"
#include "Diff/AssetDecodedValueDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackagePayloadReader.h"
#include "Serialization/AssetArchetypeResolver.h"
#include "Serialization/AssetBulkDataExport.h"
#include "Serialization/AssetContainerFinalValue.h"
#include "Serialization/AssetGraphNodePins.h"
#include "Serialization/AssetPropertyValueDecoder.h"
#include "Serialization/AssetSchemaReflection.h"
#include "Serialization/AssetStructNativeData.h"
#include "Summary/AssetExportSummary.h"
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

	/**
	 * The name maps are compared by what the names are, not by where they are: a name that is new shifts every name after it by one
	 * place, and comparing place by place would report all of those as changed (a variable that was not touched would show up as a
	 * different name). A name that is in both maps is unchanged, or moved when its place changed; a name only in the new map was
	 * added, and one only in the old map was removed.
	 */
	void CompareNames(const FAssetPackageDocument& OldDocument, const FAssetPackageDocument& NewDocument, FAssetPackageDiffResult& Result)
	{
		FAssetPackageDiffEntry Root;
		Root.Kind = EAssetPackageDiffKind::Name;
		Root.DisplayName = NSLOCTEXT("AssetPackageDiff", "NameMap", "Name Map");

		TMap<FString, int32, FDefaultSetAllocator, TCaseSensitiveStringMapKeyFuncs<int32>> OldIndexByName;
		for (int32 Index = 0; Index < OldDocument.NameMap.Num(); ++Index)
		{
			OldIndexByName.FindOrAdd(OldDocument.NameMap[Index].Name, Index);
		}

		TSet<FString, FCaseSensitiveStringSetKeyFuncs> NewNames;
		for (int32 Index = 0; Index < NewDocument.NameMap.Num(); ++Index)
		{
			const FAssetPackageNameEntry& B = NewDocument.NameMap[Index];
			NewNames.Add(B.Name);

			FAssetPackageDiffEntry Entry;
			Entry.Kind = EAssetPackageDiffKind::Name;
			Entry.Key = FString::Printf(TEXT("Name[%d]"), Index);
			Entry.DisplayName = FText::FromString(Entry.Key);
			Entry.NewValue = B.Name;
			Entry.NewOffset = B.Offset;
			Entry.NewSize = B.Size;

			const int32* OldIndex = OldIndexByName.Find(B.Name);
			if (OldIndex == nullptr)
			{
				Entry.State = EAssetPackageDiffState::Added;
			}
			else
			{
				const FAssetPackageNameEntry& A = OldDocument.NameMap[*OldIndex];
				Entry.OldValue = A.Name;
				Entry.OldOffset = A.Offset;
				Entry.OldSize = A.Size;

				if (A.NonCasePreservingHash != B.NonCasePreservingHash || A.CasePreservingHash != B.CasePreservingHash)
				{
					Entry.State = EAssetPackageDiffState::Modified;
				}
				else if (*OldIndex != Index)
				{
					Entry.State = EAssetPackageDiffState::Moved;
					Entry.Explanation =
						FText::Format(NSLOCTEXT("AssetPackageDiff", "NameMoved", "The name is the same; it moved from Name[{0}] to Name[{1}] because names before it were added or removed."),
							FText::AsNumber(*OldIndex), FText::AsNumber(Index));
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

		for (int32 Index = 0; Index < OldDocument.NameMap.Num(); ++Index)
		{
			const FAssetPackageNameEntry& A = OldDocument.NameMap[Index];
			if (NewNames.Contains(A.Name))
			{
				continue;
			}

			FAssetPackageDiffEntry Entry;
			Entry.Kind = EAssetPackageDiffKind::Name;
			Entry.Key = FString::Printf(TEXT("Name[%d] (old)"), Index);
			Entry.DisplayName = FText::FromString(Entry.Key);
			Entry.State = EAssetPackageDiffState::Removed;
			Entry.OldValue = A.Name;
			Entry.OldOffset = A.Offset;
			Entry.OldSize = A.Size;
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

	/** The struct name of "StructProperty(Name)", or empty. */
	FString StructNameOfType(const FString& TypeName)
	{
		int32 Open = INDEX_NONE;
		int32 Close = INDEX_NONE;
		if (!TypeName.StartsWith(TEXT("StructProperty(")) || !TypeName.FindChar(TEXT('('), Open) || !TypeName.FindLastChar(TEXT(')'), Close) || Close <= Open + 1)
		{
			return FString();
		}

		return TypeName.Mid(Open + 1, Close - Open - 1);
	}

	/**
	 * Replaces, inside a copy of a struct value, every set or map reached through struct fields with its final contents.
	 * FieldPath names the fields from the top-level property down to Value. Returns whether anything was replaced.
	 */
	bool ReplaceNestedContainersWithFinalValues(const FPropertyDiffNodeData& Data, TArray<FString>& FieldPath, FAssetDecodedPropertyValue& Value, FString& InOutNote)
	{
		bool bReplaced = false;

		for (FAssetDecodedPropertyValue& Child : Value.Children)
		{
			if (Child.Kind == EAssetDecodedValueKind::Struct)
			{
				FieldPath.Add(Child.Name);
				bReplaced |= ReplaceNestedContainersWithFinalValues(Data, FieldPath, Child, InOutNote);
				FieldPath.Pop();
			}
			else if (Child.Kind == EAssetDecodedValueKind::Set || Child.Kind == EAssetDecodedValueKind::Map)
			{
				FieldPath.Add(Child.Name);

				FAssetArchetypeValue Final;
				FString Message;
				if (Data.ArchetypeResolver->ResolveFinalNestedContainerValue(Data.Export.Index, Data.Node->Name, Data.Node->ArrayIndex, FieldPath, Child, Final, Message))
				{
					const FString FinalName = Child.Name;
					Child = MoveTemp(Final.Value);
					Child.Name = FinalName;
					bReplaced = true;

					if (!Final.Note.IsEmpty() && !InOutNote.Contains(Final.Note))
					{
						InOutNote += (InOutNote.IsEmpty() ? TEXT("") : TEXT(" ")) + Final.Note;
					}
				}

				FieldPath.Pop();
			}
			else if (Child.Kind == EAssetDecodedValueKind::Array)
			{
				bReplaced |= AssetContainerFinalValue::ResolveContainersInArrayElements(Child, InOutNote);
			}
		}

		return bReplaced;
	}

	/**
	 * Reconstructs the final contents of a set or map that may have been stored as a delta against its archetype, and of
	 * the sets and maps inside a struct property.
	 */
	void ResolveFinalValue(const FPropertyDiffNodeData& Data, const FAssetDecodedPropertyValue& Decoded, FString& OutFinalValue, FString& OutNote)
	{
		if (Data.Node == nullptr)
		{
			return;
		}

		if (Decoded.Kind == EAssetDecodedValueKind::Array)
		{
			FAssetDecodedPropertyValue Copy = Decoded;
			FString Note;
			if (AssetContainerFinalValue::ResolveContainersInArrayElements(Copy, Note))
			{
				OutFinalValue = FAssetPropertyValueDecoder::FormatForDisplay(Copy);
				OutNote = Note;
			}

			return;
		}

		if (Data.ArchetypeResolver == nullptr)
		{
			return;
		}

		if (Decoded.Kind == EAssetDecodedValueKind::Struct)
		{
			FAssetDecodedPropertyValue Copy = Decoded;
			TArray<FString> FieldPath;
			FString Note;
			if (ReplaceNestedContainersWithFinalValues(Data, FieldPath, Copy, Note))
			{
				OutFinalValue = FAssetPropertyValueDecoder::FormatForDisplay(Copy);
				OutNote = Note;
			}

			return;
		}

		if (Decoded.Kind != EAssetDecodedValueKind::Set && Decoded.Kind != EAssetDecodedValueKind::Map)
		{
			return;
		}

		FAssetArchetypeValue Final;
		FString Message;
		if (!Data.ArchetypeResolver->ResolveFinalContainerValue(Data.Export.Index, Data.Node->Name, Data.Node->ArrayIndex, Decoded, Final, Message))
		{
			return;
		}

		OutFinalValue = FAssetPropertyValueDecoder::FormatForDisplay(Final.Value);
		OutNote = Final.Note;
	}

	/**
	 * Describes a property that one side of the comparison does not serialize, using the value its archetype chain provides.
	 * Unreal omits properties that equal their defaults, so this is the value the asset actually has.
	 */
	void DescribeOmittedValue(const FPropertyDiffNodeData& OmittedData, const FAssetSerializationTraceNode& PresentNode, const FAssetDecodedPropertyValue* PresentDecoded, FString& OutDecodedValue,
		FString& OutFinalValue, FString& OutNote)
	{
		OutDecodedValue = TEXT("<not serialized; likely default>");

		if (OmittedData.ArchetypeResolver == nullptr)
		{
			return;
		}

		const FAssetOmittedPropertyDefault Default = OmittedData.ArchetypeResolver->DescribeOmittedProperty(OmittedData.Export.Index, PresentNode.Name, PresentNode.ArrayIndex);
		OutNote = Default.Note;

		if (Default.Status == EAssetArchetypeValueStatus::DeclaredByBlueprint)
		{
			OutDecodedValue = TEXT("<not serialized>");
			OutFinalValue = FString::Printf(TEXT("Blueprint default: %s"), *Default.Summary);
			return;
		}

		if (Default.Status == EAssetArchetypeValueStatus::NativeDefaultFromLiveReflection)
		{
			OutDecodedValue = TEXT("<not serialized>");
			OutFinalValue = FString::Printf(TEXT("native default (live): %s"), *Default.Summary);
			return;
		}

		if (Default.Status != EAssetArchetypeValueStatus::Found)
		{
			return;
		}

		OutDecodedValue = TEXT("<not serialized>");
		OutFinalValue = FString::Printf(TEXT("inherited: %s"), *Default.Summary);

		// Containers are stored as deltas, so their serialized form cannot be compared with the inherited value directly.
		const bool bComparable = PresentDecoded != nullptr && PresentDecoded->Kind != EAssetDecodedValueKind::Set && PresentDecoded->Kind != EAssetDecodedValueKind::Map;
		if (bComparable && FAssetPropertyValueDecoder::BuildSemanticValueKey(Default.Value) == FAssetPropertyValueDecoder::BuildSemanticValueKey(*PresentDecoded))
		{
			OutNote += TEXT(" Equal to the serialized value on the other side, so only the serialization changed.");
		}
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

	/**
	 * Describes a struct field that one side stores and the other leaves out: Unreal omits fields equal to their defaults, so
	 * the omitted side has the value the same field has on the archetype chain.
	 */
	void DescribeOmittedElementField(const FString& StructName, const TArray<FString>& FieldPath, bool& bOutHasValue, FString& OutDecodedValue, FString& OutFinalValue, FString& OutNote)
	{
		bOutHasValue = true;
		OutDecodedValue = TEXT("<not serialized; likely default>");

		FString Text;
		if (AssetSchemaReflection::ExportStructFieldDefault(StructName, FieldPath, Text))
		{
			OutDecodedValue = TEXT("<not serialized>");
			OutFinalValue = FString::Printf(TEXT("struct default (live): %s"), Text.IsEmpty() ? TEXT("(empty)") : *Text);
			OutNote = FString::Printf(
				TEXT("Elements of an array are saved against the defaults of their struct, so a field left out has the default of %s, read from the running editor (live reflection)."), *StructName);
		}
	}

	void DescribeOmittedStructField(const FPropertyDiffNodeData& OmittedData, const TArray<FString>& FieldPath, bool& bOutHasValue, FString& OutDecodedValue, FString& OutFinalValue, FString& OutNote)
	{
		bOutHasValue = true;
		OutDecodedValue = TEXT("<not serialized; likely default>");

		if (OmittedData.ArchetypeResolver == nullptr || OmittedData.Node == nullptr)
		{
			return;
		}

		const FAssetOmittedPropertyDefault Default = OmittedData.ArchetypeResolver->DescribeOmittedField(OmittedData.Export.Index, OmittedData.Node->Name, OmittedData.Node->ArrayIndex, FieldPath);
		OutNote = Default.Note;

		if (Default.Status == EAssetArchetypeValueStatus::Found)
		{
			OutDecodedValue = TEXT("<not serialized>");
			OutFinalValue = FString::Printf(TEXT("inherited: %s"), *Default.Summary);
		}
	}

	/** The property nodes of the two exports whose value is being compared, with the struct field path of the entry being built. */
	struct FStructFieldContext
	{
		const FPropertyDiffNodeData* OldData = nullptr;
		const FPropertyDiffNodeData* NewData = nullptr;
		TArray<FString> FieldPath;

		/** Inside the struct element of an array: the element's struct and where its fields start in FieldPath. */
		FString ElementStruct;
		int32 ElementPathStart = 0;
	};

	void AppendDecodedValueDiffChildren(const FAssetDecodedValueDiff& ValueDiff, FAssetPackageDiffEntry& Parent, FStructFieldContext* Context = nullptr, const bool bParentIsStruct = false)
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

			// A field one side leaves out has the value its archetype chain gives it.
			const bool bIsStructField = Context != nullptr && bParentIsStruct;
			if (bIsStructField)
			{
				Context->FieldPath.Add(Child.Name);

				if (Child.State == EAssetDecodedValueDiffState::Added && Context->OldData != nullptr)
				{
					Entry.OldPresence = EAssetSerializedPropertyPresence::NotSerialized;
					if (!Context->ElementStruct.IsEmpty())
					{
						DescribeOmittedElementField(Context->ElementStruct, TArray<FString>(MakeArrayView(Context->FieldPath).RightChop(Context->ElementPathStart)), Entry.bHasOldDecodedValue,
							Entry.OldDecodedValue, Entry.OldFinalValue, Entry.OldFinalValueNote);
					}
					else
					{
						DescribeOmittedStructField(*Context->OldData, Context->FieldPath, Entry.bHasOldDecodedValue, Entry.OldDecodedValue, Entry.OldFinalValue, Entry.OldFinalValueNote);
					}
				}
				else if (Child.State == EAssetDecodedValueDiffState::Removed && Context->NewData != nullptr)
				{
					Entry.NewPresence = EAssetSerializedPropertyPresence::NotSerialized;
					if (!Context->ElementStruct.IsEmpty())
					{
						DescribeOmittedElementField(Context->ElementStruct, TArray<FString>(MakeArrayView(Context->FieldPath).RightChop(Context->ElementPathStart)), Entry.bHasNewDecodedValue,
							Entry.NewDecodedValue, Entry.NewFinalValue, Entry.NewFinalValueNote);
					}
					else
					{
						DescribeOmittedStructField(*Context->NewData, Context->FieldPath, Entry.bHasNewDecodedValue, Entry.NewDecodedValue, Entry.NewFinalValue, Entry.NewFinalValueNote);
					}
				}
			}

			// The fields of a struct element of an array are saved against the struct's defaults.
			const FString SavedElementStruct = Context != nullptr ? Context->ElementStruct : FString();
			const int32 SavedElementPathStart = Context != nullptr ? Context->ElementPathStart : 0;
			if (Context != nullptr && !bParentIsStruct && Child.Name.StartsWith(TEXT("[")))
			{
				const FString ElementStruct = StructNameOfType(Child.TypeName);
				if (!ElementStruct.IsEmpty())
				{
					Context->ElementStruct = ElementStruct;
					Context->ElementPathStart = Context->FieldPath.Num();
				}
			}

			AppendDecodedValueDiffChildren(Child, Entry, Context, Child.TypeName.StartsWith(TEXT("StructProperty")));

			if (Context != nullptr)
			{
				Context->ElementStruct = SavedElementStruct;
				Context->ElementPathStart = SavedElementPathStart;
			}

			if (bIsStructField)
			{
				Context->FieldPath.Pop();
			}

			Parent.Children.Add(MoveTemp(Entry));
		}
	}

	/** The place in the Name Map that the name stored at an offset of a document points at. */
	int32 ReadNameIndex(const FAssetPackageDocument& Document, const int64 Offset)
	{
		if (!Document.IsValidRange(Offset, 8))
		{
			return INDEX_NONE;
		}

		FAssetPackagePayloadReader Reader(Document, Offset, 8);
		FAssetPackageNameReference Reference;
		return Reader.ReadNameReference(Reference) ? Reference.NameIndex : INDEX_NONE;
	}

	/** A name a value holds that is the same in both versions but sits at another place in the Name Map. */
	struct FMovedName
	{
		FString Name;
		int32 OldIndex = INDEX_NONE;
		int32 NewIndex = INDEX_NONE;
	};

	/** Walks two decoded values of the same shape side by side and collects the names whose place in the Name Map differs. */
	void CollectMovedNames(
		const FAssetPackageDocument& OldDocument, const FAssetDecodedPropertyValue& Old, const FAssetPackageDocument& NewDocument, const FAssetDecodedPropertyValue& New, TArray<FMovedName>& Out)
	{
		if (Old.Children.Num() != New.Children.Num())
		{
			return;
		}

		if (Old.Children.IsEmpty())
		{
			if (Old.TypeName == TEXT("NameProperty") && New.TypeName == TEXT("NameProperty") && Old.Value == New.Value)
			{
				const int32 OldIndex = ReadNameIndex(OldDocument, Old.AbsoluteOffset);
				const int32 NewIndex = ReadNameIndex(NewDocument, New.AbsoluteOffset);
				if (OldIndex != INDEX_NONE && NewIndex != INDEX_NONE && OldIndex != NewIndex)
				{
					Out.Add({ Old.Value, OldIndex, NewIndex });
				}
			}
			return;
		}

		for (int32 Index = 0; Index < Old.Children.Num(); ++Index)
		{
			CollectMovedNames(OldDocument, Old.Children[Index], NewDocument, New.Children[Index], Out);
		}
	}

	/** Says why a value that decoded the same is stored with other bytes, when it holds names that moved in the Name Map. */
	FText DescribeMovedNames(const TArray<FMovedName>& Moved)
	{
		TArray<FString> Parts;
		for (int32 Index = 0; Index < Moved.Num() && Index < 3; ++Index)
		{
			Parts.Add(FString::Printf(TEXT("\"%s\" from Name[%d] to Name[%d]"), *Moved[Index].Name, Moved[Index].OldIndex, Moved[Index].NewIndex));
		}

		FString Text = FString::Printf(
			TEXT("The value is the same. A name is stored as its place in the Name Map, and it moved because names were added or removed before it: %s"), *FString::Join(Parts, TEXT(", ")));
		if (Moved.Num() > 3)
		{
			Text += FString::Printf(TEXT(" and %d more"), Moved.Num() - 3);
		}

		return FText::FromString(Text + TEXT("."));
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

			const FAssetDecodedPropertyValue NewDecoded = FAssetPropertyValueDecoder::Decode(NewData.Document, *NewData.Node, NewData.Export.SerialOffset);
			if (NewDecoded.HasDecodedChildren())
			{
				Entry.bHasNewDecodedValue = true;
				Entry.NewDecodedValue = NewDecoded.Value;
				Entry.NewValue = NewDecoded.Value;
				if (NewDecoded.IsSuccess())
				{
					ResolveFinalValue(NewData, NewDecoded, Entry.NewFinalValue, Entry.NewFinalValueNote);
				}
			}

			Entry.bHasOldDecodedValue = true;
			DescribeOmittedValue(OldData, *NewData.Node, NewDecoded.IsSuccess() ? &NewDecoded : nullptr, Entry.OldDecodedValue, Entry.OldFinalValue, Entry.OldFinalValueNote);

			Entry.SemanticPath = AssetPackageDiff::AppendSemanticPath(PayloadEntry.SemanticPath, Entry.Key);
			PayloadEntry.Children.Add(MoveTemp(Entry));

			return;
		}

		if (OldData.Node != nullptr && NewData.Node == nullptr)
		{
			Entry.State = EAssetPackageDiffState::Modified;
			Entry.OldPresence = EAssetSerializedPropertyPresence::Present;
			Entry.NewPresence = EAssetSerializedPropertyPresence::NotSerialized;

			const FAssetDecodedPropertyValue OldDecoded = FAssetPropertyValueDecoder::Decode(OldData.Document, *OldData.Node, OldData.Export.SerialOffset);
			if (OldDecoded.HasDecodedChildren())
			{
				Entry.bHasOldDecodedValue = true;
				Entry.OldDecodedValue = OldDecoded.Value;
				Entry.OldValue = OldDecoded.Value;
				if (OldDecoded.IsSuccess())
				{
					ResolveFinalValue(OldData, OldDecoded, Entry.OldFinalValue, Entry.OldFinalValueNote);
				}
			}

			Entry.bHasNewDecodedValue = true;
			DescribeOmittedValue(NewData, *OldData.Node, OldDecoded.IsSuccess() ? &OldDecoded : nullptr, Entry.NewDecodedValue, Entry.NewFinalValue, Entry.NewFinalValueNote);

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
		if (OldDecoded.HasDecodedChildren())
		{
			Entry.bHasOldDecodedValue = true;
			Entry.OldDecodedValue = OldDecoded.Value;
			Entry.OldValue = OldDecoded.Value;
			if (OldDecoded.IsSuccess())
			{
				ResolveFinalValue(OldData, OldDecoded, Entry.OldFinalValue, Entry.OldFinalValueNote);
			}
		}
		const FAssetDecodedPropertyValue NewDecoded = FAssetPropertyValueDecoder::Decode(NewData.Document, *NewData.Node, NewData.Export.SerialOffset);
		if (NewDecoded.HasDecodedChildren())
		{
			Entry.bHasNewDecodedValue = true;
			Entry.NewDecodedValue = NewDecoded.Value;
			Entry.NewValue = NewDecoded.Value;
			if (NewDecoded.IsSuccess())
			{
				ResolveFinalValue(NewData, NewDecoded, Entry.NewFinalValue, Entry.NewFinalValueNote);
			}
		}

		if (OldDecoded.Status == EAssetPropertyDecodeStatus::Partial || NewDecoded.Status == EAssetPropertyDecodeStatus::Partial)
		{
			Entry.Explanation = FText::FromString(TEXT(
				"An element of this array could not be decoded, so only the elements before it are compared. Elements from the first one that failed on are shown as not compared; see the hex view for their bytes."));
		}

		const FAssetDecodedPropertyValue* OldPtr = OldDecoded.HasDecodedChildren() ? &OldDecoded : nullptr;
		const FAssetDecodedPropertyValue* NewPtr = NewDecoded.HasDecodedChildren() ? &NewDecoded : nullptr;
		if (OldPtr != nullptr || NewPtr != nullptr)
		{
			const FAssetDecodedValueDiff ValueDiff = FAssetDecodedValueDiffer::Compare(OldPtr, NewPtr);

			// Bytes that differ with nothing different in the decoded values: the same entries stored in another order.
			if (OldPtr != nullptr && NewPtr != nullptr && OldDecoded.IsSuccess() && NewDecoded.IsSuccess() && ValueDiff.State == EAssetDecodedValueDiffState::Unchanged
				&& (OldDecoded.Kind == EAssetDecodedValueKind::Set || OldDecoded.Kind == EAssetDecodedValueKind::Map))
			{
				Entry.bRepresentationOnly = true;
				Entry.Explanation = FText::FromString(
					TEXT("The entries are the same; they are stored in another order. Maps and sets are written in the order of an internal hash table, which can change between saves."));
			}

			// Bytes that differ with nothing different in the decoded values, and a name that moved in the Name Map: say which.
			if (OldPtr != nullptr && NewPtr != nullptr && OldDecoded.IsSuccess() && NewDecoded.IsSuccess() && ValueDiff.State == EAssetDecodedValueDiffState::Unchanged && Entry.Explanation.IsEmpty())
			{
				TArray<FMovedName> Moved;
				CollectMovedNames(OldData.Document, OldDecoded, NewData.Document, NewDecoded, Moved);
				if (!Moved.IsEmpty())
				{
					Entry.Explanation = DescribeMovedNames(Moved);
				}
			}

			FStructFieldContext FieldContext;
			FieldContext.OldData = &OldData;
			FieldContext.NewData = &NewData;
			const bool bIsStruct = (OldPtr != nullptr && OldPtr->Kind == EAssetDecodedValueKind::Struct) || (NewPtr != nullptr && NewPtr->Kind == EAssetDecodedValueKind::Struct);
			AppendDecodedValueDiffChildren(ValueDiff, Entry, &FieldContext, bIsStruct);
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

	/** The ranges of an export's payload that no property accounts for, in order. */
	TArray<const FAssetSerializationTraceNode*> CollectNativeNodes(const FAssetSerializationTrace* Trace)
	{
		TArray<const FAssetSerializationTraceNode*> Result;

		if (Trace != nullptr && Trace->Root.IsValid())
		{
			for (const TSharedPtr<FAssetSerializationTraceNode>& Node : Trace->Root->Children)
			{
				if (Node.IsValid() && Node->Kind == EAssetSerializationTraceKind::Native)
				{
					Result.Add(Node.Get());
				}
			}
		}

		return Result;
	}

	/** Adds what the reading of a range's native data found different as the children of its entry. */
	void AddNativeDataChildren(const TArray<FAssetNativeDataChange>& Changes, FAssetPackageDiffEntry& RangeEntry)
	{
		for (const FAssetNativeDataChange& Change : Changes)
		{
			FAssetPackageDiffEntry Child;
			Child.Kind = EAssetPackageDiffKind::Property;
			Child.Key = Change.Key;
			Child.DisplayName = FText::FromString(Change.Title);
			Child.TypeName = TEXT("native data");
			Child.SemanticPath = AssetPackageDiff::AppendSemanticPath(RangeEntry.SemanticPath, Change.Key);
			Child.OldExportIndex = RangeEntry.OldExportIndex;
			Child.NewExportIndex = RangeEntry.NewExportIndex;
			Child.State = Change.State == FAssetNativeDataChange::EState::Added ? EAssetPackageDiffState::Added
				: Change.State == FAssetNativeDataChange::EState::Removed		? EAssetPackageDiffState::Removed
																				: EAssetPackageDiffState::Modified;

			if (Change.State != FAssetNativeDataChange::EState::Added)
			{
				Child.bHasOldDecodedValue = true;
				Child.OldDecodedValue = Change.OldValue;
				Child.OldValue = Change.OldValue;
			}

			if (Change.State != FAssetNativeDataChange::EState::Removed)
			{
				Child.bHasNewDecodedValue = true;
				Child.NewDecodedValue = Change.NewValue;
				Child.NewValue = Change.NewValue;
			}

			RangeEntry.Children.Add(MoveTemp(Child));
		}

		// Both sides were read to their last byte, so what differs is what the children say.
		RangeEntry.bNativeDataDecoded = !RangeEntry.Children.IsEmpty();
	}

	/** The data of a class or function, read on both sides. Returns false when the export is not one. */
	bool AppendStructDataChanges(const FAssetPackageDocument& OldDocument, const FAssetPackageExportEntry& OldExport, const FAssetPackageDocument& NewDocument,
		const FAssetPackageExportEntry& NewExport, FAssetPackageDiffEntry& RangeEntry)
	{
		FAssetStructNativeData OldData;
		FAssetStructNativeData NewData;
		if (!AssetStructNativeData::Decode(OldDocument, OldExport, RangeEntry.OldOffset, RangeEntry.OldSize, OldData)
			|| !AssetStructNativeData::Decode(NewDocument, NewExport, RangeEntry.NewOffset, RangeEntry.NewSize, NewData))
		{
			return false;
		}

		// Data that did not read to its last byte on both sides is not understood: what is read of it could be anything, so the range
		// stays opaque rather than being explained wrongly.
		if (OldData.bComplete && NewData.bComplete)
		{
			RangeEntry.NativeDataTitle = NSLOCTEXT("AssetPackageDiff", "ClassOrFunctionData", "Class or function data");
			AddNativeDataChildren(AssetStructNativeData::Compare(OldDocument, OldData, NewDocument, NewData), RangeEntry);
		}

		return true;
	}

	/**
	 * The pins of a graph node, read on both sides. When the pins are the same but the bytes differ, the references to other objects
	 * were renumbered (a node was added or removed before them in the export map), which is not a change of the graph.
	 */
	bool AppendPinChanges(const FAssetPackageDocument& OldDocument, const FAssetPackageExportEntry& OldExport, const FAssetGraphPinNames& OldNames, const FAssetPackageDocument& NewDocument,
		const FAssetPackageExportEntry& NewExport, const FAssetGraphPinNames& NewNames, FAssetPackageDiffEntry& RangeEntry)
	{
		FAssetGraphNodePins OldData;
		FAssetGraphNodePins NewData;
		if (!AssetGraphNodePins::Decode(OldDocument, OldExport, RangeEntry.OldOffset, RangeEntry.OldSize, OldData)
			|| !AssetGraphNodePins::Decode(NewDocument, NewExport, RangeEntry.NewOffset, RangeEntry.NewSize, NewData))
		{
			return false;
		}

		if (!OldData.bComplete || !NewData.bComplete)
		{
			return true;
		}

		RangeEntry.NativeDataTitle = NSLOCTEXT("AssetPackageDiff", "GraphNodePins", "Graph node pins");
		AddNativeDataChildren(AssetGraphNodePins::Compare(OldData, OldNames, NewData, NewNames), RangeEntry);

		if (RangeEntry.Children.IsEmpty())
		{
			RangeEntry.bRepresentationOnly = true;
			RangeEntry.Explanation =
				NSLOCTEXT("AssetPackageDiff", "PinsRenumbered", "The pins, their values and their links are the same; the bytes differ because the objects they refer to are numbered differently.");
		}

		return true;
	}

	/**
	 * The record of the source data of a texture, or of a mesh description, read on both sides. The data itself is not in the export,
	 * so a change shows as the content hash and the size of the data; when they are the same and only where the data is kept or its
	 * identifier differ, nothing of the asset changed.
	 */
	bool AppendBulkDataChanges(const FAssetPackageDocument& OldDocument, const FAssetPackageExportEntry& OldExport, const FAssetPackageDocument& NewDocument, const FAssetPackageExportEntry& NewExport,
		FAssetPackageDiffEntry& RangeEntry)
	{
		FAssetBulkDataExport OldData;
		FAssetBulkDataExport NewData;
		if (!AssetBulkDataExport::Decode(OldDocument, OldExport, RangeEntry.OldOffset, RangeEntry.OldSize, OldData)
			|| !AssetBulkDataExport::Decode(NewDocument, NewExport, RangeEntry.NewOffset, RangeEntry.NewSize, NewData))
		{
			return false;
		}

		if (!OldData.bComplete || !NewData.bComplete)
		{
			return true;
		}

		RangeEntry.NativeDataTitle =
			NewData.Kind == TEXT("Texture") ? NSLOCTEXT("AssetPackageDiff", "TextureData", "Texture data") : NSLOCTEXT("AssetPackageDiff", "MeshDescriptionData", "Mesh description data");
		AddNativeDataChildren(AssetBulkDataExport::Compare(OldData, NewData), RangeEntry);

		if (RangeEntry.Children.IsEmpty())
		{
			RangeEntry.bRepresentationOnly = true;
			RangeEntry.Explanation = NSLOCTEXT(
				"AssetPackageDiff", "BulkDataMoved", "The content of the data is the same; the bytes differ because of where the data is kept in the file and the identifier the save gave it.");
		}

		return true;
	}

	/** Reads the native data of a class, function or graph node on both sides and adds what differs in it as children of the range's entry. */
	void AppendNativeDataChanges(const FAssetPackageDocument& OldDocument, const FAssetPackageExportEntry& OldExport, const FAssetGraphPinNames& OldNames, const FAssetPackageDocument& NewDocument,
		const FAssetPackageExportEntry& NewExport, const FAssetGraphPinNames& NewNames, FAssetPackageDiffEntry& RangeEntry)
	{
		if (!AppendStructDataChanges(OldDocument, OldExport, NewDocument, NewExport, RangeEntry) && !AppendPinChanges(OldDocument, OldExport, OldNames, NewDocument, NewExport, NewNames, RangeEntry))
		{
			AppendBulkDataChanges(OldDocument, OldExport, NewDocument, NewExport, RangeEntry);
		}
	}

	/**
	 * Adds an entry for every native range of the two exports that changed: the bytes the inspector cannot decode. Ranges are
	 * paired by their reason (before the properties, after them, ...) and their order within it, and compared byte by byte, so a
	 * save that changes only native data is reported instead of passing as no change.
	 */
	void AppendNativeRangeDiffs(const FAssetPackageDocument& OldDocument, const FAssetPackageExportEntry& OldExport, const FAssetSerializationTrace* OldTrace, const FAssetPackageDocument& NewDocument,
		const FAssetPackageExportEntry& NewExport, const FAssetSerializationTrace* NewTrace, const FAssetGraphPinNames& OldNames, const FAssetGraphPinNames& NewNames,
		FAssetPackageDiffEntry& PayloadEntry)
	{
		const TArray<const FAssetSerializationTraceNode*> OldNodes = CollectNativeNodes(OldTrace);
		const TArray<const FAssetSerializationTraceNode*> NewNodes = CollectNativeNodes(NewTrace);

		const auto Find = [](const TArray<const FAssetSerializationTraceNode*>& Nodes, const FString& Reason, const int32 Occurrence) -> const FAssetSerializationTraceNode* {
			int32 Seen = 0;
			for (const FAssetSerializationTraceNode* Node : Nodes)
			{
				if (Node->TypeName == Reason && Seen++ == Occurrence)
				{
					return Node;
				}
			}
			return nullptr;
		};

		TSet<FString> Keys;
		TArray<TPair<FString, int32>> Order;
		const auto Collect = [&](const TArray<const FAssetSerializationTraceNode*>& Nodes) {
			TMap<FString, int32> Counts;
			for (const FAssetSerializationTraceNode* Node : Nodes)
			{
				const int32 Occurrence = Counts.FindOrAdd(Node->TypeName)++;
				const FString Key = FString::Printf(TEXT("%s#%d"), *Node->TypeName, Occurrence);
				if (!Keys.Contains(Key))
				{
					Keys.Add(Key);
					Order.Emplace(Node->TypeName, Occurrence);
				}
			}
		};
		Collect(NewNodes);
		Collect(OldNodes);

		const FAssetExportSummary Summary = AssetExportSummary::Summarize(NewDocument, NewExport, NewTrace);

		for (const TPair<FString, int32>& Item : Order)
		{
			const FAssetSerializationTraceNode* OldNode = Find(OldNodes, Item.Key, Item.Value);
			const FAssetSerializationTraceNode* NewNode = Find(NewNodes, Item.Key, Item.Value);

			FAssetPackageDiffEntry Entry;
			Entry.Kind = EAssetPackageDiffKind::UnknownPayloadRange;
			Entry.Key = Item.Value == 0 ? Item.Key : FString::Printf(TEXT("%s (%d)"), *Item.Key, Item.Value + 1);
			Entry.DisplayName = FText::FromString(Entry.Key);
			Entry.SemanticPath = AssetPackageDiff::AppendSemanticPath(PayloadEntry.SemanticPath, Entry.Key);
			Entry.TypeName = Item.Key;
			Entry.OldExportIndex = OldExport.Index;
			Entry.NewExportIndex = NewExport.Index;

			if (OldNode != nullptr)
			{
				Entry.OldOffset = OldExport.SerialOffset + OldNode->Offset;
				Entry.OldSize = OldNode->Size;
				Entry.OldValue = FString::Printf(TEXT("%lld bytes"), OldNode->Size);
			}

			if (NewNode != nullptr)
			{
				Entry.NewOffset = NewExport.SerialOffset + NewNode->Offset;
				Entry.NewSize = NewNode->Size;
				Entry.NewValue = FString::Printf(TEXT("%lld bytes"), NewNode->Size);
			}

			if (OldNode == nullptr)
			{
				Entry.State = EAssetPackageDiffState::Added;
				Entry.ChangedByteCount = NewNode->Size;
			}
			else if (NewNode == nullptr)
			{
				Entry.State = EAssetPackageDiffState::Removed;
				Entry.ChangedByteCount = OldNode->Size;
			}
			else
			{
				Entry.ChangedSpans = FAssetByteDiff::Compare(OldDocument, Entry.OldOffset, Entry.OldSize, NewDocument, Entry.NewOffset, Entry.NewSize);

				for (const FAssetByteDiffSpan& Span : Entry.ChangedSpans)
				{
					Entry.ChangedByteCount += Span.Size;
				}

				if (Entry.ChangedByteCount == 0 && Entry.OldSize == Entry.NewSize)
				{
					continue;
				}

				Entry.State = EAssetPackageDiffState::Modified;

				// The last range of an export is what its class writes after the properties. For a class or a function that is
				// readable (its variables, functions, bytecode), so say what differs in it instead of only how many bytes.
				if (OldNode == OldNodes.Last() && NewNode == NewNodes.Last())
				{
					AppendNativeDataChanges(OldDocument, OldExport, OldNames, NewDocument, NewExport, NewNames, Entry);
				}
			}

			FString Explanation = Summary.ToText();
			if (Entry.bRepresentationOnly)
			{
				Explanation = Entry.Explanation.ToString() + TEXT(" ") + Explanation;
			}

			if (Entry.bNativeDataDecoded && !Entry.Children.IsEmpty())
			{
				TArray<FString> Titles;
				for (const FAssetPackageDiffEntry& Child : Entry.Children)
				{
					Titles.Add(Child.DisplayName.ToString());
				}

				Explanation += FString::Printf(TEXT(" Read: %d changes (%s)."), Entry.Children.Num(), *FString::Join(Titles, TEXT("; ")));
			}

			Entry.Explanation = FText::FromString(Explanation);
			PayloadEntry.Children.Add(MoveTemp(Entry));
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

		// The names of the pins that links refer to, read the first time a link is described.
		const FAssetGraphPinNames OldPinNames(OldDocument, OldTraces);
		const FAssetGraphPinNames NewPinNames(NewDocument, NewTraces);

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
					AppendNativeRangeDiffs(OldDocument, A, OldTrace, NewDocument, B, NewTrace, OldPinNames, NewPinNames, Payload);
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

		AssetPackageDiff::AppendHeaderDiff(OldDocument, NewDocument, Result);
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
