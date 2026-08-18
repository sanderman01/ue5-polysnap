// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapSocketName.h"

namespace PolySnapSocketNamePrivate
{
/** Three digits, zero-padded, 001-999. Zero is not an identity. */
constexpr int32 IdDigitCount = 3;
constexpr int32 MinId = 1;
constexpr int32 MaxId = 999;

/** Fields in the head of a well-formed Straight socket: Edge, ID, SubType, Size. */
constexpr int32 StraightFieldCount = 4;

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
} // namespace PolySnapSocketNamePrivate

void FPolySnapSocketName::SplitAuthoringTail(FStringView SocketName, FStringView& OutHead, FStringView& OutTail)
{
	int32 TailStart = INDEX_NONE;
	if (SocketName.FindChar(TailSeparator, TailStart))
	{
		OutHead = SocketName.Left(TailStart);
		OutTail = SocketName.RightChop(TailStart + 1);
	}
	else
	{
		OutHead = SocketName;
		OutTail = FStringView();
	}
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
	// Blender's suffix, and the real duplicate-ID check runs on stripped names, per piece.
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
			FString::Printf(TEXT("expected %d underscore-separated fields (Edge_<ID>_<SubType>_<Size>), found %d"),
				StraightFieldCount, Fields.Num()));
		return EPolySnapParseResult::Malformed;
	}

	const FStringView IdToken = Fields[1];
	const FStringView SubTypeToken = Fields[2];
	const FStringView SizeToken = Fields[3];

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

	// Size is unpadded on purpose: one size, one spelling, so Straight_500 and Straight_12000 can
	// coexist without a fixed width inviting a four-digit assumption.
	if (!IsAllDigits(SizeToken))
	{
		SetError(OutError, SocketName,
			FString::Printf(TEXT("size '%.*s' must be a bare integer in millimetres"), SizeToken.Len(),
				SizeToken.GetData()));
		return EPolySnapParseResult::Malformed;
	}

	if (SizeToken.Len() > 1 && SizeToken[0] == TEXT('0'))
	{
		SetError(OutError, SocketName,
			FString::Printf(TEXT("size '%.*s' is zero-padded; write Straight_500, never Straight_0500"),
				SizeToken.Len(), SizeToken.GetData()));
		return EPolySnapParseResult::Malformed;
	}

	const int32 SizeMillimetres = ToInt(SizeToken);
	if (SizeMillimetres <= 0)
	{
		SetError(OutError, SocketName, TEXT("size must be greater than zero"));
		return EPolySnapParseResult::Malformed;
	}

	OutDescriptor.SocketName = FName(SocketName);
	OutDescriptor.Id = Id;
	OutDescriptor.SubType = SubType;
	OutDescriptor.SizeMillimetres = SizeMillimetres;

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
