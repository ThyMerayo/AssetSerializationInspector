// Copyright Diego Merayo Merayo. All Rights Reserved
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "AssetSerializationInspectorSettings.generated.h"

/**
 * The monitored assets are a per-user, per-project editor setting: they are saved to the user's own EditorPerProjectUserSettings
 * (under the project's Saved folder) rather than to a Default*.ini that would be committed with the project. Entries found in a
 * DefaultEditorPerProjectUserSettings.ini are still read as the starting list.
 */
UCLASS(Config = EditorPerProjectUserSettings)
class UAssetSerializationInspectorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Packages (such as /Game/Characters/Hero) whose saves are observed and explained. Edits here take effect immediately. */
	UPROPERTY(Config, EditAnywhere, Category = "Monitoring")
	TArray<FName> MonitoredPackages;
};
