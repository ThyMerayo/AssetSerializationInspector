// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

struct FAssetPackageDocument;

/** One statement of the bytecode of a function: an expression of the top level, such as a call, an assignment or a jump. */
struct FAssetBytecodeStatement
{
	/** Where the statement starts in the file, and how many bytes it takes there. */
	int64 Offset = 0;
	int64 Size = 0;

	/** Where it starts in the script as the engine runs it: what the jumps of the function refer to. */
	int64 MemoryOffset = 0;

	/** The statement in words, with its calls, variables and constants: "LocalFinalFunction(/Script/Engine.KismetSystemLibrary:PrintString, ...)". */
	FString Text;

	/** What decides whether two statements are the same: the text, with each jump as the statement it goes to and not as its number. */
	FString Key;
};

/** The bytecode of a function (what Blueprint graphs compile to), read statement by statement. */
struct FAssetBytecode
{
	/** The whole stream was read, and its in-memory size is the one the function says. When false, Error says where it stopped. */
	bool bComplete = false;
	FString Error;

	TArray<FAssetBytecodeStatement> Statements;
};

namespace AssetBytecode
{
	/**
	 * Reads the bytecode of a function as the engine's UStruct::SerializeExpr writes it: each expression is a token followed by its
	 * operands, which are other expressions, constants, names, and references to objects, functions and properties.
	 *
	 * The text of a statement names what it uses by path, so it does not change when the numbering of the package does, and a jump
	 * names the statement it goes to ("#12") and not an offset, so inserting a statement does not change the jumps to the ones after it.
	 *
	 * @param Offset Where the bytecode starts in the document.
	 * @param StorageSize How many bytes it takes in the file.
	 * @param MemorySize How many bytes it takes in memory (the size the function stores), which the reading must add up to.
	 */
	bool Disassemble(const FAssetPackageDocument& Document, int64 Offset, int64 StorageSize, int32 MemorySize, FAssetBytecode& Out);

	/** What differs between the statements of two versions: runs of statements removed and added, in order, as text. */
	struct FStatementChange
	{
		int32 OldStart = 0;
		int32 NewStart = 0;
		TArray<FString> Removed;
		TArray<FString> Added;
	};

	TArray<FStatementChange> Compare(const FAssetBytecode& Old, const FAssetBytecode& New);
} // namespace AssetBytecode
