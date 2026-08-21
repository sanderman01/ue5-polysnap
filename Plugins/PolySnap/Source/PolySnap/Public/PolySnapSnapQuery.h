// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "PolySnapTypes.h"

/**
 * The tolerances a query runs against, copied out of UPolySnapSettings by the caller.
 *
 * Passing them in rather than reading the settings object is what keeps the query pure, and
 * therefore reachable from an automation spec with no engine state set up.
 */
struct POLYSNAP_API FPolySnapQueryTolerances
{
	/** How close two socket positions must be, in Unreal units. */
	double SnapDistanceUu = 25.0;

	/** How far from collinear the two tangent lines may be, in degrees. */
	double TangentAngleToleranceDegrees = 20.0;
};

/** One rejected pair, kept so the debug readout can say why a snap did not happen. */
struct POLYSNAP_API FPolySnapRejectedPair
{
	FName HeldSocketName;
	FName TargetSocketName;
	EPolySnapRejection Reason = EPolySnapRejection::None;
	double GapUu = 0.0;
	double TangentAngleDegrees = 0.0;
};

/**
 * DESIGN section 2.5's candidate evaluation, and the choice of which passing pair anchors.
 *
 * Pure: it takes two arrays of world sockets and returns a candidate. The world, the actors and
 * the settings object all stay on the caller's side of this line.
 */
class POLYSNAP_API FPolySnapSnapQuery
{
public:
	/**
	 * Tests one socket pair against all four criteria, in the order section 2.5 gives them --
	 * descriptor first, because it is an integer comparison that rejects most pairs for free.
	 *
	 * @param OutCandidate Filled in only when the pair passes, and only with the fields that do
	 *                     not depend on a placement solve.
	 * @return None when the pair passes, otherwise why it did not.
	 */
	[[nodiscard]] static EPolySnapRejection TestPair(const FPolySnapWorldSocket& HeldSocket,
		const FPolySnapWorldSocket& TargetSocket, const FPolySnapQueryTolerances& Tolerances,
		FPolySnapCandidate& OutCandidate);

	/**
	 * Finds the pair that should anchor a placement.
	 *
	 * The anchor is the pair the player is driving. Where several pass, they are scored
	 *
	 *     cost = Gap / SnapDistance + RequiredRotation / TangentAngleTolerance
	 *
	 * -- each term normalised by its own tolerance, so the score is dimensionless and introduces
	 * no tuning number that is not already a documented tolerance. DESIGN section 7 lists anchor
	 * selection as unexplored; this is the placeholder, and the residual it leaves is the thing
	 * Milestone 2 measures.
	 *
	 * @param HeldSockets       Sockets of the part being placed, in world space.
	 * @param TargetSockets     Every other socket in range, in world space.
	 * @param HeldPartTransform Where the held part currently is, which decides polarity.
	 * @param OutRejections     Optional; the pairs that failed, for the debug readout.
	 * @return The winning candidate, or an unset candidate when nothing passed.
	 */
	[[nodiscard]] static FPolySnapCandidate FindBest(const TArray<FPolySnapWorldSocket>& HeldSockets,
		const TArray<FPolySnapWorldSocket>& TargetSockets, const FTransform& HeldPartTransform,
		const FPolySnapQueryTolerances& Tolerances, TArray<FPolySnapRejectedPair>* OutRejections = nullptr);

	/** Human-readable rejection reason, for logs and the on-screen readout. */
	[[nodiscard]] static FString RejectionToString(EPolySnapRejection Rejection);

private:
	/**
	 * DESIGN section 2.4: socket occupancy is a capacity question, not a boolean -- "is this
	 * socket taken?" becomes "does this joint accept another participant?", and by default it
	 * does. Milestone 1 has no first-class joint, so this always passes; it exists as a call so
	 * Milestone 3's assembly graph has somewhere to put the real test.
	 */
	[[nodiscard]] static bool JointAcceptsParticipant(const FPolySnapWorldSocket& HeldSocket,
		const FPolySnapWorldSocket& TargetSocket);
};
