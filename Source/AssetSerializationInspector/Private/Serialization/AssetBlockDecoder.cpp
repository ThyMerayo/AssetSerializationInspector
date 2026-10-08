// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetBlockDecoder.h"

#include "Serialization/AssetBc7Tables.h"

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

	/** Reads bits from a 16 byte block, the lowest bit of the first byte first, which is the order BC7 packs its fields in. */
	class FBlockBits
	{
	public:
		explicit FBlockBits(const uint8* InBlock) : Block(InBlock) {}

		uint32 Read(const int32 Count)
		{
			uint32 Value = 0;
			for (int32 Bit = 0; Bit < Count; ++Bit, ++Position)
			{
				Value |= static_cast<uint32>((Block[Position >> 3] >> (Position & 7)) & 1) << Bit;
			}
			return Value;
		}

	private:
		const uint8* Block;
		int32 Position = 0;
	};

	struct FBc7Mode
	{
		int32 Subsets;
		int32 PartitionBits;
		int32 RotationBits;
		int32 IndexSelectionBits;
		int32 ColorBits;
		int32 AlphaBits;
		int32 UniquePBits; // one P-bit per endpoint
		int32 SharedPBits; // one P-bit per subset
		int32 IndexBits;
		int32 SecondIndexBits;
	};

	constexpr FBc7Mode Bc7Modes[8] = {
		{ 3, 4, 0, 0, 4, 0, 1, 0, 3, 0 },
		{ 2, 6, 0, 0, 6, 0, 0, 1, 3, 0 },
		{ 3, 6, 0, 0, 5, 0, 0, 0, 2, 0 },
		{ 2, 6, 0, 0, 7, 0, 1, 0, 2, 0 },
		{ 1, 0, 2, 1, 5, 6, 0, 0, 2, 3 },
		{ 1, 0, 2, 0, 7, 8, 0, 0, 2, 2 },
		{ 1, 0, 0, 0, 7, 7, 1, 0, 4, 0 },
		{ 2, 6, 0, 0, 5, 5, 1, 0, 2, 0 },
	};

	int32 Bc7Weight(const int32 IndexBits, const uint32 Index)
	{
		static constexpr int32 Weights2[4] = { 0, 21, 43, 64 };
		static constexpr int32 Weights3[8] = { 0, 9, 18, 27, 37, 46, 55, 64 };
		static constexpr int32 Weights4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };
		return IndexBits == 2 ? Weights2[Index] : IndexBits == 3 ? Weights3[Index] : Weights4[Index];
	}

	int32 Bc7Interpolate(const int32 A, const int32 B, const int32 Weight)
	{
		return ((64 - Weight) * A + Weight * B + 32) >> 6;
	}

	/** One BC7 block: the mode is the number of zero bits before the first one. A block with no bit set is invalid and decodes to transparent black. */
	void DecodeBc7(const uint8* Block, FVector4f OutPixels[16])
	{
		const int32 ModeIndex = Block[0] == 0 ? 8 : FMath::CountTrailingZeros(static_cast<uint32>(Block[0]));
		if (ModeIndex >= 8)
		{
			for (int32 Pixel = 0; Pixel < 16; ++Pixel)
			{
				OutPixels[Pixel] = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
			}
			return;
		}

		const FBc7Mode& Mode = Bc7Modes[ModeIndex];
		FBlockBits Bits(Block);
		Bits.Read(ModeIndex + 1);

		const uint32 Partition = Bits.Read(Mode.PartitionBits);
		const uint32 Rotation = Bits.Read(Mode.RotationBits);
		const uint32 IndexSelection = Bits.Read(Mode.IndexSelectionBits);

		// The endpoints, channel by channel: every red, then every green, then every blue, then every alpha.
		const int32 Endpoints = Mode.Subsets * 2;
		int32 Values[6][4] = {};
		for (int32 Channel = 0; Channel < 3; ++Channel)
		{
			for (int32 Endpoint = 0; Endpoint < Endpoints; ++Endpoint)
			{
				Values[Endpoint][Channel] = static_cast<int32>(Bits.Read(Mode.ColorBits));
			}
		}
		for (int32 Endpoint = 0; Endpoint < Endpoints; ++Endpoint)
		{
			Values[Endpoint][3] = Mode.AlphaBits > 0 ? static_cast<int32>(Bits.Read(Mode.AlphaBits)) : 255;
		}

		// The P-bits add a low bit to every channel of the endpoint (or of both endpoints of a subset).
		int32 PBits[6] = {};
		const bool bHasP = Mode.UniquePBits > 0 || Mode.SharedPBits > 0;
		if (Mode.UniquePBits > 0)
		{
			for (int32 Endpoint = 0; Endpoint < Endpoints; ++Endpoint)
			{
				PBits[Endpoint] = static_cast<int32>(Bits.Read(1));
			}
		}
		else if (Mode.SharedPBits > 0)
		{
			for (int32 Subset = 0; Subset < Mode.Subsets; ++Subset)
			{
				PBits[Subset * 2] = PBits[Subset * 2 + 1] = static_cast<int32>(Bits.Read(1));
			}
		}

		// Expand each channel to 8 bits by repeating its top bits.
		const auto Expand = [](const int32 Value, const int32 Count) { return (Value << (8 - Count)) | (Value >> (2 * Count - 8)); };
		for (int32 Endpoint = 0; Endpoint < Endpoints; ++Endpoint)
		{
			for (int32 Channel = 0; Channel < 4; ++Channel)
			{
				const bool bAlpha = Channel == 3;
				if (bAlpha && Mode.AlphaBits == 0)
				{
					continue;
				}
				const int32 Count = bAlpha ? Mode.AlphaBits : Mode.ColorBits;
				// Mode 6 has the P-bit in alpha as well; a mode without alpha channel bits has none.
				const int32 Value = bHasP ? (Values[Endpoint][Channel] << 1) | PBits[Endpoint] : Values[Endpoint][Channel];
				Values[Endpoint][Channel] = Expand(Value, bHasP ? Count + 1 : Count);
			}
		}

		// The anchor pixels have an index with one bit less (its top bit is zero).
		const auto SubsetOf = [&](const int32 Pixel) -> int32 {
			if (Mode.Subsets == 2)
			{
				return AssetBc7Tables::TwoSubsets[Partition * 16 + Pixel];
			}
			if (Mode.Subsets == 3)
			{
				return AssetBc7Tables::ThreeSubsets[Partition * 16 + Pixel];
			}
			return 0;
		};
		const auto IsAnchor = [&](const int32 Pixel, const int32 Subset) -> bool {
			if (Subset == 0)
			{
				return Pixel == 0;
			}
			if (Mode.Subsets == 2)
			{
				return Pixel == AssetBc7Tables::TwoSubsetsAnchor[Partition];
			}
			return Pixel == AssetBc7Tables::ThreeSubsetsAnchors[Partition * 2 + (Subset - 1)];
		};

		uint32 Indices[16];
		uint32 SecondIndices[16] = {};
		for (int32 Pixel = 0; Pixel < 16; ++Pixel)
		{
			Indices[Pixel] = Bits.Read(Mode.IndexBits - (IsAnchor(Pixel, SubsetOf(Pixel)) ? 1 : 0));
		}
		if (Mode.SecondIndexBits > 0)
		{
			for (int32 Pixel = 0; Pixel < 16; ++Pixel)
			{
				SecondIndices[Pixel] = Bits.Read(Mode.SecondIndexBits - (Pixel == 0 ? 1 : 0));
			}
		}

		for (int32 Pixel = 0; Pixel < 16; ++Pixel)
		{
			const int32 Subset = SubsetOf(Pixel);
			const int32* E0 = Values[Subset * 2];
			const int32* E1 = Values[Subset * 2 + 1];

			// Modes 4 and 5 have an index for the color and one for the alpha; in mode 4 the selection bit says which of the two sizes is which.
			int32 ColorBits = Mode.IndexBits;
			uint32 ColorIndex = Indices[Pixel];
			int32 AlphaBits = Mode.IndexBits;
			uint32 AlphaIndex = Indices[Pixel];
			if (Mode.SecondIndexBits > 0)
			{
				AlphaBits = Mode.SecondIndexBits;
				AlphaIndex = SecondIndices[Pixel];
				if (IndexSelection != 0)
				{
					Swap(ColorBits, AlphaBits);
					Swap(ColorIndex, AlphaIndex);
				}
			}

			const int32 ColorWeight = Bc7Weight(ColorBits, ColorIndex);
			const int32 AlphaWeight = Bc7Weight(AlphaBits, AlphaIndex);
			int32 Channels[4];
			for (int32 Channel = 0; Channel < 3; ++Channel)
			{
				Channels[Channel] = Bc7Interpolate(E0[Channel], E1[Channel], ColorWeight);
			}
			Channels[3] = Bc7Interpolate(E0[3], E1[3], AlphaWeight);

			// The rotation swaps alpha with one of the color channels.
			if (Rotation != 0)
			{
				Swap(Channels[3], Channels[Rotation - 1]);
			}

			OutPixels[Pixel] = FVector4f(Channels[0] / 255.0f, Channels[1] / 255.0f, Channels[2] / 255.0f, Channels[3] / 255.0f);
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

		case EAssetBlockCodec::BC7:
			DecodeBc7(Block, OutPixels);
			break;

		default:
			for (int32 Pixel = 0; Pixel < 16; ++Pixel)
			{
				OutPixels[Pixel] = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
			}
			break;
	}
}
