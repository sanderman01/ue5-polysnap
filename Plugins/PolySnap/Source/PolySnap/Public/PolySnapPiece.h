// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "PolySnapPiece.generated.h"

class UPolySnapPieceComponent;
class UStaticMeshComponent;

/**
 * A placeable PolySnap piece: a static mesh whose edge sockets connect it to other pieces.
 *
 * Nothing about this class is required -- UPolySnapPieceComponent works on any actor with a mesh.
 * It exists so a piece can be dropped into a level and configured in the details panel with no
 * Blueprint in between, which is what a playtest level wants.
 */
UCLASS(ClassGroup = "PolySnap", DisplayName = "PolySnap Piece")
class POLYSNAP_API APolySnapPiece : public AActor
{
	GENERATED_BODY()

public:
	APolySnapPiece();

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor interface

	/**
	 * Keeps this piece kinematic, so it stays put and gives the player something fixed to build
	 * against. In microgravity nothing else in the level holds still.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PolySnap")
	bool bStartAnchored = false;

	UStaticMeshComponent* GetMeshComponent() const { return MeshComponent; }

	UPolySnapPieceComponent* GetPieceComponent() const { return PieceComponent; }

	/**
	 * Takes the piece out of the simulation so it can be carried, or hands it back.
	 *
	 * A held piece is kinematic on purpose: while it is being placed its transform is the answer
	 * the snap solver produced, and a simulating body would immediately drift off it.
	 */
	void SetSimulating(bool bSimulate);

	/** True while the piece is kinematic because it is anchored or held. */
	[[nodiscard]] bool IsSimulating() const;

private:
	/**
	 * Applies the damping and sleep thresholds from UPolySnapSettings. Without them a nudged panel
	 * drifts away for good, and even a damped one never quite stops.
	 *
	 * Safe to call on a piece that is already simulating: everything it sets is pushed to the live
	 * body, which is what lets the settings be retuned during PIE.
	 */
	void ApplyPhysicsSettings();

#if WITH_EDITOR
	/** Subscription to UPolySnapSettings, so an edit in the settings panel reaches a live piece. */
	FDelegateHandle SettingsChangedHandle;
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PolySnap", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PolySnap", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPolySnapPieceComponent> PieceComponent;
};
