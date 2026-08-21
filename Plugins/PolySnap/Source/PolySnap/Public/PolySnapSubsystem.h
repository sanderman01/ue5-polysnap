// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "PolySnapSnapQuery.h"
#include "PolySnapTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "PolySnapSubsystem.generated.h"

class UPolySnapConnectorComponent;

/**
 * The world's registry of PolySnap parts, and the one place the global debug pass runs from.
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

	/** Called by a part at BeginPlay. */
	void RegisterPart(UPolySnapConnectorComponent* Part);

	/** Called by a part at EndPlay. */
	void UnregisterPart(UPolySnapConnectorComponent* Part);

	const TArray<TWeakObjectPtr<UPolySnapConnectorComponent>>& GetRegisteredParts() const { return RegisteredParts; }

	/**
	 * Every socket in the world, excluding one part's own, resolved into world space and
	 * filtered to those within SearchRadiusUu of Origin.
	 *
	 * The radius is a broad-phase filter on the part, not the section 2.5 proximity test -- that
	 * one runs per socket pair, at a much tighter tolerance.
	 */
	void GatherWorldSockets(const UPolySnapConnectorComponent* ExcludePart, const FVector& Origin,
		double SearchRadiusUu, TArray<FPolySnapWorldSocket>& OutSockets) const;

	/** The current tolerances, read from UPolySnapSettings. */
	[[nodiscard]] static FPolySnapQueryTolerances GetQueryTolerances();

	/**
	 * Marks a part as held, so the global debug pass can highlight it and skip drawing it twice.
	 * Passing null clears it.
	 */
	void SetHeldPart(UPolySnapConnectorComponent* Part);

	[[nodiscard]] UPolySnapConnectorComponent* GetHeldPart() const { return HeldPart.Get(); }

private:
	/** Registered parts. Weak, because a part may be destroyed without unregistering cleanly. */
	TArray<TWeakObjectPtr<UPolySnapConnectorComponent>> RegisteredParts;

	/** The part currently carried by a builder component, if any. */
	TWeakObjectPtr<UPolySnapConnectorComponent> HeldPart;
};
