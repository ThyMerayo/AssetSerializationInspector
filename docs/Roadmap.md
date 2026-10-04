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
- [x] Final values for containers nested in structs (arrays of structs not covered); newly serialized containers use the stated empty-default assumption
- [x] Default-value reconstruction for omitted top-level properties, shown in the diff from the archetype chain (#5)
- [x] Default values for properties omitted inside structs (struct properties only, not structs inside containers)
- [x] Zero/empty default for Blueprint-declared properties omitted by their class default object (needs tagged properties; not for unversioned packages)
- [x] Live-reflection fallback for native defaults of omitted top-level properties (containers still assume an empty default; unversioned-property schemas not done)
- [x] Text histories that carry arguments (formatted, number, date/time, transform; generators shown opaquely); only unit-tested, no real asset yet
- [x] Real-asset fixture tests for the decoders and archetype resolution (BP_BOX50 from UE 5.0, BP_Box1 from this engine; found that only class default objects had their properties decoded)
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
- [x] Additional property/native serializer coverage driven by real assets (coverage scan `ASI.DecodeCoverage`, gameplay tag and Vector2f structs; delegates and soft object path table fixed; GameplayEffectVersion and boxes done; FieldPath done, every tagged property in the project survey decodes; remaining gap: unversioned properties and native export data)
- [ ] Persistent monitored-asset configuration
- [x] Support additional package versions: property tags from packages before UE 5.4 (legacy layout); unverified on real older packages, value layouts of older versions not audited
