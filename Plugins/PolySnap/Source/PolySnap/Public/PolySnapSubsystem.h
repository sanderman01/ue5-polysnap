// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "PolySnapSnapQuery.h"
#include "PolySnapTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "PolySnapSubsystem.generated.h"

class UPolySnapPieceComponent;

/**
 * The world's registry of PolySnap pieces, and the one place the global debug pass runs from.
 *
 * DESIGN section 2.8 says the assembly graph should be maintained incrementally rather than
 * rebuilt by scanning the world; this registry is where that graph will live once Milestone 3
 * builds it. For now it does the smaller job of answering "which sockets are near enough to be
 * worth testing".
 */
UCLASS()
class POLYSNAP_API UPolySnapSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Null outside a world, so callers must check. */
	[[nodiscard]] static UPolySnapSubsystem* Get(const UObject* WorldContextObject);

	//~ Begin FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject interface

	/** Called by a piece at BeginPlay. */
	void RegisterPiece(UPolySnapPieceComponent* Piece);

	/** Called by a piece at EndPlay. */
	void UnregisterPiece(UPolySnapPieceComponent* Piece);

	const TArray<TWeakObjectPtr<UPolySnapPieceComponent>>& GetRegisteredPieces() const { return RegisteredPieces; }

	/**
	 * Every socket in the world, excluding one piece's own, resolved into world space and
	 * filtered to those within SearchRadiusUu of Origin.
	 *
	 * The radius is a broad-phase filter on the piece, not the section 2.5 proximity test -- that
	 * one runs per socket pair, at a much tighter tolerance.
	 */
	void GatherWorldSockets(const UPolySnapPieceComponent* ExcludePiece, const FVector& Origin, double SearchRadiusUu,
		TArray<FPolySnapWorldSocket>& OutSockets) const;

	/** The current tolerances, read from UPolySnapSettings. */
	[[nodiscard]] static FPolySnapQueryTolerances GetQueryTolerances();

	/**
	 * Marks a piece as held, so the global debug pass can highlight it and skip drawing it twice.
	 * Passing null clears it.
	 */
	void SetHeldPiece(UPolySnapPieceComponent* Piece);

	[[nodiscard]] UPolySnapPieceComponent* GetHeldPiece() const { return HeldPiece.Get(); }

private:
	/** Registered pieces. Weak, because a piece may be destroyed without unregistering cleanly. */
	TArray<TWeakObjectPtr<UPolySnapPieceComponent>> RegisteredPieces;

	/** The piece currently carried by a builder component, if any. */
	TWeakObjectPtr<UPolySnapPieceComponent> HeldPiece;
};
