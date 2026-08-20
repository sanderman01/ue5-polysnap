// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "PolySnapTypes.h"

/** Outcome of parsing one socket name. See FPolySnapSocketName::Parse for why there are three. */
enum class EPolySnapParseResult : uint8
{
	/** A well-formed PolySnap edge socket. OutDescriptor is filled in. */
	Parsed,

	/** Not named Edge_*, so it belongs to some other system. Pass over it in silence. */
	NotPolySnap,

	/** Named Edge_* and the rest failed to parse. This is a loud error, not a foreign socket. */
	Malformed
};

/**
 * The socket naming grammar of DESIGN section 2.2:
 *
 *     SOCKET_Edge_<ID>_<SubType>_<Thickness>_<Length>[.<AuthoringTail>]    as authored
 *     Edge_001_Straight_40_2000_Pent                                       as imported
 *
 * Unreal strips the SOCKET_ prefix at import, so the names this class sees begin at Edge_ -- and
 * it rewrites the tail's '.' to '_', so the underscore spelling is the only one that ever reaches
 * here. The head has fixed arity, which is what keeps that unambiguous: five fields, and anything
 * after them is tail.
 *
 * Everything here is pure: it takes a string and returns fields, with no engine state involved.
 */
class POLYSNAP_API FPolySnapSocketName
{
public:
	/** The namespace tag that marks a socket as PolySnap's. Never a variable field. */
	static constexpr TCHAR TagEdge[] = TEXT("Edge");

	/** Delimiter between fields. May not appear inside one. */
	static constexpr TCHAR FieldSeparator = TEXT('_');

	/**
	 * Splits an authoring tail off a socket name: everything past the five-field head.
	 *
	 * Blender object names are unique per file rather than per object, so a hex and a pent
	 * authored together cannot both own SOCKET_Edge_001_Straight_40_2000 and Blender appends a
	 * suffix to the second. Everything past the head is authoring scratch: not identity, not
	 * compatibility, and read by nothing at runtime.
	 *
	 * The '.' the grammar reserves for the tail does not survive import -- Unreal rewrites it to
	 * '_' -- so there is nothing to look for but the sixth field.
	 *
	 * @param SocketName  The full socket name.
	 * @param OutHead     The name with the tail removed.
	 * @param OutTail     The tail, excluding its separator. Empty when there is none.
	 */
	static void SplitAuthoringTail(FStringView SocketName, FStringView& OutHead, FStringView& OutTail);

	/**
	 * Parses one socket name into its fields.
	 *
	 * The three-way result is the point. DESIGN section 2.2 requires that a socket which is not
	 * PolySnap's is passed over without a diagnostic, while one that is PolySnap's and malformed
	 * fails loudly -- Edge_01_Straight_40_2000 is a typo, not a foreign socket. A bool cannot
	 * express that difference.
	 *
	 * The tag and subtype compare case-insensitively. A wrongly-cased tag would otherwise be
	 * silently skipped as somebody else's socket, which is exactly the failure the tag exists to
	 * prevent.
	 *
	 * @param SocketName    Engine socket name, i.e. with SOCKET_ already stripped.
	 * @param OutDescriptor Filled in only when the result is Parsed.
	 * @param OutError      Optional human-readable reason, set only when the result is Malformed.
	 */
	[[nodiscard]] static EPolySnapParseResult Parse(FStringView SocketName, FPolySnapSocketDescriptor& OutDescriptor,
		FString* OutError = nullptr);

	/** Convenience overload for the engine's socket names, which are already FNames. */
	[[nodiscard]] static EPolySnapParseResult Parse(FName SocketName, FPolySnapSocketDescriptor& OutDescriptor,
		FString* OutError = nullptr);

	/**
	 * True when the name is PolySnap's business at all, regardless of whether it parses.
	 * Cheap enough to use as a filter before deciding whether a parse failure deserves an error.
	 */
	[[nodiscard]] static bool IsPolySnapSocket(FStringView SocketName);

	/** Spelling of a subtype, for diagnostics and for round-tripping a name in tests. */
	[[nodiscard]] static FString SubTypeToString(EPolySnapEdgeSubType SubType);

	/** Parses a subtype token. Case-insensitive. Returns false for anything not in scope. */
	[[nodiscard]] static bool ParseSubType(FStringView Token, EPolySnapEdgeSubType& OutSubType);
};
