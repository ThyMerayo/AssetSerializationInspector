// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Serialization/AssetArchetypeResolver.h"
#include "Serialization/AssetPropertyValueDecoder.h"
#include "Trace/AssetPackageFieldDecoder.h"
#include "Trace/AssetSerializationTrace.h"
#include "UObject/ObjectVersion.h"

namespace AssetArchetypeResolverTestUtils
{
	/** A synthetic package whose exports each store one top-level "Tags" property. */
	struct FTestPackage
	{
		FAssetPackageDocument Document;
		FAssetPackageTraceCollection Traces;

		int32 AddName(const FString& Name)
		{
			FAssetPackageNameEntry Entry;
			Entry.Name = Name;
			return Document.NameMap.Add(Entry);
		}

		void AppendInt32(const int32 Value)
		{
			const int32 Start = Document.FileData.Num();
			Document.FileData.AddUninitialized(sizeof(int32));
			FMemory::Memcpy(Document.FileData.GetData() + Start, &Value, sizeof(int32));
		}

		/**
		 * Adds an export. TemplateRawIndex is a serialized FPackageIndex (0 none, positive export + 1, negative import).
		 * Value is a list of int32 words written as the property payload; an empty list leaves the property out.
		 */
		int32 AddExport(const FString& Name, const int32 TemplateRawIndex, const FAssetSerializedPropertyType& PropertyType, const TArray<int32>& Words)
		{
			FAssetPackageExportEntry Export;
			Export.Index = Document.ExportMap.Num();
			Export.ObjectName.NameIndex = AddName(Name);
			Export.TemplateIndex.RawIndex = TemplateRawIndex;
			Export.SerialOffset = Document.FileData.Num();

			for (const int32 Word : Words)
			{
				AppendInt32(Word);
			}

			Export.SerialSize = Document.FileData.Num() - Export.SerialOffset;
			Document.ExportMap.Add(Export);

			if (!Words.IsEmpty())
			{
				TSharedPtr<FAssetSerializationTraceNode> Node = MakeShared<FAssetSerializationTraceNode>();
				Node->Kind = EAssetSerializationTraceKind::Property;
				Node->Name = TEXT("Tags");
				Node->PropertyType = PropertyType;
				Node->Offset = 0;
				Node->Size = Export.SerialSize;

				FAssetSerializationTrace Trace;
				Trace.Root = MakeShared<FAssetSerializationTraceNode>();
				Trace.Root->Children.Add(Node);
				Traces.ExportTraces.Add(Export.Index, MoveTemp(Trace));
			}

			return Export.Index;
		}
	};

	static FAssetSerializedPropertyType MakeType(const FString& Name, const TArray<FString>& Parameters = {})
	{
		FAssetSerializedPropertyType Type;
		Type.Name = Name;
		for (const FString& Parameter : Parameters)
		{
			FAssetSerializedPropertyType Inner;
			Inner.Name = Parameter;
			Type.Parameters.Add(Inner);
		}
		return Type;
	}

