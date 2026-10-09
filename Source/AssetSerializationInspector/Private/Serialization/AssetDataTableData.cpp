// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetDataTableData.h"

#include "Engine/DataTable.h"

#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetNativeReader.h"
#include "Serialization/AssetPropertyValueDecoder.h"
#include "Serialization/AssetSchemaReflection.h"
#include "Trace/AssetSerializationTrace.h"

namespace
{
	/** A row takes at least the name (8 bytes) and the end of its properties (8 bytes), so a count beyond what fits is not a count. */
	constexpr int64 MinimumRowBytes = 16;

	/** The path of the object the RowStruct property refers to; sets bFound when the export has the property at all. */
	FString ReadRowStruct(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, const FAssetSerializationTrace* Trace, bool& bFound, FString& OutError)
	{
		bFound = false;
		if (Trace == nullptr || !Trace->Root.IsValid())
		{
			OutError = TEXT("The properties of the table were not read, so its row struct is not known");
			return FString();
		}

		for (const TSharedPtr<FAssetSerializationTraceNode>& Node : Trace->Root->Children)
		{
			if (!Node.IsValid() || Node->Kind != EAssetSerializationTraceKind::Property || Node->Name != TEXT("RowStruct"))
			{
				continue;
			}

			bFound = true;
			if (Node->bIsZeroValue)
			{
				return FString();
			}

			FNativeReader Reader(Document, Export.SerialOffset + Node->Offset, Node->Size);
			const FString Path = Reader.ReadObject();
			if (!Reader.Ok())
			{
				OutError = Reader.GetError();
			}
			return Path;
		}

		return FString();
	}

	/** The name of a struct from its path: what follows the last dot. */
	FString StructNameOf(const FString& Path)
	{
		int32 Dot = INDEX_NONE;
		if (Path.FindLastChar(TEXT('.'), Dot))
		{
			return Path.Mid(Dot + 1);
		}

		int32 Slash = INDEX_NONE;
		return Path.FindLastChar(TEXT('/'), Slash) ? Path.Mid(Slash + 1) : Path;
	}

	/** The name of a property as the user knows it: a Blueprint struct saves its members as Name_Number_Guid, and only the name is shown. */
	FString DisplayName(const FString& Name)
	{
		int32 Last = INDEX_NONE;
		if (Name.FindLastChar(TEXT('_'), Last) && Name.Len() - Last - 1 == 32)
		{
			int32 Previous = INDEX_NONE;
			const FString Head = Name.Left(Last);
			if (Head.FindLastChar(TEXT('_'), Previous) && Previous > 0 && Head.Mid(Previous + 1).IsNumeric())
			{
				return Head.Left(Previous);
			}
		}
		return Name;
	}

	FString JoinProperties(const TArray<TPair<FString, FString>>& Properties)
	{
		TArray<FString> Parts;
		Parts.Reserve(Properties.Num());
		for (const TPair<FString, FString>& Property : Properties)
		{
			Parts.Add(Property.Key + TEXT("=") + Property.Value);
		}
		return FString::Join(Parts, TEXT(", "));
	}
} // namespace

FString FAssetDataTableRow::Describe() const
{
	return Properties.IsEmpty() ? FString(TEXT("(no properties)")) : JoinProperties(Properties);
}

FString FAssetDataTableData::Summarize() const
{
	return FString::Printf(TEXT("Data table: %d rows%s"), Rows.Num(), RowStruct.IsEmpty() ? TEXT("") : *(TEXT(" of ") + StructNameOf(RowStruct)));
}

