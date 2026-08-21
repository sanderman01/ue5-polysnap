// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapDebug.h"

#include "Components/MeshComponent.h"
#include "Containers/Ticker.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PolySnap.h"
#include "PolySnapConnectorComponent.h"
#include "PolySnapGeometry.h"
#include "PolySnapSettings.h"
#include "PolySnapSubsystem.h"

namespace PolySnapDebugPrivate
{
static TAutoConsoleVariable<int32> CVarDrawMode(TEXT("PolySnap.Debug.Draw"), 1,
	TEXT("PolySnap socket visualisation. 0 off, 1 held part and candidates, 2 every registered part."), ECVF_Cheat);

static TAutoConsoleVariable<int32> CVarText(TEXT("PolySnap.Debug.Text"), 1,
	TEXT("PolySnap on-screen readout: held part, candidate, gap, angle, polarity, dihedral."), ECVF_Cheat);

static TAutoConsoleVariable<float> CVarMaxDistance(TEXT("PolySnap.Debug.MaxDistance"), 3000.0f,
	TEXT("Beyond this distance from the viewer, PolySnap draws nothing. Unreal units."), ECVF_Cheat);

static TAutoConsoleVariable<float> CVarGizmoScale(TEXT("PolySnap.Debug.GizmoScale"), 1.0f,
	TEXT("Size multiplier for PolySnap socket gizmos."), ECVF_Cheat);

/** Base gizmo arrow length in Unreal units, before the scale multiplier. */
constexpr double BaseArrowLength = 18.0;

/** Base length of the committed-joint hinge line, before the scale multiplier. */
constexpr double BaseJointLength = 60.0;

/** Unreal units are centimetres and the debug readout is in millimetres, being a real distance. */
constexpr double UuToMm = 10.0;

const FColor OutwardColour = FColor::Red;
const FColor TangentColour = FColor::Green;
const FColor NormalColour = FColor::Blue;
const FColor FreeSocketColour = FColor(160, 160, 160);
const FColor ConnectedSocketColour = FColor(60, 220, 90);
const FColor HighlightColour = FColor::Yellow;
const FColor JointColour = FColor(220, 60, 220);

[[nodiscard]] FVector GetViewerLocation(const UWorld* World)
{
	if (World == nullptr)
	{
		return FVector::ZeroVector;
	}

	if (const APlayerController* Controller = World->GetFirstPlayerController())
	{
		FVector ViewLocation = FVector::ZeroVector;
		FRotator ViewRotation = FRotator::ZeroRotator;
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);

		return ViewLocation;
	}

	return FVector::ZeroVector;
}

[[nodiscard]] const TCHAR* DescribeObjectState(Chaos::EObjectStateType State)
{
	switch (State)
	{
		case Chaos::EObjectStateType::Sleeping:
			return TEXT("asleep");
		case Chaos::EObjectStateType::Dynamic:
			return TEXT("awake");
		case Chaos::EObjectStateType::Kinematic:
			return TEXT("kinematic");
		case Chaos::EObjectStateType::Static:
			return TEXT("static");
		default:
			return TEXT("uninitialised");
	}
}

/**
 * Reports what each part's body actually has, as opposed to what the settings asked for.
 *
 * The two are not the same thing, and the gap between them is where a part that will not settle
 * hides: a value can be written to a body instance and never reach the particle the solver reads.
 * This prints both sides, plus the state and speed that say whether the part is still being
 * driven by something.
 */
void DumpPhysicsOnce(UWorld* World);

/**
 * PolySnap.DumpPhysics [seconds] -- one dump, or one every half second for that long.
 *
 * The timed form is the one that answers "does a nudged part settle": a single sample cannot
 * tell a part that is slowing down from one that is not.
 */
