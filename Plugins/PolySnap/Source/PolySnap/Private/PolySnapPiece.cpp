// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapPiece.h"

#include "Components/StaticMeshComponent.h"
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
