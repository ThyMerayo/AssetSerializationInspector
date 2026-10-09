# Architecture

## Why no engine modifications

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

## Overview

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

The package header takes part in this analysis: a table of the header that changes in many saves (the thumbnail table, the name map, the asset registry data) becomes a pattern like a property does, with how often it changed and the bytes it cost, so a header that is rewritten on every save shows up in the repeated-save report next to the properties.

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

### Decoder blind spots

The coverage scan says what the decoder cannot read. `AssetDecoderBlindSpots` finds the opposite problem: values it reads but does not keep. For the top-level tagged properties that decode completely, it changes each byte of the value in turn (one bit, in its own copy of the document), decodes the property again and counts the bytes whose change left the decoded value exactly as it was. Such a byte could differ between two saves without the diff showing what changed. Bytes of a struct that no field accounts for (the tags that name and size each field) are not counted, because they describe the layout, not the content.

```text
ASI.DecodeBlindSpots <Folder> [ReportFile]
```

The report lists the types with blind bytes, the most first, with where they were seen (default `Saved/AssetSerializationInspector/Coverage/DecoderBlindSpots.txt`). Run on the 5,261 packages of the engine's content, the only structure that decoded to a constant text (a pin type, found this way) was already fixed. The scan also showed that floating point values were written with a few decimals, so two different values could read the same: that is fixed too (below), and a second run over the engine's material functions went from 10,407 blind bytes to 1.

### Numbers

Floats, doubles and the vector, rotator, quaternion and box structs are written as the engine writes them (`1.000000`, `X=1.000 Y=2.000 Z=3.000`) whenever that text gives the value back exactly. When it does not (a float one bit away from 1, a location that moved by 0.0001), the number is written with the fewest digits that do (`1.0000001`, `X=100.0001 Y=2.000 Z=0.000`), and only the numbers that need it. Before this, a change smaller than the printed precision showed as a modified property with the same text on both sides.

### Names and case

Unreal compares names and strings without regard to case, and so does `FString` in C++, so a value that changed from `b` to `B` (or `Hello` to `hello`) used to be the same value to the inspector: inside a struct, an array, a set or a map it did not show in the diff. Decoded values, the keys of sets and maps, the Name Map, the header fields, the native data of classes and the repeated-save history are now compared with case, so a change of case is reported as a change.

### Slate UI

`SAssetSerializationDiff` renders the structural diff, semantic values, Save Analysis, Repeated Save Analysis, old/new byte ranges, and side-by-side hex data. High-level analysis remains navigable back to the authoritative low-level diff.
