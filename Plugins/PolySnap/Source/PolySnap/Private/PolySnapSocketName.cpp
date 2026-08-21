// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapSocketName.h"

namespace PolySnapSocketNamePrivate
{
/** Three digits, zero-padded, 001-999. Zero is not an identity. */
constexpr int32 IdDigitCount = 3;
constexpr int32 MinId = 1;
constexpr int32 MaxId = 999;

/** Fields in the head of a well-formed Straight socket: Edge, ID, SubType, Thickness, Length. */
constexpr int32 StraightFieldCount = 5;

[[nodiscard]] bool IsAllDigits(FStringView Token)
{
	if (Token.IsEmpty())
	{
		return false;
	}

	for (const TCHAR Character : Token)
	{
		if (!FChar::IsDigit(Character))
		{
			return false;
		}
	}

	return true;
}

/**
	 * Reads a decimal token that has already been confirmed to be all digits.
	 * Kept separate from validation so each caller can phrase its own error.
	 */
[[nodiscard]] int32 ToInt(FStringView Token)
{
	int32 Value = 0;
	for (const TCHAR Character : Token)
	{
		Value = Value * 10 + (Character - TEXT('0'));
	}

	return Value;
}

void SetError(FString* OutError, FStringView SocketName, const FString& Reason)
{
	if (OutError != nullptr)
	{
		*OutError = FString::Printf(TEXT("Socket '%.*s': %s"), SocketName.Len(), SocketName.GetData(), *Reason);
	}
}

/**
	 * Reads one size token: a non-empty run of letters and digits, stored verbatim.
	 *
	 * The token carries no unit and is never read as a number. "40", "40mm", "4cm" and "Thin" are
	 * equally legal, because the only thing done with the value is compare it for equality against
	 * the matching field on another socket (DESIGN section 2.2). A project chooses a vocabulary;
	 * PolySnap only insists that both ends of a joint spell it the same way.
	 *
	 * Letters and digits only is what keeps the structural diagnostics sharp. It is the rule that
	 * catches a name still carrying its authoring-tail '.' after import, which would otherwise
	 * disappear into a token nobody meant to write.
	 *
	 * Thickness and Length have identical rules, so they share this. FieldName is what puts the
	 * right word in the diagnostic -- with two size fields in the grammar, "size" no longer says
	 * which of them the author mistyped.
	 */
[[nodiscard]] bool ParseSizeToken(FStringView Token, const TCHAR* FieldName, FStringView SocketName, FName& OutValue,
	FString* OutError)
{
	if (Token.IsEmpty())
	{
		SetError(OutError, SocketName, FString::Printf(TEXT("%s is empty"), FieldName));
		return false;
	}

	for (const TCHAR Character : Token)
	{
		if (!FChar::IsAlnum(Character))
		{
			SetError(OutError, SocketName,
				FString::Printf(TEXT("%s '%.*s' must be letters and digits only"), FieldName, Token.Len(),
					Token.GetData()));
			return false;
		}
	}

	OutValue = FName(Token.Len(), Token.GetData());
	return true;
}
} // namespace PolySnapSocketNamePrivate

void FPolySnapSocketName::SplitAuthoringTail(FStringView SocketName, FStringView& OutHead, FStringView& OutTail)
{
	using namespace PolySnapSocketNamePrivate;

	// Import rewrites the authored '.' to '_', so by the time a name reaches this parser the tail
	// is just a sixth underscore-separated field. There is no separator left to distinguish it by;
	// the head's fixed arity is the whole of the rule, and is why the grammar puts the variable
	// part last rather than letting the head grow.
	int32 Remaining = StraightFieldCount;
	for (int32 Index = 0; Index < SocketName.Len(); ++Index)
	{
		if (SocketName[Index] == FieldSeparator && --Remaining == 0)
		{
			OutHead = SocketName.Left(Index);
			OutTail = SocketName.RightChop(Index + 1);
			return;
		}
	}

	OutHead = SocketName;
	OutTail = FStringView();
}

bool FPolySnapSocketName::IsPolySnapSocket(FStringView SocketName)
{
	FStringView Head;
	FStringView Tail;
	SplitAuthoringTail(SocketName, Head, Tail);

	// The tag alone is not enough: "EdgeGuard" starts with "Edge" and is not ours. The separator
	// has to be there too, which is also what makes "Edge" on its own a foreign name rather than
	// a malformed one.
	const FStringView Tag = FStringView(TagEdge);
	if (Head.Len() <= Tag.Len())
	{
		return false;
	}

	return Head.Left(Tag.Len()).Equals(Tag, ESearchCase::IgnoreCase) && Head[Tag.Len()] == FieldSeparator;
}

bool FPolySnapSocketName::ParseSubType(FStringView Token, EPolySnapEdgeSubType& OutSubType)
{
	if (Token.Equals(TEXT("Straight"), ESearchCase::IgnoreCase))
	{
		OutSubType = EPolySnapEdgeSubType::Straight;
		return true;
	}

	return false;
}