bool AssetDataTableData::Decode(
	const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, const int64 NativeOffset, const int64 NativeSize, FAssetDataTableData& Out, const FAssetSerializationTrace* Trace)
{
	const UClass* NativeClass = AssetSchemaReflection::FindNativeClass(Document, Export.Index);
	if (NativeClass == nullptr || !NativeClass->IsChildOf(UDataTable::StaticClass()) || NativeSize <= 0 || !Document.IsValidRange(NativeOffset, NativeSize))
	{
		return false;
	}

	Out = FAssetDataTableData();
	Out.Offset = NativeOffset;
	Out.Size = NativeSize;

	FNativeReader Reader(Document, NativeOffset, NativeSize);

	// UObject::Serialize ends with the object's GUID, when it has one.
	if (Reader.ReadBool())
	{
		Out.ObjectGuid = Reader.ReadGuid().ToString(EGuidFormats::DigitsWithHyphens);
	}

	bool bHasRowStructProperty = false;
	FString RowStructError;
	Out.RowStruct = ReadRowStruct(Document, Export, Trace, bHasRowStructProperty, RowStructError);
	if (!RowStructError.IsEmpty())
	{
		Out.Error = RowStructError;
		return true;
	}

	// UDataTable::LoadStructData: the number of rows, then for each its name and the row struct written as tagged properties.
	const int32 RowCount = Reader.Read<int32>();
	if (Reader.Ok() && (RowCount < 0 || RowCount > Reader.Remaining() / MinimumRowBytes))
	{
		Reader.Fail(TEXT("The number of rows does not fit the data"));
	}

	FAssetSerializedPropertyType RowType;
	RowType.Name = TEXT("StructProperty");
	RowType.Parameters.AddDefaulted_GetRef().Name = Out.RowStruct.IsEmpty() ? FString(TEXT("TableRowBase")) : StructNameOf(Out.RowStruct);

	for (int32 Index = 0; Index < RowCount && Reader.Ok(); ++Index)
	{
		FAssetDataTableRow& Row = Out.Rows.AddDefaulted_GetRef();
		Row.Name = Reader.ReadName();
		if (!Reader.Ok())
		{
			break;
		}

		const int64 Start = Reader.Tell();
		FAssetDecodedPropertyValue Value;
		if (!FAssetPropertyValueDecoder::DecodeTypeAt(Document, RowType, Start, Reader.Remaining(), Value) || !Value.IsSuccess())
		{
			Reader.Fail(FString::Printf(TEXT("Row '%s' is not read: %s"), *Row.Name, Value.Error.IsEmpty() ? TEXT("a value of it does not decode") : *Value.Error));
			break;
		}

		// A property that decoded in part would make the row look like what it is not.
		for (const FAssetDecodedPropertyValue& Child : Value.Children)
		{
			if (!Child.IsSuccess())
			{
				Reader.Fail(FString::Printf(TEXT("Property '%s' of row '%s' is not read: %s"), *Child.Name, *Row.Name, Child.Error.IsEmpty() ? *Child.TypeName : *Child.Error));
				break;
			}
			Row.Properties.Emplace(Child.Name, FAssetPropertyValueDecoder::FormatForDisplay(Child, 32));
		}
		if (!Reader.Ok())
		{
			break;
		}

		Reader.Seek(Start + Value.Size);
	}

	if (!Reader.Ok())
	{
		Out.Error = Reader.GetError();
	}
	else if (Reader.Remaining() != 0)
	{
		Out.Error = FString::Printf(TEXT("%lld bytes follow what this reading knows"), Reader.Remaining());
	}
	else
	{
		Out.bComplete = true;
	}

	return true;
}

