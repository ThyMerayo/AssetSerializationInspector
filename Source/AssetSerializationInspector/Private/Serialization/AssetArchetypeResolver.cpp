// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetArchetypeResolver.h"

#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Serialization/AssetContainerFinalValue.h"
#include "Trace/AssetPackageFieldDecoder.h"
#include "Trace/AssetSerializationTrace.h"

namespace
{
	constexpr int32 MaximumArchetypeDepth = 32;

	/** Part of the message FindOrLoadPackage gives when an archetype is a native class default object. */
	const TCHAR* const NativePackageMessage = TEXT("is a native package");

	/** Names of an export and its outers up to the package, joined with '.'. Empty when the chain leaves the package. */
	FString BuildExportRelativePath(const FAssetPackageDocument& Document, int32 ExportIndex)
	{
		FString Path;

		for (int32 Guard = 0; Guard < MaximumArchetypeDepth; ++Guard)
		{
			if (!Document.ExportMap.IsValidIndex(ExportIndex))
			{
				return FString();
			}

			const FAssetPackageExportEntry& Export = Document.ExportMap[ExportIndex];
			const FString Name = Document.ResolveNameReference(Export.ObjectName);
			Path = Path.IsEmpty() ? Name : Name + TEXT(".") + Path;

			switch (Export.OuterIndex.GetKind())
			{
				case EAssetPackageIndexKind::Null:
					return Path;

				case EAssetPackageIndexKind::Export:
					ExportIndex = Export.OuterIndex.GetArrayIndex();
					break;

				default:
					return FString();
			}
		}

		return FString();
	}

	const FAssetSerializationTraceNode* FindTopLevelPropertyNode(const FAssetSerializationTrace& Trace, const FString& PropertyName, const int32 ArrayIndex)
	{
		if (!Trace.Root.IsValid())
		{
			return nullptr;
		}

		for (const TSharedPtr<FAssetSerializationTraceNode>& Child : Trace.Root->Children)
		{
			if (Child.IsValid() && Child->Kind == EAssetSerializationTraceKind::Property && Child->ArrayIndex == ArrayIndex && Child->Name == PropertyName)
			{
				return Child.Get();
			}
		}

		return nullptr;
	}

	FString GetClassShortName(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export)
	{
		FString Path;
		if (!Document.ResolvePackageIndexPath(Export.ClassIndex, Path))
		{
			return FString();
		}

		int32 Dot = INDEX_NONE;
		return Path.FindLastChar(TEXT('.'), Dot) ? Path.RightChop(Dot + 1) : Path;
	}

	/**
	 * Looks for a variable called PropertyName in the NewVariables of a Blueprint stored in the same package, when the export is
	 * an object of a class generated in that package. Returns the variable's default value text (empty when it has none).
	 */
	bool FindBlueprintVariable(const FAssetPackageDocument& Document, const FAssetPackageTraceCollection& Traces, const int32 ExportIndex, const FString& PropertyName, FString& OutDefaultText)
	{
		if (!Document.ExportMap.IsValidIndex(ExportIndex) || Document.ExportMap[ExportIndex].ClassIndex.GetKind() != EAssetPackageIndexKind::Export)
		{
			return false;
		}

		for (const FAssetPackageExportEntry& Candidate : Document.ExportMap)
		{
			if (GetClassShortName(Document, Candidate) != TEXT("Blueprint"))
			{
				continue;
			}

			const FAssetSerializationTrace* Trace = Traces.FindExportTrace(Candidate.Index);
			const FAssetSerializationTraceNode* Node = Trace != nullptr ? FindTopLevelPropertyNode(*Trace, TEXT("NewVariables"), 0) : nullptr;
			if (Node == nullptr)
			{
				continue;
			}

			const FAssetDecodedPropertyValue Variables = FAssetPropertyValueDecoder::Decode(Document, *Node, Candidate.SerialOffset);
			if (!Variables.IsSuccess())
			{
				continue;
			}

			for (const FAssetDecodedPropertyValue& Variable : Variables.Children)
			{
				const FAssetDecodedPropertyValue* Name = nullptr;
				const FAssetDecodedPropertyValue* DefaultValue = nullptr;
				for (const FAssetDecodedPropertyValue& Field : Variable.Children)
				{
					if (Field.Name == TEXT("VarName"))
					{
						Name = &Field;
					}
					else if (Field.Name == TEXT("DefaultValue"))
					{
						DefaultValue = &Field;
					}
				}

				if (Name != nullptr && Name->Value == PropertyName)
				{
					OutDefaultText = DefaultValue != nullptr ? DefaultValue->Value : FString();
					return true;
				}
			}
		}

		return false;
	}

