// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapPieceComponent.h"

#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "GameFramework/Actor.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PolySnap.h"
#include "PolySnapGeometry.h"
#include "PolySnapPiece.h"
#include "PolySnapSettings.h"
#include "PolySnapSocketName.h"
#include "PolySnapSubsystem.h"

UPolySnapPieceComponent::UPolySnapPieceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPolySnapPieceComponent::BeginPlay()
{
	Super::BeginPlay();

	RebuildSocketCache();

	if (const AActor* Owner = GetOwner())
	{
		// DESIGN section 2.2: a placed piece is never scaled. A panel scaled 1.5x has 3000 mm
		// edges while its descriptors still read 2000, so it snaps to things it cannot meet --
		// and no import-time validator can catch it, because the scale lives on the instance.
		ensureMsgf(Owner->GetActorScale3D().Equals(FVector::OneVector),
			TEXT("PolySnap piece '%s' is scaled %s. Pieces must be placed at scale 1; its sockets now "
				 "lie about their edge lengths."),
				*Owner->GetName(), *Owner->GetActorScale3D().ToString());
	}

	if (UPolySnapSubsystem* Subsystem = UPolySnapSubsystem::Get(this))
	{
		Subsystem->RegisterPiece(this);
	}

	ApplyPhysicsSettings();

	// Damping is a feel value, and feel values are found by trying them. Re-applying on every
	// settings change means a value can be changed in Project Settings, or with PolySnap.SetDamping,
	// while PIE runs and felt immediately, instead of costing a restart per attempt.
	SettingsChangedHandle =
		UPolySnapSettings::OnSettingsChanged().AddWeakLambda(this, [this]() { ApplyPhysicsSettings(); });
}

void UPolySnapPieceComponent::ApplyPhysicsSettings()
{
	UMeshComponent* Mesh = ResolvedSocketMesh;
	if (Mesh == nullptr)
	{
		return;
	}

	const UPolySnapSettings& Settings = UPolySnapSettings::Get();
	Mesh->SetLinearDamping(Settings.PieceLinearDamping);
	Mesh->SetAngularDamping(Settings.PieceAngularDamping);

	FBodyInstance* Body = Mesh->GetBodyInstance();
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

void UPolySnapPieceComponent::SetSimulating(bool bSimulate)
{
	UMeshComponent* Mesh = ResolvedSocketMesh;
	if (Mesh == nullptr)
	{
		return;
	}

	// APolySnapPiece knows about anchoring, so prefer its own answer where there is one.
	if (APolySnapPiece* SnapPiece = Cast<APolySnapPiece>(GetOwner()))
	{
		SnapPiece->SetSimulating(bSimulate);
	}
	else
	{
		Mesh->SetSimulatePhysics(bSimulate);
	}

	if (!bSimulate || !Mesh->IsSimulatingPhysics())
	{
		return;
	}

	// The body may have been created after BeginPlay, or created kinematic and only now made
	// dynamic, so the settings are re-applied at the one moment they matter.
	ApplyPhysicsSettings();

	// A carried piece is kinematic and is teleported to the solved transform every frame, so the
	// solver has been deriving a velocity from the player's own movement. Handing the body back to
	// the simulation still carrying it is what sends a just-placed piece wandering off: the player
	// put it there, so it starts at rest and damping has nothing to bleed off.
	Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
}

void UPolySnapPieceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UPolySnapSettings::OnSettingsChanged().Remove(SettingsChangedHandle);
	SettingsChangedHandle.Reset();

	if (UPolySnapSubsystem* Subsystem = UPolySnapSubsystem::Get(this))
	{
		Subsystem->UnregisterPiece(this);
	}

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void UPolySnapPieceComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// The cache is built once at BeginPlay, so without this an axis convention changed while a PIE
	// session is running keeps snapping to the old one -- which reads as the setting not working.
	RebuildSocketCache();
}
#endif

FPolySnapSocketAxes UPolySnapPieceComponent::GetEffectiveSocketAxes() const
{
	return bOverrideSocketAxes ? SocketAxes : UPolySnapSettings::Get().DefaultSocketAxes;
}

