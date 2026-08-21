// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapBuilderComponent.h"

#include "CollisionQueryParams.h"
#include "Components/MeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "PolySnap.h"
#include "PolySnapConnectorComponent.h"
#include "PolySnapDebug.h"
#include "PolySnapSettings.h"
#include "PolySnapSnapQuery.h"
#include "PolySnapSubsystem.h"

namespace PolySnapBuilderPrivate
{
/** Unreal units are centimetres; the readout speaks millimetres, being a real measured distance. */
constexpr double UuToMm = 10.0;

/**
	 * How far out to gather candidate sockets, as a multiple of the snap distance. Wide enough
	 * that a part's far socket is still considered, cheap because it only filters whole parts.
	 */
constexpr double BroadPhaseMultiplier = 40.0;

/** On-screen debug message keys, so each line replaces itself instead of scrolling. */
constexpr uint64 ReadoutKeyHeld = 0x504F4C59;
constexpr uint64 ReadoutKeyCandidate = 0x504F4C5A;
constexpr uint64 ReadoutKeyPlacement = 0x504F4C5B;

}

UPolySnapBuilderComponent::UPolySnapBuilderComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// After the pawn has moved for the frame, so a carried part does not lag the view by a frame.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UPolySnapBuilderComponent::BeginPlay()
{
	Super::BeginPlay();

	HoldDistanceUu = UPolySnapSettings::Get().DefaultHoldDistanceUu;

	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		// A component's BeginPlay can run before the pawn is possessed and its input component
		// exists, so bind now if it is there and subscribe for the restart if it is not.
		Pawn->ReceiveRestartedDelegate.AddDynamic(this, &UPolySnapBuilderComponent::HandlePawnRestarted);
	}

	SetupInput();
}

void UPolySnapBuilderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HeldPart != nullptr)
	{
		DropHeldPart();
	}

	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveRestartedDelegate.RemoveDynamic(this, &UPolySnapBuilderComponent::HandlePawnRestarted);
	}

	Super::EndPlay(EndPlayReason);
}

void UPolySnapBuilderComponent::HandlePawnRestarted(APawn* Pawn)
{
	SetupInput();
}

void UPolySnapBuilderComponent::SetupInput()
{
	if (bInputBound)
	{
		return;
	}

	APawn* Pawn = Cast<APawn>(GetOwner());
	if (Pawn == nullptr)
	{
		UE_LOG(LogPolySnap, Warning,
			TEXT("PolySnap builder on '%s' is not on a pawn, so it has no input to bind to."),
				*GetNameSafe(GetOwner()));
		return;
	}

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(Pawn->InputComponent);
	if (EnhancedInput == nullptr)
	{
		// Not an error: the pawn has not been possessed yet. The restart delegate brings us back.
		return;
	}

	BuildInputMappings();

	EnhancedInput->BindAction(GrabAction, ETriggerEvent::Started, this, &UPolySnapBuilderComponent::HandleGrabOrPlace);
	EnhancedInput->BindAction(DropAction, ETriggerEvent::Started, this, &UPolySnapBuilderComponent::HandleDrop);
	EnhancedInput->BindAction(RollAction, ETriggerEvent::Triggered, this, &UPolySnapBuilderComponent::HandleRoll);
	EnhancedInput->BindAction(HoldDistanceAction, ETriggerEvent::Triggered, this,
		&UPolySnapBuilderComponent::HandleHoldDistance);

	const APlayerController* PlayerController = Cast<APlayerController>(Pawn->GetController());
	if (PlayerController == nullptr)
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
	{
		InputSubsystem->AddMappingContext(BuildMappingContext, InputMappingContextPriority);
		bInputBound = true;

		UE_LOG(LogPolySnap, Log,
			TEXT("PolySnap build keys bound on '%s': F grab/place, G drop, Q/E roll, mouse wheel distance."),
				*GetNameSafe(GetOwner()));
	}
}

void UPolySnapBuilderComponent::BuildInputMappings()
{
	if (IsValid(BuildMappingContext))
	{
		return;
	}

	GrabAction = NewObject<UInputAction>(this, TEXT("IA_PolySnapGrab"));
	GrabAction->ValueType = EInputActionValueType::Boolean;

	DropAction = NewObject<UInputAction>(this, TEXT("IA_PolySnapDrop"));
	DropAction->ValueType = EInputActionValueType::Boolean;

	RollAction = NewObject<UInputAction>(this, TEXT("IA_PolySnapRoll"));
	RollAction->ValueType = EInputActionValueType::Axis1D;

	HoldDistanceAction = NewObject<UInputAction>(this, TEXT("IA_PolySnapHoldDistance"));
	HoldDistanceAction->ValueType = EInputActionValueType::Axis1D;

	BuildMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_PolySnapBuild"));
	BuildMappingContext->MapKey(GrabAction, EKeys::F);
	BuildMappingContext->MapKey(DropAction, EKeys::G);

	BuildMappingContext->MapKey(RollAction, EKeys::E);
	FEnhancedActionKeyMapping& RollLeft = BuildMappingContext->MapKey(RollAction, EKeys::Q);
	RollLeft.Modifiers.Add(NewObject<UInputModifierNegate>(BuildMappingContext));

	// One notch of the wheel is +-1 on this axis, which the handler scales by the step size.
	BuildMappingContext->MapKey(HoldDistanceAction, EKeys::MouseWheelAxis);
}

