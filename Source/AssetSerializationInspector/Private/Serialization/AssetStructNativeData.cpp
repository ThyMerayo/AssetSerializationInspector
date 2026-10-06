// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetStructNativeData.h"

#include "UObject/Class.h"
#include "UObject/Package.h"

#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetNativeReader.h"
#include "Serialization/AssetSchemaReflection.h"

namespace
{
	struct FFlagName
	{
		uint64 Mask;
		const TCHAR* Name;
	};

	// Names of the flags of EPropertyFlags, FUNC_ and CLASS_ (ObjectMacros.h, Script.h), by the bit they set.
	const FFlagName PropertyFlagNames[] = {
		{ 0x1, TEXT("Edit") },
		{ 0x2, TEXT("ConstParm") },
		{ 0x4, TEXT("BlueprintVisible") },
		{ 0x8, TEXT("ExportObject") },
		{ 0x10, TEXT("BlueprintReadOnly") },
		{ 0x20, TEXT("Net") },
		{ 0x40, TEXT("EditFixedSize") },
		{ 0x80, TEXT("Parm") },
		{ 0x100, TEXT("OutParm") },
		{ 0x200, TEXT("ZeroConstructor") },
		{ 0x400, TEXT("ReturnParm") },
		{ 0x800, TEXT("DisableEditOnTemplate") },
		{ 0x1000, TEXT("NonNullable") },
		{ 0x2000, TEXT("Transient") },
		{ 0x4000, TEXT("Config") },
		{ 0x8000, TEXT("RequiredParm") },
		{ 0x10000, TEXT("DisableEditOnInstance") },
		{ 0x20000, TEXT("EditConst") },
		{ 0x40000, TEXT("GlobalConfig") },
		{ 0x80000, TEXT("InstancedReference") },
		{ 0x100000, TEXT("ExperimentalExternalObjects") },
		{ 0x200000, TEXT("DuplicateTransient") },
		{ 0x1000000, TEXT("SaveGame") },
		{ 0x2000000, TEXT("NoClear") },
		{ 0x4000000, TEXT("Virtual") },
		{ 0x8000000, TEXT("ReferenceParm") },
		{ 0x10000000, TEXT("BlueprintAssignable") },
		{ 0x20000000, TEXT("Deprecated") },
		{ 0x40000000, TEXT("IsPlainOldData") },
		{ 0x80000000, TEXT("RepSkip") },
		{ 0x100000000, TEXT("RepNotify") },
		{ 0x200000000, TEXT("Interp") },
		{ 0x400000000, TEXT("NonTransactional") },
		{ 0x800000000, TEXT("EditorOnly") },
		{ 0x1000000000, TEXT("NoDestructor") },
		{ 0x4000000000, TEXT("AutoWeak") },
		{ 0x8000000000, TEXT("ContainsInstancedReference") },
		{ 0x10000000000, TEXT("AssetRegistrySearchable") },
		{ 0x20000000000, TEXT("SimpleDisplay") },
		{ 0x40000000000, TEXT("AdvancedDisplay") },
		{ 0x80000000000, TEXT("Protected") },
		{ 0x100000000000, TEXT("BlueprintCallable") },
		{ 0x200000000000, TEXT("BlueprintAuthorityOnly") },
		{ 0x400000000000, TEXT("TextExportTransient") },
		{ 0x800000000000, TEXT("NonPIEDuplicateTransient") },
		{ 0x1000000000000, TEXT("ExposeOnSpawn") },
		{ 0x2000000000000, TEXT("PersistentInstance") },
		{ 0x4000000000000, TEXT("UObjectWrapper") },
		{ 0x8000000000000, TEXT("HasGetValueTypeHash") },
		{ 0x10000000000000, TEXT("NativeAccessSpecifierPublic") },
		{ 0x20000000000000, TEXT("NativeAccessSpecifierProtected") },
		{ 0x40000000000000, TEXT("NativeAccessSpecifierPrivate") },
		{ 0x80000000000000, TEXT("SkipSerialization") },
		{ 0x100000000000000, TEXT("TObjectPtr") },
		{ 0x200000000000000, TEXT("ExperimentalOverridableLogic") },
		{ 0x400000000000000, TEXT("ExperimentalAlwaysOverriden") },
		{ 0x800000000000000, TEXT("ExperimentalNeverOverriden") },
		{ 0x1000000000000000, TEXT("AllowSelfReference") },
		{ 0x2000000000000000, TEXT("ForcePostConstructLink") },
	};

