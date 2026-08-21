// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "Misc/AutomationTest.h"
#include "PolySnapGeometry.h"
#include "PolySnapSnapQuery.h"
#include "PolySnapTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FPolySnapSnapQuerySpec, "PolySnap.SnapQuery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Half of the calibration panel's edge, in Unreal units. */
static constexpr double HalfPanelUu = 100.0;

/** The calibration panel's thickness token. Only the tests about it pass anything else. */
static constexpr const TCHAR* PanelThickness = TEXT("40");

/**
 * A world socket with no part behind it, which is all the pure query needs.
 *
 * The size fields are tokens rather than numbers, so they are spelled as text here; these ones
 * happen to read as millimetres because the calibration panel does, but the query never knows
 * that. Thickness is defaulted because almost every test here is about geometry rather than
 * compatibility, and spelling the same 40 at every call site would bury the two cases that
 * actually vary it.
 */
static FPolySnapWorldSocket MakeSocket(int32 Id, const TCHAR* Length, const FTransform& WorldTransform,
	const TCHAR* Thickness = PanelThickness)
{
	FPolySnapWorldSocket Socket;
	Socket.Descriptor.SocketName = FName(*FString::Printf(TEXT("Edge_%03d_Straight_%s_%s"), Id, Thickness, Length));
	Socket.Descriptor.Id = Id;
	Socket.Descriptor.SubType = EPolySnapEdgeSubType::Straight;
	Socket.Descriptor.Thickness = FName(Thickness);
	Socket.Descriptor.Length = FName(Length);
	Socket.WorldTransform = WorldTransform;

	return Socket;
}

/** The socket on the +X edge of a panel at the given transform. */
static FTransform PrimarySocketAt(const FTransform& PartTransform)
{
	return FTransform(FRotator::ZeroRotator, FVector(HalfPanelUu, 0.0, 0.0)) * PartTransform;
}

static FString RejectionName(EPolySnapRejection Rejection)
{
	return FPolySnapSnapQuery::RejectionToString(Rejection);
}

END_DEFINE_SPEC(FPolySnapSnapQuerySpec)

