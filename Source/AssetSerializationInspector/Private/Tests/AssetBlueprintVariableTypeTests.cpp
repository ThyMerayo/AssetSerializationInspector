// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "Diff/AssetPackageDiff.h"
#include "Model/AssetPackageDocument.h"
#include "Readers/AssetPackageReader.h"
#include "Tests/AssetTestPackageNames.h"
#include "Trace/AssetPackageFieldDecoder.h"

namespace VariableTypeTestUtils
{
	static const TCHAR* const BlueprintPackage = TEXT("/Game/__AssetSerializationInspectorTests/VariableType/BP_VariableType");
	static const TCHAR* const BlueprintFolder = TEXT("/Game/__AssetSerializationInspectorTests/VariableType");

	static FEdGraphPinType MakeType(const TCHAR* Category, const EPinContainerType Container = EPinContainerType::None)
	{
		FEdGraphPinType Type;
		Type.PinCategory = Category;
		Type.ContainerType = Container;
		return Type;
	}

	/** Saves the Blueprint and keeps a copy of the file under another name, which the next save would overwrite. */
	static FString SaveCopy(UBlueprint* Blueprint, const TCHAR* Extension)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		const FString File = FPackageName::LongPackageNameToFilename(Blueprint->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		SaveArgs.bSlowTask = false;
		if (!UPackage::SavePackage(Blueprint->GetPackage(), Blueprint, *File, SaveArgs))
		{
			return FString();
		}

		const FString Copy = FPaths::ChangeExtension(File, Extension);
		IFileManager::Get().Copy(*Copy, *File);
		return Copy;
	}

	static const FAssetPackageDiffEntry* FindNamed(const FAssetPackageDiffEntry& Entry, const FString& Name)
	{
		if (Entry.DisplayName.ToString() == Name)
		{
			return &Entry;
		}

		for (const FAssetPackageDiffEntry& Child : Entry.Children)
		{
			if (const FAssetPackageDiffEntry* Found = FindNamed(Child, Name))
			{
				return Found;
			}
		}
		return nullptr;
	}

	/** The NewVariables array of the Blueprint in the diff. */
	static const FAssetPackageDiffEntry* FindVariables(const FAssetPackageDiffResult& Diff)
	{
		for (const FAssetPackageDiffEntry& Entry : Diff.Entries)
		{
			if (const FAssetPackageDiffEntry* Found = FindNamed(Entry, TEXT("NewVariables")))
			{
				return Found;
			}
		}
		return nullptr;
	}

