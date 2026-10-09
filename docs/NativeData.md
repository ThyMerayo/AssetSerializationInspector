# Native data

Some bytes of an export are not tagged properties: mesh and texture data, compiled Blueprint classes, graph pins. A plain diff can only count them. This page lists what the plugin reads inside them and what the diff then says.

## How it works

- Every native range that changed gets its own entry in the diff, with the number of changed bytes.
- The entry says what the data is for the export's class, with facts read from its properties. For example: `Texture2D: texture data (mips and platform data). imported size: X=4096 Y=4096, source format: TSF_BGRA8`, or `SkeletalMesh: mesh render data (LODs, vertices, skin weights, morph targets) (934,452 of 969,237 bytes are native data). Source LODs: 3, skeleton: ...`.
- Where a reader exists, it must account for the **last byte** of the range. Then the diff lists the individual changes (a variable added, a vertex hash changed) and the Save Analysis counts the range as explained.
- If a reader cannot do that on both sides (an unknown layout, a version it does not handle), the range stays opaque and the Save Analysis counts it as *unexplained*. The reader never guesses.
- The *Changed bytes*, *Explained* and *Unexplained* totals add up property changes, header changes and native changes.

## What is read

| Data | The diff can tell you | Read on the engine's content |
|---|---|---|
| [Classes and functions](#classes-and-functions) | variables, functions, flags, interfaces added or changed | every generated class, checked against the editor |
| [Function bytecode](#function-bytecode) | statements added, removed or replaced | 157 functions, 16,351 statements |
| [Graph nodes and pins](#blueprint-graph-nodes-and-pins) | pins added, removed or changed, with links and types | 8,873 nodes, 28,854 pins |
| [Texture sources](#texture-source-images) | the source image changed, how many pixels and where | 7,491 of 7,491 with mesh descriptions |
| [Mesh descriptions](#mesh-descriptions) | geometry and attributes of an uncooked static mesh | 7,491 of 7,491 with texture sources |
| [Cooked textures](#cooked-textures) | mips, changed blocks, colors, virtual texture chunks | 369 of 369 in a cook of engine textures |
| [Static meshes](#static-meshes) | slots, sockets, collision, render LODs, Nanite, distance fields, cards | 1,374 of 1,374 (plugins), 184 of 184 (engine content) |
| [Skeletal meshes](#skeletal-meshes) | bones, materials, bounds, LODs, sections, vertices | 58 of 58 (plugins) |
| [Morph targets](#morph-targets) | which LOD's vertex deltas changed | 858 of 858 (plugins) |

The same numbers come from the decoder coverage scan, which also lists what could not be read ([how to run it](Architecture.md#coverage-scan)).

## Classes and functions

A Blueprint's generated class, and any other class or function export, writes after its tagged properties:

- its parent and its child functions,
- the properties it declares (name, type, flags, size, metadata),
- the size of its bytecode,
- for a class: its function map, flags, interfaces and default object.

All of it is decoded to the last byte, and checked against the running editor: every property of a generated class is found with the type, flags and size the editor reports.

In the diff:

- *Variable NewVar_12* added, with its type and flags.
- A function added to the function map.
- A flag set.
- A statement added to a function's bytecode (below).

If the data cannot be read to its last byte on both sides, the range stays opaque.

## Function bytecode

A Blueprint graph compiles to a stream of expressions. The inspector reads it as the engine does (`UStruct::SerializeExpr`) and shows one statement per line:

```text
LocalFinalFunction(/Script/Engine.KismetSystemLibrary:PrintString, StringConst("Hello"), ...)
JumpIfNot(#7, ...)
```

- Calls, variables and properties are named by **path**, so the text does not change when the package numbers its objects differently.
- A jump names the **statement** it goes to (`#7`), not an offset, so inserting a statement does not change the jumps after it.
- The diff lists statements added, removed or replaced.
- As a check, the size the statements add up to in memory must equal the size the function stores. If it does not, or a token is unknown, the bytecode is compared by size only.

On the engine's content all 157 functions with bytecode are read. The coverage scan reports any that are not in its *Bytecode* section.

## Blueprint graph nodes and pins

Any graph node (`UEdGraphNode`: events, function calls, getters, branches, ...) writes its pins after its tagged properties. For each pin: id, name, display name, direction, type, default value, default object and text, links, sub pins and flags. All of it is read to the last byte.

In the diff:

- One change per pin, such as *Pin Target (links)* with the pin described before and after: `Target (input, object (/Script/Engine.Actor)), linked to K2Node_Event_0.Instigator`.
- A pin added or removed, or a changed default value, type or direction.
- Pins are matched by **id**, so a renamed pin is one change, not a removal plus an addition.
- Events, function entries and results also list the pins the user declared (*Declared pin NewParam*).
- A cast node's purity (*Purity*).
- If only the bytes differ because links were renumbered (a node was added before them), no pin is reported and the Save Analysis lists a layout change.

Texts on a pin go through the same decoder as text properties, so every text history is read. A node whose data cannot be read to its last byte on both sides (a class that writes more than its pins) stays opaque.

**Pin types** (what a pin, a Blueprint variable or a declared parameter holds) are read and described too: `name`, `int`, `array of object (/Script/Engine.Actor)`, `map of name to int`, with `by reference`, `const`, `weak`, `object wrapper`, `single precision`, and the member a delegate or function pin refers to. Changing a variable from `Name` to `Integer` now shows in the Blueprint's `NewVariables`, not only in the generated class.

Coverage: on the 5,261 packages of the engine content, 8,873 nodes, all read, 28,854 pins.

## Texture source images

A texture keeps its source image outside its properties, as editor bulk data (`FEditorBulkData`). The package stores a small record of it: how it is stored, an identifier, a hash of the content and the size. The pixels live in the package, a sidecar file or a virtualization backend.

In the diff:

- *Source image content* from one hash and size to another.
- **Not differences**: where the data sits in the file, and the identifier a save gives it. A resave of the same image reports no change, with an explanation for the bytes that differ.
- Packages from before editor bulk data use the older format (a header of flags, size and offset, then the payload). That is read too. A resave that only upgraded the format is reported as a change of *storage*, not of the image.

### Pixels

When the source image changed and the package holds its pixels (in the package trailer, as a save writes them), they are decoded and compared:

- how many pixels differ, and the bounding box,
- the largest change of a channel,
- the average color of each image.

Supported: 8 and 16 bit channels, half and full floats. The delta transform a save applies is undone. PNG and JPEG are decoded with the engine's own image reader (the red/blue swap the engine applies to a BGRA PNG is undone).

Reported as **not compared**, with the reason: a virtualized image, one in the older format, a compressed image with several slices, a different size or format, or an image over 256 MB.

A lightmap texture also writes its lightmap flags after the texture data; they are read and compared.

## Mesh descriptions

The geometry of an uncooked static mesh is its **mesh description**, stored as the same kind of bulk data record. The package holds the data in its trailer (or at the offset the record gives), and the inspector deserializes it with the engine's own `FMeshDescription` and the package's versions.

When the content changed, the diff lists:

- **Counts**: vertices, triangles, polygons.
- **Bounds**.
- **Vertices**: when the count is the same, how many moved, the largest move and where.
- **Corner attributes**: normals and tangents (how many changed, largest turn in degrees), UV channels added or removed and how many corners moved, material slots, and how many triangles use another slot.
- **Colors** (how many changed, by how much of the range), **binormal signs**, **hard edges** and the **object name** of each polygon.

A mesh description has no smoothing groups. Those exist only when a mesh is imported; what it keeps of them is the hardness of the edges.

Coverage: texture sources and mesh descriptions together, 7,491 of 7,491 on the engine's plugins (4,670 of 4,670 on the engine content).

## Cooked textures

A texture saved for a platform has no source image. Its platform data is read to the last byte (`UTexture2D::Serialize`, `UTexture::SerializeCookedPlatformData`, `FTexturePlatformData`), using the package's data resource table to locate every mip.

The diff lists:

- the pixel format, size, slices and cube map,
- mips left out by the cook and mips in the tail,
- each mip: its size, whether it is inline or streams from the `.ubulk` (or `.uptnl`) next to the package, and a hash of its pixels (read from the package or the sidecar file when it is there).

Where the pixels are kept in the file moves from cook to cook and is **not** a difference.

### Which part of a mip changed

When a mip kept its size and storage but its pixels changed:

- **Blocks**: BC and ASTC store pixels in fixed-size blocks (4 by 4 pixels in 8 or 16 bytes for BC, the format's block size for ASTC). The diff compares block by block and reports how many differ, what share of the mip, and the box of pixels they cover. Uncompressed formats are compared pixel by pixel.
- **Colors**: for DXT1, DXT3, DXT5, BC4, BC5 and BC7 the blocks are decoded, and the diff adds the largest change of a channel and the average color on each side. Colors are shown as stored, with no gamma conversion. BC4 is a grey, BC5 has red and green.
- BC6H and ASTC are compared by blocks only.
- A mip is compared only when its size is exactly the number of blocks of its format; any other layout is reported as not compared.

### Virtual textures

A virtual texture keeps tiles instead of mips (`FVirtualTextureBuiltData`). The diff lists its layers and their pixel formats, size and mip count, tile size and border, and each chunk of tiles by the hash and size the engine keeps for it, so changed tiles show without reading pixels.

Not read: platform data stored as derived data references (the entry says so).

Coverage: 369 of 369 in a cook of the engine's own textures.

## Static meshes

After its properties a static mesh writes its collision and navigation collision objects, its lighting GUID, its sockets and its **material slots** (the material, slot name, imported name, overlay material, and texture streaming density of its UV channels). It is read to the last byte, including fields that older editors wrote and newer ones only read (the package version decides which exist).

The diff lists a slot added, removed or given another material, a socket, the collision object and the lighting GUID.

### Cooked meshes: render data

A cooked static mesh keeps its geometry in the render data (`FStaticMeshRenderData`). Per LOD:

- sections (material slot, triangles, vertex range, flags), bounds and deviation,
- the vertex and index buffers: counts of vertices, UV channels and indices plus a hash of the bytes when they are in the export, or the reference when they stream from a sidecar file.

Then the mesh bounds, the screen size at which each LOD is drawn, and whether the LODs share static lighting. The diff names the LOD and section that changed, so a changed vertex shows as a changed buffer hash with the counts next to it.

Between the LODs and the bounds the render data holds four more things. Each is read and **named in the diff**:

| Part | What the diff shows |
|---|---|
| **Nanite resources** | clusters, pages, root pages, the triangles and vertices the mesh was built from, the pages that stream |
| **Ray tracing proxy** | whether it exists, how many LODs, whether it shares the render buffers |
| **Card representation** (Lumen), per LOD | each card (origin, extent, axes, the side of the mesh it faces), their bounds, whether the mesh is mostly two sided; a card that moved, was added or was removed is named by its place in the list |
| **Distance field**, per LOD | bounds, the bricks of each of the three mips, the size of the always-loaded part, the streamed mips hashed from the sidecar file |

So a rebuilt distance field shows as *Render LOD 0: distance field* from one brick count to another, and a mesh that gained Nanite data as *Nanite resources* from 0 clusters to 6. Each part is also hashed, so a change the numbers do not show is still reported.

The **contents** of the Nanite pages, the ray tracing proxy's buffers and the distance field volumes are hashed, not decoded. A layout that is not read (an inline ray tracing proxy) falls back to one hash of everything between the LODs and the bounds.

### Source models of older meshes

An editor from before the mesh description was an object of its own writes each source model's data inline in the mesh:

- a **mesh description** per source model (bulk data in the older format, an identifier, a flag), or
- before that, a **raw mesh** (the same record without the leading flag), listed as *raw mesh*.

The number of source models comes from the `SourceModels` property. The diff lists a source model whose content changed, was added or was removed, by the hash of its stored bytes and size, without opening the geometry. A new identifier is not a difference. Only packages older than the section info map stay unread.

Coverage: 184 of 184 on the engine content, 1,374 of 1,374 on the engine's plugins, 85 of 85 in a cook of the engine's own meshes.

## Skeletal meshes

After its properties a skeletal mesh writes its imported bounds, its material slots, its reference skeleton (bones with parent, pose and the name they had in the source file) and the **imported model**: LODs, each with sections, vertices, index buffer and bones. All of it is read (`USkeletalMesh::Serialize`, `FSkeletalMaterial`, `FReferenceSkeleton`, `FSkeletalMeshModel`, `FSkeletalMeshLODModel`, `FSkelMeshSection`, `FSoftSkinVertex`), and the data counts as *explained* only when the last byte is accounted for.

The diff lists:

- a bone added, removed, moved to another parent or given another pose,
- a material slot given another material, and changed bounds,
- a LOD or section added, removed or changed (triangles, vertices, bones, flags, and a hash of its vertices, so moved or repainted vertices are reported),
- the vertices, indices and bones of a LOD as a whole.

Earlier layouts are read back to 4.2x: their raw point indices and reduction sources were bulk data blocks of their own (the older bulk data format), and their sections have no ray tracing flag.

**Not read** (the entry says so, the mesh is read from the start only, and its changed bytes stay unexplained): cloth data, skin weight profiles, older layouts still, and the render data of a cooked skeletal mesh.

Coverage: all 3 on the engine content, all 58 on the engine's plugins.

## Morph targets

A morph target is an object of its own (`UMorphTarget`), read to the last byte (`UMorphTarget::Serialize`, `FMorphTargetLODModel`). Per LOD: how many vertices it moves, a hash of those deltas (position, normal and the vertex they apply to), the base mesh's vertex count, its sections, whether the engine generated it, and the file it was imported from.

The diff names the LOD whose deltas changed, so a moved vertex shows as a change without listing the deltas.

**Not read**: cooked morph targets with compressed deltas, and layouts from before sections were saved (the entry says so).

Coverage: 858 of 858 on the engine's plugins.

## Engine layouts followed

For anyone checking a reader against the engine source:

| Data | Engine serializers |
|---|---|
| Bytecode | `UStruct::SerializeExpr` |
| Graph nodes and pins | `UEdGraphNode`, the pin and pin type structs |
| Texture source and mesh description records | `UTexture::Serialize`, `UTexture2D::Serialize`, `UMeshDescriptionBaseBulkData::Serialize`, `FEditorBulkData::Serialize` |
| Mesh description geometry | `FMeshDescription` (deserialized with the package's versions) |
| Cooked textures | `UTexture2D::Serialize`, `UTexture::SerializeCookedPlatformData`, `FTexturePlatformData`, `FVirtualTextureBuiltData`; sidecar files `.ubulk` and `.uptnl` |
| Static meshes | `UStaticMesh::Serialize`, `FStaticMeshRenderData`, `FStaticMeshSourceModel` (older editors) |
| Skeletal meshes | `USkeletalMesh::Serialize`, `FSkeletalMaterial`, `FReferenceSkeleton`, `FSkeletalMeshModel`, `FSkeletalMeshLODModel`, `FSkelMeshSection`, `FSoftSkinVertex` |
| Morph targets | `UMorphTarget::Serialize`, `FMorphTargetLODModel` |
