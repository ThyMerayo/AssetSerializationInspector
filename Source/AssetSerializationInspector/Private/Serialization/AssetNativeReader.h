// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/EditorObjectVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"
#include "UObject/ObjectVersion.h"

#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackagePayloadReader.h"
#include "Serialization/AssetPropertyValueDecoder.h"
#include "Serialization/AssetSerializedPropertyTag.h"

/** Reads the values of the stream in order and remembers the first thing that went wrong, so that a caller reads on without checking each value. */
class FNativeReader
{
public:
	FNativeReader(const FAssetPackageDocument& InDocument, const int64 Offset, const int64 Size) : Document(InDocument), Reader(InDocument, Offset, Size), Begin(Offset), End(Offset + Size) {}

	bool Ok() const { return Error.IsEmpty(); }
	const FString& GetError() const { return Error; }
	int64 Tell() { return Reader.Tell(); }
	int64 Remaining() { return End - Reader.Tell(); }
	int32 CustomVer(const FGuid& Key) const { return Reader.CustomVer(Key); }
	bool UEVerAtLeast(const EUnrealEngineObjectUE5Version Version) const { return Reader.UEVer() >= Version; }
	bool UEVerBelow(const EUnrealEngineObjectUE4Version UE4Version) const { return Reader.UEVer() < UE4Version; }
	void Seek(const int64 Offset) { Reader.Seek(Offset); }

	void Fail(const FString& Message)
	{
		if (Error.IsEmpty())
		{
			Error = FString::Printf(TEXT("%s (at byte %lld)"), *Message, Reader.Tell() - Begin);
		}
	}

	template <typename T> T Read()
	{
		T Value{};
		if (!Ok() || !Need(sizeof(T)))
		{
			return Value;
		}

		Reader << Value;
		return Value;
	}

	FString ReadName()
	{
		if (!Ok() || !Need(8))
		{
			return FString();
		}

		FAssetPackageNameReference Reference;
		if (!Reader.ReadNameReference(Reference) || !Reference.IsValid(Document.NameMap.Num()))
		{
			Fail(TEXT("A name does not point into the name map"));
			return FString();
		}

		return Document.ResolveNameReference(Reference);
	}

	FString ReadObject()
	{
		const int32 Raw = Read<int32>();
		FString Path;
		if (Ok() && !Document.ResolvePackageIndexPath(FAssetPackageIndexReference{ Raw }, Path))
		{
			Fail(FString::Printf(TEXT("Object reference %d does not point into the import or export map"), Raw));
		}

		return Path;
	}

	FGuid ReadGuid()
	{
		FGuid Guid;
		Guid.A = Read<uint32>();
		Guid.B = Read<uint32>();
		Guid.C = Read<uint32>();
		Guid.D = Read<uint32>();
		return Guid;
	}

	/**
	 * An FText (FText::SerializeText), read by the decoder that reads the text properties of a package, so every kind of history it
	 * knows is read here too (plain, formatted, string table, numbers and dates). Returns the text; a kind it does not read fails the
	 * reading.
	 */
	FString ReadText()
	{
		FAssetSerializedPropertyType Type;
		Type.Name = TEXT("TextProperty");

		const int64 Start = Reader.Tell();
		FAssetDecodedPropertyValue Value;
		if (!Ok())
		{
			return FString();
		}

		if (!FAssetPropertyValueDecoder::DecodeTypeAt(Document, Type, Start, Remaining(), Value) || !Value.IsSuccess())
		{
			Fail(Value.Error.IsEmpty() ? FString(TEXT("A text is not read")) : Value.Error);
			return FString();
		}

		Reader.Seek(Start + Value.Size);
		return Value.Value;
	}

	/** A bool of a binary archive is 32 bits. */
	bool ReadBool() { return Read<uint32>() != 0; }

	FString ReadString()
	{
		const int32 Length = Read<int32>();
		if (!Ok() || Length == 0)
		{
			return FString();
		}

		const int64 Bytes = Length > 0 ? Length : -static_cast<int64>(Length) * 2;
		if (!Need(Bytes) || Bytes > 1 << 20)
		{
			Fail(TEXT("A string is longer than the data"));
			return FString();
		}

		TArray<uint8> Data;
		Data.SetNumUninitialized(static_cast<int32>(Bytes));
		Reader.Serialize(Data.GetData(), Bytes);

		if (Length > 0)
		{
			return FString(Length - 1, reinterpret_cast<const ANSICHAR*>(Data.GetData()));
		}

		return FString(-Length - 1, reinterpret_cast<const UTF16CHAR*>(Data.GetData()));
	}

	void Skip(const int64 Bytes)
	{
		if (Ok() && Need(Bytes))
		{
			Reader.Seek(Reader.Tell() + Bytes);
		}
	}

private:
	bool Need(const int64 Bytes)
	{
		if (Bytes < 0 || Reader.Tell() + Bytes > End)
		{
			Fail(TEXT("The data ends before the structure does"));
			return false;
		}

		return true;
	}

	const FAssetPackageDocument& Document;
	FAssetPackagePayloadReader Reader;
	int64 Begin;
	int64 End;
	FString Error;
};
