// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "PolySnapSnapQuery.h"
#include "PolySnapTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "PolySnapSubsystem.generated.h"

class UPhysicsConstraintComponent;
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

	/**
	 * Records a solved placement: the anchor connection, every adopted one, and their residuals.
	 *
	 * The one place a connection is made. The player's builder and any developer tool that places
	 * a part programmatically both come through here, so what a milestone measures is what a
	 * player would have built, rather than a second implementation that agrees by inspection.
	 *
	 * The caller has already teleported the part onto Candidate.SolvedPartTransform: DESIGN
	 * section 2.5's exactness describes the instant of placement, so the residuals recorded here
	 * are measured from where the sockets actually ended up, not from where the solve said they
	 * would.
	 *
	 * @param bCreateConstraints Whether to hand the part to the simulation and build a physics
	 *                           constraint per connection. False records the topology and leaves
	 *                           every body kinematic -- which is what a measurement run wants,
	 *                           because a constraint solver leaves bodies slightly off their ideal
	 *                           positions and that strain would be indistinguishable from panel
	 *                           error (DESIGN section 5).
	 */
	void CommitPlacement(const FPolySnapCandidate& Candidate, bool bCreateConstraints);

	/**
	 * Realises one recorded connection as a physics constraint: free about the shared edge, locked
	 * everywhere else. Returns null when either side has no live body to bind to.
	 *
	 * Nothing authors an angle. What removes the remaining freedom is the next connection, not a
	 * limit set here.
	 */
	UPhysicsConstraintComponent* CreateConnectionConstraint(UPolySnapConnectorComponent* PartA, int32 SocketIdA,
		UPolySnapConnectorComponent* PartB, int32 SocketIdB);

	/**
	 * Builds one constraint for every connection recorded across every registered part, and
	 * returns how many. Meant to be called once, on an assembly committed without constraints;
	 * calling it twice would double up every joint.
	 *
	 * The counterpart to committing with bCreateConstraints false: an assembly can be built and
	 * measured kinematically, and only then handed to the simulation.
	 */
	int32 CreateConstraintsForAllConnections();

	/**
	 * What the assembly currently looks like: parts, connections, open sockets, and the residuals
	 * the adopted connections closed at. Printed by PolySnap.Report.
	 *
	 * Open sockets is the number that says whether a shell sealed; the worst residual is the
	 * number that says whether its panels are cut correctly.
	 */
	[[nodiscard]] FString BuildAssemblyReport() const;

	/** The current tolerances, read from UPolySnapSettings. */
	[[nodiscard]] static FPolySnapQueryTolerances GetQueryTolerances();

	/**
	 * Marks a part as held, so the global debug pass can highlight it and skip drawing it twice.
	 * Passing null clears it.
	 */
	void SetHeldPart(UPolySnapConnectorComponent* Part);

	[[nodiscard]] UPolySnapConnectorComponent* GetHeldPart() const { return HeldPart.Get(); }

private:
	/** How many of the worst seams BuildAssemblyReport lists. Enough to see a pattern, short enough to read. */
	static constexpr int32 ReportedWorstEdgeCount = 5;

	/** Registered parts. Weak, because a part may be destroyed without unregistering cleanly. */
	TArray<TWeakObjectPtr<UPolySnapConnectorComponent>> RegisteredParts;

	/** The part currently carried by a builder component, if any. */
	TWeakObjectPtr<UPolySnapConnectorComponent> HeldPart;
};