	/**
	 * Follows an export's class (through the Blueprint classes generated in the package) to the native class it derives from and
	 * returns the class when the running editor has it loaded.
	 */
	UClass* FindNativeClass(const FAssetPackageDocument& Document, const int32 ExportIndex)
	{
		if (!Document.ExportMap.IsValidIndex(ExportIndex))
		{
			return nullptr;
		}

		FAssetPackageIndexReference Index = Document.ExportMap[ExportIndex].ClassIndex;

		for (int32 Guard = 0; Guard < MaximumArchetypeDepth; ++Guard)
		{
			if (Index.GetKind() == EAssetPackageIndexKind::Export)
			{
				if (!Document.ExportMap.IsValidIndex(Index.GetArrayIndex()))
				{
					return nullptr;
				}

				Index = Document.ExportMap[Index.GetArrayIndex()].SuperIndex;
				continue;
			}

			FString Path;
			if (Index.GetKind() != EAssetPackageIndexKind::Import || !Document.ResolvePackageIndexPath(Index, Path) || !Path.StartsWith(TEXT("/Script/")))
			{
				return nullptr;
			}

			return FindObject<UClass>(nullptr, *Path);
		}

		return nullptr;
	}

	/** Exports a property's value on the class default object of the native class an export derives from. */
	bool ReflectNativeDefault(const FAssetPackageDocument& Document, const int32 ExportIndex, const FString& PropertyName, const int32 ArrayIndex, FString& OutText, FString& OutClassName)
	{
		UClass* Class = FindNativeClass(Document, ExportIndex);
		if (Class == nullptr)
		{
			return false;
		}

		const FProperty* Property = FindFProperty<FProperty>(Class, *PropertyName);
		if (Property == nullptr || ArrayIndex < 0 || ArrayIndex >= Property->ArrayDim)
		{
			return false;
		}

		const UObject* DefaultObject = Class->GetDefaultObject();
		if (DefaultObject == nullptr)
		{
			return false;
		}

		const void* Value = Property->ContainerPtrToValuePtr<void>(DefaultObject, ArrayIndex);
		Property->ExportText_Direct(OutText, Value, Value, nullptr, PPF_None);
		OutClassName = Property->GetOwnerClass() != nullptr ? Property->GetOwnerClass()->GetName() : Class->GetName();
		return true;
	}
} // namespace

struct FAssetArchetypeResolver::FLoadedPackage
{
	TSharedPtr<FAssetPackageDocument> Document;
	TSharedPtr<FAssetPackageTraceCollection> Traces;
};

FAssetArchetypeResolver::FAssetArchetypeResolver(const FAssetPackageDocument& InRootDocument, const FAssetPackageTraceCollection& InRootTraces) : RootDocument(InRootDocument), RootTraces(InRootTraces)
{
}

FAssetArchetypeResolver::~FAssetArchetypeResolver() = default;

const FAssetArchetypeResolver::FLoadedPackage* FAssetArchetypeResolver::FindOrLoadPackage(const FString& PackageName, FString& OutMessage)
{
	if (const TUniquePtr<FLoadedPackage>* Cached = LoadedPackages.Find(PackageName))
	{
		return Cached->Get();
	}

	// Native classes live in /Script/ modules and have no package file. Their defaults come from C++ constructors.
	if (PackageName.StartsWith(TEXT("/Script/")))
	{
		OutMessage = FString::Printf(TEXT("'%s' %s; its defaults come from C++ and are not stored in a package file."), *PackageName, NativePackageMessage);
		return nullptr;
	}

	FString Filename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, Filename, TEXT(".uasset")) || !FPaths::FileExists(Filename))
	{
		OutMessage = FString::Printf(TEXT("The package file for '%s' could not be found on disk."), *PackageName);
		return nullptr;
	}

	FText LoadError;
	TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(Filename, LoadError);
	if (!Document.IsValid())
	{
		OutMessage = LoadError.ToString();
		return nullptr;
	}

	TUniquePtr<FLoadedPackage> Loaded = MakeUnique<FLoadedPackage>();
	Loaded->Document = Document;
	Loaded->Traces = FAssetPackageFieldDecoder::Decode(*Document);

	return LoadedPackages.Add(PackageName, MoveTemp(Loaded)).Get();
}

