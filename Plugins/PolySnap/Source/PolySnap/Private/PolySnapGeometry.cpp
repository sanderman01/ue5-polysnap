// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapGeometry.h"

namespace PolySnapGeometryPrivate
{
/**
	 * Below this, a quaternion carries no usable twist about the axis and the decomposition is
	 * ambiguous. Falling back to zero is harmless: it only means the piece is not already near
	 * any particular fold, so any dihedral is equally far away.
	 */
constexpr double TwistEpsilon = UE_DOUBLE_SMALL_NUMBER;
}

double FPolySnapGeometry::WrapDegrees(double Degrees)
{
	double Wrapped = FMath::Fmod(Degrees, 360.0);
	if (Wrapped < 0.0)
	{
		Wrapped += 360.0;
	}

	return Wrapped;
}

FPolySnapSocketBasis FPolySnapGeometry::BasisFromTransform(const FTransform& SocketTransform)
{
	FPolySnapSocketBasis Basis;
	Basis.Location = SocketTransform.GetLocation();
	Basis.Outward = SocketTransform.GetUnitAxis(EAxis::X);

	// CONVENTIONS.md section 2: the FBX conversion negates Y, so Tangent lands on -Y. Deriving it
	// as +Y runs every polarity test in the snapper backwards.
	Basis.Tangent = -SocketTransform.GetUnitAxis(EAxis::Y);
	Basis.Normal = SocketTransform.GetUnitAxis(EAxis::Z);

	return Basis;
}

FTransform FPolySnapGeometry::TransformFromBasis(const FPolySnapSocketBasis& Basis)
{
	// Rows are the basis axes, read back through the same convention: X is Outward, Y is the
	// negated Tangent, Z is Normal.
	const FMatrix AxisMatrix(Basis.Outward, -Basis.Tangent, Basis.Normal, Basis.Location);
	return FTransform(AxisMatrix);
}

double FPolySnapGeometry::DihedralDegrees(const FPolySnapSocketBasis& Anchor, const FPolySnapSocketBasis& Other)
{
	const double SinComponent = FVector::CrossProduct(Anchor.Outward, Other.Outward) | Anchor.Tangent;
	const double CosComponent = Anchor.Outward | Other.Outward;

	return WrapDegrees(FMath::RadiansToDegrees(FMath::Atan2(SinComponent, CosComponent)));
}

FPolySnapSocketBasis FPolySnapGeometry::MatedBasis(const FPolySnapSocketBasis& Anchor, EPolySnapPolarity Polarity,
	double InDihedralDegrees)
{
	const FQuat Swing(Anchor.Tangent, FMath::DegreesToRadians(InDihedralDegrees));

	FPolySnapSocketBasis Mated;
	Mated.Location = Anchor.Location;
	Mated.Tangent = MatedTangent(Anchor, Polarity);
	Mated.Outward = Swing.RotateVector(Anchor.Outward);

	// Unreal's cross product is the plain right-hand formula while its coordinate system is
	// left-handed, so the physically right-handed triad computes as Normal == Tangent ^ Outward.
	Mated.Normal = FVector::CrossProduct(Mated.Tangent, Mated.Outward);

	return Mated;
}

FTransform FPolySnapGeometry::SolvePieceTransform(const FTransform& HeldSocketLocal, const FPolySnapSocketBasis& Anchor,
	EPolySnapPolarity Polarity, double InDihedralDegrees)
{
	const FTransform DesiredSocketWorld = TransformFromBasis(MatedBasis(Anchor, Polarity, InDihedralDegrees));

	// SocketWorld == SocketLocal * PieceWorld, so the piece transform is what remains once the
	// socket's own offset is divided out. FTransform composes left to right: A * B applies A
	// first, which is the opposite order to FQuat.
	return HeldSocketLocal.Inverse() * DesiredSocketWorld;
}

double FPolySnapGeometry::AngleBetweenDegrees(const FQuat& A, const FQuat& B)
{
	const double CosHalfAngle = FMath::Abs(A | B);
	return FMath::RadiansToDegrees(2.0 * FMath::Acos(FMath::Clamp(CosHalfAngle, -1.0, 1.0)));
}

double FPolySnapGeometry::NearestDihedralDegrees(const FTransform& HeldSocketLocal,
	const FTransform& CurrentPieceTransform, const FPolySnapSocketBasis& Anchor, EPolySnapPolarity Polarity,
	double& OutRequiredRotationDegrees)
{
	using namespace PolySnapGeometryPrivate;

	const FQuat CurrentSocketRotation = (HeldSocketLocal * CurrentPieceTransform).GetRotation();
	const FQuat BaseRotation = TransformFromBasis(MatedBasis(Anchor, Polarity, 0.0)).GetRotation();

	// The admissible poses are BaseRotation followed by a rotation about the world axis
	// Anchor.Tangent, so the family is a left-multiplication: R(theta) = Swing(theta) * Base.
	// The theta closest to where the piece already is, is therefore the twist of the relative
	// rotation about that axis. FQuat composes right to left: C = A * B applies B first.
	FQuat Relative = CurrentSocketRotation * BaseRotation.Inverse();
	Relative.Normalize();

	const FVector Axis = Anchor.Tangent.GetSafeNormal();
	const double TwistProjection = FVector(Relative.X, Relative.Y, Relative.Z) | Axis;

	double Dihedral = 0.0;
	if (FMath::Abs(TwistProjection) > TwistEpsilon || FMath::Abs(Relative.W) > TwistEpsilon)
	{
		Dihedral = WrapDegrees(FMath::RadiansToDegrees(2.0 * FMath::Atan2(TwistProjection, Relative.W)));
	}

	const FQuat SolvedRotation = TransformFromBasis(MatedBasis(Anchor, Polarity, Dihedral)).GetRotation();
	OutRequiredRotationDegrees = AngleBetweenDegrees(SolvedRotation, CurrentSocketRotation);

	return Dihedral;
}

void FPolySnapGeometry::SolveNearestPlacement(const FTransform& HeldSocketLocal,
	const FTransform& CurrentPieceTransform, const FPolySnapSocketBasis& Anchor, EPolySnapPolarity& OutPolarity,
	double& OutDihedralDegrees, double& OutRequiredRotationDegrees)
{
	double AlignedRotation = 0.0;
	const double AlignedDihedral = NearestDihedralDegrees(HeldSocketLocal, CurrentPieceTransform, Anchor,
		EPolySnapPolarity::Aligned, AlignedRotation);

	double FlippedRotation = 0.0;
	const double FlippedDihedral = NearestDihedralDegrees(HeldSocketLocal, CurrentPieceTransform, Anchor,
		EPolySnapPolarity::Flipped, FlippedRotation);

	// Both polarities are always admissible -- a flip is a proper 180 degree rotation the player
	// could perform by hand, not a mirror image -- so the choice is settled by which costs less
	// rotation from where the piece already is. That is the whole of the flip decision.
	if (FlippedRotation < AlignedRotation)
	{
		OutPolarity = EPolySnapPolarity::Flipped;
		OutDihedralDegrees = FlippedDihedral;
		OutRequiredRotationDegrees = FlippedRotation;
	}
	else
	{
		OutPolarity = EPolySnapPolarity::Aligned;
		OutDihedralDegrees = AlignedDihedral;
		OutRequiredRotationDegrees = AlignedRotation;
	}
}
