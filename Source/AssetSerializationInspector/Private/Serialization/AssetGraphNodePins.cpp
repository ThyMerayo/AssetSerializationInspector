// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetGraphNodePins.h"

#include "EdGraph/EdGraphNode.h"
#include "UObject/BlueprintsObjectVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"
#include "UObject/FrameworkObjectVersion.h"
#include "UObject/ReleaseObjectVersion.h"
#include "UObject/UE5MainStreamObjectVersion.h"
#include "UObject/UE5ReleaseStreamObjectVersion.h"

#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetNativeReader.h"
#include "Serialization/AssetSchemaReflection.h"
#include "Serialization/AssetSerializationPrimitives.h"
#include "Trace/AssetSerializationTrace.h"

namespace
{
	// The persistent bits of UEdGraphPin::Serialize, by position (the order must not change in the engine).
	const TCHAR* const PinFlagNames[] = { TEXT("Hidden"), TEXT("NotConnectable"), TEXT("DefaultValueReadOnly"), TEXT("DefaultValueIgnored"), TEXT("AdvancedView"), TEXT("Orphaned"),
		TEXT("HasSnappedChild"), TEXT("HasSnappedParent") };

	/** A pin of a graph holds at most this many links, sub pins and the like; a count beyond it means the data is not what this reading expects. */
	constexpr int32 MaximumPinReferences = 4096;

	/** An object an optional reference points at; a null reference reads as an empty string. */
	FString ReadOptionalObject(FNativeReader& Reader)
	{
		const FString Object = Reader.ReadObject();
		return Object == TEXT("None") ? FString() : Object;
	}

	/** One part of a pin type: the category, the sub category, and the object the type refers to ("object (/Script/Engine.Actor)"). */
	FString DescribeTypePart(const FString& Category, const FString& SubCategory, const FString& Object)
	{
		FString Text = Category == TEXT("None") ? FString(TEXT("(none)")) : Category;
		if (SubCategory != TEXT("None") && !SubCategory.IsEmpty())
		{
			Text += TEXT(" ") + SubCategory;
		}

		if (!Object.IsEmpty())
		{
			Text += FString::Printf(TEXT(" (%s)"), *Object);
		}

		return Text;
	}

	/** A pin reference: a flag for null, then the owning node and the id (UEdGraphPin::SerializePin). */
	void ReadPinReference(FNativeReader& Reader, TArray<FAssetGraphPinReference>& Out)
	{
		if (Reader.ReadBool())
		{
			return;
		}

		FAssetGraphPinReference& Reference = Out.AddDefaulted_GetRef();
		Reference.Node = Reader.ReadObject();
		Reference.Pin = Reader.ReadGuid();
	}

	void ReadPinReferences(FNativeReader& Reader, TArray<FAssetGraphPinReference>& Out)
	{
		const int32 Count = Reader.Read<int32>();
		if (Count < 0 || Count > MaximumPinReferences || Count > Reader.Remaining() / 4)
		{
			Reader.Fail(TEXT("The number of pin references does not fit the data"));
			return;
		}

		for (int32 Index = 0; Index < Count && Reader.Ok(); ++Index)
		{
			ReadPinReference(Reader, Out);
		}
	}

	/** The type of a pin: FEdGraphPinType::Serialize. */
	FString ReadPinType(FNativeReader& Reader)
	{
		const FString Category = Reader.ReadName();
		const FString SubCategory = Reader.ReadName();
		const FString Object = ReadOptionalObject(Reader);
		const uint8 Container = Reader.Read<uint8>();

		FString Value;
		if (Container == 3) // EPinContainerType::Map: the type of the values follows.
		{
			const FString ValueCategory = Reader.ReadName();
			const FString ValueSubCategory = Reader.ReadName();
			const FString ValueObject = ReadOptionalObject(Reader);
			const bool bValueConst = Reader.ReadBool();
			const bool bValueWeak = Reader.ReadBool();
			bool bValueWrapper = false;
			if (Reader.CustomVer(FReleaseObjectVersion::GUID) >= FReleaseObjectVersion::PinTypeIncludesUObjectWrapperFlag)
			{
				bValueWrapper = Reader.ReadBool();
			}

			Value = DescribeTypePart(ValueCategory, ValueSubCategory, ValueObject) + (bValueConst ? TEXT(" const") : TEXT("")) + (bValueWeak ? TEXT(" weak") : TEXT(""))
				+ (bValueWrapper ? TEXT(" object wrapper") : TEXT(""));
		}

		const bool bReference = Reader.ReadBool();
		const bool bWeak = Reader.ReadBool();

		// The member a delegate or function pin refers to: its parent, name and id.
		const FString MemberParent = ReadOptionalObject(Reader);
		const FString MemberName = Reader.ReadName();
		const FGuid MemberGuid = Reader.ReadGuid();

		const bool bConst = Reader.ReadBool();
		bool bWrapper = false;
		if (Reader.CustomVer(FReleaseObjectVersion::GUID) >= FReleaseObjectVersion::PinTypeIncludesUObjectWrapperFlag)
		{
			bWrapper = Reader.ReadBool();
		}

		bool bSinglePrecision = false;
		if (Reader.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) >= FUE5ReleaseStreamObjectVersion::SerializeFloatPinDefaultValuesAsSinglePrecision)
		{
			bSinglePrecision = Reader.ReadBool();
		}

