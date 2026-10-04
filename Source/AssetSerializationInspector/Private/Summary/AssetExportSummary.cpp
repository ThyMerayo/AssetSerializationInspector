// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Summary/AssetExportSummary.h"

#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetPropertyValueDecoder.h"
#include "Trace/AssetSerializationTrace.h"

namespace
{
	enum class EFactMode : uint8
	{
		/** The property's displayed value. */
		Value,

		/** The number of elements of an array property. */
		Count
	};

	/** A fact to read from a top-level property, or from a field of a struct property when Path has a dot ("Source.SizeX"). */
	struct FFactSpec
	{
		const TCHAR* Path;
		const TCHAR* Label;
		EFactMode Mode = EFactMode::Value;
	};

	struct FClassDescription
	{
		const TCHAR* NativeDataKind;
		TArray<FFactSpec> Facts;
	};

	const FClassDescription* FindDescription(const FString& ClassName)
	{
		static const TMap<FString, FClassDescription> Descriptions = []() {
			TMap<FString, FClassDescription> Result;

			const FClassDescription Texture{ TEXT("texture data (mips and platform data)"),
				{ { TEXT("ImportedSize"), TEXT("imported size") }, { TEXT("Source.SizeX"), TEXT("source width") }, { TEXT("Source.SizeY"), TEXT("source height") },
					{ TEXT("Source.NumMips"), TEXT("source mips") }, { TEXT("Source.Format"), TEXT("source format") }, { TEXT("CompressionSettings"), TEXT("compression") },
					{ TEXT("LODGroup"), TEXT("LOD group") } } };
			for (const TCHAR* Name : { TEXT("Texture2D"), TEXT("TextureCube"), TEXT("Texture2DArray"), TEXT("VolumeTexture"), TEXT("TextureCubeArray"), TEXT("TextureRenderTarget2D") })
			{
				Result.Add(Name, Texture);
			}

			Result.Add(TEXT("StaticMesh"),
				{ TEXT("mesh render data (LODs, vertices, indices, Nanite data)"),
					{ { TEXT("SourceModels"), TEXT("source LODs"), EFactMode::Count }, { TEXT("StaticMaterials"), TEXT("materials"), EFactMode::Count }, { TEXT("LODGroup"), TEXT("LOD group") },
						{ TEXT("LightMapResolution"), TEXT("lightmap resolution") } } });

			Result.Add(TEXT("SkeletalMesh"),
				{ TEXT("mesh render data (LODs, vertices, skin weights, morph targets)"),
					{ { TEXT("SourceModels"), TEXT("source LODs"), EFactMode::Count }, { TEXT("Skeleton"), TEXT("skeleton") }, { TEXT("PhysicsAsset"), TEXT("physics asset") },
						{ TEXT("MinLod"), TEXT("minimum LOD") } } });

			Result.Add(TEXT("AnimSequence"),
				{ TEXT("compressed animation tracks"),
					{ { TEXT("NumFrames"), TEXT("frames") }, { TEXT("SequenceLength"), TEXT("length (s)") }, { TEXT("SamplingFrameRate"), TEXT("sampling rate") },
						{ TEXT("Skeleton"), TEXT("skeleton") }, { TEXT("Notifies"), TEXT("notifies"), EFactMode::Count } } });

			Result.Add(TEXT("Skeleton"), { TEXT("reference skeleton (bone hierarchy and poses)"), {} });
			Result.Add(TEXT("DNAAsset"), { TEXT("MetaHuman DNA rig definition (binary)"), {} });
			Result.Add(TEXT("InterchangeAssetImportData"), { TEXT("Interchange import data (the node container and pipelines used to import the asset)"), { { TEXT("NodeUniqueID"), TEXT("node") } } });
			Result.Add(TEXT("AssetImportData"), { TEXT("import source data"), {} });
			Result.Add(
				TEXT("SoundWave"), { TEXT("audio data"), { { TEXT("Duration"), TEXT("duration (s)") }, { TEXT("NumChannels"), TEXT("channels") }, { TEXT("SampleRate"), TEXT("sample rate") } } });
			Result.Add(TEXT("NiagaraSystem"), { TEXT("compiled Niagara data"), {} });
			Result.Add(TEXT("ControlRigBlueprintGeneratedClass"), { TEXT("compiled Control Rig data"), {} });
			Result.Add(TEXT("Function"), { TEXT("compiled Blueprint function (bytecode)"), {} });
			Result.Add(TEXT("BlueprintGeneratedClass"), { TEXT("compiled Blueprint class (property layout and defaults)"), {} });
			Result.Add(TEXT("RecastNavMesh"), { TEXT("navigation mesh tiles"), {} });
			Result.Add(TEXT("StaticMeshComponent"), { TEXT("component state that follows the properties"), {} });
			Result.Add(TEXT("InstancedStaticMeshComponent"), { TEXT("instance data"), {} });
			return Result;
		}();

		return Descriptions.Find(ClassName);
	}

