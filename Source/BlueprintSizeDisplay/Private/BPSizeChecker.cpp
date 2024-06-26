#include "BPSizeChecker.h"

#include "BlueprintEditorContext.h"
#include "Engine/AssetManager.h"
#include "ToolMenus.h"

namespace
{
	FString MakeBestSizeString(const SIZE_T SizeInBytes, const bool bHasKnownSize)
	{
		FText SizeText;

		if (SizeInBytes < 1000)
		{
			// We ended up with bytes, so show a decimal number
			SizeText = FText::AsMemory(SizeInBytes, EMemoryUnitStandard::SI);
		}
		else
		{
			// Show a fractional number with the best possible units
			FNumberFormattingOptions NumberFormattingOptions;
			NumberFormattingOptions.MaximumFractionalDigits = 1;	// @todo sizemap: We could make the number of digits customizable in the UI
			NumberFormattingOptions.MinimumFractionalDigits = 0;
			NumberFormattingOptions.MinimumIntegralDigits = 1;

			SizeText = FText::AsMemory(SizeInBytes, &NumberFormattingOptions, nullptr, EMemoryUnitStandard::SI);
		}

		if (!bHasKnownSize)
		{
			if (SizeInBytes == 0)
			{
				SizeText = FText::FromString(TEXT("unknown size"));
			}
			else
			{
				SizeText = FText::Format(FText::FromString(TEXT("at least {0}")), SizeText);
			}
		}

		return SizeText.ToString();
	}
}

