// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetSourceImage.h"

#include "Engine/Texture.h"
#include "ImageCore.h"
#include "ImageCoreDelta.h"
#include "ImageCoreUtils.h"
#include "ImageUtils.h"
#include "Math/Float16.h"

#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetEditorPayload.h"
#include "Serialization/AssetPropertyValueDecoder.h"
#include "Serialization/AssetSerializationPrimitives.h"
#include "Trace/AssetSerializationTrace.h"

namespace
{
	/** An image larger than this is not loaded: comparing it would take more memory than the inspector should use. */
	constexpr int64 MaximumImageBytes = 256ll * 1024 * 1024;

	/** The channels of one pixel, as they are stored, normalized: 0 to 1 for the integer formats, the value for the float ones. */
	struct FSourcePixelFormatInfo
	{
		const TCHAR* Name;
		int32 BytesPerPixel;
	};

	const FSourcePixelFormatInfo* FindSourceFormat(const FString& Format)
	{
		static const FSourcePixelFormatInfo Formats[] = { { TEXT("TSF_G8"), 1 }, { TEXT("TSF_BGRA8"), 4 }, { TEXT("TSF_RGBA8"), 4 }, { TEXT("TSF_G16"), 2 }, { TEXT("TSF_RGBA16"), 8 },
			{ TEXT("TSF_RGBA16F"), 8 }, { TEXT("TSF_RGBA32F"), 16 }, { TEXT("TSF_R16F"), 2 }, { TEXT("TSF_R32F"), 4 } };
		for (const FSourcePixelFormatInfo& Info : Formats)
		{
			if (Format == Info.Name)
			{
				return &Info;
			}
		}
		return nullptr;
	}

	/** The pixel at Index as red, green, blue and alpha values; a one channel image is grey with full alpha. */
	FVector4d ReadSourcePixel(const FString& Format, const uint8* Data)
	{
		const auto U16 = [](const uint8* P) { return static_cast<double>(*reinterpret_cast<const uint16*>(P)) / 65535.0; };
		const auto Half = [](const uint8* P) { return static_cast<double>(reinterpret_cast<const FFloat16*>(P)->GetFloat()); };

		if (Format == TEXT("TSF_G8"))
		{
			const double G = Data[0] / 255.0;
			return FVector4d(G, G, G, 1.0);
		}
		if (Format == TEXT("TSF_BGRA8"))
		{
			return FVector4d(Data[2] / 255.0, Data[1] / 255.0, Data[0] / 255.0, Data[3] / 255.0);
		}
		if (Format == TEXT("TSF_RGBA8"))
		{
			return FVector4d(Data[0] / 255.0, Data[1] / 255.0, Data[2] / 255.0, Data[3] / 255.0);
		}
		if (Format == TEXT("TSF_G16"))
		{
			const double G = U16(Data);
			return FVector4d(G, G, G, 1.0);
		}
		if (Format == TEXT("TSF_RGBA16"))
		{
			return FVector4d(U16(Data), U16(Data + 2), U16(Data + 4), U16(Data + 6));
		}
		if (Format == TEXT("TSF_RGBA16F"))
		{
			return FVector4d(Half(Data), Half(Data + 2), Half(Data + 4), Half(Data + 6));
		}
		if (Format == TEXT("TSF_RGBA32F"))
		{
			const float* F = reinterpret_cast<const float*>(Data);
			return FVector4d(F[0], F[1], F[2], F[3]);
		}
		if (Format == TEXT("TSF_R16F"))
		{
			const double R = Half(Data);
			return FVector4d(R, R, R, 1.0);
		}
		const double R = *reinterpret_cast<const float*>(Data);
		return FVector4d(R, R, R, 1.0);
	}

