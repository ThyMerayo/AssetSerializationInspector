// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Compare/AssetSourceControlCompare.h"

#include "HAL/FileManager.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlRevision.h"
#include "ISourceControlState.h"
#include "Misc/Paths.h"
#include "SourceControlOperations.h"

FString FAssetRevisionInfo::ToLabel() const
{
	FString Text = Revision;

	if (!UserName.IsEmpty())
	{
		Text += TEXT("  ") + UserName;
	}

	if (Date.GetTicks() != 0)
	{
		Text += TEXT("  ") + Date.ToString(TEXT("%Y-%m-%d %H:%M"));
	}

	if (!Summary.IsEmpty())
	{
		Text += TEXT("  ") + Summary;
	}

	return Text;
}

bool AssetSourceControlCompare::IsAvailable()
{
	ISourceControlModule& Module = ISourceControlModule::Get();
	return Module.IsEnabled() && Module.GetProvider().IsAvailable();
}

FAssetRevisionInfo AssetSourceControlCompare::Describe(const ISourceControlRevision& Revision, const int32 Index)
{
	FAssetRevisionInfo Info;
	Info.Index = Index;
	Info.Revision = Revision.GetRevision().IsEmpty() ? FString::Printf(TEXT("#%d"), Revision.GetRevisionNumber()) : Revision.GetRevision();
	Info.UserName = Revision.GetUserName();
	Info.Date = Revision.GetDate();
	Info.Action = Revision.GetAction();

	// Descriptions can run to many lines; the first is the subject.
	FString Description = Revision.GetDescription();
	Description.TrimStartAndEndInline();
	int32 LineEnd = INDEX_NONE;
	Info.Summary = Description.FindChar(TEXT('\n'), LineEnd) ? Description.Left(LineEnd).TrimEnd() : Description;
	return Info;
}

TArray<FAssetRevisionInfo> AssetSourceControlCompare::DescribeAll(const TArray<FRevision>& Revisions)
{
	TArray<FAssetRevisionInfo> Result;
	for (int32 Index = 0; Index < Revisions.Num(); ++Index)
	{
		if (Revisions[Index].IsValid())
		{
			Result.Add(Describe(*Revisions[Index], Index));
		}
	}

	return Result;
}

bool AssetSourceControlCompare::FetchHistory(const FString& Filename, TArray<FRevision>& OutRevisions, FString& OutError)
{
	OutRevisions.Reset();

	if (!IsAvailable())
	{
		OutError = TEXT("Source control is not enabled or not connected. Connect to a provider (Perforce, Git, ...) from the Revision Control menu first.");
		return false;
	}

	ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();

	const TSharedRef<FUpdateStatus> UpdateStatus = ISourceControlOperation::Create<FUpdateStatus>();
	UpdateStatus->SetUpdateHistory(true);
	if (Provider.Execute(UpdateStatus, Filename) != ECommandResult::Succeeded)
	{
		OutError = FString::Printf(TEXT("Source control could not get the status of %s."), *FPaths::GetCleanFilename(Filename));
		return false;
	}

	const TSharedPtr<ISourceControlState> State = Provider.GetState(Filename, EStateCacheUsage::Use);
	if (!State.IsValid() || !State->IsSourceControlled())
	{
		OutError = FString::Printf(TEXT("%s is not under source control."), *FPaths::GetCleanFilename(Filename));
		return false;
	}

	for (int32 Index = 0; Index < State->GetHistorySize(); ++Index)
	{
		if (const FRevision Revision = State->GetHistoryItem(Index); Revision.IsValid())
		{
			OutRevisions.Add(Revision);
		}
	}

	if (OutRevisions.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Source control has no earlier revision of %s."), *FPaths::GetCleanFilename(Filename));
		return false;
	}

	return true;
}

bool AssetSourceControlCompare::DownloadRevision(const ISourceControlRevision& Revision, const FString& OriginalFilename, FString& OutFilename, FString& OutError)
{
	// A revision name can hold characters a file name cannot ("#12", a path).
	FString Tag = Revision.GetRevision().IsEmpty() ? FString::FromInt(Revision.GetRevisionNumber()) : Revision.GetRevision();
	for (TCHAR& Char : Tag)
	{
		if (!FChar::IsAlnum(Char))
		{
			Char = TEXT('_');
		}
	}

	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AssetSerializationInspector"), TEXT("Revisions")));
	IFileManager::Get().MakeDirectory(*Folder, true);

	OutFilename = FPaths::Combine(Folder, FString::Printf(TEXT("%s_%s%s"), *FPaths::GetBaseFilename(OriginalFilename), *Tag, *FPaths::GetExtension(OriginalFilename, true)));
	IFileManager::Get().Delete(*OutFilename, false, true, true);

	if (!Revision.Get(OutFilename) || !IFileManager::Get().FileExists(*OutFilename))
	{
		OutError = FString::Printf(TEXT("Source control could not get revision %s of %s."), *Revision.GetRevision(), *FPaths::GetCleanFilename(OriginalFilename));
		return false;
	}

	return true;
}
