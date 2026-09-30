// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Serialization/AssetContainerFinalValue.h"

namespace AssetContainerFinalValueTestUtils
{
	static FAssetDecodedPropertyValue MakeScalar(const FString& Value)
	{
		FAssetDecodedPropertyValue Scalar;
		Scalar.Status = EAssetPropertyDecodeStatus::Success;
		Scalar.Kind = EAssetDecodedValueKind::Scalar;
		Scalar.TypeName = TEXT("IntProperty");
		Scalar.Value = Value;
		return Scalar;
	}

	static FAssetDecodedPropertyValue MakeSetElement(const FString& Value, const EAssetDecodedContainerOperation Operation)
	{
		FAssetDecodedPropertyValue Element = MakeScalar(Value);
		Element.ContainerOperation = Operation;
		Element.SemanticKey = FAssetPropertyValueDecoder::BuildSemanticValueKey(Element);
		return Element;
	}

	/** Builds a set. Plain values are added elements; values prefixed with '-' are removals. */
	static FAssetDecodedPropertyValue MakeSet(const TArray<FString>& Values)
	{
		FAssetDecodedPropertyValue Set;
		Set.Status = EAssetPropertyDecodeStatus::Success;
		Set.Kind = EAssetDecodedValueKind::Set;

		for (const FString& Value : Values)
		{
			const bool bRemove = Value.StartsWith(TEXT("-"));
			Set.Children.Add(MakeSetElement(bRemove ? Value.RightChop(1) : Value, bRemove ? EAssetDecodedContainerOperation::Remove : EAssetDecodedContainerOperation::Add));
		}

		return Set;
	}

	static FAssetDecodedPropertyValue MakeMapEntry(const FString& Key, const FString& Value, const EAssetDecodedContainerOperation Operation)
	{
		FAssetDecodedPropertyValue Entry;
		Entry.Status = EAssetPropertyDecodeStatus::Success;
		Entry.Kind = EAssetDecodedValueKind::MapEntry;
		Entry.ContainerOperation = Operation;

		FAssetDecodedPropertyValue KeyValue = MakeScalar(Key);
		KeyValue.Name = TEXT("Key");
		KeyValue.SemanticKey = FAssetPropertyValueDecoder::BuildSemanticValueKey(KeyValue);
		Entry.SemanticKey = KeyValue.SemanticKey;
		Entry.Name = FString::Printf(TEXT("[%s]"), *Entry.SemanticKey);
		Entry.Children.Add(KeyValue);

		if (Operation != EAssetDecodedContainerOperation::Remove)
		{
			FAssetDecodedPropertyValue ValueValue = MakeScalar(Value);
			ValueValue.Name = TEXT("Value");
			Entry.Children.Add(ValueValue);
		}

		return Entry;
	}

	/** Builds a map from "key=value" strings (added or modified) and "-key" strings (removals). */
	static FAssetDecodedPropertyValue MakeMap(const TArray<FString>& Entries, const EAssetDecodedContainerSerializationMode Mode = EAssetDecodedContainerSerializationMode::Unknown)
	{
		FAssetDecodedPropertyValue Map;
		Map.Status = EAssetPropertyDecodeStatus::Success;
		Map.Kind = EAssetDecodedValueKind::Map;
		Map.ContainerMode = Mode;

		for (const FString& Entry : Entries)
		{
			if (Entry.StartsWith(TEXT("-")))
			{
				Map.Children.Add(MakeMapEntry(Entry.RightChop(1), FString(), EAssetDecodedContainerOperation::Remove));
				continue;
			}

			FString Key;
			FString Value;
			Entry.Split(TEXT("="), &Key, &Value);
			const EAssetDecodedContainerOperation Operation =
				Mode == EAssetDecodedContainerSerializationMode::Full ? EAssetDecodedContainerOperation::Replace : EAssetDecodedContainerOperation::AddOrModify;
			Map.Children.Add(MakeMapEntry(Key, Value, Operation));
		}

		return Map;
	}