	/** A decoded property of the Source struct of the texture ("SizeX", "Format"...), as text. */
	bool ReadSourceField(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, const FAssetSerializationTrace& Trace, const TCHAR* Field, FString& Out)
	{
		const FAssetSerializationTraceNode* Node = Trace.FindProperty(TEXT("Source"));
		if (Node == nullptr)
		{
			return false;
		}

		const FAssetDecodedPropertyValue Value = FAssetPropertyValueDecoder::Decode(Document, *Node, Export.SerialOffset);
		if (!Value.IsSuccess())
		{
			return false;
		}

		const FAssetDecodedPropertyValue* Child = Value.Children.FindByPredicate([Field](const FAssetDecodedPropertyValue& Candidate) { return Candidate.Name == Field; });
		if (Child == nullptr || !Child->IsSuccess())
		{
			return false;
		}

		Out = Child->Kind == EAssetDecodedValueKind::Scalar ? Child->Value : FAssetPropertyValueDecoder::FormatForDisplay(*Child);
		return true;
	}

	/** An enum value as its short name: "ETextureSourceFormat::TSF_BGRA8" and "TSF_BGRA8" are both "TSF_BGRA8". */
	FString SourceEnumName(const FString& Value)
	{
		return AssetSerializationPrimitives::TailAfterLast(Value, TEXT(':'));
	}
} // namespace

int32 FAssetSourceImage::BytesPerPixel() const
{
	const FSourcePixelFormatInfo* Info = FindSourceFormat(Format);
	return Info != nullptr ? Info->BytesPerPixel : 0;
}