void UBPSizeChecker::GatherDependenciesRecursively(
	TMap<FAssetIdentifier, TSharedPtr<FTreeMapNodeData>>& VisitedAssetIdentifiers,
	const TArray<FAssetIdentifier>& AssetIdentifiers,
	const FPrimaryAssetId& FilterPrimaryAsset,
	const TSharedPtr<FTreeMapNodeData>& Node,
	TSharedPtr<FTreeMapNodeData>& SharedRootNode,
	int32& NumAssetsWhichFailedToLoad)
{
	if (!CurrentRegistrySource->HasRegistry())
	{
		return;
	}
	for (const FAssetIdentifier& AssetIdentifier : AssetIdentifiers)
	{
		FName AssetPackageName = AssetIdentifier.IsPackage() ? AssetIdentifier.PackageName : NAME_None;
		FString AssetPackageNameString = (AssetPackageName != NAME_None) ? AssetPackageName.ToString() : FString();
		FPrimaryAssetId AssetPrimaryId = AssetIdentifier.GetPrimaryAssetId();
		int32 ChunkId = UAssetManager::ExtractChunkIdFromPrimaryAssetId(AssetPrimaryId);
		int32 FilterChunkId = UAssetManager::ExtractChunkIdFromPrimaryAssetId(FilterPrimaryAsset);
		const FAssetManagerChunkInfo* FilterChunkInfo = FilterChunkId != INDEX_NONE ? CurrentRegistrySource->ChunkAssignments.Find(FilterChunkId) : nullptr;

		// Only support packages and primary assets
		if (AssetPackageName == NAME_None && !AssetPrimaryId.IsValid())
		{
			continue;
		}

		// Don't bother showing code references
		if (AssetPackageNameString.StartsWith(TEXT("/Script/")))
		{
			continue;
		}

		// Have we already added this asset to the tree?  If so, we'll either move it to a "shared" group or (if it's referenced again by the same
		// root-level asset) ignore it
		if (VisitedAssetIdentifiers.Contains(AssetIdentifier))
		{
			// OK, we've determined that this asset has already been referenced by something else in our tree.  We'll move it to a "shared" group
			// so all of the assets that are referenced in multiple places can be seen together.
			TSharedPtr<FTreeMapNodeData> ExistingNode = VisitedAssetIdentifiers[AssetIdentifier];

			// Is the existing node not already under the "shared" group?  Note that it might still be (indirectly) under
			// the "shared" group, in which case we'll still want to move it up to the root since we've figured out that it is
			// actually shared between multiple assets which themselves may be shared
			if (ExistingNode->Parent != SharedRootNode.Get())
			{
				// Don't bother moving any of the assets at the root level into a "shared" bucket.  We're only trying to best
				// represent the memory used when all of the root-level assets have become loaded.  It's OK if root-level assets
				// are referenced by other assets in the set -- we don't need to indicate they are shared explicitly
				FTreeMapNodeData* ExistingNodeParent = ExistingNode->Parent;
				check(ExistingNodeParent != nullptr);
				const bool bExistingNodeIsAtRootLevel = ExistingNodeParent->Parent == nullptr || RootAssetIdentifiers.Contains(AssetIdentifier);
				if (!bExistingNodeIsAtRootLevel)
				{
					// OK, the current asset (AssetIdentifier) is definitely not a root level asset, but its already in the tree
					// somewhere as a non-shared, non-root level asset.  We need to make sure that this Node's reference is not from the
					// same root-level asset as the ExistingNodeInTree.  Otherwise, there's no need to move it to a 'shared' group.
					FTreeMapNodeData* MyParentNode = Node.Get();
					check(MyParentNode != nullptr);
					FTreeMapNodeData* MyRootLevelAssetNode = MyParentNode;
					while (MyRootLevelAssetNode->Parent != nullptr && MyRootLevelAssetNode->Parent->Parent != nullptr)
					{
						MyRootLevelAssetNode = MyRootLevelAssetNode->Parent;
					}
					if (MyRootLevelAssetNode->Parent == nullptr)
					{
						// No root asset (Node must be a root level asset itself!)
						MyRootLevelAssetNode = nullptr;
					}

					// Find the existing node's root level asset node
					FTreeMapNodeData* ExistingNodeRootLevelAssetNode = ExistingNodeParent;
					while (ExistingNodeRootLevelAssetNode->Parent->Parent != nullptr)
					{
						ExistingNodeRootLevelAssetNode = ExistingNodeRootLevelAssetNode->Parent;
					}

					// If we're being referenced by another node within the same asset, no need to move it to a 'shared' group.  
					if (MyRootLevelAssetNode != ExistingNodeRootLevelAssetNode)
					{
						// This asset was already referenced by something else (or was in our top level list of assets to display sizes for)
						if (!SharedRootNode.IsValid())
						{
							// Find the root-most tree node
							FTreeMapNodeData* RootNode = MyParentNode;
							while (RootNode->Parent != nullptr)
							{
								RootNode = RootNode->Parent;
							}

							SharedRootNode = MakeShareable(new FTreeMapNodeData());
							RootNode->Children.Add(SharedRootNode);
							SharedRootNode->Parent = RootNode;	// Keep back-pointer to parent node
						}

						// Reparent the node that we've now determined to be shared
						ExistingNode->Parent->Children.Remove(ExistingNode);
						SharedRootNode->Children.Add(ExistingNode);
						ExistingNode->Parent = SharedRootNode.Get();
					}
				}
			}
		}
		else
		{
			// This asset is new to us so far!  Let's add it to the tree.  Later as we descend through references, we might find that the
			// asset is referenced by something else as well, in which case we'll pull it out and move it to a "shared" top-level box
			FTreeMapNodeDataRef ChildTreeMapNode = MakeShareable(new FTreeMapNodeData());
			Node->Children.Add(ChildTreeMapNode);
			ChildTreeMapNode->Parent = Node.Get();	// Keep back-pointer to parent node

			VisitedAssetIdentifiers.Add(AssetIdentifier, ChildTreeMapNode);

			FNodeSizeMapData& NodeSizeMapData = NodeSizeMapDataMap.Add(ChildTreeMapNode);

			// Set some defaults for this node.  These will be used if we can't actually locate the asset.
			if (AssetPackageName != NAME_None)
			{
				NodeSizeMapData.AssetData.AssetName = AssetPackageName;
				NodeSizeMapData.AssetData.AssetClassPath = FTopLevelAssetPath(TEXT("/None"), TEXT("MISSING!"));

				const FString AssetPathString = AssetPackageNameString + TEXT(".") + FPackageName::GetLongPackageAssetName(AssetPackageNameString);
				FAssetData FoundData = CurrentRegistrySource->GetAssetByObjectPath(FSoftObjectPath(AssetPathString));

				if (FoundData.IsValid())
				{
					NodeSizeMapData.AssetData = MoveTemp(FoundData);
				}
			}
			else
			{
				NodeSizeMapData.AssetData = IAssetManagerEditorModule::CreateFakeAssetDataFromPrimaryAssetId(AssetPrimaryId);
			}
			
			NodeSizeMapData.AssetSize = 0;
			NodeSizeMapData.bHasKnownSize = false;

			if (NodeSizeMapData.AssetData.IsValid())
			{
				FAssetManagerDependencyQuery DependencyQuery = FAssetManagerDependencyQuery::None();
				if (AssetPackageName != NAME_None)
				{
					DependencyQuery.Categories = UE::AssetRegistry::EDependencyCategory::Package;
					DependencyQuery.Flags = UE::AssetRegistry::EDependencyQuery::Hard;
				}
				else
				{
					DependencyQuery.Categories = UE::AssetRegistry::EDependencyCategory::Manage;
					DependencyQuery.Flags = UE::AssetRegistry::EDependencyQuery::Direct;
				}

/*
				if (GetDefault<USizeMapSettings>()->DependencyType == ESizeMapDependencyType::EditorOnly)
				{
					DependencyQuery.Flags |= UE::AssetRegistry::EDependencyQuery::EditorOnly;
				}
				else if (GetDefault<USizeMapSettings>()->DependencyType == ESizeMapDependencyType::Game)
				{
					DependencyQuery.Flags |= UE::AssetRegistry::EDependencyQuery::Game;
				}
*/
				
//				DependencyQuery.Flags |= UE::AssetRegistry::EDependencyQuery::EditorOnly;
				DependencyQuery.Flags |= UE::AssetRegistry::EDependencyQuery::Game;

				TArray<FAssetIdentifier> References;
				
				if (ChunkId != INDEX_NONE)
				{
					// Look in the platform state
					const FAssetManagerChunkInfo* FoundChunkInfo = CurrentRegistrySource->ChunkAssignments.Find(ChunkId);
					if (FoundChunkInfo)
					{
						References.Append(FoundChunkInfo->ExplicitAssets.Array());
					}
				}
				else
				{
					CurrentRegistrySource->GetDependencies(AssetIdentifier, References, DependencyQuery.Categories, DependencyQuery.Flags);
				}
				
				// Filter for registry source
				IAssetManagerEditorModule::Get().FilterAssetIdentifiersForCurrentRegistrySource(References, DependencyQuery, true);

				TArray<FAssetIdentifier> ReferencedAssetIdentifiers;

				for (FAssetIdentifier& FoundAssetIdentifier : References)
				{
					if (FoundAssetIdentifier.IsPackage())
					{
						FName FoundPackageName = FoundAssetIdentifier.PackageName;

						if (FoundPackageName != NAME_None)
						{
							if (FilterChunkId != INDEX_NONE)
							{
								if (!FilterChunkInfo || !FilterChunkInfo->AllAssets.Contains(FoundPackageName))
								{
									// Not found in the chunk list, skip
									continue;
								}
							} 
							else if (FilterPrimaryAsset.IsValid())
							{
								// Check to see if this is managed by the filter asset
								TArray<FAssetIdentifier> Managers;
								CurrentRegistrySource->GetReferencers(FoundAssetIdentifier, Managers, UE::AssetRegistry::EDependencyCategory::Manage);

								if (!Managers.Contains(FilterPrimaryAsset))
								{
									continue;
								}
							}

							ReferencedAssetIdentifiers.Add(FoundPackageName);
						}
					}
					else
					{
						ReferencedAssetIdentifiers.Add(FoundAssetIdentifier);
					}
				}
				int64 FoundSize = 0;

				if (AssetPackageName != NAME_None)
				{
					if (EditorModule->GetIntegerValueForCustomColumn(NodeSizeMapData.AssetData, SizeType, FoundSize))
					{
						// If we're reading cooked data, this will fail for dependencies that are editor only. This is fine, they will have 0 size
						NodeSizeMapData.AssetSize = FoundSize;
						NodeSizeMapData.bHasKnownSize = true;
					}
				}
				else
				{
					// Virtual node, size is known to be 0
					NodeSizeMapData.bHasKnownSize = true;
				}

				// Now visit all of the assets that we are referencing
				GatherDependenciesRecursively(
					VisitedAssetIdentifiers,
					ReferencedAssetIdentifiers,
					ChunkId != INDEX_NONE ? AssetPrimaryId : FilterPrimaryAsset,
					ChildTreeMapNode,
					SharedRootNode,
					NumAssetsWhichFailedToLoad);
			}
			else
			{
				++NumAssetsWhichFailedToLoad;
			}
		}
	}
}

