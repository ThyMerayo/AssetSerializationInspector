// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

#include "Coverage/AssetDecoderBlindSpots.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAssetDecoderBlindSpots_ScansTheFixtures, "AssetSerializationInspector.Coverage.AssetDecoderBlindSpots.ScansTheFixtures", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FAssetDecoderBlindSpots_ScansTheFixtures::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AssetSerializationInspector"));
	if (!TestTrue(TEXT("The plugin is found"), Plugin.IsValid()))
	{
		return false;
	}

	const FString Folder = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("TestFixtures"));

	int32 Calls = 0;
	const FAssetDecoderBlindSpotResult Result = AssetDecoderBlindSpots::Run(Folder, 256, 5, [&Calls](int32, int32) {
		++Calls;
		return true;
	});

	TestEqual(TEXT("Both fixture Blueprints are scanned"), Result.AssetsScanned, 2);
	TestEqual(TEXT("The scan asked before each file"), Calls, 2);
	TestTrue(TEXT("Properties were tested"), Result.PropertiesTested > 0);
	TestTrue(TEXT("Bytes were changed one at a time"), Result.BytesTested > 0);
	TestTrue(TEXT("The bytes that changed nothing are a part of those tested"), Result.BlindBytes >= 0 && Result.BlindBytes <= Result.BytesTested);

	int64 Listed = 0;
	for (const FAssetDecoderBlindSpot& Spot : Result.Spots)
	{
		TestTrue(*FString::Printf(TEXT("%s lists only types with blind bytes"), *Spot.TypeName), Spot.BlindBytes > 0);
		Listed += Spot.BlindBytes;
	}
	TestEqual(TEXT("The types listed account for every blind byte"), Listed, Result.BlindBytes);

	TestTrue(TEXT("The report says what it is"), AssetDecoderBlindSpots::ToText(Result).StartsWith(TEXT("Asset Serialization Inspector: decoder blind spots")));

	// The scan changes the bytes of its own copy and puts each one back: a second scan sees the same.
	const FAssetDecoderBlindSpotResult Again = AssetDecoderBlindSpots::Run(Folder, 256, 5, [](int32, int32) { return true; });
	TestEqual(TEXT("A second scan finds the same blind bytes"), Again.BlindBytes, Result.BlindBytes);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
