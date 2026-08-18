// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "Misc/AutomationTest.h"
#include "PolySnapGeometry.h"
#include "PolySnapTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FPolySnapGeometrySpec, "PolySnap.Geometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Half of the calibration panel's 2000 mm edge, in Unreal units. CONVENTIONS.md section 7. */
static constexpr double HalfPanelUu = 100.0;

/** The socket on the +X edge of a panel authored per CONVENTIONS.md: unrotated, at (100, 0, 0). */
static FTransform PrimarySocketLocal()
{
	return FTransform(FRotator::ZeroRotator, FVector(HalfPanelUu, 0.0, 0.0));
}

END_DEFINE_SPEC(FPolySnapGeometrySpec)

void FPolySnapGeometrySpec::Define()
{
	Describe("the socket basis",
		[this]()
		{
			It("reads Outward from local +X and Normal from local +Z",
				[this]()
				{
					const FPolySnapSocketBasis Basis = FPolySnapGeometry::BasisFromTransform(FTransform::Identity);

					TestEqual("outward", Basis.Outward, FVector::ForwardVector);
					TestEqual("normal", Basis.Normal, FVector::UpVector);
				});

			It("reads Tangent from local MINUS Y",
				[this]()
				{
					// CONVENTIONS.md section 2. The FBX conversion negates Y, so an unrotated socket's
					// Tangent points along -Y. Deriving it as +Y runs every polarity test backwards, and
					// this is the assertion that catches that.
					const FPolySnapSocketBasis Basis = FPolySnapGeometry::BasisFromTransform(FTransform::Identity);

					TestEqual("tangent", Basis.Tangent, -FVector::RightVector);
				});

			It("keeps the triad right-handed as a physical arrangement",
				[this]()
				{
					// Unreal's cross product is the plain right-hand formula while its coordinate system
					// is left-handed, so the identical socket computes as Normal == Tangent ^ Outward.
					const FPolySnapSocketBasis Basis =
						FPolySnapGeometry::BasisFromTransform(FTransform(FRotator(15.0, 40.0, -25.0)));

					TestEqual("normal", FVector::CrossProduct(Basis.Tangent, Basis.Outward), Basis.Normal);
				});

			It("round-trips through TransformFromBasis",
				[this]()
				{
					const FTransform Original(FRotator(10.0, -70.0, 33.0), FVector(12.0, -4.0, 8.0));
					const FPolySnapSocketBasis Basis = FPolySnapGeometry::BasisFromTransform(Original);
					const FPolySnapSocketBasis Rebuilt =
						FPolySnapGeometry::BasisFromTransform(FPolySnapGeometry::TransformFromBasis(Basis));

					TestEqual("outward", Rebuilt.Outward, Basis.Outward);
					TestEqual("tangent", Rebuilt.Tangent, Basis.Tangent);
					TestEqual("normal", Rebuilt.Normal, Basis.Normal);
					TestEqual("location", Rebuilt.Location, Basis.Location);
				});
		});

	Describe("the dihedral angle",
		[this]()
		{
			It("reads 180 degrees for a coplanar seam",
				[this]()
				{
					// CONVENTIONS.md section 7 check 4, in code: panel A at the origin and panel B at
					// (200, 0, 0) yawed 180 degrees meet along one flush edge.
					const FTransform PanelB(FRotator(0.0, 180.0, 0.0), FVector(2.0 * HalfPanelUu, 0.0, 0.0));

					const FPolySnapSocketBasis SocketA = FPolySnapGeometry::BasisFromTransform(PrimarySocketLocal());
					const FPolySnapSocketBasis SocketB =
						FPolySnapGeometry::BasisFromTransform(PrimarySocketLocal() * PanelB);

					TestEqual("sockets coincide", SocketB.Location, SocketA.Location);
					TestEqual("tangents opposed", SocketB.Tangent, -SocketA.Tangent);
					TestEqual("dihedral", FPolySnapGeometry::DihedralDegrees(SocketA, SocketB), 180.0, 1.0e-6);
				});

			It("round-trips every angle the mating basis was built at",
				[this]()
				{
					const FPolySnapSocketBasis Anchor = FPolySnapGeometry::BasisFromTransform(
						FTransform(FRotator(20.0, -35.0, 12.0), FVector(5.0, 7.0, -3.0)));

					for (const double Expected : {0.0, 45.0, 90.0, 138.19, 180.0, 250.0, 359.0})
					{
						const FPolySnapSocketBasis Mated =
							FPolySnapGeometry::MatedBasis(Anchor, EPolySnapPolarity::Aligned, Expected);

						TestEqual(*FString::Printf(TEXT("dihedral %.2f"), Expected),
							FPolySnapGeometry::DihedralDegrees(Anchor, Mated), Expected, 1.0e-6);
					}
				});

			It("is signed, so a concave fold does not read as its convex mirror",
				[this]()
				{
					const FPolySnapSocketBasis Anchor = FPolySnapGeometry::BasisFromTransform(FTransform::Identity);

					const double Convex = FPolySnapGeometry::DihedralDegrees(Anchor,
						FPolySnapGeometry::MatedBasis(Anchor, EPolySnapPolarity::Aligned, 100.0));
					const double Concave = FPolySnapGeometry::DihedralDegrees(Anchor,
						FPolySnapGeometry::MatedBasis(Anchor, EPolySnapPolarity::Aligned, 260.0));

					TestEqual("convex", Convex, 100.0, 1.0e-6);
					TestEqual("concave", Concave, 260.0, 1.0e-6);
				});

			It("returns the same angle from the other basis when the pair is aligned",
				[this]()
				{
					const FPolySnapSocketBasis Anchor = FPolySnapGeometry::BasisFromTransform(FTransform::Identity);
					const FPolySnapSocketBasis Mated =
						FPolySnapGeometry::MatedBasis(Anchor, EPolySnapPolarity::Aligned, 138.19);

					TestEqual("measured from B", FPolySnapGeometry::DihedralDegrees(Mated, Anchor), 138.19, 1.0e-6);
				});

			It("returns 360 minus the angle from the other basis when the pair is flipped",
				[this]()
				{
					// This asymmetry is why naming the anchor as the reference is what makes the angle
					// single-valued at all, rather than a convenience.
					const FPolySnapSocketBasis Anchor = FPolySnapGeometry::BasisFromTransform(FTransform::Identity);
					const FPolySnapSocketBasis Mated =
						FPolySnapGeometry::MatedBasis(Anchor, EPolySnapPolarity::Flipped, 138.19);

					TestEqual("measured from A", FPolySnapGeometry::DihedralDegrees(Anchor, Mated), 138.19, 1.0e-6);
					TestEqual("measured from B", FPolySnapGeometry::DihedralDegrees(Mated, Anchor), 360.0 - 138.19,
						1.0e-6);
				});
		});

	Describe("the mating basis",
		[this]()
		{
			It("opposes the tangent when aligned and matches it when flipped",
				[this]()
				{
					const FPolySnapSocketBasis Anchor =
						FPolySnapGeometry::BasisFromTransform(FTransform(FRotator(0.0, 30.0, 0.0)));

					TestEqual("aligned",
						FPolySnapGeometry::MatedBasis(Anchor, EPolySnapPolarity::Aligned, 180.0).Tangent,
						-Anchor.Tangent);
					TestEqual("flipped",
						FPolySnapGeometry::MatedBasis(Anchor, EPolySnapPolarity::Flipped, 180.0).Tangent,
						Anchor.Tangent);
				});

			It("keeps positions coincident whatever the angle",
				[this]()
				{
					const FPolySnapSocketBasis Anchor = FPolySnapGeometry::BasisFromTransform(
						FTransform(FRotator(11.0, 22.0, 33.0), FVector(9.0, 1.0, 4.0)));

					TestEqual("location",
						FPolySnapGeometry::MatedBasis(Anchor, EPolySnapPolarity::Aligned, 73.0).Location,
						Anchor.Location);
				});
		});

	Describe("the solved piece transform",
		[this]()
		{
			It("puts the held socket exactly on the anchor",
				[this]()
				{
					const FTransform HeldSocketLocal = PrimarySocketLocal();
					const FPolySnapSocketBasis Anchor = FPolySnapGeometry::BasisFromTransform(
						FTransform(FRotator(5.0, 61.0, -14.0), FVector(300.0, -120.0, 45.0)));

					const FTransform Solved = FPolySnapGeometry::SolvePieceTransform(HeldSocketLocal, Anchor,
						EPolySnapPolarity::Aligned, 138.19);
					const FPolySnapSocketBasis Placed = FPolySnapGeometry::BasisFromTransform(HeldSocketLocal * Solved);

					// The anchor's mating conditions hold to float precision. That exactness is the
					// invariant that tells a geometry bug from an accumulation artefact.
					TestEqual("position", Placed.Location, Anchor.Location, 1.0e-4f);
					TestEqual("tangent", Placed.Tangent, -Anchor.Tangent, 1.0e-4f);
					TestEqual("dihedral", FPolySnapGeometry::DihedralDegrees(Anchor, Placed), 138.19, 1.0e-4);
				});

			It("leaves the piece unscaled",
				[this]()
				{
					const FTransform Solved = FPolySnapGeometry::SolvePieceTransform(PrimarySocketLocal(),
						FPolySnapGeometry::BasisFromTransform(FTransform(FRotator(0.0, 45.0, 0.0))),
						EPolySnapPolarity::Aligned, 90.0);

					TestEqual("scale", Solved.GetScale3D(), FVector::OneVector, 1.0e-4f);
				});

			It("reproduces the calibration seam from panel A's own socket",
				[this]()
				{
					// The snapper's answer to CONVENTIONS.md section 7 check 4: given panel A at the
					// origin, placing panel B coplanar must put it at (200, 0, 0) yawed 180 degrees.
					const FPolySnapSocketBasis Anchor = FPolySnapGeometry::BasisFromTransform(PrimarySocketLocal());
					const FTransform Solved = FPolySnapGeometry::SolvePieceTransform(PrimarySocketLocal(), Anchor,
						EPolySnapPolarity::Aligned, 180.0);

					TestEqual("location", Solved.GetLocation(), FVector(2.0 * HalfPanelUu, 0.0, 0.0), 1.0e-4f);
					TestEqual("rotation", Solved.Rotator().Yaw, 180.0, 1.0e-3);
				});
		});

	Describe("the least-rotation placement",
		[this]()
		{
			It("picks the polarity nearer the orientation the piece is already in",
				[this]()
				{
					const FTransform HeldSocketLocal = PrimarySocketLocal();
					const FPolySnapSocketBasis Anchor = FPolySnapGeometry::BasisFromTransform(PrimarySocketLocal());

					// Start from the pose an aligned coplanar seam would need, nudged slightly. The
					// player turned the piece roughly this way, so the snap must commit to that reading.
					const FTransform NearAligned = FPolySnapGeometry::SolvePieceTransform(HeldSocketLocal, Anchor,
						EPolySnapPolarity::Aligned, 175.0);

					EPolySnapPolarity Polarity = EPolySnapPolarity::Flipped;
					double Dihedral = 0.0;
					double RequiredRotation = 0.0;
					FPolySnapGeometry::SolveNearestPlacement(HeldSocketLocal, NearAligned, Anchor, Polarity, Dihedral,
						RequiredRotation);

					TestTrue("chose aligned", Polarity == EPolySnapPolarity::Aligned);
					TestEqual("dihedral", Dihedral, 175.0, 1.0e-3);
					TestEqual("no rotation left to do", RequiredRotation, 0.0, 1.0e-3);
				});

			It("picks the flipped polarity when the piece is turned over",
				[this]()
				{
					const FTransform HeldSocketLocal = PrimarySocketLocal();
					const FPolySnapSocketBasis Anchor = FPolySnapGeometry::BasisFromTransform(PrimarySocketLocal());

					const FTransform NearFlipped = FPolySnapGeometry::SolvePieceTransform(HeldSocketLocal, Anchor,
						EPolySnapPolarity::Flipped, 200.0);

					EPolySnapPolarity Polarity = EPolySnapPolarity::Aligned;
					double Dihedral = 0.0;
					double RequiredRotation = 0.0;
					FPolySnapGeometry::SolveNearestPlacement(HeldSocketLocal, NearFlipped, Anchor, Polarity, Dihedral,
						RequiredRotation);

					TestTrue("chose flipped", Polarity == EPolySnapPolarity::Flipped);
					TestEqual("dihedral", Dihedral, 200.0, 1.0e-3);
				});
		});

	Describe("angle wrapping",
		[this]()
		{
			It("maps everything into [0, 360)",
				[this]()
				{
					TestEqual("negative", FPolySnapGeometry::WrapDegrees(-90.0), 270.0, 1.0e-9);
					TestEqual("full turn", FPolySnapGeometry::WrapDegrees(360.0), 0.0, 1.0e-9);
					TestEqual("over a turn", FPolySnapGeometry::WrapDegrees(450.0), 90.0, 1.0e-9);
				});
		});
}

#endif // WITH_DEV_AUTOMATION_TESTS
