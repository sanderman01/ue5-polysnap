// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "PolySnapTypes.h"

class UPolySnapPieceComponent;
class UWorld;

/**
 * Debug visualisation for playtesting.
 *
 * Driven entirely by console variables, so what is drawn can be changed mid-session without a
 * rebuild or a rebind:
 *
 *     PolySnap.Debug.Draw         0 off, 1 held piece and candidates, 2 every registered piece
 *     PolySnap.Debug.Text         on-screen readout
 *     PolySnap.Debug.MaxDistance  culling distance in Unreal units
 *     PolySnap.Debug.GizmoScale   size multiplier for the socket gizmos
 *
 * The socket gizmo is the important one. It draws the three named directions in the level --
 * Outward red, Tangent green, Normal blue -- so an axis-role mistake in Blender is something you
 * can see rather than infer from a snap that misbehaves.
 */
class POLYSNAP_API FPolySnapDebug
{
public:
	/** 0 off, 1 held piece and candidates only, 2 every registered piece. */
	[[nodiscard]] static int32 GetDrawMode();

	/** Whether the on-screen readout is enabled. */
	[[nodiscard]] static bool IsTextEnabled();

	/** Beyond this distance from the viewer, nothing is drawn. */
	[[nodiscard]] static double GetMaxDrawDistance();

	/** Gizmo size multiplier. */
	[[nodiscard]] static double GetGizmoScale();

	/** True when Location is close enough to the local viewer to be worth drawing. */
	[[nodiscard]] static bool IsInDrawRange(const UWorld* World, const FVector& Location);

	/**
	 * Draws one socket's basis: three arrows for Outward, Tangent and Normal, a sphere at the
	 * socket position coloured by whether it is connected, and the socket ID as text.
	 */
	static void DrawSocket(const UWorld* World, const FTransform& SocketTransform,
		const FPolySnapSocketDescriptor& Descriptor, bool bConnected, bool bHighlighted);

	/** Draws every socket on one piece. */
	static void DrawPiece(const UWorld* World, const UPolySnapPieceComponent& Piece, bool bHighlighted);

	/** Draws the link between the two sockets of a candidate pair, with the gap and angle. */
	static void DrawCandidate(const UWorld* World, const FPolySnapCandidate& Candidate);

	/** Draws where a held piece would land if placed now: its bounds as a wireframe box. */
	static void DrawPreview(const UWorld* World, const UPolySnapPieceComponent& HeldPiece,
		const FTransform& SolvedTransform);

	/**
	 * Draws the shared edge line of a committed connection, with the dihedral it settled at.
	 *
	 * The line marks the hinge axis at a fixed gizmo size; it is not the edge's true extent.
	 * Nothing knows that extent -- the Length token is an opaque label with no unit (DESIGN
	 * section 2.2), and the mesh is not measured at runtime.
	 */
	static void DrawCommittedJoint(const UWorld* World, const FTransform& JointTransform, double DihedralDegrees);
};
