// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapPieceComponent.h"

#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "GameFramework/Actor.h"
#include "PolySnap.h"
#include "PolySnapGeometry.h"
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
}

void UPolySnapPieceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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
