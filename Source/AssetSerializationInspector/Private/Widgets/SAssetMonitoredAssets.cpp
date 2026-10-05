// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Widgets/SAssetMonitoredAssets.h"

#include "ContentBrowserModule.h"
#include "Framework/Application/SlateApplication.h"
#include "IContentBrowserSingleton.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#include "Save/AssetSaveHistoryManager.h"
#include "Save/AssetSaveObserver.h"

#define LOCTEXT_NAMESPACE "AssetMonitoredAssets"

namespace
{
	const FName MonitoredAssetColumn(TEXT("Asset"));
	const FName MonitoredFolderColumn(TEXT("Folder"));
	const FName MonitoredSavesColumn(TEXT("Saves"));
	const FName MonitoredLastSaveColumn(TEXT("LastSave"));
	const FName MonitoredNoteColumn(TEXT("Note"));
} // namespace

class SAssetMonitoredRow : public SMultiColumnTableRow<TSharedPtr<FAssetMonitoredItem>>
{
public:
	SLATE_BEGIN_ARGS(SAssetMonitoredRow) {}
	SLATE_ARGUMENT(TSharedPtr<FAssetMonitoredItem>, Item)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
	{
		Item = InArgs._Item;
		SMultiColumnTableRow<TSharedPtr<FAssetMonitoredItem>>::Construct(FSuperRowType::FArguments(), OwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == MonitoredAssetColumn)
		{
			return SNew(STextBlock).Text(FText::FromString(Item->Name)).ToolTipText(FText::FromName(Item->PackageName));
		}

		if (ColumnName == MonitoredFolderColumn)
		{
			return SNew(STextBlock).Text(FText::FromString(Item->Folder)).ColorAndOpacity(FSlateColor::UseSubduedForeground());
		}

		if (ColumnName == MonitoredSavesColumn)
		{
			return SNew(STextBlock).Text(Item->SavesRecorded > 0 ? FText::AsNumber(Item->SavesRecorded) : FText::GetEmpty());
		}

		if (ColumnName == MonitoredLastSaveColumn)
		{
			return SNew(STextBlock)
				.Text(Item->SavesRecorded > 0
						? FText::Format(LOCTEXT("LastSaveCell", "{0}: {1}"), FText::FromString(Item->LastSave.ToString(TEXT("%H:%M:%S"))), SAssetMonitoredAssets::GetResultText(Item->LastResult))
						: LOCTEXT("NoSavesYet", "no saves recorded yet"))
				.ColorAndOpacity(Item->SavesRecorded > 0 ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground());
		}

		return SNew(STextBlock).Text(Item->bFileMissing ? LOCTEXT("FileMissing", "no file on disk") : FText::GetEmpty()).ColorAndOpacity(FLinearColor(0.9f, 0.5f, 0.2f));
	}

private:
	TSharedPtr<FAssetMonitoredItem> Item;
};

