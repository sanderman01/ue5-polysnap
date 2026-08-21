// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "PolySnapTypes.h"

/**
 * The socket maths of DESIGN sections 2.3 and 2.5. Pure: transforms in, transforms out, no
 * engine state, no world. This is the part Milestone 1 exists to prove, and it is deliberately
 * reachable from an automation spec without spawning anything.
 *
 * Every function from BasisFromTransform down obeys one canonical convention: Outward is
 * socket-local +X, Tangent is socket-local MINUS Y, Normal is +Z, and Unreal's cross product
 * gives Normal == Tangent ^ Outward rather than the other way round. That is CONVENTIONS.md
 * section 2's table, and the default a project ships with.
 *
 * An asset may be authored to any of the 24 legal mappings instead, so callers put a socket
 * transform through Canonicalise first and everything below is spared knowing about it.
 * AxisCorrection is the only function here that reads an FPolySnapSocketAxes.
 */
class POLYSNAP_API FPolySnapGeometry
{
public:
	/**
	 * The socket-local rotation that re-expresses a socket authored to Axes in the canonical
	 * convention. Identity for the canonical convention itself, so the default path is a no-op.
	 *
	 * Every legal mapping is one of the 24 rotations of a cube, so the result is exact. An
	 * illegal one returns identity rather than the singular frame its axes would otherwise
	 * build -- FTransform would turn that into a normalised quaternion of nonsense, which is
	 * far harder to notice than a part that simply did not move.
	 */
	[[nodiscard]] static FQuat AxisCorrection(const FPolySnapSocketAxes& Axes);

	/**
	 * The same frame at unit scale: rotation and translation kept, scale dropped.
	 *
	 * A socket's scale carries no information -- it is a Blender empty's object scale, which says
	 * nothing about the edge it marks -- and letting it reach the solver actively corrupts the
	 * answer, so it is discarded rather than trusted. Built from the quaternion directly: it is
	 * already exact, and a matrix round trip through the basis would only cost precision.
	 */
	[[nodiscard]] static FTransform WithoutScale(const FTransform& SocketTransform);

	/**
	 * Whether a frame is a rotation carrying at most a positive uniform scale.
	 *
	 * What the runtime is entitled to assume of an authored socket, and therefore what the
	 * import-time validator checks. Any positive uniform scale passes, because WithoutScale
	 * removes it before anything reads the frame. Non-uniform scale fails, having no single scale
	 * to remove; a mirror and a degenerate frame fail because they break the right-handed
	 * Outward/Tangent/Normal triad that every polarity test depends on.
	 *
	 * Shear is not on that list because an FTransform cannot carry one -- its rotation is a
	 * quaternion -- and a UStaticMeshSocket, storing an FRotator and an FVector, cannot either.
	 *
	 * Reads the raw matrix, never GetUnitAxis. CONVENTIONS.md section 6: that accessor goes
	 * through TransformVectorNoScale, so a check written on its output is orthonormal by
	 * construction and can never fail.
	 *
	 * @param OutUniformScale The scale that was found, meaningful only when this returns true.
	 * @param OutDeterminant  The raw determinant, for a diagnostic. Negative means mirrored.
	 */
	[[nodiscard]] static bool IsUniformlyScaledRotation(const FTransform& Transform, double& OutUniformScale,
		double& OutDeterminant);

	/**
	 * A socket transform as read off a mesh, re-expressed canonically and stripped of scale.
	 *
	 * The socket does not move and does not turn; only which of its axes carries which role
	 * changes. Pass the correction in rather than the axes: this runs once per socket per tick
	 * for every part near the player, and the correction is worth caching.
	 *
	 * This is the one chokepoint every runtime socket transform passes through, so it is where
	 * scale leaves the system -- the socket's own and any stray component scale alike. Everything
	 * downstream may take a socket transform to be unit-scaled.
	 */
	[[nodiscard]] static FTransform Canonicalise(const FTransform& RawSocketTransform, const FQuat& Correction);

	/** Resolves a socket transform into its three named directions. Any scale is ignored. */
	[[nodiscard]] static FPolySnapSocketBasis BasisFromTransform(const FTransform& SocketTransform);

	/**
	 * Rebuilds a socket transform from a basis, the inverse of BasisFromTransform.
	 *
	 * Used to turn a desired mating pose back into a transform. The axis assignment is the
	 * convention read backwards: X is Outward, Y is -Tangent, Z is Normal.
	 */
	[[nodiscard]] static FTransform TransformFromBasis(const FPolySnapSocketBasis& Basis);

	/**
	 * The signed dihedral from Anchor to Other, about the anchor's tangent, over [0, 360).
	 *
	 *     theta = atan2( (Outward_A ^ Outward_B) . Tangent_A,  Outward_A . Outward_B )
	 *
	 * 180 degrees is coplanar; 0 and 360 are both fully closed, reached from opposite sides.
	 *
	 * Deliberately not symmetric in its arguments. Measuring from the other socket's basis
	 * returns the same angle for an aligned pair and 360 - theta for a flipped one, so naming
	 * the anchor as the reference is what makes the angle single-valued at all.
	 *
	 * A signed range is necessary rather than tidy: the unsigned angle between two Outward
	 * vectors spans only 0-180 and reports theta and 360 - theta identically, collapsing
	 * concave corners onto convex ones.
	 */
	[[nodiscard]] static double DihedralDegrees(const FPolySnapSocketBasis& Anchor, const FPolySnapSocketBasis& Other);

