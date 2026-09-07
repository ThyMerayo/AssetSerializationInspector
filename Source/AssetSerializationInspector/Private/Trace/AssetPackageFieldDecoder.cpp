// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Trace/AssetPackageFieldDecoder.h"

#include "UObject/OverriddenPropertySet.h"
#include "UObject/PropertyTag.h"

#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackagePayloadReader.h"
#include "Serialization/AssetPropertyTagDecoder.h"
#include "Serialization/AssetSerializedPropertyTag.h"
#include "Trace/AssetSerializationTrace.h"

#define LOCTEXT_NAMESPACE "FAssetPackageFieldDecoder"

namespace
{
	struct FAssetSerializedPropertyTag_OLD
	{
		FAssetPackageNameReference Name;
		FAssetPackageNameReference Type;

		int32 Size = 0;
		int32 ArrayIndex = 0;

		int64 TagOffset = 0;
		int64 ValueOffset = 0;

		FString ResolvedName;
		FString ResolvedType;
	};

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

	bool ReadPropertyTag(const FAssetPackageDocument& Document, FAssetPackagePayloadReader& Reader, FAssetSerializedPropertyTag_OLD& OutTag, FText& OutError)
	{
		OutTag = {};
		OutTag.TagOffset = Reader.Tell();

		if (!ReadPackageNameReference(Reader, OutTag.Name, Document.NameMap.Num(), OutError))
		{
			return false;
		}

		OutTag.ResolvedName = Document.ResolveNameReference(OutTag.Name);

		// "None" terminates the tagged-property list.
		if (OutTag.ResolvedName == TEXT("None"))
		{
			OutTag.ValueOffset = Reader.Tell();
			return true;
		}

		if (!ReadPackageNameReference(Reader, OutTag.Type, Document.NameMap.Num(), OutError))
		{
			return false;
		}

		OutTag.ResolvedType = Document.ResolveNameReference(OutTag.Type);

		if (!ReadInt<int32>(Reader, OutTag.Size))
		{
			OutError = LOCTEXT("PropertyTagSizeFailed", "Could not read property tag size.");
			return false;
		}

		if (!ReadInt<int32>(Reader, OutTag.ArrayIndex))
		{
			OutError = LOCTEXT("PropertyTagArrayIndexFailed", "Could not read property tag array index.");
			return false;
		}

		if (OutTag.Size < 0)
		{
			OutError = FText::Format(LOCTEXT("InvalidPropertyTagSize", "Property '{0}' declares an invalid size of {1}."), FText::FromString(OutTag.ResolvedName), FText::AsNumber(OutTag.Size));
			return false;
		}

		/*
		 * IMPORTANT:
		 * Type-specific tag metadata comes here.
		 *
		 * We will decode that next.
		 */

		OutTag.ValueOffset = Reader.Tell();

		return true;
	}