	static FString Describe(const FAssetContainerFinalValue& Final)
	{
		TArray<FString> Parts;
		for (const FAssetDecodedPropertyValue& Child : Final.Value.Children)
		{
			if (Child.Kind == EAssetDecodedValueKind::MapEntry)
			{
				Parts.Add(FString::Printf(TEXT("%s=%s"), *Child.Children[0].Value, *Child.Children[1].Value));
			}
			else
			{
				Parts.Add(Child.Value);
			}
		}

		return FString::Join(Parts, TEXT(","));
	}
} // namespace AssetContainerFinalValueTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetContainerFinalValue_SetAppliesRemovalsAndAdditions, "AssetSerializationInspector.Serialization.AssetContainerFinalValue.SetAppliesRemovalsAndAdditions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetContainerFinalValue_SetAppliesRemovalsAndAdditions::RunTest(const FString& Parameters)
{
	using namespace AssetContainerFinalValueTestUtils;

	const FAssetDecodedPropertyValue Defaults = MakeSet({ TEXT("1"), TEXT("2"), TEXT("3") });
	const FAssetDecodedPropertyValue Serialized = MakeSet({ TEXT("-2"), TEXT("4") });

	FAssetContainerFinalValue Final;
	FString Error;
	TestTrue(TEXT("The delta applies"), AssetContainerFinalValue::Compute(Serialized, &Defaults, Final, Error));
	TestEqual(TEXT("Defaults minus removals plus additions, keeping default order"), Describe(Final), FString(TEXT("1,3,4")));
	TestEqual(TEXT("Removals prove a delta"), Final.Confidence, EAssetContainerFinalValueConfidence::Certain);
	TestFalse(TEXT("It was not a full container"), Final.bSerializedAsFullContainer);
	TestEqual(TEXT("The result is marked as a complete container"), Final.Value.ContainerMode, EAssetDecodedContainerSerializationMode::Full);
	TestEqual(TEXT("The summary counts the final elements"), Final.Value.Value, FString(TEXT("3 elements")));
	TestEqual(TEXT("Final elements carry no operation"), Final.Value.Children[0].ContainerOperation, EAssetDecodedContainerOperation::None);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetContainerFinalValue_SetDetectsFullContainer, "AssetSerializationInspector.Serialization.AssetContainerFinalValue.SetDetectsFullContainer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetContainerFinalValue_SetDetectsFullContainer::RunTest(const FString& Parameters)
{
	using namespace AssetContainerFinalValueTestUtils;

	const FAssetDecodedPropertyValue Defaults = MakeSet({ TEXT("1"), TEXT("2") });

	// Element 2 repeats a default, which a delta never does, so the set was written without defaults.
	FAssetContainerFinalValue Final;
	FString Error;
	TestTrue(TEXT("A complete set is recognized"), AssetContainerFinalValue::Compute(MakeSet({ TEXT("2"), TEXT("5") }), &Defaults, Final, Error));
	TestEqual(TEXT("The serialized elements are the final value"), Describe(Final), FString(TEXT("2,5")));
	TestTrue(TEXT("It is reported as a full container"), Final.bSerializedAsFullContainer);
	TestEqual(TEXT("Repeating a default proves it"), Final.Confidence, EAssetContainerFinalValueConfidence::Certain);

	// A delta equal to its defaults would not have been serialized, so an empty serialized set is a complete, empty one.
	TestTrue(TEXT("An empty serialized set applies"), AssetContainerFinalValue::Compute(MakeSet({}), &Defaults, Final, Error));
	TestEqual(TEXT("An empty set stays empty"), Describe(Final), FString());
	TestTrue(TEXT("It is reported as a full container"), Final.bSerializedAsFullContainer);
	TestEqual(TEXT("It rests on the assumption that unchanged values are not saved"), Final.Confidence, EAssetContainerFinalValueConfidence::Inferred);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetContainerFinalValue_SetFlagsAmbiguousAdditions, "AssetSerializationInspector.Serialization.AssetContainerFinalValue.SetFlagsAmbiguousAdditions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetContainerFinalValue_SetFlagsAmbiguousAdditions::RunTest(const FString& Parameters)
{
	using namespace AssetContainerFinalValueTestUtils;

	FAssetContainerFinalValue Final;
	FString Error;

	const FAssetDecodedPropertyValue Defaults = MakeSet({ TEXT("1"), TEXT("2") });
	TestTrue(TEXT("Disjoint additions apply"), AssetContainerFinalValue::Compute(MakeSet({ TEXT("7") }), &Defaults, Final, Error));
	TestEqual(TEXT("They are treated as additions to the defaults"), Describe(Final), FString(TEXT("1,2,7")));
	TestEqual(TEXT("A complete set of {7} would look the same, so the result is only inferred"), Final.Confidence, EAssetContainerFinalValueConfidence::Inferred);

	const FAssetDecodedPropertyValue EmptyDefaults = MakeSet({});
	TestTrue(TEXT("Additions to empty defaults apply"), AssetContainerFinalValue::Compute(MakeSet({ TEXT("7") }), &EmptyDefaults, Final, Error));
	TestEqual(TEXT("With empty defaults both readings agree"), Final.Confidence, EAssetContainerFinalValueConfidence::Certain);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetContainerFinalValue_SetRejectsInconsistentInput, "AssetSerializationInspector.Serialization.AssetContainerFinalValue.SetRejectsInconsistentInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetContainerFinalValue_SetRejectsInconsistentInput::RunTest(const FString& Parameters)
{
	using namespace AssetContainerFinalValueTestUtils;

	FAssetContainerFinalValue Final;
	FString Error;
	const FAssetDecodedPropertyValue Defaults = MakeSet({ TEXT("1"), TEXT("2") });

	TestFalse(TEXT("Defaults are required"), AssetContainerFinalValue::Compute(MakeSet({ TEXT("3") }), nullptr, Final, Error));
	TestFalse(TEXT("Removing an element the defaults lack is inconsistent"), AssetContainerFinalValue::Compute(MakeSet({ TEXT("-9") }), &Defaults, Final, Error));
	TestFalse(TEXT("Removing and repeating a default at once is inconsistent"), AssetContainerFinalValue::Compute(MakeSet({ TEXT("-1"), TEXT("2") }), &Defaults, Final, Error));

	const FAssetDecodedPropertyValue DeltaDefaults = MakeSet({ TEXT("-1") });
	TestFalse(TEXT("Defaults must already be final"), AssetContainerFinalValue::Compute(MakeSet({ TEXT("3") }), &DeltaDefaults, Final, Error));

	FAssetDecodedPropertyValue Scalar = MakeScalar(TEXT("1"));
	TestFalse(TEXT("Only containers can be resolved"), AssetContainerFinalValue::Compute(Scalar, &Defaults, Final, Error));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetContainerFinalValue_MapAppliesRemovalsAndOverrides, "AssetSerializationInspector.Serialization.AssetContainerFinalValue.MapAppliesRemovalsAndOverrides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetContainerFinalValue_MapAppliesRemovalsAndOverrides::RunTest(const FString& Parameters)
{
	using namespace AssetContainerFinalValueTestUtils;

	const FAssetDecodedPropertyValue Defaults = MakeMap({ TEXT("1=a"), TEXT("2=b"), TEXT("3=c") });
	const FAssetDecodedPropertyValue Serialized = MakeMap({ TEXT("-2"), TEXT("3=z"), TEXT("4=d") });

	FAssetContainerFinalValue Final;
	FString Error;
	TestTrue(TEXT("The delta applies"), AssetContainerFinalValue::Compute(Serialized, &Defaults, Final, Error));
	TestEqual(TEXT("Removals drop keys, entries override values and new keys are appended"), Describe(Final), FString(TEXT("1=a,3=z,4=d")));
	TestEqual(TEXT("Removals prove a delta"), Final.Confidence, EAssetContainerFinalValueConfidence::Certain);
	TestEqual(TEXT("The summary counts the final entries"), Final.Value.Value, FString(TEXT("3 entries")));

	// Without removals, a changed value still needs the defaults but is only an inferred delta.
	TestTrue(TEXT("A modification without removals applies"), AssetContainerFinalValue::Compute(MakeMap({ TEXT("1=q") }), &Defaults, Final, Error));
	TestEqual(TEXT("The value is overridden in place"), Describe(Final), FString(TEXT("1=q,2=b,3=c")));
	TestEqual(TEXT("A complete map of one entry would look the same"), Final.Confidence, EAssetContainerFinalValueConfidence::Inferred);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetContainerFinalValue_MapDetectsReplaceAndFullContainers, "AssetSerializationInspector.Serialization.AssetContainerFinalValue.MapDetectsReplaceAndFullContainers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetContainerFinalValue_MapDetectsReplaceAndFullContainers::RunTest(const FString& Parameters)
{
	using namespace AssetContainerFinalValueTestUtils;

	FAssetContainerFinalValue Final;
	FString Error;

	// The engine's explicit replace marker needs no defaults.
	const FAssetDecodedPropertyValue Replace = MakeMap({ TEXT("9=x") }, EAssetDecodedContainerSerializationMode::Full);
	TestTrue(TEXT("A replace map applies without defaults"), AssetContainerFinalValue::Compute(Replace, nullptr, Final, Error));
	TestEqual(TEXT("A replace map is its own final value"), Describe(Final), FString(TEXT("9=x")));
	TestEqual(TEXT("The replace marker proves it"), Final.Confidence, EAssetContainerFinalValueConfidence::Certain);
	TestTrue(TEXT("It is reported as a full container"), Final.bSerializedAsFullContainer);

	// An entry identical to a default cannot come from a delta, so the map was written without defaults.
	const FAssetDecodedPropertyValue Defaults = MakeMap({ TEXT("1=a"), TEXT("2=b") });
	TestTrue(TEXT("A complete map is recognized"), AssetContainerFinalValue::Compute(MakeMap({ TEXT("2=b"), TEXT("5=e") }), &Defaults, Final, Error));
	TestEqual(TEXT("The serialized entries are the final value"), Describe(Final), FString(TEXT("2=b,5=e")));
	TestTrue(TEXT("It is reported as a full container"), Final.bSerializedAsFullContainer);

	TestFalse(TEXT("A delta map needs defaults"), AssetContainerFinalValue::Compute(MakeMap({ TEXT("-1") }), nullptr, Final, Error));
	TestFalse(TEXT("Removing a key the defaults lack is inconsistent"), AssetContainerFinalValue::Compute(MakeMap({ TEXT("-9") }), &Defaults, Final, Error));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
