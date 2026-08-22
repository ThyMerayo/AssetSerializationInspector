// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "AssetSerializationInspectorStyle.h"
#include "Framework/Commands/Commands.h"

class FAssetSerializationInspectorCommands : public TCommands<FAssetSerializationInspectorCommands>
{
public:
	FAssetSerializationInspectorCommands()
		: TCommands<FAssetSerializationInspectorCommands>(TEXT("AssetSerializationInspector"), NSLOCTEXT("Contexts", "AssetSerializationInspector", "AssetSerializationInspector Plugin"), NAME_None,
			  FAssetSerializationInspectorStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr<FUICommandInfo> OpenPluginWindow;
};