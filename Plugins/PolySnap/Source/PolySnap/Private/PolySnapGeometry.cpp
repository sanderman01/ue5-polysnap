// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapGeometry.h"

namespace PolySnapGeometryPrivate
{
/**
	 * Below this, a quaternion carries no usable twist about the axis and the decomposition is
	 * ambiguous. Falling back to zero is harmless: it only means the part is not already near
	 * any particular fold, so any dihedral is equally far away.
	 */
constexpr double TwistEpsilon = UE_DOUBLE_SMALL_NUMBER;

/**
	 * How far from a uniformly scaled rotation a socket's frame may be before it is called
	 * non-uniform or mirrored. Applied to quantities already divided by the frame's own scale, so
	 * it means the same thing at every scale -- see IsUniformlyScaledRotation.
	 */
constexpr double ConformalTolerance = 1.0e-4;
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

FQuat FPolySnapGeometry::AxisCorrection(const FPolySnapSocketAxes& Axes)
{
	if (!Axes.Validate())
	{
		return FQuat::Identity;
	}

	// FMatrix's four-vector constructor fills M[0], M[1], M[2] in turn, and Unreal multiplies row
	// vectors, so row N is where canonical axis N ends up. The rows are TransformFromBasis read
	// forwards: X is Outward, Y is the negated Tangent, Z is Normal. Validate has already
	// guaranteed the three are a basis, so this is a rotation and not merely close to one.
	const FMatrix AxisMatrix(PolySnapAxisToVector(Axes.OutwardAxis), -PolySnapAxisToVector(Axes.TangentAxis),
		Axes.DerivedNormal(), FVector::ZeroVector);

	return AxisMatrix.ToQuat();
}

FTransform FPolySnapGeometry::WithoutScale(const FTransform& SocketTransform)
{
	return FTransform(SocketTransform.GetRotation(), SocketTransform.GetTranslation());
}

bool FPolySnapGeometry::IsUniformlyScaledRotation(const FTransform& Transform, double& OutUniformScale,
	double& OutDeterminant)
{
	using namespace PolySnapGeometryPrivate;

	const FMatrix Matrix = Transform.ToMatrixWithScale();

	const FVector Row0(Matrix.M[0][0], Matrix.M[0][1], Matrix.M[0][2]);
	const FVector Row1(Matrix.M[1][0], Matrix.M[1][1], Matrix.M[1][2]);
	const FVector Row2(Matrix.M[2][0], Matrix.M[2][1], Matrix.M[2][2]);

	OutUniformScale = Row0.Size();
	OutDeterminant = Row0 | FVector::CrossProduct(Row1, Row2);

	// A frame with no length left has no axes to test, and every ratio below would divide by zero.
	if (OutUniformScale <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	// Every comparison is against a quantity divided by the scale it grows with: row lengths by
	// s, the determinant by s cubed. Testing the raw values instead would make the tolerance mean
	// something different at every scale -- silently strict on a 0.1x socket, silently loose on a
	// 10x one -- which is the very dependence this check exists to remove.
	const double InverseScale = 1.0 / OutUniformScale;

	const bool bUniform = FMath::IsNearlyEqual(Row1.Size() * InverseScale, 1.0, ConformalTolerance)
					   && FMath::IsNearlyEqual(Row2.Size() * InverseScale, 1.0, ConformalTolerance);

	// There is deliberately no perpendicularity test. FTransform stores its rotation as an FQuat,
	// so a shear has nowhere to live: whatever is fed in, the rows of ToMatrixWithScale come back
	// as scaled axes of a genuine rotation and are perpendicular by construction. Nor could an
	// authored socket be sheared in the first place -- UStaticMeshSocket stores an FRotator and an
	// FVector. A dot-product check here would look like a check while testing nothing, the same
	// trap CONVENTIONS.md section 6 warns about for GetUnitAxis.
	//
	// Plus one, not merely unit magnitude. A mirrored socket has determinant -1 and would still
	// hand BasisFromTransform a usable-looking triad, because GetUnitAxis reads the quaternion and
	// never sees the flip -- so this is the only place the handedness error can be caught at all.
	const bool bRightHanded =
		FMath::IsNearlyEqual(OutDeterminant * InverseScale * InverseScale * InverseScale, 1.0, ConformalTolerance);

	return bUniform && bRightHanded;
}

FTransform FPolySnapGeometry::Canonicalise(const FTransform& RawSocketTransform, const FQuat& Correction)
{
	// FTransform(Correction) * RawSocketTransform, written out. The correction applies first, in
	// the socket's own frame, which is what relabelling its axes means -- and FQuat composes right
	// to left where FTransform composes left to right, hence the order. Spelling it out keeps the
	// location untouched by construction rather than by cancellation, and keeps a per-socket,
	// per-tick call off FTransform's general multiply path.
	//
	// Unit scale, deliberately. The raw transform arrives carrying the socket's own scale and its
	// component's, and neither says anything about the edge -- so this is where both leave. Note
	// that the correction is a rotation and would have carried a non-uniform scale through
	// unrotated, quietly attaching it to the wrong axes; there is nothing to get wrong once the
	// scale is gone.
	return FTransform(RawSocketTransform.GetRotation() * Correction, RawSocketTransform.GetTranslation());
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

FTransform FPolySnapGeometry::SolvePartTransform(const FTransform& HeldSocketLocal, const FPolySnapSocketBasis& Anchor,
	EPolySnapPolarity Polarity, double InDihedralDegrees)
{
	const FTransform DesiredSocketWorld = TransformFromBasis(MatedBasis(Anchor, Polarity, InDihedralDegrees));

	// SocketWorld == SocketLocal * PartWorld, so the part transform is what remains once the
	// socket's own offset is divided out. FTransform composes left to right: A * B applies A
	// first, which is the opposite order to FQuat.
	//
	// Scale is dropped before inverting, not after. DesiredSocketWorld is unit-scaled by
	// construction, so inverting a socket scaled by s would leave the part scaled by 1/s AND
	// displaced, because Inverse divides the translation by s as well -- an offset that s was
	// never applied to on the way in. Canonicalise already strips scale on the runtime path;
	// this repeats it because a caller building HeldSocketLocal from GetRelativeTransform can
	// reintroduce scale from the part side without passing through Canonicalise at all.
	return WithoutScale(HeldSocketLocal).Inverse() * DesiredSocketWorld;
}

double FPolySnapGeometry::AngleBetweenDegrees(const FQuat& A, const FQuat& B)
{
	const double CosHalfAngle = FMath::Abs(A | B);
	return FMath::RadiansToDegrees(2.0 * FMath::Acos(FMath::Clamp(CosHalfAngle, -1.0, 1.0)));
}

double FPolySnapGeometry::NearestDihedralDegrees(const FTransform& HeldSocketLocal,
	const FTransform& CurrentPartTransform, const FPolySnapSocketBasis& Anchor, EPolySnapPolarity Polarity,
	double& OutRequiredRotationDegrees)
{
	using namespace PolySnapGeometryPrivate;

	// (HeldSocketLocal * CurrentPartTransform).GetRotation(), composed directly. FTransform's
	// multiply sets Out.Rotation = B.Rotation * A.Rotation, so this is the same quaternion for an
	// unscaled pair and an exact one for any other: a non-uniform scale pushes FTransform onto its
	// matrix path, whose extracted rotation is a shear-contaminated approximation.
	const FQuat CurrentSocketRotation = CurrentPartTransform.GetRotation() * HeldSocketLocal.GetRotation();
	const FQuat BaseRotation = TransformFromBasis(MatedBasis(Anchor, Polarity, 0.0)).GetRotation();

	// The admissible poses are BaseRotation followed by a rotation about the world axis
	// Anchor.Tangent, so the family is a left-multiplication: R(theta) = Swing(theta) * Base.
	// The theta closest to where the part already is, is therefore the twist of the relative
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

void FPolySnapGeometry::SolveNearestPlacement(const FTransform& HeldSocketLocal, const FTransform& CurrentPartTransform,
	const FPolySnapSocketBasis& Anchor, EPolySnapPolarity& OutPolarity, double& OutDihedralDegrees,
	double& OutRequiredRotationDegrees)
{
	double AlignedRotation = 0.0;
	const double AlignedDihedral = NearestDihedralDegrees(HeldSocketLocal, CurrentPartTransform, Anchor,
		EPolySnapPolarity::Aligned, AlignedRotation);

	double FlippedRotation = 0.0;
	const double FlippedDihedral = NearestDihedralDegrees(HeldSocketLocal, CurrentPartTransform, Anchor,
		EPolySnapPolarity::Flipped, FlippedRotation);

	// Both polarities are always admissible -- a flip is a proper 180 degree rotation the player
	// could perform by hand, not a mirror image -- so the choice is settled by which costs less
	// rotation from where the part already is. That is the whole of the flip decision.
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