bool UPolySnapBuilderComponent::GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PlayerController =
		Pawn != nullptr ? Cast<APlayerController>(Pawn->GetController()) : nullptr;

	if (PlayerController == nullptr)
	{
		return false;
	}

	PlayerController->GetPlayerViewPoint(OutLocation, OutRotation);
	return true;
}

void UPolySnapBuilderComponent::HandleGrabOrPlace()
{
	if (HeldPart != nullptr)
	{
		PlaceHeldPart();
	}
	else
	{
		GrabPartUnderCrosshair();
	}
}

void UPolySnapBuilderComponent::HandleDrop()
{
	DropHeldPart();
}

void UPolySnapBuilderComponent::HandleRoll(const FInputActionValue& Value)
{
	// Triggered fires once per frame for as long as the key is held, so this is a rate and has to
	// be scaled by the frame's delta -- otherwise the roll speed follows the framerate.
	const UWorld* World = GetWorld();
	const double DeltaSeconds = World != nullptr ? World->GetDeltaSeconds() : 0.0;

	HeldRollDegrees -= Value.Get<float>() * UPolySnapSettings::Get().RollRateDegreesPerSecond * DeltaSeconds;
}

void UPolySnapBuilderComponent::HandleHoldDistance(const FInputActionValue& Value)
{
	const UPolySnapSettings& Settings = UPolySnapSettings::Get();
	HoldDistanceUu = FMath::Clamp(HoldDistanceUu + Value.Get<float>() * Settings.HoldDistanceStepUu,
		static_cast<double>(Settings.MinHoldDistanceUu), static_cast<double>(Settings.MaxHoldDistanceUu));
}

bool UPolySnapBuilderComponent::GrabPartUnderCrosshair()
{
	if (HeldPart != nullptr)
	{
		return false;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	if (!GetViewPoint(ViewLocation, ViewRotation))
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	const UPolySnapSettings& Settings = UPolySnapSettings::Get();
	const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * Settings.GrabReachUu;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PolySnapGrab), false, GetOwner());
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, ECC_PhysicsBody, QueryParams))
	{
		return false;
	}

	AActor* HitActor = Hit.GetActor();
	UPolySnapConnectorComponent* Part =
		HitActor != nullptr ? HitActor->FindComponentByClass<UPolySnapConnectorComponent>() : nullptr;

	if (Part == nullptr)
	{
		return false;
	}

	if (Part->IsAnchored())
	{
		UE_LOG(LogPolySnap, Verbose, TEXT("'%s' is anchored and cannot be picked up."), *HitActor->GetName());
		return false;
	}

	HeldPart = Part;
	HeldPart->SetSimulating(false);

	// Carry the part at the orientation it was already in, expressed relative to the view, so
	// picking something up never jerks it round. Roll is then the player's to add.
	HeldRelativeRotation = ViewRotation.Quaternion().Inverse() * Part->GetPartTransform().GetRotation();
	HeldRollDegrees = 0.0;
	HoldDistanceUu = FMath::Clamp(FVector::Dist(ViewLocation, Part->GetPartTransform().GetLocation()),
		static_cast<double>(Settings.MinHoldDistanceUu), static_cast<double>(Settings.MaxHoldDistanceUu));

	if (UPolySnapSubsystem* Subsystem = UPolySnapSubsystem::Get(this))
	{
		Subsystem->SetHeldPart(HeldPart);
	}

	return true;
}

void UPolySnapBuilderComponent::DropHeldPart()
{
	if (HeldPart == nullptr)
	{
		return;
	}

	HeldPart->SetSimulating(true);

	if (UPolySnapSubsystem* Subsystem = UPolySnapSubsystem::Get(this))
	{
		Subsystem->SetHeldPart(nullptr);
	}

	HeldPart = nullptr;
	CurrentCandidate = FPolySnapCandidate();
}

