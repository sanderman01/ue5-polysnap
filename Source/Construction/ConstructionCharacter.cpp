// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "ConstructionCharacter.h"

#include "Construction.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

AConstructionCharacter::AConstructionCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AConstructionCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (bDebugFlightEnabled)
	{
		EnterDebugFlight();
	}
}

void AConstructionCharacter::EnterDebugFlight()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!IsValid(Movement))
	{
		UE_LOG(LogConstruction, Warning,
			TEXT("%s has no character movement component; debug flight unavailable."), *GetName());
		return;
	}

	// MOVE_Flying is the engine's own gravity-free mode: PhysFlying integrates velocity without
	// applying gravity and without needing a floor beneath the capsule, so nothing else has to be
	// switched off. DefaultLandMovementMode covers the paths that re-derive the mode later (landing,
	// leaving a volume); SetMovementMode makes it take effect now.
	Movement->DefaultLandMovementMode = MOVE_Flying;
	Movement->SetMovementMode(MOVE_Flying);

	Movement->MaxFlySpeed = FlySpeed;
	Movement->MaxAcceleration = FlyAcceleration;

	// BrakingDecelerationFlying defaults to 0, which leaves the character coasting indefinitely once
	// input stops -- unusable for placing anything. This is the setting that makes flight precise.
	Movement->BrakingDecelerationFlying = FlyBrakingDeceleration;

	// Brake on a value of our own rather than GroundFriction, which is meaningless in the air.
	Movement->bUseSeparateBrakingFriction = true;
	Movement->BrakingFriction = 4.0f;

	UE_LOG(LogConstruction, Log,
		TEXT("Debug flight enabled on %s (%.0f cm/s, boost %.0f cm/s)."), *GetName(), FlySpeed, BoostedFlySpeed);
}

void AConstructionCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (!bDebugFlightEnabled)
	{
		return;
	}

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogConstruction, Warning,
			TEXT("%s expected an enhanced input component; debug flight keys will not be bound."), *GetName());
		return;
	}

	BuildFlightInputMappings();

	EnhancedInput->BindAction(FlyVerticalAction, ETriggerEvent::Triggered, this,
		&AConstructionCharacter::HandleFlyVertical);
	EnhancedInput->BindAction(FlyBoostAction, ETriggerEvent::Started, this,
		&AConstructionCharacter::HandleBoostStarted);
	EnhancedInput->BindAction(FlyBoostAction, ETriggerEvent::Completed, this,
		&AConstructionCharacter::HandleBoostCompleted);

	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController)
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
	{
		InputSubsystem->AddMappingContext(FlightMappingContext, FlightMappingContextPriority);
	}
}

void AConstructionCharacter::BuildFlightInputMappings()
{
	if (IsValid(FlightMappingContext))
	{
		return;
	}

	FlyVerticalAction = NewObject<UInputAction>(this, TEXT("IA_DebugFlyVertical"));
	FlyVerticalAction->ValueType = EInputActionValueType::Axis1D;

	FlyBoostAction = NewObject<UInputAction>(this, TEXT("IA_DebugFlyBoost"));
	FlyBoostAction->ValueType = EInputActionValueType::Boolean;

	FlightMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_DebugFlight"));
	FlightMappingContext->MapKey(FlyVerticalAction, EKeys::SpaceBar);

	// Down is the same axis inverted, so a single handler covers both directions.
	FEnhancedActionKeyMapping& DownMapping = FlightMappingContext->MapKey(FlyVerticalAction, EKeys::LeftControl);
	DownMapping.Modifiers.Add(NewObject<UInputModifierNegate>(FlightMappingContext));

	FlightMappingContext->MapKey(FlyBoostAction, EKeys::LeftShift);
}

void AConstructionCharacter::HandleFlyVertical(const FInputActionValue& Value)
{
	// World up, not actor up: vertical stays vertical however far the camera is pitched, which is
	// what makes it usable for lining panels up.
	AddMovementInput(FVector::UpVector, Value.Get<float>());
}

void AConstructionCharacter::HandleBoostStarted()
{
	ApplyFlySpeed(BoostedFlySpeed);
}

void AConstructionCharacter::HandleBoostCompleted()
{
	ApplyFlySpeed(FlySpeed);
}

void AConstructionCharacter::ApplyFlySpeed(const float NewFlySpeed)
{
	if (UCharacterMovementComponent* Movement = GetCharacterMovement(); IsValid(Movement))
	{
		Movement->MaxFlySpeed = NewFlySpeed;
	}
}
