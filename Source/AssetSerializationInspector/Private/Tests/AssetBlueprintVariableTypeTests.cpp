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

		const FString File = FPackageName::LongPackageNameToFilename(BlueprintPackage, FPackageName::GetAssetPackageExtension());
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

	UPackage* Package = CreatePackage(BlueprintPackage);
	UBlueprint* Blueprint =
		FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package, TEXT("BP_VariableType"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("A Blueprint is created"), Blueprint))
	{
		return false;
	}

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

#endif // WITH_DEV_AUTOMATION_TESTS
