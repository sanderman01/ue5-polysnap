// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapSubsystem.h"

#include "Components/MeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PolySnap.h"
#include "PolySnapConnectorComponent.h"
#include "PolySnapDebug.h"
#include "PolySnapGeometry.h"
#include "PolySnapSettings.h"

namespace PolySnapSubsystemPrivate
{
/** Unreal units are centimetres; a residual is a real measured distance, so it is reported in millimetres. */
constexpr double UuToMm = 10.0;

/** The two records one connection leaves, one on each part. Both carry the same residual. */
void RecordConnection(UPolySnapConnectorComponent& PartA, int32 SocketIdA, UPolySnapConnectorComponent& PartB,
	int32 SocketIdB, bool bWasAnchor, double ResidualUu)
{
	const float ResidualMm = static_cast<float>(ResidualUu * UuToMm);

	// One record per participating socket, which is the shape persistence stores and the shape a
	// joint of degree three will need without a special case.
	FPolySnapConnection SideA;
	SideA.LocalSocketId = SocketIdA;
	SideA.OtherPart = &PartB;
	SideA.OtherSocketId = SocketIdB;
	SideA.bWasAnchor = bWasAnchor;
	SideA.ResidualMm = ResidualMm;
	PartA.AddConnection(SideA);

	FPolySnapConnection SideB;
	SideB.LocalSocketId = SocketIdB;
	SideB.OtherPart = &PartA;
	SideB.OtherSocketId = SocketIdA;
	SideB.bWasAnchor = bWasAnchor;
	SideB.ResidualMm = ResidualMm;
	PartB.AddConnection(SideB);
}

/** The socket world transform of one socket by ID, or identity when the part has no such socket. */
[[nodiscard]] FTransform SocketWorldTransformById(const UPolySnapConnectorComponent& Part, int32 SocketId)
{
	if (const FPolySnapSocketDescriptor* Descriptor = Part.FindDescriptorById(SocketId))
	{
		return Part.GetSocketWorldTransform(Descriptor->SocketName);
	}

	return FTransform::Identity;
}
} // namespace PolySnapSubsystemPrivate

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
	Tolerances.AdoptionDistanceUu = Settings.AdoptionDistanceUu;
	Tolerances.AdoptionAngleToleranceDegrees = Settings.AdoptionAngleToleranceDegrees;
	Tolerances.MinDihedralDegrees = Settings.MinDihedralDegrees;

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

void UPolySnapSubsystem::CommitPlacement(const FPolySnapCandidate& Candidate, bool bCreateConstraints)
{
	using namespace PolySnapSubsystemPrivate;

	UPolySnapConnectorComponent* HeldConnector = Candidate.HeldSocket.Part.Get();
	UPolySnapConnectorComponent* TargetConnector = Candidate.TargetSocket.Part.Get();

	if (HeldConnector == nullptr || TargetConnector == nullptr)
	{
		return;
	}

	// Measured now, and only now. The anchor's mating conditions hold to float precision at this
	// instant; the next physics tick perturbs everything, the anchor included. Measuring here is
	// what makes the error diagnosable where it is introduced.
	const int32 AnchorHeldId = Candidate.HeldSocket.Descriptor.Id;
	const int32 AnchorTargetId = Candidate.TargetSocket.Descriptor.Id;

	const double AnchorResidualUu = FVector::Dist(SocketWorldTransformById(*HeldConnector, AnchorHeldId).GetLocation(),
		SocketWorldTransformById(*TargetConnector, AnchorTargetId).GetLocation());

	RecordConnection(*HeldConnector, AnchorHeldId, *TargetConnector, AnchorTargetId, true, AnchorResidualUu);

	UE_LOG(LogPolySnap, Log,
		TEXT("Snapped '%s' socket %03d to '%s' socket %03d: polarity %s, dihedral %.3f deg, residual %.4f mm."),
			*GetNameSafe(HeldConnector->GetOwner()), AnchorHeldId, *GetNameSafe(TargetConnector->GetOwner()),
			AnchorTargetId,
			Candidate.Polarity == EPolySnapPolarity::Aligned
			? TEXT("aligned")
			: TEXT("flipped"), Candidate.DihedralDegrees, AnchorResidualUu * UuToMm);

	// One transform, several connections (DESIGN section 2.5). Every adopted pair is measured
	// where it actually landed rather than where the query predicted, so a solve that disagrees
	// with the placement shows up as a residual instead of hiding in one.
	for (const FPolySnapAdoption& Adoption : Candidate.Adoptions)
	{
		UPolySnapConnectorComponent* AdoptedPart = Adoption.TargetSocket.Part.Get();
		if (AdoptedPart == nullptr)
		{
			continue;
		}

		const int32 AdoptedTargetId = Adoption.TargetSocket.Descriptor.Id;

		const double ResidualUu =
			FVector::Dist(SocketWorldTransformById(*HeldConnector, Adoption.HeldSocketId).GetLocation(),
				SocketWorldTransformById(*AdoptedPart, AdoptedTargetId).GetLocation());

		RecordConnection(*HeldConnector, Adoption.HeldSocketId, *AdoptedPart, AdoptedTargetId, false, ResidualUu);

		UE_LOG(LogPolySnap, Log,
			TEXT("  adopted '%s' socket %03d to '%s' socket %03d: residual %.4f mm."),
				*GetNameSafe(HeldConnector->GetOwner()), Adoption.HeldSocketId, *GetNameSafe(AdoptedPart->GetOwner()),
				AdoptedTargetId, ResidualUu * UuToMm);
	}

	if (!bCreateConstraints)
	{
		return;
	}

	// Simulation is enabled before the constraints are created, because a constraint needs live
	// body instances on both sides to bind to.
	HeldConnector->SetSimulating(true);

	CreateConnectionConstraint(HeldConnector, AnchorHeldId, TargetConnector, AnchorTargetId);

	for (const FPolySnapAdoption& Adoption : Candidate.Adoptions)
	{
		if (UPolySnapConnectorComponent* AdoptedPart = Adoption.TargetSocket.Part.Get())
		{
			CreateConnectionConstraint(HeldConnector, Adoption.HeldSocketId, AdoptedPart,
				Adoption.TargetSocket.Descriptor.Id);
		}
	}
}