		FString Text = DescribeTypePart(Category, SubCategory, Object);
		switch (Container)
		{
			case 1:
				Text = TEXT("array of ") + Text;
				break;
			case 2:
				Text = TEXT("set of ") + Text;
				break;
			case 3:
				Text = FString::Printf(TEXT("map of %s to %s"), *Text, *Value);
				break;
			default:
				break;
		}

		if (bReference)
		{
			Text += TEXT(", by reference");
		}
		if (bConst)
		{
			Text += TEXT(", const");
		}
		if (bWeak)
		{
			Text += TEXT(", weak");
		}
		if (bWrapper)
		{
			Text += TEXT(", object wrapper");
		}
		if (bSinglePrecision)
		{
			Text += TEXT(", single precision");
		}
		if (!MemberName.IsEmpty() && MemberName != TEXT("None"))
		{
			Text += FString::Printf(TEXT(", member %s%s"), MemberParent.IsEmpty() ? TEXT("") : *(MemberParent + TEXT(".")), *MemberName);
		}
		if (MemberGuid.IsValid())
		{
			Text += FString::Printf(TEXT(", member id %s"), *MemberGuid.ToString(EGuidFormats::Digits));
		}

		return Text;
	}

	/** One pin of the array a node owns: a flag for null, the owner and id, then the pin itself (UEdGraphPin::Serialize). */
	void ReadPin(FNativeReader& Reader, const bool bEditorOnlyStripped, FAssetGraphNodePins& Out)
	{
		if (Reader.ReadBool())
		{
			Reader.Fail(TEXT("A pin of the node is null"));
			return;
		}

		Reader.ReadObject(); // the owner, once more for the array
		const FGuid ListedId = Reader.ReadGuid();

		FAssetGraphPin Pin;
		Reader.ReadObject(); // the owning node
		Pin.PinId = Reader.ReadGuid();
		if (Reader.Ok() && Pin.PinId != ListedId)
		{
			Reader.Fail(TEXT("A pin is stored under another id than its own"));
			return;
		}

		Pin.Name = Reader.ReadName();
		if (!bEditorOnlyStripped)
		{
			Pin.FriendlyName = Reader.ReadText();
		}

		if (Reader.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::EdGraphPinSourceIndex)
		{
			Pin.SourceIndex = Reader.Read<int32>();
		}

		Pin.ToolTip = Reader.ReadString();
		Pin.bOutput = Reader.Read<uint8>() == 1;
		Pin.Type = ReadPinType(Reader);
		Pin.DefaultValue = Reader.ReadString();
		Pin.AutogeneratedDefaultValue = Reader.ReadString();
		Pin.DefaultObject = ReadOptionalObject(Reader);
		Pin.DefaultText = Reader.ReadText();

		ReadPinReferences(Reader, Pin.LinkedTo);
		ReadPinReferences(Reader, Pin.SubPins);
		ReadPinReference(Reader, Pin.Parent);
		ReadPinReference(Reader, Pin.PassThrough);

		if (!bEditorOnlyStripped)
		{
			Pin.PersistentGuid = Reader.ReadGuid();
			Pin.Flags = Reader.Read<uint32>();
		}

		if (Reader.Ok())
		{
			Out.Pins.Add(MoveTemp(Pin));
		}
	}

	/** The pins a node with editable pins declares (UK2Node_EditablePinBase): an array of name, type, direction and default value. */
	void ReadUserPins(FNativeReader& Reader, FAssetGraphNodePins& Out)
	{
		const int32 Count = Reader.Read<int32>();
		if (Count < 0 || Count > Reader.Remaining() / 16)
		{
			Reader.Fail(TEXT("The number of declared pins does not fit the data"));
			return;
		}

		for (int32 Index = 0; Index < Count && Reader.Ok(); ++Index)
		{
			FAssetGraphPin Pin;
			Pin.Name = Reader.ReadName();
			Pin.Type = ReadPinType(Reader);
			Pin.bOutput = Reader.Read<uint8>() == 1;
			Pin.DefaultValue = Reader.ReadString();
			if (Reader.Ok())
			{
				Out.UserPins.Add(MoveTemp(Pin));
			}
		}
	}

	bool IsClassNamed(const UClass* Class, const TCHAR* Name)
	{
		for (; Class != nullptr; Class = Class->GetSuperClass())
		{
			if (Class->GetFName() == Name)
			{
				return true;
			}
		}
		return false;
	}

	/** Whether a class is a node that declares pins of its own: an event, a function entry or result, a custom event. */
	bool DeclaresPins(const UClass* Class)
	{
		for (; Class != nullptr; Class = Class->GetSuperClass())
		{
			if (Class->GetFName() == TEXT("K2Node_EditablePinBase"))
			{
				return true;
			}
		}
		return false;
	}

	FString DescribeReference(const FAssetGraphPinReference& Reference, const FAssetGraphPinNames& Names)
	{
		const FString Name = Names.Find(Reference.Pin);
		if (!Name.IsEmpty())
		{
			return Name;
		}

		return FString::Printf(TEXT("%s (pin %s)"), *Reference.Node, *Reference.Pin.ToString(EGuidFormats::Digits));
	}

	FString DescribeReferences(const TArray<FAssetGraphPinReference>& References, const FAssetGraphPinNames& Names)
	{
		TArray<FString> Parts;
		for (const FAssetGraphPinReference& Reference : References)
		{
			Parts.Add(DescribeReference(Reference, Names));
		}

		// The order links were made in is not a difference worth reporting.
		Parts.Sort();
		return FString::Join(Parts, TEXT(", "));
	}

	/** What differs between the two versions of a pin, as the names of the parts. */
	TArray<FString> DifferingParts(const FAssetGraphPin& Old, const FAssetGraphPinNames& OldNames, const FAssetGraphPin& New, const FAssetGraphPinNames& NewNames)
	{
		TArray<FString> Parts;
		if (Old.Name != New.Name)
		{
			Parts.Add(TEXT("name"));
		}
		if (Old.FriendlyName != New.FriendlyName)
		{
			Parts.Add(TEXT("display name"));
		}
		if (Old.bOutput != New.bOutput)
		{
			Parts.Add(TEXT("direction"));
		}
		if (Old.Type != New.Type)
		{
			Parts.Add(TEXT("type"));
		}
		if (Old.DefaultValue != New.DefaultValue || Old.DefaultObject != New.DefaultObject || Old.DefaultText != New.DefaultText)
		{
			Parts.Add(TEXT("default value"));
		}
		if (Old.AutogeneratedDefaultValue != New.AutogeneratedDefaultValue)
		{
			Parts.Add(TEXT("autogenerated default"));
		}
		if (DescribeReferences(Old.LinkedTo, OldNames) != DescribeReferences(New.LinkedTo, NewNames))
		{
			Parts.Add(TEXT("links"));
		}
		if (DescribeReferences(Old.SubPins, OldNames) != DescribeReferences(New.SubPins, NewNames) || DescribeReferences(Old.Parent, OldNames) != DescribeReferences(New.Parent, NewNames)
			|| DescribeReferences(Old.PassThrough, OldNames) != DescribeReferences(New.PassThrough, NewNames))
		{
			Parts.Add(TEXT("sub pins"));
		}
		if (Old.Flags != New.Flags)
		{
			Parts.Add(TEXT("flags"));
		}
		if (Old.ToolTip != New.ToolTip)
		{
			Parts.Add(TEXT("tooltip"));
		}

		return Parts;
	}
} // namespace

