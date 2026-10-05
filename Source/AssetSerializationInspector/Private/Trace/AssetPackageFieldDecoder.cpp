// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Trace/AssetPackageFieldDecoder.h"

#include "UObject/OverriddenPropertySet.h"
#include "UObject/PropertyTag.h"

#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackagePayloadReader.h"
#include "Serialization/AssetPropertyTagDecoder.h"
#include "Serialization/AssetSchemaReflection.h"
#include "Serialization/AssetSerializedPropertyTag.h"
#include "Serialization/AssetUnversionedProperties.h"
#include "Trace/AssetSerializationTrace.h"
#include "UObject/Class.h"

#define LOCTEXT_NAMESPACE "FAssetPackageFieldDecoder"

namespace
{
	struct FAssetSerializationControl
	{
		uint32 RawExtensions = 0;

		bool bHasOverridableSerializationInformation = false;

		uint8 RawOverridableOperation = 0;

		int64 Offset = 0;
		int64 Size = 0;
	};

	enum class EAssetClassSerializationControlExtension : uint8
	{
		NoExtension = 0x00,
		ReserveForFutureUse = 0x01, // Can be use to add a next group of extension

		////////////////////////////////////////////////
		// First extension group
		OverridableSerializationInformation = 0x02,

		//
		// Add more extension for the first group here
		//
	};
	ENUM_CLASS_FLAGS(EAssetClassSerializationControlExtension);

	/**
	 * The properties of an export saved without tags, read with the class of the running editor: one node per property (zero values
	 * included), the header and whatever follows or cannot be read as undecoded ranges. Returns false when the stream could not be
	 * read at all, so that the caller reports the whole export as undecoded.
	 */
	bool AddUnversionedProperties(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, const UStruct* Class, FAssetSerializationTrace& OutTrace);

	void AddUnknownRange(FAssetSerializationTrace& Trace, const int64 RelativeOffset, const int64 Size, const FString& Reason)
	{
		if (Size <= 0 || !Trace.Root.IsValid())
		{
			return;
		}

		TSharedPtr<FAssetSerializationTraceNode> Node = MakeShared<FAssetSerializationTraceNode>();
		Node->Kind = EAssetSerializationTraceKind::Native;
		Node->Name = TEXT("<native / undecoded>");
		Node->TypeName = Reason;
		Node->Offset = RelativeOffset;
		Node->Size = Size;
		Node->Parent = Trace.Root;
		Trace.Root->Children.Add(MoveTemp(Node));
	}

	bool AddUnversionedProperties(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, const UStruct* Class, FAssetSerializationTrace& OutTrace)
	{
		const int64 ScriptStart = Export.HasScriptSerializationRange() ? Export.SerialOffset + Export.ScriptSerializationStartOffset : Export.SerialOffset;
		const int64 ScriptEnd = Export.HasScriptSerializationRange() ? Export.SerialOffset + Export.ScriptSerializationEndOffset : Export.SerialOffset + Export.SerialSize;

		TArray<FAssetUnversionedValue> Values;
		int64 EndOffset = ScriptStart;
		FString Error;
		const bool bRead = AssetUnversionedProperties::Read(Document, Class, ScriptStart, ScriptEnd, Values, EndOffset, Error);

		// An unreadable header means the class is not the one the package was saved with: nothing to show.
		if (!bRead && Values.IsEmpty() && EndOffset == ScriptStart)
		{
			return false;
		}

		if (Export.ScriptSerializationStartOffset > 0 && Export.HasScriptSerializationRange())
		{
			AddUnknownRange(OutTrace, 0, Export.ScriptSerializationStartOffset, TEXT("Native/custom serialization before properties"));
		}

		// The values that were read; a value that failed is left to the undecoded range after them.
		const int32 ValueCount = Values.Num();
		const int64 HeaderEnd = Values.IsEmpty() ? EndOffset : Values[0].Offset;
		AddUnknownRange(OutTrace, ScriptStart - Export.SerialOffset, HeaderEnd - ScriptStart, TEXT("Unversioned property header (fragments and zero mask)"));

		for (int32 Index = 0; Index < ValueCount; ++Index)
		{
			const FAssetUnversionedValue& Slot = Values[Index];

			TSharedPtr<FAssetSerializationTraceNode> Node = MakeShared<FAssetSerializationTraceNode>();
			Node->Kind = EAssetSerializationTraceKind::Property;
			Node->Name = Slot.Property->GetName();
			Node->TypeName = Slot.Type.ToString();
			Node->PropertyType = Slot.Type;
			Node->ArrayIndex = Slot.ArrayIndex;
			Node->Offset = Slot.Offset - Export.SerialOffset;
			Node->Size = Slot.Size;
			Node->bIsZeroValue = Slot.bZero;
			Node->Parent = OutTrace.Root;

			if (Slot.Type.Name == TEXT("BoolProperty") && Slot.bZero)
			{
				Node->bHasInlineBoolValue = true;
				Node->bInlineBoolValue = false;
			}

			OutTrace.Root->Children.Add(MoveTemp(Node));
		}

		const int64 ReadEnd = EndOffset;
		if (ReadEnd < ScriptEnd)
		{
			AddUnknownRange(OutTrace, ReadEnd - Export.SerialOffset, ScriptEnd - ReadEnd, bRead ? TEXT("Native/custom serialized data") : TEXT("Undecoded payload"));
		}

		if (Export.HasScriptSerializationRange() && Export.ScriptSerializationEndOffset < Export.SerialSize)
		{
			AddUnknownRange(OutTrace, Export.ScriptSerializationEndOffset, Export.SerialSize - Export.ScriptSerializationEndOffset, TEXT("Native/custom serialization after properties"));
		}

		return true;
	}