	const FFlagName FunctionFlagNames[] = {
		{ 0x1, TEXT("Final") },
		{ 0x2, TEXT("RequiredAPI") },
		{ 0x4, TEXT("BlueprintAuthorityOnly") },
		{ 0x8, TEXT("BlueprintCosmetic") },
		{ 0x40, TEXT("Net") },
		{ 0x80, TEXT("NetReliable") },
		{ 0x100, TEXT("NetRequest") },
		{ 0x200, TEXT("Exec") },
		{ 0x400, TEXT("Native") },
		{ 0x800, TEXT("Event") },
		{ 0x1000, TEXT("NetResponse") },
		{ 0x2000, TEXT("Static") },
		{ 0x4000, TEXT("NetMulticast") },
		{ 0x8000, TEXT("UbergraphFunction") },
		{ 0x10000, TEXT("MulticastDelegate") },
		{ 0x20000, TEXT("Public") },
		{ 0x40000, TEXT("Private") },
		{ 0x80000, TEXT("Protected") },
		{ 0x100000, TEXT("Delegate") },
		{ 0x200000, TEXT("NetServer") },
		{ 0x400000, TEXT("HasOutParms") },
		{ 0x800000, TEXT("HasDefaults") },
		{ 0x1000000, TEXT("NetClient") },
		{ 0x2000000, TEXT("DLLImport") },
		{ 0x4000000, TEXT("BlueprintCallable") },
		{ 0x8000000, TEXT("BlueprintEvent") },
		{ 0x10000000, TEXT("BlueprintPure") },
		{ 0x20000000, TEXT("EditorOnly") },
		{ 0x40000000, TEXT("Const") },
		{ 0x80000000, TEXT("NetValidate") },
	};

	const FFlagName ClassFlagNames[] = {
		{ 0x1, TEXT("Abstract") },
		{ 0x2, TEXT("DefaultConfig") },
		{ 0x4, TEXT("Config") },
		{ 0x8, TEXT("Transient") },
		{ 0x10, TEXT("Optional") },
		{ 0x20, TEXT("MatchedSerializers") },
		{ 0x40, TEXT("ProjectUserConfig") },
		{ 0x80, TEXT("Native") },
		{ 0x100, TEXT("Partial") },
		{ 0x200, TEXT("NotPlaceable") },
		{ 0x400, TEXT("PerObjectConfig") },
		{ 0x800, TEXT("ReplicationDataIsSetUp") },
		{ 0x1000, TEXT("EditInlineNew") },
		{ 0x2000, TEXT("CollapseCategories") },
		{ 0x4000, TEXT("Interface") },
		{ 0x8000, TEXT("PerPlatformConfig") },
		{ 0x10000, TEXT("Const") },
		{ 0x20000, TEXT("NeedsDeferredDependencyLoading") },
		{ 0x40000, TEXT("CompiledFromBlueprint") },
		{ 0x80000, TEXT("MinimalAPI") },
		{ 0x100000, TEXT("RequiredAPI") },
		{ 0x200000, TEXT("DefaultToInstanced") },
		{ 0x400000, TEXT("TokenStreamAssembled") },
		{ 0x800000, TEXT("HasInstancedReference") },
		{ 0x1000000, TEXT("Hidden") },
		{ 0x2000000, TEXT("Deprecated") },
		{ 0x4000000, TEXT("HideDropDown") },
		{ 0x8000000, TEXT("GlobalUserConfig") },
		{ 0x10000000, TEXT("Intrinsic") },
		{ 0x20000000, TEXT("Constructed") },
		{ 0x40000000, TEXT("ConfigDoNotCheckDefaults") },
		{ 0x80000000, TEXT("NewerVersionExists") },
	};

