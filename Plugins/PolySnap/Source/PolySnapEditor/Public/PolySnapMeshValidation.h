// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class UStaticMesh;

/** How badly a validation message should be taken. */
enum class EPolySnapValidationSeverity : uint8
{
	/** A convention this project chose but another might not. Switchable in UPolySnapSettings. */
	Warning,

	/** The asset is wrong, and a snap built on it will misbehave in a way that looks like a bug. */
	Error
};

/** One thing the validator found. */
struct FPolySnapValidationMessage
{
	EPolySnapValidationSeverity Severity = EPolySnapValidationSeverity::Warning;
	FString Text;
};

/** Everything the validator found on one asset. */
struct FPolySnapValidationReport
{
	TArray<FPolySnapValidationMessage> Messages;

	/** How many PolySnap sockets the mesh carried. Zero means the asset was not ours to check. */
	int32 SocketCount = 0;

	[[nodiscard]] int32 CountBySeverity(EPolySnapValidationSeverity Severity) const;

	[[nodiscard]] bool HasErrors() const { return CountBySeverity(EPolySnapValidationSeverity::Error) > 0; }

	void Add(EPolySnapValidationSeverity Severity, FString&& Text)
	{
		Messages.Add(FPolySnapValidationMessage{Severity, MoveTemp(Text)});
	}
};

/**
 * Checks a static mesh against the socket conventions, so a bad export fails loudly at import
 * rather than mysteriously at runtime. CONVENTIONS.md section 6 is the specification.
 *
 * Sockets not named Edge_* belong to some other system and are skipped without a diagnostic.
 */
class POLYSNAPEDITOR_API FPolySnapMeshValidation
{
public:
	/** Runs every check. Fills OutReport; never logs. */
	static void Validate(const UStaticMesh* StaticMesh, FPolySnapValidationReport& OutReport);

	/** Runs every check and writes the result to LogPolySnap. Returns false if anything errored. */
	static bool ValidateAndLog(const UStaticMesh* StaticMesh);
};
