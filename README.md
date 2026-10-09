# Asset Serialization Inspector

A UE5 editor plugin that shows **what Unreal Engine actually changed when an asset was saved**.

An asset can change on disk even when nobody touched it. A normal binary diff only says that bytes differ. This plugin parses the `.uasset` / `.umap`, matches the bytes back to properties, tables and native data, and tells you why the file changed: a property, a moved export, a regenerated GUID, a rewritten header, or something Unreal did on its own.

Targets **Unreal Engine 5.8.2** and works on **vanilla engine builds** (no engine changes).

## What it does

- **Inspect** a package: header, name/import/export maps, exports, decoded properties, with offsets and sizes. [Details](docs/Features.md#package-inspection)
- **Diff** two packages semantically: properties are matched by identity, not by byte position, so a value that moved is not a change. Side-by-side hex with linked scrolling. [Details](docs/Features.md#structural-and-semantic-diffing)
- **Explain a save**: meaningful changes, layout changes, moved exports, and how many changed bytes are still unexplained. [Details](docs/Features.md#save-explanation)
- **Monitor** assets and spot values that change on every save. [Details](docs/Features.md#repeated-save-analysis)
- **Test a no-op resave**: does Unreal rewrite this asset when nothing was edited, and does it do so every time? Works on one asset, a folder or a whole project. [Details](docs/Features.md#no-op-resave-test)
- **Compare** two folders (for example before and after an engine upgrade), or an asset against a source control revision. [Details](docs/Features.md#folder-comparison-and-engine-versions)
- **Read native data** instead of showing opaque bytes: Blueprint classes and bytecode, graph pins, textures (source and cooked), static and skeletal meshes, morph targets. [What is read](docs/NativeData.md)
- **Report** to text, JSON or self-contained HTML, from the editor or a headless commandlet. [Details](docs/Usage.md#running-headless)

## Example

```text
Save Analysis
    Result: Semantic and native/undecoded changes

    Meaningful changes
        TestFloat
            1.0 -> 2.0

    Layout / serialization
        Default__BP_Box_C
            payload unchanged but moved by +16 bytes
            likely cause: preceding serialized data grew by 16 bytes

    Unexplained
        8 native/undecoded changed bytes
```

Every line navigates back to the exact old and new bytes. More examples, including repeated-save patterns, are in [Concepts](docs/Concepts.md#example).

## Install

1. Copy or clone the plugin to `YourProject/Plugins/AssetSerializationInspector/`.
2. Regenerate project files if needed and build the Editor target.
3. Enable **Asset Serialization Inspector** in *Edit -> Plugins* and restart the editor.

Other engine versions may need small serialization-version adjustments.

## Quick start

- **Look at an asset**: *Window -> Asset Serialization -> Asset Serialization Inspector*, pick a `.uasset`.
- **Compare two assets**: open the diff window, choose the old and the new file, press **Compare**.
- **Watch what saves do**: right-click an asset in the Content Browser -> *Asset Serialization* -> *Start Monitoring*, then save it and open the diff.
- **Check whether a resave is a no-op**: right-click an asset or a folder -> *Run No-op Resave Test*.

All the menus, windows and commandlet options are in [Usage](docs/Usage.md).

## Documentation

| Document | What is in it |
|---|---|
| [Features](docs/Features.md) | Every feature in detail: decoding, diffing, save analysis, monitoring, reports, folder and source control comparison |
| [Native data](docs/NativeData.md) | What the plugin reads inside the binary parts of an export (classes, bytecode, graphs, textures, meshes) |
| [Usage](docs/Usage.md) | Windows, menus, the commandlet and build machine checks |
| [Concepts](docs/Concepts.md) | Serialized defaults, undecoded data, a worked example |
| [Architecture](docs/Architecture.md) | How the pieces fit together and why no engine changes are needed |
| [Development](docs/Development.md) | Running the tests, contributing |
| [Roadmap](docs/Roadmap.md) | What is done and what is next |

## Status

Experimental, intended as a serialization and debugging tool. The `.uasset` format depends heavily on versions and many classes serialize custom data, so the decoder is conservative: it validates ranges and leaves a region marked `<native / undecoded>` rather than guess.

Known limits:

- Native C++ defaults are read from the running editor, for top-level properties only; set and map deltas still assume an empty default there.
- Package versions this editor build cannot read are reported as failed, not guessed.
- Most of the UI has been exercised through automation tests and small assets. Large projects and assets from several engine versions deserve more real-world testing.

## Principles

- **Vanilla engine**: no custom engine patches.
- **Decode, don't guess**: unsupported regions stay explicitly unknown.
- **Semantic diff before byte diff**: offsets are implementation details.
- **Raw bytes stay accessible**: every explanation traces back to exact offsets.

## Contributing

Bug reports, serialization findings and test assets are welcome. Please include the Unreal Engine version and the asset type. See [Development](docs/Development.md).

## License

BSD 3-Clause, see [LICENSE](LICENSE).

This project is an independent developer tool and is not affiliated with or endorsed by Epic Games. Unreal Engine and related names are trademarks or registered trademarks of Epic Games, Inc.