	bool LooksLikeTaggedPropertyStream(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export)
	{
		if (Export.SerialSize < 8 || !Document.IsValidRange(Export.SerialOffset, 8))
		{
			return false;
		}

		int32 NameIndex = INDEX_NONE;
		int32 Number = 0;

		FMemory::Memcpy(&NameIndex, Document.FileData.GetData() + Export.SerialOffset, sizeof(int32));
		FMemory::Memcpy(&Number, Document.FileData.GetData() + Export.SerialOffset + sizeof(int32), sizeof(int32));

		return NameIndex >= 0 && NameIndex < Document.NameMap.Num() && Number >= 0;
	}

	bool ReadSerializationControl(FAssetPackagePayloadReader& Reader, const bool bIsUClass, FAssetSerializationControl& OutControl, FText& OutError)
	{
		OutControl = {};

		if (!bIsUClass || Reader.UEVer() < EUnrealEngineObjectUE5Version::PROPERTY_TAG_EXTENSION_AND_OVERRIDABLE_SERIALIZATION)
		{
			return true;
		}

		const int64 StartOffset = Reader.Tell();

		EAssetClassSerializationControlExtension Extensions = EAssetClassSerializationControlExtension::NoExtension;
		Reader << Extensions;

		if (Reader.IsError())
		{
			OutError = NSLOCTEXT("AssetPropertyTagDecoder", "SerializationControlReadFailed", "Could not read class serialization control extensions.");
			return false;
		}

		OutControl.RawExtensions = static_cast<uint32>(Extensions);

		if (EnumHasAnyFlags(Extensions, EAssetClassSerializationControlExtension::OverridableSerializationInformation))
		{
			EOverriddenPropertyOperation Operation = EOverriddenPropertyOperation::None;
			Reader << Operation;

			if (Reader.IsError())
			{
				OutError = NSLOCTEXT("AssetPropertyTagDecoder", "OverridableOperationReadFailed", "Could not read the class overridable operation.");
				return false;
			}

			OutControl.bHasOverridableSerializationInformation = true;
			OutControl.RawOverridableOperation = static_cast<uint8>(Operation);
		}

		OutControl.Offset = StartOffset;
		OutControl.Size = Reader.Tell() - StartOffset;

		return true;
	}