	static FString Describe(const FAssetDecodedPropertyValue& Value)
	{
		TArray<FString> Parts;
		for (const FAssetDecodedPropertyValue& Child : Value.Children)
		{
			Parts.Add(Child.Kind == EAssetDecodedValueKind::MapEntry ? FString::Printf(TEXT("%s=%s"), *Child.Children[0].Value, *Child.Children[1].Value) : Child.Value);
		}
		return FString::Join(Parts, TEXT(","));
	}
} // namespace AssetArchetypeResolverTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetArchetypeResolver_ResolvesSetThroughSamePackageChain, "AssetSerializationInspector.Serialization.AssetArchetypeResolver.ResolvesSetThroughSamePackageChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetArchetypeResolver_ResolvesSetThroughSamePackageChain::RunTest(const FString& Parameters)
{
	using namespace AssetArchetypeResolverTestUtils;

	const FAssetSerializedPropertyType SetOfInt = MakeType(TEXT("SetProperty"), { TEXT("IntProperty") });

	FTestPackage Package;
	// Grandparent stores {1, 2, 3}: no removals, three elements.
	const int32 Grandparent = Package.AddExport(TEXT("Grandparent"), 0, SetOfInt, { 0, 3, 1, 2, 3 });
	// Parent does not store the property at all.
	const int32 Parent = Package.AddExport(TEXT("Parent"), Grandparent + 1, SetOfInt, {});
	// Child removes 2 and adds 4.
	const int32 Child = Package.AddExport(TEXT("Child"), Parent + 1, SetOfInt, { 1, 2, 1, 4 });

	FAssetArchetypeResolver Resolver(Package.Document, Package.Traces);
	FAssetArchetypeValue Inherited;
	FString Message;

	// The child's inherited value is the grandparent's, reached through the parent that does not override it.
	TestEqual(TEXT("The value is found through the chain"), Resolver.ResolveInheritedValue(Child, TEXT("Tags"), 0, Inherited, Message), EAssetArchetypeValueStatus::Found);
	TestEqual(TEXT("The child inherits the grandparent's set"), Describe(Inherited.Value), FString(TEXT("1,2,3")));
	TestTrue(TEXT("The source names the export that stored it"), Inherited.Source.Contains(TEXT("Grandparent")));
	TestEqual(TEXT("The chain root assumed empty native defaults, so the result is only inferred"), Inherited.Confidence, EAssetContainerFinalValueConfidence::Inferred);
	TestFalse(TEXT("The assumption is explained"), Inherited.Note.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetArchetypeResolver_AppliesIntermediateDelta, "AssetSerializationInspector.Serialization.AssetArchetypeResolver.AppliesIntermediateDelta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetArchetypeResolver_AppliesIntermediateDelta::RunTest(const FString& Parameters)
{
	using namespace AssetArchetypeResolverTestUtils;

	const FAssetSerializedPropertyType SetOfInt = MakeType(TEXT("SetProperty"), { TEXT("IntProperty") });

	FTestPackage Package;
	const int32 Grandparent = Package.AddExport(TEXT("Grandparent"), 0, SetOfInt, { 0, 3, 1, 2, 3 });
	// Parent removes 2 and adds 4, so its final value is {1, 3, 4}.
	const int32 Parent = Package.AddExport(TEXT("Parent"), Grandparent + 1, SetOfInt, { 1, 2, 1, 4 });
	const int32 Child = Package.AddExport(TEXT("Child"), Parent + 1, SetOfInt, { 0, 0 });

	FAssetArchetypeResolver Resolver(Package.Document, Package.Traces);
	FAssetArchetypeValue Inherited;
	FString Message;

	TestEqual(TEXT("The value is found"), Resolver.ResolveInheritedValue(Child, TEXT("Tags"), 0, Inherited, Message), EAssetArchetypeValueStatus::Found);
	TestEqual(TEXT("The parent's delta is applied before the child sees it"), Describe(Inherited.Value), FString(TEXT("1,3,4")));
	TestEqual(TEXT("The source is the parent"), Inherited.Source.Contains(TEXT("Parent")), true);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetArchetypeResolver_ResolvesMapReplaceWithCertainty, "AssetSerializationInspector.Serialization.AssetArchetypeResolver.ResolvesMapReplaceWithCertainty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetArchetypeResolver_ResolvesMapReplaceWithCertainty::RunTest(const FString& Parameters)
{
	using namespace AssetArchetypeResolverTestUtils;

	const FAssetSerializedPropertyType MapOfInt = MakeType(TEXT("MapProperty"), { TEXT("IntProperty"), TEXT("IntProperty") });

	FTestPackage Package;
	// The parent uses the engine's replace marker (-1 removals) with one entry, key 1 -> 10.
	const int32 Parent = Package.AddExport(TEXT("Parent"), 0, MapOfInt, { -1, 1, 1, 10 });
	// The child removes key 1 and adds key 2 -> 20.
	const int32 Child = Package.AddExport(TEXT("Child"), Parent + 1, MapOfInt, { 1, 1, 1, 2, 20 });

	FAssetArchetypeResolver Resolver(Package.Document, Package.Traces);
	FAssetArchetypeValue Inherited;
	FString Message;

	TestEqual(TEXT("The value is found"), Resolver.ResolveInheritedValue(Child, TEXT("Tags"), 0, Inherited, Message), EAssetArchetypeValueStatus::Found);
	TestEqual(TEXT("The parent's replaced map is inherited as-is"), Describe(Inherited.Value), FString(TEXT("1=10")));
	TestEqual(TEXT("The replace marker needs no assumption"), Inherited.Confidence, EAssetContainerFinalValueConfidence::Certain);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetArchetypeResolver_ReportsEndOfChainAndNativeArchetypes, "AssetSerializationInspector.Serialization.AssetArchetypeResolver.ReportsEndOfChainAndNativeArchetypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetArchetypeResolver_ReportsEndOfChainAndNativeArchetypes::RunTest(const FString& Parameters)
{
	using namespace AssetArchetypeResolverTestUtils;

	const FAssetSerializedPropertyType SetOfInt = MakeType(TEXT("SetProperty"), { TEXT("IntProperty") });

	FTestPackage Package;
	const int32 Root = Package.AddExport(TEXT("Root"), 0, SetOfInt, {});
	const int32 Child = Package.AddExport(TEXT("Child"), Root + 1, SetOfInt, { 0, 1, 5 });

	FAssetArchetypeResolver Resolver(Package.Document, Package.Traces);
	FAssetArchetypeValue Inherited;
	FString Message;

	TestEqual(TEXT("A chain that never stores the property ends in native defaults"), Resolver.ResolveInheritedValue(Child, TEXT("Tags"), 0, Inherited, Message),
		EAssetArchetypeValueStatus::NotSerializedInChain);
	TestFalse(TEXT("The reason is reported"), Message.IsEmpty());

	// An export whose template is an import from a /Script/ package (a native class default object).
	FTestPackage Native;
	FAssetPackageImportEntry Package0;
	Package0.Index = 0;
	Package0.ObjectName.NameIndex = Native.AddName(TEXT("/Script/Engine"));
	Native.Document.ImportMap.Add(Package0);

	FAssetPackageImportEntry Cdo;
	Cdo.Index = 1;
	Cdo.ObjectName.NameIndex = Native.AddName(TEXT("Default__Actor"));
	Cdo.OuterIndex.RawIndex = -1;
	Native.Document.ImportMap.Add(Cdo);

	const int32 NativeChild = Native.AddExport(TEXT("BP"), -2, SetOfInt, { 0, 1, 5 });

	FAssetArchetypeResolver NativeResolver(Native.Document, Native.Traces);
	TestEqual(
		TEXT("A native archetype cannot be read from package files"), NativeResolver.ResolveInheritedValue(NativeChild, TEXT("Tags"), 0, Inherited, Message), EAssetArchetypeValueStatus::Unavailable);
	TestTrue(TEXT("The message says the archetype is native"), Message.Contains(TEXT("native")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetArchetypeResolver_DescribesOmittedProperties, "AssetSerializationInspector.Serialization.AssetArchetypeResolver.DescribesOmittedProperties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetArchetypeResolver_DescribesOmittedProperties::RunTest(const FString& Parameters)
{
	using namespace AssetArchetypeResolverTestUtils;

	const FAssetSerializedPropertyType IntType = MakeType(TEXT("IntProperty"));

	FTestPackage Package;
	// The parent stores Tags = 42; the child omits it, so it has the parent's value.
	const int32 Parent = Package.AddExport(TEXT("Parent"), 0, IntType, { 42 });
	const int32 Child = Package.AddExport(TEXT("Child"), Parent + 1, IntType, {});
	// A separate chain whose root never stores the property.
	const int32 Empty = Package.AddExport(TEXT("Empty"), 0, IntType, {});
	const int32 EmptyChild = Package.AddExport(TEXT("EmptyChild"), Empty + 1, IntType, {});

	FAssetArchetypeResolver Resolver(Package.Document, Package.Traces);

	const FAssetOmittedPropertyDefault Inherited = Resolver.DescribeOmittedProperty(Child, TEXT("Tags"), 0);
	TestEqual(TEXT("An omitted property takes the archetype's value"), Inherited.Status, EAssetArchetypeValueStatus::Found);
	TestEqual(TEXT("The summary is the inherited value"), Inherited.Summary, FString(TEXT("42")));
	TestEqual(TEXT("The value is available for comparison"), Inherited.Value.Value, FString(TEXT("42")));
	TestTrue(TEXT("The note names the export it came from"), Inherited.Note.Contains(TEXT("Parent")));

	const FAssetOmittedPropertyDefault Native = Resolver.DescribeOmittedProperty(EmptyChild, TEXT("Tags"), 0);
	TestEqual(TEXT("A property no package stores comes from native defaults"), Native.Status, EAssetArchetypeValueStatus::NotSerializedInChain);
	TestTrue(TEXT("The summary is empty"), Native.Summary.IsEmpty());
	TestFalse(TEXT("The reason is reported"), Native.Note.IsEmpty());

	const FAssetOmittedPropertyDefault Wrong = Resolver.DescribeOmittedProperty(Child, TEXT("Other"), 0);
	TestNotEqual(TEXT("A different property name is not found on the chain"), Wrong.Status, EAssetArchetypeValueStatus::Found);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetArchetypeResolver_ResolvesSetNestedInStruct, "AssetSerializationInspector.Serialization.AssetArchetypeResolver.ResolvesSetNestedInStruct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetArchetypeResolver_ResolvesSetNestedInStruct::RunTest(const FString& Parameters)
{
	using namespace AssetArchetypeResolverTestUtils;

	FTestPackage Package;
	Package.Document.PackageSummary.SetFileVersions(522, static_cast<int32>(EUnrealEngineObjectUE5Version::INITIAL_VERSION), 0);

	const FAssetSerializedPropertyType StructType = MakeType(TEXT("StructProperty"), { TEXT("MySettings") });

	// Adds an export that stores a "Settings" struct. The struct holds a "Tags" set written with the given words (no field when
	// empty), in the layout used before complete type names. A null Words pointer leaves "Settings" out altogether.
	const auto AddExportWithStruct = [&](const FString& Name, const int32 TemplateRawIndex, const TArray<int32>* Words) {
		FAssetPackageExportEntry Export;
		Export.Index = Package.Document.ExportMap.Num();
		Export.ObjectName.NameIndex = Package.AddName(Name);
		Export.TemplateIndex.RawIndex = TemplateRawIndex;
		Export.SerialOffset = Package.Document.FileData.Num();

		const auto AppendName = [&Package](const FString& Text) {
			Package.AppendInt32(Package.AddName(Text));
			Package.AppendInt32(0);
		};

		if (Words != nullptr)
		{
			if (!Words->IsEmpty())
			{
				AppendName(TEXT("Tags"));
				AppendName(TEXT("SetProperty"));
				Package.AppendInt32(Words->Num() * 4);
				Package.AppendInt32(0);
				AppendName(TEXT("IntProperty"));
				Package.Document.FileData.Add(0); // no property GUID
				for (const int32 Word : *Words)
				{
					Package.AppendInt32(Word);
				}
			}
			AppendName(TEXT("None"));
		}

		Export.SerialSize = Package.Document.FileData.Num() - Export.SerialOffset;
		Package.Document.ExportMap.Add(Export);

		if (Words != nullptr)
		{
			TSharedPtr<FAssetSerializationTraceNode> Node = MakeShared<FAssetSerializationTraceNode>();
			Node->Kind = EAssetSerializationTraceKind::Property;
			Node->Name = TEXT("Settings");
			Node->PropertyType = StructType;
			Node->Offset = 0;
			Node->Size = Export.SerialSize;

			FAssetSerializationTrace Trace;
			Trace.Root = MakeShared<FAssetSerializationTraceNode>();
			Trace.Root->Children.Add(Node);
			Package.Traces.ExportTraces.Add(Export.Index, MoveTemp(Trace));
		}

		return Export.Index;
	};

	const TArray<int32> GrandparentTags = { 0, 3, 1, 2, 3 };
	const TArray<int32> ChildTags = { 1, 2, 1, 4 };
	const TArray<int32> NoFields;

	const int32 Grandparent = AddExportWithStruct(TEXT("Grandparent"), 0, &GrandparentTags);
	// The parent stores the struct but not the field, so the field's value still comes from the grandparent.
	const int32 Parent = AddExportWithStruct(TEXT("Parent"), Grandparent + 1, &NoFields);
	// The child removes 2 and adds 4.
	const int32 Child = AddExportWithStruct(TEXT("Child"), Parent + 1, &ChildTags);

	const FAssetPackageExportEntry& ChildExport = Package.Document.ExportMap[Child];
	const FAssetSerializationTraceNode& ChildNode = *Package.Traces.FindExportTrace(Child)->Root->Children[0];
	const FAssetDecodedPropertyValue ChildStruct = FAssetPropertyValueDecoder::Decode(Package.Document, ChildNode, ChildExport.SerialOffset);

	if (!TestTrue(TEXT("The child's struct decodes"), ChildStruct.IsSuccess()) || !TestEqual(TEXT("With its set field"), ChildStruct.Children.Num(), 1))
	{
		return false;
	}

	FAssetArchetypeResolver Resolver(Package.Document, Package.Traces);
	FAssetArchetypeValue Final;
	FString Message;

	if (TestTrue(TEXT("The nested set resolves"), Resolver.ResolveFinalNestedContainerValue(Child, TEXT("Settings"), 0, { TEXT("Tags") }, ChildStruct.Children[0], Final, Message)))
	{
		TestEqual(TEXT("The delta applies to the grandparent's field, through the parent that stores none"), Describe(Final.Value), FString(TEXT("1,3,4")));
	}

	// The parent stores the struct without the field, so the field has the grandparent's value.
	const FAssetOmittedPropertyDefault Omitted = Resolver.DescribeOmittedField(Parent, TEXT("Settings"), 0, { TEXT("Tags") });
	TestEqual(TEXT("An omitted struct field takes the archetype's value"), Omitted.Status, EAssetArchetypeValueStatus::Found);
	TestEqual(TEXT("Which is the grandparent's set"), Describe(Omitted.Value), FString(TEXT("1,2,3")));
	TestTrue(TEXT("And says where it came from"), Omitted.Note.Contains(TEXT("Grandparent")));

	const FAssetOmittedPropertyDefault OtherField = Resolver.DescribeOmittedField(Parent, TEXT("Settings"), 0, { TEXT("Missing") });
	TestNotEqual(TEXT("A field no archetype stores is not found"), OtherField.Status, EAssetArchetypeValueStatus::Found);

	// With no archetype providing the field, removals prove its defaults were not empty, so they cannot be assumed away.
	const int32 Lone = AddExportWithStruct(TEXT("Lone"), 0, &ChildTags);
	const FAssetSerializationTraceNode& LoneNode = *Package.Traces.FindExportTrace(Lone)->Root->Children[0];
	const FAssetDecodedPropertyValue LoneStruct = FAssetPropertyValueDecoder::Decode(Package.Document, LoneNode, Package.Document.ExportMap[Lone].SerialOffset);

	// Removing an element the defaults do not have is inconsistent, so the delta is refused rather than guessed.
	FAssetArchetypeResolver LoneResolver(Package.Document, Package.Traces);
	TestFalse(TEXT("A delta that removes what the defaults lack is refused"), LoneResolver.ResolveFinalNestedContainerValue(Lone, TEXT("Settings"), 0, { TEXT("Tags") }, LoneStruct.Children[0], Final, Message));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetArchetypeResolver_DescribesBlueprintVariableDefaults, "AssetSerializationInspector.Serialization.AssetArchetypeResolver.DescribesBlueprintVariableDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetArchetypeResolver_DescribesBlueprintVariableDefaults::RunTest(const FString& Parameters)
{
	// BP_BOX50 is a real Blueprint (migrated from UE 5.0) that declares variables.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
	if (!TestTrue(TEXT("The plugin is found"), Plugin.IsValid()))
	{
		return false;
	}

	FText Error;
	const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("TestFixtures"), TEXT("BP_BOX50.uasset")), Error);
	if (!TestTrue(TEXT("The fixture loads"), Document.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);

	int32 ClassDefaultObject = INDEX_NONE;
	const FAssetSerializationTraceNode* VariablesNode = nullptr;
	const FAssetPackageExportEntry* BlueprintExport = nullptr;

	for (const FAssetPackageExportEntry& Export : Document->ExportMap)
	{
		if (Document->IsExportClassDefaultObject(Export))
		{
			ClassDefaultObject = Export.Index;
		}

		const FAssetSerializationTrace* Trace = Traces->FindExportTrace(Export.Index);
		if (Trace != nullptr && Trace->Root.IsValid())
		{
			for (const TSharedPtr<FAssetSerializationTraceNode>& Child : Trace->Root->Children)
			{
				if (Child.IsValid() && Child->Name == TEXT("NewVariables"))
				{
					VariablesNode = Child.Get();
					BlueprintExport = &Export;
				}
			}
		}
	}

	if (!TestTrue(TEXT("The package has a class default object and a variable list"), ClassDefaultObject != INDEX_NONE && VariablesNode != nullptr))
	{
		return false;
	}

	const FAssetDecodedPropertyValue Variables = FAssetPropertyValueDecoder::Decode(*Document, *VariablesNode, BlueprintExport->SerialOffset);
	if (!TestTrue(TEXT("The variables decode"), Variables.IsSuccess() && !Variables.Children.IsEmpty()))
	{
		return false;
	}

	FString VariableName;
	for (const FAssetDecodedPropertyValue& Field : Variables.Children[0].Children)
	{
		if (Field.Name == TEXT("VarName"))
		{
			VariableName = Field.Value;
		}
	}

	if (!TestFalse(TEXT("The first variable has a name"), VariableName.IsEmpty()))
	{
		return false;
	}

	FAssetArchetypeResolver Resolver(*Document, *Traces);

	const FAssetOmittedPropertyDefault Declared = Resolver.DescribeOmittedProperty(ClassDefaultObject, VariableName, 0);
	TestEqual(TEXT("A Blueprint variable the chain does not store is declared by the Blueprint"), Declared.Status, EAssetArchetypeValueStatus::DeclaredByBlueprint);
	TestFalse(TEXT("It has a summary"), Declared.Summary.IsEmpty());
	TestTrue(TEXT("The note says where the declaration is"), Declared.Note.Contains(TEXT("Blueprint")));

	const FAssetOmittedPropertyDefault Other = Resolver.DescribeOmittedProperty(ClassDefaultObject, TEXT("NotAVariableOfThisBlueprint"), 0);
	TestNotEqual(TEXT("A name the Blueprint does not declare is not"), Other.Status, EAssetArchetypeValueStatus::DeclaredByBlueprint);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetArchetypeResolver_ReadsNativeDefaultsFromLiveReflection, "AssetSerializationInspector.Serialization.AssetArchetypeResolver.ReadsNativeDefaultsFromLiveReflection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetArchetypeResolver_ReadsNativeDefaultsFromLiveReflection::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
	if (!TestTrue(TEXT("The plugin is found"), Plugin.IsValid()))
	{
		return false;
	}

	FText Error;
	const TSharedPtr<FAssetPackageDocument> Document = FAssetPackageReader::LoadFromFile(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("TestFixtures"), TEXT("BP_BOX50.uasset")), Error);
	if (!TestTrue(TEXT("The fixture loads"), Document.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FAssetPackageTraceCollection> Traces = FAssetPackageFieldDecoder::Decode(*Document);

	// The Blueprint's StaticMeshComponent template derives from a native component, whose defaults live in C++.
	int32 Component = INDEX_NONE;
	for (const FAssetPackageExportEntry& Export : Document->ExportMap)
	{
		FString ClassPath;
		if (Document->ResolvePackageIndexPath(Export.ClassIndex, ClassPath) && ClassPath == TEXT("/Script/Engine.StaticMeshComponent"))
		{
			Component = Export.Index;
		}
	}

	if (!TestTrue(TEXT("The package has a static mesh component"), Component != INDEX_NONE))
	{
		return false;
	}

	FAssetArchetypeResolver Resolver(*Document, *Traces);

	const FAssetOmittedPropertyDefault Hidden = Resolver.DescribeOmittedProperty(Component, TEXT("bHiddenInGame"), 0);
	TestEqual(TEXT("A native default comes from live reflection"), Hidden.Status, EAssetArchetypeValueStatus::NativeDefaultFromLiveReflection);
	TestEqual(TEXT("It is the class default object's value"), Hidden.Summary, FString(TEXT("False")));
	TestTrue(TEXT("The note says it was read live"), Hidden.Note.Contains(TEXT("live reflection")));

	const FAssetOmittedPropertyDefault Missing = Resolver.DescribeOmittedProperty(Component, TEXT("NotAProperty"), 0);
	TestNotEqual(TEXT("A property the native class lacks is not reflected"), Missing.Status, EAssetArchetypeValueStatus::NativeDefaultFromLiveReflection);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