EAssetArchetypeValueStatus FAssetArchetypeResolver::ResolveInheritedValue(
	const int32 ExportIndex, const FString& PropertyName, const int32 ArrayIndex, FAssetArchetypeValue& OutValue, FString& OutMessage)
{
	FLocation Root;
	Root.Document = &RootDocument;
	Root.Traces = &RootTraces;
	Root.ExportIndex = ExportIndex;

	return ResolveFromArchetype(Root, PropertyName, ArrayIndex, TArray<FString>(), 0, OutValue, OutMessage);
}

bool FAssetArchetypeResolver::LocateArchetype(const FLocation& Location, FLocation& OutArchetype, FString& OutMessage)
{
	const FAssetPackageDocument& Document = *Location.Document;

	if (!Document.ExportMap.IsValidIndex(Location.ExportIndex))
	{
		OutMessage = FString::Printf(TEXT("Export %d does not exist."), Location.ExportIndex);
		return false;
	}

	const FAssetPackageIndexReference Template = Document.ExportMap[Location.ExportIndex].TemplateIndex;

	if (Template.GetKind() == EAssetPackageIndexKind::Export)
	{
		OutArchetype = Location;
		OutArchetype.ExportIndex = Template.GetArrayIndex();

		if (!Document.ExportMap.IsValidIndex(OutArchetype.ExportIndex))
		{
			OutMessage = FString::Printf(TEXT("The archetype export %d does not exist."), OutArchetype.ExportIndex);
			return false;
		}

		return true;
	}

	if (Template.GetKind() != EAssetPackageIndexKind::Import)
	{
		OutMessage = TEXT("The export has no archetype.");
		return false;
	}

	// Walk the import's outers up to its package. The names below the package identify the object inside it.
	FString RelativePath;
	FString PackageName;
	int32 ImportIndex = Template.GetArrayIndex();

	for (int32 Guard = 0; Guard < MaximumArchetypeDepth; ++Guard)
	{
		if (!Document.ImportMap.IsValidIndex(ImportIndex))
		{
			OutMessage = TEXT("The archetype import chain is malformed.");
			return false;
		}

		const FAssetPackageImportEntry& Import = Document.ImportMap[ImportIndex];
		const FString Name = Document.ResolveNameReference(Import.ObjectName);

		if (Import.OuterIndex.GetKind() == EAssetPackageIndexKind::Null)
		{
			PackageName = Name;
			break;
		}

		if (Import.OuterIndex.GetKind() != EAssetPackageIndexKind::Import)
		{
			OutMessage = TEXT("The archetype import is outered to an export, which is not supported.");
			return false;
		}

		RelativePath = RelativePath.IsEmpty() ? Name : Name + TEXT(".") + RelativePath;
		ImportIndex = Import.OuterIndex.GetArrayIndex();
	}

	if (PackageName.IsEmpty() || RelativePath.IsEmpty())
	{
		OutMessage = TEXT("The archetype import does not name an object inside a package.");
		return false;
	}

	const FLoadedPackage* Package = FindOrLoadPackage(PackageName, OutMessage);
	if (Package == nullptr)
	{
		return false;
	}

	for (const FAssetPackageExportEntry& Export : Package->Document->ExportMap)
	{
		if (BuildExportRelativePath(*Package->Document, Export.Index) == RelativePath)
		{
			OutArchetype.Document = Package->Document.Get();
			OutArchetype.Traces = Package->Traces.Get();
			OutArchetype.ExportIndex = Export.Index;
			return true;
		}
	}

	OutMessage = FString::Printf(TEXT("'%s' was not found in package '%s'."), *RelativePath, *PackageName);
	return false;
}