void DumpPhysics(const TArray<FString>& Args, UWorld* World)
{
	DumpPhysicsOnce(World);

	const double WatchSeconds = Args.Num() > 0 ? FCString::Atod(*Args[0]) : 0.0;
	if (WatchSeconds <= 0.0)
	{
		return;
	}

	// A ticker rather than a world timer, so the sampling interval is wall clock and a dump still
	// lands if the world is paused -- a paused part that looks stopped is exactly the confusion
	// this is meant to resolve.
	constexpr float SampleInterval = 0.5f;
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda(
			[WeakWorld = TWeakObjectPtr<UWorld>(World), Deadline = FPlatformTime::Seconds() + WatchSeconds](float)
			{
				UWorld* TickWorld = WeakWorld.Get();
				if (TickWorld == nullptr)
				{
					return false;
				}

				DumpPhysicsOnce(TickWorld);

				return FPlatformTime::Seconds() < Deadline;
			}),
		SampleInterval);
}

void DumpPhysicsOnce(UWorld* World)
{
	const UPolySnapSubsystem* Subsystem = World != nullptr ? World->GetSubsystem<UPolySnapSubsystem>() : nullptr;
	if (Subsystem == nullptr)
	{
		UE_LOG(LogPolySnap, Warning, TEXT("PolySnap.DumpPhysics: no PolySnap subsystem in this world."));
		return;
	}

	const UPolySnapSettings& Settings = UPolySnapSettings::Get();
	UE_LOG(LogPolySnap, Display,
		TEXT("PolySnap physics dump. Settings ask for damping %.2f linear, %.2f angular; sleep %s x%.2f."),
			Settings.PartLinearDamping, Settings.PartAngularDamping,
			Settings.bUsePartSleepThreshold ? TEXT("custom") : TEXT("normal"), Settings.PartSleepThresholdMultiplier);

	int32 PartCount = 0;

	for (const TWeakObjectPtr<UPolySnapConnectorComponent>& WeakPart : Subsystem->GetRegisteredParts())
	{
		const UPolySnapConnectorComponent* Part = WeakPart.Get();
		if (Part == nullptr)
		{
			continue;
		}

		++PartCount;

		const AActor* Owner = Part->GetOwner();
		UMeshComponent* Mesh = Part->GetResolvedSocketMesh();
		if (Mesh == nullptr)
		{
			UE_LOG(LogPolySnap, Warning, TEXT("  %s: no resolved socket mesh."), *GetNameSafe(Owner));
			continue;
		}

		const FBodyInstance* Body = Mesh->GetBodyInstance();
		FPhysicsActorHandle Handle = Body != nullptr ? Body->GetPhysicsActor() : nullptr;
		if (Handle == nullptr)
		{
			UE_LOG(LogPolySnap, Warning, TEXT("  %s: no physics body."), *GetNameSafe(Owner));
			continue;
		}

		// What the solver will actually use this tick, read straight off the particle rather than
		// from the body instance that was asked to set it.
		const Chaos::FRigidBodyHandle_External& Particle = Handle->GetGameThreadAPI();
		const double LinearSpeed = Mesh->GetPhysicsLinearVelocity().Size();
		const double AngularSpeed = Mesh->GetPhysicsAngularVelocityInDegrees().Size();

		UE_LOG(LogPolySnap, Display,
			TEXT("  %-38s %-11s v %7.2f cm/s  w %7.2f deg/s  drag %5.2f/%5.2f (asked %5.2f/%5.2f)  ")
				TEXT("sleep x%.2f  connections %d  class %s"), *GetNameSafe(Owner),
					DescribeObjectState(Particle.ObjectState()), LinearSpeed, AngularSpeed, Particle.LinearEtherDrag(),
					Particle.AngularEtherDrag(), Settings.PartLinearDamping, Settings.PartAngularDamping,
					Body->GetSleepThresholdMultiplier(), Part->GetConnections().Num(),
					*GetNameSafe(Owner != nullptr ? Owner->GetClass() : nullptr));
	}

	UE_LOG(LogPolySnap, Display, TEXT("PolySnap physics dump: %d part(s)."), PartCount);
}

