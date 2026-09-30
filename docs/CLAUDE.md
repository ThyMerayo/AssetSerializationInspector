# AssetSerializationInspector — Claude Code instructions

Unreal Engine 5.8 editor plugin (C++) that parses and diffs binary `.uasset` package files,
explains what changed on save, and tracks repeated-save patterns. See `README.md` for the
user-facing overview and design principles.

## Working rules (mandatory)

- **Do not trust the existing code.** It compiles and runs but contains bugs. Verify behavior
  against real assets and engine source before building on any existing function. Do not treat
  current output as ground truth in tests — derive expected values from the format/engine source.
- **Never commit or push to `main`.** For every piece of work:
  1. `git checkout main && git pull`
  2. Create a branch: `fix/<short-name>`, `feat/<short-name>`, or `test/<short-name>`
  3. Keep each branch scoped to one root cause or feature
  4. Push the branch and open a pull request against `main` (e.g. `gh pr create --base main`)
  5. Never merge PRs yourself — the owner reviews and merges
- PR descriptions must state: the root cause, what changed, how it was verified, and any
  known follow-ups left out of scope.
- Fix tightly around the identified root cause. Track follow-ups explicitly rather than
  fixing things speculatively.

## Architecture

- `FAssetPackageDocument` (`Model/`) — parsed package: raw bytes, summary, name/import/export
  maps, path resolution (`ResolveImportPath`, `ResolveExportPath`), range validation.
- `FAssetPackageExportEntry` — export map entry; `ObjectFlags` captured as `uint32`.
- `FAssetPackageFieldDecoder` (`Trace/`) — builds per-export field traces from the tagged
  property stream; unknown/native bytes recorded via `AddUnknownRange`.
- `FAssetPropertyTagDecoder` — reads `FPropertyTag` headers (`ReadTag`).
- `FAssetPropertyValueDecoder` (`Serialization/AssetPropertyValueDecoder.cpp`) — recursive
  value decoding with semantic keys.
- Diff: `AssetPackageDiff` (structural + semantic property diff), `FAssetByteDiff`,
  `FAssetDecodedValueDiffer` (LCS-based container matching).
- Save analysis: `FAssetSaveAnalyzer`, repeated-save analysis, `FAssetSaveObserver`,
  `FAssetMonitoringManager`, `FAssetSaveHistoryManager`.
- UI: `SAssetSerializationInspector` (single-file inspector) and `SAssetSerializationDiff`
  (diff tree, Save Analysis, Repeated Save Analysis, hex views).

## Key serialization facts

- `Default__*_C` exports are Class Default Objects, not classes. Discriminate with
  `RF_ClassDefaultObject` via `FAssetPackageDocument::IsExportClassDefaultObject()`, not with
  class-chain or import-name checks.
- Some structs (e.g. `FTransform` sub-fields) go through the tagged-property fallback
  (`FStructProperty::SerializeItem`) and carry `FPropertyTag` headers. Others use native
  serialization with no tags (`UScriptStruct::UseNativeSerialization()`).
- `ReadTag` itself is reliable. Crashes in `DecodeTaggedStruct` happen when it is called on
  natively serialized structs, misreading raw value bytes as tag metadata.
- Useful engine sources: `PropertyTypeName.cpp`, `Class.cpp`, `FStructProperty::SerializeItem`,
  `UScriptStruct::UseNativeSerialization()`.

## Tests

- UE automation tests live in `Source/AssetSerializationInspector/Private/Tests/`, wrapped in
  `#if WITH_DEV_AUTOMATION_TESTS`, using `IMPLEMENT_SIMPLE_AUTOMATION_TEST` with flags
  `EditorContext | ProductFilter`.
- Naming: `AssetSerializationInspector.<Area>.<Class>.<Behavior>`
  (e.g. `AssetSerializationInspector.Diff.AssetByteDiff.DetectsSingleContiguousChange`).
- Run headless: `UnrealEditor-Cmd <Project>.uproject -ExecCmds="Automation RunTests AssetSerializationInspector; Quit" -unattended -nullrhi -log`
- Existing coverage: serialization primitives, `FAssetPackageDocument`, `FAssetByteDiff`,
  `FAssetDecodedValueDiffer`, trace range helpers.

## Done (keep as regressions)

- CDO gating: `IsExportClassDefaultObject()` is used in `DecodeExport`; parameter is
  `bIsClassDefaultObject`. `IsExportUClass` / `IsCoreUObjectClassImport` still exist — check
  whether anything still uses them before removing.
- Layout: `SAssetSerializationDiff` header has `SaveAnalysisScrollBox` /
  `RepeatedSaveAnalysisScrollBox` members.

## Known open issues

- `AssetPackageDiff.cpp`, `BuildOnePropertyDiff`, "not serialized → present" branch: after
  decoding the new value it sets `bHasOldDecodedValue = true` instead of
  `bHasNewDecodedValue = true`, so the new value is likely not flagged for display.
- Verify `SAssetSerializationDiff::UpdateSaveAnalysisLayout()`: if it calls
  `SaveAnalysisBox->SetContent(...)` it silently removes the scroll wrapper on refresh; updates
  should go through the scroll boxes.
- Native-vs-tagged struct ambiguity: `DecodeStructFromReader` falls back to
  `DecodeTaggedStruct` for every unknown struct. Planned mitigation: speculative read with
  rollback — record reader position, attempt `ReadTag`, validate cheaply (name plausibility,
  type name sanity, size bounds), and on failure seek back and report the range as an
  undecoded native block (`Unsupported`) instead of crashing.

## Roadmap

- Test gaps: `FAssetPackageFieldDecoder` (incl. CDO control-byte handling),
  `FAssetPropertyValueDecoder`, `AssetPackageDiff`, `FAssetSaveAnalyzer`.
- Functional/integration tests that run the real pipeline (package read → field decode →
  diff → save analysis) against real-world `.uasset` fixtures.
- Extend decoder testing to C++-authored `UPROPERTY()` properties, including native classes
  with custom `Serialize()` overrides (likely repro for the `DecodeTaggedStruct` crash path).
- Explain engine-side changes: the tool explains user-made edits well, but not the extra
  changes Unreal makes on save (e.g. adding a Blueprint property touches many other parts of
  the package). These should be identified and explained in the diff.
