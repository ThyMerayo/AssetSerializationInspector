// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Serialization/AssetContainerFinalValue.h"
#include "Serialization/AssetPropertyValueDecoder.h"

struct FAssetPackageDocument;
struct FAssetPackageTraceCollection;

enum class EAssetArchetypeValueStatus : uint8
{
	/** A value was found on the archetype chain; OutValue holds the value the property would have without the export's own data. */
	Found,

	/** The whole archetype chain was read but none of it stores the property, so the value comes from C++ defaults. */
	NotSerializedInChain,

	/** The chain leaves the packages that can be read (native class, missing or unreadable file), or is malformed. */
	Unavailable
};

struct FAssetArchetypeValue
{
	/** The inherited value. Containers are already resolved to their final contents. */
	FAssetDecodedPropertyValue Value;

	/** Where the value was found, for display. */
	FString Source;

	/** Inferred when a container's value rests on an assumption, described by Note. */
	EAssetContainerFinalValueConfidence Confidence = EAssetContainerFinalValueConfidence::Certain;
	FString Note;
};

/**
 * Finds the value a property inherits from an export's archetype chain, entirely from package files.
 *
 * An export's archetype is its TemplateIndex: the parent class default object for a class default object, or the template
 * for an instanced subobject. Archetypes in the same package are read directly; archetypes in imported packages are loaded
 * from disk. Containers stored as deltas are resolved through their own archetype first, so the returned value is final.
 *
 * Values that come from native C++ defaults (a /Script/ class, or a property no package in the chain overrides) cannot be
 * recovered from package files alone. If that turns out to matter, the fallback is to reflect the property from the class
 * default object of the running editor (FProperty on UClass::GetDefaultObject()), labelling the value as coming from live
 * reflection rather than from a package, since editor state can differ from what was saved. That fallback is intentionally
 * not implemented here; see the Unavailable and NotSerializedInChain results, which are where it would plug in.
 */
class FAssetArchetypeResolver
{
public:
	/**
	 * @param InRootDocument The package containing the export being analysed.
	 * @param InRootTraces Its decoded property traces. Traces of other packages are decoded on demand.
	 */
	FAssetArchetypeResolver(const FAssetPackageDocument& InRootDocument, const FAssetPackageTraceCollection& InRootTraces);
	~FAssetArchetypeResolver();

	/**
	 * Resolves the value of a top-level property as inherited by an export, before the export's own data is applied.
	 *
	 * @param ExportIndex Export of the root document whose archetype chain is searched.
	 * @param PropertyName The top-level property's name.
	 * @param ArrayIndex The property's static array index.
	 * @param OutValue The inherited value when the result is Found.
	 * @param OutMessage Explains why no value was found.
	 */
	EAssetArchetypeValueStatus ResolveInheritedValue(int32 ExportIndex, const FString& PropertyName, int32 ArrayIndex, FAssetArchetypeValue& OutValue, FString& OutMessage);

	/**
	 * Reconstructs the final contents of a set or map stored on an export, by applying its serialized delta to the value
	 * inherited from the export's archetype chain.
	 *
	 * @param Serialized The container as decoded from the export itself.
	 * @param OutFinal The final container. Confidence is Inferred when it rests on an assumption described by Note.
	 * @param OutMessage Explains why the final value could not be reconstructed.
	 */
	bool ResolveFinalContainerValue(
		int32 ExportIndex, const FString& PropertyName, int32 ArrayIndex, const FAssetDecodedPropertyValue& Serialized, FAssetArchetypeValue& OutFinal, FString& OutMessage);

private:
	struct FLoadedPackage;

	struct FLocation
	{
		const FAssetPackageDocument* Document = nullptr;
		const FAssetPackageTraceCollection* Traces = nullptr;
		int32 ExportIndex = INDEX_NONE;
	};

	EAssetArchetypeValueStatus ResolveEffectiveValue(const FLocation& Location, const FString& PropertyName, int32 ArrayIndex, int32 Depth, FAssetArchetypeValue& OutValue, FString& OutMessage);
	EAssetArchetypeValueStatus ResolveFromArchetype(const FLocation& Location, const FString& PropertyName, int32 ArrayIndex, int32 Depth, FAssetArchetypeValue& OutValue, FString& OutMessage);
	EAssetArchetypeValueStatus ApplyContainerToArchetype(
		const FLocation& Location, const FString& PropertyName, int32 ArrayIndex, int32 Depth, const FAssetDecodedPropertyValue& Serialized, FAssetArchetypeValue& OutValue, FString& OutMessage);
	bool LocateArchetype(const FLocation& Location, FLocation& OutArchetype, FString& OutMessage);
	const FLoadedPackage* FindOrLoadPackage(const FString& PackageName, FString& OutMessage);

	const FAssetPackageDocument& RootDocument;
	const FAssetPackageTraceCollection& RootTraces;

	TMap<FString, TUniquePtr<FLoadedPackage>> LoadedPackages;
};