FAssetSourceImage AssetSourceImage::Load(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, const FAssetSerializationTrace* Trace, const FAssetBulkDataInfo& Bulk)
{
	FAssetSourceImage Image;
	if (Trace == nullptr)
	{
		Image.Error = TEXT("The properties of the texture are not available");
		return Image;
	}

	FString Width, Height, Slices, Format, Compression;
	if (!ReadSourceField(Document, Export, *Trace, TEXT("SizeX"), Width) || !ReadSourceField(Document, Export, *Trace, TEXT("SizeY"), Height)
		|| !ReadSourceField(Document, Export, *Trace, TEXT("Format"), Format))
	{
		Image.Error = TEXT("The size and format of the source image are not in the properties");
		return Image;
	}

	// Properties left at their default are not written: one slice, no compression.
	Image.Width = FCString::Atoi(*Width);
	Image.Height = FCString::Atoi(*Height);
	Image.NumSlices = ReadSourceField(Document, Export, *Trace, TEXT("NumSlices"), Slices) ? FMath::Max(FCString::Atoi(*Slices), 1) : 1;
	Image.Format = SourceEnumName(Format);

	// Left out when it is the default (none). The delta transform that textures are saved with is undone below, and PNG and JPEG are decoded.
	const FString CompressionName = ReadSourceField(Document, Export, *Trace, TEXT("CompressionFormat"), Compression) ? SourceEnumName(Compression) : FString();
	const bool bDelta = CompressionName == TEXT("TSCF_UEDELTA");
	const bool bPng = CompressionName == TEXT("TSCF_PNG");
	const bool bImageFile = bPng || CompressionName == TEXT("TSCF_JPEG");
	if (!CompressionName.IsEmpty() && CompressionName != TEXT("TSCF_None") && !bDelta && !bImageFile)
	{
		Image.Error = FString::Printf(TEXT("The image is compressed as %s"), *CompressionName);
		return Image;
	}

	const int32 BytesPerPixel = Image.BytesPerPixel();
	if (BytesPerPixel == 0)
	{
		Image.Error = FString::Printf(TEXT("The pixel format %s is not decoded"), *Image.Format);
		return Image;
	}
	if (Image.Width <= 0 || Image.Height <= 0)
	{
		Image.Error = TEXT("The source image has no size");
		return Image;
	}

	TArray64<uint8> Payload;
	if (!AssetEditorPayload::Load(Document, Bulk, TEXT("The image"), MaximumImageBytes, Payload, Image.Error))
	{
		return Image;
	}

	// A PNG or JPEG file holds the first mip of an image of one slice (the engine does not compress more than that).
	if (bImageFile)
	{
		if (Image.NumSlices != 1)
		{
			Image.Error = FString::Printf(TEXT("The image is compressed as %s and has several slices"), *CompressionName);
			return Image;
		}

		FImage Decoded;
		if (!FImageUtils::DecompressImage(Payload.GetData(), Payload.Num(), Decoded))
		{
			// The engine notes that some packages mark a payload as PNG when it is the raw pixels.
			if (Payload.Num() == static_cast<int64>(Image.Width) * Image.Height * BytesPerPixel)
			{
				Image.Pixels = MoveTemp(Payload);
				Image.bLoaded = true;
				return Image;
			}

			Image.Error = FString::Printf(TEXT("The %s file of the image cannot be decoded"), bPng ? TEXT("PNG") : TEXT("JPEG"));
			return Image;
		}

		const UEnum* SourceFormatEnum = StaticEnum<ETextureSourceFormat>();
		const int64 FormatValue = SourceFormatEnum != nullptr ? SourceFormatEnum->GetValueByNameString(Image.Format) : INDEX_NONE;
		if (FormatValue == INDEX_NONE)
		{
			Image.Error = FString::Printf(TEXT("The pixel format %s is not known"), *Image.Format);
			return Image;
		}

		const ERawImageFormat::Type RawFormat = FImageCoreUtils::ConvertToRawImageFormat(static_cast<ETextureSourceFormat>(FormatValue));
		if (Decoded.Format != RawFormat)
		{
			Decoded.ChangeFormat(RawFormat, EGammaSpace::Linear);
		}
		if (bPng && Image.Format == TEXT("TSF_BGRA8"))
		{
			// The engine stores a BGRA image as RGBA in the PNG, and undoes that when it reads it.
			FImageCore::TransposeImageRGBABGRA(Decoded);
		}

		if (Decoded.SizeX != Image.Width || Decoded.SizeY != Image.Height || Decoded.RawData.Num() != static_cast<int64>(Image.Width) * Image.Height * BytesPerPixel)
		{
			Image.Error = TEXT("The decoded image does not have the size the source says");
			return Image;
		}

		Image.Pixels = MoveTemp(Decoded.RawData);
		Image.bLoaded = true;
		return Image;
	}

	// The first mip of the first block is at the start; the rest of the payload is its smaller mips and the other blocks.
	const int64 FirstMipBytes = static_cast<int64>(Image.Width) * Image.Height * Image.NumSlices * BytesPerPixel;
	if (Payload.Num() < FirstMipBytes)
	{
		Image.Error = TEXT("The image is smaller than its size and format say");
		return Image;
	}

	Image.Pixels.SetNumUninitialized(FirstMipBytes);
	if (!bDelta)
	{
		FMemory::Memcpy(Image.Pixels.GetData(), Payload.GetData(), FirstMipBytes);
	}
	else
	{
		// UTexture saves the pixels as the difference between neighbours, mip by mip; the first mip is undone on its own.
		const UEnum* SourceFormatEnum = StaticEnum<ETextureSourceFormat>();
		const int64 FormatValue = SourceFormatEnum != nullptr ? SourceFormatEnum->GetValueByNameString(Image.Format) : INDEX_NONE;
		if (FormatValue == INDEX_NONE)
		{
			Image.Error = FString::Printf(TEXT("The pixel format %s is not known"), *Image.Format);
			return Image;
		}

		const FImageInfo Info(Image.Width, Image.Height, Image.NumSlices, FImageCoreUtils::ConvertToRawImageFormat(static_cast<ETextureSourceFormat>(FormatValue)), EGammaSpace::Linear);
		const FImageView View(Info, Payload.GetData());
		TArray64<FImageViewStrided> Parts;
		FImageCoreDelta::AddSplitStridedViewsForDelta(Parts, View);
		for (const FImageViewStrided& Part : Parts)
		{
			const int64 Offset = static_cast<const uint8*>(Part.RawData) - Payload.GetData();
			FImageCoreDelta::DoTransform(Part, Image.Pixels.GetData() + Offset, false);
		}
	}

	Image.bLoaded = true;
	return Image;
}

