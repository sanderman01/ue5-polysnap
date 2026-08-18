// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PolySnapTypes.h"

#include "PolySnapSettings.generated.h"

/**
 * PolySnap's own settings, shipped with the plugin rather than kept in project config.
 *
 * Defaults live in the constructor, so a project that authors to different conventions overrides
 * them locally and the plugin keeps carrying its own. CONVENTIONS.md section 6 requires this of
 * the validator switches in particular.
 *
 * README section 7 lists tolerances as an open question -- whether they are global, per socket
 * type, or scaled by piece size is unexplored. Everything here is global, which is the simplest
 * thing that can work and the thing to revisit first.
 */
UCLASS(config = PolySnap, defaultconfig, meta = (DisplayName = "PolySnap"))
class POLYSNAP_API UPolySnapSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPolySnapSettings();

	/** Convenience accessor. Never null: developer settings are a CDO the engine always provides. */
	[[nodiscard]] static const UPolySnapSettings& Get();

	//~ Begin UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	//~ End UDeveloperSettings interface

	// -- Conventions, CONVENTIONS.md section 2 ----------------------------------------------

	/**
	 * Which socket-local axis carries which role, for every piece that does not override it.
	 *
	 * The default is CONVENTIONS.md section 2's table, which is what the Blender pipeline in
	 * sections 3 to 5 produces. Set this to whichever pipeline the project's assets mostly come
	 * from: the import validator has no piece component to ask and checks against this alone.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Conventions")
	FPolySnapSocketAxes DefaultSocketAxes;

	// -- Snapping ---------------------------------------------------------------------------

	/** How close two socket positions must be before the pair is a candidate at all. */
	UPROPERTY(config, EditAnywhere, Category = "Snapping", meta = (ClampMin = "0.1", ForceUnits = "cm"))
	float SnapDistanceUu = 25.0f;

	/**
	 * How far from collinear the two tangent lines may be. Polarity is ignored here: the test is
	 * on the lines, and both polarities are admissible (README section 2.3).
	 */
	UPROPERTY(config, EditAnywhere, Category = "Snapping",
		meta = (ClampMin = "0.1", ClampMax = "89.0", ForceUnits = "deg"))
	float TangentAngleToleranceDegrees = 20.0f;

	/** How far in front of the camera a held piece is carried, before the player pushes or pulls it. */
	UPROPERTY(config, EditAnywhere, Category = "Building", meta = (ClampMin = "10.0", ForceUnits = "cm"))
	float DefaultHoldDistanceUu = 250.0f;

	/** Limits on how far the player may push or pull a held piece. */
	UPROPERTY(config, EditAnywhere, Category = "Building", meta = (ClampMin = "10.0", ForceUnits = "cm"))
	float MinHoldDistanceUu = 100.0f;

	UPROPERTY(config, EditAnywhere, Category = "Building", meta = (ClampMin = "10.0", ForceUnits = "cm"))
	float MaxHoldDistanceUu = 800.0f;

	/** How far the pick-up trace reaches from the camera. */
	UPROPERTY(config, EditAnywhere, Category = "Building", meta = (ClampMin = "10.0", ForceUnits = "cm"))
	float GrabReachUu = 600.0f;

	/** How much one push-or-pull input step moves the held piece. */
	UPROPERTY(config, EditAnywhere, Category = "Building", meta = (ClampMin = "1.0", ForceUnits = "cm"))
	float HoldDistanceStepUu = 25.0f;

	/**
	 * How fast holding a roll key turns the held piece about the view axis.
	 *
	 * A rate rather than a per-press step: the roll keys are held down, so the input fires every
	 * frame and only a rate multiplied by delta time is both smooth and framerate independent.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Building", meta = (ClampMin = "1.0", ForceUnits = "deg/s"))
	float RollRateDegreesPerSecond = 90.0f;

	// -- Physics ----------------------------------------------------------------------------

	/**
	 * Damping applied to a released piece. Gravity is zero in this game, so without damping a
	 * nudged panel drifts out of the level and never comes back.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Physics", meta = (ClampMin = "0.0"))
	float PieceLinearDamping = 1.5f;

	UPROPERTY(config, EditAnywhere, Category = "Physics", meta = (ClampMin = "0.0"))
	float PieceAngularDamping = 2.0f;

	// -- Validation, CONVENTIONS.md section 6 -----------------------------------------------

	/** Percentage tolerance on the edge length check. A percentage is deliberate: a panel mislabelled
	 * by a millimetre announces itself the first time it refuses to snap, faster than any warning. */
	UPROPERTY(config, EditAnywhere, Category = "Validation", meta = (ClampMin = "0.01", ClampMax = "50.0"))
	float EdgeLengthTolerancePercent = 1.0f;

	/**
	 * How deep behind the outermost vertex along a socket's Outward axis to gather vertices when
	 * measuring that edge. Wide enough to catch a chamfer, narrow enough to exclude the opposite
	 * side of the panel.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Validation", meta = (ClampMin = "0.01", ForceUnits = "cm"))
	float EdgeProbeDepthUu = 2.0f;

	/** Outward pointing into the piece -- a 180 degree slip, or swapped Outward and Tangent roles. */
	UPROPERTY(config, EditAnywhere, Category = "Validation")
	bool bWarnOnInwardOutward = true;

	/** On a planar piece, a socket Normal not parallel to the piece's local +Z: authored standing, or stray roll. */
	UPROPERTY(config, EditAnywhere, Category = "Validation")
	bool bWarnOnNonCanonicalPieceFrame = true;

	/** On a planar piece, a socket off the mid-plane -- the origin-on-a-face flip-displacement trap. */
	UPROPERTY(config, EditAnywhere, Category = "Validation")
	bool bWarnOnSocketOffMidPlane = true;

	/** Socket 001's Outward not parallel to the piece's local +X. House style, not a requirement. */
	UPROPERTY(config, EditAnywhere, Category = "Validation")
	bool bWarnOnUnalignedPrimarySocket = true;

	/** A purely numeric authoring tail: Blender assigned it, so a name collision went unnoticed. */
	UPROPERTY(config, EditAnywhere, Category = "Validation")
	bool bWarnOnNumericAuthoringTail = true;

	/** How far from parallel the warnings above tolerate before firing. */
	UPROPERTY(config, EditAnywhere, Category = "Validation",
		meta = (ClampMin = "0.01", ClampMax = "45.0", ForceUnits = "deg"))
	float AxisAlignmentToleranceDegrees = 1.0f;

	/**
	 * A piece counts as planar when its smallest bounding extent is at most this fraction of its
	 * largest. Three of the warnings above are scoped to planar pieces, because the convention
	 * they check is itself about flat panels.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Validation", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float PlanarExtentRatio = 0.25f;
};
