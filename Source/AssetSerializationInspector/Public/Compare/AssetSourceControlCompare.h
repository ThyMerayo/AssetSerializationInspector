// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

class ISourceControlRevision;

/** A revision of a file in source control, reduced to what a list needs to show. */
struct FAssetRevisionInfo
{
	/** The position in the history the provider returned, newest first. */
	int32 Index = 0;

	/** The provider's name for the revision: a changelist number, a commit hash, "#12". */
	FString Revision;

	FString UserName;
	FDateTime Date;

	/** The first line of the check-in description. */
	FString Summary;

	/** What the check-in did to the file: "edit", "add", "delete", ... */
	FString Action;

	/** One line for a menu or a list: the revision, who made it, when, and what for. */
	FString ToLabel() const;
};

/** Compares an asset on disk with an earlier revision of it in source control, with any provider the editor is connected to. */
namespace AssetSourceControlCompare
{
	using FRevision = TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe>;

	/** Whether the editor has a source control provider that is connected. */
	bool IsAvailable();

	FAssetRevisionInfo Describe(const ISourceControlRevision& Revision, int32 Index);

	/** Describes every revision, keeping the order they are given in. */
	TArray<FAssetRevisionInfo> DescribeAll(const TArray<FRevision>& Revisions);

	/**
	 * Asks the provider for the history of a file on disk (newest first). Fails, with the reason in OutError, when source control
	 * is not available, the file is not under source control or has no history.
	 */
	bool FetchHistory(const FString& Filename, TArray<FRevision>& OutRevisions, FString& OutError);

	/**
	 * Gets a revision's contents into a temporary file that keeps the extension of the file it belongs to (the package reader
	 * recognizes packages by extension). Returns the path of that file.
	 */
	bool DownloadRevision(const ISourceControlRevision& Revision, const FString& OriginalFilename, FString& OutFilename, FString& OutError);
} // namespace AssetSourceControlCompare
