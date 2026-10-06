// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Serialization/AssetBytecode.h"

#include "UObject/ObjectVersion.h"
#include "UObject/Script.h"

#include "Model/AssetPackageDocument.h"
#include "Serialization/AssetNativeReader.h"
#include "Serialization/AssetNumberText.h"

namespace
{
	/** A deeper expression than this is not bytecode this reading knows. */
	constexpr int32 MaximumExpressionDepth = 96;

	/** How many bytes a name and a pointer take in memory: what the jumps and the size of the function count in. */
	constexpr int64 MemoryNameSize = 12;
	constexpr int64 MemoryPointerSize = 8;

	const TCHAR* TokenName(const uint8 Token)
	{
		switch (static_cast<EExprToken>(Token))
		{
			case EX_LocalVariable:
				return TEXT("LocalVariable");
			case EX_InstanceVariable:
				return TEXT("InstanceVariable");
			case EX_DefaultVariable:
				return TEXT("DefaultVariable");
			case EX_Return:
				return TEXT("Return");
			case EX_Jump:
				return TEXT("Jump");
			case EX_JumpIfNot:
				return TEXT("JumpIfNot");
			case EX_Assert:
				return TEXT("Assert");
			case EX_Nothing:
				return TEXT("Nothing");
			case EX_NothingInt32:
				return TEXT("NothingInt32");
			case EX_Let:
				return TEXT("Let");
			case EX_BitFieldConst:
				return TEXT("BitFieldConst");
			case EX_ClassContext:
				return TEXT("ClassContext");
			case EX_MetaCast:
				return TEXT("MetaCast");
			case EX_LetBool:
				return TEXT("LetBool");
			case EX_EndParmValue:
				return TEXT("EndParmValue");
			case EX_EndFunctionParms:
				return TEXT("EndFunctionParms");
			case EX_Self:
				return TEXT("Self");
			case EX_Skip:
				return TEXT("Skip");
			case EX_Context:
				return TEXT("Context");
			case EX_Context_FailSilent:
				return TEXT("Context_FailSilent");
			case EX_VirtualFunction:
				return TEXT("VirtualFunction");
			case EX_FinalFunction:
				return TEXT("FinalFunction");
			case EX_IntConst:
				return TEXT("IntConst");
			case EX_FloatConst:
				return TEXT("FloatConst");
			case EX_StringConst:
				return TEXT("StringConst");
			case EX_ObjectConst:
				return TEXT("ObjectConst");
			case EX_NameConst:
				return TEXT("NameConst");
			case EX_RotationConst:
				return TEXT("RotationConst");
			case EX_VectorConst:
				return TEXT("VectorConst");
			case EX_ByteConst:
				return TEXT("ByteConst");
			case EX_IntZero:
				return TEXT("IntZero");
			case EX_IntOne:
				return TEXT("IntOne");
			case EX_True:
				return TEXT("True");
			case EX_False:
				return TEXT("False");
			case EX_TextConst:
				return TEXT("TextConst");
			case EX_NoObject:
				return TEXT("NoObject");
			case EX_TransformConst:
				return TEXT("TransformConst");
			case EX_IntConstByte:
				return TEXT("IntConstByte");
			case EX_NoInterface:
				return TEXT("NoInterface");
			case EX_DynamicCast:
				return TEXT("DynamicCast");
			case EX_StructConst:
				return TEXT("StructConst");
			case EX_EndStructConst:
				return TEXT("EndStructConst");
			case EX_SetArray:
				return TEXT("SetArray");
			case EX_EndArray:
				return TEXT("EndArray");
			case EX_PropertyConst:
				return TEXT("PropertyConst");
			case EX_UnicodeStringConst:
				return TEXT("UnicodeStringConst");
			case EX_Int64Const:
				return TEXT("Int64Const");
			case EX_UInt64Const:
				return TEXT("UInt64Const");
			case EX_DoubleConst:
				return TEXT("DoubleConst");
			case EX_Cast:
				return TEXT("Cast");
			case EX_SetSet:
				return TEXT("SetSet");
			case EX_EndSet:
				return TEXT("EndSet");
			case EX_SetMap:
				return TEXT("SetMap");
			case EX_EndMap:
				return TEXT("EndMap");
			case EX_SetConst:
				return TEXT("SetConst");
			case EX_EndSetConst:
				return TEXT("EndSetConst");
			case EX_MapConst:
				return TEXT("MapConst");
			case EX_EndMapConst:
				return TEXT("EndMapConst");
			case EX_Vector3fConst:
				return TEXT("Vector3fConst");
			case EX_StructMemberContext:
				return TEXT("StructMemberContext");
			case EX_LetMulticastDelegate:
				return TEXT("LetMulticastDelegate");
			case EX_LetDelegate:
				return TEXT("LetDelegate");
			case EX_LocalVirtualFunction:
				return TEXT("LocalVirtualFunction");
			case EX_LocalFinalFunction:
				return TEXT("LocalFinalFunction");
			case EX_LocalOutVariable:
				return TEXT("LocalOutVariable");
			case EX_DeprecatedOp4A:
				return TEXT("DeprecatedOp4A");
			case EX_InstanceDelegate:
				return TEXT("InstanceDelegate");
			case EX_PushExecutionFlow:
				return TEXT("PushExecutionFlow");
			case EX_PopExecutionFlow:
				return TEXT("PopExecutionFlow");
			case EX_ComputedJump:
				return TEXT("ComputedJump");
			case EX_PopExecutionFlowIfNot:
				return TEXT("PopExecutionFlowIfNot");
			case EX_Breakpoint:
				return TEXT("Breakpoint");
			case EX_InterfaceContext:
				return TEXT("InterfaceContext");
			case EX_ObjToInterfaceCast:
				return TEXT("ObjToInterfaceCast");
			case EX_EndOfScript:
				return TEXT("EndOfScript");
			case EX_CrossInterfaceCast:
				return TEXT("CrossInterfaceCast");
			case EX_InterfaceToObjCast:
				return TEXT("InterfaceToObjCast");
			case EX_WireTracepoint:
				return TEXT("WireTracepoint");
			case EX_SkipOffsetConst:
				return TEXT("SkipOffsetConst");
			case EX_AddMulticastDelegate:
				return TEXT("AddMulticastDelegate");
			case EX_ClearMulticastDelegate:
				return TEXT("ClearMulticastDelegate");
			case EX_Tracepoint:
				return TEXT("Tracepoint");
			case EX_LetObj:
				return TEXT("LetObj");
			case EX_LetWeakObjPtr:
				return TEXT("LetWeakObjPtr");
			case EX_BindDelegate:
				return TEXT("BindDelegate");
			case EX_RemoveMulticastDelegate:
				return TEXT("RemoveMulticastDelegate");
			case EX_CallMulticastDelegate:
				return TEXT("CallMulticastDelegate");
			case EX_LetValueOnPersistentFrame:
				return TEXT("LetValueOnPersistentFrame");
			case EX_ArrayConst:
				return TEXT("ArrayConst");
			case EX_EndArrayConst:
				return TEXT("EndArrayConst");
			case EX_SoftObjectConst:
				return TEXT("SoftObjectConst");
			case EX_CallMath:
				return TEXT("CallMath");
			case EX_SwitchValue:
				return TEXT("SwitchValue");
			case EX_InstrumentationEvent:
				return TEXT("InstrumentationEvent");
			case EX_ArrayGetByRef:
				return TEXT("ArrayGetByRef");
			case EX_ClassSparseDataVariable:
				return TEXT("ClassSparseDataVariable");
			case EX_FieldPathConst:
				return TEXT("FieldPathConst");
			case EX_AutoRtfmTransact:
				return TEXT("AutoRtfmTransact");
			case EX_AutoRtfmStopTransact:
				return TEXT("AutoRtfmStopTransact");
			case EX_AutoRtfmAbortIfNot:
				return TEXT("AutoRtfmAbortIfNot");
			case EX_AutoRtfmAbort:
				return TEXT("AutoRtfmAbort");
			default:
				return TEXT("Unknown");
		}
	}

