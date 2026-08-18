// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PolySnapDebug.h"
#include "PolySnapPieceComponent.h"
#include "PolySnapSettings.h"

UPolySnapSubsystem* UPolySnapSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine != nullptr
							? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
							: nullptr;

	return World != nullptr ? World->GetSubsystem<UPolySnapSubsystem>() : nullptr;
}

TStatId UPolySnapSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPolySnapSubsystem, STATGROUP_Tickables);
}

void UPolySnapSubsystem::RegisterPiece(UPolySnapPieceComponent* Piece)
{
	if (Piece != nullptr)
	{
		RegisteredPieces.AddUnique(Piece);
	}
}

void UPolySnapSubsystem::UnregisterPiece(UPolySnapPieceComponent* Piece)
{
	RegisteredPieces.Remove(Piece);

	if (HeldPiece.Get() == Piece)
	{
		HeldPiece.Reset();
	}
}

void UPolySnapSubsystem::SetHeldPiece(UPolySnapPieceComponent* Piece)
{
	HeldPiece = Piece;
}

FPolySnapQueryTolerances UPolySnapSubsystem::GetQueryTolerances()
{
	const UPolySnapSettings& Settings = UPolySnapSettings::Get();

	FPolySnapQueryTolerances Tolerances;
	Tolerances.SnapDistanceUu = Settings.SnapDistanceUu;
	Tolerances.TangentAngleToleranceDegrees = Settings.TangentAngleToleranceDegrees;

	return Tolerances;
}

void UPolySnapSubsystem::GatherWorldSockets(const UPolySnapPieceComponent* ExcludePiece, const FVector& Origin,
	double SearchRadiusUu, TArray<FPolySnapWorldSocket>& OutSockets) const
{
	const double RadiusSquared = SearchRadiusUu * SearchRadiusUu;

	for (const TWeakObjectPtr<UPolySnapPieceComponent>& WeakPiece : RegisteredPieces)
	{
		const UPolySnapPieceComponent* Piece = WeakPiece.Get();
		if (Piece == nullptr || Piece == ExcludePiece)
		{
			continue;
		}

		// Broad phase on the piece, so a level full of panels does not cost a socket-pair test
		// each. The real proximity test is per pair and much tighter.
		if (FVector::DistSquared(Piece->GetPieceTransform().GetLocation(), Origin) > RadiusSquared)
		{
			continue;
		}

		Piece->AppendWorldSockets(OutSockets);
	}
}

void UPolySnapSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

#if ENABLE_DRAW_DEBUG
	// Mode 2 draws every registered piece. Mode 1 leaves the drawing to the builder component,
	// which knows which piece is held and which pair is a candidate.
	if (FPolySnapDebug::GetDrawMode() < 2)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const UPolySnapPieceComponent* Held = HeldPiece.Get();

	for (const TWeakObjectPtr<UPolySnapPieceComponent>& WeakPiece : RegisteredPieces)
	{
		if (const UPolySnapPieceComponent* Piece = WeakPiece.Get())
		{
			FPolySnapDebug::DrawPiece(World, *Piece, Piece == Held);
		}
	}
#endif
}
