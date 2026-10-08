// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "Serialization/AssetStructNativeData.h"

struct FAssetPackageDocument;
struct FAssetPackageExportEntry;

/**
 * The record of a block of editor data kept outside the properties of an object (FEditorBulkData): the source image of a texture, the
 * mesh description of a static mesh. The record is small and holds what identifies the data, not the data: its content hash, its size
 * and how it is stored. The data itself is in the package, a sidecar file or a virtualization backend.
 */
struct FAssetBulkDataInfo
{
	/**
	 * A package from before the editor bulk data existed stores the source in the older bulk data format (FByteBulkData): a small header
	 * (flags, size, where the payload is), and the payload inline or at the end of the file. It has no identifier and no content hash
	 * of its own; the hash here is of the bytes as stored, so it is not comparable with the hash of a record in the current format.
	 */
	bool bLegacy = false;

	/** EFlags of FEditorBulkData; for a legacy record, the EBulkDataFlags of the header. */
	uint32 Flags = 0;

	/** The identifier of the bulk data, and the hash of its content (what decides whether it changed). */
	FString Id;
	FString ContentHash;

	int64 PayloadSize = 0;

	/** Where the data is in the file, when the record says so (it moves from save to save). */
	int64 OffsetInFile = INDEX_NONE;

	/** The names of the flags that describe how the data is stored ("Virtualized, StoredInPackageTrailer"). */
	FString DescribeStorage() const;
};

/**
 * One mip of the platform data of a cooked texture: its size and where its pixels are kept. The package holds a small record of the
 * mip; the pixels are inline in the package, or in a sidecar file (.ubulk, .uptnl) that streaming reads when the mip is needed.
 */
struct FAssetTextureMip
{
	int32 SizeX = 0;
	int32 SizeY = 0;
	int32 SizeZ = 0;

	/** The size of the pixels in bytes (BC7 and the like: the encoded size, not the size of the image). */
	int64 PayloadSize = 0;

	/** EBulkDataFlags of the data resource of the mip. */
	uint32 BulkFlags = 0;

	/** Where the pixels are in the package (inline) or in its sidecar file; it moves from cook to cook. */
	int64 Offset = INDEX_NONE;

	/** A hash of the pixels, when they could be read: inline, or in the sidecar file next to the package. Empty otherwise. */
	FString PayloadHash;

	/** Where the pixels are to be read and how many bytes they take: the offset in the document when inline, in the sidecar file otherwise. */
	int64 DataOffset = INDEX_NONE;
	int64 StoredSize = 0;

	bool IsInline() const;

	/** "Inline", "streamed (.ubulk)", "optional (.uptnl)" and the like. */
	FString DescribeStorage() const;

	/** "1024x512, 349,525 bytes, streamed (.ubulk), pixels 3fa2c1d9". */
	FString Describe() const;
};

/** One chunk of the tiled data of a virtual texture: a block of tiles that streams in whole. */
struct FAssetVirtualTextureChunk
{
	/** The hash of the chunk the engine keeps with it (what decides whether the tiles changed). */
	FString ContentHash;

	/** The size of the chunk in bytes. */
	uint32 SizeInBytes = 0;

	/** EBulkDataFlags of the data resource of the chunk. */
	uint32 BulkFlags = 0;

	bool IsInline() const;
	FString DescribeStorage() const;
	FString Describe() const;
};

/** The tiled data of a virtual texture (FVirtualTextureBuiltData): its layers, its tiles and the chunks they are stored in. */
struct FAssetVirtualTextureData
{
	uint32 NumLayers = 0;
	uint32 NumMips = 0;
	uint32 Width = 0;
	uint32 Height = 0;

	/** How many UDIM blocks make up the texture. */
	uint32 WidthInBlocks = 0;
	uint32 HeightInBlocks = 0;

	/** The tile size without its border, and the border added around each tile. */
	uint32 TileSize = 0;
	uint32 TileBorderSize = 0;

	/** The pixel format of each layer ("PF_DXT1"...). */
	TArray<FString> LayerFormats;

	TArray<FAssetVirtualTextureChunk> Chunks;

	/** "2 layers (PF_DXT1, PF_BC5), 4096x4096, 128 pixel tiles with a 4 pixel border, 13 mips, 3 chunks". */
	FString Describe() const;
};

/** The platform data of a cooked texture (FTexturePlatformData): what the cooker made of the source image for one platform. */
struct FAssetTexturePlatformData
{
	/** "PF_DXT5", "PF_BC7"... */
	FString PixelFormat;

	int32 SizeX = 0;
	int32 SizeY = 0;
	int32 NumSlices = 0;
	bool bCubeMap = false;

	/** How many of the smallest mips are packed together in a tail (a layout some platforms need). */
	int32 NumMipsInTail = 0;

	/** The lowest mips of the source that the cook left out (the LOD bias applied by the texture group). */
	int32 FirstMipToSerialize = 0;

	TArray<FAssetTextureMip> Mips;

	/** A virtual texture keeps its pixels as tiles in chunks instead of mips. */
	bool bVirtual = false;
	FAssetVirtualTextureData Virtual;

	/** A hash of the copy of the image that the CPU keeps for some textures (small ones sampled on the CPU); empty when there is none. */
	FString CpuCopyHash;

	/** "PF_BC7, 1024x1024, 11 mips (2 inline, 9 streamed)". */
	FString Describe() const;
};

/**
 * What a texture or the bulk data of a mesh description writes after its tagged properties, decoded: for a texture the record of its
 * source image, and whether it is cooked; for a mesh description the record of the mesh and its id. The pixels and the vertices
 * themselves are not here.
 */
struct FAssetBulkDataExport
{
	/** The whole range was read and nothing was left over. When false, Error says where it stopped. */
	bool bComplete = false;
	FString Error;

	/** The bytes the data covers in the document. */
	int64 Offset = 0;
	int64 Size = 0;

	/** "Texture" or "Mesh description". */
	FString Kind;

	FString ObjectGuid;

	/** The record of the source image or of the mesh description; absent when the editor data was stripped. */
	bool bHasBulkData = false;
	FAssetBulkDataInfo Bulk;

	/** A texture saved for a platform (its mips are stored cooked). */
	bool bCooked = false;

	/** A lightmap texture writes the flags of the lightmap (ELightMapFlags) after the data of the texture. */
	bool bHasLightmapFlags = false;
	uint32 LightmapFlags = 0;

	/** A cooked texture: the data the cook made for each platform it holds (one, in every package the cooker writes). */
	TArray<FAssetTexturePlatformData> PlatformData;

	/** A mesh description: its id, and whether the id is a hash of the content. */
	FString MeshGuid;

	/** A short account: the kind, the size of the data and its hash. */
	FString Summarize() const;
};

namespace AssetBulkDataExport
{
	/**
	 * Decodes the native data of a texture (a UTexture of any kind) or of the bulk data of a mesh description. Returns false, without
	 * touching Out, for any other export.
	 *
	 * @param NativeOffset Where the native data starts in the document (right after the tagged properties).
	 * @param NativeSize How many bytes it has.
	 */
	bool Decode(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, int64 NativeOffset, int64 NativeSize, FAssetBulkDataExport& Out);

	/**
	 * What differs between two versions: the content of the source data (its hash and size, in one change), how it is stored, and for
	 * a texture whether it is cooked. Where the data is in the file, and the identifier the save gives it, are not differences.
	 */
	TArray<FAssetNativeDataChange> Compare(const FAssetBulkDataExport& Old, const FAssetBulkDataExport& New);
} // namespace AssetBulkDataExport