EAssetArchetypeValueStatus FAssetArchetypeResolver::ResolveFromArchetype(const FLocation& Location, const FString& PropertyName, const int32 ArrayIndex, const TArray<FString>& FieldPath,
	const int32 Depth, FAssetArchetypeValue& OutValue, FString& OutMessage)
{
	if (Depth >= MaximumArchetypeDepth)
	{
		OutMessage = TEXT("The archetype chain is too deep or cyclic.");
		return EAssetArchetypeValueStatus::Unavailable;
	}

	const FAssetPackageDocument& Document = *Location.Document;
	if (!Document.ExportMap.IsValidIndex(Location.ExportIndex))
	{
		OutMessage = FString::Printf(TEXT("Export %d does not exist."), Location.ExportIndex);
		return EAssetArchetypeValueStatus::Unavailable;
	}

	// An export without a template ends the chain: whatever is left comes from native defaults.
	if (Document.ExportMap[Location.ExportIndex].TemplateIndex.GetKind() == EAssetPackageIndexKind::Null)
	{
		OutMessage = TEXT("The archetype chain ends without a package that stores the property; its default comes from native code.");
		return EAssetArchetypeValueStatus::NotSerializedInChain;
	}

	FLocation Archetype;
	if (!LocateArchetype(Location, Archetype, OutMessage))
	{
		return EAssetArchetypeValueStatus::Unavailable;
	}

	return ResolveEffectiveValue(Archetype, PropertyName, ArrayIndex, FieldPath, Depth + 1, OutValue, OutMessage);
}

EAssetArchetypeValueStatus FAssetArchetypeResolver::ResolveEffectiveValue(const FLocation& Location, const FString& PropertyName, const int32 ArrayIndex, const TArray<FString>& FieldPath,
	const int32 Depth, FAssetArchetypeValue& OutValue, FString& OutMessage)
{
	const FAssetPackageDocument& Document = *Location.Document;
	const FAssetPackageExportEntry& Export = Document.ExportMap[Location.ExportIndex];

	const FAssetSerializationTrace* Trace = Location.Traces != nullptr ? Location.Traces->FindExportTrace(Location.ExportIndex) : nullptr;
	const FAssetSerializationTraceNode* Node = Trace != nullptr ? FindTopLevelPropertyNode(*Trace, PropertyName, ArrayIndex) : nullptr;

	if (Node == nullptr)
	{
		// This archetype does not override the property; keep looking further up.
		return ResolveFromArchetype(Location, PropertyName, ArrayIndex, FieldPath, Depth, OutValue, OutMessage);
	}

	FAssetDecodedPropertyValue DecodedProperty = FAssetPropertyValueDecoder::Decode(Document, *Node, Export.SerialOffset);
	if (!DecodedProperty.IsSuccess())
	{
		OutMessage = FString::Printf(TEXT("The archetype's '%s' could not be decoded: %s"), *PropertyName, *DecodedProperty.Error);
		return EAssetArchetypeValueStatus::Unavailable;
	}

	// For a field of a struct, follow the field names down. A struct that does not store the field leaves its value to the
	// struct on the archetype's own archetype, so the search continues up the chain.
	const FAssetDecodedPropertyValue* Field = &DecodedProperty;
	for (const FString& FieldName : FieldPath)
	{
		const FAssetDecodedPropertyValue* Next = nullptr;
		for (const FAssetDecodedPropertyValue& Child : Field->Children)
		{
			if (Child.Name == FieldName)
			{
				Next = &Child;
				break;
			}
		}

		if (Next == nullptr)
		{
			return ResolveFromArchetype(Location, PropertyName, ArrayIndex, FieldPath, Depth, OutValue, OutMessage);
		}

		Field = Next;
	}

	FAssetDecodedPropertyValue Decoded = *Field;
	if (!Decoded.IsSuccess())
	{
		OutMessage = FString::Printf(TEXT("The archetype's '%s' could not be decoded: %s"), *PropertyName, *Decoded.Error);
		return EAssetArchetypeValueStatus::Unavailable;
	}

	FAssetArchetypeValue Result;
	Result.Source = FString::Printf(TEXT("%s in %s"), *Document.ResolveExportPath(Location.ExportIndex), *FPaths::GetCleanFilename(Document.Filename));

	if (Decoded.Kind == EAssetDecodedValueKind::Set || Decoded.Kind == EAssetDecodedValueKind::Map)
	{
		const EAssetArchetypeValueStatus Status = ApplyContainerToArchetype(Location, PropertyName, ArrayIndex, FieldPath, Depth, Decoded, Result, OutMessage);
		if (Status != EAssetArchetypeValueStatus::Found)
		{
			return Status;
		}
	}
	else
	{
		Result.Value = MoveTemp(Decoded);
	}

	OutValue = MoveTemp(Result);
	return EAssetArchetypeValueStatus::Found;
}

