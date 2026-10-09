// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "astc_thunk.h"

/**
 * Decodes ASTC images with the astcenc library the engine itself encodes them with. The engine loads that library as a dynamic library
 * through a small thunk (astc_thunk.h); this loads the same one. When the library cannot be loaded the colors of ASTC mips are not
 * decoded and the mips are compared by blocks only.
 */
namespace AssetAstcDecoder
{
	/** The entry points of the thunk. */
	struct FThunk
	{
		AstcThunk_CreateFnType* Create = nullptr;
		AstcThunk_DoWorkFnType* DoWork = nullptr;
		AstcThunk_DestroyFnType* Destroy = nullptr;
	};

	/** The thunk, loaded the first time. Null when the library is not there, with the reason in OutError. */
	const FThunk* Load(FString& OutError);

	/**
	 * Decodes a whole 2D image stored as ASTC blocks of BlockSize by BlockSize pixels (16 bytes each, row by row) to red, green, blue and
	 * alpha. An LDR image gives values from 0 to 1 as stored (no gamma conversion); an HDR image gives half floats converted to float.
	 */
	bool DecodeImage(const uint8* Data, int64 Size, int32 Width, int32 Height, int32 BlockSize, bool bHdr, TArray<FVector4f>& OutPixels, FString& OutError);
} // namespace AssetAstcDecoder