/** PolySnap.SetDamping <linear> <angular> [sleepMultiplier] -- retune from inside PIE. */
void SetDamping(const TArray<FString>& Args)
{
	if (Args.Num() < 2)
	{
		UE_LOG(LogPolySnap, Warning, TEXT("Usage: PolySnap.SetDamping <linear> <angular> [sleepMultiplier]"));
		return;
	}

	UPolySnapSettings* Settings = GetMutableDefault<UPolySnapSettings>();
	check(Settings != nullptr);

	Settings->PartLinearDamping = FCString::Atof(*Args[0]);
	Settings->PartAngularDamping = FCString::Atof(*Args[1]);

	if (Args.Num() >= 3)
	{
		Settings->PartSleepThresholdMultiplier = FCString::Atof(*Args[2]);
		Settings->bUsePartSleepThreshold = Settings->PartSleepThresholdMultiplier > 0.0f;
	}

	// The same broadcast the settings panel makes, so every live part re-applies. Nothing is
	// written to the ini: a value found this way is meant to be typed into Project Settings once
	// it is the one you want to keep.
	UPolySnapSettings::OnSettingsChanged().Broadcast();

	UE_LOG(LogPolySnap, Display,
		TEXT("PolySnap damping now %.2f linear, %.2f angular; sleep %s x%.2f."), Settings->PartLinearDamping,
			Settings->PartAngularDamping,
			Settings->bUsePartSleepThreshold ? TEXT("custom") : TEXT("normal"), Settings->PartSleepThresholdMultiplier);
}

/**
 * PolySnap.Nudge [speed] -- hands every part to the simulation and shoves it.
 *
 * The behaviour this project cares about most is what a part does after the player lets go, and
 * that is otherwise only reachable by playing. This reproduces it in one command, so a settling
 * time can be measured in a headless session instead of estimated by eye.
 */
void Nudge(const TArray<FString>& Args, UWorld* World)
{
	UPolySnapSubsystem* Subsystem = World != nullptr ? World->GetSubsystem<UPolySnapSubsystem>() : nullptr;
	if (Subsystem == nullptr)
	{
		UE_LOG(LogPolySnap, Warning, TEXT("PolySnap.Nudge: no PolySnap subsystem in this world."));
		return;
	}

	const double Speed = Args.Num() > 0 ? FCString::Atod(*Args[0]) : 200.0;
	int32 NudgedCount = 0;

	for (const TWeakObjectPtr<UPolySnapConnectorComponent>& WeakPart : Subsystem->GetRegisteredParts())
	{
		UPolySnapConnectorComponent* Part = WeakPart.Get();
		if (Part == nullptr)
		{
			continue;
		}

		Part->SetSimulating(true);

		UMeshComponent* Mesh = Part->GetResolvedSocketMesh();
		if (Mesh == nullptr || !Mesh->IsSimulatingPhysics())
		{
			continue;
		}

		// Deterministic rather than random: two runs of the same test should shove the same part
		// the same way, or a settling time cannot be compared against the last one.
		const double Phase = NudgedCount * 1.7;
		Mesh->SetPhysicsLinearVelocity(FVector(FMath::Cos(Phase), FMath::Sin(Phase), 0.35) * Speed);
		Mesh->SetPhysicsAngularVelocityInDegrees(FVector(0.4, FMath::Sin(Phase), FMath::Cos(Phase)) * Speed);
		++NudgedCount;
	}

	UE_LOG(LogPolySnap, Display, TEXT("PolySnap.Nudge: shoved %d part(s) at %.0f cm/s."), NudgedCount, Speed);
}