EAssetArchetypeValueStatus FAssetArchetypeResolver::ApplyContainerToArchetype(const FLocation& Location, const FString& PropertyName, const int32 ArrayIndex, const TArray<FString>& FieldPath,
	const int32 Depth, const FAssetDecodedPropertyValue& Serialized, FAssetArchetypeValue& OutValue, FString& OutMessage)
{
	// The container may be a delta on top of the value its own archetype has.
	FAssetArchetypeValue Parent;
	FString ParentMessage;
	const EAssetArchetypeValueStatus ParentStatus = ResolveFromArchetype(Location, PropertyName, ArrayIndex, FieldPath, Depth, Parent, ParentMessage);

	const FAssetDecodedPropertyValue* Defaults = nullptr;
	FAssetDecodedPropertyValue AssumedEmptyDefaults;

	if (ParentStatus == EAssetArchetypeValueStatus::Found)
	{
		Defaults = &Parent.Value;
		OutValue.Confidence = Parent.Confidence;
		OutValue.Note = Parent.Note;
	}
	else if (Serialized.Kind == EAssetDecodedValueKind::Map && Serialized.ContainerMode == EAssetDecodedContainerSerializationMode::Full)
	{
		// A map stored with the replace marker is complete on its own and needs no defaults.
	}
	else if (AssetContainerFinalValue::CanAssumeEmptyDefaults(Serialized))
	{
		// Fall back on a stated assumption: containers declared in Blueprints, which is where set and map deltas usually
		// come from, default to empty. A native property with a non-empty default would need reflection.
		AssumedEmptyDefaults.Status = EAssetPropertyDecodeStatus::Success;
		AssumedEmptyDefaults.Kind = Serialized.Kind;
		Defaults = &AssumedEmptyDefaults;
		OutValue.Confidence = EAssetContainerFinalValueConfidence::Inferred;
		OutValue.Note = TEXT("Assumes the container's default is empty; a non-empty native default cannot be read from package files.");
	}

	FAssetContainerFinalValue Final;
	FString Error;
	if (!AssetContainerFinalValue::Compute(Serialized, Defaults, Final, Error))
	{
		OutMessage = ParentStatus == EAssetArchetypeValueStatus::Found ? Error : FString::Printf(TEXT("%s %s"), *Error, *ParentMessage);
		return ParentStatus == EAssetArchetypeValueStatus::Found ? EAssetArchetypeValueStatus::Unavailable : ParentStatus;
	}

	if (Final.Confidence == EAssetContainerFinalValueConfidence::Inferred)
	{
		OutValue.Confidence = EAssetContainerFinalValueConfidence::Inferred;
	}

	OutValue.Value = MoveTemp(Final.Value);
	return EAssetArchetypeValueStatus::Found;
}

bool FAssetArchetypeResolver::ResolveFinalContainerValue(
	const int32 ExportIndex, const FString& PropertyName, const int32 ArrayIndex, const FAssetDecodedPropertyValue& Serialized, FAssetArchetypeValue& OutFinal, FString& OutMessage)
{
	return ResolveFinalNestedContainerValue(ExportIndex, PropertyName, ArrayIndex, TArray<FString>(), Serialized, OutFinal, OutMessage);
}

