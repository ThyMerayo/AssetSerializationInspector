// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

/**
 * Names the engine release a package was saved by when the package does not say. Packages saved by an engine built from source
 * carry no version, but they always carry the package file version, which only changes when the engine's serialization does,
 * so it narrows the engine down to the releases that shipped that version.
 *
 * The table comes from the object versions of the engine's release tags (ObjectVersion.h), from 4.0 to 5.8.
 */
namespace AssetEngineVersionInference
{
	/** One package file version and the releases that had it as their latest. */
	struct FRelease
	{
		int32 FileVersionUE4;

		/** Zero for the UE4 releases. */
		int32 FileVersionUE5;

		/** The first and the last release with these file versions, such as "5.2.0" and "5.3.2". */
		const TCHAR* FirstRelease;
		const TCHAR* LastRelease;
	};

	/** The releases, oldest first. */
	TConstArrayView<FRelease> GetReleases();

	/**
	 * What the file version says about the engine, such as "5.4.0 to 5.4.4 (inferred)". A file version no release had is placed
	 * between the two releases around it ("between 5.3.2 and 5.4.0 (inferred)", a build in between), or beyond the last or the first.
	 * The result is never empty.
	 */
	FString Infer(int32 FileVersionUE4, int32 FileVersionUE5);
} // namespace AssetEngineVersionInference