void UPolySnapPieceComponent::RebuildSocketCache()
{
	SocketDescriptors.Reset();
	ResolvedSocketMesh = SocketMesh;

	FPolySnapSocketAxes EffectiveAxes = GetEffectiveSocketAxes();
	FString AxesError;
	if (!EffectiveAxes.Validate(&AxesError))
	{
		// Loud, and then carry on with the default. A piece that refused to snap at all would be
		// a worse diagnostic than one that snaps to the convention the rest of the project uses.
		UE_LOG(LogPolySnap, Error,
			TEXT("PolySnap piece on '%s' has an impossible socket axis convention. %s"), *GetNameSafe(GetOwner()),
				*AxesError);

		EffectiveAxes = FPolySnapSocketAxes();
	}

	SocketAxisCorrection = FPolySnapGeometry::AxisCorrection(EffectiveAxes);

	if (ResolvedSocketMesh == nullptr)
	{
		if (const AActor* Owner = GetOwner())
		{
			ResolvedSocketMesh = Owner->FindComponentByClass<UMeshComponent>();
		}
	}

	if (ResolvedSocketMesh == nullptr)
	{
		UE_LOG(LogPolySnap, Warning,
			TEXT("PolySnap piece on '%s' has no mesh component; it owns no sockets."), *GetNameSafe(GetOwner()));
		return;
	}

	TArray<FName> SocketNames = ResolvedSocketMesh->GetAllSocketNames();
	TMap<int32, FName> SeenIds;

	for (const FName& SocketName : SocketNames)
	{
		FPolySnapSocketDescriptor Descriptor;
		FString Error;

		switch (FPolySnapSocketName::Parse(SocketName, Descriptor, &Error))
		{
			case EPolySnapParseResult::NotPolySnap:
				// Somebody else's socket -- an effect anchor, an equipment mount. Passed over in
				// silence, which is the whole basis for two plugins sharing one mesh.
				break;

			case EPolySnapParseResult::Malformed:
				UE_LOG(LogPolySnap, Error, TEXT("%s on '%s'"), *Error, *GetNameSafe(GetOwner()));
				break;

			case EPolySnapParseResult::Parsed:
				// The tail is already stripped by the parser, so a hex and a pent sharing a
				// .blend do not collide here -- only a genuinely duplicated ID does.
				if (const FName* Existing = SeenIds.Find(Descriptor.Id))
				{
					UE_LOG(LogPolySnap, Error,
						TEXT("Duplicate socket ID %03d on '%s': '%s' and '%s'. IDs are permanent identity "
							 "and must be unique within a piece."),
							Descriptor.Id, *GetNameSafe(GetOwner()), *Existing->ToString(), *SocketName.ToString());
				}
				else
				{
					SeenIds.Add(Descriptor.Id, SocketName);
					SocketDescriptors.Add(Descriptor);
				}
				break;
		}
	}

	UE_LOG(LogPolySnap, Verbose,
		TEXT("PolySnap piece '%s' cached %d edge socket(s)."), *GetNameSafe(GetOwner()), SocketDescriptors.Num());
}

FTransform UPolySnapPieceComponent::GetSocketWorldTransform(FName SocketName) const
{
	if (ResolvedSocketMesh == nullptr)
	{
		return FTransform::Identity;
	}

	return FPolySnapGeometry::Canonicalise(ResolvedSocketMesh->GetSocketTransform(SocketName, RTS_World),
		SocketAxisCorrection);
}

FTransform UPolySnapPieceComponent::GetPieceTransform() const
{
	const AActor* Owner = GetOwner();
	return Owner != nullptr ? Owner->GetActorTransform() : FTransform::Identity;
}

void UPolySnapPieceComponent::AppendWorldSockets(TArray<FPolySnapWorldSocket>& OutSockets) const
{
	OutSockets.Reserve(OutSockets.Num() + SocketDescriptors.Num());

	for (const FPolySnapSocketDescriptor& Descriptor : SocketDescriptors)
	{
		FPolySnapWorldSocket& WorldSocket = OutSockets.AddDefaulted_GetRef();
		WorldSocket.Piece = const_cast<UPolySnapPieceComponent*>(this);
		WorldSocket.Descriptor = Descriptor;
		WorldSocket.WorldTransform = GetSocketWorldTransform(Descriptor.SocketName);
		WorldSocket.bConnected = IsSocketConnected(Descriptor.Id);
	}
}

bool UPolySnapPieceComponent::IsConnectedTo(const UPolySnapPieceComponent* Other, int32 OtherSocketId,
	int32 LocalSocketId) const
{
	for (const FPolySnapConnection& Connection : Connections)
	{
		if (Connection.LocalSocketId == LocalSocketId && Connection.OtherSocketId == OtherSocketId
			&& Connection.OtherPiece.Get() == Other)
		{
			return true;
		}
	}

	return false;
}

bool UPolySnapPieceComponent::IsSocketConnected(int32 LocalSocketId) const
{
	for (const FPolySnapConnection& Connection : Connections)
	{
		if (Connection.LocalSocketId == LocalSocketId)
		{
			return true;
		}
	}

	return false;
}

void UPolySnapPieceComponent::AddConnection(const FPolySnapConnection& Connection)
{
	Connections.Add(Connection);
}

const FPolySnapSocketDescriptor* UPolySnapPieceComponent::FindDescriptorById(int32 SocketId) const
{
	return SocketDescriptors.FindByPredicate(
		[SocketId](const FPolySnapSocketDescriptor& Descriptor) { return Descriptor.Id == SocketId; });
}