	FString Call(const TCHAR* Name, const TArray<FString>& Arguments)
	{
		return Arguments.IsEmpty() ? FString(Name) : FString::Printf(TEXT("%s(%s)"), Name, *FString::Join(Arguments, TEXT(", ")));
	}

	FString Quote(const FString& Text)
	{
		return FString::Printf(TEXT("\"%s\""), *Text.Replace(TEXT("\n"), TEXT("\\n")));
	}

	/** Reads the expressions of a function the way UStruct::SerializeExpr does, and says what each one is. */
	class FDisassembler
	{
	public:
		explicit FDisassembler(FNativeReader& InReader) : R(InReader) {}

		/** Where the reading is in memory, which the jumps count in. */
		int64 Mem = 0;

		/** One expression and everything it holds; OutToken is its token. */
		FString Expr(EExprToken* OutToken, const int32 Depth)
		{
			if (!R.Ok())
			{
				return FString();
			}

			if (Depth > MaximumExpressionDepth)
			{
				R.Fail(TEXT("The expressions nest deeper than bytecode does"));
				return FString();
			}

			const uint8 Raw = Get<uint8>();
			const EExprToken Token = static_cast<EExprToken>(Raw);
			if (OutToken != nullptr)
			{
				*OutToken = Token;
			}

			TArray<FString> A;
			const auto Sub = [&]() { return Expr(nullptr, Depth + 1); };
			const auto Until = [&](const EExprToken End) {
				for (EExprToken Last = EX_Nothing; R.Ok() && Last != End;)
				{
					const FString Part = Expr(&Last, Depth + 1);
					if (Last != End)
					{
						A.Add(Part);
					}
				}
			};

			switch (Token)
			{
				case EX_Cast:
					A.Add(LexToString(Get<uint8>()));
					A.Add(Sub());
					break;
				case EX_ObjToInterfaceCast:
				case EX_CrossInterfaceCast:
				case EX_InterfaceToObjCast:
				case EX_MetaCast:
				case EX_DynamicCast:
					A.Add(Object());
					A.Add(Sub());
					break;
				case EX_Let:
					A.Add(Prop());
					A.Add(Sub());
					A.Add(Sub());
					break;
				case EX_LetObj:
				case EX_LetWeakObjPtr:
				case EX_LetBool:
				case EX_LetDelegate:
				case EX_LetMulticastDelegate:
					A.Add(Sub());
					A.Add(Sub());
					break;
				case EX_LetValueOnPersistentFrame:
				case EX_StructMemberContext:
					A.Add(Prop());
					A.Add(Sub());
					break;
				case EX_Jump:
				case EX_PushExecutionFlow:
				case EX_SkipOffsetConst:
					A.Add(Skip());
					break;
				case EX_ComputedJump:
				case EX_InterfaceContext:
				case EX_Return:
				case EX_PopExecutionFlowIfNot:
				case EX_ClearMulticastDelegate:
				case EX_AutoRtfmAbortIfNot:
				case EX_SoftObjectConst:
				case EX_FieldPathConst:
					A.Add(Sub());
					break;
				case EX_LocalVariable:
				case EX_InstanceVariable:
				case EX_DefaultVariable:
				case EX_LocalOutVariable:
				case EX_ClassSparseDataVariable:
				case EX_PropertyConst:
					A.Add(Prop());
					break;
				case EX_NothingInt32:
				case EX_IntConst:
					A.Add(LexToString(Get<int32>()));
					break;
				case EX_Int64Const:
					A.Add(LexToString(Get<int64>()));
					break;
				case EX_UInt64Const:
					A.Add(LexToString(Get<uint64>()));
					break;
				case EX_FloatConst:
					A.Add(AssetNumberText::Text(Get<float>()));
					break;
				case EX_DoubleConst:
					A.Add(AssetNumberText::Text(Get<double>()));
					break;
				case EX_ByteConst:
				case EX_IntConstByte:
					A.Add(LexToString(Get<uint8>()));
					break;
				case EX_StringConst:
					A.Add(Quote(AnsiString()));
					break;
				case EX_UnicodeStringConst:
					A.Add(Quote(UnicodeString()));
					break;
				case EX_TextConst:
					Text(A, Depth);
					break;
				case EX_ObjectConst:
					A.Add(Object());
					break;
				case EX_NameConst:
					A.Add(Name());
					break;
				case EX_RotationConst:
					if (R.UEVerAtLeast(EUnrealEngineObjectUE5Version::LARGE_WORLD_COORDINATES))
					{
						A.Add(LexToString(Get<int64>()));
						A.Add(LexToString(Get<int64>()));
						A.Add(LexToString(Get<int64>()));
					}
					else
					{
						A.Add(LexToString(Get<int32>()));
						A.Add(LexToString(Get<int32>()));
						A.Add(LexToString(Get<int32>()));
					}
					break;
				case EX_VectorConst:
					Floats(A, 3, R.UEVerAtLeast(EUnrealEngineObjectUE5Version::LARGE_WORLD_COORDINATES));
					break;
				case EX_Vector3fConst:
					Floats(A, 3, false);
					break;
				case EX_TransformConst:
					Floats(A, 10, R.UEVerAtLeast(EUnrealEngineObjectUE5Version::LARGE_WORLD_COORDINATES));
					break;
				case EX_CallMath:
				case EX_LocalFinalFunction:
				case EX_FinalFunction:
				case EX_CallMulticastDelegate:
					A.Add(Object());
					Until(EX_EndFunctionParms);
					break;
				case EX_LocalVirtualFunction:
				case EX_VirtualFunction:
					A.Add(Name());
					Until(EX_EndFunctionParms);
					break;
				case EX_ClassContext:
				case EX_Context:
				case EX_Context_FailSilent:
					A.Add(Sub());
					A.Add(Skip());
					A.Add(Prop());
					A.Add(Sub());
					break;
				case EX_AddMulticastDelegate:
				case EX_RemoveMulticastDelegate:
				case EX_ArrayGetByRef:
					A.Add(Sub());
					A.Add(Sub());
					break;
				case EX_StructConst:
					A.Add(Object());
					A.Add(FString::Printf(TEXT("%d bytes"), Get<int32>()));
					Until(EX_EndStructConst);
					break;
				case EX_SetArray:
					A.Add(Sub());
					Until(EX_EndArray);
					break;
				case EX_SetSet:
					A.Add(Sub());
					A.Add(FString::Printf(TEXT("%d elements"), Get<int32>()));
					Until(EX_EndSet);
					break;
				case EX_SetMap:
					A.Add(Sub());
					A.Add(FString::Printf(TEXT("%d elements"), Get<int32>()));
					Until(EX_EndMap);
					break;
				case EX_ArrayConst:
					A.Add(Prop());
					A.Add(FString::Printf(TEXT("%d elements"), Get<int32>()));
					Until(EX_EndArrayConst);
					break;
				case EX_SetConst:
					A.Add(Prop());
					A.Add(FString::Printf(TEXT("%d elements"), Get<int32>()));
					Until(EX_EndSetConst);
					break;
				case EX_MapConst:
					A.Add(Prop());
					A.Add(Prop());
					A.Add(FString::Printf(TEXT("%d elements"), Get<int32>()));
					Until(EX_EndMapConst);
					break;
				case EX_BitFieldConst:
					A.Add(Prop());
					A.Add(LexToString(Get<uint8>()));
					break;
				case EX_JumpIfNot:
				case EX_Skip:
					A.Add(Skip());
					A.Add(Sub());
					break;
				case EX_Assert:
					A.Add(FString::Printf(TEXT("line %d"), Get<uint16>()));
					A.Add(LexToString(Get<uint8>()));
					A.Add(Sub());
					break;
				case EX_InstanceDelegate:
					A.Add(Name());
					break;
				case EX_BindDelegate:
					A.Add(Name());
					A.Add(Sub());
					A.Add(Sub());
					break;
				case EX_SwitchValue:
				{
					const uint16 Cases = Get<uint16>();
					A.Add(FString::Printf(TEXT("%d cases"), Cases));
					A.Add(Skip());
					A.Add(Sub()); // the index
					for (uint16 Index = 0; Index < Cases && R.Ok(); ++Index)
					{
						const FString Value = Sub();
						const FString Next = Skip();
						const FString Term = Sub();
						A.Add(FString::Printf(TEXT("case %s -> %s -> %s"), *Value, *Next, *Term));
					}
					A.Add(FString::Printf(TEXT("default %s"), *Sub()));
					break;
				}
				case EX_AutoRtfmTransact:
					A.Add(LexToString(Get<int32>()));
					A.Add(Skip());
					Until(EX_AutoRtfmStopTransact);
					break;
				case EX_AutoRtfmStopTransact:
					A.Add(LexToString(Get<int32>()));
					A.Add(LexToString(Get<int8>()));
					break;
				case EX_InstrumentationEvent:
					R.Fail(TEXT("Instrumentation events are not read"));
					break;
				case EX_Nothing:
				case EX_EndOfScript:
				case EX_EndFunctionParms:
				case EX_EndStructConst:
				case EX_EndArray:
				case EX_EndArrayConst:
				case EX_EndSet:
				case EX_EndMap:
				case EX_EndSetConst:
				case EX_EndMapConst:
				case EX_IntZero:
				case EX_IntOne:
				case EX_True:
				case EX_False:
				case EX_NoObject:
				case EX_NoInterface:
				case EX_Self:
				case EX_EndParmValue:
				case EX_PopExecutionFlow:
				case EX_DeprecatedOp4A:
				case EX_WireTracepoint:
				case EX_Tracepoint:
				case EX_Breakpoint:
				case EX_AutoRtfmAbort:
					break;
				default:
					R.Fail(FString::Printf(TEXT("Unknown bytecode 0x%02X"), Raw));
					break;
			}

			return Call(TokenName(Raw), A);
		}

