# AssetSerializationInspector — Claude Code instructions

Unreal Engine 5.8 editor plugin (C++) that parses and diffs binary `.uasset` / `.umap` package files, explains what
changed on save, tracks repeated-save patterns, and checks that saving an asset changes nothing. See `README.md` for the
user-facing overview and `docs/Roadmap.md` for what has landed and what is still open.

## Working rules (mandatory)

- **Do not trust the existing code.** It compiles and runs but has contained bugs. Verify behavior against real assets and
  engine source before building on any existing function. Do not treat current output as ground truth in tests — derive
  expected values from the format/engine source, or from a second independent path (the unversioned-properties tests save
  the same object with and without tags and require identical values).
- **Never commit or push to `main`.** For every piece of work:
  1. `git fetch`, then branch from `origin/main`: `fix/<short-name>`, `feat/<short-name>`, `test/<short-name>`,
     `docs/<short-name>` or `style/<short-name>` (`main` may be checked out in another worktree, so branch from
     `origin/main` rather than checking `main` out; re-check `git branch --show-current` before committing)
  2. Keep each branch scoped to one root cause or feature
  3. Push the branch and open a pull request against `main` (`gh pr create --base main`)
  4. Never merge PRs yourself — the owner reviews and merges
- PR descriptions state: the root cause (or why), what changed, how it was verified, what was **not** verified, and known
  follow-ups left out of scope. Say plainly when something was only checked by constructed data or not run in the editor.
- Fix tightly around the identified root cause. Track follow-ups explicitly rather than fixing things speculatively.
- Update `README.md` in the same PR as a feature or behavior change, and tick `docs/Roadmap.md` as items land.
- **Format before every commit**: run `clang-format` (the `.clang-format` at the plugin root; tabs, 200 columns, Allman
  braces) on the C++ files you changed. clang-format keeps each file's line endings; files written by scripts come out
  unformatted.
- **Include order** (checked by `python Scripts/CheckIncludeOrder.py`, exit 1 on a violation): blocks separated by one blank
  line in this order — Engine, other plugins, this plugin's own headers, external libraries; a block holds one kind and two
  blocks of one kind are one block; a `.cpp`'s own header is the first include, alone in its block; a `.generated.h` is the
  last include, alone in its block.

## Build and test

- The plugin is built inside a host project (`D:\dev\ASIHost`, with the plugin linked or copied into its `Plugins` folder)
  and an engine built from source. `Scripts/RunAutomationTests.ps1 -EngineRoot <engine> -HostProject <.uproject>
  [-LinkPlugin]` builds, runs every test with `UnrealEditor-Cmd`, runs the widget tests with the full editor, and fails on a
  failed test, an unfinished queue or no tests. Run it before opening a PR. There is no CI: the tests need an engine built
  from source and a self-hosted runner on a public repository would run fork code.
- Close any running editor first: it locks the plugin's DLL (LNK1104). A leftover `UnrealEditor-Cmd.exe` does too.
- **Widget (Slate) tests are skipped under `UnrealEditor-Cmd`** (Slate is not initialized): they must also run with
  `UnrealEditor.exe -nullrhi`. A widget that was never constructed has crashed in the editor before.
- Commandlet: `-run=AssetSerializationInspector -Mode=NoOpResave|CompareFolders|DecodeCoverage` (see its header).
- In Git Bash, prefix commands that take `/Game/...` paths with `MSYS_NO_PATHCONV=1`.

## Architecture (`Source/AssetSerializationInspector`, `Public/` and `Private/` mirror each other)

- `Model/` — `FAssetPackageDocument`: raw bytes (header file plus its `.uexp` when split), summary, name/import/export maps,
  path resolution, range validation, `IsExportClassDefaultObject()`; `AssetPackageHeaderLayout` describes the header's regions.
- `Readers/` — `AssetPackageReader` loads `.uasset` / `.umap` (and a `.uexp` through its header), `AssetPackageMemoryReader`
  applies the package's versions to table readers, `AssetPackagePayloadReader` resolves names and object references.
- `Trace/` — `FAssetPackageFieldDecoder` builds per-export traces: one node per property (tagged, or read from an unversioned
  stream), unknown/native bytes as native ranges (`AddUnknownRange`).
- `Serialization/` — `AssetPropertyTagDecoder` (modern and pre-5.4 tags), `AssetPropertyValueDecoder` (recursive values with
  semantic keys; `Partial` status for arrays that decoded in part), `AssetSchemaReflection` (struct names and defaults from the
  running editor's classes), `AssetUnversionedProperties` (tagless streams), `AssetArchetypeResolver` (values inherited from
  archetypes, Blueprint defaults, live reflection), `AssetContainerFinalValue` (set/map deltas).