void UBPSizeChecker::FinalizeNodesRecursively(
	TSharedPtr<FTreeMapNodeData>& Node,
	const TSharedPtr<FTreeMapNodeData>& SharedRootNode,
	int32& TotalAssetCount,
	SIZE_T& TotalSize,
	bool& bAnyUnknownSizes)
{
	// Process children first, so we can get the totals for the root node and shared nodes
	int32 SubtreeAssetCount = 0;
	SIZE_T SubtreeSize = 0;
	bool bAnyUnknownSizesInSubtree = false;
	{
		for (TSharedPtr<FTreeMapNodeData> ChildNode : Node->Children)
		{
			FinalizeNodesRecursively(ChildNode, SharedRootNode, SubtreeAssetCount, SubtreeSize, bAnyUnknownSizesInSubtree);
		}

		TotalAssetCount += SubtreeAssetCount;
		TotalSize += SubtreeSize;
		if (bAnyUnknownSizesInSubtree)
		{
			bAnyUnknownSizes = true;
		}
	}

	if (Node == SharedRootNode)
	{
		Node->Name = FString::Printf(TEXT("%s  (%s)"),
			TEXT("*SHARED*"),
			*MakeBestSizeString(SubtreeSize, !bAnyUnknownSizes));

		// Container nodes are always auto-sized
		Node->Size = 0.0f;
	}
	else if (Node->Parent == nullptr)
	{
		// Tree root is always auto-sized
		Node->Size = 0.0f;
	}
	else
	{
		// Make a copy as the map may get resized
		FNodeSizeMapData NodeSizeMapData = NodeSizeMapDataMap.FindChecked(Node.ToSharedRef());
		FPrimaryAssetId PrimaryAssetId = IAssetManagerEditorModule::ExtractPrimaryAssetIdFromFakeAssetData(NodeSizeMapData.AssetData);

		++TotalAssetCount;
		TotalSize += NodeSizeMapData.AssetSize;

		if (!NodeSizeMapData.bHasKnownSize)
		{
			bAnyUnknownSizes = true;
		}

		// Setup a thumbnail
/*		 
		const FSlateBrush* DefaultThumbnailSlateBrush;
		{
			// For non-class types, use the default based upon the actual asset class
			// This has the side effect of not showing a class icon for assets that don't have a proper thumbnail image available
			bool bIsClassType = false;
			const UClass* ThumbnailClass = FClassIconFinder::GetIconClassForAssetData(NodeSizeMapData.AssetData, &bIsClassType);
			const FName DefaultThumbnail = (bIsClassType) ? NAME_None : FName(*FString::Printf(TEXT("ClassThumbnail.%s"), *NodeSizeMapData.AssetData.AssetClassPath.GetAssetName().ToString()));
			DefaultThumbnailSlateBrush = FClassIconFinder::FindThumbnailForClass(ThumbnailClass, DefaultThumbnail);

			// @todo sizemap urgent: Actually implement rendered thumbnail support, not just class-based background images

			// const int32 ThumbnailSize = 128;	// @todo sizemap: Hard-coded thumbnail size.  Move this elsewhere
			//	TSharedRef<FAssetThumbnail> AssetThumbnail( new FAssetThumbnail( NodeSizeMapData.AssetData, ThumbnailSize, ThumbnailSize, AssetThumbnailPool ) );
			//	ChildTreeMapNode->AssetThumbnail = AssetThumbnail->MakeThumbnailImage();
		}
*/

		if (PrimaryAssetId.IsValid())
		{
			Node->LogicalName = PrimaryAssetId.ToString();
		}
		else
		{
			Node->LogicalName = NodeSizeMapData.AssetData.PackageName.ToString();
		}
		
		if (Node->IsLeafNode())
		{
			Node->CenterText = MakeBestSizeString(NodeSizeMapData.AssetSize, NodeSizeMapData.bHasKnownSize);

			Node->Size = NodeSizeMapData.AssetSize;

			// The STreeMap widget is not expecting zero-sized leaf nodes.  So we make them very small instead.
			if (Node->Size == 0)
			{
				Node->Size = 1;
			}

			// Leaf nodes get a background picture
			//Node->BackgroundBrush = DefaultThumbnailSlateBrush;

			if (PrimaryAssetId.IsValid())
			{
				// "Asset name"
				// "Asset type"
				Node->Name = PrimaryAssetId.ToString();
			}
			else
			{
				// "Asset name"
				// "Asset type"
				Node->Name = NodeSizeMapData.AssetData.AssetName.ToString();
				Node->Name2 = NodeSizeMapData.AssetData.AssetClassPath.ToString();
			}
		}
		else
		{
			// Container nodes are always auto-sized
			Node->Size = 0.0f;

			if (PrimaryAssetId.IsValid())
			{
				Node->Name = FString::Printf(TEXT("%s  (%s)"),
					*PrimaryAssetId.ToString(),
					*MakeBestSizeString(SubtreeSize + NodeSizeMapData.AssetSize, !bAnyUnknownSizesInSubtree && NodeSizeMapData.bHasKnownSize));
			}
			else
			{
				// "Asset name (asset type, size)"
				Node->Name = FString::Printf(TEXT("%s  (%s, %s)"),
					*NodeSizeMapData.AssetData.AssetName.ToString(),
					*NodeSizeMapData.AssetData.AssetClassPath.ToString(),
					*MakeBestSizeString(SubtreeSize + NodeSizeMapData.AssetSize, !bAnyUnknownSizesInSubtree && NodeSizeMapData.bHasKnownSize));
			}

			const bool bNeedsSelfNode = NodeSizeMapData.AssetSize > 0;
			if (bNeedsSelfNode)
			{
				// We have children, so make some space for our own asset's size within our box
				FTreeMapNodeDataRef ChildSelfTreeMapNode = MakeShareable(new FTreeMapNodeData());
				Node->Children.Add(ChildSelfTreeMapNode);
				ChildSelfTreeMapNode->Parent = Node.Get();	// Keep back-pointer to parent node

				// Map the "self" node to the same node data as its parent
				NodeSizeMapDataMap.Add(ChildSelfTreeMapNode, NodeSizeMapData);

				// "*SELF*"
				// "Asset type"
				ChildSelfTreeMapNode->Name = TEXT("*SELF*");
				ChildSelfTreeMapNode->Name2 = NodeSizeMapData.AssetData.AssetClassPath.ToString();

				ChildSelfTreeMapNode->CenterText = MakeBestSizeString(NodeSizeMapData.AssetSize, NodeSizeMapData.bHasKnownSize);
				ChildSelfTreeMapNode->Size = NodeSizeMapData.AssetSize;

				// Leaf nodes get a background picture
				//ChildSelfTreeMapNode->BackgroundBrush = DefaultThumbnailSlateBrush;
			}
		}

	}

	// Sort all of my child nodes alphabetically.  This is just so that we get deterministic results when viewing the
	// same sets of assets.
	Node->Children.StableSort(
		[](const FTreeMapNodeDataPtr& A, const FTreeMapNodeDataPtr& B)
	{
		return A->Name < B->Name;
	}
	);
}

