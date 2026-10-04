# Asset Serialization Inspector

A UE5 editor plugin for inspecting, visualizing, and diffing the serialized contents of `.uasset` files.

The main goal of the project is to answer a deceptively simple question:

> **What did Unreal Engine actually change when this asset was saved?**

Unreal assets can sometimes change on disk even when no obvious user-facing modification was made. This plugin is intended as a diagnostic tool for understanding those changes at the package, export, property, and raw-byte levels.

The plugin currently targets **Unreal Engine 5.8.2** and is designed to work with **vanilla engine builds**, without requiring engine source modifications.

---

## Motivation

`.uasset` files are binary package files containing several different serialized regions, including package metadata, name/import/export tables, UObject payloads, tagged properties, native/custom serialization, and other version-dependent data.

When an asset changes unexpectedly, a normal binary diff only tells you that bytes changed. It does not tell you whether the change came from:

- Package metadata,
- table ordering,
- an export being relocated,
- a property being serialized or omitted,
- a property value changing,
- a GUID or generated field changing,
- native/custom serialization,
- or some other internal Unreal serialization behavior.

Asset Serialization Inspector attempts to bridge that gap.

---

## Current Features

### Package inspection

The plugin can parse and visualize the major structural regions of an Unreal package, including:

- Package summary
- Name Map
- Import Map
- Export Map
- Export payload and script-serialization ranges
- Package-relative references
- Cross-navigation between imports, exports, and referenced entries

The inspector exposes decoded data alongside offsets and sizes so the package layout can be examined directly.

### Structural and semantic diffing

Two `.uasset` files can be compared structurally and semantically. The diff distinguishes between added, removed, modified, relocated, and unchanged entries, allowing it to separate a real payload change from a byte-identical payload that merely moved to a different package offset.

Property matching is based on semantic identity rather than byte position. A property remains one logical change even when an earlier field grows or shrinks and moves all following serialized data.

Nested decoded values are represented as a semantic tree, so changes can be inspected down to individual struct members, array elements, set operations, and map entries.

### Side-by-side hex diff

Changed payload ranges can be inspected as side-by-side hex dumps with:

- Relative offsets
- ASCII representation
- Changed-byte highlighting
- Monospaced rendering
- Copyable/selectable text
- Changed byte-span detection
- Navigation from semantic changes to their exact old/new byte ranges

### Tagged property decoding

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

### Recursive property value decoding

Decoded property values can be compared semantically instead of only as changed bytes. Current decoding infrastructure covers:

