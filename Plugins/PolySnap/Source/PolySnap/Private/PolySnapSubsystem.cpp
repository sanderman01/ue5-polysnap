// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PolySnapConnectorComponent.h"
#include "PolySnapDebug.h"
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

void UPolySnapSubsystem::RegisterPart(UPolySnapConnectorComponent* Part)
{
	if (Part != nullptr)
	{
		RegisteredParts.AddUnique(Part);
	}
}

void UPolySnapSubsystem::UnregisterPart(UPolySnapConnectorComponent* Part)
{
	RegisteredParts.Remove(Part);

	if (HeldPart.Get() == Part)
	{
		HeldPart.Reset();
	}
}

void UPolySnapSubsystem::SetHeldPart(UPolySnapConnectorComponent* Part)
{
	HeldPart = Part;
}

FPolySnapQueryTolerances UPolySnapSubsystem::GetQueryTolerances()
{
	const UPolySnapSettings& Settings = UPolySnapSettings::Get();

	FPolySnapQueryTolerances Tolerances;
	Tolerances.SnapDistanceUu = Settings.SnapDistanceUu;
	Tolerances.TangentAngleToleranceDegrees = Settings.TangentAngleToleranceDegrees;

	return Tolerances;
}

void UPolySnapSubsystem::GatherWorldSockets(const UPolySnapConnectorComponent* ExcludePart, const FVector& Origin,
	double SearchRadiusUu, TArray<FPolySnapWorldSocket>& OutSockets) const
{
	const double RadiusSquared = SearchRadiusUu * SearchRadiusUu;

	for (const TWeakObjectPtr<UPolySnapConnectorComponent>& WeakPart : RegisteredParts)
	{
		const UPolySnapConnectorComponent* Part = WeakPart.Get();
		if (Part == nullptr || Part == ExcludePart)
		{
			continue;
		}

		// Broad phase on the part, so a level full of panels does not cost a socket-pair test
		// each. The real proximity test is per pair and much tighter.
		if (FVector::DistSquared(Part->GetPartTransform().GetLocation(), Origin) > RadiusSquared)
		{
			continue;
		}

		Part->AppendWorldSockets(OutSockets);
	}
}

void UPolySnapSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

#if ENABLE_DRAW_DEBUG
	// Mode 2 draws every registered part. Mode 1 leaves the drawing to the builder component,
	// which knows which part is held and which pair is a candidate.
	if (FPolySnapDebug::GetDrawMode() < 2)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const UPolySnapConnectorComponent* Held = HeldPart.Get();

	for (const TWeakObjectPtr<UPolySnapConnectorComponent>& WeakPart : RegisteredParts)
	{
		if (const UPolySnapConnectorComponent* Part = WeakPart.Get())
		{
			FPolySnapDebug::DrawPart(World, *Part, Part == Held);
		}
	}
#endif
}
