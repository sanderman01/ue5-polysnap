// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PolySnapTypes.h"

#include "PolySnapBuilderComponent.generated.h"

class APawn;
class UInputAction;
class UInputMappingContext;
class UPolySnapConnectorComponent;
struct FInputActionValue;

/**
 * Lets a pawn pick up a PolySnap part, carry it, see where it would snap, and place it.
 *
 * Add it to any player pawn -- it needs nothing from the pawn's class, and it builds its own
 * input mapping context at runtime rather than requiring Input Action assets to be authored. That
 * is what keeps PolySnap free of dependencies on any particular project's input setup.
 *
 * While a part is held it is kinematic and locked to the view, because its transform is the
 * answer the snap solver produced and a simulating body would drift straight off it. On placement
 * the part is teleported to the solved transform and handed to UPolySnapSubsystem::CommitPlacement,
 * which records the anchor connection and every adopted one, then starts the part simulating
 * again -- joined to its neighbours by constraints free about each shared edge and locked
 * everywhere else.
 */
UCLASS(ClassGroup = "PolySnap", meta = (BlueprintSpawnableComponent), DisplayName = "PolySnap Builder")
class POLYSNAP_API UPolySnapBuilderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPolySnapBuilderComponent();

	//~ Begin UActorComponent interface
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent interface

	/** The part being carried, if any. */
	UFUNCTION(BlueprintPure, Category = "PolySnap")
	UPolySnapConnectorComponent* GetHeldPart() const { return HeldPart; }

	/** Picks up the part under the crosshair. Does nothing if something is already held. */
	UFUNCTION(BlueprintCallable, Category = "PolySnap")
	bool GrabPartUnderCrosshair();

	/** Places the held part, snapping it if a candidate is in tolerance. */
	UFUNCTION(BlueprintCallable, Category = "PolySnap")
	void PlaceHeldPart();

	/** Lets go of the held part where it is, never snapping. */
	UFUNCTION(BlueprintCallable, Category = "PolySnap")
	void DropHeldPart();

	/** Priority of the runtime mapping context. Above the project's own, so building keys win. */
	UPROPERTY(EditDefaultsOnly, Category = "PolySnap")
	int32 InputMappingContextPriority = 110;

private:
	/** Binds the build keys. Called from BeginPlay, and again if the pawn restarts. */
	void SetupInput();

	UFUNCTION()
	void HandlePawnRestarted(APawn* Pawn);

	/** Builds the transient input actions and mapping context. Idempotent. */
	void BuildInputMappings();

	void HandleGrabOrPlace();
	void HandleDrop();
	void HandleRoll(const FInputActionValue& Value);
	void HandleHoldDistance(const FInputActionValue& Value);

	/** Where the player is looking from. False when there is no local player controller yet. */
	bool GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const;

	/** Moves the held part to where the view says it should be, before the query runs. */
	void UpdateHeldPartTransform();

	/** Runs the section 2.5 candidate search for the held part. */
	void UpdateCandidate();

	void DrawDebug() const;

	/** The part being carried. Kinematic while held. */
	UPROPERTY(Transient)
	TObjectPtr<UPolySnapConnectorComponent> HeldPart;

	/** The part's rotation relative to the view when it was grabbed, so pick-up does not jerk it. */
	FQuat HeldRelativeRotation = FQuat::Identity;

	/** Player-applied roll about the view forward axis, the one orientation freedom looking cannot give. */
	double HeldRollDegrees = 0.0;

	/** How far in front of the view the part is carried. */
	double HoldDistanceUu = 250.0;

	/** The best candidate as of the last tick, or unset. What the preview and readout describe. */
	FPolySnapCandidate CurrentCandidate;

	/** Set once the build keys are bound, so a pawn restart does not bind them twice. */
	bool bInputBound = false;

	/** Built at runtime rather than authored as assets -- see the class comment. */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> BuildMappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> GrabAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> DropAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> RollAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> HoldDistanceAction;
};
