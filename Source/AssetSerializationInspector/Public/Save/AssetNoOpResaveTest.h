// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

class UPackage;
struct FObservedAssetSave;

/**
 * How a package behaves when it is saved again without any edit.
 *
 * The test saves the loaded package twice to temporary files and compares the original file with the first copy and the
 * first copy with the second.
 */
enum class ENoOpResaveVerdict : uint8
{
	/** The first resave is byte-identical to the original file. */
	Stable,

	/** The first resave changes the file (an older save format, for example) but a second resave changes nothing more. */
	NormalizedOnFirstSave,

	/** Resaving keeps changing the file, so something in the save is not deterministic. */
	Unstable
};

struct FNoOpResaveResult
{
	FName PackageName;

	bool bSucceeded = false;
	FText Error;

	ENoOpResaveVerdict Verdict = ENoOpResaveVerdict::Stable;

	/** The original file compared with the first resave. */
	TSharedPtr<FObservedAssetSave> FirstResave;

	/** The first resave compared with the second one. */
	TSharedPtr<FObservedAssetSave> SecondResave;

	/** True when the original file on disk was modified during the test, which should never happen. */
	bool bOriginalFileModified = false;
};

namespace AssetNoOpResaveTest
{
	/** Classifies the outcome from whether each comparison found the files byte-identical. */
	ENoOpResaveVerdict ClassifyVerdict(bool bFirstResaveIdentical, bool bSecondResaveIdentical);

	/**
	 * Runs the test on a loaded package.
	 *
	 * The package is saved to two temporary files under Saved/AssetSerializationInspector/NoOpResave, never to its own
	 * file, and its dirty state is left as it was. The original file is only read. Unsaved edits would be saved into the
	 * copies and show up as differences, so a dirty package is refused.
	 */
	FNoOpResaveResult Run(UPackage* Package);
} // namespace AssetNoOpResaveTest