UPhysicsConstraintComponent* UPolySnapSubsystem::CreateConnectionConstraint(UPolySnapConnectorComponent* PartA,
	int32 SocketIdA, UPolySnapConnectorComponent* PartB, int32 SocketIdB)
{
	using namespace PolySnapSubsystemPrivate;

	if (PartA == nullptr || PartB == nullptr)
	{
		return nullptr;
	}

	UMeshComponent* MeshA = PartA->GetResolvedSocketMesh();
	UMeshComponent* MeshB = PartB->GetResolvedSocketMesh();
	AActor* ActorB = PartB->GetOwner();

	if (MeshA == nullptr || MeshB == nullptr || ActorB == nullptr)
	{
		return nullptr;
	}

	const FTransform JointTransform = SocketWorldTransformById(*PartB, SocketIdB);
	const FPolySnapSocketBasis JointBasis = FPolySnapGeometry::BasisFromTransform(JointTransform);

	UPhysicsConstraintComponent* Constraint = NewObject<UPhysicsConstraintComponent>(ActorB);
	Constraint->RegisterComponent();
	Constraint->AttachToComponent(MeshB, FAttachmentTransformRules::KeepWorldTransform);

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
	Constraint->SetConstrainedComponents(MeshB, NAME_None, MeshA, NAME_None);

	FPolySnapDebug::DrawCommittedJoint(GetWorld(), JointTransform,
		FPolySnapGeometry::DihedralDegrees(JointBasis,
			FPolySnapGeometry::BasisFromTransform(SocketWorldTransformById(*PartA, SocketIdA))));

	return Constraint;
}

int32 UPolySnapSubsystem::CreateConstraintsForAllConnections()
{
	int32 Created = 0;

	// A connection is recorded on both parts, so walking every part visits each seam twice. Object
	// IDs are a total order over live objects, so building only from the lower-ID side visits each
	// seam exactly once without keeping a set of what has been seen.
	for (const TWeakObjectPtr<UPolySnapConnectorComponent>& WeakPart : RegisteredParts)
	{
		UPolySnapConnectorComponent* Part = WeakPart.Get();
		if (Part == nullptr)
		{
			continue;
		}

		for (const FPolySnapConnection& Connection : Part->GetConnections())
		{
			UPolySnapConnectorComponent* Other = Connection.OtherPart.Get();
			if (Other == nullptr || Other->GetUniqueID() < Part->GetUniqueID())
			{
				continue;
			}

			if (CreateConnectionConstraint(Part, Connection.LocalSocketId, Other, Connection.OtherSocketId) != nullptr)
			{
				++Created;
			}
		}
	}

	return Created;
}