	const FAssetSerializationTraceNode* FindProperty(const FAssetSerializationTrace& Trace, const FString& Name)
	{
		if (!Trace.Root.IsValid())
		{
			return nullptr;
		}

		for (const TSharedPtr<FAssetSerializationTraceNode>& Node : Trace.Root->Children)
		{
			if (Node.IsValid() && Node->Kind == EAssetSerializationTraceKind::Property && Node->Name == Name)
			{
				return Node.Get();
			}
		}

		return nullptr;
	}

	bool ReadFact(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, const FAssetSerializationTrace& Trace, const FFactSpec& Spec, FString& OutText)
	{
		FString Property = Spec.Path;
		FString Field;
		Property.Split(TEXT("."), &Property, &Field);

		const FAssetSerializationTraceNode* Node = FindProperty(Trace, Property);
		if (Node == nullptr)
		{
			return false;
		}

		const FAssetDecodedPropertyValue Value = FAssetPropertyValueDecoder::Decode(Document, *Node, Export.SerialOffset);
		if (!Value.IsSuccess())
		{
			return false;
		}

		const FAssetDecodedPropertyValue* Target = &Value;
		if (!Field.IsEmpty())
		{
			Target = Value.Children.FindByPredicate([&Field](const FAssetDecodedPropertyValue& Child) { return Child.Name == Field; });
			if (Target == nullptr || !Target->IsSuccess())
			{
				return false;
			}
		}

		const FString Text = Spec.Mode == EFactMode::Count	 ? LexToString(Target->Children.Num())
			: Target->Kind == EAssetDecodedValueKind::Scalar ? Target->Value
															 : FAssetPropertyValueDecoder::FormatForDisplay(*Target);
		OutText = FString::Printf(TEXT("%s: %s"), Spec.Label, *Text);
		return true;
	}
} // namespace

FString FAssetExportSummary::ToText() const
{
	FString Text = NativeDataKind.IsEmpty() ? FString::Printf(TEXT("Native data of %s"), ClassName.IsEmpty() ? TEXT("the export") : *ClassName)
											: FString::Printf(TEXT("%s: %s"), ClassName.IsEmpty() ? TEXT("Export") : *ClassName, *NativeDataKind);

	if (NativeBytes > 0 && PayloadBytes > 0)
	{
		Text += FString::Printf(TEXT(" (%s of %s bytes are native data)"), *FText::AsNumber(NativeBytes).ToString(), *FText::AsNumber(PayloadBytes).ToString());
	}

	if (!Facts.IsEmpty())
	{
		Text += TEXT(". ") + FString::Join(Facts, TEXT(", "));
	}

	return Text;
}

FString AssetExportSummary::GetClassName(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export)
{
	FString Path;
	if (!Document.ResolvePackageIndexPath(Export.ClassIndex, Path) || Path == TEXT("None"))
	{
		return FString();
	}

	int32 Dot = INDEX_NONE;
	return Path.FindLastChar(TEXT('.'), Dot) ? Path.RightChop(Dot + 1) : Path;
}

FAssetExportSummary AssetExportSummary::Summarize(const FAssetPackageDocument& Document, const FAssetPackageExportEntry& Export, const FAssetSerializationTrace* Trace)
{
	FAssetExportSummary Result;
	Result.ClassName = GetClassName(Document, Export);
	Result.PayloadBytes = Export.SerialSize;

	if (Trace != nullptr && Trace->Root.IsValid())
	{
		for (const TSharedPtr<FAssetSerializationTraceNode>& Node : Trace->Root->Children)
		{
			if (Node.IsValid() && Node->Kind == EAssetSerializationTraceKind::Native)
			{
				Result.NativeBytes += Node->Size;
			}
		}
	}

	if (const FClassDescription* Description = FindDescription(Result.ClassName))
	{
		Result.NativeDataKind = Description->NativeDataKind;

		if (Trace != nullptr)
		{
			for (const FFactSpec& Spec : Description->Facts)
			{
				FString Fact;
				if (ReadFact(Document, Export, *Trace, Spec, Fact))
				{
					Result.Facts.Add(MoveTemp(Fact));
				}
			}
		}
	}

	return Result;
}
