// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"

#include "SandboxCharacter.generated.h"

class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * First-person character for the Sandbox playtest level.
 *
 * Its only current behaviour is a debug free-flight mode: the character movement component is
 * put into MOVE_Flying and tuned to stop the instant input is released, so panels can be
 * positioned precisely without fighting gravity or momentum. This is playtest scaffolding
 * rather than a gameplay feature -- the real movement model is still undecided.
 *
 * Horizontal movement stays on whatever the Blueprint binds to IA_Move, which in the First
 * Person template is control-yaw relative and therefore already horizontal. This class only
 * adds vertical translation and a speed boost, through a mapping context it builds at runtime
 * so that no extra Input Action assets are needed.
 */
UCLASS()
class ASandboxCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ASandboxCharacter();

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	//~ End AActor interface

	//~ Begin APawn interface
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	//~ End APawn interface

protected:
	/** Master switch for debug flight. Turn this off to get the template's walking character back. */
	UPROPERTY(EditDefaultsOnly, Category = "Debug Flight")
	bool bDebugFlightEnabled = true;

	/** Flight speed with no boost held. Slow on purpose: this is a placement tool, not a traversal one. */
	UPROPERTY(EditDefaultsOnly, Category = "Debug Flight",
		meta = (ClampMin = "0.0", ForceUnits = "cm/s", EditCondition = "bDebugFlightEnabled"))
	float FlySpeed = 600.0f;

	/** Flight speed while the boost key is held, for crossing the level quickly. */
	UPROPERTY(EditDefaultsOnly, Category = "Debug Flight",
		meta = (ClampMin = "0.0", ForceUnits = "cm/s", EditCondition = "bDebugFlightEnabled"))
	float BoostedFlySpeed = 2400.0f;

	/** How hard the character accelerates. High enough that it reaches full speed within a frame or two. */
	UPROPERTY(EditDefaultsOnly, Category = "Debug Flight",
		meta = (ClampMin = "0.0", EditCondition = "bDebugFlightEnabled"))
	float FlyAcceleration = 8192.0f;

	/** How hard the character brakes once input stops. See EnterDebugFlight() for why this matters most. */
	UPROPERTY(EditDefaultsOnly, Category = "Debug Flight",
		meta = (ClampMin = "0.0", EditCondition = "bDebugFlightEnabled"))
	float FlyBrakingDeceleration = 8192.0f;

	/**
	 * Priority of the runtime flight mapping context. Above IMC_Default so the flight keys win,
	 * which also stops Space reaching IA_Jump -- jumping does nothing in MOVE_Flying anyway.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Debug Flight", meta = (EditCondition = "bDebugFlightEnabled"))
	int32 FlightMappingContextPriority = 100;

private:
	/** Puts the movement component into MOVE_Flying and applies the tuning above. */
	void EnterDebugFlight();

	/** Builds the transient input actions and mapping context. Idempotent. */
	void BuildFlightInputMappings();

	void HandleFlyVertical(const FInputActionValue& Value);
	void HandleBoostStarted();
	void HandleBoostCompleted();

	/** Applies a max speed to the movement component, guarding against a missing component. */
	void ApplyFlySpeed(float NewFlySpeed);

	/** Built at runtime rather than authored as assets -- see the class comment. */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> FlightMappingContext;

	/** Axis1D: +1 up, -1 down, in world space regardless of where the camera points. */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> FlyVerticalAction;

	/** Boolean: held for BoostedFlySpeed. */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> FlyBoostAction;
};