FString FAssetGraphPin::DescribeFlags() const
{
	TArray<FString> Parts;
	for (int32 Bit = 0; Bit < UE_ARRAY_COUNT(PinFlagNames); ++Bit)
	{
		if ((Flags & (1u << Bit)) != 0)
		{
			Parts.Add(PinFlagNames[Bit]);
		}
	}

	return FString::Join(Parts, TEXT(", "));
}

FString FAssetGraphNodePins::Summarize() const
{
	int32 Links = 0;
	for (const FAssetGraphPin& Pin : Pins)
	{
		Links += Pin.LinkedTo.Num();
	}

	return FString::Printf(TEXT("%d pins, %d links"), Pins.Num(), Links);
}

FString FAssetGraphPinNames::Find(const FGuid& PinId) const
{
	if (!bBuilt)
	{
		bBuilt = true;

		for (const FAssetPackageExportEntry& Export : Document.ExportMap)
		{
			const FAssetSerializationTrace* Trace = Traces != nullptr ? Traces->FindExportTrace(Export.Index) : nullptr;
			if (Trace == nullptr || !Trace->Root.IsValid())
			{
				continue;
			}

			const FAssetSerializationTraceNode* Native = nullptr;
			for (const TSharedPtr<FAssetSerializationTraceNode>& Node : Trace->Root->Children)
			{
				if (Node.IsValid() && Node->Kind == EAssetSerializationTraceKind::Native)
				{
					Native = Node.Get();
				}
			}

			FAssetGraphNodePins Data;
			if (Native == nullptr || !AssetGraphNodePins::Decode(Document, Export, Export.SerialOffset + Native->Offset, Native->Size, Data) || !Data.bComplete)
			{
				continue;
			}

			const FString Node = Document.ResolveExportPath(Export.Index);
			const FString NodeName = AssetSerializationPrimitives::TailAfterLast(Node, TEXT('.'));
			for (const FAssetGraphPin& Pin : Data.Pins)
			{
				Names.Add(Pin.PinId, FString::Printf(TEXT("%s.%s"), *NodeName, *Pin.Name));
			}
		}
	}

	const FString* Found = Names.Find(PinId);
	return Found != nullptr ? *Found : FString();
}

