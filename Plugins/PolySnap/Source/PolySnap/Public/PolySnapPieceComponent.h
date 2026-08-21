// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PolySnapTypes.h"

#include "PolySnapPieceComponent.generated.h"

class UMeshComponent;

/**
 * Makes an actor a PolySnap piece: parses the edge sockets off its mesh once, caches them, and
 * records what they are connected to.
 *
 * Add it to any actor that already has a mesh component. It finds the first one on the owner
 * unless one is assigned explicitly, so it works on a Blueprint actor with no code behind it.
 */
UCLASS(ClassGroup = "PolySnap", meta = (BlueprintSpawnableComponent), DisplayName = "PolySnap Piece")
class POLYSNAP_API UPolySnapPieceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPolySnapPieceComponent();

	//~ Begin UActorComponent interface
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent interface

#if WITH_EDITOR
	//~ Begin UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject interface
#endif

	/** The mesh whose sockets this piece owns. Null means "the owner's first mesh component". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PolySnap")
	TObjectPtr<UMeshComponent> SocketMesh;

	/**
	 * Tick when this piece's mesh was authored to a different socket axis convention than the
	 * project default. Leave it off and the piece follows UPolySnapSettings::DefaultSocketAxes.
	 */
	UPROPERTY(EditAnywhere, Category = "PolySnap", meta = (InlineEditConditionToggle))
	bool bOverrideSocketAxes = false;

	/** Which socket-local axis carries which role on this piece's mesh. CONVENTIONS.md section 2. */
	UPROPERTY(EditAnywhere, Category = "PolySnap", meta = (EditCondition = "bOverrideSocketAxes"))
	FPolySnapSocketAxes SocketAxes;

	/** The convention in force: this piece's override if it has one, otherwise the project default. */
	UFUNCTION(BlueprintPure, Category = "PolySnap")
	FPolySnapSocketAxes GetEffectiveSocketAxes() const;

	/** The mesh the sockets were actually read from, resolved at BeginPlay. */
	UFUNCTION(BlueprintPure, Category = "PolySnap")
	UMeshComponent* GetResolvedSocketMesh() const { return ResolvedSocketMesh; }

	/** The parsed edge sockets on this piece, in the order the mesh lists them. */
	const TArray<FPolySnapSocketDescriptor>& GetSocketDescriptors() const { return SocketDescriptors; }

	/** Reparses the mesh's sockets. Called at BeginPlay; public so an editor tool can refresh. */
	void RebuildSocketCache();

	/**
	 * Applies UPolySnapSettings' damping and sleep thresholds to this piece's mesh body.
	 *
	 * Lives here rather than on APolySnapPiece because this component is what makes an actor a
	 * piece: a plain Blueprint actor with a mesh and this component is a piece in every other
	 * respect, and it needs the same physics or it drifts away in zero gravity. Called at
	 * BeginPlay, whenever the settings change, and whenever the piece re-enters the simulation.
	 */
	void ApplyPhysicsSettings();

	/**
	 * Hands the piece to the simulation, or takes it out so it can be carried.
	 *
	 * The one place that knows what handing a piece back involves: an anchored APolySnapPiece
	 * refuses, the physics settings are re-applied, and the body starts at rest rather than
	 * carrying the velocity the solver derived while the player walked it into place.
	 */
	void SetSimulating(bool bSimulate);

	/** Every socket on this piece, resolved into world space, appended to OutSockets. */
	void AppendWorldSockets(TArray<FPolySnapWorldSocket>& OutSockets) const;

	/**
	 * The transform of one socket in world space, in the canonical axis convention. Identity when
	 * the socket is not on the mesh.
	 *
	 * The only way to reach a socket transform. Everything downstream -- the query, the debug
	 * draw, the builder's constraint -- assumes the canonical convention, and this is where a
	 * piece's own convention is applied; a caller that goes to the mesh component directly gets
	 * raw axes and silently wrong snapping.
	 */
	[[nodiscard]] FTransform GetSocketWorldTransform(FName SocketName) const;

	/** The piece's own transform -- its owning actor's. What a solved placement is expressed in. */
	[[nodiscard]] FTransform GetPieceTransform() const;

	/** True when this exact socket pair is already connected, in either direction. */
	[[nodiscard]] bool IsConnectedTo(const UPolySnapPieceComponent* Other, int32 OtherSocketId,
		int32 LocalSocketId) const;

	/** True when this socket participates in any connection. Drives the debug colouring. */
	[[nodiscard]] bool IsSocketConnected(int32 LocalSocketId) const;

	/** Records one side of a connection. The caller records the other side on the other piece. */
	void AddConnection(const FPolySnapConnection& Connection);

	const TArray<FPolySnapConnection>& GetConnections() const { return Connections; }

	/** Finds a cached descriptor by socket ID. Null when this piece has no such socket. */
	[[nodiscard]] const FPolySnapSocketDescriptor* FindDescriptorById(int32 SocketId) const;

private:
	/** Parsed once at BeginPlay and never re-parsed. DESIGN section 2.2 is explicit about this. */
	TArray<FPolySnapSocketDescriptor> SocketDescriptors;

	/** One record per participating socket, mirroring what persistence will store. */
	UPROPERTY()
	TArray<FPolySnapConnection> Connections;

	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> ResolvedSocketMesh;

	/** AxisCorrection of the effective convention, resolved in RebuildSocketCache and reused. */
	FQuat SocketAxisCorrection = FQuat::Identity;

	/** Subscription to UPolySnapSettings, so a retune reaches a piece that is already simulating. */
	FDelegateHandle SettingsChangedHandle;
};
