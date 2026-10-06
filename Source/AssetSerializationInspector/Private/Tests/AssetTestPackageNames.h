// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

namespace AssetTestPackages
{
	/**
	 * A package name that is new in this editor session. The objects a test creates in a package stay in memory after the test has
	 * deleted its files, and creating the same Blueprint or object in the same package again (a second run of the tests in one editor)
	 * is an assertion in the engine. A name of its own for each run avoids it, and leaves the tests with nothing to clean up in memory.
	 */
	inline FString Unique(const TCHAR* Path)
	{
		return FString::Printf(TEXT("%s_%s"), Path, *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
	}
} // namespace AssetTestPackages