- `Diff/` — `AssetPackageDiff` (structural, semantic, header, native-range diffs), `AssetByteDiff`, `AssetDecodedValueDiff`
  (LCS-based containers), `AssetPackageHeaderDiff`, `AssetDiffFilter`.
- `Save/` — `AssetSaveAnalyzer` (explains a save; totals are property + header + native bytes), `RepeatedSaveAnalyzer`,
  `AssetSaveObserver` / `FAssetMonitoringManager` (monitored assets, a per-user setting), `AssetSaveHistoryManager`,
  `AssetNoOpResaveTest`, `AssetBatchResave`.
- `Compare/` — folder comparison, source control revision comparison, engine version inference from the file version.
- `Summary/` — `AssetExportSummary` (what native bytes are, per class). `Coverage/` — decoder coverage scan.
- `Report/` — text, JSON and HTML writers for the asset analysis, batch and folder reports (`AssetHtmlReport` helpers).
- `Commandlets/` — the headless entry point. `Widgets/` — inspector, diff window, batch and folder results windows,
  monitored assets window.

## Key serialization facts

- Since UE 5.4 (`PROPERTY_TAG_EXTENSION_AND_OVERRIDABLE_SERIALIZATION`) **every** object export starts its tagged properties
  with a serialization-control byte, not only class default objects. Older packages use the legacy tag layout, have no script
  serialization offsets, and do not name the struct of map/set elements (`AssetSchemaReflection::CompleteType` supplies it).
- `Default__*_C` exports are Class Default Objects, not classes; discriminate with `RF_ClassDefaultObject`.
- A struct is read as tagged properties only when it is not natively serialized: the tag's `BinaryOrNative` flag says so;
  natively serialized structs with an unknown layout are reported unsupported, never read as tags. Booleans are `uint32` in
  binary archives.
- Packages saved with `PKG_UnversionedProperties` have no tags: a fragment header, a zero mask, then values in the order of the
  class's `PropertyLink` list. They are read with the running editor's classes (not possible for Blueprint classes).
- Bytes outside the tagged properties (mesh, texture, compiled class data) are not parsed; they are native ranges, reported
  with a class-based description. Source builds save an empty engine version; the package file version is the dependable sign.
- Useful engine sources: `PropertyTypeName.cpp`, `Class.cpp`, `UnversionedPropertySerialization.cpp`,
  `FStructProperty::SerializeItem`, `ObjectVersion.h` (file versions per release tag).

## Tests

- UE automation tests live in `Private/Tests/`, wrapped in `#if WITH_DEV_AUTOMATION_TESTS`, using
  `IMPLEMENT_SIMPLE_AUTOMATION_TEST` with flags `EditorContext | ProductFilter`.
- Naming: `AssetSerializationInspector.<Area>.<Class>.<Behavior>`.
- Real fixtures are in `Resources/TestFixtures` (`BP_BOX50` from UE 5.0, `BP_Box1` from this engine); `*.uasset` is binary in
  `.gitattributes`. Tests that save packages use `/Game/__AssetSerializationInspectorTests/...` and must delete what they write.
- Tests must not touch global state (the save observer, the global save history, the monitored list): a run in the same editor
  session would see the previous run's data, and a made-up save announced through the observer shows a notification.

## Pitfalls met before

- Unity builds: adding a file reshuffles which `.cpp` files share a translation unit and exposes missing includes and clashing
  names in file-local helpers (give test helpers unique names).
- Not exported by the engine (link errors): `FPropertyTag`, `FPackageFileSummary::SetPackageFlags`.
- `FProperty::ExportText_Direct` appends to its string; `FParse::Value` stops at commas unless told not to;
  `SLATE_ARGUMENT` raw pointers are uninitialized (use `SLATE_ARGUMENT_DEFAULT(T*, Name) = nullptr;`).
- Python scripts that edit sources must read and write them as latin-1 (cp1252 bytes exist) and keep CRLF line endings.

## Known gaps (also in `docs/Roadmap.md`)

- Native bytes are described, not parsed (no vertex counts or mip tables); a few rare native structs are undecoded.
- Sets and maps still fail as a whole when an element fails (arrays decode in part).
- Layout changes are not part of the repeated-save patterns.
- Unversioned reading needs the editor to have the class: Blueprint classes and natively serialized structs stay undecoded.
- `.ubulk` and similar bulk files are not read.
