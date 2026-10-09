# Features

The details of what the plugin does. For a short overview see the [README](../README.md); for the binary data inside exports (meshes, textures, classes, bytecode) see [Native data](NativeData.md).

## Package inspection

The plugin can parse and visualize the major structural regions of an Unreal package, including:

- Package summary
- Name Map
- Import Map
- Export Map
- Export payload and script-serialization ranges
- Package-relative references
- Cross-navigation between imports, exports, and referenced entries

The inspector exposes decoded data alongside offsets and sizes so the package layout can be examined directly.

## Structural and semantic diffing

Two `.uasset` files can be compared structurally and semantically. The diff distinguishes between added, removed, modified, relocated, and unchanged entries, allowing it to separate a real payload change from a byte-identical payload that merely moved to a different package offset.

Property matching is based on semantic identity rather than byte position. A property remains one logical change even when an earlier field grows or shrinks and moves all following serialized data.

Nested decoded values are represented as a semantic tree, so changes can be inspected down to individual struct members, array elements, set operations, and map entries.

## Side-by-side hex diff

Changed payload ranges can be inspected as side-by-side hex dumps with:

- Relative offsets
- ASCII representation
- Changed-byte highlighting
- Monospaced rendering
- Copyable/selectable text
- **Linked scrolling**: scrolling the old or the new hex (wheel or scroll bar) scrolls the other to the same offset, so a big range is compared with one hand; the *Link scrolling* box next to *Copy* turns it off
- Changed byte-span detection
- Navigation from semantic changes to their exact old/new byte ranges

## Tagged property decoding

For versioned tagged-property streams, the plugin decodes the modern UE5 property-tag structure, including:

- Property names
- Complete `FPropertyTypeName`
- Serialized value sizes
- Tag flags
- Optional array indices
- Property GUIDs
- Property tag extensions
- Inline bool values
- Binary/native serialization markers
- Script serialization ranges stored in the Export Map
- Class serialization-control extensions that precede root UObject property streams

The decoder mirrors Unreal's serialized formats conservatively and uses bounded readers so malformed or unsupported values cannot consume following fields.

## Recursive property value decoding

Decoded property values can be compared semantically instead of only as changed bytes. Current decoding infrastructure covers:

- Bool properties
- Signed and unsigned integer properties
- Float and double properties
- Name and string properties
- Byte and enum properties (enum values are decoded by name)
- Field paths (`FieldPathProperty`), shown as owner and property path
- Delegates (single and multicast; bindings with payloads are not decoded)
- Natively serialized structs: gameplay tags and tag containers, single-precision 2D vectors, boxes, gameplay effect versions, per-platform values (`PerPlatformInt/Float/Bool`), rich curve keys, material inputs (`ExpressionInput` and the typed ones), Sequencer frame ranges and float/double channels, Niagara variables, single-precision vectors and quaternions, integer vectors, frame numbers, date times, optionals and sparse delegates; a struct that a tag says writes itself natively and that is not recognized is read as tagged properties only when that parses cleanly, and is otherwise reported as natively serialized instead of being read as garbage
- Text properties: empty, base and string-table texts, and the histories that carry arguments (named, ordered and Blueprint-argument formats with their arguments, numbers, percents, currencies, dates and times, case transforms, text generators as their type and size); a history type this build does not know is reported as unsupported
- Hard UObject/package references, shown as stable paths rather than table indices
- Soft object/class references (resolved through the package's soft object path table)
- Common deterministic structs such as GUIDs, vectors, rotators, quaternions, transforms, colors, integer vectors/points, and related math types
- Recursively tagged structs
- Arrays
- Sets
- Maps

Container decoding preserves Unreal's serialization semantics. In particular, set and map payloads may represent operations relative to defaults rather than complete final values, and the inspector does not incorrectly present such deltas as complete containers. Where the defaults can be found, the final contents are reconstructed as well (see [Values inherited from archetypes](#values-inherited-from-archetypes)).

## Container-aware semantic comparison

Arrays use sequence-aware matching rather than a simple index-by-index comparison. LCS-based matching reduces noise when elements are inserted or removed, while stable semantic keys can identify logical elements that moved or changed.

The elements of an array of structs are saved against the defaults of their struct, not against the owning object's archetype. So a set or map inside such an element is shown as its final contents against the struct's empty defaults (unless it was written with removals, which prove the defaults were not empty; those are left as stored), and a struct field one side leaves out is described with the default the struct has in the running editor (`struct default (live): ...`).

When an element of an array cannot be decoded, the elements before it are kept instead of the whole array being reported as undecoded: the value reads "3 of 5 elements decoded", the display lists the elements that decoded followed by "(could not be decoded)", and a comparison of two arrays, one of which decoded only in part, compares the elements both sides decoded and ends with one `[3...]` entry that says what was not compared, so elements nobody could read are never reported as added or removed. The elements after the first failure are not read, because a failed element leaves the reader at an unknown position; their bytes are in the hex view.

Set and map comparisons use decoded semantic keys. Map entries are matched by key and values are compared recursively. Serialized container operations such as add, remove, replace, or add/modify remain distinguishable when the package contains delta serialization.

The entries of a map or set are written in the order of an internal hash table, which can change from one editor session to the next (a map keyed by objects, for example, can come out in another order after any save). When the bytes of such a property differ but the decoded entries are identical, the diff marks it as the same value stored in another order, and the Save Analysis lists it as a layout change (*Same entries, stored in another order*), not as a property change, so saving a Blueprint no longer reports a map variable that did not change as modified.

## Property-level change attribution

Changed byte spans are mapped back to decoded serialized properties. For example:

```text
Serialized Payload
    TestFloat
        1.0 -> 2.0

    Inventory
        [2]
            Count
                1 -> 2

    <native / undecoded>
        4 changed bytes
```

Unknown bytes remain explicit rather than being attributed to a property without sufficient evidence.

## Save explanation

Observed saves are classified at a higher level than the raw diff. The Save Analysis view separates:

- Meaningful semantic/property changes
- Properties that became serialized or omitted
- Container changes
- Package/layout changes
- Export relocations
- Native or undecoded changes

It also tracks changed-byte coverage so the UI can report how much of a save is explained by decoded properties and how many changed bytes remain unexplained.

When an unchanged export moves because preceding serialized data changed size, the analyzer can identify the relocation and infer the likely preceding layout change that caused it. Inferred causes are kept separate from facts directly established by the bytes.

Example:

```text
Save Analysis

Result: Semantic and native/undecoded changes
Properties changed:     2
Payload bytes changed: 52
Explained:             44
Unexplained:            8
Relocations:            1

Meaningful changes
    TestFloat
        1.0 -> 2.0

Layout / serialization
    Default__BP_Box_C
        Payload unchanged but moved by +16 bytes
        Likely cause: preceding serialized data grew by 16 bytes

Unexplained
    Native / undecoded serialization
        8 changed bytes
```

Analysis entries navigate back to the underlying structural/semantic diff and exact byte ranges.

## Asset monitoring

Assets can be opted into monitoring from the Content Browser. The plugin snapshots the previous on-disk `.uasset`, lets Unreal save normally, parses the new file, computes the diff, and produces a save explanation only for monitored assets.

The monitoring system uses public editor/package APIs and does not require changes to `FLinkerSave`, `SavePackage`, or other engine internals.

## Repeated-save analysis

The plugin keeps recent observed-save history per monitored asset and aggregates changes by stable semantic property path.

Repeated Save Analysis can show:

- How many observed saves changed a field
- Old and new decoded values for each save
- Saves where the field remained unchanged
- Recurring fields
- Fields that changed on every observed save
- Continuously changing values
- Alternating values
- Changed byte counts across observations

For example:

```text
LightingGuid
Changed: 6 / 6 saves
Pattern: Continuously changing

15:22:14    A13F... -> B7C2...
15:23:01    B7C2... -> 91D4...
15:24:18    91D4... -> E882...
```

A history row can open the corresponding historical save diff and navigate directly to the same semantic property, connecting a recurring pattern to the exact semantic and raw-byte change that produced it.

## Values inherited from archetypes

Unreal omits a property that equals its default and stores sets and maps as changes against the default, so what a package serializes is often not what the asset actually contains. The inspector follows each export's archetype chain (`TemplateIndex`) to recover it, reading archetypes in the same package directly and archetypes in other packages from disk:

- **Omitted properties**: where one side of a comparison does not serialize a property, the diff shows the value it inherits (`<not serialized>  ->  inherited: 42`) and where it came from, and notes when that value equals the serialized value on the other side, so only the serialization changed.
- **Delta sets and maps**: the final contents are computed as `(defaults - removed) + added` and shown next to the stored delta. A container written without defaults is told apart from a delta by the writer's own rule (a delta never repeats an element that equals its default). Results that rest on an assumption, such as an empty default for a container declared in a Blueprint, are marked *inferred* with the reason.

A property the archetype chain does not store but that the Blueprint of the export's class declares as a variable (in its `NewVariables`) is zero or empty unless the variable carries a default value; the diff shows it as *Blueprint default* with the reason. This needs the Blueprint's properties to be readable, so it does not apply to packages saved with unversioned properties.

Values that come from native C++ defaults cannot be read from package files. For an omitted top-level property whose chain ends in a native class, the inspector reads the value from that class's default object in the running editor and shows it as *native default (live)*, with a note: it is what this editor build's C++ gives the property, which can differ from the engine that saved the asset. Anything else that cannot be determined is reported as unavailable instead of guessed. Sets and maps that are fields of a struct property (also in nested structs) get their final contents the same way, from the same field of the struct on the archetype chain; sets and maps inside arrays are not resolved. A field of a struct that one side leaves out is described like an omitted top-level property: it shows the value the same field has on the archetype chain.

## Package header diff

The header is compared as its own part of the diff, not only as a few changed numbers:

- Every field of the package summary (versions, flags, counts and offsets, saved hash, persistent GUID, engine versions, compression flags, chunk IDs, generations, and each custom version).
- Every region of the header, with offset, size and entry count: the summary, name/import/export maps, depends map, soft object paths, soft package references, searchable names, thumbnail table and thumbnail data, asset registry data, and so on. A region is **modified**, **moved** (identical bytes at another offset), **added** or **removed**, and selecting it shows the usual side-by-side hex view.
- **Explanations** say why: the header's size change broken down by region, what a removed thumbnail table used to hold, that an offset moved by exactly the header's size change, that a region differs only in stored absolute file offsets (those bytes are shown in orange in the hex view, apart from real changes in red), and what the saved hash, package source and persistent GUID mean.

The **Save Analysis** summarizes the header too, in a *Package Header* section (and in the exported reports): how the header's size changed, which tables grew, shrank, appeared or disappeared (with their old and new sizes and the reason, such as a thumbnail table that was removed), how many tables only moved, and which summary fields changed.

## Native data

Bytes of an export that no tagged property accounts for are described and, for many kinds, decoded. See [Native data](NativeData.md).

## Search and filters

The structural diff has a search box (several terms must all match; names, paths, types and, optionally, values), toggles for added/removed/modified/moved entries, and the existing *Show unchanged*. Matching entries keep their parents visible, and an entry that matches brings what is inside it. The Save Analysis can be filtered by text and confidence, and the Repeated Save Analysis by text and value pattern. The tree keeps the expansion you chose while filters change.

## Reports

*Export report...* saves the current comparison as a text or JSON file: both files and hashes, totals, the full save analysis, every changed entry with its values and explanations, and the repeated-save patterns recorded for the asset. The report does not depend on the view's filters, so it is a complete record; the JSON layout is versioned (`schemaVersion`) and documented in the source. Files are named after the asset and the time.

## No-op resave test

*Run No-op Resave Test* (Content Browser asset menu) answers the opposite question: what does Unreal change when nothing was edited, and does it do so every time? The package is saved twice to temporary files, never over its own file, and the original is compared with the first copy and the first with the second. The verdict is **stable**, **normalized on the first save** (the first resave rewrites the file, later ones do not) or **unstable** (resaving keeps changing the file), and the notification opens either diff. Assets with unsaved changes and levels are refused.

## Batch and project-wide analysis

The same test runs on many assets: select several assets, or use *Run No-op Resave Test on Folder* (Content Browser folder menu), or *Window -> Asset Serialization -> Run No-op Resave Test on Project* (everything under `/Game`). A progress dialog with cancel is shown, and each result is condensed as soon as it is produced so memory does not grow with the project. When a run finishes, a **results window** opens (and *Window -> Asset Serialization -> Show Last No-op Resave Results* reopens it): every asset with its outcome (unstable, normalized on the first save, failed, skipped, stable), most worrying first, with outcome filters and a search box. Selecting an asset shows what its first and second resaves changed, and *Open First Resave* / *Open Second Resave* open the comparison in the diff window (the asset is tested again to produce it, since a run keeps only condensed results). The saved text/JSON report lists unstable assets first and groups **changes found in several assets**, which points at causes that are not about any one asset (for example every asset losing its thumbnails).

## Folder comparison and engine versions

*Window -> Asset Serialization -> Compare Asset Folders...* compares two folders of `.uasset` files on disk, for example a copy of a project from before an engine upgrade and the upgraded one. Files are paired by relative path; each pair is identical, changed, only in one folder, or could not be compared (with the reason, such as a package version this editor cannot read). Packages saved by a source build of the engine (without a changelist) do not name the engine that saved them, so the engine is inferred from the package file version (such as `UE4 522 / UE5 1012`, which only changes when serialization does): `5.4.0 to 5.4.4 (inferred)` names the releases that shipped that file version, a file version no release had is placed `between 5.3.2 and 5.4.0` (a build in between) or `newer than` the last release the table knows (5.8.3), and a name the package gives is always used as it is. The file version is shown next to the engine and remains the dependable sign of a version change; the table comes from the object versions of the engine release tags from 4.0 to 5.8. The report groups what changed: totals, how many files went from which engine version to which, changes found in several files, and then the files themselves.

Packages saved before UE 5.4 (UE4 and UE5.0 to 5.3) store property tags in an older layout, with only the type name and the extra fields each type needs. The inspector reads both layouts, including the inner struct tag that older arrays of structs carry, so their properties decode like newer ones. The tables and exports are read with the versions the package was saved with, which also covers older name maps (no hashes before UE 4.4x), exports without a script serialization range (their properties are looked for from the start of the export) and soft object paths stored inline instead of in the header table. Checked on a package migrated from UE 5.0 and on UE4-era starter content. Those versions also do not store the struct type of a map or set element (a `Map<Name, Guid>`, for example), and a native struct cannot be told from a tagged one without its definition, so the type is read from the property of the same name on the class (or struct) in the running editor. When the editor does not have the class, as with variables a Blueprint declares, such an element that is not a tagged struct is reported as unsupported.

## HTML reports

Every report the plugin saves (the diff window's analysis, the batch no-op resave test and the folder comparison) can be written as a self-contained HTML page next to the text and JSON versions: choose *HTML report* (or *JSON report*) as the file type in the save dialog (the suggested name has no extension, so the dialog adds the one of the type you pick; a name typed with an extension keeps it and decides the format), give the file a `.html` name, or pass `-Report=Something.html` to the commandlet. The page has no scripts and no external files, so it can be attached to a ticket or kept as a build artifact; it follows the browser's light or dark theme, shows the save analysis and the differences as folding blocks with coloured state labels (added, removed, modified, moved), and escapes every name and value that comes from an asset so that a property value cannot inject markup.

## Comparing with a source control revision

With a source control provider connected (Perforce, Git or any other the editor supports, through the Revision Control menu), *Compare with Source Control Revision...* in an asset's Content Browser menu asks the provider for the file's history and lists the latest revisions at the cursor, newest first. Picking one gets that revision into `Saved/AssetSerializationInspector/Revisions`, and the diff window opens with the revision as the old side and the asset on disk as the new one, with the same Save Analysis (what changed, and whether it is explained by properties, the header or native data) as for an observed save. It works on the file on disk, so unsaved changes in the editor are not part of the comparison. The entry is disabled when source control is not connected, and a file that is not under source control, or has no earlier revision, is reported. Only the `.uasset`/`.umap` of a revision is fetched, so a package saved in two files is compared by its header and the exports of the file on disk.

## Automated tests

The plugin's automation tests run with `Scripts\RunAutomationTests.ps1`, which builds a host project that has the plugin, runs every test with `UnrealEditor-Cmd`, then runs the widget tests with the full editor (Slate is only initialized there; under `-Cmd` the widget tests skip themselves), and fails on a failed test, a run that did not finish its queue (a crash or a timeout) and a run that found no tests:

```
.\Scripts\RunAutomationTests.ps1 -EngineRoot D:\dev\UnrealEngine -HostProject D:\dev\ASIHost\ASIHost.uproject -LinkPlugin
```

`-LinkPlugin` links this repository into the host project's `Plugins` folder for the run (a junction, removed afterwards); without it the plugin must already be there. Each editor run goes through its tests twice in one editor session (`-Loops`, default 2): a test that leaves an object behind in memory, such as a Blueprint created in a package of a fixed name, passes the first time and is an assertion the second, which is how a second run of the tests in an open editor crashes it. The logs are copied to `Saved\AutomationLogs`. Close the editor first: an open editor locks the plugin's DLL.

There is no CI for the tests: they need an Unreal Engine built from source, which GitHub's hosted runners cannot provide, and a self-hosted runner on a public repository would run the code of any fork's pull request on its machine. Run the script before opening a pull request.

## Running the checks without the editor UI

The plugin has a commandlet for build machines. It runs the no-op resave test, the folder comparison and the decode coverage scan headless and writes the same reports as the windows (JSON when the report file ends in `.json`, text otherwise):

```
UnrealEditor-Cmd.exe Project.uproject -run=AssetSerializationInspector -Mode=NoOpResave -Path=/Game/Characters,/Game/Props -Report=Saved/NoOp.json -FailOnUnstable
UnrealEditor-Cmd.exe Project.uproject -run=AssetSerializationInspector -Mode=CompareFolders -Old=D:/Before/Content -New=Content -Report=Saved/Compare.txt -FailOnChanges
UnrealEditor-Cmd.exe Project.uproject -run=AssetSerializationInspector -Mode=DecodeCoverage -Folder=Content -Report=Saved/Coverage.txt
```

`-Path` takes content folders (comma separated, searched recursively), not single assets. Relative paths are taken from the project folder; the report folder is created if needed. The exit code is 0 when the check ran, 1 when the arguments are wrong or a report could not be written, and 2 when a check ran and found what its switch fails on: `-FailOnUnstable` fails when an asset is unstable or could not be tested, `-FailOnChanges` fails when any file differs, is in only one folder or could not be compared. Without a switch the run only reports. In a Git Bash shell on Windows, prefix the command with `MSYS_NO_PATHCONV=1` so that `/Game/...` is not rewritten as a Windows path.

## Levels, split packages and cooked packages

Levels (`.umap`) are read like any other package: the inspector, the diff window, the folder comparison and the save observer all accept `.umap` as well as `.uasset` (a monitored level is snapshotted and compared on save). A package saved in two files keeps its header in the `.uasset` or `.umap` and its exports in a `.uexp` beside it, with export offsets that count the two as one file; the reader appends the `.uexp` when the header file holds nothing else, so every export decodes, and opening the `.uexp` opens its header file. Bulk data in `.ubulk` and similar files is read only where a decoder needs it, such as the mips of a cooked texture (see [Native data](NativeData.md)); the properties that point at it are always read. Cooked packages usually save their properties without tags: a header of fragments and a zero mask say which of the class's properties are stored, then the values follow in the order of the class's property list, with no names and no lengths. They are read with the classes and structs of the running editor (so the editor must have the class, as it does for native classes and for the structs of plugins and modules it has loaded): every property of an export of a native class decodes like a tagged one, zero values (false, 0, None, empty) included, enums as the integers they are stored as and shown by name, and what follows the properties stays a native range. A package saved by another version of the class cannot be told apart from a valid one except where the stream does not fit (a property the class does not have, a value that does not decode), where the export falls back to one undecoded range; exports of classes the editor does not have, such as Blueprint classes, and structs that serialize themselves natively, are reported as undecoded too, with the reason. The tables, the exports' sizes and the header diff work as usual. Checked by saving engine objects (a sound class, curves, a hierarchy of objects) both ways and requiring identical values.

When the comparison finishes, a **results window** opens (*Window -> Asset Serialization -> Show Last Folder Comparison* reopens it): every file with its status (could not compare, changed, only in the new or old folder, identical), the engine that saved each side and the file sizes, with status filters and a search box. Selecting a file shows what differs, and *Open Comparison* opens a changed pair in the diff window (both files are read again for it).

Both results windows (this one and the no-op resave results) sort by a click on a column header (click again to reverse it; the default order puts the most worrying rows first) and select several rows with Ctrl and Shift: the details show every selected row, *Copy Names* puts their names on the clipboard, and the comparison buttons work on a single row.

In the two-file diff, differences in the engine, file or custom versions are explained, including that some differences can come from the format rather than from edits.