	template <int32 Count> FString DescribeFlags(const uint64 Flags, const FFlagName (&Names)[Count])
	{
		TArray<FString> Parts;
		uint64 Remaining = Flags;
		for (const FFlagName& Entry : Names)
		{
			if ((Flags & Entry.Mask) != 0)
			{
				Parts.Add(Entry.Name);
				Remaining &= ~Entry.Mask;
			}
		}

		if (Remaining != 0)
		{
			Parts.Add(FString::Printf(TEXT("0x%llX"), Remaining));
		}

		return Parts.IsEmpty() ? FString(TEXT("none")) : FString::Join(Parts, TEXT(", "));
	}

	constexpr int32 MaximumFieldDepth = 16;

	void ReadField(FNativeReader& Reader, const FAssetPackageDocument& Document, const FString& TypeName, FAssetFieldDefinition& Out, int32 Depth);

	/** An optional nested field: its type name, then the field when there is one (SerializeSingleField). */
	FString ReadNestedField(FNativeReader& Reader, const FAssetPackageDocument& Document, const int32 Depth)
	{
		const FString TypeName = Reader.ReadName();
		if (!Reader.Ok() || TypeName == TEXT("None"))
		{
			return TEXT("None");
		}

		FAssetFieldDefinition Inner;
		ReadField(Reader, Document, TypeName, Inner, Depth + 1);
		return Inner.Type;
	}

