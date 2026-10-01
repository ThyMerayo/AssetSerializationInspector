// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Model/AssetPackageHeaderLayout.h"

#include "UObject/PackageFileSummary.h"

#include "Model/AssetPackageDocument.h"

int64 AssetPackageHeaderLayout::GetHeaderSize(const FAssetPackageDocument& Document)
{
	const int64 TotalHeaderSize = Document.PackageSummary.TotalHeaderSize;

	return TotalHeaderSize > 0 && TotalHeaderSize <= Document.GetFileSize() ? TotalHeaderSize : Document.GetFileSize();
}

TArray<FAssetPackageHeaderRegion> AssetPackageHeaderLayout::Build(const FAssetPackageDocument& Document)
{
	const FPackageFileSummary& Summary = Document.PackageSummary;
	const int64 HeaderSize = GetHeaderSize(Document);

	TArray<FAssetPackageHeaderRegion> Tables;

	const auto AddTable = [&](const TCHAR* Key, const FText& Name, const int64 Offset, const int32 Count = INDEX_NONE) {
		// A table outside the header (bulk data, for example) is not part of it, and offset 0 means "absent".
		if (Offset <= 0 || Offset >= HeaderSize)
		{
			return;
		}

		FAssetPackageHeaderRegion& Region = Tables.AddDefaulted_GetRef();
		Region.Key = Key;
		Region.Name = Name;
		Region.Offset = Offset;
		Region.EntryCount = Count;
	};

	AddTable(TEXT("NameMap"), NSLOCTEXT("AssetPackageHeader", "NameMap", "Name map"), Summary.NameOffset, Summary.NameCount);
	AddTable(TEXT("SoftObjectPaths"), NSLOCTEXT("AssetPackageHeader", "SoftObjectPaths", "Soft object paths"), Summary.SoftObjectPathsOffset, Summary.SoftObjectPathsCount);
	AddTable(TEXT("GatherableTextData"), NSLOCTEXT("AssetPackageHeader", "GatherableTextData", "Gatherable text data"), Summary.GatherableTextDataOffset, Summary.GatherableTextDataCount);
	AddTable(TEXT("MetaData"), NSLOCTEXT("AssetPackageHeader", "MetaData", "Metadata"), Summary.MetaDataOffset);
	AddTable(TEXT("ImportMap"), NSLOCTEXT("AssetPackageHeader", "ImportMap", "Import map"), Summary.ImportOffset, Summary.ImportCount);
	AddTable(TEXT("ExportMap"), NSLOCTEXT("AssetPackageHeader", "ExportMap", "Export map"), Summary.ExportOffset, Summary.ExportCount);
	AddTable(TEXT("CellExportMap"), NSLOCTEXT("AssetPackageHeader", "CellExportMap", "Cell export map"), Summary.CellExportOffset, Summary.CellExportCount);
	AddTable(TEXT("CellImportMap"), NSLOCTEXT("AssetPackageHeader", "CellImportMap", "Cell import map"), Summary.CellImportOffset, Summary.CellImportCount);
	AddTable(TEXT("DependsMap"), NSLOCTEXT("AssetPackageHeader", "DependsMap", "Depends map"), Summary.DependsOffset);
	AddTable(
		TEXT("SoftPackageReferences"), NSLOCTEXT("AssetPackageHeader", "SoftPackageReferences", "Soft package references"), Summary.SoftPackageReferencesOffset, Summary.SoftPackageReferencesCount);
	AddTable(TEXT("SearchableNames"), NSLOCTEXT("AssetPackageHeader", "SearchableNames", "Searchable names"), Summary.SearchableNamesOffset);
	AddTable(TEXT("ThumbnailTable"), NSLOCTEXT("AssetPackageHeader", "ThumbnailTable", "Thumbnail table"), Summary.ThumbnailTableOffset);
	AddTable(
		TEXT("ImportTypeHierarchies"), NSLOCTEXT("AssetPackageHeader", "ImportTypeHierarchies", "Import type hierarchies"), Summary.ImportTypeHierarchiesOffset, Summary.ImportTypeHierarchiesCount);
	AddTable(TEXT("AssetRegistryData"), NSLOCTEXT("AssetPackageHeader", "AssetRegistryData", "Asset registry data"), Summary.AssetRegistryDataOffset);
	AddTable(TEXT("WorldTileInfo"), NSLOCTEXT("AssetPackageHeader", "WorldTileInfo", "World tile info"), Summary.WorldTileInfoDataOffset);
	AddTable(TEXT("PreloadDependencies"), NSLOCTEXT("AssetPackageHeader", "PreloadDependencies", "Preload dependencies"), Summary.PreloadDependencyOffset, Summary.PreloadDependencyCount);
	AddTable(TEXT("DataResources"), NSLOCTEXT("AssetPackageHeader", "DataResources", "Data resources"), Summary.DataResourceOffset);

	Tables.StableSort([](const FAssetPackageHeaderRegion& Left, const FAssetPackageHeaderRegion& Right) { return Left.Offset < Right.Offset; });

	TArray<FAssetPackageHeaderRegion> Regions;

	// The summary runs from the start of the file to the first table, or to the end of the header if there are no tables.
	FAssetPackageHeaderRegion& SummaryRegion = Regions.AddDefaulted_GetRef();
	SummaryRegion.Key = TEXT("Summary");
	SummaryRegion.Name = NSLOCTEXT("AssetPackageHeader", "Summary", "Package summary");
	SummaryRegion.Offset = 0;
	SummaryRegion.Size = Tables.IsEmpty() ? HeaderSize : Tables[0].Offset;

	for (int32 Index = 0; Index < Tables.Num(); ++Index)
	{
		FAssetPackageHeaderRegion Region = Tables[Index];

		int64 End = HeaderSize;
		for (int32 NextIndex = Index + 1; NextIndex < Tables.Num(); ++NextIndex)
		{
			if (Tables[NextIndex].Offset > Region.Offset)
			{
				End = Tables[NextIndex].Offset;
				break;
			}
		}

		Region.Size = End - Region.Offset;
		Regions.Add(MoveTemp(Region));
	}

	return Regions;
}
