// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Trace/AssetPackageFieldDecoder.h"

#include "UObject/PropertyTag.h"

#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackagePayloadReader.h"
#include "Trace/AssetSerializationTrace.h"

#define LOCTEXT_NAMESPACE "FAssetPackageFieldDecoder"

namespace
{
	struct FAssetSerializedPropertyTag
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

	bool ReadPropertyTag(const FAssetPackageDocument& Document, FAssetPackagePayloadReader& Reader, FAssetSerializedPropertyTag& OutTag, FText& OutError)
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

	bool DecodeExport(const FAssetPackageDocument& Document, const int32 ExportIndex, FAssetSerializationTrace& OutTrace)
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
			FAssetSerializedPropertyTag Tag;
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
