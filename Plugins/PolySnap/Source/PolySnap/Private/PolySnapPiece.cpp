// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapPiece.h"

#include "Components/StaticMeshComponent.h"
#include "PolySnapPieceComponent.h"

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

	// Damping and sleep are UPolySnapPieceComponent's job -- every piece has one, including the
	// Blueprint actors that never inherit from this class -- and its BeginPlay has already run by
	// the time this does. All that is left here is anchoring.
	SetSimulating(!bStartAnchored);
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
