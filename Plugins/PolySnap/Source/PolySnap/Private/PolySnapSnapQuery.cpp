// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapSnapQuery.h"

#include "PolySnapConnectorComponent.h"
#include "PolySnapGeometry.h"

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

			FPolySnapGeometry::SolveNearestPlacement(HeldSocketLocal, HeldPartTransform, TargetBasis,
				Candidate.Polarity, Candidate.DihedralDegrees, Candidate.RequiredRotationDegrees);

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

	return Best;
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
