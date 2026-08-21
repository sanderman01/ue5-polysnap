// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "Misc/AutomationTest.h"
#include "PolySnapGeometry.h"
#include "PolySnapTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FPolySnapSocketAxesSpec, "PolySnap.SocketAxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Half of the calibration panel's 2000 mm edge, in Unreal units. CONVENTIONS.md section 7. */
static constexpr double HalfPanelUu = 100.0;

/** The socket on the +X edge of a panel authored per CONVENTIONS.md: unrotated, at (100, 0, 0). */
static FTransform PrimarySocketLocal()
{
	return FTransform(FRotator::ZeroRotator, FVector(HalfPanelUu, 0.0, 0.0));
}

static FPolySnapSocketAxes MakeAxes(EPolySnapSocketAxis Outward, EPolySnapSocketAxis Tangent,
	EPolySnapSocketAxis Normal)
{
	FPolySnapSocketAxes Axes;
	Axes.OutwardAxis = Outward;
	Axes.TangentAxis = Tangent;
	Axes.NormalAxis = Normal;

	return Axes;
}

/** Every mapping Validate accepts, found by trying all 216 rather than by asserting the answer. */
static TArray<FPolySnapSocketAxes> LegalMappings()
{
	constexpr uint8 Last = static_cast<uint8>(EPolySnapSocketAxis::MinusZ);

	TArray<FPolySnapSocketAxes> Legal;
	for (uint8 Outward = 0; Outward <= Last; ++Outward)
	{
		for (uint8 Tangent = 0; Tangent <= Last; ++Tangent)
		{
			for (uint8 Normal = 0; Normal <= Last; ++Normal)
			{
				const FPolySnapSocketAxes Candidate = MakeAxes(static_cast<EPolySnapSocketAxis>(Outward),
					static_cast<EPolySnapSocketAxis>(Tangent), static_cast<EPolySnapSocketAxis>(Normal));

				if (Candidate.Validate())
				{
					Legal.Add(Candidate);
				}
			}
		}
	}

	return Legal;
}

/** Labels an assertion, so a failure says which of the 24 mappings broke. */
static FString MappingName(const FPolySnapSocketAxes& Axes)
{
	return FString::Printf(TEXT("Outward %s / Tangent %s / Normal %s"), PolySnapAxisToString(Axes.OutwardAxis),
		PolySnapAxisToString(Axes.TangentAxis), PolySnapAxisToString(Axes.NormalAxis));
}

END_DEFINE_SPEC(FPolySnapSocketAxesSpec)