	bool DecodeExport_OLD(const FAssetPackageDocument& Document, const int32 ExportIndex, FAssetSerializationTrace& OutTrace)
	{
		if (!Document.ExportMap.IsValidIndex(ExportIndex))
		{
			return false;
		}

		const FAssetPackageExportEntry& Export = Document.ExportMap[ExportIndex];
		FAssetPackagePayloadReader Reader(Document, Export.SerialOffset, Export.SerialSize);

		OutTrace.ObjectPath = Document.ResolveExportPath(ExportIndex);
		OutTrace.PayloadOffset = Export.SerialOffset;
		OutTrace.PayloadSize = Export.SerialSize;
		OutTrace.Root = MakeShared<FAssetSerializationTraceNode>();
		OutTrace.Root->Kind = EAssetSerializationTraceKind::Object;
		OutTrace.Root->Name = OutTrace.ObjectPath;
		OutTrace.Root->Offset = 0;
		OutTrace.Root->Size = Export.SerialSize;

		const int64 ExportEnd = Export.SerialOffset + Export.SerialSize;
		while (!Reader.IsError() && Reader.Tell() < ExportEnd)
		{
			FAssetSerializedPropertyTag_OLD Tag;
			FText Error;

			if (!ReadPropertyTag(Document, Reader, Tag, Error))
			{
				break;
			}

			if (Tag.ResolvedName == TEXT("None"))
			{
				break;
			}

			const int64 ValueStart = Tag.ValueOffset;
			if (!Document.IsValidRange(ValueStart, Tag.Size))
			{
				break;
			}

			TSharedPtr<FAssetSerializationTraceNode> Node = MakeShared<FAssetSerializationTraceNode>();
			Node->Kind = EAssetSerializationTraceKind::Property;
			Node->Name = Tag.ResolvedName;
			Node->TypeName = Tag.ResolvedType;
			Node->Offset = ValueStart - Export.SerialOffset;
			Node->Size = Tag.Size;
			Node->Parent = OutTrace.Root;
			OutTrace.Root->Children.Add(Node);
			Reader.Seek(ValueStart + Tag.Size);
		}

		const int64 Current = Reader.Tell();
		if (Current < ExportEnd)
		{
			TSharedPtr<FAssetSerializationTraceNode> Unknown = MakeShared<FAssetSerializationTraceNode>();
			Unknown->Kind = EAssetSerializationTraceKind::Native;
			Unknown->Name = TEXT("<native / undecoded>");
			Unknown->Offset = Current - Export.SerialOffset;
			Unknown->Size = ExportEnd - Current;
			Unknown->Parent = OutTrace.Root;

			OutTrace.Root->Children.Add(Unknown);
		}

		/*
		 * We may have successfully decoded some leading tagged properties
		 * followed by native/custom data.
		 */
		return !OutTrace.Root->Children.IsEmpty();
	}

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

		const FAssetPackageExportEntry& Export = Document.ExportMap[ExportIndex];
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

		const bool bHasClassSerializationControl = Reader.UEVer() >= EUnrealEngineObjectUE5Version::PROPERTY_TAG_EXTENSION_AND_OVERRIDABLE_SERIALIZATION;

		if (!ReadSerializationControl(Reader, bHasClassSerializationControl, SerializationControl, Error))
		{
			AddUnknownRange(OutTrace, Export.ScriptSerializationStartOffset, ScriptSize, Error.ToString());
			return true;
		}

		bool bFoundTerminator = false;
		const int64 ExportEnd = Export.SerialOffset + Export.SerialSize;
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
				AddUnknownRange(OutTrace, BeforeTag - Export.SerialOffset, ExportEnd - BeforeTag, Error.ToString());
				break;
			}

			if (Tag.IsTerminator())
			{
				bFoundTerminator = true;
				break;
			}

			TSharedPtr<FAssetSerializationTraceNode> Node = MakeShared<FAssetSerializationTraceNode>();
			Node->Kind = EAssetSerializationTraceKind::Property;
			Node->Name = Tag.ResolvedName;
			Node->TypeName = Tag.Type.ToString();
			Node->PropertyType = Tag.Type;
			Node->Offset = Tag.ValueOffset - Export.SerialOffset;
			Node->Size = Tag.Size;
			Node->Parent = OutTrace.Root;
			if (Tag.Type.Name == TEXT("BoolProperty"))
			{
				Node->bHasInlineBoolValue = true;
				Node->bInlineBoolValue = Tag.bBoolValue;
			}
			OutTrace.Root->Children.Add(Node);

			Reader.Seek(Tag.ValueOffset + Tag.Size);
		}

		if (Export.ScriptSerializationEndOffset < Export.SerialSize)
		{
			AddUnknownRange(OutTrace, Export.ScriptSerializationEndOffset, Export.SerialSize - Export.ScriptSerializationEndOffset, TEXT("Native/custom serialization after properties"));
		}

		const int64 Current = Reader.Tell();
		if (Current < ExportEnd)
		{
			AddUnknownRange(OutTrace, Current - Export.SerialOffset, ExportEnd - Current, bFoundTerminator ? TEXT("Native/custom serialized data") : TEXT("Undecoded payload"));
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
