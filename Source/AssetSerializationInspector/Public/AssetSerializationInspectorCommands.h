// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "Framework/Commands/Commands.h"

#include "AssetSerializationInspectorStyle.h"

class FAssetSerializationInspectorCommands : public TCommands<FAssetSerializationInspectorCommands>
{
public:
	FAssetSerializationInspectorCommands()
		: TCommands<FAssetSerializationInspectorCommands>(TEXT("Asset Serialization Inspector"), NSLOCTEXT("Contexts", "AssetSerializationInspector", "AssetSerializationInspector Plugin"), NAME_None,
			  FAssetSerializationInspectorStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr<FUICommandInfo> OpenPluginWindow;
};