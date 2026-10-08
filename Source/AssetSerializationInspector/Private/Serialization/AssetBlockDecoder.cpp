// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetBlockDecoder.h"

namespace
{
	FVector3f Expand565(const uint16 Value)
	{
		const uint32 R = (Value >> 11) & 31;
		const uint32 G = (Value >> 5) & 63;
		const uint32 B = Value & 31;
		return FVector3f(R / 31.0f, G / 63.0f, B / 31.0f);
	}

	/** The 8 byte color block of BC1, BC2 and BC3. A BC1 block with the first color not above the second has three colors and a transparent one. */
	void DecodeBc1Colors(const uint8* Block, const bool bAllowTransparent, FVector4f OutPixels[16])
	{
		const uint16 C0 = Block[0] | (Block[1] << 8);
		const uint16 C1 = Block[2] | (Block[3] << 8);
		const FVector3f A = Expand565(C0);
		const FVector3f B = Expand565(C1);

		FVector4f Palette[4];
		Palette[0] = FVector4f(A.X, A.Y, A.Z, 1.0f);
		Palette[1] = FVector4f(B.X, B.Y, B.Z, 1.0f);
		if (C0 > C1 || !bAllowTransparent)
		{
			const FVector3f P2 = (A * 2.0f + B) / 3.0f;
			const FVector3f P3 = (A + B * 2.0f) / 3.0f;
			Palette[2] = FVector4f(P2.X, P2.Y, P2.Z, 1.0f);
			Palette[3] = FVector4f(P3.X, P3.Y, P3.Z, 1.0f);
		}
		else
		{
			const FVector3f Mid = (A + B) * 0.5f;
			Palette[2] = FVector4f(Mid.X, Mid.Y, Mid.Z, 1.0f);
			Palette[3] = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
		}

		const uint32 Indices = Block[4] | (Block[5] << 8) | (Block[6] << 16) | (static_cast<uint32>(Block[7]) << 24);
		for (int32 Pixel = 0; Pixel < 16; ++Pixel)
		{
			OutPixels[Pixel] = Palette[(Indices >> (2 * Pixel)) & 3];
		}
	}

	/** The 8 byte block of BC4 (and of the alpha of BC3, and each channel of BC5): two end values and sixteen 3 bit indices. */
	void DecodeBc4Channel(const uint8* Block, float OutValues[16])
	{
		const float E0 = Block[0] / 255.0f;
		const float E1 = Block[1] / 255.0f;

		float Palette[8];
		Palette[0] = E0;
		Palette[1] = E1;
		if (Block[0] > Block[1])
		{
			for (int32 Step = 1; Step <= 6; ++Step)
			{
				Palette[1 + Step] = ((7 - Step) * E0 + Step * E1) / 7.0f;
			}
		}
		else
		{
			for (int32 Step = 1; Step <= 4; ++Step)
			{
				Palette[1 + Step] = ((5 - Step) * E0 + Step * E1) / 5.0f;
			}
			Palette[6] = 0.0f;
			Palette[7] = 1.0f;
		}

		uint64 Indices = 0;
		for (int32 Byte = 0; Byte < 6; ++Byte)
		{
			Indices |= static_cast<uint64>(Block[2 + Byte]) << (8 * Byte);
		}
		for (int32 Pixel = 0; Pixel < 16; ++Pixel)
		{
			OutValues[Pixel] = Palette[(Indices >> (3 * Pixel)) & 7];
		}
	}
} // namespace

void AssetBlockDecoder::DecodeBlock(const EAssetBlockCodec Codec, const uint8* Block, FVector4f OutPixels[16])
{
	switch (Codec)
	{
		case EAssetBlockCodec::BC1:
			DecodeBc1Colors(Block, true, OutPixels);
			break;

		case EAssetBlockCodec::BC2:
		{
			// Sixteen 4 bit alphas, then the colors.
			DecodeBc1Colors(Block + 8, false, OutPixels);
			for (int32 Pixel = 0; Pixel < 16; ++Pixel)
			{
				OutPixels[Pixel].W = ((Block[Pixel / 2] >> (4 * (Pixel & 1))) & 15) / 15.0f;
			}
			break;
		}

		case EAssetBlockCodec::BC3:
		{
			float Alpha[16];
			DecodeBc4Channel(Block, Alpha);
			DecodeBc1Colors(Block + 8, false, OutPixels);
			for (int32 Pixel = 0; Pixel < 16; ++Pixel)
			{
				OutPixels[Pixel].W = Alpha[Pixel];
			}
			break;
		}

		case EAssetBlockCodec::BC4:
		{
			float Red[16];
			DecodeBc4Channel(Block, Red);
			for (int32 Pixel = 0; Pixel < 16; ++Pixel)
			{
				OutPixels[Pixel] = FVector4f(Red[Pixel], Red[Pixel], Red[Pixel], 1.0f);
			}
			break;
		}

		case EAssetBlockCodec::BC5:
		{
			float Red[16];
			float Green[16];
			DecodeBc4Channel(Block, Red);
			DecodeBc4Channel(Block + 8, Green);
			for (int32 Pixel = 0; Pixel < 16; ++Pixel)
			{
				OutPixels[Pixel] = FVector4f(Red[Pixel], Green[Pixel], 0.0f, 1.0f);
			}
			break;
		}

		default:
			for (int32 Pixel = 0; Pixel < 16; ++Pixel)
			{
				OutPixels[Pixel] = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
			}
			break;
	}
}
