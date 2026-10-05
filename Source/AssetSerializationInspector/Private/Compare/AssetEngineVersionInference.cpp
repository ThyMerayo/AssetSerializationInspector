// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Compare/AssetEngineVersionInference.h"

namespace
{
	// Generated from the ObjectVersion.h of every X.Y.Z-release tag of the engine: the latest UE4 and UE5 package file versions of
	// each release, with consecutive releases that share them grouped. Add a row when a release brings a new file version.
	const AssetEngineVersionInference::FRelease Releases[] = {
		{ 342, 0, TEXT("4.0.1"), TEXT("4.0.2") },
		{ 352, 0, TEXT("4.1.0"), TEXT("4.1.1") },
		{ 363, 0, TEXT("4.2.0"), TEXT("4.2.1") },
		{ 382, 0, TEXT("4.3.0"), TEXT("4.3.1") },
		{ 385, 0, TEXT("4.4.0"), TEXT("4.4.3") },
		{ 401, 0, TEXT("4.5.0"), TEXT("4.5.1") },
		{ 413, 0, TEXT("4.6.0"), TEXT("4.6.1") },
		{ 434, 0, TEXT("4.7.0"), TEXT("4.7.6") },
		{ 451, 0, TEXT("4.8.0"), TEXT("4.8.3") },
		{ 482, 0, TEXT("4.9.0"), TEXT("4.10.4") },
		{ 498, 0, TEXT("4.11.0"), TEXT("4.11.2") },
		{ 504, 0, TEXT("4.12.0"), TEXT("4.12.5") },
		{ 505, 0, TEXT("4.13.0"), TEXT("4.13.2") },
		{ 508, 0, TEXT("4.14.0"), TEXT("4.14.3") },
		{ 510, 0, TEXT("4.15.0"), TEXT("4.15.3") },
		{ 513, 0, TEXT("4.16.0"), TEXT("4.17.2") },
		{ 514, 0, TEXT("4.18.0"), TEXT("4.18.3") },
		{ 516, 0, TEXT("4.19.0"), TEXT("4.20.3") },
		{ 517, 0, TEXT("4.21.0"), TEXT("4.23.1") },
		{ 518, 0, TEXT("4.24.0"), TEXT("4.25.4") },
		{ 522, 0, TEXT("4.26.0"), TEXT("4.27.2") },
		{ 522, 1004, TEXT("5.0.0"), TEXT("5.0.3") },
		{ 522, 1008, TEXT("5.1.0"), TEXT("5.1.1") },
		{ 522, 1009, TEXT("5.2.0"), TEXT("5.3.2") },
		{ 522, 1012, TEXT("5.4.0"), TEXT("5.4.4") },
		{ 522, 1013, TEXT("5.5.0"), TEXT("5.5.4") },
		{ 522, 1017, TEXT("5.6.0"), TEXT("5.6.1") },
		{ 522, 1018, TEXT("5.7.0"), TEXT("5.8.3") },
	};

	/** Orders file versions: UE5 packages by their UE5 version, older ones by their UE4 version (UE5 packages come after). */
	int64 Rank(const int32 FileVersionUE4, const int32 FileVersionUE5)
	{
		return FileVersionUE5 > 0 ? 1000000 + FileVersionUE5 : FileVersionUE4;
	}

	FString Describe(const AssetEngineVersionInference::FRelease& Release)
	{
		return FCString::Strcmp(Release.FirstRelease, Release.LastRelease) == 0 ? FString(Release.FirstRelease) : FString::Printf(TEXT("%s to %s"), Release.FirstRelease, Release.LastRelease);
	}
} // namespace

TConstArrayView<AssetEngineVersionInference::FRelease> AssetEngineVersionInference::GetReleases()
{
	return MakeArrayView(Releases);
}

FString AssetEngineVersionInference::Infer(const int32 FileVersionUE4, const int32 FileVersionUE5)
{
	const int64 Wanted = Rank(FileVersionUE4, FileVersionUE5);

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Releases); ++Index)
	{
		const FRelease& Release = Releases[Index];
		const int64 Have = Rank(Release.FileVersionUE4, Release.FileVersionUE5);

		if (Have == Wanted)
		{
			return Describe(Release) + TEXT(" (inferred)");
		}

		if (Have > Wanted)
		{
			// A version no release had: a build between the previous release and this one, or older than every release.
			return Index == 0 ? FString::Printf(TEXT("older than %s (inferred)"), Release.FirstRelease)
							  : FString::Printf(TEXT("between %s and %s (inferred)"), Releases[Index - 1].LastRelease, Release.FirstRelease);
		}
	}

	return FString::Printf(TEXT("newer than %s (inferred)"), Releases[UE_ARRAY_COUNT(Releases) - 1].LastRelease);
}
