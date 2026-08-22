// Copyright Diego Merayo Merayo. All Rights Reserved

#include "AssetSerializationInspectorStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Slate/SlateGameResources.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FAssetSerializationInspectorStyle::StyleInstance = nullptr;

void FAssetSerializationInspectorStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FAssetSerializationInspectorStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FAssetSerializationInspectorStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("AssetSerializationInspectorStyle"));
	return StyleSetName;
}

const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);

TSharedRef<FSlateStyleSet> FAssetSerializationInspectorStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet("AssetSerializationInspectorStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("AssetSerializationInspector")->GetBaseDir() / TEXT("Resources"));

	Style->Set("AssetSerializationInspector.OpenPluginWindow", new IMAGE_BRUSH_SVG(TEXT("PlaceholderButtonIcon"), Icon20x20));

	return Style;
}

void FAssetSerializationInspectorStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FAssetSerializationInspectorStyle::Get()
{
	return *StyleInstance;
}
