# Usage

Where to find things, and how to do the common tasks. For what each analysis means see [Features](Features.md).

## Where things are

**Window menu**: *Window -> Asset Serialization*

| Entry | Opens |
|---|---|
| Asset Serialization Inspector | the inspector window for one package |
| Compare Asset Folders... | a folder comparison ([task](#compare-two-folders)) |
| Show Last Folder Comparison | the results of the last folder comparison |
| Monitored Assets... | the list of monitored assets ([details](#the-monitored-assets-window)) |
| Run No-op Resave Test on Project | the no-op resave test on everything under `/Game` |
| Show Last No-op Resave Results | the results of the last batch run |

**Content Browser, right-click an asset**: the *Asset Serialization* section

| Entry | Does |
|---|---|
| Start Monitoring / Stop Monitoring | turns monitoring on or off for the selected assets |
| Run No-op Resave Test | tests what a save does on its own |
| Compare with Source Control Revision... | diffs the file against an earlier revision |

**Content Browser, right-click a folder**: *Monitor Assets in Folder* and *Run No-op Resave Test on Folder*.

The diff window is *Asset Serialization Diff*, opened from a monitored save, a results window or the buttons above.

## Look at one asset

Open *Asset Serialization Inspector* and select a `.uasset`. The package tree shows the decoded regions and lets you jump between related name map, import map and export map entries.

## Compare two assets

1. Open the diff window and choose the old and the new asset.
2. Press **Compare**.

Only changed entries are shown. Turn on **Show unchanged** for a full structural comparison. Selecting a payload or property shows its old and new data with the matching hex ranges. The search box and the *Added / Removed / Modified / Moved* toggles narrow a large diff, and **Export report...** saves the whole comparison.

### Notes on what the diff shows

- **Name maps** are compared by name, not place by place. A name present in both is *unchanged* or *moved*; only a name in one map alone is *added* or *removed*. Adding a Blueprint variable therefore does not report every later name as different.
- **Name properties** store a place in the name map, so adding a name can change the bytes of a property that still holds the same name. It is reported as modified, with the reason: *The value is the same. A name is stored as its place in the Name Map, and it moved because names were added or removed before it: "NewVar" from Name[11] to Name[12]*. This covers names inside structs, arrays and containers, and shows the first three names that moved.
- **Maps of GUIDs** (a Blueprint's `PropertyGuids`) are compared by GUID as well as by key. Renaming a variable keeps its GUID and changes its key, so it shows as one modification (`Beta` to `Gamma`), not a removal plus an addition. A GUID that is not on exactly one removed and one added entry is not matched. Only arrays merge neighboring removals and additions into a replacement.

## Monitor an asset

Right-click an asset, *Asset Serialization -> Start Monitoring*. The plugin records save-related changes only for monitored assets. *Stop Monitoring* is in the same place.

- The list is a **per-user preference**, saved in the editor's user settings under the project's `Saved` folder (not into a `Default*.ini` that would be committed), and kept across restarts.
- It can also be edited in *Editor Preferences -> Plugins -> Asset Serialization Inspector*; changes apply at once.
- Monitoring follows an asset when it is renamed or moved, and stops when it is deleted.

### The monitored assets window

*Window -> Asset Serialization -> Monitored Assets...* lists every monitored asset with:

- its folder,
- how many saves were recorded in this editor session,
- what the latest save did (identical, layout only, property changes, native data changes, ...),
- a note when the package has no file on disk.

It has a search box and updates by itself when assets are monitored, preferences are edited or a monitored asset is saved (the selection is kept). Buttons and actions:

| Action | Does |
|---|---|
| Refresh | reads everything again, for changes nothing announces (a file deleted outside the editor) |
| Add Folder... | monitors every asset in a content folder and its subfolders (redirectors left out, levels included); also *Monitor Assets in Folder* in the folder's menu |
| Stop Monitoring Selected / All | removes assets in one step (*All* asks first) |
| Open Latest Save, or double-click | opens the comparison of the selected asset's latest recorded save |

## Review a save

When a monitored asset is saved, open its diff.

- **Save Analysis** summarizes semantic changes, layout and relocation changes, and the bytes that remain native or undecoded. Selecting an explanation navigates to the matching diff entry and hex range.
- **Repeated Save Analysis** (after several saves) groups changes by stable property path and shows the value history. Selecting a past transition reopens that save at the property.

## Test what a save does on its own

Right-click an asset or a folder, *Run No-op Resave Test* (or *... on Folder*), or use *Window -> Asset Serialization -> Run No-op Resave Test on Project*.

When the run finishes a notification shows the totals and offers **Save Report...**. For a single asset it offers the diffs of the first and second resave instead. A batch opens a results window.

## Compare two folders

*Window -> Asset Serialization -> Compare Asset Folders...*: pick the folder with the older assets, then the one with the newer ones. The notification offers **Save Report...**, and a results window opens.

## Running headless

A commandlet runs the no-op resave test, the folder comparison and the decode coverage scan on a build machine and writes the same reports as the windows (JSON when the report file ends in `.json`, HTML when it ends in `.html`, text otherwise):

```text
UnrealEditor-Cmd.exe Project.uproject -run=AssetSerializationInspector -Mode=NoOpResave -Path=/Game/Characters,/Game/Props -Report=Saved/NoOp.json -FailOnUnstable
UnrealEditor-Cmd.exe Project.uproject -run=AssetSerializationInspector -Mode=CompareFolders -Old=D:/Before/Content -New=Content -Report=Saved/Compare.txt -FailOnChanges
UnrealEditor-Cmd.exe Project.uproject -run=AssetSerializationInspector -Mode=DecodeCoverage -Folder=Content -Report=Saved/Coverage.txt
```

- `-Path` takes content folders (comma separated, searched recursively), not single assets.
- Relative paths are taken from the project folder, and the report folder is created if needed.
- **Exit code**: 0 when the check ran; 1 when the arguments are wrong or a report could not be written; 2 when a check ran and found what its switch fails on.
- `-FailOnUnstable` fails when an asset is unstable or could not be tested. `-FailOnChanges` fails when any file differs, is in only one folder or could not be compared. Without a switch the run only reports.
- In a Git Bash shell on Windows, prefix the command with `MSYS_NO_PATHCONV=1` so that `/Game/...` is not rewritten as a Windows path.
