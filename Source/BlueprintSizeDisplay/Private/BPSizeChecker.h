#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "AssetManagerEditorModule.h"
#include "ITreeMap.h"
#include "BPSizeChecker.generated.h"

UCLASS(BlueprintType)
class UBPSizeChecker : public UObject
{
	GENERATED_BODY()

private:
	struct FNodeSizeMapData
	{
		/** How big the asset is */
		SIZE_T AssetSize;

		/** Whether it has a known size or not */
		bool bHasKnownSize;

		/** Data from the asset registry about this asset */
		FAssetData AssetData;
	};

	struct FAssetSizeData
	{
		FName PackageName;
		bool IsDirty = true;
		int64 Size = 0;
		int64 InitialSize = 0;
		bool HasKnownSize = false;
	};
	
	IAssetManagerEditorModule* EditorModule = nullptr;
	const FAssetManagerEditorRegistrySource* CurrentRegistrySource = nullptr;
	
	TArray<FAssetIdentifier> RootAssetIdentifiers;

	typedef TMap<TSharedRef<FTreeMapNodeData>, FNodeSizeMapData> FNodeSizeMapDataMap;
	FNodeSizeMapDataMap NodeSizeMapDataMap;

	TMap<FName, FAssetSizeData> FileSizeDataCache;
	
	FName SizeType;

	// This method is a copy of the SSizeMap::GatherDependenciesRecursively one from the engine source code
	void GatherDependenciesRecursively(
		TMap<FAssetIdentifier, TSharedPtr<FTreeMapNodeData>>& VisitedAssetIdentifiers,
		const TArray<FAssetIdentifier>& AssetIdentifiers,
		const FPrimaryAssetId& FilterPrimaryAsset,
		const TSharedPtr<FTreeMapNodeData>& ParentTreeMapNode,
		TSharedPtr<FTreeMapNodeData>& SharedRootNode,
		int32& NumAssetsWhichFailedToLoad);

	// This method is a copy of the SSizeMap::FinalizeNodesRecursively one from the engine source code
	void FinalizeNodesRecursively(
		TSharedPtr<FTreeMapNodeData>& Node,
		const TSharedPtr<FTreeMapNodeData>& SharedRootNode,
		int32& TotalAssetCount,
		SIZE_T& TotalSize,
		bool& bAnyUnknownSizes);

	const FAssetSizeData& CalculateAssetSize(const FName& PackageName, const FName& SizeTypeToCalculate);
	void FormatAssetSize(const FAssetSizeData& SizeData, FString& OutDisplayString);
	
public:
	UFUNCTION(BlueprintCallable)
	void Init();
	
	UFUNCTION(BlueprintCallable)
	void GetAssetSize(const FName& PackageName, const FName& SizeTypeToCalculate, FString& OutSize);

	UFUNCTION(BlueprintCallable)
	UBlueprint* TryExtractBlueprintFromContext(const FToolMenuContext& ToolMenuContext);
};