void FPolySnapSocketAxesSpec::Define()
{
	Describe("the default mapping",
		[this]()
		{
			It("is CONVENTIONS.md section 2's table",
				[this]()
				{
					const FPolySnapSocketAxes Default;

					TestEqual("outward", PolySnapAxisToString(Default.OutwardAxis), TEXT("+X"));
					TestEqual("tangent", PolySnapAxisToString(Default.TangentAxis), TEXT("-Y"));
					TestEqual("normal", PolySnapAxisToString(Default.NormalAxis), TEXT("+Z"));
				});

			It("corrects by identity, so the default path is untouched",
				[this]()
				{
					// The regression guard for the whole feature: every existing spec and every
					// existing snap must be reading exactly the transform it read before.
					TestTrue("identity", FPolySnapGeometry::AxisCorrection(FPolySnapSocketAxes()).IsIdentity(1.0e-8));
				});

			It("leaves a socket transform untouched",
				[this]()
				{
					const FTransform Raw(FRotator(10.0, -70.0, 33.0), FVector(12.0, -4.0, 8.0));
					const FTransform Canonical =
						FPolySnapGeometry::Canonicalise(Raw, FPolySnapGeometry::AxisCorrection(FPolySnapSocketAxes()));

					TestTrue("unchanged", Canonical.Equals(Raw, 1.0e-6));
				});
		});

	Describe("canonicalising",
		[this]()
		{
			It("is the correction applied first, in the socket's own frame",
				[this]()
				{
					// Canonicalise is written out by hand rather than composed, because it runs
					// once per socket per tick. This is the assertion that the hand-written form
					// still means FTransform(C) * Raw -- FTransform composes left to right while
					// FQuat composes right to left, and that is an easy sign to get backwards.
					const FTransform Raw(FRotator(-22.0, 51.0, 7.0), FVector(3.0, 91.0, -18.0));

					for (const FPolySnapSocketAxes& Axes : LegalMappings())
					{
						const FQuat Correction = FPolySnapGeometry::AxisCorrection(Axes);

						TestTrue(MappingName(Axes), FPolySnapGeometry::Canonicalise(Raw, Correction)
														.Equals(FTransform(Correction) * Raw, 1.0e-6));
					}
				});

			It("never moves the socket",
				[this]()
				{
					const FTransform Raw(FRotator(-22.0, 51.0, 7.0), FVector(3.0, 91.0, -18.0));

					for (const FPolySnapSocketAxes& Axes : LegalMappings())
					{
						const FTransform Canonical =
							FPolySnapGeometry::Canonicalise(Raw, FPolySnapGeometry::AxisCorrection(Axes));

						TestEqual(MappingName(Axes), Canonical.GetLocation(), Raw.GetLocation(), 1.0e-6f);
					}
				});

			It("drops the socket's scale without disturbing its frame",
				[this]()
				{
					// The chokepoint every runtime socket transform passes through, and therefore
					// where scale leaves the system. CONVENTIONS.md section 2: a socket may carry
					// any uniform scale and nothing downstream may be able to tell.
					const FTransform Unscaled(FRotator(-22.0, 51.0, 7.0), FVector(3.0, 91.0, -18.0));
					const FTransform Scaled(FQuat(FRotator(-22.0, 51.0, 7.0)), FVector(3.0, 91.0, -18.0), FVector(4.0));

					for (const FPolySnapSocketAxes& Axes : LegalMappings())
					{
						const FQuat Correction = FPolySnapGeometry::AxisCorrection(Axes);

						TestTrue(MappingName(Axes),
							FPolySnapGeometry::Canonicalise(Scaled, Correction)
								.Equals(FPolySnapGeometry::Canonicalise(Unscaled, Correction), 1.0e-6));
					}
				});
		});

	Describe("a legal mapping",
		[this]()
		{
			It("is one of the 24 rotations of a cube",
				[this]()
				{
					// 6 choices of Outward, 4 perpendicular Tangents, and Normal forced by those
					// two. If this number moves, Validate has stopped meaning what it says.
					TestEqual("count", LegalMappings().Num(), 24);
				});

			It("corrects by a proper rotation",
				[this]()
				{
					for (const FPolySnapSocketAxes& Axes : LegalMappings())
					{
						TestTrue(MappingName(Axes), FPolySnapGeometry::AxisCorrection(Axes).IsNormalized());
					}
				});

			It("delivers all three declared roles exactly",
				[this]()
				{
					// The reason all three axes are declared rather than two. Canonicalising an
					// unrotated socket has to reproduce the declared directions -- including the
					// Normal's sign, which is the one a laxer design would have silently dropped.
					for (const FPolySnapSocketAxes& Axes : LegalMappings())
					{
						const FPolySnapSocketBasis Basis =
							FPolySnapGeometry::BasisFromTransform(FPolySnapGeometry::Canonicalise(FTransform::Identity,
								FPolySnapGeometry::AxisCorrection(Axes)));

						TestEqual(MappingName(Axes)
								  + TEXT(" outward"), Basis.Outward, PolySnapAxisToVector(Axes.OutwardAxis), 1.0e-6f);
						TestEqual(MappingName(Axes)
								  + TEXT(" tangent"), Basis.Tangent, PolySnapAxisToVector(Axes.TangentAxis), 1.0e-6f);
						TestEqual(MappingName(Axes)
								  + TEXT(" normal"), Basis.Normal, PolySnapAxisToVector(Axes.NormalAxis), 1.0e-6f);
					}
				});

			It("keeps the triad right-handed as a physical arrangement",
				[this]()
				{
					for (const FPolySnapSocketAxes& Axes : LegalMappings())
					{
						const FPolySnapSocketBasis Basis = FPolySnapGeometry::BasisFromTransform(
							FPolySnapGeometry::Canonicalise(FTransform(FRotator(15.0, 40.0, -25.0)),
								FPolySnapGeometry::AxisCorrection(Axes)));

						// The invariant MatedBasis assumes. Every mapping has to land on it, or
						// the mating solve would be reading a basis it cannot rebuild.
						TestEqual(MappingName(Axes), FVector::CrossProduct(Basis.Tangent, Basis.Outward), Basis.Normal,
							1.0e-6f);
					}
				});
		});

	Describe("an impossible mapping",
		[this]()
		{
			It("is rejected when two roles name the same axis",
				[this]()
				{
					FString Error;
					const FPolySnapSocketAxes Axes =
						MakeAxes(EPolySnapSocketAxis::PlusX, EPolySnapSocketAxis::MinusX, EPolySnapSocketAxis::PlusZ);

					TestFalse("rejected", Axes.Validate(&Error));
					TestTrue("explains itself", Error.Len() > 0);
				});

			It("is rejected when the Normal is on the side the other two do not put it",
				[this]()
				{
					// Outward +X with Tangent +Y is a perfectly good pair -- it is what a socket
					// authored directly in Unreal looks like -- but it puts the Normal on -Z. The
					// declaration below asks for a triad no socket frame can have. Accepting it
					// and honouring two of the three would leave a dropdown that does nothing.
					FString Error;
					const FPolySnapSocketAxes Axes =
						MakeAxes(EPolySnapSocketAxis::PlusX, EPolySnapSocketAxis::PlusY, EPolySnapSocketAxis::PlusZ);

					TestFalse("rejected", Axes.Validate(&Error));
					TestTrue("names the axis to flip", Error.Contains(TEXT("-Z")));
				});

			It("corrects by identity rather than by a singular frame",
				[this]()
				{
					// A repeated axis makes the third row a zero cross product. Left to reach
					// FMatrix, that becomes a normalised quaternion of nonsense and parts land
					// at garbage transforms; a part that simply did not move is far easier to see.
					const FPolySnapSocketAxes Axes =
						MakeAxes(EPolySnapSocketAxis::PlusX, EPolySnapSocketAxis::MinusX, EPolySnapSocketAxis::PlusZ);

					TestTrue("identity", FPolySnapGeometry::AxisCorrection(Axes).IsIdentity(1.0e-8));
				});
		});

	Describe("a part authored to a non-default mapping",
		[this]()
		{
			It("stores the calibration socket with its declared axes on the panel's real directions",
				[this]()
				{
					// What the same physical socket looks like on disk under each mapping. The
					// panel has not changed: its edge is still the +X one, so whichever axis the
					// mapping calls Outward has to be the one pointing along world +X.
					for (const FPolySnapSocketAxes& Axes : LegalMappings())
					{
						const FTransform Correction(FPolySnapGeometry::AxisCorrection(Axes));
						const FTransform Raw = Correction.Inverse() * PrimarySocketLocal();

						TestEqual(MappingName(Axes), Raw.TransformVectorNoScale(PolySnapAxisToVector(Axes.OutwardAxis)),
							FVector::ForwardVector, 1.0e-6f);
					}
				});

			It("snaps to exactly the place the default mapping does",
				[this]()
				{
					// CONVENTIONS.md section 7 check 4, once per mapping: panel A at the origin,
					// panel B placed coplanar, and B must land at (200, 0, 0) yawed 180 degrees.
					// The answer must not depend on how the asset spelled its axes.
					for (const FPolySnapSocketAxes& Axes : LegalMappings())
					{
						const FTransform Correction(FPolySnapGeometry::AxisCorrection(Axes));
						const FTransform Raw = Correction.Inverse() * PrimarySocketLocal();
						const FTransform Canonical =
							FPolySnapGeometry::Canonicalise(Raw, FPolySnapGeometry::AxisCorrection(Axes));

						TestTrue(MappingName(Axes)
								 + TEXT(" round trip"), Canonical.Equals(PrimarySocketLocal(), 1.0e-6));

						const FPolySnapSocketBasis Anchor = FPolySnapGeometry::BasisFromTransform(Canonical);
						const FTransform Solved =
							FPolySnapGeometry::SolvePartTransform(Canonical, Anchor, EPolySnapPolarity::Aligned, 180.0);

						TestEqual(MappingName(Axes)
								  + TEXT(" location"), Solved.GetLocation(), FVector(2.0 * HalfPanelUu, 0.0, 0.0),
									  1.0e-4f);
						TestEqual(MappingName(Axes) + TEXT(" yaw"), Solved.Rotator().Yaw, 180.0, 1.0e-3);
					}
				});
		});
}

#endif // WITH_DEV_AUTOMATION_TESTS