bool AssetGraphNodePins::Decode(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, const int64 NativeOffset, const int64 NativeSize, FAssetGraphNodePins& Out)
{
	const UClass* NativeClass = AssetSchemaReflection::FindNativeClass(Document, Export.Index);
	if (NativeClass == nullptr || !NativeClass->IsChildOf(UEdGraphNode::StaticClass()) || NativeSize <= 0 || !Document.IsValidRange(NativeOffset, NativeSize))
	{
		return false;
	}

	Out = FAssetGraphNodePins();
	Out.Offset = NativeOffset;
	Out.Size = NativeSize;

	FNativeReader Reader(Document, NativeOffset, NativeSize);

	// The pins are written in the compact format since FBlueprintsObjectVersion::EdGraphPinOptimized, and the names as names since
	// FFrameworkObjectVersion::PinsStoreFName; a package from before that is not read.
	if (Reader.CustomVer(FBlueprintsObjectVersion::GUID) < FBlueprintsObjectVersion::EdGraphPinOptimized
		|| Reader.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::EdGraphPinContainerType)
	{
		Out.Error = TEXT("The package stores its pins in an older format");
		return true;
	}

	if (Reader.ReadBool())
	{
		Out.ObjectGuid = Reader.ReadGuid().ToString(EGuidFormats::DigitsWithHyphens);
	}

	const bool bEditorOnlyStripped = (Document.PackageSummary.GetPackageFlags() & PKG_FilterEditorOnly) != 0;

	const int32 PinCount = Reader.Read<int32>();
	if (Reader.Ok() && (PinCount < 0 || PinCount > Reader.Remaining() / 8))
	{
		Reader.Fail(TEXT("The number of pins does not fit the data"));
	}

	for (int32 Index = 0; Index < PinCount && Reader.Ok(); ++Index)
	{
		ReadPin(Reader, bEditorOnlyStripped, Out);
	}

	if (Reader.Ok() && DeclaresPins(NativeClass))
	{
		ReadUserPins(Reader, Out);
	}

	// A cast node writes whether it is pure (EPureState) once the package is new enough to have it.
	if (Reader.Ok() && IsClassNamed(NativeClass, TEXT("K2Node_DynamicCast"))
		&& Reader.CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::DynamicCastNodesUsePureStateEnum)
	{
		const uint8 PureState = Reader.Read<uint8>();
		Out.Extras.Emplace(TEXT("Purity"), PureState == 0 ? TEXT("pure") : (PureState == 1 ? TEXT("impure") : TEXT("default")));
	}

	if (!Reader.Ok())
	{
		Out.Error = Reader.GetError();
	}
	else if (Reader.Remaining() != 0)
	{
		Out.Error = Reader.TrailingBytesError();
	}
	else
	{
		Out.bComplete = true;
	}

	return true;
}