void AssetSourceImage::AppendPixelChange(const FAssetSourceImage& Old, const FAssetSourceImage& New, TArray<FAssetNativeDataChange>& Changes)
{
	const auto AddNote = [&Changes](const FString& Note) {
		FAssetNativeDataChange& Change = Changes.AddDefaulted_GetRef();
		Change.Key = TEXT("BulkData/Pixels");
		Change.Title = TEXT("Source image pixels");
		Change.State = FAssetNativeDataChange::EState::Modified;
		Change.OldValue = TEXT("not compared");
		Change.NewValue = Note;
	};

	if (!Old.bLoaded || !New.bLoaded)
	{
		AddNote(FString::Printf(TEXT("%s"), !Old.bLoaded ? *Old.Error : *New.Error));
		return;
	}
	if (Old.Width != New.Width || Old.Height != New.Height || Old.NumSlices != New.NumSlices || Old.Format != New.Format)
	{
		AddNote(TEXT("the size or the format changed, so the pixels are not compared"));
		return;
	}

	const int32 Bytes = Old.BytesPerPixel();
	const int64 Pixels = static_cast<int64>(Old.Width) * Old.Height * Old.NumSlices;

	int64 Differing = 0;
	int32 MinX = MAX_int32, MinY = MAX_int32, MaxX = -1, MaxY = -1;
	double LargestChange = 0.0;
	FVector4d OldSum(0.0, 0.0, 0.0, 0.0), NewSum(0.0, 0.0, 0.0, 0.0);
	for (int64 Index = 0; Index < Pixels; ++Index)
	{
		const uint8* OldPixel = Old.Pixels.GetData() + Index * Bytes;
		const uint8* NewPixel = New.Pixels.GetData() + Index * Bytes;
		const FVector4d OldColor = ReadSourcePixel(Old.Format, OldPixel);
		const FVector4d NewColor = ReadSourcePixel(New.Format, NewPixel);
		OldSum += OldColor;
		NewSum += NewColor;

		if (FMemory::Memcmp(OldPixel, NewPixel, Bytes) != 0)
		{
			++Differing;
			const int32 X = static_cast<int32>(Index % Old.Width);
			const int32 Y = static_cast<int32>((Index / Old.Width) % Old.Height);
			MinX = FMath::Min(MinX, X);
			MaxX = FMath::Max(MaxX, X);
			MinY = FMath::Min(MinY, Y);
			MaxY = FMath::Max(MaxY, Y);
			const FVector4d Delta(FMath::Abs(OldColor.X - NewColor.X), FMath::Abs(OldColor.Y - NewColor.Y), FMath::Abs(OldColor.Z - NewColor.Z), FMath::Abs(OldColor.W - NewColor.W));
			LargestChange = FMath::Max(LargestChange, FMath::Max(FMath::Max(Delta.X, Delta.Y), FMath::Max(Delta.Z, Delta.W)));
		}
	}

	if (Differing == 0)
	{
		return;
	}

	const auto Describe = [Pixels](const FVector4d& Sum) {
		const FVector4d Mean = Sum / static_cast<double>(Pixels);
		return FString::Printf(TEXT("average color (%.3f, %.3f, %.3f, %.3f)"), Mean.X, Mean.Y, Mean.Z, Mean.W);
	};

	FAssetNativeDataChange& Change = Changes.AddDefaulted_GetRef();
	Change.Key = TEXT("BulkData/Pixels");
	Change.Title = FString::Printf(TEXT("Source image pixels: %lld of %lld differ (%.1f%%), within x %d to %d and y %d to %d, largest change %.3f of the range"), Differing, Pixels,
		100.0 * static_cast<double>(Differing) / static_cast<double>(Pixels), MinX, MaxX, MinY, MaxY, LargestChange);
	Change.State = FAssetNativeDataChange::EState::Modified;
	Change.OldValue = Describe(OldSum);
	Change.NewValue = Describe(NewSum);
}
