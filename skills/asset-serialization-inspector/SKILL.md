---
name: asset-serialization-inspector
description: Use the Asset Serialization Inspector plugin to find out what Unreal changed in .uasset/.umap files. Use when asked whether resaving or a new engine version changes assets, why two copies of an asset or two folders of assets differ, whether a content folder resaves cleanly (CI check), or how much of the content the inspector can read. Runs the plugin's headless commandlet and reads its JSON reports.
---

# Asset Serialization Inspector

The plugin (an Unreal Engine editor plugin) parses `.uasset` and `.umap` packages and explains what changed on disk: which property, name, import or export, and which bytes it cannot explain. The headless commandlet runs three checks and writes the same reports as the editor windows. Use it when the question is about what a save, an engine upgrade or an edit did to the files, not about what an asset contains.

It works only in a project that has the plugin installed and built (`Plugins/AssetSerializationInspector`).

## Pick the check

| The question | Mode |
|---|---|
| Does resaving change these assets when nothing was edited? Do they change every time? | `NoOpResave` |
| What differs between two sets of files (before and after an engine upgrade, two branches, two builds)? | `CompareFolders` |
| How much of this content can the inspector read, and what can't it? | `DecodeCoverage` |

There is no headless diff of one pair of files. To compare two versions of one asset, put each in its own folder and use `CompareFolders`.

## Run it

```text
UnrealEditor-Cmd.exe Project.uproject -run=AssetSerializationInspector -Mode=NoOpResave -Path=/Game/Characters,/Game/Props -Report=Saved/NoOp.json -FailOnUnstable
UnrealEditor-Cmd.exe Project.uproject -run=AssetSerializationInspector -Mode=CompareFolders -Old=D:/Before/Content -New=Content -Report=Saved/Compare.json -FailOnChanges
UnrealEditor-Cmd.exe Project.uproject -run=AssetSerializationInspector -Mode=DecodeCoverage -Folder=Content -Report=Saved/Coverage.json
```

- `-Path` takes content paths (`/Game/...`, comma separated, searched recursively), not single assets. In Git Bash on Windows, prefix the command with `MSYS_NO_PATHCONV=1` so `/Game/...` is not rewritten as a Windows path.
- `-Old`, `-New` and `-Folder` are folders of package files. Relative paths are taken from the project folder.
- `-Report` picks the format by extension: `.json` (ask for this one when you will read the result), `.html`, anything else is text. The folder is created.
- `NoOpResave` saves each asset twice to temporary files. The assets on disk are not modified.
- Exit code: 0 when the check ran, 1 when the arguments are wrong or the report could not be written, 2 when the check ran and found what its switch fails on. Without `-FailOnUnstable` or `-FailOnChanges` the exit code is 0 even when there are findings, so read the report.
- A big folder takes a while; the log shows progress about every ten seconds.

## Read the result

Every JSON report has `schemaVersion` and `kind` (`batchNoOpResave`, `folderComparison` or `decoderCoverage`). Start from the summary:

- `batchNoOpResave`: `scope`, `summary` (`tested`, `stable`, `normalizedOnFirstSave`, `unstable`, `skipped`, `failed`), then the assets and the changes found in several of them. Read the other top-level keys of the file before relying on them.
- `folderComparison`: `summary`, `engineVersions`, `recurringChanges`, `assets` (one entry per file).
- `decoderCoverage`: `totals`, `issues` (what could not be decoded, by type), then one section per kind of native data (`staticMeshes`, `dataTables`, ...) and `nativeRegions` (bytes outside the tagged properties, by class).

**No-op resave verdicts**, per asset:
- **stable**: resaving changes nothing.
- **normalized on the first save**: the first resave rewrites the file, later ones do not. A one-time cost, e.g. after an engine upgrade.
- **unstable**: resaving keeps changing the file. This is the one to investigate; the report lists unstable assets first and groups changes that recur across many assets, which points at a cause that is not about any one of them.
- **skipped** (levels, assets with unsaved changes) and **failed** are listed with a reason.

**Folder comparison**: each file is identical, changed, only in the old folder, only in the new folder or failed. A changed pair notes when the two files were saved by different engine versions, which explains many differences by itself.

**Undecoded data**: a range reported as native or undecoded is bytes the inspector cannot attribute to a field. It is not "no change" and it is not a guess. If the only differences are in such ranges, say that the files differ and that the cause is not decoded. `DecodeCoverage` lists, by class, what the inspector cannot read yet.

## More

Read these in the plugin's `docs/` folder rather than guessing: `Usage.md` (every task and the commandlet), `Features.md` (what each analysis means), `Concepts.md` (native and undecoded data), `NativeData.md` (which native formats are read).
