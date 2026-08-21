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

/**
 * The same socket with a uniform scale on it -- a Blender empty enlarged so it is easier to click.
 * The frame is identical; only the number PolySnap is meant to ignore differs. CONVENTIONS.md
 * section 2.
 *
 * The offset matters as much as the scale here: a socket at the origin would hide the misposition
 * half of a scale bug, because there would be no lever arm for an inverse to divide.
 */
static FTransform ScaledPrimarySocketLocal(double UniformScale)
{
	return FTransform(FQuat::Identity, FVector(HalfPanelUu, 0.0, 0.0), FVector(UniformScale));
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

	Describe("the solved part transform",
		[this]()
		{
			It("puts the held socket exactly on the anchor",
				[this]()
				{
					const FTransform HeldSocketLocal = PrimarySocketLocal();
					const FPolySnapSocketBasis Anchor = FPolySnapGeometry::BasisFromTransform(
						FTransform(FRotator(5.0, 61.0, -14.0), FVector(300.0, -120.0, 45.0)));

					const FTransform Solved = FPolySnapGeometry::SolvePartTransform(HeldSocketLocal, Anchor,
						EPolySnapPolarity::Aligned, 138.19);
					const FPolySnapSocketBasis Placed = FPolySnapGeometry::BasisFromTransform(HeldSocketLocal * Solved);

					// The anchor's mating conditions hold to float precision. That exactness is the
					// invariant that tells a geometry bug from an accumulation artefact.
					TestEqual("position", Placed.Location, Anchor.Location, 1.0e-4f);
					TestEqual("tangent", Placed.Tangent, -Anchor.Tangent, 1.0e-4f);
					TestEqual("dihedral", FPolySnapGeometry::DihedralDegrees(Anchor, Placed), 138.19, 1.0e-4);
				});

			It("leaves the part unscaled",
				[this]()
				{
					const FTransform Solved = FPolySnapGeometry::SolvePartTransform(PrimarySocketLocal(),
						FPolySnapGeometry::BasisFromTransform(FTransform(FRotator(0.0, 45.0, 0.0))),
						EPolySnapPolarity::Aligned, 90.0);

					TestEqual("scale", Solved.GetScale3D(), FVector::OneVector, 1.0e-4f);
				});

			It("leaves the part unscaled even when the held socket is scaled",
				[this]()
				{
					// The inverse of a socket scaled by s carries 1/s, and the desired pose is
					// unit-scaled, so an unguarded solve hands the part a scale of 1/s.
					const FTransform Solved = FPolySnapGeometry::SolvePartTransform(ScaledPrimarySocketLocal(3.0),
						FPolySnapGeometry::BasisFromTransform(FTransform(FRotator(0.0, 45.0, 0.0))),
						EPolySnapPolarity::Aligned, 90.0);

					TestEqual("scale", Solved.GetScale3D(), FVector::OneVector, 1.0e-4f);
				});

			It("places the part identically whatever scale the held socket carries",
				[this]()
				{
					// The load-bearing one. Scale on a socket must be inert, not merely divided
					// back out of the scale channel: Inverse divides the translation by s as well,
					// and s was never applied to the socket's own offset on the way in. A solver
					// that only fixed the scale would still put the part in the wrong place, and
					// that shows up as a snap residual rather than as a visibly scaled part.
					const FPolySnapSocketBasis Anchor = FPolySnapGeometry::BasisFromTransform(
						FTransform(FRotator(5.0, 61.0, -14.0), FVector(300.0, -120.0, 45.0)));

					const FTransform Unscaled = FPolySnapGeometry::SolvePartTransform(PrimarySocketLocal(), Anchor,
						EPolySnapPolarity::Aligned, 138.19);
					const FTransform Scaled = FPolySnapGeometry::SolvePartTransform(ScaledPrimarySocketLocal(2.5),
						Anchor, EPolySnapPolarity::Aligned, 138.19);

					TestEqual("location", Scaled.GetLocation(), Unscaled.GetLocation(), 1.0e-4f);
					TestTrue("rotation", Scaled.GetRotation().Equals(Unscaled.GetRotation(), 1.0e-4));
					TestEqual("scale", Scaled.GetScale3D(), Unscaled.GetScale3D(), 1.0e-4f);
				});

			It("picks the same placement whatever scale the held socket carries",
				[this]()
				{
					// SolveNearestPlacement reads the socket rotation out of a product with the
					// part transform, which is the other route a scale could take into the answer.
					const FPolySnapSocketBasis Anchor = FPolySnapGeometry::BasisFromTransform(
						FTransform(FRotator(5.0, 61.0, -14.0), FVector(300.0, -120.0, 45.0)));
					const FTransform CurrentPart(FRotator(20.0, -70.0, 35.0), FVector(280.0, -90.0, 60.0));

					EPolySnapPolarity UnscaledPolarity = EPolySnapPolarity::Aligned;
					double UnscaledDihedral = 0.0;
					double UnscaledRotation = 0.0;
					FPolySnapGeometry::SolveNearestPlacement(PrimarySocketLocal(), CurrentPart, Anchor,
						UnscaledPolarity, UnscaledDihedral, UnscaledRotation);

					EPolySnapPolarity ScaledPolarity = EPolySnapPolarity::Aligned;
					double ScaledDihedral = 0.0;
					double ScaledRotation = 0.0;
					FPolySnapGeometry::SolveNearestPlacement(ScaledPrimarySocketLocal(2.5), CurrentPart, Anchor,
						ScaledPolarity, ScaledDihedral, ScaledRotation);

					TestTrue("polarity", ScaledPolarity == UnscaledPolarity);
					TestEqual("dihedral", ScaledDihedral, UnscaledDihedral, 1.0e-4);
					TestEqual("required rotation", ScaledRotation, UnscaledRotation, 1.0e-4);
				});

			It("reproduces the calibration seam from panel A's own socket",
				[this]()
				{
					// The snapper's answer to CONVENTIONS.md section 7 check 4: given panel A at the
					// origin, placing panel B coplanar must put it at (200, 0, 0) yawed 180 degrees.
					const FPolySnapSocketBasis Anchor = FPolySnapGeometry::BasisFromTransform(PrimarySocketLocal());
					const FTransform Solved = FPolySnapGeometry::SolvePartTransform(PrimarySocketLocal(), Anchor,
						EPolySnapPolarity::Aligned, 180.0);

					TestEqual("location", Solved.GetLocation(), FVector(2.0 * HalfPanelUu, 0.0, 0.0), 1.0e-4f);
					TestEqual("rotation", Solved.Rotator().Yaw, 180.0, 1.0e-3);
				});
		});

	Describe("the least-rotation placement",
		[this]()
		{
			It("picks the polarity nearer the orientation the part is already in",
				[this]()
				{
					const FTransform HeldSocketLocal = PrimarySocketLocal();
					const FPolySnapSocketBasis Anchor = FPolySnapGeometry::BasisFromTransform(PrimarySocketLocal());

					// Start from the pose an aligned coplanar seam would need, nudged slightly. The
					// player turned the part roughly this way, so the snap must commit to that reading.
					const FTransform NearAligned = FPolySnapGeometry::SolvePartTransform(HeldSocketLocal, Anchor,
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

			It("picks the flipped polarity when the part is turned over",
				[this]()
				{
					const FTransform HeldSocketLocal = PrimarySocketLocal();
					const FPolySnapSocketBasis Anchor = FPolySnapGeometry::BasisFromTransform(PrimarySocketLocal());

					const FTransform NearFlipped = FPolySnapGeometry::SolvePartTransform(HeldSocketLocal, Anchor,
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

	Describe("dropping scale",
		[this]()
		{
			It("keeps the rotation and the translation",
				[this]()
				{
					const FTransform Scaled(FQuat(FRotator(-22.0, 51.0, 7.0)), FVector(3.0, 91.0, -18.0), FVector(4.0));
					const FTransform Stripped = FPolySnapGeometry::WithoutScale(Scaled);

					TestEqual("location", Stripped.GetLocation(), Scaled.GetLocation(), 1.0e-6f);
					TestTrue("rotation", Stripped.GetRotation().Equals(Scaled.GetRotation(), 1.0e-6));
					TestEqual("scale", Stripped.GetScale3D(), FVector::OneVector, 1.0e-6f);
				});

			It("leaves the socket's named directions where they were",
				[this]()
				{
					// BasisFromTransform reads unit axes, so this can only fail if WithoutScale
					// disturbs the quaternion -- which is exactly what a matrix round trip would
					// risk, and why it is built from the rotation directly.
					const FTransform Scaled(FQuat(FRotator(-22.0, 51.0, 7.0)), FVector(3.0, 91.0, -18.0),
						FVector(0.25));

					const FPolySnapSocketBasis Before = FPolySnapGeometry::BasisFromTransform(Scaled);
					const FPolySnapSocketBasis After =
						FPolySnapGeometry::BasisFromTransform(FPolySnapGeometry::WithoutScale(Scaled));

					TestEqual("outward", After.Outward, Before.Outward, 1.0e-6f);
					TestEqual("tangent", After.Tangent, Before.Tangent, 1.0e-6f);
					TestEqual("normal", After.Normal, Before.Normal, 1.0e-6f);
				});
		});

	Describe("the frame a socket is allowed to have",
		[this]()
		{
			It("accepts a plain rotation",
				[this]()
				{
					double UniformScale = 0.0;
					double Determinant = 0.0;

					TestTrue("accepted", FPolySnapGeometry::IsUniformlyScaledRotation(
											 FTransform(FRotator(15.0, 40.0, -25.0)), UniformScale, Determinant));
					TestEqual("scale", UniformScale, 1.0, 1.0e-6);
					TestEqual("determinant", Determinant, 1.0, 1.0e-6);
				});

			It("accepts any uniform scale, large or small",
				[this]()
				{
					// The point of the change: an empty enlarged in Blender so it is easier to
					// click is a legitimate asset. CONVENTIONS.md section 2.
					for (const double Scale : {0.01, 0.25, 2.5, 100.0})
					{
						double UniformScale = 0.0;
						double Determinant = 0.0;

						const FTransform Socket(FQuat(FRotator(15.0, 40.0, -25.0)), FVector(7.0, -3.0, 1.0),
							FVector(Scale));

						TestTrue(FString::Printf(TEXT("accepted at %g"), Scale),
							FPolySnapGeometry::IsUniformlyScaledRotation(Socket, UniformScale, Determinant));
						TestEqual(FString::Printf(TEXT("scale at %g"), Scale), UniformScale, Scale, 1.0e-6);
					}
				});

			It("rejects a non-uniform scale",
				[this]()
				{
					double UniformScale = 0.0;
					double Determinant = 0.0;

					const FTransform Socket(FQuat(FRotator(15.0, 40.0, -25.0)), FVector::ZeroVector,
						FVector(1.0, 2.0, 1.0));

					TestFalse("rejected",
						FPolySnapGeometry::IsUniformlyScaledRotation(Socket, UniformScale, Determinant));
				});

			It("rejects a mirrored frame",
				[this]()
				{
					// A mirror survives BasisFromTransform unnoticed -- GetUnitAxis reads the
					// quaternion and never sees the sign -- so this check is the only thing
					// standing between a flipped empty and every polarity test running backwards.
					double UniformScale = 0.0;
					double Determinant = 0.0;

					const FTransform Socket(FQuat(FRotator(15.0, 40.0, -25.0)), FVector::ZeroVector,
						FVector(-1.0, 1.0, 1.0));

					TestFalse("rejected",
						FPolySnapGeometry::IsUniformlyScaledRotation(Socket, UniformScale, Determinant));
					TestTrue("determinant is negative", Determinant < 0.0);
				});

			It("rejects a negative uniform scale",
				[this]()
				{
					// Uniform, and still a mirror: negating all three axes flips the handedness
					// just as one does. "Any uniform scale" means any POSITIVE uniform scale.
					double UniformScale = 0.0;
					double Determinant = 0.0;

					const FTransform Socket(FQuat::Identity, FVector::ZeroVector, FVector(-2.0));

					TestFalse("rejected",
						FPolySnapGeometry::IsUniformlyScaledRotation(Socket, UniformScale, Determinant));
				});

			It("rejects a degenerate frame",
				[this]()
				{
					double UniformScale = 0.0;
					double Determinant = 0.0;

					const FTransform Socket(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);

					TestFalse("rejected",
						FPolySnapGeometry::IsUniformlyScaledRotation(Socket, UniformScale, Determinant));
				});

			It("reaches the same verdict at every scale",
				[this]()
				{
					// The tolerance is applied to row lengths already divided by the frame's own
					// scale, so "how non-uniform is this" means the same thing everywhere. An
					// absolute tolerance would judge the identical asset differently depending on
					// how big its empty happened to be -- a scale dependence inside the very check
					// that exists to remove one. The deviation here is 2e-4 relative, twice the
					// tolerance, so it must be rejected at 0.01x and at 100x alike.
					const FQuat Rotation(FRotator(15.0, 40.0, -25.0));

					for (const double Scale : {0.01, 1.0, 100.0})
					{
						double UniformScale = 0.0;
						double Determinant = 0.0;

						const FTransform Uniform(Rotation, FVector::ZeroVector, FVector(Scale));
						TestTrue(FString::Printf(TEXT("uniform accepted at %g"), Scale),
							FPolySnapGeometry::IsUniformlyScaledRotation(Uniform, UniformScale, Determinant));

						const FTransform NearlyUniform(Rotation, FVector::ZeroVector,
							FVector(Scale, Scale * 1.0002, Scale));
						TestFalse(FString::Printf(TEXT("non-uniform rejected at %g"), Scale),
							FPolySnapGeometry::IsUniformlyScaledRotation(NearlyUniform, UniformScale, Determinant));
					}
				});
		});

	Describe("the adopt-driven dihedral",
		[this]()
		{
			// A panel hinged about an anchor on its +X edge, with a second socket on the -X edge --
			// the arrangement every closure is: one pair already mating, a second sweeping a circle
			// about the shared edge until it lands on something.
			const FPolySnapSocketBasis Anchor = FPolySnapGeometry::BasisFromTransform(FTransform::Identity);
			const FTransform AnchorSocketLocal = PrimarySocketLocal();
			const FVector SecondaryLocal(-HalfPanelUu, 0.0, 0.0);

			const auto SecondaryAt = [AnchorSocketLocal, SecondaryLocal, Anchor](double Dihedral)
			{
				return FPolySnapGeometry::SolvePartTransform(AnchorSocketLocal, Anchor, EPolySnapPolarity::Aligned,
					Dihedral)
					.TransformPosition(SecondaryLocal);
			};

			It("recovers the angle that puts the secondary socket on its target",
				[this, Anchor, AnchorSocketLocal, SecondaryLocal, SecondaryAt]()
				{
					// Closed form rather than a search, so the tolerance is float noise and not a
					// step size.
					for (const double Expected : {12.5, 90.0, 138.1897, 250.0, 359.0})
					{
						double Solved = 0.0;
						double ResidualUu = 0.0;

						TestTrue("solved",
							FPolySnapGeometry::AdoptDihedralDegrees(AnchorSocketLocal, SecondaryLocal, Anchor,
								EPolySnapPolarity::Aligned, SecondaryAt(Expected), Solved, ResidualUu));

						TestEqual(FString::Printf(TEXT("dihedral at %g"), Expected), Solved, Expected, 1.0e-6);
						TestEqual(FString::Printf(TEXT("residual at %g"), Expected), ResidualUu, 0.0, 1.0e-6);
					}
				});

			It("reports what rotation could not close as the residual",
				[this, Anchor, AnchorSocketLocal, SecondaryLocal, SecondaryAt]()
				{
					// Along the anchor's Tangent, which is the one direction hinging cannot move the
					// secondary socket in. The angle is unaffected and the whole offset survives as
					// the residual -- this is the number a connection records.
					const double ExpectedDihedral = 138.1897;
					const FVector AlongAxis = Anchor.Tangent * 3.0;

					double Solved = 0.0;
					double ResidualUu = 0.0;

					TestTrue("solved",
						FPolySnapGeometry::AdoptDihedralDegrees(AnchorSocketLocal, SecondaryLocal, Anchor,
							EPolySnapPolarity::Aligned, SecondaryAt(ExpectedDihedral) + AlongAxis, Solved, ResidualUu));

					TestEqual("dihedral", Solved, ExpectedDihedral, 1.0e-6);
					TestEqual("residual", ResidualUu, 3.0, 1.0e-6);
				});

			It("declines a secondary socket lying on the anchor axis",
				[this, Anchor, AnchorSocketLocal]()
				{
					// Such a socket sweeps a degenerate circle: every dihedral leaves it in the same
					// place, so there is no angle to read off it and answering with one would be noise
					// dressed as an answer.
					double Solved = 0.0;
					double ResidualUu = 0.0;

					TestFalse("declined",
						FPolySnapGeometry::AdoptDihedralDegrees(AnchorSocketLocal, FVector(HalfPanelUu, 0.0, 0.0),
							Anchor, EPolySnapPolarity::Aligned, FVector(0.0, 50.0, 0.0), Solved, ResidualUu));
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
