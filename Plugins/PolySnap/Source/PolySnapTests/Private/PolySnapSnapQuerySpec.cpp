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

/** The socket on the -X edge of a panel at the given transform, facing the other way. */
static FTransform SecondarySocketAt(const FTransform& PartTransform)
{
	return FTransform(FRotator(0.0, 180.0, 0.0), FVector(-HalfPanelUu, 0.0, 0.0)) * PartTransform;
}

/**
 * A panel hinged Degrees away from lying flat, about the edge a panel at the origin owns.
 *
 * Zero leaves it coplanar and beside that panel; ninety stands it upright on the shared edge.
 * Written as a hinge rather than as a transform literal because that is the motion every one of
 * these tests is about, and a bare rotator plus location hides which fold it means.
 */
static FTransform PanelHingedAt(double Degrees)
{
	const FRotator Fold(Degrees, 0.0, 0.0);
	const FVector AnchorEdge(HalfPanelUu, 0.0, 0.0);

	return FTransform(Fold, AnchorEdge + Fold.RotateVector(FVector(HalfPanelUu, 0.0, 0.0)));
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

	Describe("adopting the connections a placement closes",
		[this, Tolerances]()
		{
			// Three panels in a row, coplanar: A at the origin, C two panel widths along, and B
			// placed between them. Mating B on A's edge is one anchor; B's other edge landing on C's
			// is the adoption. Every closure a builder makes is this, with more geometry.
			const FTransform PanelA = FTransform::Identity;
			const FTransform PanelC(FRotator::ZeroRotator, FVector(4.0 * HalfPanelUu, 0.0, 0.0));

			It("adopts a second pair the anchor's transform brings into tolerance",
				[this, Tolerances, PanelA, PanelC]()
				{
					// B held a little off true, the way a player holds it: near enough for the anchor,
					// and its far socket therefore near enough for C once the anchor is solved.
					const FTransform PanelB(FRotator(0.0, 1.0, 0.0), FVector(2.0 * HalfPanelUu + 3.0, 1.0, 0.0));

					const TArray<FPolySnapWorldSocket> Targets = {MakeSocket(1, TEXT("2000"), PrimarySocketAt(PanelA)),
						MakeSocket(2, TEXT("2000"), SecondarySocketAt(PanelC))};
					const TArray<FPolySnapWorldSocket> Held = {MakeSocket(5, TEXT("2000"), SecondarySocketAt(PanelB)),
						MakeSocket(6, TEXT("2000"), PrimarySocketAt(PanelB))};

					const FPolySnapCandidate Candidate =
						FPolySnapSnapQuery::FindBest(Held, Targets, PanelB, Tolerances);

					TestTrue("found an anchor", Candidate.IsSet());
					TestEqual("one adoption", Candidate.Adoptions.Num(), 1);

					if (Candidate.Adoptions.Num() == 1)
					{
						// The adoption is the pair the anchor did not use, on both sides of the seam.
						TestNotEqual("a different held socket", Candidate.Adoptions[0].HeldSocketId,
							Candidate.HeldSocket.Descriptor.Id);
						TestNotEqual("a different target socket", Candidate.Adoptions[0].TargetSocket.Descriptor.Id,
							Candidate.TargetSocket.Descriptor.Id);

						// Coplanar panels of the right length: the seam closes to float noise, which is
						// the whole claim the milestone measurement rests on.
						TestTrue("closed to well under the tolerance",
							Candidate.Adoptions[0].GapUu < 0.5 * Tolerances.AdoptionDistanceUu);
					}
				});

			It("keeps the anchor when the second pair is out of tolerance",
				[this, Tolerances, PanelA]()
				{
					// C a centimetre out of reach: a panel set that does not quite fit. Adopt or drop,
					// per secondary -- the player still gets the placement they asked for, plus a seam
					// they can see is open.
					const FTransform FarPanelC(FRotator::ZeroRotator, FVector(4.0 * HalfPanelUu + 10.0, 0.0, 0.0));
					const FTransform PanelB(FRotator::ZeroRotator, FVector(2.0 * HalfPanelUu, 0.0, 0.0));

					const TArray<FPolySnapWorldSocket> Targets = {MakeSocket(1, TEXT("2000"), PrimarySocketAt(PanelA)),
						MakeSocket(2, TEXT("2000"), SecondarySocketAt(FarPanelC))};
					const TArray<FPolySnapWorldSocket> Held = {MakeSocket(5, TEXT("2000"), SecondarySocketAt(PanelB)),
						MakeSocket(6, TEXT("2000"), PrimarySocketAt(PanelB))};

					const FPolySnapCandidate Candidate =
						FPolySnapSnapQuery::FindBest(Held, Targets, PanelB, Tolerances);

					TestTrue("still anchored", Candidate.IsSet());
					TestEqual("nothing adopted", Candidate.Adoptions.Num(), 0);
				});

			It("never adopts a socket that is already connected",
				[this, Tolerances, PanelA, PanelC]()
				{
					// A joint may host more than two panels, but joining a new part into an occupied
					// one waits for Milestone 3's first-class joint. Until then an occupied socket is
					// passed over, leaving a seam that is visibly open rather than silently a threesome.
					const FTransform PanelB(FRotator::ZeroRotator, FVector(2.0 * HalfPanelUu, 0.0, 0.0));

					TArray<FPolySnapWorldSocket> Targets = {MakeSocket(1, TEXT("2000"), PrimarySocketAt(PanelA)),
						MakeSocket(2, TEXT("2000"), SecondarySocketAt(PanelC))};
					Targets[1].bConnected = true;

					const TArray<FPolySnapWorldSocket> Held = {MakeSocket(5, TEXT("2000"), SecondarySocketAt(PanelB)),
						MakeSocket(6, TEXT("2000"), PrimarySocketAt(PanelB))};

					const FPolySnapCandidate Candidate =
						FPolySnapSnapQuery::FindBest(Held, Targets, PanelB, Tolerances);

					TestTrue("still anchored", Candidate.IsSet());
					TestEqual("nothing adopted", Candidate.Adoptions.Num(), 0);
				});

			It("prefers the nearer closure to a nearer-fitting one that folds the part onto a panel",
				[this, Tolerances, PanelA]()
				{
					// The case that stops a shell closing at all. Folding a panel flat onto a panel
					// already placed lands every one of its sockets on that panel's, at a residual of
					// exactly zero -- so ranking closures by residual makes the degenerate answer beat
					// the intended one every time, and the assembly stacks instead of closing.
					//
					// B is held most of the way to standing, because a closure has to be one the
					// player has nearly made before it is offered at all.
					const FTransform PanelB = PanelHingedAt(75.0);

					// The intended closure: a socket standing over the anchor, missing by a hair.
					const FTransform StandingTarget(FRotator(0.0, 180.0, 0.0),
						FVector(HalfPanelUu, 0.0, 2.0 * HalfPanelUu + 0.05));

					// The degenerate one: exactly where B's far socket lands if B folds onto A.
					const FTransform StackedTarget(FRotator::ZeroRotator, FVector(-HalfPanelUu, 0.0, 0.0));

					const TArray<FPolySnapWorldSocket> Targets = {MakeSocket(1, TEXT("2000"), PrimarySocketAt(PanelA)),
						MakeSocket(2, TEXT("2000"), StandingTarget), MakeSocket(3, TEXT("2000"), StackedTarget)};
					const TArray<FPolySnapWorldSocket> Held = {MakeSocket(5, TEXT("2000"), SecondarySocketAt(PanelB)),
						MakeSocket(6, TEXT("2000"), PrimarySocketAt(PanelB))};

					const FPolySnapCandidate Candidate =
						FPolySnapSnapQuery::FindBest(Held, Targets, PanelB, Tolerances);

					TestTrue("found an anchor", Candidate.IsSet());

					// A quarter turn away rather than a half turn, and the seam it closes is the one
					// that leaves the panel somewhere a panel is not already.
					TestEqual("folded to stand on the anchor, not onto the panel",
						Candidate.SolvedPartTransform.GetLocation(), FVector(HalfPanelUu, 0.0, HalfPanelUu), 1.0f);
				});

			It("folds the part to close a second pair rather than to where it was held",
				[this, Tolerances, PanelA]()
				{
					// DESIGN section 2.5's adopt-driven reading, and the one that makes a shell close.
					// B is held fifteen degrees short of where its far socket meets the second
					// target: the wrist gets the fold close and the geometry already built supplies
					// the rest, which is the whole of what adoption is for.
					const FTransform PanelB = PanelHingedAt(75.0);

					// A socket standing directly over the anchor, one panel width up. Only finishing
					// the fold puts B's far edge there.
					const FTransform StandingTarget(FRotator(0.0, 180.0, 0.0),
						FVector(HalfPanelUu, 0.0, 2.0 * HalfPanelUu));

					const TArray<FPolySnapWorldSocket> Targets = {MakeSocket(1, TEXT("2000"), PrimarySocketAt(PanelA)),
						MakeSocket(2, TEXT("2000"), StandingTarget)};
					const TArray<FPolySnapWorldSocket> Held = {MakeSocket(5, TEXT("2000"), SecondarySocketAt(PanelB)),
						MakeSocket(6, TEXT("2000"), PrimarySocketAt(PanelB))};

					const FPolySnapCandidate Candidate =
						FPolySnapSnapQuery::FindBest(Held, Targets, PanelB, Tolerances);

					TestTrue("found an anchor", Candidate.IsSet());
					TestEqual("one adoption", Candidate.Adoptions.Num(), 1);

					// Held short of true, the least-rotation reading would have left it short and
					// closed nothing. The seam decides the last fifteen degrees.
					TestEqual("folded to stand on the anchor", Candidate.SolvedPartTransform.GetLocation(),
						FVector(HalfPanelUu, 0.0, HalfPanelUu), 1.0e-3f);
				});
		});

	Describe("refusing a placement that folds a part onto a part",
		[this, Tolerances]()
		{
			// Two panels and one seam between them, which is where a build starts and where the
			// least-rotation ranking has nothing to rank: the only closure on offer is the
			// degenerate one, so something else has to refuse it.
			const FTransform PanelA = FTransform::Identity;

			It("stays coplanar when the only closure on offer would stack the panels",
				[this, Tolerances, PanelA]()
				{
					// B held beside A the way a player holds it, a couple of units off true. Its far
					// socket can only reach one of A's sockets by folding B flat onto A -- theta 0,
					// residual zero, every remaining pair closed. The coplanar answer the player is
					// asking for closes nothing at all, so it never enters the adopt-driven search;
					// refusing the coincident fold is the only thing that leaves it standing.
					const FTransform PanelB(FRotator::ZeroRotator, FVector(2.0 * HalfPanelUu + 2.0, 0.0, 0.0));

					const TArray<FPolySnapWorldSocket> Targets = {MakeSocket(1, TEXT("2000"), PrimarySocketAt(PanelA)),
						MakeSocket(2, TEXT("2000"), SecondarySocketAt(PanelA))};
					const TArray<FPolySnapWorldSocket> Held = {MakeSocket(5, TEXT("2000"), SecondarySocketAt(PanelB)),
						MakeSocket(6, TEXT("2000"), PrimarySocketAt(PanelB))};

					const FPolySnapCandidate Candidate =
						FPolySnapSnapQuery::FindBest(Held, Targets, PanelB, Tolerances);

					TestTrue("found an anchor", Candidate.IsSet());
					TestEqual("a flat seam", Candidate.DihedralDegrees, 180.0, 1.0);
					TestEqual("nothing adopted", Candidate.Adoptions.Num(), 0);
					TestEqual("left beside A rather than on it", Candidate.SolvedPartTransform.GetLocation(),
						FVector(2.0 * HalfPanelUu, 0.0, 0.0), 1.0e-3f);
				});

			It("refuses a pair the part is already sitting on top of",
				[this, Tolerances, PanelA]()
				{
					// B held exactly over A, a half turn about its own normal, so both of its sockets
					// land on one of A's. Nothing here is a seam: the player-driven reading would
					// commit the pose as it stands and weld two panels occupying one space.
					const FTransform PanelB(FRotator(0.0, 180.0, 0.0), FVector::ZeroVector);

					const TArray<FPolySnapWorldSocket> Targets = {MakeSocket(1, TEXT("2000"), PrimarySocketAt(PanelA)),
						MakeSocket(2, TEXT("2000"), SecondarySocketAt(PanelA))};
					const TArray<FPolySnapWorldSocket> Held = {MakeSocket(5, TEXT("2000"), SecondarySocketAt(PanelB)),
						MakeSocket(6, TEXT("2000"), PrimarySocketAt(PanelB))};

					TArray<FPolySnapRejectedPair> Rejections;
					const FPolySnapCandidate Candidate =
						FPolySnapSnapQuery::FindBest(Held, Targets, PanelB, Tolerances, &Rejections);

					TestFalse("nothing to snap to", Candidate.IsSet());

					const bool bSaidWhy = Rejections.ContainsByPredicate([](const FPolySnapRejectedPair& Pair)
						{ return Pair.Reason == EPolySnapRejection::Coincident; });

					// The readout has to name it. A snap that silently does nothing is the one failure
					// a builder cannot tell from a bug.
					TestTrue(*FString::Printf(TEXT("said why: %s"), *RejectionName(EPolySnapRejection::Coincident)),
						bSaidWhy);
				});
		});

	Describe("declining a closure the part is nowhere near",
		[this, Tolerances]()
		{
			const FTransform PanelA = FTransform::Identity;

			// A socket standing over the anchor, a quarter turn from where a flat-held panel is.
			const FTransform StandingTarget(FRotator(0.0, 180.0, 0.0), FVector(HalfPanelUu, 0.0, 2.0 * HalfPanelUu));

			It("leaves the seam open rather than folding the part somewhere it was not turned",
				[this, Tolerances, PanelA, StandingTarget]()
				{
					// As a part hinges, each of its sockets sweeps a circle past whatever else is
					// built, and any panel that circle passes offers a closure. Held flat, B is a
					// quarter turn from this one -- a fold nobody made and nobody asked for.
					const FTransform PanelB = PanelHingedAt(0.0);

					const TArray<FPolySnapWorldSocket> Targets = {MakeSocket(1, TEXT("2000"), PrimarySocketAt(PanelA)),
						MakeSocket(2, TEXT("2000"), StandingTarget)};
					const TArray<FPolySnapWorldSocket> Held = {MakeSocket(5, TEXT("2000"), SecondarySocketAt(PanelB)),
						MakeSocket(6, TEXT("2000"), PrimarySocketAt(PanelB))};

					const FPolySnapCandidate Candidate =
						FPolySnapSnapQuery::FindBest(Held, Targets, PanelB, Tolerances);

					// The anchor is never what a declined closure costs. The part lands on the edge
					// the player aimed at, at the angle they were holding it, and the seam that did
					// not close is there to be seen and nudged.
					TestTrue("still anchored", Candidate.IsSet());
					TestEqual("nothing adopted", Candidate.Adoptions.Num(), 0);
					TestEqual("left at the fold it was held at", Candidate.SolvedPartTransform.GetLocation(),
						PanelHingedAt(0.0).GetLocation(), 1.0e-3f);
				});

			It("closes the same seam once the part is turned most of the way to it",
				[this, Tolerances, PanelA, StandingTarget]()
				{
					// The same panel and the same seam as above. Nothing about the geometry changed;
					// the player turned the part, and that is what makes the closure theirs.
					const FTransform PanelB = PanelHingedAt(75.0);

					const TArray<FPolySnapWorldSocket> Targets = {MakeSocket(1, TEXT("2000"), PrimarySocketAt(PanelA)),
						MakeSocket(2, TEXT("2000"), StandingTarget)};
					const TArray<FPolySnapWorldSocket> Held = {MakeSocket(5, TEXT("2000"), SecondarySocketAt(PanelB)),
						MakeSocket(6, TEXT("2000"), PrimarySocketAt(PanelB))};

					const FPolySnapCandidate Candidate =
						FPolySnapSnapQuery::FindBest(Held, Targets, PanelB, Tolerances);

					TestTrue("found an anchor", Candidate.IsSet());
					TestEqual("one adoption", Candidate.Adoptions.Num(), 1);
					TestEqual("folded the rest of the way", Candidate.SolvedPartTransform.GetLocation(),
						PanelHingedAt(90.0).GetLocation(), 1.0e-3f);
				});
		});
}

#endif // WITH_DEV_AUTOMATION_TESTS