FString UPolySnapSubsystem::BuildAssemblyReport() const
{
	using namespace PolySnapSubsystemPrivate;

	struct FReportedEdge
	{
		FString Description;
		float ResidualMm = 0.0f;

		/** The seam's distance right now, which under simulation is the strain the solver holds. */
		double GapNowMm = 0.0;

		bool bWasAnchor = false;
	};

	TArray<FReportedEdge> Edges;
	int32 PartCount = 0;
	int32 SocketCount = 0;
	int32 ConnectedSocketCount = 0;

	for (const TWeakObjectPtr<UPolySnapConnectorComponent>& WeakPart : RegisteredParts)
	{
		const UPolySnapConnectorComponent* Part = WeakPart.Get();
		if (Part == nullptr)
		{
			continue;
		}

		++PartCount;

		for (const FPolySnapSocketDescriptor& Descriptor : Part->GetSocketDescriptors())
		{
			++SocketCount;
			ConnectedSocketCount += Part->IsSocketConnected(Descriptor.Id) ? 1 : 0;
		}

		// Each seam is recorded on both parts; the lower-ID side reports it, as in
		// CreateConstraintsForAllConnections.
		for (const FPolySnapConnection& Connection : Part->GetConnections())
		{
			const UPolySnapConnectorComponent* Other = Connection.OtherPart.Get();
			if (Other == nullptr || Other->GetUniqueID() < Part->GetUniqueID())
			{
				continue;
			}

			// Measured now, not at placement. The two differ by exactly the strain the constraint
			// solver is holding, which is why DESIGN section 5 wants the panel measurement taken
			// with physics off -- and why the pair of numbers side by side says which is which.
			const double GapNowMm =
				FVector::Dist(SocketWorldTransformById(*Part, Connection.LocalSocketId).GetLocation(),
					SocketWorldTransformById(*Other, Connection.OtherSocketId).GetLocation())
				* UuToMm;

			Edges.Add(FReportedEdge{FString::Printf(TEXT("%s socket %03d -> %s socket %03d"),
														*GetNameSafe(Part->GetOwner()), Connection.LocalSocketId,
														*GetNameSafe(Other->GetOwner()), Connection.OtherSocketId),
				Connection.ResidualMm, GapNowMm, Connection.bWasAnchor});
		}
	}

	int32 AnchoredCount = 0;
	double ResidualSumMm = 0.0;
	double GapSumMm = 0.0;
	double WorstGapNowMm = 0.0;
	for (const FReportedEdge& Edge : Edges)
	{
		AnchoredCount += Edge.bWasAnchor ? 1 : 0;
		ResidualSumMm += Edge.ResidualMm;
		GapSumMm += Edge.GapNowMm;
		WorstGapNowMm = FMath::Max(WorstGapNowMm, Edge.GapNowMm);
	}

	// Worst first: the milestone number is the worst one, and the tail is where a build went wrong.
	Edges.Sort([](const FReportedEdge& A, const FReportedEdge& B) { return A.ResidualMm > B.ResidualMm; });

	FString Report = FString::Printf(TEXT("PolySnap: %d parts, %d connections (%d anchored, %d adopted), %d open sockets of %d"), PartCount, Edges.Num(),
		AnchoredCount, Edges.Num() - AnchoredCount, SocketCount - ConnectedSocketCount, SocketCount);

	if (Edges.IsEmpty())
	{
		return Report;
	}

	Report += FString::Printf(TEXT("\n  residual at placement   worst %.4f mm   mean %.4f mm"), Edges[0].ResidualMm,
		ResidualSumMm / Edges.Num());
	Report += FString::Printf(TEXT("\n  seam gap now            worst %.4f mm   mean %.4f mm"), WorstGapNowMm,
		GapSumMm / Edges.Num());

	const int32 ListedCount = FMath::Min(Edges.Num(), ReportedWorstEdgeCount);
	for (int32 Index = 0; Index < ListedCount; ++Index)
	{
		Report += FString::Printf(TEXT("\n  %s  %s  %.4f mm placed, %.4f mm now"),
			Index == 0 ? TEXT("worst:")
					   : TEXT("      "), *Edges[Index].Description, Edges[Index].ResidualMm, Edges[Index].GapNowMm);
	}

	return Report;
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
