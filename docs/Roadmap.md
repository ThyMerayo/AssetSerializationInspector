## Roadmap

Implemented or substantially in place:

- [x] Primitive property value decoding (all fixed-width integers added in #1)
- [x] Enum and byte decoding (#1)
- [x] Object/package and soft-reference decoding (#1; soft paths via the header table only)
- [x] Text property decoding: None, Base and StringTableEntry histories (#2)
- [x] Common Unreal struct decoding
- [x] Recursive tagged-struct decoding
- [x] Array decoding and sequence-aware semantic comparison
- [x] Set decoding with delta-operation awareness
- [x] Map decoding with key-based semantic comparison (replace-marker maps fixed in #4)
- [x] Residual native/unknown byte attribution
- [x] Save-change classification
- [x] Export relocation and preceding-size-change inference
- [x] Recent observed-save history
- [x] Repeated-field/value analysis across saves
- [x] Historical save navigation by semantic property path

Planned next steps:

- [x] Final values for delta-serialized sets and maps, resolved through the archetype chain in package files (#4, top-level properties only)
- [ ] Final values for containers nested in structs, and for newly serialized or removed containers
- [x] Default-value reconstruction for omitted top-level properties, shown in the diff from the archetype chain (#5)
- [ ] Default values for properties omitted inside structs
- [ ] Zero/empty default for Blueprint-declared properties omitted by their class default object
- [ ] Live-reflection fallback for native defaults the package chain cannot provide
- [ ] Text histories that carry arguments (formatted, number, date/time, transform, generator)
- [ ] Real-asset fixture tests for the decoders and archetype resolution
- [x] Search and state filtering for the structural diff (#6)
- [x] Package header diff: every summary field plus per-region byte comparison with hex view (#9)
- [ ] Describe header changes in the Save Analysis (for example which tables grew)
- [x] Search and filtering for Save Analysis (confidence) and repeated-save patterns (pattern) (#11)
- [x] No-op resave test mode: two temporary resaves per asset with a stable / normalized / unstable verdict (#8)
- [x] Exportable text/JSON analysis reports for a comparison (#7); reports for batches of assets reuse `FAssetAnalysisReport`
- [x] Batch/project-wide save analysis: the no-op resave test over selected assets, folders or the whole project, with a text/JSON report (#13)
- [ ] Results window for a batch run (browse assets, open one asset's diff)
- [x] Cross-engine/version comparison: compare two folders of assets with a grouped text/JSON report, and version-aware explanations in the two-file diff (#14)
- [ ] Results window for a folder comparison (browse pairs, open one pair's diff)
- [ ] Verify the folder comparison on packages that really were saved by different engine versions
- [x] Additional property/native serializer coverage driven by real assets (coverage scan `ASI.DecodeCoverage`, gameplay tag and Vector2f structs; delegates and soft object path table fixed; remaining gaps: GameplayEffectVersion, FieldPath, Box2f, unversioned properties)
- [ ] Persistent monitored-asset configuration
- [ ] Support additional UE5 package versions