	private:
		template <typename T> T Get()
		{
			Mem += sizeof(T);
			return R.Read<T>();
		}

		/** An offset into the script (a jump, the end of a skipped expression), marked to be replaced by the statement it goes to. */
		FString Skip() { return FString::Printf(TEXT("\x01%u\x02"), Get<uint32>()); }

		FString Name()
		{
			Mem += MemoryNameSize;
			return R.ReadName();
		}

		FString Object()
		{
			Mem += MemoryPointerSize;
			return R.ReadObject();
		}

		/** A property: the path of fields from the struct that owns it (FFieldPath: a count, the names, and the owner). */
		FString Prop()
		{
			Mem += MemoryPointerSize;
			const int32 Count = R.Read<int32>();
			if (!R.Ok() || Count < 0 || Count > 64)
			{
				R.Fail(TEXT("A property path does not fit the data"));
				return FString();
			}

			TArray<FString> Names;
			for (int32 Index = 0; Index < Count && R.Ok(); ++Index)
			{
				Names.Add(R.ReadName());
			}

			const FString Owner = R.ReadObject();
			const FString Path = FString::Join(Names, TEXT("."));
			if (Owner == TEXT("None") || Owner.IsEmpty())
			{
				return Path.IsEmpty() ? FString(TEXT("None")) : Path;
			}

			return Owner + TEXT(":") + Path;
		}