bool FAssetArchetypeResolver::ResolveFinalNestedContainerValue(const int32 ExportIndex, const FString& PropertyName, const int32 ArrayIndex, const TArray<FString>& FieldPath,
	const FAssetDecodedPropertyValue& Serialized, FAssetArchetypeValue& OutFinal, FString& OutMessage)
{
	FLocation Root;
	Root.Document = &RootDocument;
	Root.Traces = &RootTraces;
	Root.ExportIndex = ExportIndex;

	if (!RootDocument.ExportMap.IsValidIndex(ExportIndex))
	{
		OutMessage = FString::Printf(TEXT("Export %d does not exist."), ExportIndex);
		return false;
	}

	FAssetArchetypeValue Result;
	Result.Source = FString::Printf(TEXT("%s in %s"), *RootDocument.ResolveExportPath(ExportIndex), *FPaths::GetCleanFilename(RootDocument.Filename));

	if (ApplyContainerToArchetype(Root, PropertyName, ArrayIndex, FieldPath, 0, Serialized, Result, OutMessage) != EAssetArchetypeValueStatus::Found)
	{
		return false;
	}

	OutFinal = MoveTemp(Result);
	return true;
}

FAssetOmittedPropertyDefault FAssetArchetypeResolver::DescribeOmittedProperty(const int32 ExportIndex, const FString& PropertyName, const int32 ArrayIndex)
{
	return DescribeOmittedField(ExportIndex, PropertyName, ArrayIndex, TArray<FString>());
}

FAssetOmittedPropertyDefault FAssetArchetypeResolver::DescribeOmittedField(const int32 ExportIndex, const FString& PropertyName, const int32 ArrayIndex, const TArray<FString>& FieldPath)
{
	FAssetOmittedPropertyDefault Result;
	FAssetArchetypeValue Inherited;
	FString Message;

	FLocation Root;
	Root.Document = &RootDocument;
	Root.Traces = &RootTraces;
	Root.ExportIndex = ExportIndex;

	Result.Status = ResolveFromArchetype(Root, PropertyName, ArrayIndex, FieldPath, 0, Inherited, Message);

	if (Result.Status == EAssetArchetypeValueStatus::Found)
	{
		Result.Summary = FAssetPropertyValueDecoder::FormatForDisplay(Inherited.Value);
		Result.Note = FString::Printf(TEXT("Inherited from %s."), *Inherited.Source);
		if (!Inherited.Note.IsEmpty())
		{
			Result.Note += TEXT(" ") + Inherited.Note;
		}
		Result.Value = MoveTemp(Inherited.Value);
	}
	else
	{
		// Nothing in the package chain stores the property, so the value is a native (C++) default, unless the property is a
		// variable of the Blueprint itself. Live reflection on the class default object could supply native values; see the
		// class comment.
		Result.Note = Message;

		FString DefaultText;
		if (FieldPath.IsEmpty() && FindBlueprintVariable(RootDocument, RootTraces, ExportIndex, PropertyName, DefaultText))
		{
			Result.Status = EAssetArchetypeValueStatus::DeclaredByBlueprint;
			Result.Summary = DefaultText.IsEmpty() ? FString(TEXT("zero / empty")) : FString::Printf(TEXT("\"%s\""), *DefaultText);
			Result.Note = DefaultText.IsEmpty() ? TEXT("Declared as a variable of the Blueprint with no default value, so it is zero or empty (inferred from the Blueprint's NewVariables).")
												: FString::Printf(TEXT("Declared as a variable of the Blueprint with the default value text \"%s\" (from the Blueprint's NewVariables)."), *DefaultText);
		}
		else if (FieldPath.IsEmpty() && (Result.Status == EAssetArchetypeValueStatus::NotSerializedInChain || Message.Contains(NativePackageMessage)))
		{
			// The chain ends in native code. Only then is the running editor's class default object a fair stand-in: an archetype
			// that merely could not be read might hold a different value.
			FString Text;
			FString OwnerClass;
			if (ReflectNativeDefault(RootDocument, ExportIndex, PropertyName, ArrayIndex, Text, OwnerClass))
			{
				Result.Status = EAssetArchetypeValueStatus::NativeDefaultFromLiveReflection;
				Result.Summary = Text.IsEmpty() ? FString(TEXT("(empty)")) : Text;
				Result.Note = FString::Printf(TEXT("Native default of %s, read from the running editor's class default object (live reflection); the editor that saved the asset may have used another value."), *OwnerClass);
			}
		}
	}

	return Result;
}