void FPolySnapSnapQuerySpec::Define()
{
	const FPolySnapQueryTolerances Tolerances;

	Describe("testing one socket pair",
		[this, Tolerances]()
		{
			It("rejects an incompatible length before anything geometric runs",
				[this, Tolerances]()
				{
					// Cheap integer comparison, so it goes first. Two sockets in exactly the same place
					// still do not mate if their lengths differ.
					const FPolySnapWorldSocket Held = MakeSocket(1, TEXT("2000"), FTransform::Identity);
					const FPolySnapWorldSocket Target = MakeSocket(1, TEXT("3464"), FTransform::Identity);

					FPolySnapCandidate Candidate;
					TestEqual("reason",
						RejectionName(FPolySnapSnapQuery::TestPair(Held, Target, Tolerances, Candidate)),
						RejectionName(EPolySnapRejection::Incompatible));
				});

			It("rejects an incompatible thickness even when the lengths agree",
				[this, Tolerances]()
				{
					// A thin partition against a thick hull panel: same edge length, so the geometry
					// would mate happily, and the seam it produces is stepped rather than flush. The
					// thickness token is the only thing that knows, since no socket transform carries
					// a cross-section.
					const FPolySnapWorldSocket Held = MakeSocket(1, TEXT("2000"), FTransform::Identity, TEXT("40"));
					const FPolySnapWorldSocket Target = MakeSocket(1, TEXT("2000"), FTransform::Identity, TEXT("100"));

					FPolySnapCandidate Candidate;
					TestEqual("reason",
						RejectionName(FPolySnapSnapQuery::TestPair(Held, Target, Tolerances, Candidate)),
						RejectionName(EPolySnapRejection::Incompatible));
				});

			It("rejects a pair further apart than the snap distance",
				[this, Tolerances]()
				{
					const FPolySnapWorldSocket Held = MakeSocket(1, TEXT("2000"), FTransform::Identity);
					const FPolySnapWorldSocket Target =
						MakeSocket(1, TEXT("2000"), FTransform(FRotator(0.0, 180.0, 0.0), FVector(500.0, 0.0, 0.0)));

					FPolySnapCandidate Candidate;
					TestEqual("reason",
						RejectionName(FPolySnapSnapQuery::TestPair(Held, Target, Tolerances, Candidate)),
						RejectionName(EPolySnapRejection::TooFar));
				});

			It("rejects a pair whose edges are not collinear",
				[this, Tolerances]()
				{
					const FPolySnapWorldSocket Held = MakeSocket(1, TEXT("2000"), FTransform::Identity);
					const FPolySnapWorldSocket Target =
						MakeSocket(1, TEXT("2000"), FTransform(FRotator(0.0, 90.0, 0.0)));

					FPolySnapCandidate Candidate;
					TestEqual("reason",
						RejectionName(FPolySnapSnapQuery::TestPair(Held, Target, Tolerances, Candidate)),
						RejectionName(EPolySnapRejection::TangentNotCollinear));
					TestEqual("angle", Candidate.TangentAngleDegrees, 90.0, 1.0e-3);
				});

			It("accepts either tangent polarity, because both are admissible",
				[this, Tolerances]()
				{
					const FPolySnapWorldSocket Held = MakeSocket(1, TEXT("2000"), FTransform::Identity);

					// Opposed tangents.
					const FPolySnapWorldSocket Aligned =
						MakeSocket(1, TEXT("2000"), FTransform(FRotator(0.0, 180.0, 0.0)));
					FPolySnapCandidate AlignedCandidate;
					TestEqual("aligned",
						RejectionName(FPolySnapSnapQuery::TestPair(Held, Aligned, Tolerances, AlignedCandidate)),
						RejectionName(EPolySnapRejection::None));

					// Same tangent: the part is turned over, which is a motion the player can perform.
					const FPolySnapWorldSocket Flipped =
						MakeSocket(1, TEXT("2000"), FTransform(FRotator(0.0, 0.0, 180.0)));
					FPolySnapCandidate FlippedCandidate;
					TestEqual("flipped",
						RejectionName(FPolySnapSnapQuery::TestPair(Held, Flipped, Tolerances, FlippedCandidate)),
						RejectionName(EPolySnapRejection::None));
				});

			It("reports the gap and the edge angle even for a pair it rejects",
				[this, Tolerances]()
				{
					// The measurements are what the debug readout shows while the player closes in, so
					// they have to be filled in on the way to a rejection, not only on success.
					const FPolySnapWorldSocket Held = MakeSocket(1, TEXT("2000"), FTransform::Identity);
					const FPolySnapWorldSocket Target =
						MakeSocket(1, TEXT("2000"), FTransform(FRotator(0.0, 180.0, 0.0), FVector(60.0, 0.0, 0.0)));

					FPolySnapCandidate Candidate;
					TestEqual("reason",
						RejectionName(FPolySnapSnapQuery::TestPair(Held, Target, Tolerances, Candidate)),
						RejectionName(EPolySnapRejection::TooFar));

					TestEqual("gap", Candidate.GapUu, 60.0, 1.0e-6);
					TestEqual("angle", Candidate.TangentAngleDegrees, 0.0, 1.0e-3);
				});
		});

	Describe("finding the anchor",
		[this, Tolerances]()
		{
			It("returns nothing when no pair is in tolerance",
				[this, Tolerances]()
				{
					const TArray<FPolySnapWorldSocket> Held = {MakeSocket(1, TEXT("2000"), FTransform::Identity)};
					const TArray<FPolySnapWorldSocket> Targets = {
						MakeSocket(1, TEXT("2000"), FTransform(FRotator::ZeroRotator, FVector(900.0, 0.0, 0.0)))};

					const FPolySnapCandidate Candidate =
						FPolySnapSnapQuery::FindBest(Held, Targets, FTransform::Identity, Tolerances);

					TestFalse("unset", Candidate.IsSet());
				});

			It("solves a transform that places the held socket exactly on the target",
				[this, Tolerances]()
				{
					// Panel B sits a few units off a flush coplanar seam with panel A, the way a player
					// would hold it. The solve has to close that gap exactly.
					const FTransform PanelA = FTransform::Identity;
					const FTransform PanelB(FRotator(0.0, 178.0, 0.0), FVector(2.0 * HalfPanelUu + 4.0, 2.0, 0.0));

					const TArray<FPolySnapWorldSocket> Held = {MakeSocket(1, TEXT("2000"), PrimarySocketAt(PanelB))};
					const TArray<FPolySnapWorldSocket> Targets = {MakeSocket(1, TEXT("2000"), PrimarySocketAt(PanelA))};

					const FPolySnapCandidate Candidate =
						FPolySnapSnapQuery::FindBest(Held, Targets, PanelB, Tolerances);

					TestTrue("found a candidate", Candidate.IsSet());

					const FTransform HeldSocketLocal = Candidate.HeldSocket.WorldTransform.GetRelativeTransform(PanelB);
					const FPolySnapSocketBasis Placed =
						FPolySnapGeometry::BasisFromTransform(HeldSocketLocal * Candidate.SolvedPartTransform);
					const FPolySnapSocketBasis Anchor =
						FPolySnapGeometry::BasisFromTransform(Candidate.TargetSocket.WorldTransform);

					TestEqual("sockets coincide", Placed.Location, Anchor.Location, 1.0e-3f);
					TestEqual("tangents collinear", FMath::Abs(Placed.Tangent | Anchor.Tangent), 1.0, 1.0e-4);
				});

			It("solves the same transform whatever scale the sockets carry",
				[this, Tolerances]()
				{
					// MakeSocket builds WorldTransform directly, so this never passes through
					// Canonicalise -- which is the point. It is an independent check that the
					// solver itself is immune, for the case where scale re-enters from the part
					// side of GetRelativeTransform rather than off the mesh.
					const FTransform PanelA = FTransform::Identity;
					const FTransform PanelB(FRotator(0.0, 178.0, 0.0), FVector(2.0 * HalfPanelUu + 4.0, 2.0, 0.0));

					const auto Scaled = [](const FTransform& Socket, double Scale)
					{
						return FTransform(Socket.GetRotation(), Socket.GetTranslation(), FVector(Scale));
					};

					const TArray<FPolySnapWorldSocket> Held = {MakeSocket(1, TEXT("2000"), PrimarySocketAt(PanelB))};
					const TArray<FPolySnapWorldSocket> Targets = {MakeSocket(1, TEXT("2000"), PrimarySocketAt(PanelA))};

					const TArray<FPolySnapWorldSocket> ScaledHeld = {
						MakeSocket(1, TEXT("2000"), Scaled(PrimarySocketAt(PanelB), 2.5))};
					const TArray<FPolySnapWorldSocket> ScaledTargets = {
						MakeSocket(1, TEXT("2000"), Scaled(PrimarySocketAt(PanelA), 0.4))};

					const FPolySnapCandidate Plain = FPolySnapSnapQuery::FindBest(Held, Targets, PanelB, Tolerances);
					const FPolySnapCandidate WithScale =
						FPolySnapSnapQuery::FindBest(ScaledHeld, ScaledTargets, PanelB, Tolerances);

					TestTrue("found a candidate", WithScale.IsSet());
					TestTrue("same placement", WithScale.SolvedPartTransform.Equals(Plain.SolvedPartTransform, 1.0e-4));
					TestEqual("part is unscaled", WithScale.SolvedPartTransform.GetScale3D(), FVector::OneVector,
						1.0e-4f);
				});

			It("prefers the nearer of two passing pairs",
				[this, Tolerances]()
				{
					const FTransform PanelB(FRotator(0.0, 180.0, 0.0), FVector(2.0 * HalfPanelUu + 2.0, 0.0, 0.0));

					const TArray<FPolySnapWorldSocket> Held = {MakeSocket(1, TEXT("2000"), PrimarySocketAt(PanelB))};

					// Two targets on the same edge line, one 2 uu away and one 12 uu away.
					const TArray<FPolySnapWorldSocket> Targets = {MakeSocket(7,
						TEXT("2000"), PrimarySocketAt(FTransform(FRotator::ZeroRotator, FVector(10.0, 0.0, 0.0)))),
						MakeSocket(4, TEXT("2000"), PrimarySocketAt(FTransform::Identity))};

					const FPolySnapCandidate Candidate =
						FPolySnapSnapQuery::FindBest(Held, Targets, PanelB, Tolerances);

					TestTrue("found a candidate", Candidate.IsSet());
					TestEqual("anchored on the nearer socket", Candidate.TargetSocket.Descriptor.Id, 4);
				});

			It("records why the pairs that failed did",
				[this, Tolerances]()
				{
					const TArray<FPolySnapWorldSocket> Held = {MakeSocket(1, TEXT("2000"), FTransform::Identity)};
					const TArray<FPolySnapWorldSocket> Targets = {
						MakeSocket(1, TEXT("2000"), FTransform(FRotator::ZeroRotator, FVector(900.0, 0.0, 0.0)))};

					TArray<FPolySnapRejectedPair> Rejections;
					const FPolySnapCandidate Candidate =
						FPolySnapSnapQuery::FindBest(Held, Targets, FTransform::Identity, Tolerances, &Rejections);

					TestFalse("nothing was accepted", Candidate.IsSet());
					TestEqual("one rejection", Rejections.Num(), 1);
					if (Rejections.Num() == 1)
					{
						TestEqual("reason", RejectionName(Rejections[0].Reason),
							RejectionName(EPolySnapRejection::TooFar));
					}
				});
		});
}

#endif // WITH_DEV_AUTOMATION_TESTS