		FString AnsiString()
		{
			TArray<ANSICHAR> Characters;
			while (R.Ok())
			{
				const uint8 Byte = Get<uint8>();
				if (Byte == 0)
				{
					break;
				}

				Characters.Add(static_cast<ANSICHAR>(Byte));
				if (Characters.Num() > (1 << 20))
				{
					R.Fail(TEXT("A string is longer than the data"));
				}
			}
			return FString(Characters.Num(), Characters.GetData());
		}

		FString UnicodeString()
		{
			FString Result;
			while (R.Ok())
			{
				const uint16 Unit = Get<uint16>();
				if (Unit == 0)
				{
					break;
				}

				Result.AppendChar(static_cast<TCHAR>(Unit));
				if (Result.Len() > (1 << 20))
				{
					R.Fail(TEXT("A string is longer than the data"));
				}
			}
			return Result;
		}

		void Floats(TArray<FString>& A, const int32 Count, const bool bDouble)
		{
			for (int32 Index = 0; Index < Count && R.Ok(); ++Index)
			{
				A.Add(bDouble ? AssetNumberText::Text(Get<double>()) : AssetNumberText::Text(Get<float>()));
			}
		}

		/** A text constant (XFERTEXT): a kind, then the strings the kind holds, each as an expression. */
		void Text(TArray<FString>& A, const int32 Depth)
		{
			const EBlueprintTextLiteralType Type = static_cast<EBlueprintTextLiteralType>(Get<uint8>());
			switch (Type)
			{
				case EBlueprintTextLiteralType::Empty:
					A.Add(TEXT("empty"));
					break;
				case EBlueprintTextLiteralType::LocalizedTextWithNotes:
					A.Add(Expr(nullptr, Depth + 1));
					[[fallthrough]];
				case EBlueprintTextLiteralType::LocalizedText:
					A.Add(Expr(nullptr, Depth + 1));
					A.Add(Expr(nullptr, Depth + 1));
					A.Add(Expr(nullptr, Depth + 1));
					break;
				case EBlueprintTextLiteralType::InvariantText:
				case EBlueprintTextLiteralType::LiteralString:
					A.Add(Expr(nullptr, Depth + 1));
					break;
				case EBlueprintTextLiteralType::StringTableEntry:
					A.Add(Object());
					A.Add(Expr(nullptr, Depth + 1));
					A.Add(Expr(nullptr, Depth + 1));
					break;
				default:
					R.Fail(TEXT("A text constant of a kind this reading does not know"));
					break;
			}
		}