TArray<FAssetNativeDataChange> AssetDataTableData::Compare(const FAssetDataTableData& Old, const FAssetDataTableData& New)
{
	TArray<FAssetNativeDataChange> Changes;
	if (!Old.bComplete || !New.bComplete)
	{
		return Changes;
	}

	const auto Add = [&Changes](const FString& Key, const FString& Title, const FAssetNativeDataChange::EState State, const FString& OldValue, const FString& NewValue) {
		FAssetNativeDataChange& Change = Changes.AddDefaulted_GetRef();
		Change.Key = Key;
		Change.Title = Title;
		Change.State = State;
		Change.OldValue = OldValue;
		Change.NewValue = NewValue;
	};

	// A row that appears or goes away is listed property by property, like a row that changed, so every value has its own entry.
	const auto AddWholeRow = [&Add](const FAssetDataTableRow& Row, const FAssetNativeDataChange::EState State) {
		const bool bAdded = State == FAssetNativeDataChange::EState::Added;
		const FString Key = TEXT("Row/") + Row.Name;
		const FString Title = FString::Printf(TEXT("Row %s"), *Row.Name);
		if (Row.Properties.IsEmpty())
		{
			Add(Key, Title, State, bAdded ? FString() : Row.Describe(), bAdded ? Row.Describe() : FString());
		}
		for (const TPair<FString, FString>& Property : Row.Properties)
		{
			Add(Key + TEXT("/") + Property.Key, FString::Printf(TEXT("%s: %s"), *Title, *DisplayName(Property.Key)), State, bAdded ? FString() : Property.Value, bAdded ? Property.Value : FString());
		}
	};

	if (!Old.RowStruct.Equals(New.RowStruct, ESearchCase::CaseSensitive))
	{
		Add(TEXT("RowStruct"), TEXT("Row struct"), FAssetNativeDataChange::EState::Modified, Old.RowStruct, New.RowStruct);
	}

	// The rows are paired by name: the table is a map, and the order it is written in is not part of what it holds.
	TMap<FString, const FAssetDataTableRow*> NewRows;
	for (const FAssetDataTableRow& Row : New.Rows)
	{
		NewRows.Add(Row.Name, &Row);
	}
	TSet<FString> Seen;

	for (const FAssetDataTableRow& OldRow : Old.Rows)
	{
		const FString Key = TEXT("Row/") + OldRow.Name;
		const FString Title = FString::Printf(TEXT("Row %s"), *OldRow.Name);
		const FAssetDataTableRow* const* Found = NewRows.Find(OldRow.Name);
		if (Found == nullptr)
		{
			AddWholeRow(OldRow, FAssetNativeDataChange::EState::Removed);
			continue;
		}

		Seen.Add(OldRow.Name);
		const FAssetDataTableRow& NewRow = **Found;
		TMap<FString, const FString*> NewValues;
		for (const TPair<FString, FString>& Property : NewRow.Properties)
		{
			NewValues.Add(Property.Key, &Property.Value);
		}

		TSet<FString> SeenProperties;
		for (const TPair<FString, FString>& Property : OldRow.Properties)
		{
			const FString PropertyKey = Key + TEXT("/") + Property.Key;
			const FString PropertyTitle = FString::Printf(TEXT("%s: %s"), *Title, *DisplayName(Property.Key));
			const FString* const* NewValue = NewValues.Find(Property.Key);
			if (NewValue == nullptr)
			{
				Add(PropertyKey, PropertyTitle, FAssetNativeDataChange::EState::Removed, Property.Value, FString());
				continue;
			}

			SeenProperties.Add(Property.Key);
			if (!Property.Value.Equals(**NewValue, ESearchCase::CaseSensitive))
			{
				Add(PropertyKey, PropertyTitle, FAssetNativeDataChange::EState::Modified, Property.Value, **NewValue);
			}
		}
		for (const TPair<FString, FString>& Property : NewRow.Properties)
		{
			if (!SeenProperties.Contains(Property.Key))
			{
				Add(Key + TEXT("/") + Property.Key, FString::Printf(TEXT("%s: %s"), *Title, *DisplayName(Property.Key)), FAssetNativeDataChange::EState::Added, FString(), Property.Value);
			}
		}
	}

	for (const FAssetDataTableRow& NewRow : New.Rows)
	{
		if (!Seen.Contains(NewRow.Name))
		{
			AddWholeRow(NewRow, FAssetNativeDataChange::EState::Added);
		}
	}

	return Changes;
}