static FAutoConsoleCommandWithWorldAndArgs CmdDumpPhysics(TEXT("PolySnap.DumpPhysics"),
	TEXT("PolySnap.DumpPhysics [seconds]. Logs what each part's body actually has: state, speed, ")
		TEXT("damping, sleep multiplier. With a duration, samples every half second so a part can be ")
			TEXT("watched settling."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DumpPhysics));

static FAutoConsoleCommandWithWorldAndArgs CmdNudge(TEXT("PolySnap.Nudge"),
	TEXT("PolySnap.Nudge [speed]. Hands every part to the simulation and shoves it, to watch it settle."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Nudge));

static FAutoConsoleCommand CmdSetDamping(TEXT("PolySnap.SetDamping"),
	TEXT("PolySnap.SetDamping <linear> <angular> [sleepMultiplier]. Applies to every live part at once."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&SetDamping));
} // namespace PolySnapDebugPrivate

int32 FPolySnapDebug::GetDrawMode()
{
	return PolySnapDebugPrivate::CVarDrawMode.GetValueOnAnyThread();
}

bool FPolySnapDebug::IsTextEnabled()
{
	return PolySnapDebugPrivate::CVarText.GetValueOnAnyThread() != 0;
}

double FPolySnapDebug::GetMaxDrawDistance()
{
	return PolySnapDebugPrivate::CVarMaxDistance.GetValueOnAnyThread();
}

double FPolySnapDebug::GetGizmoScale()
{
	return PolySnapDebugPrivate::CVarGizmoScale.GetValueOnAnyThread();
}

bool FPolySnapDebug::IsInDrawRange(const UWorld* World, const FVector& Location)
{
	const double MaxDistance = GetMaxDrawDistance();
	return FVector::DistSquared(PolySnapDebugPrivate::GetViewerLocation(World), Location) <= MaxDistance * MaxDistance;
}

void FPolySnapDebug::DrawSocket(const UWorld* World, const FTransform& SocketTransform,
	const FPolySnapSocketDescriptor& Descriptor, bool bConnected, bool bHighlighted)
{
#if ENABLE_DRAW_DEBUG
	using namespace PolySnapDebugPrivate;

	if (World == nullptr || !IsInDrawRange(World, SocketTransform.GetLocation()))
	{
		return;
	}

	const FPolySnapSocketBasis Basis = FPolySnapGeometry::BasisFromTransform(SocketTransform);
	const double ArrowLength = BaseArrowLength * GetGizmoScale();
	const double ArrowHead = ArrowLength * 0.25;

	// The three named directions, drawn in the level so an axis-role mistake in Blender is
	// visible rather than inferred. These are the resolved roles rather than raw mesh axes --
	// GetSocketWorldTransform has already applied the part's convention -- so red is Outward
	// whichever local axis that part's assets put it on, and a wrong convention shows up here
	// as arrows pointing somewhere the panel's geometry says they should not. The arrow colours
	// are never overridden -- the held part is signalled by the socket sphere below, so the
	// axis legend still reads on the one part whose convention you are most likely checking.
	DrawDebugDirectionalArrow(World, Basis.Location, Basis.Location + Basis.Outward * ArrowLength, ArrowHead,
		OutwardColour, false, -1.0f, 0, 0.6f);
	DrawDebugDirectionalArrow(World, Basis.Location, Basis.Location + Basis.Tangent * ArrowLength, ArrowHead,
		TangentColour, false, -1.0f, 0, 0.6f);
	DrawDebugDirectionalArrow(World, Basis.Location, Basis.Location + Basis.Normal * ArrowLength, ArrowHead,
		NormalColour, false, -1.0f, 0, 0.6f);

	const FColor SocketColour =
		bHighlighted ? HighlightColour : (bConnected ? ConnectedSocketColour : FreeSocketColour);
	DrawDebugSphere(World, Basis.Location, ArrowLength * 0.12, 8, SocketColour, false, -1.0f, 0, 0.5f);

	if (IsTextEnabled())
	{
		DrawDebugString(World, Basis.Location + Basis.Normal * (ArrowLength * 1.2),
			FString::Printf(TEXT("%03d"), Descriptor.Id), nullptr, SocketColour, 0.0f, true, 1.0f);
	}
#endif
}