	/**
	 * The world basis a held socket must take to mate with Anchor at the given polarity and
	 * dihedral. Positions coincide, tangents are collinear, and Outward is swept about the
	 * shared edge by theta.
	 */
	[[nodiscard]] static FPolySnapSocketBasis MatedBasis(const FPolySnapSocketBasis& Anchor, EPolySnapPolarity Polarity,
		double InDihedralDegrees);

	/**
	 * The world transform a part must take so that one of its sockets mates with an anchor.
	 *
	 * DESIGN section 2.5: exactly one socket pair is the anchor, and the part's transform is
	 * solved from the anchor and from nothing else. The result is a pure function of the anchor,
	 * the polarity and the dihedral -- which is also what lets a server re-derive it instead of
	 * trusting a client transform.
	 *
	 * The result is always unit-scaled: any scale on HeldSocketLocal is discarded rather than
	 * inverted into the answer. A socket's scale was never applied to its own offset, so dividing
	 * the offset by it would displace the part as well as shrink it.
	 *
	 * @param HeldSocketLocal  The held socket's transform relative to its part. Scale is ignored.
	 * @param Anchor           The target socket's basis, in world space.
	 * @param Polarity         Which way round the held part ends up.
	 * @param InDihedralDegrees The angle about the shared edge.
	 */
	[[nodiscard]] static FTransform SolvePartTransform(const FTransform& HeldSocketLocal,
		const FPolySnapSocketBasis& Anchor, EPolySnapPolarity Polarity, double InDihedralDegrees);

	/**
	 * The dihedral that puts the held part closest to the orientation it is already in, for one
	 * polarity. DESIGN section 2.5's player-driven reading: the nearest point on the constraint
	 * manifold to the part's current pose, so the player turns the part roughly the way they
	 * want it and the snap commits to that reading.
	 *
	 * Closed form, not a search. The admissible poses are a one-parameter family generated by
	 * rotation about the fixed world axis Tangent_A, so the optimum is the twist of the relative
	 * rotation about that axis -- one swing-twist decomposition.
	 *
	 * @param OutRequiredRotationDegrees How far the part still has to turn once that theta is
	 *                                   applied. This is the number the polarity choice compares.
	 */
	[[nodiscard]] static double NearestDihedralDegrees(const FTransform& HeldSocketLocal,
		const FTransform& CurrentPartTransform, const FPolySnapSocketBasis& Anchor, EPolySnapPolarity Polarity,
		double& OutRequiredRotationDegrees);

	/**
	 * The dihedral that brings one secondary socket pair as close together as rotation about the
	 * shared edge can. DESIGN section 2.5's adopt-driven reading, and the one that takes
	 * precedence: where a second pair is in tolerance, the placement is decided by the geometry
	 * already built rather than by how the player happened to be holding the part.
	 *
	 * Closed form, not a search. Solving the anchor at theta = 0 and then hinging is a rigid
	 * rotation of the whole part about the world line through the anchor, so the secondary socket
	 * sweeps a circle about that line. Decompose both offsets from the anchor into components
	 * along and across the axis, and one atan2 turns the held socket's across-axis component onto
	 * the target's.
	 *
	 * The along-axis component does not rotate, and neither does the radius: whatever those two
	 * leave over is the residual, and it is returned as the true post-solve distance rather than
	 * as either component, because that is the number a connection records.
	 *
	 * @param HeldAnchorSocketLocal The anchor socket's transform relative to its part.
	 * @param HeldSecondaryLocal    The secondary socket's position relative to the same part.
	 * @param Anchor                The target socket's basis, in world space.
	 * @param Polarity              Which way round the held part ends up.
	 * @param TargetSecondaryLocation The world position the secondary socket should close on.
	 * @return False when the secondary lies on the anchor axis, or its target does. Such a socket
	 *         sweeps a degenerate circle and has no dihedral to offer, so it is skipped rather
	 *         than answered with a meaningless angle.
	 */
	[[nodiscard]] static bool AdoptDihedralDegrees(const FTransform& HeldAnchorSocketLocal,
		const FVector& HeldSecondaryLocal, const FPolySnapSocketBasis& Anchor, EPolySnapPolarity Polarity,
		const FVector& TargetSecondaryLocation, double& OutDihedralDegrees, double& OutResidualUu);

	/**
	 * Picks the polarity and dihedral nearest the part's current orientation, over both
	 * polarities. Both are always admissible (DESIGN section 2.3), so this is the whole of the
	 * flip decision and the reason no explicit flip control exists.
	 */
	static void SolveNearestPlacement(const FTransform& HeldSocketLocal, const FTransform& CurrentPartTransform,
		const FPolySnapSocketBasis& Anchor, EPolySnapPolarity& OutPolarity, double& OutDihedralDegrees,
		double& OutRequiredRotationDegrees);

	/** The tangent a held socket must end up with, for a polarity. Aligned means opposed. */
	[[nodiscard]] static FVector MatedTangent(const FPolySnapSocketBasis& Anchor, EPolySnapPolarity Polarity)
	{
		return Polarity == EPolySnapPolarity::Aligned ? -Anchor.Tangent : Anchor.Tangent;
	}

	/** Geodesic angle between two rotations, in degrees. The measure "least rotation" is least by. */
	[[nodiscard]] static double AngleBetweenDegrees(const FQuat& A, const FQuat& B);

	/** Wraps an angle in degrees into [0, 360). */
	[[nodiscard]] static double WrapDegrees(double Degrees);
};