- Bool properties
- Signed and unsigned integer properties
- Float and double properties
- Name and string properties
- Byte and enum properties (enum values are decoded by name)
- Field paths (`FieldPathProperty`), shown as owner and property path
- Delegates (single and multicast; bindings with payloads are not decoded)
- Natively serialized structs: gameplay tags and tag containers, single-precision 2D vectors, boxes, gameplay effect versions, per-platform values (`PerPlatformInt/Float/Bool`), rich curve keys
- Text properties: empty, base and string-table texts, and the histories that carry arguments (named, ordered and Blueprint-argument formats with their arguments, numbers, percents, currencies, dates and times, case transforms, text generators as their type and size); a history type this build does not know is reported as unsupported
- Hard UObject/package references, shown as stable paths rather than table indices
- Soft object/class references (resolved through the package's soft object path table)
- Common deterministic structs such as GUIDs, vectors, rotators, quaternions, transforms, colors, integer vectors/points, and related math types
- Recursively tagged structs
- Arrays
- Sets
- Maps

Container decoding preserves Unreal's serialization semantics. In particular, set and map payloads may represent operations relative to defaults rather than complete final values, and the inspector does not incorrectly present such deltas as complete containers. Where the defaults can be found, the final contents are reconstructed as well (see [Values inherited from archetypes](#values-inherited-from-archetypes)).

### Container-aware semantic comparison

Arrays use sequence-aware matching rather than a simple index-by-index comparison. LCS-based matching reduces noise when elements are inserted or removed, while stable semantic keys can identify logical elements that moved or changed.

Set and map comparisons use decoded semantic keys. Map entries are matched by key and values are compared recursively. Serialized container operations such as add, remove, replace, or add/modify remain distinguishable when the package contains delta serialization.

### Property-level change attribution

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

### Save explanation

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

### Asset monitoring

Assets can be opted into monitoring from the Content Browser. The plugin snapshots the previous on-disk `.uasset`, lets Unreal save normally, parses the new file, computes the diff, and produces a save explanation only for monitored assets.

The monitoring system uses public editor/package APIs and does not require changes to `FLinkerSave`, `SavePackage`, or other engine internals.

### Repeated-save analysis

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

### Values inherited from archetypes

Unreal omits a property that equals its default and stores sets and maps as changes against the default, so what a package serializes is often not what the asset actually contains. The inspector follows each export's archetype chain (`TemplateIndex`) to recover it, reading archetypes in the same package directly and archetypes in other packages from disk:

- **Omitted properties**: where one side of a comparison does not serialize a property, the diff shows the value it inherits (`<not serialized>  ->  inherited: 42`) and where it came from, and notes when that value equals the serialized value on the other side, so only the serialization changed.
- **Delta sets and maps**: the final contents are computed as `(defaults - removed) + added` and shown next to the stored delta. A container written without defaults is told apart from a delta by the writer's own rule (a delta never repeats an element that equals its default). Results that rest on an assumption, such as an empty default for a container declared in a Blueprint, are marked *inferred* with the reason.

A property the archetype chain does not store but that the Blueprint of the export's class declares as a variable (in its `NewVariables`) is zero or empty unless the variable carries a default value; the diff shows it as *Blueprint default* with the reason. This needs the Blueprint's properties to be readable, so it does not apply to packages saved with unversioned properties.

Values that come from native C++ defaults cannot be read from package files. For an omitted top-level property whose chain ends in a native class, the inspector reads the value from that class's default object in the running editor and shows it as *native default (live)*, with a note: it is what this editor build's C++ gives the property, which can differ from the engine that saved the asset. Anything else that cannot be determined is reported as unavailable instead of guessed. Sets and maps that are fields of a struct property (also in nested structs) get their final contents the same way, from the same field of the struct on the archetype chain; sets and maps inside arrays are not resolved. A field of a struct that one side leaves out is described like an omitted top-level property: it shows the value the same field has on the archetype chain.

### Package header diff

The header is compared as its own part of the diff, not only as a few changed numbers:

- Every field of the package summary (versions, flags, counts and offsets, saved hash, persistent GUID, engine versions, compression flags, chunk IDs, generations, and each custom version).
- Every region of the header, with offset, size and entry count: the summary, name/import/export maps, depends map, soft object paths, soft package references, searchable names, thumbnail table and thumbnail data, asset registry data, and so on. A region is **modified**, **moved** (identical bytes at another offset), **added** or **removed**, and selecting it shows the usual side-by-side hex view.
- **Explanations** say why: the header's size change broken down by region, what a removed thumbnail table used to hold, that an offset moved by exactly the header's size change, that a region differs only in stored absolute file offsets (those bytes are shown in orange in the hex view, apart from real changes in red), and what the saved hash, package source and persistent GUID mean.

The **Save Analysis** summarizes the header too, in a *Package Header* section (and in the exported reports): how the header's size changed, which tables grew, shrank, appeared or disappeared (with their old and new sizes and the reason, such as a thumbnail table that was removed), how many tables only moved, and which summary fields changed.

### Search and filters

The structural diff has a search box (several terms must all match; names, paths, types and, optionally, values), toggles for added/removed/modified/moved entries, and the existing *Show unchanged*. Matching entries keep their parents visible, and an entry that matches brings what is inside it. The Save Analysis can be filtered by text and confidence, and the Repeated Save Analysis by text and value pattern. The tree keeps the expansion you chose while filters change.

### Reports

*Export report...* saves the current comparison as a text or JSON file: both files and hashes, totals, the full save analysis, every changed entry with its values and explanations, and the repeated-save patterns recorded for the asset. The report does not depend on the view's filters, so it is a complete record; the JSON layout is versioned (`schemaVersion`) and documented in the source. Files are named after the asset and the time.

### No-op resave test

*Run No-op Resave Test* (Content Browser asset menu) answers the opposite question: what does Unreal change when nothing was edited, and does it do so every time? The package is saved twice to temporary files, never over its own file, and the original is compared with the first copy and the first with the second. The verdict is **stable**, **normalized on the first save** (the first resave rewrites the file, later ones do not) or **unstable** (resaving keeps changing the file), and the notification opens either diff. Assets with unsaved changes and levels are refused.

### Batch and project-wide analysis

The same test runs on many assets: select several assets, or use *Run No-op Resave Test on Folder* (Content Browser folder menu), or *Window -> Run No-op Resave Test on Project* (everything under `/Game`). A progress dialog with cancel is shown, and each result is condensed as soon as it is produced so memory does not grow with the project. When a run finishes, a **results window** opens (and *Window -> Show Last No-op Resave Results* reopens it): every asset with its outcome (unstable, normalized on the first save, failed, skipped, stable), most worrying first, with outcome filters and a search box. Selecting an asset shows what its first and second resaves changed, and *Open First Resave* / *Open Second Resave* open the comparison in the diff window (the asset is tested again to produce it, since a run keeps only condensed results). The saved text/JSON report lists unstable assets first and groups **changes found in several assets**, which points at causes that are not about any one asset (for example every asset losing its thumbnails).

### Folder comparison and engine versions

*Window -> Compare Asset Folders...* compares two folders of `.uasset` files on disk, for example a copy of a project from before an engine upgrade and the upgraded one. Files are paired by relative path; each pair is identical, changed, only in one folder, or could not be compared (with the reason, such as a package version this editor cannot read). The report groups what changed: totals, how many files went from which engine version to which, changes found in several files, and then the files themselves.

Packages saved before UE 5.4 (UE4 and UE5.0 to 5.3) store property tags in an older layout, with only the type name and the extra fields each type needs. The inspector reads both layouts, including the inner struct tag that older arrays of structs carry, so their properties decode like newer ones. The tables and exports are read with the versions the package was saved with, which also covers older name maps (no hashes before UE 4.4x), exports without a script serialization range (their properties are looked for from the start of the export) and soft object paths stored inline instead of in the header table. Checked on a package migrated from UE 5.0 and on UE4-era starter content; elements of maps and sets whose struct type is not stored by those versions and that are not tagged structs are reported as unsupported.

When the comparison finishes, a **results window** opens (*Window -> Show Last Folder Comparison* reopens it): every file with its status (could not compare, changed, only in the new or old folder, identical), the engine that saved each side and the file sizes, with status filters and a search box. Selecting a file shows what differs, and *Open Comparison* opens a changed pair in the diff window (both files are read again for it).

In the two-file diff, differences in the engine, file or custom versions are explained, including that some differences can come from the format rather than from edits.

---

## Why No Engine Modifications?

An early approach considered tracing the live `FLinkerSave` archive during package serialization. That would provide extremely precise runtime information, but it would require modifying Unreal Engine source.

One of the goals of this project is to remain usable as a normal plugin on vanilla engine builds.

The current approach therefore uses:

- package pre-save/post-save notifications,
- snapshots of the on-disk asset,
- package structure parsing,
- script serialization ranges,
- offline tagged-property decoding,
- and semantic comparison of the decoded payload.

This provides exact byte positions for data that can be decoded from the package itself while keeping the plugin independent of custom engine changes.

---

## Architecture

At a high level:

```text
.uasset
   |
   v
FAssetPackageReader
   |
   +-- Package Summary
   +-- Name Map
   +-- Import Map
   +-- Export Map
   +-- Export/script payload ranges
   |
   v
FAssetPackageDocument
   |
   +------------------------------+
   |                              |
   v                              v
Property/value decoder       Structural diff
   |                              |
   v                              |
Serialized field map              |
   |                              |
   +--------------+---------------+
                  |
                  v
        Semantic property diff
                  |
                  v
          FAssetSaveAnalyzer
                  |
                  +-----------------------+
                  |                       |
                  v                       v
       Save explanation         Save history / repeated
                                pattern analysis
                  |                       |
                  +-----------+-----------+
                              |
                              v
                   SAssetSerializationDiff
```

The major concepts are intentionally separated:

### `FAssetPackageDocument`

Represents the parsed contents and physical structure of a package, including raw bytes, package summary, Name Map, Import Map, Export Map, and decoded offsets/ranges.

### Serialized field map / trace

Represents known semantic ranges inside an export payload. The field map is derived from the bytes already stored in the `.uasset`; it is not a runtime serialization trace. Known ranges may represent property tags, property values, native/custom regions, and undecoded regions.

### Value decoder

Decodes supported serialized property values into a recursive, engine-independent representation. Every decoder is bounded by the property's serialized range. Complex values can contain children and stable semantic keys used by the diff engine.

### Diff model

Compares two documents and produces structural and semantic results independently of Slate. Diff nodes can represent package fields, names, imports, exports, payloads, properties, nested decoded values, and unknown/native ranges.

### `FAssetSaveAnalyzer`

Interprets the diff and answers what the save means: semantic property changes, serialization/default transitions, layout-only movement, export relocation, and residual unexplained/native changes. It also performs changed-byte attribution and layout-cause inference.

### Save history and repeated-pattern analysis

Recent monitored saves are retained by stable save ID. Semantic paths are aggregated across observations to identify recurring, every-save, continuously changing, and alternating behavior. Historical samples can reopen their original diff session.

### Archetype resolver and container final values

`FAssetArchetypeResolver` walks an export's archetype chain through package files and returns the value a property inherits. `AssetContainerFinalValue` applies a decoded set or map delta to that value and reports how sure it is. Both are independent of the UI and unit tested.

### Header layout and header diff

`AssetPackageHeaderLayout` splits a header into regions (including the thumbnail data that no summary field points at), and the header diff compares fields and regions and attaches the explanations described above.

### Reports and runners

`FAssetAnalysisReport` is the format-independent model of one comparison; the writers turn it into text or JSON. The no-op resave test, the batch runner (`AssetBatchResave`) and the folder comparison (`AssetFolderComparison`) each condense their per-asset results into small entries and have their own report writers, so large runs stay cheap and every result can be saved.

### Decoder coverage scan

`AssetDecoderCoverage` decodes every tagged property of every export under a folder and ranks what the decoder could not read (by how many assets and bytes it affects), plus the bytes that are native or custom serialized, by export class. Run it from the editor console:

```text
ASI.DecodeCoverage <Folder> [ReportFile]
```

It writes a text report (default `Saved/AssetSerializationInspector/Coverage/DecoderCoverage.txt`) and a JSON file beside it. Engine content is mostly saved with unversioned properties (no tags), so run it on project content to find real gaps.

### Slate UI

`SAssetSerializationDiff` renders the structural diff, semantic values, Save Analysis, Repeated Save Analysis, old/new byte ranges, and side-by-side hex data. High-level analysis remains navigable back to the authoritative low-level diff.

---

## Understanding Serialized Defaults

A property not appearing in the serialized payload does **not** necessarily mean that the property does not exist.

Unreal frequently uses delta serialization. If a property matches its default value, it may be omitted from the tagged property stream entirely.

This is important when comparing two versions of an asset.

For example:

```text
Old:
    TestFloat not serialized

New:
    TestFloat = 10.0
```

should not be interpreted as:

```text
Property added
```

Instead, it more likely means:

```text
Old:
    <not serialized; likely default>

New:
    10.0
```

The diff system therefore matches properties semantically instead of assuming the serialized field lists must have identical layouts.

---

## Native and Undecoded Data

Not all UObject serialization is reflected tagged-property serialization.

Native classes can serialize custom data before, after, or instead of reflected properties.

When the plugin cannot confidently map a byte range to a known field, it is reported explicitly as:

```text
<native / undecoded>
```

The plugin should prefer an honest unknown region over guessing and producing misleading field attribution.

As support for more Unreal serialization formats is added, these regions can gradually become more specific.

---

## Example

A monitored Blueprint that repeatedly changes during otherwise ordinary saves might produce:

```text
BP_Box.uasset

Save Analysis
    Result: Semantic and native/undecoded changes

    Meaningful changes
        TestFloat
            1.0 -> 2.0

        LightingGuid
            3c4d... -> 9a21...

    Layout / serialization
        Default__BP_Box_C
            payload unchanged but moved by +16 bytes
            likely cause: preceding serialized data grew by 16 bytes

    Unexplained
        8 native/undecoded changed bytes

Repeated Save Analysis
    LightingGuid
        Changed: 6 / 6 saves
        Pattern: Continuously changing

        15:22:14    3c4d... -> 9a21...
        15:23:01    9a21... -> f140...
        15:24:18    f140... -> 442a...
```

The intent is to make it possible to move from "the file changed" to a specific semantic explanation, and then trace that explanation all the way back to the exact serialized bytes.

---

## Installation

1. Clone or copy the plugin into your project's `Plugins` directory.

   ```text
   YourProject/
       Plugins/
           AssetSerializationInspector/
   ```

2. Regenerate project files if necessary.

3. Build the Editor target.

4. Enable **Asset Serialization Inspector** from:

   ```text
   Edit -> Plugins
   ```

5. Restart the editor if requested.

> The plugin currently targets Unreal Engine 5.8.2. Other engine versions may require small serialization-version adjustments.

---

## Usage

### Inspecting an asset file

Open the Asset Serialization Inspector window (Window -> Asset Serialization Inspector) and select a `.uasset`.

The package tree exposes decoded regions and allows navigating between related Name Map, Import Map, and Export Map entries.

### Comparing two assets

Open the Asset Serialization Diff window and select:

- Old asset
- New asset

Press **Compare**.

The tree shows only changed entries by default. Enable **Show unchanged** when a complete structural comparison is needed.

Selecting a payload or property displays its old/new data and corresponding hex ranges.

### Monitoring an asset

From the Content Browser:

```text
Right-click asset
    -> Asset Serialization Inspector
        -> Start Monitoring
```

The plugin records save-related package changes only for monitored assets.

Monitoring can later be disabled from the same context menu.

Use the search box and the *Added / Removed / Modified / Moved* toggles to narrow a large diff. **Export report...** saves the whole comparison.

### Reviewing a monitored save

When a monitored asset changes on save, open its diff to review **Save Analysis**. This view summarizes semantic changes, layout/relocation changes, and any bytes that remain native or undecoded. Selecting an explanation navigates to the corresponding semantic diff and hex range.

### Reviewing repeated-save behavior

After multiple observed saves, **Repeated Save Analysis** groups changes by stable semantic path and shows the value history across saves. Selecting a historical transition reopens that captured save and navigates to the corresponding property.

### Testing what a save does on its own

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

### Comparing two folders

```text
Window
    -> Compare Asset Folders...
```

Pick the folder with the older assets and then the folder with the newer ones. The notification offers **Save Report...**.

---

## Project Status

This project is currently experimental and intended primarily as a serialization/debugging tool.

The `.uasset` format is heavily version-dependent, and many UObject types contain custom serialization that cannot be understood using reflected property metadata alone.

The decoder is intentionally conservative and validates ranges aggressively to avoid silently interpreting unrelated bytes as valid package data.

Known limitations:

- Only `.uasset` packages are read; levels (`.umap`) are not.
- Native C++ defaults are read from the running editor (top-level properties only; set and map deltas still assume an empty default), and omitted fields are only described for struct properties (not for structs inside arrays, sets or maps).
- Package versions this editor build cannot read are reported as failed rather than guessed.
- Most of the UI has been exercised through automation tests and manual use on small assets. Large projects and assets saved by several different engine versions deserve more real-world testing.

---

## Design Principles

### Vanilla-engine compatible

The plugin should not require custom engine patches.

### Decode, don't guess

Unsupported regions should remain explicitly unknown rather than being interpreted using unsafe heuristics.

### Semantic diff before byte diff

Offsets are implementation details.

Properties should be matched by semantic identity first, with byte differences used to explain how their serialized representations changed.

### Keep raw bytes accessible

High-level explanations should always be traceable back to exact file offsets and byte ranges.

---

## Contributing

Contributions, bug reports, serialization findings, and test assets are welcome.

Because package serialization can vary by:

- Unreal Engine version,
- UObject type,
- cooking state,
- custom versions,
- editor/runtime context,
- and native serialization implementations,

please include the Unreal Engine version and asset type when reporting parsing issues.

---

## License

```text
BSD 3-Clause License

Copyright (c) 2026, Diego Merayo Merayo

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

```

---

## Disclaimer

This project is an independent developer tool and is not affiliated with or endorsed by Epic Games.

Unreal Engine and related names are trademarks or registered trademarks of Epic Games, Inc.
