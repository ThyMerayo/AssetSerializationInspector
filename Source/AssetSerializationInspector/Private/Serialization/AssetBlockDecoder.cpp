// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetBlockDecoder.h"

#include "Math/Float16.h"

#include "Serialization/AssetBc6hTables.h"
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

	int32 SignExtend(const int32 Value, const int32 Bits)
	{
		return (Value & (1 << (Bits - 1))) != 0 ? Value - (1 << Bits) : Value;
	}

	/** The 16 bit value of a BC6H endpoint channel stretched from its precision (unsigned format). */
	int32 Bc6hUnquantize(const int32 Value, const int32 Bits)
	{
		if (Bits >= 15)
		{
			return Value;
		}
		if (Value == 0)
		{
			return 0;
		}
		if (Value == (1 << Bits) - 1)
		{
			return 0xFFFF;
		}
		return ((Value << 16) + 0x8000) >> Bits;
	}

	/**
	 * One BC6H block (the unsigned format, as UE uses for HDR): a mode of 2 or 5 bits picks one of 14 layouts that say where each bit of
	 * the endpoints is, one region or two, whether the other endpoints are differences from the first, and how wide the indices are.
	 * The result is half floats. A reserved or invalid mode is opaque black.
	 */
	void DecodeBc6h(const uint8* Block, FVector4f OutPixels[16])
	{
		using namespace AssetBc6hTables;

		const auto Bit = [Block](const int32 Position) -> int32 { return (Block[Position >> 3] >> (Position & 7)) & 1; };
		int32 Position = 0;
		const auto Read = [&](const int32 Count) {
			int32 Value = 0;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Value |= Bit(Position++) << Index;
			}
			return Value;
		};

		int32 ModeValue = Read(2);
		if (ModeValue > 1)
		{
			ModeValue = (Read(3) << 2) | ModeValue;
		}

		const int32 ModeIndex = ModeOfValue[ModeValue];
		if (ModeIndex < 0)
		{
			for (int32 Pixel = 0; Pixel < 16; ++Pixel)
			{
				OutPixels[Pixel] = FVector4f(0.0f, 0.0f, 0.0f, 1.0f);
			}
			return;
		}
		const FModeInfo& Mode = Modes[ModeIndex];

		// The header: every bit goes to the field the mode says. Endpoints are End[region][endpoint][channel]; the endpoint 0 of a region is
		// W (or Y) and the endpoint 1 is X (or Z), which a transformed mode stores as the difference from W.
		int32 End[2][2][3] = {};
		int32 ShapeValue = 0;
		const int32 HeaderBits = Mode.Partitions > 0 ? 82 : 65;
		while (Position < HeaderBits)
		{
			const int32 Current = Position++;
			if (Bit(Current) == 0)
			{
				continue;
			}

			const int32 Field = FieldOfHeaderBit[ModeIndex][Current];
			const int32 FieldBit = 1 << BitOfHeaderBit[ModeIndex][Current];
			if (Field == Shape)
			{
				ShapeValue |= FieldBit;
			}
			else if (Field >= RW)
			{
				const int32 Channel = (Field - RW) / 4;
				const int32 Slot = (Field - RW) % 4;
				End[Slot / 2][Slot % 2][Channel] |= FieldBit;
			}
		}

		// A transformed mode stores the other endpoints as signed differences, which are then added to the first one.
		if (Mode.bTransformed)
		{
			for (int32 Region = 0; Region <= Mode.Partitions; ++Region)
			{
				for (int32 Channel = 0; Channel < 3; ++Channel)
				{
					if (Region != 0)
					{
						End[Region][0][Channel] = SignExtend(End[Region][0][Channel], Mode.Precision[Region][0][Channel]);
					}
					End[Region][1][Channel] = SignExtend(End[Region][1][Channel], Mode.Precision[Region][1][Channel]);
				}
			}
			for (int32 Channel = 0; Channel < 3; ++Channel)
			{
				const int32 Mask = (1 << Mode.Precision[0][0][Channel]) - 1;
				End[0][1][Channel] = (End[0][1][Channel] + End[0][0][Channel]) & Mask;
				End[1][0][Channel] = (End[1][0][Channel] + End[0][0][Channel]) & Mask;
				End[1][1][Channel] = (End[1][1][Channel] + End[0][0][Channel]) & Mask;
			}
		}

		// The indices (the anchor of each region has one bit less), and the interpolation between the endpoints of the pixel's region.
		for (int32 Pixel = 0; Pixel < 16; ++Pixel)
		{
			const bool bAnchor = Pixel == 0 || (Mode.Partitions > 0 && Pixel == AssetBc7Tables::TwoSubsetsAnchor[ShapeValue]);
			const int32 IndexBits = Mode.IndexBits - (bAnchor ? 1 : 0);
			if (Position + IndexBits > 128)
			{
				OutPixels[Pixel] = FVector4f(0.0f, 0.0f, 0.0f, 1.0f);
				continue;
			}
			const int32 Index = Read(IndexBits);
			const int32 Region = Mode.Partitions > 0 ? AssetBc7Tables::TwoSubsets[ShapeValue * 16 + Pixel] : 0;
			const int32 Weight = Bc7Weight(Mode.IndexBits, static_cast<uint32>(Index));

			float Channels[3];
			for (int32 Channel = 0; Channel < 3; ++Channel)
			{
				const int32 Bits = Mode.Precision[0][0][Channel];
				const int32 From = Bc6hUnquantize(End[Region][0][Channel], Bits);
				const int32 To = Bc6hUnquantize(End[Region][1][Channel], Bits);
				const int32 Mixed = (From * (64 - Weight) + To * Weight + 32) >> 6;
				FFloat16 Half;
				Half.Encoded = static_cast<uint16>((Mixed * 31) >> 6);
				Channels[Channel] = Half.GetFloat();
			}
			OutPixels[Pixel] = FVector4f(Channels[0], Channels[1], Channels[2], 1.0f);
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

		case EAssetBlockCodec::BC6H:
			DecodeBc6h(Block, OutPixels);
			break;

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
