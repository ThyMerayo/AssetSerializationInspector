// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

/**
 * Text for the numbers of decoded values. The engine prints a float with six decimals and a vector with three, so two different
 * values can print the same and a diff of them would show a modified property with the same text on both sides. The text is the
 * engine's whenever it gives the value back exactly; when it does not, the number is written with the fewest digits that do.
 */
namespace AssetNumberText
{
	namespace Private
	{
		/** Whether the text, read as a number, is the value (as a float when bFloat). */
		inline bool IsExact(const FString& Token, const double Value, const bool bFloat)
		{
			const double Parsed = FCString::Atod(*Token);
			return bFloat ? static_cast<float>(Parsed) == static_cast<float>(Value) : Parsed == Value;
		}

		/** The number with the fewest significant digits that reads back as the same value. */
		inline FString Shortest(const double Value, const bool bFloat)
		{
			const int32 MaximumDigits = bFloat ? 9 : 17;
			for (int32 Digits = 1; Digits <= MaximumDigits; ++Digits)
			{
				const FString Text = FString::Printf(TEXT("%.*g"), Digits, Value);
				if (IsExact(Text, Value, bFloat))
				{
					return Text;
				}
			}
			return FString::Printf(TEXT("%.*g"), MaximumDigits, Value);
		}

		inline FString Number(const double Value, const bool bFloat)
		{
			if (!FMath::IsFinite(Value))
			{
				return LexToString(Value);
			}

			const FString Text = bFloat ? LexToString(static_cast<float>(Value)) : LexToString(Value);
			return IsExact(Text, Value, bFloat) ? Text : Shortest(Value, bFloat);
		}

		/**
		 * The engine's text of a vector, a rotator or the like ("X=1.000 Y=2.000"), with each number that does not give its component
		 * back replaced by one that does. The text is returned as it is when every number is exact.
		 */
		inline FString Components(const FString& EngineText, const TArray<double>& Values, const bool bFloat)
		{
			FString Result;
			int32 Next = 0;
			bool bChanged = false;

			for (int32 Index = 0; Index < EngineText.Len();)
			{
				Result.AppendChar(EngineText[Index]);
				if (EngineText[Index++] != TEXT('=') || Next >= Values.Num())
				{
					continue;
				}

				int32 End = Index;
				while (End < EngineText.Len()
					&& (FChar::IsDigit(EngineText[End]) || EngineText[End] == TEXT('-') || EngineText[End] == TEXT('+') || EngineText[End] == TEXT('.') || EngineText[End] == TEXT('e')
						|| EngineText[End] == TEXT('E')))
				{
					++End;
				}

				const FString Token = EngineText.Mid(Index, End - Index);
				if (Token.IsEmpty() || IsExact(Token, Values[Next], bFloat) || !FMath::IsFinite(Values[Next]))
				{
					Result += Token;
				}
				else
				{
					Result += Shortest(Values[Next], bFloat);
					bChanged = true;
				}

				++Next;
				Index = End;
			}

			return bChanged ? Result : EngineText;
		}
	} // namespace Private

	inline FString Text(const float Value)
	{
		return Private::Number(Value, true);
	}

	inline FString Text(const double Value)
	{
		return Private::Number(Value, false);
	}

	/** Anything else is written as the engine writes it. */
	template <typename T> FString Text(const T& Value)
	{
		return LexToString(Value);
	}

	inline FString Text(const FVector& Value)
	{
		return Private::Components(Value.ToString(), { Value.X, Value.Y, Value.Z }, false);
	}

	inline FString Text(const FVector2D& Value)
	{
		return Private::Components(Value.ToString(), { Value.X, Value.Y }, false);
	}

	inline FString Text(const FVector4& Value)
	{
		return Private::Components(Value.ToString(), { Value.X, Value.Y, Value.Z, Value.W }, false);
	}

	inline FString Text(const FQuat& Value)
	{
		return Private::Components(Value.ToString(), { Value.X, Value.Y, Value.Z, Value.W }, false);
	}

	inline FString Text(const FRotator& Value)
	{
		return Private::Components(Value.ToString(), { Value.Pitch, Value.Yaw, Value.Roll }, false);
	}

	inline FString Text(const FVector2f& Value)
	{
		return Private::Components(Value.ToString(), { Value.X, Value.Y }, true);
	}

	inline FString Text(const FVector3f& Value)
	{
		return Private::Components(Value.ToString(), { Value.X, Value.Y, Value.Z }, true);
	}

	inline FString Text(const FVector4f& Value)
	{
		return Private::Components(Value.ToString(), { Value.X, Value.Y, Value.Z, Value.W }, true);
	}

	inline FString Text(const FQuat4f& Value)
	{
		return Private::Components(Value.ToString(), { Value.X, Value.Y, Value.Z, Value.W }, true);
	}

	inline FString Text(const FRotator3f& Value)
	{
		return Private::Components(Value.ToString(), { Value.Pitch, Value.Yaw, Value.Roll }, true);
	}
} // namespace AssetNumberText
