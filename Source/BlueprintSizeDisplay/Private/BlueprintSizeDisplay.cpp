// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintSizeDisplay.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "BlueprintEditorLibrary.h"
#include "EditorUtilityObject.h"

#define LOCTEXT_NAMESPACE "FBlueprintSizeDisplayModule"

void FBlueprintSizeDisplayModule::StartupModule()
{
	IAssetRegistry* assetRegistry = IAssetRegistry::Get();

	auto run = [assetRegistry]() {
		TArray<FAssetData> outAssetData;
		assetRegistry->GetAssetsByPackageName("/BlueprintSizeDisplay/EUB_BPSizeDisplay", outAssetData);

		FAssetData& assetData = outAssetData[0];
		UObject* asset = assetData.GetAsset();

		UBlueprint* bp = UBlueprintEditorLibrary::GetBlueprintAsset(asset);
		UClass* bpClass = UBlueprintEditorLibrary::GeneratedClass(bp);

		UEditorUtilityObject* eub = NewObject<UEditorUtilityObject>(GetTransientPackage(), bpClass);
		eub->Run();
	};

	bool isLoading = assetRegistry->IsLoadingAssets();
	if (!isLoading) {
		run();
		return;
	}

	assetRegistry->OnFilesLoaded().AddLambda(run);
}

void FBlueprintSizeDisplayModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FBlueprintSizeDisplayModule, BlueprintSizeDisplay)