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
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PolySnap.h"
#include "PolySnapDebug.h"
#include "PolySnapGeometry.h"
#include "PolySnapPiece.h"
#include "PolySnapPieceComponent.h"
#include "PolySnapSettings.h"
#include "PolySnapSnapQuery.h"
#include "PolySnapSubsystem.h"

namespace PolySnapBuilderPrivate
{
/** Unreal units are centimetres; the readout speaks millimetres, being a real measured distance. */
constexpr double UuToMm = 10.0;

/**
	 * How far out to gather candidate sockets, as a multiple of the snap distance. Wide enough
	 * that a piece's far socket is still considered, cheap because it only filters whole pieces.
	 */
constexpr double BroadPhaseMultiplier = 40.0;

/** On-screen debug message keys, so each line replaces itself instead of scrolling. */
constexpr uint64 ReadoutKeyHeld = 0x504F4C59;
constexpr uint64 ReadoutKeyCandidate = 0x504F4C5A;
constexpr uint64 ReadoutKeyPlacement = 0x504F4C5B;

void SetPieceSimulating(UPolySnapPieceComponent* Piece, bool bSimulate)
{
	if (Piece == nullptr)
	{
		return;
	}

	// APolySnapPiece knows about anchoring, so prefer its own answer where there is one.
	if (APolySnapPiece* SnapPiece = Cast<APolySnapPiece>(Piece->GetOwner()))
	{
		SnapPiece->SetSimulating(bSimulate);
	}
	// UMeshComponent is already a UPrimitiveComponent, so no cast is involved.
	else if (UMeshComponent* Mesh = Piece->GetResolvedSocketMesh())
	{
		Mesh->SetSimulatePhysics(bSimulate);
	}

	if (!bSimulate)
	{
		return;
	}

	// A carried piece is kinematic and is teleported to the solved transform every frame, so the
	// solver has been deriving a velocity from the player's own movement. Handing the body back to
	// the simulation still carrying it is what sends a just-placed piece wandering off: the player
	// put it there, so it starts at rest and damping has nothing to bleed off.
	UMeshComponent* Mesh = Piece->GetResolvedSocketMesh();
	if (Mesh != nullptr && Mesh->IsSimulatingPhysics())
	{
		Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
}
} // namespace PolySnapBuilderPrivate

UPolySnapBuilderComponent::UPolySnapBuilderComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// After the pawn has moved for the frame, so a carried piece does not lag the view by a frame.
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
	if (HeldPiece != nullptr)
	{
		DropHeldPiece();
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
	if (HeldPiece != nullptr)
	{
		PlaceHeldPiece();
	}
	else
	{
		GrabPieceUnderCrosshair();
	}
}

void UPolySnapBuilderComponent::HandleDrop()
{
	DropHeldPiece();
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

bool UPolySnapBuilderComponent::GrabPieceUnderCrosshair()
{
	if (HeldPiece != nullptr)
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
	UPolySnapPieceComponent* Piece =
		HitActor != nullptr ? HitActor->FindComponentByClass<UPolySnapPieceComponent>() : nullptr;

	if (Piece == nullptr)
	{
		return false;
	}

	if (const APolySnapPiece* SnapPiece = Cast<APolySnapPiece>(HitActor);
		SnapPiece != nullptr && SnapPiece->bStartAnchored)
	{
		UE_LOG(LogPolySnap, Verbose, TEXT("'%s' is anchored and cannot be picked up."), *HitActor->GetName());
		return false;
	}

	HeldPiece = Piece;
	PolySnapBuilderPrivate::SetPieceSimulating(HeldPiece, false);

	// Carry the piece at the orientation it was already in, expressed relative to the view, so
	// picking something up never jerks it round. Roll is then the player's to add.
	HeldRelativeRotation = ViewRotation.Quaternion().Inverse() * Piece->GetPieceTransform().GetRotation();
	HeldRollDegrees = 0.0;
	HoldDistanceUu = FMath::Clamp(FVector::Dist(ViewLocation, Piece->GetPieceTransform().GetLocation()),
		static_cast<double>(Settings.MinHoldDistanceUu), static_cast<double>(Settings.MaxHoldDistanceUu));

	if (UPolySnapSubsystem* Subsystem = UPolySnapSubsystem::Get(this))
	{
		Subsystem->SetHeldPiece(HeldPiece);
	}

	return true;
}

void UPolySnapBuilderComponent::DropHeldPiece()
{
	if (HeldPiece == nullptr)
	{
		return;
	}

	PolySnapBuilderPrivate::SetPieceSimulating(HeldPiece, true);

	if (UPolySnapSubsystem* Subsystem = UPolySnapSubsystem::Get(this))
	{
		Subsystem->SetHeldPiece(nullptr);
	}

	HeldPiece = nullptr;
	CurrentCandidate = FPolySnapCandidate();
}

void UPolySnapBuilderComponent::UpdateHeldPieceTransform()
{
	AActor* PieceActor = HeldPiece != nullptr ? HeldPiece->GetOwner() : nullptr;
	if (PieceActor == nullptr)
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

	// FQuat composes right to left, so this reads as: the piece's grab-time offset, then the
	// current view, then the player's roll.
	const FQuat DesiredRotation = Roll * ViewQuat * HeldRelativeRotation;
	const FVector DesiredLocation = ViewLocation + ViewQuat.GetForwardVector() * HoldDistanceUu;

	PieceActor->SetActorLocationAndRotation(DesiredLocation, DesiredRotation, false, nullptr,
		ETeleportType::TeleportPhysics);
}

void UPolySnapBuilderComponent::UpdateCandidate()
{
	CurrentCandidate = FPolySnapCandidate();

	const UPolySnapSubsystem* Subsystem = UPolySnapSubsystem::Get(this);
	if (HeldPiece == nullptr || Subsystem == nullptr)
	{
		return;
	}

	const FPolySnapQueryTolerances Tolerances = UPolySnapSubsystem::GetQueryTolerances();
	const FTransform HeldPieceTransform = HeldPiece->GetPieceTransform();

	TArray<FPolySnapWorldSocket> HeldSockets;
	HeldPiece->AppendWorldSockets(HeldSockets);

	TArray<FPolySnapWorldSocket> TargetSockets;
	Subsystem->GatherWorldSockets(HeldPiece, HeldPieceTransform.GetLocation(),
		Tolerances.SnapDistanceUu * PolySnapBuilderPrivate::BroadPhaseMultiplier, TargetSockets);

	CurrentCandidate = FPolySnapSnapQuery::FindBest(HeldSockets, TargetSockets, HeldPieceTransform, Tolerances);
}

void UPolySnapBuilderComponent::PlaceHeldPiece()
{
	if (HeldPiece == nullptr)
	{
		return;
	}

	UPolySnapPieceComponent* Piece = HeldPiece;
	AActor* PieceActor = Piece->GetOwner();

	if (!CurrentCandidate.IsSet() || PieceActor == nullptr)
	{
		DropHeldPiece();
		return;
	}

	const FPolySnapCandidate Candidate = CurrentCandidate;

	// The anchor's mating conditions hold to float precision at this instant, and only at this
	// instant -- the next physics tick perturbs everything, the anchor included. Measuring here is
	// what makes the error diagnosable where it is introduced.
	PieceActor->SetActorTransform(Candidate.SolvedPieceTransform, false, nullptr, ETeleportType::TeleportPhysics);

	const FTransform PlacedHeldSocket = Piece->GetSocketWorldTransform(Candidate.HeldSocket.Descriptor.SocketName);
	const FTransform PlacedTargetSocket =
		Candidate.TargetSocket.Piece.IsValid()
			? Candidate.TargetSocket.Piece->GetSocketWorldTransform(Candidate.TargetSocket.Descriptor.SocketName)
			: Candidate.TargetSocket.WorldTransform;

	const double ResidualUu = FVector::Dist(PlacedHeldSocket.GetLocation(), PlacedTargetSocket.GetLocation());

	CommitConnection(Candidate, ResidualUu);
	DropHeldPiece();
}

void UPolySnapBuilderComponent::CommitConnection(const FPolySnapCandidate& Candidate, double ResidualUu)
{
	UPolySnapPieceComponent* HeldPieceComponent = Candidate.HeldSocket.Piece.Get();
	UPolySnapPieceComponent* TargetPieceComponent = Candidate.TargetSocket.Piece.Get();

	if (HeldPieceComponent == nullptr || TargetPieceComponent == nullptr)
	{
		return;
	}

	const float ResidualMm = static_cast<float>(ResidualUu * PolySnapBuilderPrivate::UuToMm);

	// One record per participating socket, which is the shape persistence stores and the shape a
	// joint of degree three will need without a special case.
	FPolySnapConnection HeldSide;
	HeldSide.LocalSocketId = Candidate.HeldSocket.Descriptor.Id;
	HeldSide.OtherPiece = TargetPieceComponent;
	HeldSide.OtherSocketId = Candidate.TargetSocket.Descriptor.Id;
	HeldSide.bWasAnchor = true;
	HeldSide.ResidualMm = ResidualMm;
	HeldPieceComponent->AddConnection(HeldSide);

	FPolySnapConnection TargetSide;
	TargetSide.LocalSocketId = Candidate.TargetSocket.Descriptor.Id;
	TargetSide.OtherPiece = HeldPieceComponent;
	TargetSide.OtherSocketId = Candidate.HeldSocket.Descriptor.Id;
	TargetSide.bWasAnchor = true;
	TargetSide.ResidualMm = ResidualMm;
	TargetPieceComponent->AddConnection(TargetSide);

	UE_LOG(LogPolySnap, Log,
		TEXT("Snapped '%s' socket %03d to '%s' socket %03d: polarity %s, dihedral %.3f deg, residual %.4f mm."),
			*GetNameSafe(HeldPieceComponent->GetOwner()), HeldSide.LocalSocketId,
			*GetNameSafe(TargetPieceComponent->GetOwner()), TargetSide.LocalSocketId,
			Candidate.Polarity == EPolySnapPolarity::Aligned ? TEXT("aligned")
															 : TEXT("flipped"), Candidate.DihedralDegrees, ResidualMm);

	// Simulation is enabled before the constraint is created, because the constraint needs live
	// body instances on both sides to bind to.
	PolySnapBuilderPrivate::SetPieceSimulating(HeldPieceComponent, true);

	UMeshComponent* HeldMesh = HeldPieceComponent->GetResolvedSocketMesh();
	UMeshComponent* TargetMesh = TargetPieceComponent->GetResolvedSocketMesh();
	AActor* TargetActor = TargetPieceComponent->GetOwner();

	if (HeldMesh == nullptr || TargetMesh == nullptr || TargetActor == nullptr)
	{
		return;
	}

	const FTransform JointTransform =
		TargetPieceComponent->GetSocketWorldTransform(Candidate.TargetSocket.Descriptor.SocketName);
	const FPolySnapSocketBasis JointBasis = FPolySnapGeometry::BasisFromTransform(JointTransform);

	UPhysicsConstraintComponent* Constraint = NewObject<UPhysicsConstraintComponent>(TargetActor);
	Constraint->RegisterComponent();
	Constraint->AttachToComponent(TargetMesh, FAttachmentTransformRules::KeepWorldTransform);

	// A physics constraint twists about its own local +X, so pointing that axis along the shared
	// edge is what makes the free degree of freedom the dihedral and nothing else.
	Constraint->SetWorldLocationAndRotation(JointBasis.Location,
		FRotationMatrix::MakeFromX(JointBasis.Tangent).ToQuat());

	Constraint->SetLinearXLimit(LCM_Locked, 0.0f);
	Constraint->SetLinearYLimit(LCM_Locked, 0.0f);
	Constraint->SetLinearZLimit(LCM_Locked, 0.0f);
	Constraint->SetAngularSwing1Limit(ACM_Locked, 0.0f);
	Constraint->SetAngularSwing2Limit(ACM_Locked, 0.0f);

	// The one freedom the joint keeps. Nothing authors an angle: what removes this freedom is the
	// next connection, not a limit set here.
	Constraint->SetAngularTwistLimit(ACM_Free, 0.0f);

	// The two panels meet flush along the seam, so leaving collision on between them is a
	// guaranteed source of jitter for no benefit.
	Constraint->SetDisableCollision(true);
	Constraint->SetConstrainedComponents(TargetMesh, NAME_None, HeldMesh, NAME_None);

	FPolySnapDebug::DrawCommittedJoint(GetWorld(), JointTransform, Candidate.DihedralDegrees);
}

void UPolySnapBuilderComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (HeldPiece != nullptr)
	{
		UpdateHeldPieceTransform();
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

	if (HeldPiece != nullptr)
	{
		FPolySnapDebug::DrawPiece(World, *HeldPiece, true);
	}

	if (CurrentCandidate.IsSet())
	{
		FPolySnapDebug::DrawCandidate(World, CurrentCandidate);

		if (HeldPiece != nullptr)
		{
			FPolySnapDebug::DrawPreview(World, *HeldPiece, CurrentCandidate.SolvedPieceTransform);
		}
	}

	if (!FPolySnapDebug::IsTextEnabled() || GEngine == nullptr)
	{
		return;
	}

	GEngine->AddOnScreenDebugMessage(ReadoutKeyHeld, 0.0f, FColor::White,
		HeldPiece != nullptr ? FString::Printf(TEXT("PolySnap: holding %s  (F place, G drop, Q/E roll, wheel %.0f cm)"),
								   *GetNameSafe(HeldPiece->GetOwner()), HoldDistanceUu)
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
	else if (HeldPiece != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(ReadoutKeyCandidate, 0.0f, FColor::Silver,
			TEXT("  no candidate in tolerance"));
		GEngine->AddOnScreenDebugMessage(ReadoutKeyPlacement, 0.0f, FColor::Silver, TEXT(" "));
	}
#endif
}