		FNativeReader& R;
	};

	/** Replaces the marked offsets of a text by what they stand for: "#n" for the statement a jump goes to, or the offset when it is not a statement. */
	FString ResolveJumps(const FString& Text, const TMap<int64, int32>& StatementAt, const TArray<FString>& Raw, const bool bForComparison)
	{
		FString Result;
		for (int32 Index = 0; Index < Text.Len();)
		{
			if (Text[Index] != TEXT('\x01'))
			{
				Result.AppendChar(Text[Index++]);
				continue;
			}

			int32 End = Index + 1;
			while (End < Text.Len() && Text[End] != TEXT('\x02'))
			{
				++End;
			}

			const int64 Target = FCString::Atoi64(*Text.Mid(Index + 1, End - Index - 1));
			const int32* Statement = StatementAt.Find(Target);
			if (Statement == nullptr)
			{
				Result += FString::Printf(TEXT("@%lld"), Target);
			}
			else if (bForComparison)
			{
				// What the jump goes to, not where it is numbered.
				Result += FString::Printf(TEXT("->{%08x}"), GetTypeHash(Raw[*Statement]));
			}
			else
			{
				Result += FString::Printf(TEXT("#%d"), *Statement);
			}
			Index = End + 1;
		}
		return Result;
	}
} // namespace