bool AssetGraphNodePins::ReadPinType(const FAssetPackageDocument& Document, const int64 Offset, const int64 EndOffset, FString& OutType, int64& OutEnd)
{
	if (EndOffset <= Offset || !Document.IsValidRange(Offset, EndOffset - Offset))
	{
		return false;
	}

	FNativeReader Reader(Document, Offset, EndOffset - Offset);
	const FString Type = ::ReadPinType(Reader);
	if (!Reader.Ok())
	{
		return false;
	}

	OutType = Type;
	OutEnd = Reader.Tell();
	return true;
}

FString AssetGraphNodePins::Describe(const FAssetGraphPin& Pin, const FAssetGraphPinNames& Names)
{
	FString Text = FString::Printf(TEXT("%s (%s, %s)"), *Pin.Name, Pin.bOutput ? TEXT("output") : TEXT("input"), *Pin.Type);

	if (!Pin.DefaultValue.IsEmpty())
	{
		Text += FString::Printf(TEXT(", default \"%s\""), *Pin.DefaultValue);
	}
	if (!Pin.DefaultObject.IsEmpty())
	{
		Text += FString::Printf(TEXT(", default object %s"), *Pin.DefaultObject);
	}
	if (!Pin.DefaultText.IsEmpty())
	{
		Text += FString::Printf(TEXT(", default text \"%s\""), *Pin.DefaultText);
	}
	if (!Pin.LinkedTo.IsEmpty())
	{
		Text += FString::Printf(TEXT(", linked to %s"), *DescribeReferences(Pin.LinkedTo, Names));
	}

	const FString Flags = Pin.DescribeFlags();
	if (!Flags.IsEmpty())
	{
		Text += FString::Printf(TEXT(", %s"), *Flags);
	}

	return Text;
}