const UBPSizeChecker::FAssetSizeData& UBPSizeChecker::CalculateAssetSize(const FName& PackageName, const FName& SizeTypeToCalculate)
{
	bool sizeDataJustCreated = false;
	if (!FileSizeDataCache.Contains(PackageName))
	{
		FileSizeDataCache.Add(PackageName, FAssetSizeData {
			PackageName,
			true,
			0,
			0,
			false
		});

		sizeDataJustCreated = true;
	}

	FAssetSizeData& sizeData = FileSizeDataCache[PackageName];

	if (!sizeData.IsDirty)
	{
		return sizeData;
	}
	
	TSharedPtr<FTreeMapNodeData> RootTreeMapNode = MakeShareable<FTreeMapNodeData>(new FTreeMapNodeData());
	RootAssetIdentifiers.Empty();
	NodeSizeMapDataMap.Empty();

	RootAssetIdentifiers.Add(PackageName);

	SizeType = SizeTypeToCalculate;

	// First, do a pass to gather asset dependencies and build up a tree
	TMap<FAssetIdentifier, TSharedPtr<FTreeMapNodeData>> VisitedAssetIdentifiers;
	TSharedPtr<FTreeMapNodeData> SharedRootNode;
	int32 NumAssetsWhichFailedToLoad = 0;
	GatherDependenciesRecursively(VisitedAssetIdentifiers, RootAssetIdentifiers, FPrimaryAssetId(), RootTreeMapNode, SharedRootNode, NumAssetsWhichFailedToLoad);

	// Next, do another pass over our tree to and count how big the assets are and to set the node labels.  Also in this pass, we may
	// create some additional "self" nodes for assets that have children but also take up size themselves.
	int32 TotalAssetCount = 0;
	SIZE_T TotalSize = 0;
	bool bAnyUnknownSizes = false;
	FinalizeNodesRecursively(RootTreeMapNode, SharedRootNode, TotalAssetCount, TotalSize, bAnyUnknownSizes);

	sizeData.IsDirty = false;
	sizeData.Size = static_cast<int64>(TotalSize);
	sizeData.HasKnownSize = !bAnyUnknownSizes;

	if (sizeDataJustCreated)
	{
		sizeData.InitialSize = sizeData.Size;
	}

	return sizeData;

	//
}

