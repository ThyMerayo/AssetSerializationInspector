# Usage

The windows, menus and the commandlet. For what each analysis means see [Features](Features.md).

## Inspecting an asset file

Open the Asset Serialization Inspector window (Window -> Asset Serialization -> Asset Serialization Inspector) and select a `.uasset`.

The package tree exposes decoded regions and allows navigating between related Name Map, Import Map, and Export Map entries.

The two Name Maps are compared by name, not place by place: a new name shifts every name after it by one place, so a name that is in both maps is *unchanged* or *moved* (with where it came from), only a name that is in one map alone is *added* or *removed*. Adding a variable to a Blueprint therefore adds its names and moves the rest, instead of reporting every later name, including the other variables', as a different name.

A Name property is stored as its place in the Name Map, so adding a variable (and with it a name) can change the bytes of a Name property that holds the same name as before. The property is still reported as modified, because its bytes did change, but the diff now says why: *The value is the same. A name is stored as its place in the Name Map, and it moved because names were added or removed before it: "NewVar" from Name[11] to Name[12]*. The hint covers names inside structs, arrays and other containers, and shows the first three names that moved.

A map whose values are GUIDs (a Blueprint's `PropertyGuids`: the name of each variable and the GUID that identifies it) is compared by GUID as well as by key. Renaming a variable keeps its GUID and changes its key, so the entry that went and the entry that came, with the same non-zero GUID on one of each, are shown as one modification of the key (`Beta` to `Gamma`), not as a removal and an addition. A GUID that is not on exactly one removed and one added entry is not matched, and the entries of a map or a set that merely sit next to each other are no longer merged into a replacement (that is only done for arrays).

## Comparing two assets

Open the Asset Serialization Diff window and select:

- Old asset
- New asset

Press **Compare**.

The tree shows only changed entries by default. Enable **Show unchanged** when a complete structural comparison is needed.

Selecting a payload or property displays its old/new data and corresponding hex ranges.

## Monitoring an asset

From the Content Browser:

```text
Right-click asset
    -> Asset Serialization Inspector
        -> Start Monitoring
```

The plugin records save-related package changes only for monitored assets.

Monitoring can later be disabled from the same context menu.

The list of monitored assets is a per-user preference: it is saved in the editor's user settings for the project (under the project's `Saved` folder, not into a `Default*.ini` that would be committed) and is still there after restarting the editor. It can also be edited in *Editor Preferences -> Plugins -> Asset Serialization Inspector*, and the change applies at once. Monitoring follows an asset when it is renamed or moved, and stops when it is deleted.

Use the search box and the *Added / Removed / Modified / Moved* toggles to narrow a large diff. **Export report...** saves the whole comparison.

## The monitored assets window

*Window -> Asset Serialization -> Monitored Assets...* lists every monitored asset with its folder, how many saves were recorded for it in this editor session, what the latest one did (identical, layout only, property changes, native data changes, ...) and a note when the package has no file on disk. The list has a search box and updates by itself when assets are monitored from the Content Browser, the editor preferences are edited or a monitored asset is saved (the selection is kept); *Refresh* reads everything again, for changes nothing announces, such as a file deleted outside the editor. *Add Folder...* opens a content folder picker and monitors every asset in that folder and its subfolders (redirectors are left out; levels are included); the same is available as *Monitor Assets in Folder* in a folder's Content Browser menu. *Stop Monitoring Selected* and *Stop Monitoring All* (which asks first) remove assets in one step, and *Open Latest Save* (or a double-click) opens the comparison of the selected asset's latest recorded save.

## Reviewing a monitored save

When a monitored asset changes on save, open its diff to review **Save Analysis**. This view summarizes semantic changes, layout/relocation changes, and any bytes that remain native or undecoded. Selecting an explanation navigates to the corresponding semantic diff and hex range.

## Reviewing repeated-save behavior

After multiple observed saves, **Repeated Save Analysis** groups changes by stable semantic path and shows the value history across saves. Selecting a historical transition reopens that captured save and navigates to the corresponding property.

## Testing what a save does on its own

```text
Right-click asset(s)
    -> Asset Serialization
        -> Run No-op Resave Test

Right-click folder
    -> Asset Serialization
        -> Run No-op Resave Test on Folder

Window
    -> Run No-op Resave Test on Project
```

When the run finishes, a notification shows the totals and offers **Save Report...**. For a single asset it offers the diffs of the first and second resave instead.

## Comparing two folders

```text
Window
    -> Compare Asset Folders...
```

Pick the folder with the older assets and then the folder with the newer ones. The notification offers **Save Report...**.

## Running headless

The plugin has a commandlet for build machines. It runs the no-op resave test, the folder comparison and the decode coverage scan headless and writes the same reports as the windows (JSON when the report file ends in `.json`, text otherwise):

```
UnrealEditor-Cmd.exe Project.uproject -run=AssetSerializationInspector -Mode=NoOpResave -Path=/Game/Characters,/Game/Props -Report=Saved/NoOp.json -FailOnUnstable
UnrealEditor-Cmd.exe Project.uproject -run=AssetSerializationInspector -Mode=CompareFolders -Old=D:/Before/Content -New=Content -Report=Saved/Compare.txt -FailOnChanges
UnrealEditor-Cmd.exe Project.uproject -run=AssetSerializationInspector -Mode=DecodeCoverage -Folder=Content -Report=Saved/Coverage.txt
```

`-Path` takes content folders (comma separated, searched recursively), not single assets. Relative paths are taken from the project folder; the report folder is created if needed. The exit code is 0 when the check ran, 1 when the arguments are wrong or a report could not be written, and 2 when a check ran and found what its switch fails on: `-FailOnUnstable` fails when an asset is unstable or could not be tested, `-FailOnChanges` fails when any file differs, is in only one folder or could not be compared. Without a switch the run only reports. In a Git Bash shell on Windows, prefix the command with `MSYS_NO_PATHCONV=1` so that `/Game/...` is not rewritten as a Windows path.
