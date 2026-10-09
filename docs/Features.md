# Features

What the plugin does, one feature at a time. For a short overview see the [README](../README.md). For the binary data inside exports (meshes, textures, classes, bytecode) see [Native data](NativeData.md). For menus and windows see [Usage](Usage.md).

## Package inspection

The plugin parses and shows the structure of a package:

- package summary,
- name map, import map and export map,
- export payloads and their script-serialization ranges,
- package-relative references, with navigation between imports, exports and the entries that refer to them.

Decoded data is shown next to offsets and sizes, so the layout can be examined directly.

## Structural and semantic diffing

Two packages are compared structurally and semantically.

- Entries are **added**, **removed**, **modified**, **relocated** or **unchanged**. A payload that is byte-identical but sits at another offset is a relocation, not a change.
- Properties are matched by **semantic identity**, not byte position. A property stays one logical change even when an earlier field grows and shifts everything after it.
- Decoded values form a tree, so a change can be inspected down to a struct member, an array element, a set operation or a map entry.

## Side-by-side hex diff

A changed range can be inspected as side-by-side hex dumps with:

- relative offsets and an ASCII column,
- changed bytes highlighted, and changed byte spans detected,
- monospaced, selectable, copyable text,
- **linked scrolling**: scrolling the old or the new side (wheel or scroll bar) scrolls the other to the same offset. The *Link scrolling* box next to *Copy* turns it off,
- navigation from a semantic change to its exact old and new byte ranges.

## Tagged property decoding

For versioned tagged-property streams the plugin decodes the UE5 property tag: name, full `FPropertyTypeName`, value size, flags, array index, property GUID, tag extensions, inline bool value and the binary/native marker. It also reads the script-serialization ranges in the export map and the class serialization-control extensions that precede root property streams.

The decoder follows Unreal's formats conservatively. Every reader is bounded by the property's serialized range, so a malformed or unsupported value cannot swallow the fields after it.

## Property value decoding

Values are decoded recursively, so they can be compared as values and not as changed bytes.

| Kind | What is decoded |
|---|---|
| Scalars | bool, signed and unsigned integers, float, double |
| Names and strings | name, string; byte and enum properties (enums by name) |
| References | hard object and package references as stable **paths** (not table indices); soft object and class references, resolved through the soft object path table; field paths (owner and property path) |
| Delegates | single and multicast; bindings with payloads are not decoded |
| Text | empty, base and string-table texts; histories with arguments (named, ordered, Blueprint formats, numbers, percents, currencies, dates and times, case transforms; text generators as type and size). An unknown history type is reported as unsupported |
| Math structs | GUID, vector, rotator, quaternion, transform, color, integer vector and point, and related types |
| Native structs | gameplay tags and containers, gameplay effect versions, single-precision vectors, 2D vectors and quaternions, boxes, per-platform values (`PerPlatformInt/Float/Bool`), rich curve keys, material inputs (`ExpressionInput` and the typed ones), Sequencer frame ranges and float/double channels, Niagara variables, integer vectors, frame numbers, date times, optionals, sparse delegates |
| Tagged structs | recursively |
| Containers | arrays, sets, maps |

A struct that says it serializes itself natively and is not recognized is read as tagged properties only if that parses cleanly. Otherwise it is reported as natively serialized, never read as garbage.

