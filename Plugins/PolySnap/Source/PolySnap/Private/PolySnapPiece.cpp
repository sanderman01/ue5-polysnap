// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapPiece.h"

#include "Components/StaticMeshComponent.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PolySnapPieceComponent.h"
#include "PolySnapSettings.h"

APolySnapPiece::APolySnapPiece()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComponent->SetCollisionObjectType(ECC_PhysicsBody);
	MeshComponent->SetSimulatePhysics(true);
	MeshComponent->SetNotifyRigidBodyCollision(true);
	SetRootComponent(MeshComponent);

	PieceComponent = CreateDefaultSubobject<UPolySnapPieceComponent>(TEXT("PolySnapPiece"));
	PieceComponent->SocketMesh = MeshComponent;
}

void APolySnapPiece::BeginPlay()
{
	Super::BeginPlay();

	ApplyPhysicsSettings();
	SetSimulating(!bStartAnchored);

#if WITH_EDITOR
	// Damping is a feel value, and feel values are found by trying them. Re-applying on every
	// settings edit means a value can be changed in Project Settings while PIE runs and felt
	// immediately, instead of costing a restart per attempt.
	SettingsChangedHandle =
		UPolySnapSettings::OnSettingsChanged().AddWeakLambda(this, [this]() { ApplyPhysicsSettings(); });
#endif
}

void APolySnapPiece::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
#if WITH_EDITOR
	UPolySnapSettings::OnSettingsChanged().Remove(SettingsChangedHandle);
	SettingsChangedHandle.Reset();
#endif

	Super::EndPlay(EndPlayReason);
}

void APolySnapPiece::ApplyPhysicsSettings()
{
	if (MeshComponent == nullptr)
	{
		return;
	}

	const UPolySnapSettings& Settings = UPolySnapSettings::Get();
	MeshComponent->SetLinearDamping(Settings.PieceLinearDamping);
	MeshComponent->SetAngularDamping(Settings.PieceAngularDamping);

	FBodyInstance* Body = MeshComponent->GetBodyInstance();
	if (Body == nullptr)
	{
		return;
	}

	// Damping is exponential decay: it approaches zero velocity and never arrives, so a released
	// piece keeps crawling long after it looks stopped. What ends the motion is Chaos deciding the
	// piece's island is asleep, which it does once every body in it has stayed under a linear and
	// an angular velocity threshold for a run of ticks. Custom scales both thresholds by the
	// multiplier below -- higher means a faster-moving piece already counts as at rest, so it
	// sleeps sooner -- and a sleeping island is no longer solved at all, which is what keeps a
	// large structure cheap.
	Body->SleepFamily = Settings.bUsePieceSleepThreshold ? ESleepFamily::Custom : ESleepFamily::Normal;
	Body->CustomSleepThresholdMultiplier = Settings.PieceSleepThresholdMultiplier;

	// FBodyInstance only reads those two when it creates the physics body, so a live piece needs
	// the value pushed onto its particle by hand. A no-op on a body that does not exist yet, which
	// is fine: that body will read the fields set above when it is created.
	FPhysicsCommand::ExecuteWrite(Body->GetPhysicsActor(),
		[Multiplier = Body->GetSleepThresholdMultiplier()](const FPhysicsActorHandle& Actor)
		{ FPhysicsInterface::SetSleepThresholdMultiplier_AssumesLocked(Actor, Multiplier); });
}

void APolySnapPiece::SetSimulating(bool bSimulate)
{
	if (MeshComponent == nullptr)
	{
		return;
	}

	// An anchored piece never simulates, whatever anyone asks for. It is the fixed reference the
	// rest of the structure is built against.
	MeshComponent->SetSimulatePhysics(bSimulate && !bStartAnchored);
}

bool APolySnapPiece::IsSimulating() const
{
	return MeshComponent != nullptr && MeshComponent->IsSimulatingPhysics();
}
