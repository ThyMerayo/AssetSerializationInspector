// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Model/AssetPackageDocument.h"

int64 FAssetPackageDocument::GetFileSize() const
{
	return FileData.Num();
}

bool FAssetPackageDocument::IsValidRange(const int64 Offset, const int64 Size) const
{
	if (Offset < 0 || Size < 0)
	{
		return false;
	}

	const int64 FileSize = FileData.Num();

	if (Offset > FileSize)
	{
		return false;
	}

	return Size <= FileSize - Offset;
}

const FAssetPackageNameEntry* FAssetPackageDocument::FindNameEntry(const int32 NameIndex) const
{
	return NameMap.IsValidIndex(NameIndex) ? &NameMap[NameIndex] : nullptr;
}

FString FAssetPackageDocument::ResolveNameIndex(const int32 NameIndex) const
{
	const FAssetPackageNameEntry* Entry = FindNameEntry(NameIndex);

	return Entry ? Entry->Name : FString::Printf(TEXT("<invalid name %d>"), NameIndex);
}

FString FAssetPackageDocument::ResolveNameReference(const FAssetPackageNameReference& Reference) const
{
	if (!Reference.IsValid(NameMap.Num()))
	{
		return FString::Printf(TEXT("<invalid name index %d>"), Reference.NameIndex);
	}

	FString Result = NameMap[Reference.NameIndex].Name;

	/*
	 * FName's internal number is offset by one:
	 *
	 * Number == 0  -> Name
	 * Number == 1  -> Name_0
	 * Number == 2  -> Name_1
	 */
	if (Reference.Number > 0)
	{
		Result += FString::Printf(TEXT("_%d"), Reference.Number - 1);
	}

	return Result;
}

FString FAssetPackageDocument::DescribePackageIndex(const FAssetPackageIndexReference& Reference) const
{
	switch (Reference.GetKind())
	{
		case EAssetPackageIndexKind::Null:
			return TEXT("Null");

		case EAssetPackageIndexKind::Import:
			return FString::Printf(TEXT("Import[%d]"), Reference.GetArrayIndex());

		case EAssetPackageIndexKind::Export:
			return FString::Printf(TEXT("Export[%d]"), Reference.GetArrayIndex());

		default:
			return TEXT("<invalid package index>");
	}
}

FString FAssetPackageDocument::ResolveImportPathInternal(const int32 ImportIndex, TSet<int32>& VisitedImports) const
{
	if (!ImportMap.IsValidIndex(ImportIndex))
	{
		return FString::Printf(TEXT("<invalid import %d>"), ImportIndex);
	}

	if (VisitedImports.Contains(ImportIndex))
	{
		return FString::Printf(TEXT("<cyclic import %d>"), ImportIndex);
	}

	VisitedImports.Add(ImportIndex);

	const FAssetPackageImportEntry& Import = ImportMap[ImportIndex];

	const FString ObjectName = ResolveNameReference(Import.ObjectName);

	FString Result;

	switch (Import.OuterIndex.GetKind())
	{
		case EAssetPackageIndexKind::Null:
			Result = ObjectName;
			break;

		case EAssetPackageIndexKind::Import:
		{
			const int32 OuterImportIndex = Import.OuterIndex.GetArrayIndex();

			const FString OuterPath = ResolveImportPathInternal(OuterImportIndex, VisitedImports);

			if (OuterPath.IsEmpty())
			{
				Result = ObjectName;
			}
			else
			{
				Result = OuterPath + TEXT(".") + ObjectName;
			}

			break;
		}

		case EAssetPackageIndexKind::Export:
			Result = FString::Printf(TEXT("Export[%d].%s"), Import.OuterIndex.GetArrayIndex(), *ObjectName);
			break;

		default:
			Result = ObjectName;
			break;
	}

	VisitedImports.Remove(ImportIndex);

	return Result;
}

FString FAssetPackageDocument::ResolveImportPath(const int32 ImportIndex) const
{
	TSet<int32> VisitedImports;
	return ResolveImportPathInternal(ImportIndex, VisitedImports);
}

