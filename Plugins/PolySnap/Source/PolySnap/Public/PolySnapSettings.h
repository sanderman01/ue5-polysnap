// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PolySnapTypes.h"

#include "PolySnapSettings.generated.h"

/** Fired whenever a PolySnap setting changes at runtime, from the settings panel or from
 *  PolySnap.SetDamping. It exists so a live part can re-apply a value that is otherwise only read
 *  once, at BeginPlay. */
DECLARE_MULTICAST_DELEGATE(FPolySnapSettingsChanged);

/**
 * PolySnap's own settings, shipped with the plugin rather than kept in project config.
 *
 * Defaults live in the constructor, so a project that authors to different conventions overrides
 * them locally and the plugin keeps carrying its own. CONVENTIONS.md section 6 requires this of
 * the validator switches in particular.
 *
 * DESIGN section 7 lists tolerances as an open question -- whether they are global, per socket
 * type, or scaled by part size is unexplored. Everything here is global, which is the simplest
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

#if WITH_EDITOR
	//~ Begin UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject interface
#endif

	/** Subscribe to retune without leaving PIE. Broadcast on every settings-panel edit. */
	[[nodiscard]] static FPolySnapSettingsChanged& OnSettingsChanged();

	// -- Conventions, CONVENTIONS.md section 2 ----------------------------------------------

	/**
	 * Which socket-local axis carries which role, for every part that does not override it.
	 *
	 * The default is CONVENTIONS.md section 2's table, which is what the Blender pipeline in
	 * sections 3 to 5 produces. Set this to whichever pipeline the project's assets mostly come
	 * from: the import validator has no part component to ask and checks against this alone.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Conventions")
	FPolySnapSocketAxes DefaultSocketAxes;

	// -- Snapping ---------------------------------------------------------------------------

	/** How close two socket positions must be before the pair is a candidate at all. */
	UPROPERTY(config, EditAnywhere, Category = "Snapping", meta = (ClampMin = "0.1", ForceUnits = "cm"))
	float SnapDistanceUu = 25.0f;

	/**
	 * How far from collinear the two tangent lines may be. Polarity is ignored here: the test is
	 * on the lines, and both polarities are admissible (DESIGN section 2.3).
	 */
	UPROPERTY(config, EditAnywhere, Category = "Snapping",
		meta = (ClampMin = "0.1", ClampMax = "89.0", ForceUnits = "deg"))
	float TangentAngleToleranceDegrees = 20.0f;

	/** How far in front of the camera a held part is carried, before the player pushes or pulls it. */
	UPROPERTY(config, EditAnywhere, Category = "Building", meta = (ClampMin = "10.0", ForceUnits = "cm"))
	float DefaultHoldDistanceUu = 250.0f;

	/** Limits on how far the player may push or pull a held part. */
	UPROPERTY(config, EditAnywhere, Category = "Building", meta = (ClampMin = "10.0", ForceUnits = "cm"))
	float MinHoldDistanceUu = 100.0f;

	UPROPERTY(config, EditAnywhere, Category = "Building", meta = (ClampMin = "10.0", ForceUnits = "cm"))
	float MaxHoldDistanceUu = 800.0f;

	/** How far the pick-up trace reaches from the camera. */
	UPROPERTY(config, EditAnywhere, Category = "Building", meta = (ClampMin = "10.0", ForceUnits = "cm"))
	float GrabReachUu = 600.0f;

	/** How much one push-or-pull input step moves the held part. */
	UPROPERTY(config, EditAnywhere, Category = "Building", meta = (ClampMin = "1.0", ForceUnits = "cm"))
	float HoldDistanceStepUu = 25.0f;

	/**
	 * How fast holding a roll key turns the held part about the view axis.
	 *
	 * A rate rather than a per-press step: the roll keys are held down, so the input fires every
	 * frame and only a rate multiplied by delta time is both smooth and framerate independent.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Building", meta = (ClampMin = "1.0", ForceUnits = "deg/s"))
	float RollRateDegreesPerSecond = 90.0f;

	// -- Physics ----------------------------------------------------------------------------

	/**
	 * Damping applied to a released part. Gravity is zero in this game, so without damping a
	 * nudged panel drifts out of the level and never comes back.
	 *
	 * Angular wants to be the higher of the two: a spinning panel is more disorienting than a
	 * drifting one, and it is rotation that makes a socket hard to line up. Past roughly 15 the
	 * damping starts to fight the joint solver, and a loose assembly settles slowly rather than
	 * quickly -- at that point the thing to tune is joint stiffness, not this.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Physics", meta = (ClampMin = "0.0"))
	float PartLinearDamping = 5.0f;

	UPROPERTY(config, EditAnywhere, Category = "Physics", meta = (ClampMin = "0.0"))
	float PartAngularDamping = 8.0f;

	/**
	 * Whether a settled part may fall asleep sooner than the engine's default threshold.
	 *
	 * Damping approaches zero velocity asymptotically and never arrives, so a part keeps
	 * crawling long after it looks stopped. Sleep is what actually ends the motion, and a
	 * sleeping island costs the solver nothing as assemblies grow.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Physics")
	bool bUsePartSleepThreshold = true;

	/**
	 * Multiplies the linear and angular velocities below which a part counts as at rest, so a
	 * higher value sleeps sooner. The velocities themselves come from the body's physical
	 * material, or from Chaos's defaults where it has none.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Physics",
		meta = (ClampMin = "0.1", EditCondition = "bUsePartSleepThreshold"))
	float PartSleepThresholdMultiplier = 5.0f;

	// -- Validation, CONVENTIONS.md section 6 -----------------------------------------------

	/** Outward pointing into the part -- a 180 degree slip, or swapped Outward and Tangent roles. */
	UPROPERTY(config, EditAnywhere, Category = "Validation")
	bool bWarnOnInwardOutward = true;

	/** On a planar part, a socket Normal not parallel to the part's local +Z: authored standing, or stray roll. */
	UPROPERTY(config, EditAnywhere, Category = "Validation")
	bool bWarnOnNonCanonicalPartFrame = true;

	/** On a planar part, a socket off the mid-plane -- the origin-on-a-face flip-displacement trap. */
	UPROPERTY(config, EditAnywhere, Category = "Validation")
	bool bWarnOnSocketOffMidPlane = true;

	/** The lowest-numbered socket's Outward not parallel to the part's local +X. House style, not a requirement. */
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
	 * A part counts as planar when its smallest bounding extent is at most this fraction of its
	 * largest. Three of the warnings above are scoped to planar parts, because the convention
	 * they check is itself about flat panels.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Validation", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float PlanarExtentRatio = 0.25f;
};