	/** FField::Serialize, FProperty::Serialize and the part of the type, for a field whose type name was just read. */
	void ReadField(FNativeReader& Reader, const FAssetPackageDocument& Document, const FString& TypeName, FAssetFieldDefinition& Out, const int32 Depth)
	{
		if (Depth > MaximumFieldDepth)
		{
			Reader.Fail(TEXT("Properties are nested too deeply"));
			return;
		}

		Out.Offset = Reader.Tell();
		Out.Name = Reader.ReadName();

		const uint32 PackageFlags = Document.PackageSummary.GetPackageFlags();
		if ((PackageFlags & PKG_FilterEditorOnly) == 0)
		{
			Reader.Read<uint32>(); // the field's flags, which are not used
		}

		// Editor packages keep the property's metadata; cooked ones do not.
		if ((PackageFlags & PKG_Cooked) == 0 && Reader.ReadBool())
		{
			const int32 Count = Reader.Read<int32>();
			for (int32 Index = 0; Index < Count && Reader.Ok(); ++Index)
			{
				const FString Key = Reader.ReadName();
				const FString Value = Reader.ReadString();
				Out.MetaData.Emplace(Key, Value);
			}
		}

		Out.ArrayDim = Reader.Read<int32>();
		Out.ElementSize = Reader.Read<int32>();
		Out.PropertyFlags = Reader.Read<uint64>();
		Reader.Read<uint16>(); // the replication index, which is rebuilt when needed
		Out.RepNotifyFunction = Reader.ReadName();
		Reader.Read<uint8>(); // the Blueprint replication condition

		Out.Type = TypeName;

		const auto Refer = [&Out](const TCHAR* Reference) { Out.Type += FString::Printf(TEXT(" (%s)"), Reference); };

		if (TypeName == TEXT("BoolProperty"))
		{
			Reader.Skip(6); // size, byte offset, byte mask, field mask, element size and the native bool flag
		}
		else if (TypeName == TEXT("ByteProperty"))
		{
			const FString Enum = Reader.ReadObject();
			if (Enum != TEXT("None"))
			{
				Refer(*Enum);
			}
		}
		else if (TypeName == TEXT("EnumProperty"))
		{
			const FString Enum = Reader.ReadObject();
			const FString Underlying = ReadNestedField(Reader, Document, Depth);
			Out.Type += FString::Printf(TEXT(" (%s, %s)"), *Enum, *Underlying);
		}
		else if (TypeName == TEXT("ClassProperty") || TypeName == TEXT("ClassPtrProperty") || TypeName == TEXT("SoftClassProperty"))
		{
			const FString PropertyClass = Reader.ReadObject();
			const FString MetaClass = Reader.ReadObject();
			Refer(*FString::Printf(TEXT("%s, class of %s"), *PropertyClass, *MetaClass));
		}
		else if (TypeName == TEXT("ObjectProperty") || TypeName == TEXT("ObjectPtrProperty") || TypeName == TEXT("WeakObjectProperty") || TypeName == TEXT("LazyObjectProperty")
			|| TypeName == TEXT("SoftObjectProperty") || TypeName == TEXT("InterfaceProperty") || TypeName == TEXT("StructProperty") || TypeName == TEXT("DelegateProperty")
			|| TypeName == TEXT("MulticastInlineDelegateProperty") || TypeName == TEXT("MulticastSparseDelegateProperty"))
		{
			Refer(*Reader.ReadObject());
		}
		else if (TypeName == TEXT("ArrayProperty") || TypeName == TEXT("SetProperty") || TypeName == TEXT("OptionalProperty"))
		{
			Out.Type += FString::Printf(TEXT("<%s>"), *ReadNestedField(Reader, Document, Depth));
		}
		else if (TypeName == TEXT("MapProperty"))
		{
			const FString Key = ReadNestedField(Reader, Document, Depth);
			const FString Value = ReadNestedField(Reader, Document, Depth);
			Out.Type += FString::Printf(TEXT("<%s, %s>"), *Key, *Value);
		}
		else if (TypeName == TEXT("FieldPathProperty"))
		{
			Refer(*Reader.ReadName());
		}
		else if (TypeName == TEXT("IntProperty") || TypeName == TEXT("Int8Property") || TypeName == TEXT("Int16Property") || TypeName == TEXT("Int64Property") || TypeName == TEXT("UInt16Property")
			|| TypeName == TEXT("UInt32Property") || TypeName == TEXT("UInt64Property") || TypeName == TEXT("FloatProperty") || TypeName == TEXT("DoubleProperty") || TypeName == TEXT("StrProperty")
			|| TypeName == TEXT("NameProperty") || TypeName == TEXT("TextProperty"))
		{
			// Nothing follows the common part.
		}
		else
		{
			Reader.Fail(FString::Printf(TEXT("The property type %s is not known"), *TypeName));
		}

		Out.Size = Reader.Tell() - Out.Offset;
	}
} // namespace

FString FAssetFieldDefinition::Describe() const
{
	FString Text = Type;
	if (ArrayDim > 1)
	{
		Text += FString::Printf(TEXT("[%d]"), ArrayDim);
	}

	Text += FString::Printf(TEXT(", flags: %s"), *AssetStructNativeData::DescribePropertyFlags(PropertyFlags));

	if (!RepNotifyFunction.IsEmpty() && RepNotifyFunction != TEXT("None"))
	{
		Text += FString::Printf(TEXT(", on replication calls %s"), *RepNotifyFunction);
	}

	return Text;
}

FString FAssetStructNativeData::Summarize() const
{
	FString Text = FString::Printf(TEXT("%d properties, %d child functions"), Fields.Num(), Children.Num());

	if (BytecodeStorageSize > 0)
	{
		Text += FString::Printf(TEXT(", %d bytes of bytecode"), BytecodeStorageSize);
	}

	if (bIsClass)
	{
		Text += FString::Printf(TEXT(", %d functions in the function map, %d interfaces"), FunctionMap.Num(), Interfaces.Num());
	}

	return Text;
}

FString AssetStructNativeData::DescribePropertyFlags(const uint64 Flags)
{
	return DescribeFlags(Flags, PropertyFlagNames);
}

FString AssetStructNativeData::DescribeFunctionFlags(const uint32 Flags)
{
	return DescribeFlags(Flags, FunctionFlagNames);
}

