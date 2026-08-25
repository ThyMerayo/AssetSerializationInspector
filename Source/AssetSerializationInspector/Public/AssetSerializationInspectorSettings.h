// Copyright Diego Merayo Merayo. All Rights Reserved
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "AssetSerializationInspectorSettings.generated.h"

UCLASS(Config = EditorPerProjectUserSettings, DefaultConfig)
class UAssetSerializationInspectorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "Monitoring")
	TArray<FName> MonitoredPackages;
};