Set and map payloads may be **operations against the defaults**, not complete contents. The inspector does not present such a delta as a complete container. Where the defaults can be found, the final contents are reconstructed too (see [Values inherited from archetypes](#values-inherited-from-archetypes)).

## Container-aware comparison

- **Arrays** are matched as sequences (LCS), so an inserted or removed element does not shift the report of every element after it. Stable semantic keys identify elements that moved or changed.
- **Sets and maps** are compared by decoded semantic key. Map entries are matched by key and their values compared recursively. Add, remove, replace and add/modify operations stay distinguishable in delta serialization.
- **Arrays of structs** are saved against the defaults of the struct, not the owning object's archetype. A set or map inside such an element is shown as its final contents against the struct's empty defaults, unless it was written with removals (which prove the defaults were not empty; those are left as stored). A struct field one side leaves out is described with the default the struct has in the running editor (`struct default (live): ...`).
- **Hash order**: map and set entries are written in the order of an internal hash table, which can change between editor sessions. When the bytes differ but the decoded entries are identical, the diff marks the value as the same stored in another order, and the Save Analysis lists a layout change (*Same entries, stored in another order*), not a property change.

### Arrays that decode only in part

If an element cannot be decoded, the elements before it are kept. The value reads "3 of 5 elements decoded", and the display lists the decoded elements followed by "(could not be decoded)".

When comparing two arrays where one decoded only in part, the elements both sides decoded are compared, and one `[3...]` entry says what was not compared. Elements nobody could read are never reported as added or removed.

Elements after the first failure are not read, because a failed element leaves the reader at an unknown position. Their bytes are in the hex view.

## Property-level change attribution

Changed byte spans are mapped back to decoded properties:

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

Unknown bytes stay explicit. They are never attributed to a property without enough evidence.

## Save explanation

Observed saves are classified above the level of the raw diff. The Save Analysis separates:

- meaningful semantic and property changes,
- properties that became serialized or omitted,
- container changes,
- package and layout changes,
- export relocations,
- native or undecoded changes.

It tracks **changed-byte coverage**, so you see how much of a save is explained by decoded properties and how many bytes remain unexplained.

When an unchanged export moves because earlier data changed size, the analyzer identifies the relocation and infers the likely layout change behind it. Inferred causes are kept apart from facts established by the bytes.

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

Every entry navigates back to the structural diff and the exact byte ranges.

## Asset monitoring

Opt an asset into monitoring from the Content Browser. The plugin snapshots the on-disk `.uasset`, lets Unreal save normally, parses the new file, computes the diff and produces a save explanation, only for monitored assets.

It uses public editor and package APIs. No change to `FLinkerSave`, `SavePackage` or other engine internals is needed.

## Repeated-save analysis

The plugin keeps recent saves per monitored asset and aggregates changes by stable property path. It shows:

- how many saves changed a field, with old and new values for each,
- saves where the field stayed the same,
- recurring fields, and fields that changed on **every** save,
- continuously changing values and alternating values,
- changed byte counts across saves.

```text
LightingGuid
Changed: 6 / 6 saves
Pattern: Continuously changing

15:22:14    A13F... -> B7C2...
15:23:01    B7C2... -> 91D4...
15:24:18    91D4... -> E882...
```

A history row opens the diff of that save and navigates to the same property.

The package **header** takes part: a header table that changes in many saves (thumbnail table, name map, asset registry data) becomes a pattern like a property, with how often it changed and the bytes it cost.

## Values inherited from archetypes

Unreal omits a property that equals its default, and stores sets and maps as changes against the default. So what a package serializes is often not what the asset contains. The inspector follows the export's archetype chain (`TemplateIndex`), reading archetypes in the same package directly and those in other packages from disk.

- **Omitted properties**: where one side does not serialize a property, the diff shows the value it inherits (`<not serialized>  ->  inherited: 42`) and where it came from. It also notes when that equals the other side's serialized value, so only the serialization changed.
- **Delta sets and maps**: the final contents are computed as `(defaults - removed) + added` and shown next to the stored delta. A container written without defaults is told apart from a delta by the writer's own rule (a delta never repeats an element equal to its default). Results that rest on an assumption, such as an empty default for a Blueprint-declared container, are marked *inferred* with the reason.
- **Blueprint defaults**: a property the chain does not store but the export's Blueprint declares in `NewVariables` is zero or empty unless the variable has a default value; shown as *Blueprint default*. This needs readable Blueprint properties, so it does not apply to packages saved with unversioned properties.
- **Native defaults**: for an omitted top-level property whose chain ends in a native class, the value is read from that class's default object in the running editor and shown as *native default (live)*. It is what this editor build's C++ gives, which can differ from the engine that saved the asset.
- **Struct fields**: sets and maps that are fields of a struct (also nested) get their final contents from the same field on the archetype chain. A field of a struct that one side leaves out is described the same way as an omitted top-level property. Sets and maps inside arrays are not resolved.

Anything that cannot be determined is reported as unavailable, not guessed.

## Package header diff

The header is its own part of the diff.

- **Summary fields**: every field of the package summary (versions, flags, counts and offsets, saved hash, persistent GUID, engine versions, compression flags, chunk IDs, generations, custom versions).
- **Regions**: every region with offset, size and entry count: summary, name/import/export maps, depends map, soft object paths, soft package references, searchable names, thumbnail table and data, asset registry data, and so on. A region is **modified**, **moved** (identical bytes elsewhere), **added** or **removed**, and selecting it shows the hex view.
- **Explanations**: the header's size change broken down by region; what a removed thumbnail table held; that an offset moved by exactly the header's size change; that a region differs only in stored absolute file offsets (shown in orange in the hex view, apart from real changes in red); and what the saved hash, package source and persistent GUID mean.

The Save Analysis summarizes the header in a *Package Header* section (and in exported reports): how its size changed, which tables grew, shrank, appeared or disappeared and why, how many only moved, and which summary fields changed.

## Native data

Bytes of an export that no tagged property accounts for are described and, for many kinds, decoded. See [Native data](NativeData.md).

## Search and filters

- The structural diff has a **search box**: several terms must all match, against names, paths, types and optionally values. Matching entries keep their parents visible, and an entry that matches brings what is inside it.
- Toggles for **added / removed / modified / moved** entries, and *Show unchanged*.
- The Save Analysis filters by text and confidence. The Repeated Save Analysis filters by text and value pattern.
- The tree keeps the expansion you chose while filters change.

## Reports

*Export report...* saves the current comparison as a text or JSON file: both files and hashes, totals, the full save analysis, every changed entry with values and explanations, and the repeated-save patterns for the asset. It does not depend on the view's filters, so it is a complete record. The JSON layout is versioned (`schemaVersion`) and documented in the source. Files are named after the asset and the time.

### HTML reports

Every report (diff analysis, batch no-op resave test, folder comparison) can also be written as a **self-contained HTML page**. Choose *HTML report* as the file type in the save dialog, give the file a `.html` name, or pass `-Report=Something.html` to the commandlet.

- No scripts and no external files: it can be attached to a ticket or kept as a build artifact.
- Follows the browser's light or dark theme.
- Shows the analysis and differences as folding blocks with colored state labels.
- Escapes every name and value that comes from an asset, so a property value cannot inject markup.

## No-op resave test

*Run No-op Resave Test* (Content Browser asset menu) asks the opposite question: what does Unreal change when nothing was edited, and does it do so every time?

The package is saved twice to temporary files (never over its own file). The original is compared with the first copy, and the first with the second. The verdict is:

- **stable**,
- **normalized on the first save**: the first resave rewrites the file, later ones do not,
- **unstable**: resaving keeps changing the file.

The notification opens either diff. Assets with unsaved changes and levels are refused.

## Batch and project-wide analysis

The same test runs on many assets: select several, use *Run No-op Resave Test on Folder*, or *Window -> Asset Serialization -> Run No-op Resave Test on Project* (everything under `/Game`). A progress dialog with cancel is shown, and each result is condensed as it is produced so memory does not grow with the project.

A **results window** opens when the run finishes (*Show Last No-op Resave Results* reopens it): every asset with its outcome (unstable, normalized on the first save, failed, skipped, stable), most worrying first, with outcome filters and a search box. Selecting an asset shows what its first and second resaves changed, and *Open First Resave* / *Open Second Resave* open the comparison (the asset is tested again to produce it, since a run keeps only condensed results).

The saved text, JSON or HTML report lists unstable assets first and groups **changes found in several assets**, which points at causes that are not about any one asset (for example every asset losing its thumbnails).

## Folder comparison and engine versions

*Window -> Asset Serialization -> Compare Asset Folders...* compares two folders of `.uasset` files on disk, for example a project copy from before an engine upgrade and the upgraded one. Files are paired by relative path; each pair is identical, changed, only in one folder, or could not be compared (with the reason, such as a package version this editor cannot read).

A **results window** opens (*Show Last Folder Comparison* reopens it): every file with its status, the engine that saved each side and the file sizes, with status filters and a search box. Selecting a file shows what differs, and *Open Comparison* opens a changed pair in the diff window. Both results windows sort by clicking a column header (again to reverse) and select several rows with Ctrl and Shift; *Copy Names* copies the selected names.

The report groups what changed: totals, how many files went from which engine version to which, changes found in several files, then the files.

In the two-file diff, differences in engine, file or custom versions are explained, including that some can come from the format rather than from edits.

### Inferring the engine

A package saved by a source build (no changelist) does not name its engine, so it is inferred from the package **file version** (such as `UE4 522 / UE5 1012`, which only changes when serialization does):

- `5.4.0 to 5.4.4 (inferred)` names the releases that shipped that file version,
- a file version no release had is placed `between 5.3.2 and 5.4.0` (a build in between), or `newer than` the last release the table knows (5.8.3),
- a name the package gives is always used as it is.

The file version is shown next to the engine and remains the dependable sign of a version change. The table comes from the object versions of the engine release tags from 4.0 to 5.8.

### Older packages

Packages saved before UE 5.4 (UE4 and UE5.0 to 5.3) store property tags in an older layout. The inspector reads both layouts, including the inner struct tag that older arrays of structs carry. Tables and exports are read with the versions the package was saved with, which also covers older name maps (no hashes before UE 4.4x), exports without a script serialization range, and soft object paths stored inline. Checked on a package migrated from UE 5.0 and on UE4-era starter content.

Those versions do not store the struct type of a map or set element (a `Map<Name, Guid>`, for example), and a native struct cannot be told from a tagged one without its definition. So the type is read from the property of the same name on the class (or struct) in the running editor. When the editor does not have the class (variables a Blueprint declares), such an element that is not a tagged struct is reported as unsupported.

## Comparing with a source control revision

With a source control provider connected (Perforce, Git or any other the editor supports), *Compare with Source Control Revision...* in an asset's Content Browser menu lists the latest revisions of the file, newest first. Picking one fetches it into `Saved/AssetSerializationInspector/Revisions` and opens the diff with the revision as the old side and the file on disk as the new one, with the same Save Analysis as for an observed save.

- It works on the file on disk, so unsaved editor changes are not part of the comparison.
- The entry is disabled when source control is not connected. A file not under source control, or with no earlier revision, is reported.
- Only the `.uasset` / `.umap` of a revision is fetched, so a package saved in two files is compared by its header and the exports of the file on disk.

## Levels, split packages and cooked packages

- **Levels** (`.umap`) are read like any other package: the inspector, the diff window, the folder comparison and the save observer accept them (a monitored level is snapshotted and compared on save).
- **Split packages**: a package saved in two files keeps its header in the `.uasset` or `.umap` and its exports in a `.uexp` beside it. The reader appends the `.uexp` when the header file holds nothing else, so every export decodes, and opening the `.uexp` opens its header file. Bulk data in `.ubulk` and similar files is read only where a decoder needs it, such as the mips of a cooked texture (see [Native data](NativeData.md)); the properties that point at it are always read.
- **Cooked packages** usually save properties without tags: a header of fragments and a zero mask say which of the class's properties are stored, then the values follow in the class's property order. They are read with the classes and structs of the running editor, so the editor must have the class (as for native classes and structs of loaded plugins and modules). Every property of a native-class export then decodes like a tagged one, zero values and enums included, and what follows stays a native range.

For cooked packages, a package saved by another version of the class is detected only where the stream does not fit (a property the class does not have, a value that does not decode). That export falls back to one undecoded range. Exports of classes the editor does not have (such as Blueprint classes) and structs that serialize themselves natively are reported as undecoded, with the reason. The tables, export sizes and header diff work as usual. This was checked by saving engine objects (a sound class, curves, a hierarchy of objects) both ways and requiring identical values.

To run the checks from a build machine, see [Usage](Usage.md#running-headless). To run the plugin's tests, see [Development](Development.md#automated-tests).