FString AssetStructNativeData::DescribeClassFlags(const uint32 Flags)
{
	return DescribeFlags(Flags, ClassFlagNames);
}

namespace
{
	FString ShortName(const FString& Path)
	{
		int32 Index = INDEX_NONE;
		return Path.FindLastChar(TEXT('.'), Index) ? Path.RightChop(Index + 1) : Path;
	}

	using EChange = FAssetNativeDataChange::EState;

	void AddChange(TArray<FAssetNativeDataChange>& Out, const FString& Key, const FString& Title, const EChange State, const FString& OldValue, const FString& NewValue)
	{
		FAssetNativeDataChange& Change = Out.AddDefaulted_GetRef();
		Change.Key = Key;
		Change.Title = Title;
		Change.State = State;
		Change.OldValue = OldValue;
		Change.NewValue = NewValue;
	}

	/** A value that is one text on each side: changed when they differ. */
	void CompareText(TArray<FAssetNativeDataChange>& Out, const FString& Key, const FString& Title, const FString& OldValue, const FString& NewValue)
	{
		if (!OldValue.Equals(NewValue, ESearchCase::CaseSensitive))
		{
			AddChange(Out, Key, Title, EChange::Modified, OldValue, NewValue);
		}
	}

	/** Two lists of texts compared as sets: what is only in one of them was added or removed. */
	void CompareSets(TArray<FAssetNativeDataChange>& Out, const FString& Prefix, const FString& Noun, const TArray<FString>& OldItems, const TArray<FString>& NewItems)
	{
		for (const FString& Item : NewItems)
		{
			if (!OldItems.Contains(Item))
			{
				AddChange(Out, Prefix + TEXT("/") + ShortName(Item), FString::Printf(TEXT("%s %s"), *Noun, *ShortName(Item)), EChange::Added, FString(), Item);
			}
		}

		for (const FString& Item : OldItems)
		{
			if (!NewItems.Contains(Item))
			{
				AddChange(Out, Prefix + TEXT("/") + ShortName(Item), FString::Printf(TEXT("%s %s"), *Noun, *ShortName(Item)), EChange::Removed, Item, FString());
			}
		}
	}
} // namespace