bool AssetBytecode::Disassemble(const FAssetPackageDocument& Document, const int64 Offset, const int64 StorageSize, const int32 MemorySize, FAssetBytecode& Out)
{
	Out = FAssetBytecode();
	if (StorageSize <= 0 || !Document.IsValidRange(Offset, StorageSize))
	{
		Out.Error = TEXT("There is no bytecode");
		return false;
	}

	FNativeReader Reader(Document, Offset, StorageSize);
	FDisassembler Disassembler(Reader);

	TArray<FString> Raw;
	while (Reader.Ok() && Reader.Remaining() > 0)
	{
		FAssetBytecodeStatement& Statement = Out.Statements.AddDefaulted_GetRef();
		Statement.Offset = Reader.Tell();
		Statement.MemoryOffset = Disassembler.Mem;

		EExprToken Token = EX_Nothing;
		const FString Text = Disassembler.Expr(&Token, 0);
		Statement.Size = Reader.Tell() - Statement.Offset;
		Raw.Add(Text);

		if (Token == EX_EndOfScript)
		{
			break;
		}
	}

	const auto Fail = [&Out](const FString& Message) {
		Out.Error = Message;
		Out.Statements.Reset();
		return false;
	};

	if (!Reader.Ok())
	{
		return Fail(Reader.GetError());
	}

	if (Reader.Remaining() != 0)
	{
		return Fail(FString::Printf(TEXT("%lld bytes follow the end of the script"), Reader.Remaining()));
	}

	// What the reading counted in memory is what the function says it takes there; if it is not, a size or an operand is not what
	// this reading thinks, and what it read is not to be relied on.
	if (Disassembler.Mem != MemorySize)
	{
		return Fail(FString::Printf(TEXT("The statements take %lld bytes in memory and the function says %d"), Disassembler.Mem, MemorySize));
	}

	TMap<int64, int32> StatementAt;
	for (int32 Index = 0; Index < Out.Statements.Num(); ++Index)
	{
		StatementAt.Add(Out.Statements[Index].MemoryOffset, Index);
	}

	for (int32 Index = 0; Index < Out.Statements.Num(); ++Index)
	{
		Out.Statements[Index].Text = ResolveJumps(Raw[Index], StatementAt, Raw, false);
		Out.Statements[Index].Key = ResolveJumps(Raw[Index], StatementAt, Raw, true);
	}

	Out.bComplete = true;
	return true;
}

