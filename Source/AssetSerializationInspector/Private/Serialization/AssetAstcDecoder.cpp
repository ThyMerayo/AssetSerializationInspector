// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetAstcDecoder.h"

#include "HAL/PlatformProcess.h"
#include "Math/Float16.h"
#include "Misc/ScopeLock.h"

namespace
{
	void* AstcMalloc(size_t Size, size_t Alignment)
	{
		return FMemory::Malloc(Size ? Size : 1, static_cast<uint32>(Alignment));
	}

	void AstcFree(void* Ptr)
	{
		FMemory::Free(Ptr);
	}
} // namespace

const AssetAstcDecoder::FThunk* AssetAstcDecoder::Load(FString& OutError)
{
	static FCriticalSection Lock;
	static bool bAttempted = false;
	static FThunk Thunk;
	static FString Error;

	FScopeLock ScopeLock(&Lock);
	if (!bAttempted)
	{
		bAttempted = true;

		// The newest version the engine ships first, as it does.
		for (const TCHAR* Version : { TEXT("5.0.1"), TEXT("4.2.0") })
		{
			const FString DllName = FString(TEXT(ASTCENC_DLL_PREFIX)) + Version + TEXT(ASTCENC_DLL_SUFFIX);
			void* Handle = FPlatformProcess::GetDllHandle(*DllName);
			if (Handle == nullptr)
			{
				continue;
			}

			Thunk.Create = reinterpret_cast<AstcThunk_CreateFnType*>(FPlatformProcess::GetDllExport(Handle, TEXT("AstcEncThunk_Create")));
			Thunk.DoWork = reinterpret_cast<AstcThunk_DoWorkFnType*>(FPlatformProcess::GetDllExport(Handle, TEXT("AstcEncThunk_DoWork")));
			Thunk.Destroy = reinterpret_cast<AstcThunk_DestroyFnType*>(FPlatformProcess::GetDllExport(Handle, TEXT("AstcEncThunk_Destroy")));
			AstcThunk_SetAllocatorsFnType* SetAllocators = reinterpret_cast<AstcThunk_SetAllocatorsFnType*>(FPlatformProcess::GetDllExport(Handle, TEXT("AstcEncThunk_SetAllocators")));
			if (Thunk.Create == nullptr || Thunk.DoWork == nullptr || Thunk.Destroy == nullptr || SetAllocators == nullptr)
			{
				Thunk = FThunk();
				continue;
			}

			SetAllocators(&AstcMalloc, &AstcFree);
			break;
		}

		if (Thunk.Create == nullptr)
		{
			Error = TEXT("The astcenc library the engine uses for ASTC could not be loaded");
		}
	}

	if (Thunk.Create == nullptr)
	{
		OutError = Error;
		return nullptr;
	}
	return &Thunk;
}

bool AssetAstcDecoder::DecodeImage(const uint8* Data, const int64 Size, const int32 Width, const int32 Height, const int32 BlockSize, const bool bHdr, TArray<FVector4f>& OutPixels, FString& OutError)
{
	const FThunk* Thunk = Load(OutError);
	if (Thunk == nullptr)
	{
		return false;
	}

	if (Width <= 0 || Height <= 0 || BlockSize < 4 || BlockSize > 12)
	{
		OutError = TEXT("The image has no size or a block size that is not an ASTC one");
		return false;
	}

	const int64 BlocksX = (Width + BlockSize - 1) / BlockSize;
	const int64 BlocksY = (Height + BlockSize - 1) / BlockSize;
	if (BlocksX * BlocksY * 16 != Size)
	{
		OutError = TEXT("The image is not the ASTC blocks of its size");
		return false;
	}

	// The thunk decodes into slices of the image data type: bytes for an LDR image, half floats for an HDR one.
	const int64 PixelBytes = bHdr ? 8 : 4;
	TArray<uint8> Decoded;
	Decoded.SetNumZeroed(static_cast<int64>(Width) * Height * PixelBytes);
	void* Slice = Decoded.GetData();

	FAstcEncThunk_CreateParams Params;
	Params.Flags = EAstcEncThunk_Flags::DECOMPRESS_ONLY;
	Params.Profile = bHdr ? EAstcEncThunk_Profile::HDR_RGB_LDR_A : EAstcEncThunk_Profile::LDR;
	Params.Quality = EAstcEncThunk_Quality::FAST;
	Params.BlockSize = static_cast<uint8>(BlockSize);
	Params.SizeX = static_cast<uint32>(Width);
	Params.SizeY = static_cast<uint32>(Height);
	Params.NumSlices = 1;
	Params.ImageSlices = &Slice;
	Params.ImageDataType = bHdr ? EAstcEncThunk_Type::F16 : EAstcEncThunk_Type::U8;

	// When decoding, the buffer of the parameters is the compressed input.
	Params.OutputImageBuffer = const_cast<uint8*>(Data);
	Params.OutputImageBufferSize = static_cast<uint64>(Size);

	AstcEncThunk_Context Context = nullptr;
	const char* Failure = Thunk->Create(Params, &Context);
	if (Failure == nullptr)
	{
		Failure = Thunk->DoWork(Context, 0);
	}
	Thunk->Destroy(Context);
	if (Failure != nullptr)
	{
		OutError = FString::Printf(TEXT("astcenc could not decode the image: %hs"), Failure);
		return false;
	}

	const int64 PixelCount = static_cast<int64>(Width) * Height;
	OutPixels.SetNumUninitialized(PixelCount);
	for (int64 Index = 0; Index < PixelCount; ++Index)
	{
		if (bHdr)
		{
			const FFloat16* Texel = reinterpret_cast<const FFloat16*>(Decoded.GetData()) + Index * 4;
			OutPixels[Index] = FVector4f(Texel[0].GetFloat(), Texel[1].GetFloat(), Texel[2].GetFloat(), Texel[3].GetFloat());
		}
		else
		{
			const uint8* Texel = Decoded.GetData() + Index * 4;
			OutPixels[Index] = FVector4f(Texel[0] / 255.0f, Texel[1] / 255.0f, Texel[2] / 255.0f, Texel[3] / 255.0f);
		}
	}
	return true;
}