TArray<FAssetNativeDataChange> AssetGraphNodePins::Compare(const FAssetGraphNodePins& Old, const FAssetGraphPinNames& OldNames, const FAssetGraphNodePins& New, const FAssetGraphPinNames& NewNames)
{
	TArray<FAssetNativeDataChange> Changes;

	TMap<FGuid, const FAssetGraphPin*> OldById;
	for (const FAssetGraphPin& Pin : Old.Pins)
	{
		OldById.Add(Pin.PinId, &Pin);
	}

	TSet<FGuid> NewIds;
	const auto KeyOf = [](const FAssetGraphPin& Pin) { return FString::Printf(TEXT("Pin/%s"), *Pin.PinId.ToString(EGuidFormats::Digits)); };

	for (const FAssetGraphPin& Pin : New.Pins)
	{
		NewIds.Add(Pin.PinId);

		const FAssetGraphPin* const* Before = OldById.Find(Pin.PinId);
		if (Before == nullptr)
		{
			FAssetNativeDataChange& Change = Changes.AddDefaulted_GetRef();
			Change.Key = KeyOf(Pin);
			Change.Title = FString::Printf(TEXT("Pin %s"), *Pin.Name);
			Change.State = FAssetNativeDataChange::EState::Added;
			Change.NewValue = Describe(Pin, NewNames);
			continue;
		}

		const TArray<FString> Parts = DifferingParts(**Before, OldNames, Pin, NewNames);
		if (!Parts.IsEmpty())
		{
			FAssetNativeDataChange& Change = Changes.AddDefaulted_GetRef();
			Change.Key = KeyOf(Pin);
			Change.Title = FString::Printf(TEXT("Pin %s (%s)"), *Pin.Name, *FString::Join(Parts, TEXT(", ")));
			Change.State = FAssetNativeDataChange::EState::Modified;
			Change.OldValue = Describe(**Before, OldNames);
			Change.NewValue = Describe(Pin, NewNames);
		}
	}

	for (const FAssetGraphPin& Pin : Old.Pins)
	{
		if (!NewIds.Contains(Pin.PinId))
		{
			FAssetNativeDataChange& Change = Changes.AddDefaulted_GetRef();
			Change.Key = KeyOf(Pin);
			Change.Title = FString::Printf(TEXT("Pin %s"), *Pin.Name);
			Change.State = FAssetNativeDataChange::EState::Removed;
			Change.OldValue = Describe(Pin, OldNames);
		}
	}

	// The pins the user declared on the node (the parameters of an event or function): matched by name.
	const auto DescribeDeclared = [](const FAssetGraphPin& Pin) {
		return FString::Printf(TEXT("%s (%s, %s%s)"), *Pin.Name, Pin.bOutput ? TEXT("output") : TEXT("input"), *Pin.Type,
			Pin.DefaultValue.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", default \"%s\""), *Pin.DefaultValue));
	};
	const auto FindDeclared = [](const TArray<FAssetGraphPin>& Pins, const FString& Name) { return Pins.FindByPredicate([&Name](const FAssetGraphPin& Pin) { return Pin.Name == Name; }); };

	for (const FAssetGraphPin& Pin : New.UserPins)
	{
		const FAssetGraphPin* Before = FindDeclared(Old.UserPins, Pin.Name);
		if (Before != nullptr && DescribeDeclared(*Before) == DescribeDeclared(Pin))
		{
			continue;
		}

		FAssetNativeDataChange& Change = Changes.AddDefaulted_GetRef();
		Change.Key = FString::Printf(TEXT("DeclaredPin/%s"), *Pin.Name);
		Change.Title = FString::Printf(TEXT("Declared pin %s"), *Pin.Name);
		Change.State = Before == nullptr ? FAssetNativeDataChange::EState::Added : FAssetNativeDataChange::EState::Modified;
		Change.OldValue = Before != nullptr ? DescribeDeclared(*Before) : FString();
		Change.NewValue = DescribeDeclared(Pin);
	}

	for (const FAssetGraphPin& Pin : Old.UserPins)
	{
		if (FindDeclared(New.UserPins, Pin.Name) == nullptr)
		{
			FAssetNativeDataChange& Change = Changes.AddDefaulted_GetRef();
			Change.Key = FString::Printf(TEXT("DeclaredPin/%s"), *Pin.Name);
			Change.Title = FString::Printf(TEXT("Declared pin %s"), *Pin.Name);
			Change.State = FAssetNativeDataChange::EState::Removed;
			Change.OldValue = DescribeDeclared(Pin);
		}
	}

	// What the class of the node writes besides its pins, matched by name.
	for (const TPair<FString, FString>& Extra : New.Extras)
	{
		const TPair<FString, FString>* Before = Old.Extras.FindByPredicate([&Extra](const TPair<FString, FString>& Candidate) { return Candidate.Key == Extra.Key; });
		if (Before != nullptr && Before->Value.Equals(Extra.Value, ESearchCase::CaseSensitive))
		{
			continue;
		}

		FAssetNativeDataChange& Change = Changes.AddDefaulted_GetRef();
		Change.Key = FString::Printf(TEXT("NodeData/%s"), *Extra.Key);
		Change.Title = Extra.Key;
		Change.State = Before == nullptr ? FAssetNativeDataChange::EState::Added : FAssetNativeDataChange::EState::Modified;
		Change.OldValue = Before != nullptr ? Before->Value : FString();
		Change.NewValue = Extra.Value;
	}

	// The same pins in another order (a node that lists its pins differently).
	if (Changes.IsEmpty() && Old.Pins.Num() == New.Pins.Num())
	{
		bool bSameOrder = true;
		for (int32 Index = 0; Index < Old.Pins.Num(); ++Index)
		{
			bSameOrder &= Old.Pins[Index].PinId == New.Pins[Index].PinId;
		}

		if (!bSameOrder)
		{
			FAssetNativeDataChange& Change = Changes.AddDefaulted_GetRef();
			Change.Key = TEXT("Pin order");
			Change.Title = TEXT("Pin order");
			Change.State = FAssetNativeDataChange::EState::Modified;
			Change.OldValue = Old.Summarize();
			Change.NewValue = New.Summarize();
		}
	}

	return Changes;
}
