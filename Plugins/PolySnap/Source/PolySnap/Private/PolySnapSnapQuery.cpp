// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapSnapQuery.h"

#include "PolySnapConnectorComponent.h"
#include "PolySnapGeometry.h"

namespace PolySnapSnapQueryPrivate
{
/** Every polarity a placement may take. Both are always admissible -- DESIGN section 2.3. */
constexpr EPolySnapPolarity Polarities[] = {EPolySnapPolarity::Aligned, EPolySnapPolarity::Flipped};

/** True when the two world sockets are the same socket of the same part. */
[[nodiscard]] bool IsSameSocket(const FPolySnapWorldSocket& A, const FPolySnapWorldSocket& B)
{
	return A.Part == B.Part && A.Descriptor.Id == B.Descriptor.Id;
}

/**
 * Whether a pair may be considered for adoption at all, before any distance is measured.
 *
 * Deliberately stricter than TestPair on one point: the target socket must be unconnected. See
 * FindAdoptions for why a joint of degree three is Milestone 3's business rather than something
 * to fall into by accident.
 */
[[nodiscard]] bool IsAdoptable(const FPolySnapWorldSocket& HeldSocket, const FPolySnapWorldSocket& TargetSocket,
	const FPolySnapCandidate& Anchor)
{
	if (HeldSocket.Part.IsValid() && HeldSocket.Part == TargetSocket.Part)
	{
		return false;
	}

	if (TargetSocket.bConnected || HeldSocket.bConnected)
	{
		return false;
	}

	if (IsSameSocket(HeldSocket, Anchor.HeldSocket) || IsSameSocket(TargetSocket, Anchor.TargetSocket))
	{
		return false;
	}

	return HeldSocket.Descriptor.IsCompatibleWith(TargetSocket.Descriptor);
}

/** How far the held part must still turn to reach a given placement, in degrees. */
[[nodiscard]] double RotationToPlacementDegrees(const FTransform& HeldSocketLocal,
	const FTransform& CurrentPartTransform, const FPolySnapSocketBasis& Anchor, EPolySnapPolarity Polarity,
	double DihedralDegrees)
{
	const FQuat CurrentSocketRotation = CurrentPartTransform.GetRotation() * HeldSocketLocal.GetRotation();
	const FQuat SolvedRotation =
		FPolySnapGeometry::TransformFromBasis(FPolySnapGeometry::MatedBasis(Anchor, Polarity, DihedralDegrees))
			.GetRotation();

	return FPolySnapGeometry::AngleBetweenDegrees(SolvedRotation, CurrentSocketRotation);
}

/** Below this, two placements are equally far from where the part is held and the residual decides. */
constexpr double RotationTieDegrees = 1.0e-3;

/**
 * DESIGN section 2.5 step 1: the theta and polarity that close some second socket pair.
 *
 * Searched over every secondary pair and both polarities. Where more than one closes, the one
 * nearest the pose the part is already in wins, and only a tie there is settled by the residual.
 *
 * That ranking is not a detail. Folding a panel flat onto a panel already placed closes every one
 * of its sockets at a residual of exactly zero, so ranking by residual alone makes the degenerate
 * answer beat the intended one every time, and a shell assembled that way stacks its panels
 * instead of closing. Any closure inside AdoptionDistanceUu is geometrically acceptable, so the
 * question left is which of them the builder was aiming at -- and that is the nearest one.
 *
 * @return False when no secondary pair comes within AdoptionDistanceUu, which is the caller's cue
 *         to fall back to the player-driven reading.
 */
[[nodiscard]] bool SolveAdoptDrivenPlacement(const TArray<FPolySnapWorldSocket>& HeldSockets,
	const TArray<FPolySnapWorldSocket>& TargetSockets, const FTransform& HeldPartTransform,
	const FPolySnapCandidate& Anchor, const FTransform& HeldAnchorSocketLocal, const FPolySnapSocketBasis& AnchorBasis,
	const FPolySnapQueryTolerances& Tolerances, EPolySnapPolarity& OutPolarity, double& OutDihedralDegrees,
	double& OutRequiredRotationDegrees)
{
	double BestRotationDegrees = TNumericLimits<double>::Max();
	double BestResidualUu = TNumericLimits<double>::Max();
	bool bFound = false;

	for (const FPolySnapWorldSocket& HeldSocket : HeldSockets)
	{
		const FVector HeldSecondaryLocal =
			HeldPartTransform.InverseTransformPosition(HeldSocket.WorldTransform.GetLocation());

		for (const FPolySnapWorldSocket& TargetSocket : TargetSockets)
		{
			if (!IsAdoptable(HeldSocket, TargetSocket, Anchor))
			{
				continue;
			}

			for (const EPolySnapPolarity Polarity : Polarities)
			{
				double DihedralDegrees = 0.0;
				double ResidualUu = 0.0;

				if (!FPolySnapGeometry::AdoptDihedralDegrees(HeldAnchorSocketLocal, HeldSecondaryLocal, AnchorBasis,
						Polarity, TargetSocket.WorldTransform.GetLocation(), DihedralDegrees, ResidualUu)
					|| ResidualUu > Tolerances.AdoptionDistanceUu)
				{
					continue;
				}

				const double RotationDegrees = RotationToPlacementDegrees(HeldAnchorSocketLocal, HeldPartTransform,
					AnchorBasis, Polarity, DihedralDegrees);

				const bool bNearer = RotationDegrees < BestRotationDegrees - RotationTieDegrees;
				const bool bTiedButCloser =
					RotationDegrees < BestRotationDegrees + RotationTieDegrees && ResidualUu < BestResidualUu;

				if (bNearer || bTiedButCloser)
				{
					BestRotationDegrees = RotationDegrees;
					BestResidualUu = ResidualUu;
					OutPolarity = Polarity;
					OutDihedralDegrees = DihedralDegrees;
					OutRequiredRotationDegrees = RotationDegrees;
					bFound = true;
				}
			}
		}
	}

	return bFound;
}
} // namespace PolySnapSnapQueryPrivate