void UBPSizeChecker::FormatAssetSize(const FAssetSizeData& SizeData, FString& OutDisplayString)
{
	OutDisplayString = MakeBestSizeString(SizeData.Size, SizeData.HasKnownSize);

	if (SizeData.Size != SizeData.InitialSize)
	{
		int64 sizeDiff = SizeData.Size - SizeData.InitialSize;
		
		FString tmp = MakeBestSizeString(abs(sizeDiff), true);
		OutDisplayString += FString::Format(TEXT(" ({0}{1})"), {sizeDiff >= 0 ? TEXT("+") : TEXT("-"), tmp});
	}
}

void UBPSizeChecker::GetAssetSize(const FName& PackageName, const FName& SizeTypeToCalculate, FString& OutSize)
{
	const FAssetSizeData& sizeData = CalculateAssetSize(PackageName, SizeTypeToCalculate);
	FormatAssetSize(sizeData, OutSize);
}

void UBPSizeChecker::Init()
{
	if (!EditorModule)
	{
		EditorModule = &IAssetManagerEditorModule::Get();
	}

	if (!CurrentRegistrySource)
	{
		CurrentRegistrySource = EditorModule->GetCurrentRegistrySource(true);
	}
	
	IAssetRegistry* assetRegistry = IAssetRegistry::Get();
	
	assetRegistry->OnAssetUpdatedOnDisk().AddLambda([this](const FAssetData& assetData)
	{
		const FName& packageName = assetData.PackageName;
		if (!FileSizeDataCache.Contains(packageName))
		{
			return;
		}

		FAssetSizeData& cachedValue = FileSizeDataCache[packageName];
		cachedValue.IsDirty = true;
	});
}

UBlueprint* UBPSizeChecker::TryExtractBlueprintFromContext(const FToolMenuContext& ToolMenuContext)
{
	UObject* obj = ToolMenuContext.FindByClass(UBlueprintEditorToolMenuContext::StaticClass());
	if (!obj)
	{
		return nullptr;
	}
	UBlueprintEditorToolMenuContext* ctx = Cast<UBlueprintEditorToolMenuContext>(obj);

	if (!ctx)
	{
		return nullptr;
	}

	UBlueprint* bp = ctx->GetBlueprintObj();
	return bp;
}