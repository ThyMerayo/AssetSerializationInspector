// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "AssetInspectorCommandlet.generated.h"

/**
 * Runs the plugin's batch checks without the editor UI, for build machines:
 *
 *   UnrealEditor-Cmd.exe Project.uproject -run=AssetSerializationInspector -Mode=NoOpResave -Path=/Game/Characters,/Game/Props -Report=Saved/NoOp.json -FailOnUnstable
 *   UnrealEditor-Cmd.exe Project.uproject -run=AssetSerializationInspector -Mode=CompareFolders -Old=D:/Before/Content -New=Content -Report=Saved/Compare.txt -FailOnChanges
 *   UnrealEditor-Cmd.exe Project.uproject -run=AssetSerializationInspector -Mode=DecodeCoverage -Folder=Content -Report=Saved/Coverage.txt
 *
 * See AssetInspectorCommandlet::Execute for the arguments and exit codes.
 */
UCLASS()
class UAssetSerializationInspectorCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UAssetSerializationInspectorCommandlet();

	virtual int32 Main(const FString& Params) override;
};

namespace AssetInspectorCommandlet
{
	/** The exit code of a run that did what was asked and, when a "fail on" switch is given, found nothing to fail on. */
	constexpr int32 ExitOk = 0;

	/** The arguments are wrong, or a check or the report could not be run or written. */
	constexpr int32 ExitError = 1;

	/** The check ran and found what the "fail on" switch asks to fail on. */
	constexpr int32 ExitFindings = 2;

	/**
	 * Runs the check named by -Mode and returns the exit code.
	 *
	 *   -Mode=NoOpResave      -Path=<content path>[,<content path>...]  [-Report=<file>] [-FailOnUnstable]
	 *       Saves every asset twice to temporary files and reports what the saves change. -FailOnUnstable also fails on assets
	 *       that could not be tested.
	 *   -Mode=CompareFolders  -Old=<folder> -New=<folder>  [-Report=<file>] [-FailOnChanges]
	 *       Compares the .uasset and .umap files of two folders on disk. -FailOnChanges fails when any file differs, is in
	 *       only one folder or could not be compared.
	 *   -Mode=DecodeCoverage  -Folder=<folder>  [-Report=<file>]
	 *       Reports what the property decoder cannot read.
	 *
	 * Relative paths are taken from the project folder. The report format follows the extension of -Report (.json or .html, otherwise text). Exit codes: ExitOk, ExitError, ExitFindings.
	 */
	int32 Execute(const FString& Params);
} // namespace AssetInspectorCommandlet
