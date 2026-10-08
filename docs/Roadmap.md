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
- [x] Describe header changes in the Save Analysis (which tables grew, shrank, appeared or went; changed summary fields)
- [x] Search and filtering for Save Analysis (confidence) and repeated-save patterns (pattern) (#11)
- [x] No-op resave test mode: two temporary resaves per asset with a stable / normalized / unstable verdict (#8)
- [x] Exportable text/JSON analysis reports for a comparison (#7); reports for batches of assets reuse `FAssetAnalysisReport`
- [x] Batch/project-wide save analysis: the no-op resave test over selected assets, folders or the whole project, with a text/JSON report (#13)
- [x] Results window for a batch run (browse assets, filter, open one asset's first or second resave diff)
- [x] Cross-engine/version comparison: compare two folders of assets with a grouped text/JSON report, and version-aware explanations in the two-file diff (#14)
- [x] Results window for a folder comparison (browse pairs, filter, open one pair's diff)
- [x] Verify the folder comparison on packages that really were saved by different engine versions (UE 5.0 vs 5.8 Blueprint; a real before/after of one asset still wanted)
- [x] Additional property/native serializer coverage driven by real assets (coverage scan `ASI.DecodeCoverage`, gameplay tag and Vector2f structs; delegates and soft object path table fixed; GameplayEffectVersion and boxes done; FieldPath done, every tagged property in the project survey decodes; remaining gap: unversioned properties and native export data)
- [x] Persistent monitored-asset configuration (per-user setting, edits apply at once, follows renames and deletions)
- [x] Support additional package versions: property tags from packages before UE 5.4 (legacy layout); unverified on real older packages, value layouts of older versions not audited

Next steps, in order:

- [x] Close the remaining decode failures found by the coverage scan (235 -> 37 failure kinds, 100.0% of tagged properties decode; a few rare native structs remain)
- [x] Summaries for native export data: changed native ranges are reported and described by class with facts from the properties; the byte totals of the Save Analysis are now computed (native bytes themselves not parsed)
- [x] Class schemas from live reflection: untyped struct elements in older packages' maps and sets (packages saved with unversioned properties move to the cooked-package item below)
- [x] Levels (`.umap`) and split packages (`.uexp`); cooked packages without property tags are reported as undecoded (`.ubulk` is not read)
- [x] Decode unversioned (tagless) properties of cooked packages from the class schema in live reflection
- [x] Containers inside arrays of structs: final values and omitted-field defaults
- [x] Headless commandlet for the no-op resave test and the folder comparison, with a report file, for CI and engine upgrades
- [x] Compare an asset with a source-control revision (Perforce, git) using the Save Analysis
- [x] HTML report alongside the text and JSON ones
- [x] Window listing all monitored assets, with a way to add a folder
- [x] Show the elements that did decode when one element of an array fails, instead of only "the parent changed"
- [x] Infer the engine version from the package file version when the package does not name one (source builds)
- [x] Track header changes in the repeated-save analysis; sortable columns and multi-select in the two results windows
- [x] Run the automation suite from one script (`Scripts/RunAutomationTests.ps1`); no CI, because the tests need an engine built from source and a self-hosted runner on a public repository would run fork code

Open items, in order:

- [x] A hint for a Name property whose value only changed because its index in the Name Map shifted (a Name is stored as an index, so a new name before it changes the bytes)
- [x] Scan for other natively stored structs that decode to a constant placeholder (`ASI.DecodeBlindSpots`; none left in the 5,261 engine packages after the pin type, but it found the items below)
- [x] Show floating point values (floats, doubles, vectors, rotators, quaternions, boxes) with enough digits to tell two different values apart: a change smaller than the printed precision shows as a modified property with the same text on both sides
- [x] The flags of a pin type that are not shown yet (the member reference's guid, the wrapper and single precision flags, a map's value type flags)
- [x] Look into the oddities of the scan: the `ResponseChannel` array was the tag of an empty array counted as content, the Name bytes were a change of case that the diff could not see (fixed), and the `ScalarProperty` was float precision (fixed earlier)
- [x] Tests for the type of variables that hold an object (struct or class) and for maps
- [x] Graph nodes whose class writes more than the pins, formatted texts in pin display names and default values, and a run over a large real graph (the engine content: 8,873 nodes, now all read to their last byte; the cast node writes its purity, texts use the property decoder)
- [x] Disassemble the bytecode of functions instead of comparing its size and bytes (the 157 functions of the engine content read, and their in-memory size adds up; the diff lists statements)
- [x] Decode the contents of meshes and textures: the record of the source image of a texture and of the mesh description of a static mesh (content hash, size, storage); 4,669 of the 4,670 of the engine content
- [x] The native data of static meshes: collision, sockets, lighting GUID and material slots (all 184 of the engine content); the geometry is the mesh description, and the render data of a cooked mesh is not read
- [x] The start of the native data of skeletal meshes: bounds, material slots and reference skeleton (bones, parents, poses); all three of the engine content
- [x] The imported model of skeletal meshes: LODs, sections, vertices (as a hash), index buffers and bones; all three of the engine content
- [x] Skeletal meshes saved by 4.2x and early 5.0 versions (the Mannequins, four of the 58 in the engine's plugins): the raw point indices and the reduction sources as blocks of bulk data, and the sections and their settings without the ray tracing flag; all 58 of the engine's plugins
- [ ] Skeletal meshes from before the model and the render data were split (their vertices, skin weights, colors, adjacency and cloth in separate buffers); no sample in the engine
- [ ] Skeletal meshes with cloth data or skin weight profiles, and the render data of cooked skeletal meshes: no sample in the engine content, and the engine's skeletal meshes did not cook with `-CookDir`, so a sample has to be made first (these fail with a reason until then)
- [x] Morph targets: objects of their own (`UMorphTarget`, with LOD models of their vertex deltas), read to the last byte with a hash of the deltas of each LOD (858 of the engine's plugins)
- [ ] Cooked morph targets that store their deltas compressed (`FDeltaBatchHeader` and the packed deltas): no sample, they fail with a reason
- [x] Texture classes that write more after the texture's data: `LightMapTexture2D` adds its lightmap flags (the 69 exports of the engine's plugins that ended with unread bytes)
- [x] The platform data of cooked textures (pixel format, size, mips with their storage and a hash of their pixels), and the source data of packages from before the editor bulk data (the older bulk data format)
- [x] The tiled data of virtual textures: layers, tile layout and the chunks with the hash the engine keeps for each (the 6 of a cook of the engine's textures)
- [ ] Cooked textures that store their platform data as derived data references (a cook option): no sample, they fail with a reason
- [x] Decode the pixels of the source image of a texture and say what changed in them (count, where, largest change, average color); the delta transform is undone, PNG/JPEG, virtualized and legacy images are reported as not compared
- [x] Decode the vertices of a mesh description (counts, bounds, the position of each vertex) and say what moved
- [x] Compare the other attributes of a mesh description: normals, tangents, UV channels, material slots and which triangles use them
- [x] Read the render data of a cooked static mesh: LODs with their sections and bounds, the inline vertex and index buffers (counted and hashed), the screen sizes and bounds; 85 of 85 meshes of a cook of the engine's meshes
- [ ] Decode what lies between the LODs and the bounds of the render data of a cooked static mesh (Nanite resources, ray tracing proxy, card representation, distance fields), which is only hashed, and the LODs whose buffers stream from a sidecar file
- [ ] Compare the colors, edge hardness and smoothing groups of a mesh description
- [x] Compare the pixels of cooked mips block by block (BC, ASTC and uncompressed formats): how many blocks changed, how much of the mip, and where
- [ ] Decode the colors of block compressed mips (BC and ASTC decoders) and the pixels of source images that are virtualized or compressed as PNG/JPEG; a change that is only in the sidecar file of a streamed mip is not seen, since the package itself is the same