EPolySnapRejection FPolySnapSnapQuery::TestPair(const FPolySnapWorldSocket& HeldSocket,
	const FPolySnapWorldSocket& TargetSocket, const FPolySnapQueryTolerances& Tolerances,
	FPolySnapCandidate& OutCandidate)
{
	if (HeldSocket.Part.IsValid() && HeldSocket.Part == TargetSocket.Part)
	{
		return EPolySnapRejection::SamePart;
	}

	// Cheap comparison, so it goes first: FName equality is an interned-index compare, not a string
	// walk. SubType, Thickness and Length must all match; the ID never participates, being identity
	// rather than classification.
	if (!HeldSocket.Descriptor.IsCompatibleWith(TargetSocket.Descriptor))
	{
		return EPolySnapRejection::Incompatible;
	}

	const FPolySnapSocketBasis HeldBasis = FPolySnapGeometry::BasisFromTransform(HeldSocket.WorldTransform);
	const FPolySnapSocketBasis TargetBasis = FPolySnapGeometry::BasisFromTransform(TargetSocket.WorldTransform);

	const double GapUu = FVector::Dist(HeldBasis.Location, TargetBasis.Location);

	// Both polarities are admissible, so the test is on the tangent LINES: take the absolute dot
	// product and never mind which way either one points.
	const double TangentAlignment = FMath::Abs(HeldBasis.Tangent | TargetBasis.Tangent);
	const double TangentAngleDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(TangentAlignment, -1.0, 1.0)));

	OutCandidate.HeldSocket = HeldSocket;
	OutCandidate.TargetSocket = TargetSocket;
	OutCandidate.GapUu = GapUu;
	OutCandidate.TangentAngleDegrees = TangentAngleDegrees;

	if (GapUu > Tolerances.SnapDistanceUu)
	{
		return EPolySnapRejection::TooFar;
	}

	if (TangentAngleDegrees > Tolerances.TangentAngleToleranceDegrees)
	{
		return EPolySnapRejection::TangentNotCollinear;
	}

	if (!JointAcceptsParticipant(HeldSocket, TargetSocket))
	{
		return EPolySnapRejection::JointFull;
	}

	return EPolySnapRejection::None;
}