	/** The type of the variable that changed: the VarType entry inside an element of the NewVariables array. */
	static const FAssetPackageDiffEntry* FindVarType(const FAssetPackageDiffResult& Diff)
	{
		const FAssetPackageDiffEntry* Variables = FindVariables(Diff);
		if (Variables == nullptr)
		{
			return nullptr;
		}

		for (const FAssetPackageDiffEntry& Element : Variables->Children)
		{
			for (const FAssetPackageDiffEntry& Field : Element.Children)
			{
				if (Field.DisplayName.ToString() == TEXT("VarType"))
				{
					return &Field;
				}
			}
		}
		return nullptr;
	}
} // namespace VariableTypeTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBlueprintVariableType_ShowsTheTypeThatChanged, "AssetSerializationInspector.Serialization.AssetBlueprintVariableType.ShowsTheTypeThatChanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBlueprintVariableType_ShowsTheTypeThatChanged::RunTest(const FString& Parameters)
{
	using namespace VariableTypeTestUtils;

	IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(BlueprintFolder), false, true);

	UPackage* Package = CreatePackage(*AssetTestPackages::Unique(BlueprintPackage));
	UBlueprint* Blueprint =
		FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package, TEXT("BP_VariableType"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("A Blueprint is created"), Blueprint))
	{
		return false;
	}

	// Changing the type of a variable of a compiled Blueprint makes the engine reinstance its default object, and it warns that the old
	// value had another type. It is what the test provokes (twice), not a fault of the plugin.
	AddExpectedMessage(TEXT("Type mismatch in Beta of BP_VariableType_C"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);

	FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Alpha"), MakeType(TEXT("int")));
	FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Beta"), MakeType(TEXT("name")));
	const FString AsName = SaveCopy(Blueprint, TEXT("name.uasset"));

	FBlueprintEditorUtils::ChangeMemberVariableType(Blueprint, TEXT("Beta"), MakeType(TEXT("int")));
	const FString AsInt = SaveCopy(Blueprint, TEXT("int.uasset"));

	FBlueprintEditorUtils::ChangeMemberVariableType(Blueprint, TEXT("Beta"), MakeType(TEXT("int"), EPinContainerType::Array));
	const FString AsArray = SaveCopy(Blueprint, TEXT("array.uasset"));

	FText Error;
	const TSharedPtr<FAssetPackageDocument> NameDocument = FAssetPackageReader::LoadFromFile(AsName, Error);
	const TSharedPtr<FAssetPackageDocument> IntDocument = FAssetPackageReader::LoadFromFile(AsInt, Error);
	const TSharedPtr<FAssetPackageDocument> ArrayDocument = FAssetPackageReader::LoadFromFile(AsArray, Error);
	if (!TestTrue(TEXT("The three versions are written and load"), NameDocument.IsValid() && IntDocument.IsValid() && ArrayDocument.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FAssetPackageTraceCollection> NameTraces = FAssetPackageFieldDecoder::Decode(*NameDocument);
	const TSharedPtr<FAssetPackageTraceCollection> IntTraces = FAssetPackageFieldDecoder::Decode(*IntDocument);
	const TSharedPtr<FAssetPackageTraceCollection> ArrayTraces = FAssetPackageFieldDecoder::Decode(*ArrayDocument);

	// The variable's type changed from name to int: the diff says so, and only for the variable that changed.
	const FAssetPackageDiffResult ToInt = AssetPackageDiff::Compare(*NameDocument, *IntDocument, NameTraces.Get(), IntTraces.Get());
	if (const FAssetPackageDiffEntry* Type = FindVarType(ToInt))
	{
		TestEqual(TEXT("The type was modified"), Type->State, EAssetPackageDiffState::Modified);
		TestEqual(TEXT("It was a name"), Type->OldValue, FString(TEXT("name")));
		TestEqual(TEXT("It is an int"), Type->NewValue, FString(TEXT("int")));
	}
	else
	{
		AddError(TEXT("The change of the variable's type is not in the diff"));
	}

	if (const FAssetPackageDiffEntry* Variables = FindVariables(ToInt))
	{
		TestEqual(TEXT("Only the variable that changed is listed"), Variables->Children.Num(), 1);
	}

	// A container type is described with what it holds.
	const FAssetPackageDiffResult ToArray = AssetPackageDiff::Compare(*IntDocument, *ArrayDocument, IntTraces.Get(), ArrayTraces.Get());
	if (const FAssetPackageDiffEntry* Type = FindVarType(ToArray))
	{
		TestEqual(TEXT("It was an int"), Type->OldValue, FString(TEXT("int")));
		TestEqual(TEXT("It is an array of ints"), Type->NewValue, FString(TEXT("array of int")));
	}
	else
	{
		AddError(TEXT("The change to an array is not in the diff"));
	}

	IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(BlueprintFolder), false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBlueprintVariableType_DescribesObjectsMapsAndFlags, "AssetSerializationInspector.Serialization.AssetBlueprintVariableType.DescribesObjectsMapsAndFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBlueprintVariableType_DescribesObjectsMapsAndFlags::RunTest(const FString& Parameters)
{
	using namespace VariableTypeTestUtils;

	static const TCHAR* const FlagsPackage = TEXT("/Game/__AssetSerializationInspectorTests/VariableFlags/BP_VariableFlags");
	static const TCHAR* const FlagsFolder = TEXT("/Game/__AssetSerializationInspectorTests/VariableFlags");

	IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(FlagsFolder), false, true);

	// Changing the type of a variable of a compiled Blueprint makes the engine warn that the old value had another type.
	AddExpectedMessage(TEXT("Type mismatch in"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);

	UPackage* Package = CreatePackage(*AssetTestPackages::Unique(FlagsPackage));
	UBlueprint* Blueprint =
		FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package, TEXT("BP_VariableFlags"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("A Blueprint is created"), Blueprint))
	{
		return false;
	}

	const auto ObjectType = [](UClass* Class) {
		FEdGraphPinType Type = MakeType(TEXT("object"));
		Type.PinSubCategoryObject = Class;
		return Type;
	};
	const auto ClassType = [](UClass* Class, const bool bWrapper) {
		FEdGraphPinType Type = MakeType(TEXT("class"));
		Type.PinSubCategoryObject = Class;
		Type.bIsUObjectWrapper = bWrapper;
		return Type;
	};
	const auto RealType = [](const bool bSinglePrecision) {
		FEdGraphPinType Type = MakeType(TEXT("real"));
		Type.PinSubCategory = bSinglePrecision ? TEXT("float") : TEXT("double");
		Type.bSerializeAsSinglePrecisionFloat = bSinglePrecision;
		return Type;
	};
	const auto MapType = [](const TCHAR* ValueCategory) {
		FEdGraphPinType Type = MakeType(TEXT("name"), EPinContainerType::Map);
		Type.PinValueType.TerminalCategory = ValueCategory;
		return Type;
	};

	FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Object"), ObjectType(AActor::StaticClass()));
	FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Wrapped"), ClassType(AActor::StaticClass(), false));
	FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Number"), RealType(false));
	FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Table"), MapType(TEXT("int")));
	const FString Before = SaveCopy(Blueprint, TEXT("before.uasset"));

	FBlueprintEditorUtils::ChangeMemberVariableType(Blueprint, TEXT("Object"), ObjectType(APawn::StaticClass()));
	// The engine compares pin types without the wrapper flag, so ChangeMemberVariableType would see no change: set it on the variable.
	for (FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarName == TEXT("Wrapped"))
		{
			Variable.VarType.bIsUObjectWrapper = true;
		}
	}
	FBlueprintEditorUtils::ChangeMemberVariableType(Blueprint, TEXT("Number"), RealType(true));
	FBlueprintEditorUtils::ChangeMemberVariableType(Blueprint, TEXT("Table"), MapType(TEXT("string")));
	const FString After = SaveCopy(Blueprint, TEXT("after.uasset"));

	FText Error;
	const TSharedPtr<FAssetPackageDocument> OldDocument = FAssetPackageReader::LoadFromFile(Before, Error);
	const TSharedPtr<FAssetPackageDocument> NewDocument = FAssetPackageReader::LoadFromFile(After, Error);
	if (!TestTrue(TEXT("Both versions are written and load"), OldDocument.IsValid() && NewDocument.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FAssetPackageTraceCollection> OldTraces = FAssetPackageFieldDecoder::Decode(*OldDocument);
	const TSharedPtr<FAssetPackageTraceCollection> NewTraces = FAssetPackageFieldDecoder::Decode(*NewDocument);
	const FAssetPackageDiffResult Diff = AssetPackageDiff::Compare(*OldDocument, *NewDocument, OldTraces.Get(), NewTraces.Get());

	// The variables are in the order they were added: element [0] is Object, [1] Wrapped, [2] Number and [3] Table.
	const FAssetPackageDiffEntry* Variables = FindVariables(Diff);
	if (!TestNotNull(TEXT("The variables are in the diff"), Variables))
	{
		return false;
	}

	const auto TypeOf = [Variables](const TCHAR* Element) -> const FAssetPackageDiffEntry* {
		for (const FAssetPackageDiffEntry& Candidate : Variables->Children)
		{
			if (Candidate.DisplayName.ToString() == Element)
			{
				for (const FAssetPackageDiffEntry& Field : Candidate.Children)
				{
					if (Field.DisplayName.ToString() == TEXT("VarType"))
					{
						return &Field;
					}
				}
			}
		}
		return nullptr;
	};

	if (const FAssetPackageDiffEntry* Type = TypeOf(TEXT("[0]")))
	{
		TestEqual(TEXT("An object type names its class"), Type->OldValue, FString(TEXT("object (/Script/Engine.Actor)")));
		TestEqual(TEXT("And the class it changed to"), Type->NewValue, FString(TEXT("object (/Script/Engine.Pawn)")));
	}
	else
	{
		AddError(TEXT("The object variable is not in the diff"));
	}

	if (const FAssetPackageDiffEntry* Type = TypeOf(TEXT("[1]")))
	{
		TestFalse(TEXT("A class type that is not a wrapper says nothing of it"), Type->OldValue.Contains(TEXT("wrapper")));
		TestTrue(TEXT("A class type that became a wrapper says so"), Type->NewValue.Contains(TEXT("object wrapper")));
	}
	else
	{
		AddError(TEXT("The class variable is not in the diff"));
	}

	if (const FAssetPackageDiffEntry* Type = TypeOf(TEXT("[2]")))
	{
		TestFalse(TEXT("A real that is not single precision says nothing of it"), Type->OldValue.Contains(TEXT("single precision")));
		TestTrue(TEXT("A real stored as single precision says so"), Type->NewValue.Contains(TEXT("single precision")));
	}
	else
	{
		AddError(TEXT("The real variable is not in the diff"));
	}

	if (const FAssetPackageDiffEntry* Type = TypeOf(TEXT("[3]")))
	{
		TestEqual(TEXT("A map names the keys and the values"), Type->OldValue, FString(TEXT("map of name to int")));
		TestEqual(TEXT("And what the values changed to"), Type->NewValue, FString(TEXT("map of name to string")));
	}
	else
	{
		AddError(TEXT("The map variable is not in the diff"));
	}

	IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(FlagsFolder), false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetBlueprintVariableType_ShowsARenamedVariableAsOneChange, "AssetSerializationInspector.Serialization.AssetBlueprintVariableType.ShowsARenamedVariableAsOneChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetBlueprintVariableType_ShowsARenamedVariableAsOneChange::RunTest(const FString& Parameters)
{
	using namespace VariableTypeTestUtils;

	static const TCHAR* const RenamePackage = TEXT("/Game/__AssetSerializationInspectorTests/VariableRename/BP_VariableRename");
	static const TCHAR* const RenameFolder = TEXT("/Game/__AssetSerializationInspectorTests/VariableRename");

	IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(RenameFolder), false, true);

	UPackage* Package = CreatePackage(*AssetTestPackages::Unique(RenamePackage));
	UBlueprint* Blueprint =
		FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package, TEXT("BP_VariableRename"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("A Blueprint is created"), Blueprint))
	{
		return false;
	}

	FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Alpha"), MakeType(TEXT("int")));
	FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Beta"), MakeType(TEXT("int")));
	const FString Before = SaveCopy(Blueprint, TEXT("before.uasset"));

	FBlueprintEditorUtils::RenameMemberVariable(Blueprint, TEXT("Beta"), TEXT("Gamma"));
	const FString After = SaveCopy(Blueprint, TEXT("after.uasset"));

	FText Error;
	const TSharedPtr<FAssetPackageDocument> OldDocument = FAssetPackageReader::LoadFromFile(Before, Error);
	const TSharedPtr<FAssetPackageDocument> NewDocument = FAssetPackageReader::LoadFromFile(After, Error);
	if (!TestTrue(TEXT("Both versions are written and load"), OldDocument.IsValid() && NewDocument.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FAssetPackageTraceCollection> OldTraces = FAssetPackageFieldDecoder::Decode(*OldDocument);
	const TSharedPtr<FAssetPackageTraceCollection> NewTraces = FAssetPackageFieldDecoder::Decode(*NewDocument);
	const FAssetPackageDiffResult Diff = AssetPackageDiff::Compare(*OldDocument, *NewDocument, OldTraces.Get(), NewTraces.Get());

	// The GUID of the variable stays; its key in the Blueprint's PropertyGuids is the name, which changed.
	const FAssetPackageDiffEntry* Guids = nullptr;
	for (const FAssetPackageDiffEntry& Entry : Diff.Entries)
	{
		Guids = Guids != nullptr ? Guids : FindNamed(Entry, TEXT("PropertyGuids"));
	}

	if (TestNotNull(TEXT("The property GUIDs are in the diff"), Guids))
	{
		int32 Added = 0;
		int32 Removed = 0;
		int32 Modified = 0;
		for (const FAssetPackageDiffEntry& Child : Guids->Children)
		{
			Added += Child.State == EAssetPackageDiffState::Added ? 1 : 0;
			Removed += Child.State == EAssetPackageDiffState::Removed ? 1 : 0;
			Modified += Child.State == EAssetPackageDiffState::Modified ? 1 : 0;
		}

		TestEqual(TEXT("The rename is not an addition"), Added, 0);
		TestEqual(TEXT("And not a removal"), Removed, 0);
		TestEqual(TEXT("It is one modification"), Modified, 1);

		// The row of the modification shows what changed, without opening it: the old name and the new one.
		const FAssetPackageDiffEntry* Renamed = Guids->Children.FindByPredicate([](const FAssetPackageDiffEntry& Child) { return Child.State == EAssetPackageDiffState::Modified; });
		if (TestNotNull(TEXT("The modified entry is found"), Renamed))
		{
			TestEqual(TEXT("It shows the old name"), Renamed->OldValue, FString(TEXT("Beta")));
			TestEqual(TEXT("And the new one"), Renamed->NewValue, FString(TEXT("Gamma")));
		}
	}

	IFileManager::Get().DeleteDirectory(*FPackageName::LongPackageNameToFilename(RenameFolder), false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