void SAssetMonitoredAssets::Construct(const FArguments& InArgs)
{
	Manager = InArgs._Manager != nullptr ? InArgs._Manager : &FAssetMonitoringManager::Get();
	OnOpenLastSave = InArgs._OnOpenLastSave;
	History = InArgs._History != nullptr ? InArgs._History : &FAssetSaveHistoryManager::Get();

	ChangedHandle = Manager->OnChanged().AddSP(this, &SAssetMonitoredAssets::Refresh);
	SaveEvent = InArgs._SaveEvent != nullptr ? InArgs._SaveEvent : &FAssetSaveObserver::Get().OnObservedAssetSave();
	SaveHandle = SaveEvent->AddSP(this, &SAssetMonitoredAssets::HandleObservedSave);

	ChildSlot[SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(
			8.0f, 8.0f, 8.0f, 4.0f)[SNew(STextBlock).Text(LOCTEXT("MonitoredHeading", "Assets whose saves are monitored")).Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))]

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 4.0f)[SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(LOCTEXT(
					"MonitoredHelp", "Every save of these assets is compared with the file it replaces and explained. The list is kept in your editor preferences, so it is the same next session."))]

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f)[SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)[SNew(SComboButton)
					.ButtonContent()[SNew(STextBlock).Text(LOCTEXT("AddFolder", "Add Folder..."))]
					.ToolTipText(LOCTEXT("AddFolderTooltip", "Monitor every asset in a content folder and its subfolders."))
					.OnGetMenuContent(this, &SAssetMonitoredAssets::BuildFolderPicker)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)[SNew(SButton)
					.Text(LOCTEXT("Refresh", "Refresh"))
					.ToolTipText(LOCTEXT("RefreshTooltip", "Read the list, the recorded saves and the files on disk again. The list also refreshes by itself when a monitored asset is saved."))
					.OnClicked_Lambda([this]() {
						Refresh();
						return FReply::Handled();
					})]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SAssignNew(StatusText, STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(
				220.0f)[SNew(SSearchBox).HintText(LOCTEXT("SearchMonitored", "Search assets")).OnTextChanged(this, &SAssetMonitoredAssets::HandleSearchChanged)]]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(
					8.0f, 0.0f, 0.0f, 0.0f)[SNew(SBox).WidthOverride(110.0f)[SNew(STextBlock).Text(this, &SAssetMonitoredAssets::GetCountText).ColorAndOpacity(FSlateColor::UseSubduedForeground())]]]

		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8.0f)[SNew(SBorder).Padding(0.0f)[SAssignNew(ListView, SListView<TSharedPtr<FAssetMonitoredItem>>)
				.ListItemsSource(&VisibleItems)
				.SelectionMode(ESelectionMode::Multi)
				.OnGenerateRow(this, &SAssetMonitoredAssets::GenerateRow)
				.OnMouseButtonDoubleClick_Lambda([this](const TSharedPtr<FAssetMonitoredItem> Item) {
					if (Item.IsValid() && Item->SavesRecorded > 0)
					{
						OnOpenLastSave.ExecuteIfBound(Item->PackageName);
					}
				})
				.HeaderRow(SNew(SHeaderRow) + SHeaderRow::Column(MonitoredAssetColumn).DefaultLabel(LOCTEXT("AssetColumn", "Asset")).FillWidth(0.25f)
					+ SHeaderRow::Column(MonitoredFolderColumn).DefaultLabel(LOCTEXT("FolderColumn", "Folder")).FillWidth(0.3f)
					+ SHeaderRow::Column(MonitoredSavesColumn).DefaultLabel(LOCTEXT("SavesColumn", "Saves")).FillWidth(0.08f)
					+ SHeaderRow::Column(MonitoredLastSaveColumn).DefaultLabel(LOCTEXT("LastSaveColumn", "Latest save")).FillWidth(0.25f)
					+ SHeaderRow::Column(MonitoredNoteColumn).DefaultLabel(LOCTEXT("NoteColumn", "Note")).FillWidth(0.12f))]]

		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 8.0f)[SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)[SNew(SButton)
					.Text(LOCTEXT("OpenLastSave", "Open Latest Save"))
					.ToolTipText(LOCTEXT("OpenLastSaveTooltip", "Open the comparison of the selected asset's latest recorded save."))
					.IsEnabled(this, &SAssetMonitoredAssets::CanOpenLastSave)
					.OnClicked(this, &SAssetMonitoredAssets::HandleOpenLastSave)]
			+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SSpacer)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)[SNew(SButton)
					.Text(LOCTEXT("RemoveSelected", "Stop Monitoring Selected"))
					.IsEnabled_Lambda([this]() { return ListView.IsValid() && ListView->GetNumItemsSelected() > 0; })
					.OnClicked(this, &SAssetMonitoredAssets::HandleRemoveSelected)]
			+ SHorizontalBox::Slot().AutoWidth()
				[SNew(SButton).Text(LOCTEXT("RemoveAll", "Stop Monitoring All")).IsEnabled_Lambda([this]() { return !AllItems.IsEmpty(); }).OnClicked(this, &SAssetMonitoredAssets::HandleRemoveAll)]]];

	Refresh();
}

SAssetMonitoredAssets::~SAssetMonitoredAssets()
{
	if (Manager != nullptr)
	{
		Manager->OnChanged().Remove(ChangedHandle);
	}

	if (SaveEvent != nullptr)
	{
		SaveEvent->Remove(SaveHandle);
	}
}

TArray<TSharedPtr<FAssetMonitoredItem>> SAssetMonitoredAssets::BuildItems(const FAssetMonitoringManager& Manager, const FAssetSaveHistoryManager& History)
{
	TArray<TSharedPtr<FAssetMonitoredItem>> Items;

	for (const FName PackageName : Manager.GetMonitoredAssets())
	{
		const TSharedRef<FAssetMonitoredItem> Item = MakeShared<FAssetMonitoredItem>();
		Item->PackageName = PackageName;
		Item->Name = FPackageName::GetShortName(PackageName);
		Item->Folder = FPackageName::GetLongPackagePath(PackageName.ToString());
		Item->bFileMissing = !FPackageName::DoesPackageExist(PackageName.ToString());

		if (const FAssetSaveHistory* Saves = History.FindHistory(PackageName); Saves != nullptr && !Saves->Entries.IsEmpty())
		{
			Item->SavesRecorded = Saves->Entries.Num();
			Item->LastSave = Saves->Entries.Last().Timestamp;
			Item->LastResult = Saves->Entries.Last().ResultKind;
		}

		Items.Add(Item);
	}

	Items.Sort([](const TSharedPtr<FAssetMonitoredItem>& Left, const TSharedPtr<FAssetMonitoredItem>& Right) { return Left->PackageName.LexicalLess(Right->PackageName); });
	return Items;
}

TArray<TSharedPtr<FAssetMonitoredItem>> SAssetMonitoredAssets::Filter(const TArray<TSharedPtr<FAssetMonitoredItem>>& Items, const FString& SearchText)
{
	if (SearchText.IsEmpty())
	{
		return Items;
	}

	return Items.FilterByPredicate([&SearchText](const TSharedPtr<FAssetMonitoredItem>& Item) { return Item->PackageName.ToString().Contains(SearchText, ESearchCase::IgnoreCase); });
}

