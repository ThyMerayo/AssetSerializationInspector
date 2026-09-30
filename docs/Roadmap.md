## Roadmap

Implemented or substantially in place:

- [x] Primitive property value decoding
- [x] Enum and byte decoding
- [x] Object/package and soft-reference decoding
- [x] Common Unreal struct decoding
- [x] Recursive tagged-struct decoding
- [x] Array decoding and sequence-aware semantic comparison
- [x] Set decoding with delta-operation awareness
- [x] Map decoding with key-based semantic comparison
- [x] Residual native/unknown byte attribution
- [x] Save-change classification
- [x] Export relocation and preceding-size-change inference
- [x] Recent observed-save history
- [x] Repeated-field/value analysis across saves
- [x] Historical save navigation by semantic property path

Planned next steps:

- [ ] Default-value reconstruction for omitted properties and final delta-container values
- [ ] Search and filtering for structural diff, Save Analysis, and repeated-save patterns
- [ ] No-op resave test mode
- [ ] Exportable text/JSON analysis reports
- [ ] Batch/project-wide save analysis
- [ ] Cross-engine/version comparison workflows
- [ ] Additional property/native serializer coverage driven by real assets
- [ ] Persistent monitored-asset configuration
- [ ] Support additional UE5 package versions