void FPolySnapDebug::DrawPart(const UWorld* World, const UPolySnapConnectorComponent& Part, bool bHighlighted)
{
#if ENABLE_DRAW_DEBUG
	for (const FPolySnapSocketDescriptor& Descriptor : Part.GetSocketDescriptors())
	{
		DrawSocket(World, Part.GetSocketWorldTransform(Descriptor.SocketName), Descriptor,
			Part.IsSocketConnected(Descriptor.Id), bHighlighted);
	}
#endif
}

void FPolySnapDebug::DrawCandidate(const UWorld* World, const FPolySnapCandidate& Candidate)
{
#if ENABLE_DRAW_DEBUG
	using namespace PolySnapDebugPrivate;

	if (World == nullptr || !Candidate.IsSet())
	{
		return;
	}

	const FVector HeldLocation = Candidate.HeldSocket.WorldTransform.GetLocation();
	const FVector TargetLocation = Candidate.TargetSocket.WorldTransform.GetLocation();

	if (!IsInDrawRange(World, TargetLocation))
	{
		return;
	}

	DrawSocket(World, Candidate.HeldSocket.WorldTransform, Candidate.HeldSocket.Descriptor, false, true);
	DrawSocket(World, Candidate.TargetSocket.WorldTransform, Candidate.TargetSocket.Descriptor, false, true);
	DrawDebugLine(World, HeldLocation, TargetLocation, HighlightColour, false, -1.0f, 0, 1.0f);

	if (IsTextEnabled())
	{
		DrawDebugString(World, (HeldLocation + TargetLocation) * 0.5,
			FString::Printf(TEXT("%.1f mm  %.1f deg"), Candidate.GapUu * UuToMm, Candidate.TangentAngleDegrees),
				nullptr, HighlightColour, 0.0f, true, 1.0f);
	}
#endif
}

void FPolySnapDebug::DrawPreview(const UWorld* World, const UPolySnapConnectorComponent& HeldPart,
	const FTransform& SolvedTransform)
{
#if ENABLE_DRAW_DEBUG
	using namespace PolySnapDebugPrivate;

	const UMeshComponent* Mesh = HeldPart.GetResolvedSocketMesh();
	if (World == nullptr || Mesh == nullptr)
	{
		return;
	}

	// Local bounds, re-expressed at the solved transform: where the part lands if placed now.
	const FBoxSphereBounds LocalBounds = Mesh->CalcBounds(FTransform::Identity);
	DrawDebugBox(World, SolvedTransform.TransformPosition(LocalBounds.Origin), LocalBounds.BoxExtent,
		SolvedTransform.GetRotation(), HighlightColour, false, -1.0f, 0, 0.8f);
#endif
}

void FPolySnapDebug::DrawCommittedJoint(const UWorld* World, const FTransform& JointTransform, double DihedralDegrees)
{
#if ENABLE_DRAW_DEBUG
	using namespace PolySnapDebugPrivate;

	if (World == nullptr)
	{
		return;
	}

	const FPolySnapSocketBasis Basis = FPolySnapGeometry::BasisFromTransform(JointTransform);
	const FVector HalfEdge = Basis.Tangent * (BaseJointLength * GetGizmoScale() * 0.5);

	// The shared edge line: the axis the joint is free to hinge about.
	DrawDebugLine(World, Basis.Location - HalfEdge, Basis.Location + HalfEdge, JointColour, false, 5.0f, 0, 1.5f);

	if (IsTextEnabled())
	{
		DrawDebugString(World, Basis.Location,
			FString::Printf(TEXT("%.2f deg"), DihedralDegrees), nullptr, JointColour, 5.0f, true, 1.0f);
	}
#endif
}