TArray<FAssetNativeDataChange> AssetStructNativeData::Compare(
	const FAssetPackageDocument& OldDocument, const FAssetStructNativeData& Old, const FAssetPackageDocument& NewDocument, const FAssetStructNativeData& New)
{
	TArray<FAssetNativeDataChange> Changes;

	CompareText(Changes, TEXT("ObjectGuid"), TEXT("Object GUID"), Old.ObjectGuid, New.ObjectGuid);
	CompareText(Changes, TEXT("SuperStruct"), New.bIsClass ? TEXT("Parent class") : TEXT("Parent function"), Old.SuperStruct, New.SuperStruct);

	// The properties the class declares (its variables) or the function declares (its parameters and local variables), matched by name.
	const TCHAR* FieldNoun = New.bIsClass ? TEXT("Variable") : TEXT("Parameter or local variable");
	TMap<FString, const FAssetFieldDefinition*> OldFields;
	for (const FAssetFieldDefinition& Field : Old.Fields)
	{
		OldFields.Add(Field.Name, &Field);
	}

	TSet<FString> NewNames;
	for (const FAssetFieldDefinition& Field : New.Fields)
	{
		NewNames.Add(Field.Name);
		const FAssetFieldDefinition* const* Found = OldFields.Find(Field.Name);
		const FString Key = FString::Printf(TEXT("Property/%s"), *Field.Name);
		const FString Title = FString::Printf(TEXT("%s %s"), FieldNoun, *Field.Name);

		if (Found == nullptr)
		{
			AddChange(Changes, Key, Title, EChange::Added, FString(), Field.Describe());
		}
		else if ((*Found)->Describe() != Field.Describe() || (*Found)->ElementSize != Field.ElementSize || (*Found)->MetaData != Field.MetaData)
		{
			FString OldText = (*Found)->Describe();
			FString NewText = Field.Describe();

			// The size in memory and the metadata change without showing in the description.
			if (OldText == NewText && (*Found)->ElementSize != Field.ElementSize)
			{
				OldText += FString::Printf(TEXT(", %d bytes"), (*Found)->ElementSize);
				NewText += FString::Printf(TEXT(", %d bytes"), Field.ElementSize);
			}
			else if (OldText == NewText)
			{
				OldText += FString::Printf(TEXT(", %d metadata entries"), (*Found)->MetaData.Num());
				NewText += FString::Printf(TEXT(", %d metadata entries"), Field.MetaData.Num());
			}

			AddChange(Changes, Key, Title, EChange::Modified, OldText, NewText);
		}
	}

	for (const FAssetFieldDefinition& Field : Old.Fields)
	{
		if (!NewNames.Contains(Field.Name))
		{
			AddChange(Changes, FString::Printf(TEXT("Property/%s"), *Field.Name), FString::Printf(TEXT("%s %s"), FieldNoun, *Field.Name), EChange::Removed, Field.Describe(), FString());
		}
	}

	CompareSets(Changes, TEXT("Function"), TEXT("Function"), Old.Children, New.Children);

	if (New.bIsClass)
	{
		// The function map: name to function.
		TMap<FString, FString> OldMap;
		for (const TPair<FString, FString>& Entry : Old.FunctionMap)
		{
			OldMap.Add(Entry.Key, Entry.Value);
		}

		TSet<FString> NewMapNames;
		for (const TPair<FString, FString>& Entry : New.FunctionMap)
		{
			NewMapNames.Add(Entry.Key);
			const FString* OldTarget = OldMap.Find(Entry.Key);
			const FString Key = FString::Printf(TEXT("FunctionMap/%s"), *Entry.Key);
			const FString Title = FString::Printf(TEXT("Function map entry %s"), *Entry.Key);
			if (OldTarget == nullptr)
			{
				AddChange(Changes, Key, Title, EChange::Added, FString(), Entry.Value);
			}
			else if (*OldTarget != Entry.Value)
			{
				AddChange(Changes, Key, Title, EChange::Modified, *OldTarget, Entry.Value);
			}
		}

		for (const TPair<FString, FString>& Entry : Old.FunctionMap)
		{
			if (!NewMapNames.Contains(Entry.Key))
			{
				AddChange(Changes, FString::Printf(TEXT("FunctionMap/%s"), *Entry.Key), FString::Printf(TEXT("Function map entry %s"), *Entry.Key), EChange::Removed, Entry.Value, FString());
			}
		}

		CompareSets(Changes, TEXT("Interface"), TEXT("Interface"), Old.Interfaces, New.Interfaces);

		if (Old.ClassFlags != New.ClassFlags)
		{
			AddChange(Changes, TEXT("ClassFlags"), TEXT("Class flags"), EChange::Modified, DescribeClassFlags(Old.ClassFlags), DescribeClassFlags(New.ClassFlags));
		}

		CompareText(Changes, TEXT("ClassWithin"), TEXT("Class within"), Old.ClassWithin, New.ClassWithin);
		CompareText(Changes, TEXT("ClassConfigName"), TEXT("Config name"), Old.ClassConfigName, New.ClassConfigName);
		CompareText(Changes, TEXT("GeneratedBy"), TEXT("Generated by"), Old.GeneratedBy, New.GeneratedBy);
		CompareText(Changes, TEXT("DefaultObject"), TEXT("Default object"), Old.DefaultObject, New.DefaultObject);
	}
	else
	{
		if (Old.FunctionFlags != New.FunctionFlags)
		{
			AddChange(Changes, TEXT("FunctionFlags"), TEXT("Function flags"), EChange::Modified, DescribeFunctionFlags(Old.FunctionFlags), DescribeFunctionFlags(New.FunctionFlags));
		}

		CompareText(Changes, TEXT("EventGraphFunction"), TEXT("Event graph function"), Old.EventGraphFunction, New.EventGraphFunction);
		if (Old.EventGraphCallOffset != New.EventGraphCallOffset)
		{
			AddChange(Changes, TEXT("EventGraphCallOffset"), TEXT("Event graph call offset"), EChange::Modified, LexToString(Old.EventGraphCallOffset), LexToString(New.EventGraphCallOffset));
		}
	}

	// The bytecode: the statements that differ when both sides were read; otherwise its size, or how many of its bytes differ. When
	// both were read and no statement differs, whatever else differs in the bytes is how the package numbers what the code refers to.
	if (Old.Bytecode.bComplete && New.Bytecode.bComplete)
	{
		for (const AssetBytecode::FStatementChange& Hunk : AssetBytecode::Compare(Old.Bytecode, New.Bytecode))
		{
			const EChange State = Hunk.Removed.IsEmpty() ? EChange::Added : (Hunk.Added.IsEmpty() ? EChange::Removed : EChange::Modified);
			AddChange(Changes, FString::Printf(TEXT("Bytecode/%d:%d"), Hunk.OldStart, Hunk.NewStart),
				FString::Printf(TEXT("Bytecode, statement %d"), (Hunk.Added.IsEmpty() ? Hunk.OldStart : Hunk.NewStart) + 1), State, FString::Join(Hunk.Removed, TEXT("\n")),
				FString::Join(Hunk.Added, TEXT("\n")));
		}
	}
	else if (Old.BytecodeStorageSize != New.BytecodeStorageSize || Old.BytecodeSize != New.BytecodeSize)
	{
		AddChange(
			Changes, TEXT("Bytecode"), TEXT("Bytecode"), EChange::Modified, FString::Printf(TEXT("%d bytes"), Old.BytecodeStorageSize), FString::Printf(TEXT("%d bytes"), New.BytecodeStorageSize));
	}
	else if (Old.BytecodeStorageSize > 0 && OldDocument.IsValidRange(Old.BytecodeOffset, Old.BytecodeStorageSize) && NewDocument.IsValidRange(New.BytecodeOffset, New.BytecodeStorageSize))
	{
		int64 Different = 0;
		for (int64 Index = 0; Index < Old.BytecodeStorageSize; ++Index)
		{
			Different += OldDocument.FileData[Old.BytecodeOffset + Index] != NewDocument.FileData[New.BytecodeOffset + Index] ? 1 : 0;
		}

		if (Different > 0)
		{
			AddChange(Changes, TEXT("Bytecode"), TEXT("Bytecode"), EChange::Modified, FString::Printf(TEXT("%d bytes"), Old.BytecodeStorageSize),
				FString::Printf(TEXT("%d bytes, %lld of them different"), New.BytecodeStorageSize, Different));
		}
	}

	return Changes;
}