bool FPolySnapSnapQuery::JointAcceptsParticipant(const FPolySnapWorldSocket& HeldSocket,
	const FPolySnapWorldSocket& TargetSocket)
{
	// By default a joint accepts another participant: several panels may meet along one shared
	// edge line, so an occupied socket is not a closed one. Milestone 1 draws the line at a pair
	// that is already connected to each other, which is bookkeeping rather than capacity.
	if (const UPolySnapConnectorComponent* HeldPart = HeldSocket.Part.Get())
	{
		if (HeldPart->IsConnectedTo(TargetSocket.Part.Get(), TargetSocket.Descriptor.Id, HeldSocket.Descriptor.Id))
		{
			return false;
		}
	}

	return true;
}

FPolySnapCandidate FPolySnapSnapQuery::FindBest(const TArray<FPolySnapWorldSocket>& HeldSockets,
	const TArray<FPolySnapWorldSocket>& TargetSockets, const FTransform& HeldPartTransform,
	const FPolySnapQueryTolerances& Tolerances, TArray<FPolySnapRejectedPair>* OutRejections)
{
	FPolySnapCandidate Best;
	double BestCost = TNumericLimits<double>::Max();

	for (const FPolySnapWorldSocket& HeldSocket : HeldSockets)
	{
		// The held socket's transform relative to its part. Solving needs this rather than the
		// world transform, because the answer is where the PART goes.
		const FTransform HeldSocketLocal = HeldSocket.WorldTransform.GetRelativeTransform(HeldPartTransform);

		for (const FPolySnapWorldSocket& TargetSocket : TargetSockets)
		{
			FPolySnapCandidate Candidate;
			const EPolySnapRejection Rejection = TestPair(HeldSocket, TargetSocket, Tolerances, Candidate);

			if (Rejection != EPolySnapRejection::None)
			{
				if (OutRejections != nullptr && Rejection != EPolySnapRejection::Incompatible
					&& Rejection != EPolySnapRejection::SamePart)
				{
					// Incompatible and same-part pairs are the overwhelming majority and say
					// nothing useful, so they are dropped rather than filling the readout.
					OutRejections->Add(FPolySnapRejectedPair{HeldSocket.Descriptor.SocketName,
						TargetSocket.Descriptor.SocketName, Rejection, Candidate.GapUu, Candidate.TangentAngleDegrees});
				}

				continue;
			}

			const FPolySnapSocketBasis TargetBasis = FPolySnapGeometry::BasisFromTransform(TargetSocket.WorldTransform);

			// DESIGN section 2.5 solves theta in this order, and the order is the whole point: with
			// a second pair in tolerance the fold is decided by the geometry already built, and
			// only without one does it fall to how the player happens to be holding the part.
			if (!PolySnapSnapQueryPrivate::SolveAdoptDrivenPlacement(HeldSockets, TargetSockets, HeldPartTransform,
					Candidate, HeldSocketLocal, TargetBasis, Tolerances, Candidate.Polarity, Candidate.DihedralDegrees,
					Candidate.RequiredRotationDegrees))
			{
				FPolySnapGeometry::SolveNearestPlacement(HeldSocketLocal, HeldPartTransform, TargetBasis,
					Candidate.Polarity, Candidate.DihedralDegrees, Candidate.RequiredRotationDegrees);
			}

			Candidate.SolvedPartTransform = FPolySnapGeometry::SolvePartTransform(HeldSocketLocal, TargetBasis,
				Candidate.Polarity, Candidate.DihedralDegrees);

			Candidate.Cost = Candidate.GapUu / Tolerances.SnapDistanceUu
						   + Candidate.RequiredRotationDegrees / Tolerances.TangentAngleToleranceDegrees;

			if (Candidate.Cost < BestCost)
			{
				BestCost = Candidate.Cost;
				Best = Candidate;
			}
		}
	}

	// Only for the winner. An adoption is a consequence of a solved transform, so working them out
	// for every pair that merely could have anchored would be the same search repeated once per
	// candidate for an answer thrown away.
	if (Best.IsSet())
	{
		FindAdoptions(HeldSockets, TargetSockets, HeldPartTransform, Best, Tolerances, Best.Adoptions);
	}

	return Best;
}