FString FPolySnapSocketName::SubTypeToString(EPolySnapEdgeSubType SubType)
{
	switch (SubType)
	{
		case EPolySnapEdgeSubType::Straight:
			return TEXT("Straight");
		default:
			return TEXT("Unknown");
	}
}

EPolySnapParseResult FPolySnapSocketName::Parse(FStringView SocketName, FPolySnapSocketDescriptor& OutDescriptor,
	FString* OutError)
{
	using namespace PolySnapSocketNamePrivate;

	if (!IsPolySnapSocket(SocketName))
	{
		return EPolySnapParseResult::NotPolySnap;
	}

	// The tail is stripped first and never looked at again. Doing it in this order is what lets a
	// hex and a pent share a .blend: the duplicate ID they would otherwise appear to have is
	// Blender's suffix, and the real duplicate-ID check runs on stripped names, per part.
	FStringView Head;
	FStringView Tail;
	SplitAuthoringTail(SocketName, Head, Tail);

	TArray<FStringView, TInlineAllocator<StraightFieldCount>> Fields;
	{
		int32 FieldStart = 0;
		for (int32 Index = 0; Index <= Head.Len(); ++Index)
		{
			if (Index == Head.Len() || Head[Index] == FieldSeparator)
			{
				Fields.Add(Head.Mid(FieldStart, Index - FieldStart));
				FieldStart = Index + 1;
			}
		}
	}

	if (Fields.Num() != StraightFieldCount)
	{
		SetError(OutError, SocketName,
			FString::Printf(TEXT("expected %d underscore-separated fields (Edge_<ID>_<SubType>_<Thickness>_<Length>), found %d"),
				StraightFieldCount, Fields.Num()));
		return EPolySnapParseResult::Malformed;
	}

	const FStringView IdToken = Fields[1];
	const FStringView SubTypeToken = Fields[2];
	const FStringView ThicknessToken = Fields[3];
	const FStringView LengthToken = Fields[4];

	// ID is padded to three digits so that Blender's outliner and Unreal's socket manager, which
	// both sort alphabetically, put 002 before 010.
	if (IdToken.Len() != IdDigitCount || !IsAllDigits(IdToken))
	{
		SetError(OutError, SocketName,
			FString::Printf(TEXT("ID '%.*s' must be exactly %d digits, zero-padded"), IdToken.Len(), IdToken.GetData(),
				IdDigitCount));
		return EPolySnapParseResult::Malformed;
	}

	const int32 Id = ToInt(IdToken);
	if (Id < MinId || Id > MaxId)
	{
		SetError(OutError, SocketName, FString::Printf(TEXT("ID must be in [%03d, %d]"), MinId, MaxId));
		return EPolySnapParseResult::Malformed;
	}

	EPolySnapEdgeSubType SubType = EPolySnapEdgeSubType::Straight;
	if (!ParseSubType(SubTypeToken, SubType))
	{
		SetError(OutError, SocketName,
			FString::Printf(TEXT("unknown subtype '%.*s'; only Straight is in scope"), SubTypeToken.Len(),
				SubTypeToken.GetData()));
		return EPolySnapParseResult::Malformed;
	}

	// Both size fields are opaque tokens on identical terms, so they share one reader, and they are
	// read in the order they are written. Thickness is the panel across the edge; Length is the
	// edge itself, and the pair is what makes two edges of equal length but unequal thickness -- a
	// stepped seam -- incompatible rather than merely ugly.
	//
	// Thickness comes first because it is the field every subtype has. Length is Straight's shape
	// parameter, and a curved subtype would replace it with two of its own; keeping the
	// subtype-specific parameters last is what lets that happen without moving anything ahead of
	// them.
	FName Thickness;
	if (!ParseSizeToken(ThicknessToken, TEXT("thickness"), SocketName, Thickness, OutError))
	{
		return EPolySnapParseResult::Malformed;
	}

	FName Length;
	if (!ParseSizeToken(LengthToken, TEXT("length"), SocketName, Length, OutError))
	{
		return EPolySnapParseResult::Malformed;
	}

	OutDescriptor.SocketName = FName(SocketName);
	OutDescriptor.Id = Id;
	OutDescriptor.SubType = SubType;
	OutDescriptor.Thickness = Thickness;
	OutDescriptor.Length = Length;

	return EPolySnapParseResult::Parsed;
}

EPolySnapParseResult FPolySnapSocketName::Parse(FName SocketName, FPolySnapSocketDescriptor& OutDescriptor,
	FString* OutError)
{
	const FString AsString = SocketName.ToString();
	const EPolySnapParseResult Result = Parse(FStringView(AsString), OutDescriptor, OutError);
	if (Result == EPolySnapParseResult::Parsed)
	{
		// Keep the engine's own FName rather than the one rebuilt from the view, so a socket
		// lookup against the mesh compares identical entries.
		OutDescriptor.SocketName = SocketName;
	}

	return Result;
}
