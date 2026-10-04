// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Serialization/AssetPropertyValueDecoder.h"

enum class EAssetContainerFinalValueConfidence : uint8
{
	/** The serialized data proves how the container was written. */
	Certain,

	/** The serialized data is consistent with a delta, but a full container would look identical. */
	Inferred
};

struct FAssetContainerFinalValue
{
	EAssetContainerFinalValueConfidence Confidence = EAssetContainerFinalValueConfidence::Certain;

	/** The container after applying the serialized data on top of its defaults. Elements carry no container operation. */
	FAssetDecodedPropertyValue Value;

	/** Set when the serialized data turned out to be a complete container rather than a delta. */
	bool bSerializedAsFullContainer = false;
};

namespace AssetContainerFinalValue
{
	/**
	 * Reconstructs the final contents of a set or map that may have been serialized as a delta against its defaults.
	 *
	 * The engine writes containers relative to the archetype's value: FSetProperty and FMapProperty save the elements
	 * removed from the defaults followed by the elements added or changed, so the final value is
	 * (Defaults - removed) + added. A container written without defaults has no removals and lists every element, which
	 * can look like a delta with only additions; the merge tells them apart by whether the serialized elements already
	 * exist in the defaults (a delta never repeats an unchanged default element).
	 *
	 * @param Serialized The decoded set or map exactly as stored in the package.
	 * @param Defaults The already-final container of the archetype, or nullptr when it could not be resolved. Maps stored
	 *                 with the engine's explicit "replace" marker do not need defaults.
	 * @return false with OutError set when the data is not a set or map, defaults are needed but missing, or the delta is
	 *         inconsistent with the defaults (for example it removes an element the defaults do not have).
	 */
	bool Compute(const FAssetDecodedPropertyValue& Serialized, const FAssetDecodedPropertyValue* Defaults, FAssetContainerFinalValue& OutFinal, FString& OutError);

	/**
	 * Whether a container can be reconstructed against an empty default: a set or map with no removals. Removals prove the
	 * defaults were non-empty, so they cannot be assumed away.
	 */
	bool CanAssumeEmptyDefaults(const FAssetDecodedPropertyValue& Serialized);

	/**
	 * Replaces, inside Value, the sets and maps of the struct elements of arrays with their final contents. The elements of an
	 * array are saved against the defaults of their struct, not against the owning object's archetype, and those are empty unless
	 * the container was written with removals (which prove it had defaults, so such containers are left as they are). Sets and
	 * maps outside array elements are not touched. Returns whether anything was replaced; Note says what was assumed.
	 */
	bool ResolveContainersInArrayElements(FAssetDecodedPropertyValue& Value, FString& InOutNote, bool bInsideElement = false);
} // namespace AssetContainerFinalValue