FText SAssetMonitoredAssets::GetResultText(const EAssetSaveResultKind Kind)
{
	switch (Kind)
	{
		case EAssetSaveResultKind::Identical:
			return LOCTEXT("ResultIdentical", "identical");
		case EAssetSaveResultKind::LayoutOnly:
			return LOCTEXT("ResultLayout", "layout only");
		case EAssetSaveResultKind::MetadataOnly:
			return LOCTEXT("ResultMetadata", "metadata only");
		case EAssetSaveResultKind::SemanticChanges:
			return LOCTEXT("ResultSemantic", "property changes");
		case EAssetSaveResultKind::SemanticAndNativeChanges:
			return LOCTEXT("ResultSemanticNative", "property and native changes");
		case EAssetSaveResultKind::NativeOnlyChanges:
			return LOCTEXT("ResultNative", "native data changes");
	}

	return FText::GetEmpty();
}

TSharedRef<ITableRow> SAssetMonitoredAssets::GenerateRow(TSharedPtr<FAssetMonitoredItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SAssetMonitoredRow, OwnerTable).Item(Item);
}

TSharedRef<SWidget> SAssetMonitoredAssets::BuildFolderPicker()
{
	FPathPickerConfig Config;
	Config.DefaultPath = TEXT("/Game");
	Config.bAllowContextMenu = false;
	Config.bAllowClassesFolder = false;
	Config.OnPathSelected = FOnPathSelected::CreateSP(this, &SAssetMonitoredAssets::HandleFolderPicked);

	return SNew(SBox).WidthOverride(320.0f).HeightOverride(420.0f)[FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().CreatePathPicker(Config)];
}

void SAssetMonitoredAssets::HandleFolderPicked(const FString& Path)
{
	FSlateApplication::Get().DismissAllMenus();

	const int32 Added = Manager->AddMonitoredFolder(Path, true);
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::Format(LOCTEXT("FolderAdded", "{0}: {1} assets added"), FText::FromString(Path), FText::AsNumber(Added)));
	}
}

void SAssetMonitoredAssets::Refresh()
{
	// The items are rebuilt, so the selection is kept by package name.
	TSet<FName> SelectedPackages;
	if (ListView.IsValid())
	{
		for (const TSharedPtr<FAssetMonitoredItem>& Item : ListView->GetSelectedItems())
		{
			SelectedPackages.Add(Item->PackageName);
		}
	}

	AllItems = BuildItems(*Manager, *History);
	VisibleItems = Filter(AllItems, SearchText);

	if (ListView.IsValid())
	{
		ListView->ClearSelection();
		for (const TSharedPtr<FAssetMonitoredItem>& Item : VisibleItems)
		{
			if (SelectedPackages.Contains(Item->PackageName))
			{
				ListView->SetItemSelection(Item, true);
			}
		}

		ListView->RequestListRefresh();
	}
}

void SAssetMonitoredAssets::HandleObservedSave(TSharedPtr<FObservedAssetSave> Save)
{
	// Only the saves of monitored assets are observed, so any save changes a row.
	Refresh();
}

void SAssetMonitoredAssets::HandleSearchChanged(const FText& Text)
{
	SearchText = Text.ToString();
	VisibleItems = Filter(AllItems, SearchText);
	ListView->RequestListRefresh();
}

FReply SAssetMonitoredAssets::HandleRemoveSelected()
{
	TArray<FName> Selected;
	for (const TSharedPtr<FAssetMonitoredItem>& Item : ListView->GetSelectedItems())
	{
		Selected.Add(Item->PackageName);
	}

	Manager->RemoveMonitoredAssets(Selected);
	return FReply::Handled();
}

FReply SAssetMonitoredAssets::HandleRemoveAll()
{
	if (FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(LOCTEXT("ConfirmRemoveAll", "Stop monitoring all {0} assets?"), FText::AsNumber(AllItems.Num()))) == EAppReturnType::Yes)
	{
		Manager->ClearMonitoredAssets();
	}

	return FReply::Handled();
}

bool SAssetMonitoredAssets::CanOpenLastSave() const
{
	if (!ListView.IsValid() || ListView->GetNumItemsSelected() != 1)
	{
		return false;
	}

	return ListView->GetSelectedItems()[0]->SavesRecorded > 0;
}

FReply SAssetMonitoredAssets::HandleOpenLastSave()
{
	if (CanOpenLastSave())
	{
		OnOpenLastSave.ExecuteIfBound(ListView->GetSelectedItems()[0]->PackageName);
	}

	return FReply::Handled();
}

FText SAssetMonitoredAssets::GetCountText() const
{
	return AllItems.Num() == VisibleItems.Num() ? FText::Format(LOCTEXT("MonitoredCount", "{0} assets"), FText::AsNumber(AllItems.Num()))
												: FText::Format(LOCTEXT("MonitoredCountFiltered", "{0} of {1}"), FText::AsNumber(VisibleItems.Num()), FText::AsNumber(AllItems.Num()));
}

#undef LOCTEXT_NAMESPACE