void FPolySnapSnapQuery::FindAdoptions(const TArray<FPolySnapWorldSocket>& HeldSockets,
	const TArray<FPolySnapWorldSocket>& TargetSockets, const FTransform& HeldPartTransform,
	const FPolySnapCandidate& Anchor, const FPolySnapQueryTolerances& Tolerances,
	TArray<FPolySnapAdoption>& OutAdoptions)
{
	using namespace PolySnapSnapQueryPrivate;

	OutAdoptions.Reset();

	TArray<FPolySnapAdoption> Passing;

	for (const FPolySnapWorldSocket& HeldSocket : HeldSockets)
	{
		// Where this socket ends up once the anchor's transform is applied. The held sockets were
		// gathered before the solve, so every one of them still describes the pose the part is
		// being carried in.
		const FTransform HeldSocketLocal = HeldSocket.WorldTransform.GetRelativeTransform(HeldPartTransform);
		const FTransform PlacedSocket = HeldSocketLocal * Anchor.SolvedPartTransform;
		const FPolySnapSocketBasis PlacedBasis = FPolySnapGeometry::BasisFromTransform(PlacedSocket);

		for (const FPolySnapWorldSocket& TargetSocket : TargetSockets)
		{
			if (!IsAdoptable(HeldSocket, TargetSocket, Anchor))
			{
				continue;
			}

			const FPolySnapSocketBasis TargetBasis = FPolySnapGeometry::BasisFromTransform(TargetSocket.WorldTransform);

			const double GapUu = FVector::Dist(PlacedBasis.Location, TargetBasis.Location);
			if (GapUu > Tolerances.AdoptionDistanceUu)
			{
				continue;
			}

			const double TangentAlignment = FMath::Abs(PlacedBasis.Tangent | TargetBasis.Tangent);
			const double TangentAngleDegrees =
				FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(TangentAlignment, -1.0, 1.0)));

			if (TangentAngleDegrees > Tolerances.AdoptionAngleToleranceDegrees)
			{
				continue;
			}

			if (!JointAcceptsParticipant(HeldSocket, TargetSocket))
			{
				continue;
			}

			Passing.Add(FPolySnapAdoption{HeldSocket.Descriptor.Id, TargetSocket, GapUu, TangentAngleDegrees});
		}
	}

	// Nearest first, then claim. A socket is one edge of one panel, so where two pairs want the
	// same socket the closer one is the better reading of what the builder meant.
	Passing.Sort([](const FPolySnapAdoption& A, const FPolySnapAdoption& B) { return A.GapUu < B.GapUu; });

	TArray<int32> ClaimedHeldIds;
	TArray<TPair<const UPolySnapConnectorComponent*, int32>> ClaimedTargets;

	for (const FPolySnapAdoption& Adoption : Passing)
	{
		const TPair<const UPolySnapConnectorComponent*, int32> TargetKey(Adoption.TargetSocket.Part.Get(),
			Adoption.TargetSocket.Descriptor.Id);

		if (ClaimedHeldIds.Contains(Adoption.HeldSocketId) || ClaimedTargets.Contains(TargetKey))
		{
			continue;
		}

		ClaimedHeldIds.Add(Adoption.HeldSocketId);
		ClaimedTargets.Add(TargetKey);
		OutAdoptions.Add(Adoption);
	}
}

FString FPolySnapSnapQuery::RejectionToString(EPolySnapRejection Rejection)
{
	switch (Rejection)
	{
		case EPolySnapRejection::None:
			return TEXT("accepted");
		case EPolySnapRejection::SamePart:
			return TEXT("same part");
		case EPolySnapRejection::Incompatible:
			return TEXT("incompatible subtype or size");
		case EPolySnapRejection::TooFar:
			return TEXT("too far apart");
		case EPolySnapRejection::TangentNotCollinear:
			return TEXT("edges not collinear");
		case EPolySnapRejection::JointFull:
			return TEXT("already connected");
		default:
			return TEXT("unknown");
	}
}