FString FAssetPackageDocument::DescribePackageIndexDetailed(const FAssetPackageIndexReference& Reference) const
{
	switch (Reference.GetKind())
	{
		case EAssetPackageIndexKind::Null:
			return TEXT("Null");

		case EAssetPackageIndexKind::Import:
		{
			const int32 Index = Reference.GetArrayIndex();
			return ImportMap.IsValidIndex(Index) ? FString::Printf(TEXT("Import[%d] %s"), Index, *ResolveImportPath(Index)) : FString::Printf(TEXT("<invalid import %d>"), Index);
		}

		case EAssetPackageIndexKind::Export:
		{
			const int32 Index = Reference.GetArrayIndex();
			return ExportMap.IsValidIndex(Index) ? FString::Printf(TEXT("Export[%d] %s"), Index, *ResolveExportPath(Index)) : FString::Printf(TEXT("<invalid export %d>"), Index);
		}

		default:
			return TEXT("<invalid package index>");
	}
}

FString FAssetPackageDocument::ResolveExportPathInternal(const int32 ExportIndex, TSet<int32>& VisitedExports) const
{
	if (!ExportMap.IsValidIndex(ExportIndex))
	{
		return FString::Printf(TEXT("<invalid export %d>"), ExportIndex);
	}

	if (VisitedExports.Contains(ExportIndex))
	{
		return FString::Printf(TEXT("<cyclic export %d>"), ExportIndex);
	}

	VisitedExports.Add(ExportIndex);

	const FAssetPackageExportEntry& Export = ExportMap[ExportIndex];

	const FString ObjectName = ResolveNameReference(Export.ObjectName);

	FString Result;

	switch (Export.OuterIndex.GetKind())
	{
		case EAssetPackageIndexKind::Null:
		{
			const FString Package = PackageSummary.PackageName;
			Result = Package.IsEmpty() ? ObjectName : Package + TEXT(".") + ObjectName;
			break;
		}

		case EAssetPackageIndexKind::Import:
		{
			const int32 OuterIndex = Export.OuterIndex.GetArrayIndex();
			const FString OuterPath = ResolveImportPath(OuterIndex);
			Result = OuterPath.IsEmpty() ? ObjectName : OuterPath + TEXT(".") + ObjectName;
			break;
		}

		case EAssetPackageIndexKind::Export:
		{
			const int32 OuterIndex = Export.OuterIndex.GetArrayIndex();
			const FString OuterPath = ResolveExportPathInternal(OuterIndex, VisitedExports);
			Result = OuterPath.IsEmpty() ? ObjectName : OuterPath + TEXT(".") + ObjectName;
			break;
		}
	}

	VisitedExports.Remove(ExportIndex);

	return Result;
}

FString FAssetPackageDocument::ResolveExportPath(const int32 ExportIndex) const
{
	TSet<int32> VisitedExports;
	return ResolveExportPathInternal(ExportIndex, VisitedExports);
}

bool FAssetPackageDocument::IsValidExportPayload(const FAssetPackageExportEntry& Export) const
{
	if (Export.SerialSize < 0 || Export.SerialOffset < 0)
	{
		return false;
	}

	if (Export.SerialSize == 0)
	{
		return true;
	}

	return IsValidRange(Export.SerialOffset, Export.SerialSize);
}

bool FAssetPackageDocument::IsCoreUObjectClassImport(const int32 ImportIndex) const
{
	if (!ImportMap.IsValidIndex(ImportIndex))
	{
		return false;
	}

	const FAssetPackageImportEntry& Import = ImportMap[ImportIndex];

	const FString ObjectName = ResolveNameReference(Import.ObjectName);

	if (ObjectName != TEXT("Class"))
	{
		return false;
	}

	const FString Path = ResolveImportPath(ImportIndex);

	return Path == TEXT("/Script/CoreUObject.Class");
}

bool FAssetPackageDocument::IsExportUClass(const int32 ExportIndex) const
{
	if (!ExportMap.IsValidIndex(ExportIndex))
	{
		return false;
	}

	const FAssetPackageExportEntry& Export = ExportMap[ExportIndex];

	if (Export.ClassIndex.GetKind() != EAssetPackageIndexKind::Import)
	{
		return false;
	}

	return IsCoreUObjectClassImport(Export.ClassIndex.GetArrayIndex());
}