	bool DecodeExport(const FAssetPackageDocument& Document, const int32 ExportIndex, FAssetSerializationTrace& OutTrace)
	{
		if (!Document.ExportMap.IsValidIndex(ExportIndex))
		{
			return false;
		}

		FAssetPackageExportEntry Export = Document.ExportMap[ExportIndex];
		if (Export.SerialSize <= 0 || !Document.IsValidExportPayload(Export))
		{
			return false;
		}

		OutTrace.ObjectPath = Document.ResolveExportPath(ExportIndex);
		OutTrace.PayloadOffset = Export.SerialOffset;
		OutTrace.PayloadSize = Export.SerialSize;
		OutTrace.Root = MakeShared<FAssetSerializationTraceNode>();
		OutTrace.Root->Kind = EAssetSerializationTraceKind::Object;
		OutTrace.Root->Name = OutTrace.ObjectPath;
		OutTrace.Root->Offset = 0;
		OutTrace.Root->Size = Export.SerialSize;

		// Cooked packages usually save their properties without tags: a bit mask of which properties are set, then the values in the
		// order of the class's property list. Reading that needs the class, so the export is reported as one undecoded range rather
		// than read as tags (which would produce garbage).
		if (AssetUnversionedProperties::IsUsedBy(Document))
		{
			const UClass* NativeObjectClass = AssetSchemaReflection::FindNativeClass(Document, Export.Index);
			if (NativeObjectClass == nullptr || !AddUnversionedProperties(Document, Export, NativeObjectClass, OutTrace))
			{
				AddUnknownRange(OutTrace, 0, Export.SerialSize, TEXT("Properties saved without tags (unversioned property serialization)"));
			}

			return true;
		}

		/*
		 * Before SCRIPT_SERIALIZATION_OFFSET the export map does not say where the tagged properties are. For most objects
		 * they are the first thing in the export, so try the whole export; the tag reader stops and reports whatever is left as
		 * undecoded when it is not a property stream (classes and other objects with a native header first).
		 */
		if (!Export.HasScriptSerializationRange() && (Document.PackageSummary.GetPackageFlags() & PKG_UnversionedProperties) == 0
			&& Document.PackageSummary.GetFileVersionUE() < EUnrealEngineObjectUE5Version::SCRIPT_SERIALIZATION_OFFSET && LooksLikeTaggedPropertyStream(Document, Export))
		{
			Export.ScriptSerializationStartOffset = 0;
			Export.ScriptSerializationEndOffset = Export.SerialSize;
		}

		if (!Export.HasScriptSerializationRange())
		{
			AddUnknownRange(OutTrace, 0, Export.SerialSize, TEXT("Export has no tagged property serialization range"));
			return true;
		}

		const int64 ScriptStart = Export.SerialOffset + Export.ScriptSerializationStartOffset;
		const int64 ScriptSize = Export.ScriptSerializationEndOffset - Export.ScriptSerializationStartOffset;

		if (!Document.IsValidRange(ScriptStart, ScriptSize))
		{
			AddUnknownRange(OutTrace, 0, Export.SerialSize, TEXT("Invalid script serialization range"));
			return true;
		}

		if (Export.ScriptSerializationStartOffset > 0)
		{
			AddUnknownRange(OutTrace, 0, Export.ScriptSerializationStartOffset, TEXT("Native/custom serialization before properties"));
		}

		FAssetPackagePayloadReader Reader(Document, ScriptStart, ScriptSize);

		FAssetSerializationControl SerializationControl;
		FText Error;

		// Every object, not only class default objects, starts its tagged properties with the serialization control byte
		// (UStruct::SerializeVersionedTaggedProperties writes it for any object whose class is a UClass).
		if (!ReadSerializationControl(Reader, /*bIsUClass*/ true, SerializationControl, Error))
		{
			AddUnknownRange(OutTrace, Export.ScriptSerializationStartOffset, ScriptSize, Error.ToString());
			return true;
		}

		const UStruct* NativeClass = AssetSchemaReflection::FindNativeClass(Document, Export.Index);
		bool bFoundTerminator = false;
		bool bRestAttributed = false;
		const int64 ExportEnd = Export.SerialOffset + Export.SerialSize;
		const int64 ScriptEnd = ScriptStart + ScriptSize;
		while (!Reader.IsError() && Reader.Tell() < ExportEnd)
		{
			const int64 BeforeTag = Reader.Tell();

			FAssetSerializedPropertyTag Tag;

			// Troubleshooting for extra byte before FName entry
			// 			{
			// 				UE_LOG(LogTemp, Log, TEXT("Export[%d] %s: Payload=[0x%llX,+%lld] Script=[+0x%llX,+0x%llX]"), Export.Index, *Document.ResolveExportPath(Export.Index), Export.SerialOffset,
			// 					Export.SerialSize, Export.ScriptSerializationStartOffset, Export.ScriptSerializationEndOffset);
			//
			// 				FString Bytes;
			// 				const int64 PreviewSize = FMath::Min<int64>(32, Export.SerialSize);
			// 				for (int64 Index = 0; Index < PreviewSize; ++Index)
			// 				{
			// 					Bytes += FString::Printf(TEXT("%02X "), Document.FileData[Export.SerialOffset + Index]);
			// 				}
			// 				UE_LOG(LogTemp, Log, TEXT("Payload: %s"), *Bytes);
			//
			// 				int32 CandidateNameIndex = INDEX_NONE;
			// 				int32 CandidateNumber = 0;
			//
			// 				FMemory::Memcpy(&CandidateNameIndex, Document.FileData.GetData() + ScriptStart, sizeof(int32));
			// 				FMemory::Memcpy(&CandidateNumber, Document.FileData.GetData() + ScriptStart + sizeof(int32), sizeof(int32));
			//
			// 				UE_LOG(LogTemp, Log, TEXT("Script start 0x%llX: Candidate NameIndex=%d Number=%d NameCount=%d"), ScriptStart, CandidateNameIndex, CandidateNumber, Document.NameMap.Num());
			// 			}
			//
			// 			{
			// 				const uint8 FirstByte = Document.FileData[ScriptStart];
			//
			// 				int32 CandidateNameIndex = INDEX_NONE;
			// 				int32 CandidateNumber = 0;
			//
			// 				FMemory::Memcpy(&CandidateNameIndex, Document.FileData.GetData() + ScriptStart + 1, sizeof(int32));
			// 				FMemory::Memcpy(&CandidateNumber, Document.FileData.GetData() + ScriptStart + 1 + sizeof(int32), sizeof(int32));
			//
			// 				UE_LOG(LogTemp, Log, TEXT("Script prefix=0x%02X, candidate at +1: NameIndex=%d Number=%d"), FirstByte, CandidateNameIndex, CandidateNumber);
			//
			// 				if (Document.NameMap.IsValidIndex(CandidateNameIndex))
			// 				{
			// 					UE_LOG(LogTemp, Log, TEXT("Candidate name: %s"), *Document.NameMap[CandidateNameIndex].Name);
			// 				}
			// 			}
			//
			// This is also not needed once we are skipping the control byte
			// 			if (!LooksLikeTaggedPropertyStream(Document, Export))
			// 			{
			// 				AddUnknownRange(OutTrace, 0, Export.SerialSize, TEXT("Export does not begin with a recognizable tagged-property stream"));
			// 			}

			if (!FAssetPropertyTagDecoder::ReadTag(Document, Reader, Tag, Error))
			{
				AddUnknownRange(OutTrace, BeforeTag - Export.SerialOffset, ScriptEnd - BeforeTag, Error.ToString());
				bRestAttributed = true;
				break;
			}

			if (Tag.IsTerminator())
			{
				bFoundTerminator = true;
				break;
			}

			// Packages saved before UE 5.4 leave the struct of a map or set element unnamed; the class in the running editor knows it.
			AssetSchemaReflection::CompleteType(NativeClass, Tag.ResolvedName, Tag.Type);

			TSharedPtr<FAssetSerializationTraceNode> Node = MakeShared<FAssetSerializationTraceNode>();
			Node->Kind = EAssetSerializationTraceKind::Property;
			Node->Name = Tag.ResolvedName;
			Node->TypeName = Tag.Type.ToString();
			Node->PropertyType = Tag.Type;
			Node->ArrayIndex = Tag.ArrayIndex;
			Node->Offset = Tag.ValueOffset - Export.SerialOffset;
			Node->Size = Tag.Size;
			Node->Parent = OutTrace.Root;
			Node->bBinaryOrNative = Tag.SerializeType == EAssetPropertyTagSerializeType::BinaryOrNative;
			if (Tag.Type.Name == TEXT("BoolProperty"))
			{
				Node->bHasInlineBoolValue = true;
				Node->bInlineBoolValue = Tag.bBoolValue;
			}
			OutTrace.Root->Children.Add(Node);

			Reader.Seek(Tag.ValueOffset + Tag.Size);
		}

		// What is left of the script serialization range, then what follows it, each attributed once and in order.
		const int64 Current = Reader.Tell();
		if (!bRestAttributed && Current < ScriptEnd)
		{
			AddUnknownRange(OutTrace, Current - Export.SerialOffset, ScriptEnd - Current, bFoundTerminator ? TEXT("Native/custom serialized data") : TEXT("Undecoded payload"));
		}

		if (Export.ScriptSerializationEndOffset < Export.SerialSize)
		{
			AddUnknownRange(OutTrace, Export.ScriptSerializationEndOffset, Export.SerialSize - Export.ScriptSerializationEndOffset, TEXT("Native/custom serialization after properties"));
		}

		return !OutTrace.Root->Children.IsEmpty();
	}
} // namespace

TSharedPtr<FAssetPackageTraceCollection> FAssetPackageFieldDecoder::Decode(const FAssetPackageDocument& Document)
{
	TSharedPtr<FAssetPackageTraceCollection> Result = MakeShared<FAssetPackageTraceCollection>();

	if (!Document.bHasDecodedExportMap)
	{
		return Result;
	}

	for (const FAssetPackageExportEntry& Export : Document.ExportMap)
	{
		if (Export.SerialSize <= 0 || !Document.IsValidExportPayload(Export))
		{
			continue;
		}

		FAssetSerializationTrace Trace;

		if (DecodeExport(Document, Export.Index, Trace))
		{
			Result->ExportTraces.Add(Export.Index, MoveTemp(Trace));
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
