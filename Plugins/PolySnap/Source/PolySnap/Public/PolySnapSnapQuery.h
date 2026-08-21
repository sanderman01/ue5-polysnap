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

	/**
	 * How close a secondary socket pair must come, once the anchor's transform is applied, to be
	 * adopted into its joint (DESIGN section 2.5).
	 *
	 * Much tighter than SnapDistanceUu, and for a different reason: that one is how near the
	 * player must aim, this one is how much geometric error a seam may carry and still count as
	 * closed. DESIGN section 7 has it open in both directions -- too tight and a correctly built
	 * vertex refuses its second connection, too loose and visibly misaligned panels are welded
	 * into the graph as though they had closed.
	 */
	double AdoptionDistanceUu = 2.0;

	/** How far from collinear an adopted pair's tangent lines may be. Polarity is ignored, as above. */
	double AdoptionAngleToleranceDegrees = 5.0;

	/**
	 * How far a fold must be from fully closed before it counts as a placement at all
	 * (DESIGN section 2.5).
	 *
	 * A dihedral near 0 or 360 puts the held socket's Outward onto the anchor's own Outward, so
	 * the held panel lies in the half-plane the target panel already occupies -- the two are in
	 * the same space. Decidable from the two socket bases alone, which is what lets this be
	 * refused without a collision query the snapper has no business making.
	 */
	double MinDihedralDegrees = 5.0;
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
	 * The anchor is the pair the player is driving, and its dihedral is solved in the order
	 * DESIGN section 2.5 gives: adopt-driven first -- the theta that closes some second socket
	 * pair, so the geometry already built decides the fold -- and the player-driven nearest
	 * placement only when no second pair comes within AdoptionDistanceUu. Where several pass, they
	 * are scored
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
	 * @return The winning candidate, its Adoptions filled in, or an unset candidate when nothing
	 *         passed.
	 */
	[[nodiscard]] static FPolySnapCandidate FindBest(const TArray<FPolySnapWorldSocket>& HeldSockets,
		const TArray<FPolySnapWorldSocket>& TargetSockets, const FTransform& HeldPartTransform,
		const FPolySnapQueryTolerances& Tolerances, TArray<FPolySnapRejectedPair>* OutRejections = nullptr);

	/**
	 * Every socket pair, other than the anchor's, that lands within the adoption tolerances once
	 * the solved transform is applied. DESIGN section 2.5's "one anchor, many connections".
	 *
	 * Greedy by smallest gap, and each socket on either side is claimed at most once: a socket is
	 * one edge of one panel, so two of them closing on the same target means one of the two is
	 * wrong, and the nearer is the better guess.
	 *
	 * Only unconnected target sockets are adopted. A joint may host more than two panels (DESIGN
	 * section 2.4), but joining a new part into an occupied joint needs the first-class joint that
	 * Milestone 3 introduces; until then an adoption that silently made a seam a threesome would
	 * be a harder bug to see than a seam that simply stayed open.
	 *
	 * Adopt or drop, per secondary: nothing here can reject the anchor. The player gets the
	 * placement they asked for plus an unclosed seam they can see and nudge.
	 *
	 * @param HeldPartTransform Where the held part was when the anchor was chosen, which is what
	 *                          the held sockets' world transforms are relative to.
	 */
	static void FindAdoptions(const TArray<FPolySnapWorldSocket>& HeldSockets,
		const TArray<FPolySnapWorldSocket>& TargetSockets, const FTransform& HeldPartTransform,
		const FPolySnapCandidate& Anchor, const FPolySnapQueryTolerances& Tolerances,
		TArray<FPolySnapAdoption>& OutAdoptions);

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
