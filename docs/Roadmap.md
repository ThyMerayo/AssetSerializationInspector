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
- [ ] Search and filtering for structural diff, Save Analysis, and repeated-save patterns
- [ ] No-op resave test mode
- [ ] Exportable text/JSON analysis reports
- [ ] Batch/project-wide save analysis
- [ ] Cross-engine/version comparison workflows
- [ ] Additional property/native serializer coverage driven by real assets
- [ ] Persistent monitored-asset configuration
- [ ] Support additional UE5 package versions