bool AssetStructNativeData::Decode(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, const int64 NativeOffset, const int64 NativeSize, FAssetStructNativeData& Out)
{
	const UClass* NativeClass = AssetSchemaReflection::FindNativeClass(Document, Export.Index);
	if (NativeClass == nullptr || NativeSize <= 0 || !Document.IsValidRange(NativeOffset, NativeSize))
	{
		return false;
	}

	const bool bIsClass = NativeClass->IsChildOf(UClass::StaticClass());
	const bool bIsFunction = NativeClass->IsChildOf(UFunction::StaticClass());
	if (!bIsClass && !bIsFunction)
	{
		return false;
	}

	Out = FAssetStructNativeData();
	Out.bIsClass = bIsClass;
	Out.bIsFunction = bIsFunction;
	Out.Offset = NativeOffset;
	Out.Size = NativeSize;

	FNativeReader Reader(Document, NativeOffset, NativeSize);

	// UObject::Serialize ends with the object's GUID, when it has one.
	if (Reader.ReadBool())
	{
		FGuid Guid;
		Guid.A = Reader.Read<uint32>();
		Guid.B = Reader.Read<uint32>();
		Guid.C = Reader.Read<uint32>();
		Guid.D = Reader.Read<uint32>();
		Out.ObjectGuid = Guid.ToString(EGuidFormats::DigitsWithHyphens);
	}

	// UStruct::Serialize.
	Out.SuperStruct = Reader.ReadObject();

	const int32 ChildCount = Reader.Read<int32>();
	if (ChildCount < 0 || ChildCount > Reader.Remaining() / 4)
	{
		Reader.Fail(TEXT("The number of child functions does not fit the data"));
	}
	for (int32 Index = 0; Index < ChildCount && Reader.Ok(); ++Index)
	{
		Out.Children.Add(Reader.ReadObject());
	}

	const int32 PropertyCount = Reader.Read<int32>();
	if (PropertyCount < 0 || PropertyCount > Reader.Remaining() / 8)
	{
		Reader.Fail(TEXT("The number of properties does not fit the data"));
	}
	for (int32 Index = 0; Index < PropertyCount && Reader.Ok(); ++Index)
	{
		const FString TypeName = Reader.ReadName();
		if (Reader.Ok())
		{
			ReadField(Reader, Document, TypeName, Out.Fields.AddDefaulted_GetRef(), 0);
		}
	}

	Out.BytecodeSize = Reader.Read<int32>();
	Out.BytecodeStorageSize = Reader.Read<int32>();
	Out.BytecodeOffset = Reader.Tell();
	if (Reader.Ok() && (Out.BytecodeStorageSize < 0 || Out.BytecodeStorageSize > Reader.Remaining()))
	{
		Reader.Fail(TEXT("The bytecode is longer than the data"));
	}
	if (Reader.Ok() && Out.BytecodeStorageSize > 0)
	{
		AssetBytecode::Disassemble(Document, Out.BytecodeOffset, Out.BytecodeStorageSize, Out.BytecodeSize, Out.Bytecode);
	}
	Reader.Skip(Out.BytecodeStorageSize);

	if (bIsClass)
	{
		const int32 FunctionCount = Reader.Read<int32>();
		if (FunctionCount < 0 || FunctionCount > Reader.Remaining() / 12)
		{
			Reader.Fail(TEXT("The size of the function map does not fit the data"));
		}
		for (int32 Index = 0; Index < FunctionCount && Reader.Ok(); ++Index)
		{
			const FString Name = Reader.ReadName();
			Out.FunctionMap.Emplace(Name, Reader.ReadObject());
		}

		Out.ClassFlags = Reader.Read<uint32>();
		Out.ClassWithin = Reader.ReadObject();
		Out.ClassConfigName = Reader.ReadName();
		Out.GeneratedBy = Reader.ReadObject();

		const int32 InterfaceCount = Reader.Read<int32>();
		if (InterfaceCount < 0 || InterfaceCount > Reader.Remaining() / 12)
		{
			Reader.Fail(TEXT("The number of interfaces does not fit the data"));
		}
		for (int32 Index = 0; Index < InterfaceCount && Reader.Ok(); ++Index)
		{
			const FString Interface = Reader.ReadObject();
			const int32 PointerOffset = Reader.Read<int32>();
			const bool bImplementedByK2 = Reader.ReadBool();
			Out.Interfaces.Add(FString::Printf(TEXT("%s (offset %d%s)"), *Interface, PointerOffset, bImplementedByK2 ? TEXT(", implemented in a Blueprint") : TEXT("")));
		}

		Out.bDeprecatedForceScriptOrder = Reader.ReadBool();
		Reader.ReadName(); // unused
		Out.bCooked = Reader.ReadBool();
		Out.DefaultObject = Reader.ReadObject();
	}
	else
	{
		Out.FunctionFlags = Reader.Read<uint32>();
		if ((Out.FunctionFlags & 0x40) != 0) // FUNC_Net
		{
			Reader.Read<int16>();
		}

		Out.EventGraphFunction = Reader.ReadObject();
		Out.EventGraphCallOffset = Reader.Read<int32>();
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