TArray<AssetBytecode::FStatementChange> AssetBytecode::Compare(const FAssetBytecode& Old, const FAssetBytecode& New)
{
	TArray<FStatementChange> Changes;

	const int32 OldCount = Old.Statements.Num();
	const int32 NewCount = New.Statements.Num();

	// What both versions share at the start and at the end is not looked at again.
	int32 Prefix = 0;
	while (Prefix < OldCount && Prefix < NewCount && Old.Statements[Prefix].Key == New.Statements[Prefix].Key)
	{
		++Prefix;
	}

	int32 Suffix = 0;
	while (Suffix < OldCount - Prefix && Suffix < NewCount - Prefix && Old.Statements[OldCount - 1 - Suffix].Key == New.Statements[NewCount - 1 - Suffix].Key)
	{
		++Suffix;
	}

	const int32 OldMiddle = OldCount - Prefix - Suffix;
	const int32 NewMiddle = NewCount - Prefix - Suffix;
	if (OldMiddle == 0 && NewMiddle == 0)
	{
		return Changes;
	}

	// The longest common subsequence of what is left, for a function that is not too long; beyond that the whole middle is one change.
	TArray<int32> Table;
	const bool bAlign = static_cast<int64>(OldMiddle + 1) * (NewMiddle + 1) <= 4000000;
	const auto At = [&](const int32 A, const int32 B) -> int32& { return Table[A * (NewMiddle + 1) + B]; };
	if (bAlign)
	{
		Table.SetNumZeroed((OldMiddle + 1) * (NewMiddle + 1));
		for (int32 I = OldMiddle - 1; I >= 0; --I)
		{
			for (int32 J = NewMiddle - 1; J >= 0; --J)
			{
				At(I, J) = Old.Statements[Prefix + I].Key == New.Statements[Prefix + J].Key ? At(I + 1, J + 1) + 1 : FMath::Max(At(I + 1, J), At(I, J + 1));
			}
		}
	}

	FStatementChange Current;
	bool bOpen = false;
	const auto Close = [&]() {
		if (bOpen)
		{
			Changes.Add(MoveTemp(Current));
			Current = FStatementChange();
			bOpen = false;
		}
	};
	const auto Open = [&](const int32 I, const int32 J) {
		if (!bOpen)
		{
			Current.OldStart = Prefix + I;
			Current.NewStart = Prefix + J;
			bOpen = true;
		}
	};

	int32 I = 0;
	int32 J = 0;
	while (I < OldMiddle || J < NewMiddle)
	{
		if (!bAlign)
		{
			Open(I, J);
			for (; I < OldMiddle; ++I)
			{
				Current.Removed.Add(Old.Statements[Prefix + I].Text);
			}
			for (; J < NewMiddle; ++J)
			{
				Current.Added.Add(New.Statements[Prefix + J].Text);
			}
			break;
		}

		if (I < OldMiddle && J < NewMiddle && Old.Statements[Prefix + I].Key == New.Statements[Prefix + J].Key)
		{
			Close();
			++I;
			++J;
		}
		else if (J < NewMiddle && (I == OldMiddle || At(I, J + 1) >= At(I + 1, J)))
		{
			Open(I, J);
			Current.Added.Add(New.Statements[Prefix + J].Text);
			++J;
		}
		else
		{
			Open(I, J);
			Current.Removed.Add(Old.Statements[Prefix + I].Text);
			++I;
		}
	}

	Close();
	return Changes;
}