void UPolySnapBuilderComponent::UpdateHeldPartTransform()
{
	AActor* PartActor = HeldPart != nullptr ? HeldPart->GetOwner() : nullptr;
	if (PartActor == nullptr)
	{
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	if (!GetViewPoint(ViewLocation, ViewRotation))
	{
		return;
	}

	const FQuat ViewQuat = ViewRotation.Quaternion();

	// Roll about the view's forward axis. Pitch and yaw come from looking, so this is the only
	// orientation freedom the player would otherwise be missing.
	const FQuat Roll(ViewQuat.GetForwardVector(), FMath::DegreesToRadians(HeldRollDegrees));

	// FQuat composes right to left, so this reads as: the part's grab-time offset, then the
	// current view, then the player's roll.
	const FQuat DesiredRotation = Roll * ViewQuat * HeldRelativeRotation;
	const FVector DesiredLocation = ViewLocation + ViewQuat.GetForwardVector() * HoldDistanceUu;

	PartActor->SetActorLocationAndRotation(DesiredLocation, DesiredRotation, false, nullptr,
		ETeleportType::TeleportPhysics);
}

void UPolySnapBuilderComponent::UpdateCandidate()
{
	CurrentCandidate = FPolySnapCandidate();

	const UPolySnapSubsystem* Subsystem = UPolySnapSubsystem::Get(this);
	if (HeldPart == nullptr || Subsystem == nullptr)
	{
		return;
	}

	const FPolySnapQueryTolerances Tolerances = UPolySnapSubsystem::GetQueryTolerances();
	const FTransform HeldPartTransform = HeldPart->GetPartTransform();

	TArray<FPolySnapWorldSocket> HeldSockets;
	HeldPart->AppendWorldSockets(HeldSockets);

	TArray<FPolySnapWorldSocket> TargetSockets;
	Subsystem->GatherWorldSockets(HeldPart, HeldPartTransform.GetLocation(),
		Tolerances.SnapDistanceUu * PolySnapBuilderPrivate::BroadPhaseMultiplier, TargetSockets);

	CurrentCandidate = FPolySnapSnapQuery::FindBest(HeldSockets, TargetSockets, HeldPartTransform, Tolerances);
}

void UPolySnapBuilderComponent::PlaceHeldPart()
{
	if (HeldPart == nullptr)
	{
		return;
	}

	UPolySnapConnectorComponent* Part = HeldPart;
	AActor* PartActor = Part->GetOwner();

	if (!CurrentCandidate.IsSet() || PartActor == nullptr)
	{
		DropHeldPart();
		return;
	}

	const FPolySnapCandidate Candidate = CurrentCandidate;

	// The teleport comes first, and the residuals are measured from where the sockets actually
	// land rather than from where the solve said they would. DESIGN section 2.5's exactness
	// describes this instant and no other: the next physics tick perturbs everything.
	PartActor->SetActorTransform(Candidate.SolvedPartTransform, false, nullptr, ETeleportType::TeleportPhysics);

	if (UPolySnapSubsystem* Subsystem = UPolySnapSubsystem::Get(this))
	{
		Subsystem->CommitPlacement(Candidate, /*bCreateConstraints=*/true);
	}

	DropHeldPart();
}

void UPolySnapBuilderComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (HeldPart != nullptr)
	{
		UpdateHeldPartTransform();
		UpdateCandidate();
	}

	DrawDebug();
}

void UPolySnapBuilderComponent::DrawDebug() const
{
#if ENABLE_DRAW_DEBUG
	using namespace PolySnapBuilderPrivate;

	if (FPolySnapDebug::GetDrawMode() < 1)
	{
		return;
	}

	const UWorld* World = GetWorld();

	if (HeldPart != nullptr)
	{
		FPolySnapDebug::DrawPart(World, *HeldPart, true);
	}

	if (CurrentCandidate.IsSet())
	{
		FPolySnapDebug::DrawCandidate(World, CurrentCandidate);

		if (HeldPart != nullptr)
		{
			FPolySnapDebug::DrawPreview(World, *HeldPart, CurrentCandidate.SolvedPartTransform);
		}
	}

	if (!FPolySnapDebug::IsTextEnabled() || GEngine == nullptr)
	{
		return;
	}

	GEngine->AddOnScreenDebugMessage(ReadoutKeyHeld, 0.0f, FColor::White,
		HeldPart != nullptr ? FString::Printf(TEXT("PolySnap: holding %s  (F place, G drop, Q/E roll, wheel %.0f cm)"),
								  *GetNameSafe(HeldPart->GetOwner()), HoldDistanceUu)
							: FString(TEXT("PolySnap: nothing held  (F to grab)")));

	if (CurrentCandidate.IsSet())
	{
		GEngine->AddOnScreenDebugMessage(ReadoutKeyCandidate, 0.0f, FColor::Yellow,
			FString::Printf(TEXT("  candidate %03d -> %03d   gap %.2f mm   edge angle %.2f deg"),
				CurrentCandidate.HeldSocket.Descriptor.Id, CurrentCandidate.TargetSocket.Descriptor.Id,
				CurrentCandidate.GapUu * UuToMm, CurrentCandidate.TangentAngleDegrees));

		GEngine->AddOnScreenDebugMessage(ReadoutKeyPlacement, 0.0f, FColor::Yellow,
			FString::Printf(TEXT("  %s   dihedral %.2f deg   turn %.2f deg"),
				CurrentCandidate.Polarity == EPolySnapPolarity::Aligned
				? TEXT("aligned")
				: TEXT("flipped"), CurrentCandidate.DihedralDegrees, CurrentCandidate.RequiredRotationDegrees));
	}
	else if (HeldPart != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(ReadoutKeyCandidate, 0.0f, FColor::Silver,
			TEXT("  no candidate in tolerance"));
		GEngine->AddOnScreenDebugMessage(ReadoutKeyPlacement, 0.0f, FColor::Silver, TEXT(" "));
	}
#endif
}
