// Copyright Diego Merayo Merayo. All Rights Reserved

#include "AssetSerializationInspectorCommands.h"

#define LOCTEXT_NAMESPACE "FAssetSerializationInspectorModule"

void FAssetSerializationInspectorCommands::RegisterCommands()
{
	UI_COMMAND(OpenPluginWindow, "Asset Serialization Inspector", "Bring up Asset Serialization Inspector window", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
