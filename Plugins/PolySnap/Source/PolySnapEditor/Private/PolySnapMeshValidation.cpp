// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapMeshValidation.h"

#include "Algo/AllOf.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "MeshDescription.h"
#include "PolySnap.h"
#include "PolySnapGeometry.h"
#include "PolySnapSettings.h"
#include "PolySnapSocketName.h"
#include "PolySnapTypes.h"
#include "StaticMeshAttributes.h"

namespace PolySnapMeshValidationPrivate
{
/** Unreal units are centimetres and the Size token is millimetres. The only place they meet. */
constexpr double UuToMm = 10.0;

/** How far from orthonormal a socket's rotation may be before it is called sheared. */
constexpr double OrthonormalTolerance = 1.0e-4;

[[nodiscard]] FTransform SocketTransform(const UStaticMeshSocket& Socket)
{
	return FTransform(Socket.RelativeRotation, Socket.RelativeLocation, Socket.RelativeScale);
}

/**
	 * CONVENTIONS.md section 6 insists this reads the raw matrix. A check written on GetUnitAxis
	 * output is orthonormal by construction -- that accessor goes through TransformVectorNoScale
	 * -- and can never fail, so it would look like a check while testing nothing.
	 */
[[nodiscard]] bool IsRotationOrthonormal(const FTransform& Transform, double& OutDeterminant)
{
	const FMatrix Matrix = Transform.ToMatrixWithScale();

	const FVector Row0(Matrix.M[0][0], Matrix.M[0][1], Matrix.M[0][2]);
	const FVector Row1(Matrix.M[1][0], Matrix.M[1][1], Matrix.M[1][2]);
	const FVector Row2(Matrix.M[2][0], Matrix.M[2][1], Matrix.M[2][2]);

	OutDeterminant = Row0 | FVector::CrossProduct(Row1, Row2);

	const bool bUnitLength = FMath::IsNearlyEqual(Row0.Size(), 1.0, OrthonormalTolerance)
						  && FMath::IsNearlyEqual(Row1.Size(), 1.0, OrthonormalTolerance)
						  && FMath::IsNearlyEqual(Row2.Size(), 1.0, OrthonormalTolerance);

	const bool bPerpendicular = FMath::IsNearlyZero(Row0 | Row1, OrthonormalTolerance)
							 && FMath::IsNearlyZero(Row1 | Row2, OrthonormalTolerance)
							 && FMath::IsNearlyZero(Row0 | Row2, OrthonormalTolerance);

	return bUnitLength && bPerpendicular && FMath::IsNearlyEqual(OutDeterminant, 1.0, OrthonormalTolerance);
}

[[nodiscard]] bool IsParallel(const FVector& A, const FVector& B, double ToleranceDegrees)
{
	const double Alignment = FMath::Abs(A.GetSafeNormal() | B.GetSafeNormal());
	const double AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Alignment, -1.0, 1.0)));

	return AngleDegrees <= ToleranceDegrees;
}

/** True when the piece is flat enough for the panel conventions to be meaningful. */
[[nodiscard]] bool IsPlanar(const FBox& Bounds, double PlanarExtentRatio, int32& OutFlatAxis)
{
	const FVector Extent = Bounds.GetExtent();
	OutFlatAxis = 2;

	double Smallest = Extent.Z;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (Extent[Axis] < Smallest)
		{
			Smallest = Extent[Axis];
			OutFlatAxis = Axis;
		}
	}

	const double Largest = Extent.GetMax();
	return Largest > UE_DOUBLE_SMALL_NUMBER && Smallest <= Largest * PlanarExtentRatio;
}
} // namespace PolySnapMeshValidationPrivate

int32 FPolySnapValidationReport::CountBySeverity(EPolySnapValidationSeverity Severity) const
{
	int32 Count = 0;
	for (const FPolySnapValidationMessage& Message : Messages)
	{
		Count += Message.Severity == Severity ? 1 : 0;
	}

	return Count;
}

bool FPolySnapMeshValidation::MeasureEdgeLengthUu(const UStaticMesh* StaticMesh, const FTransform& InSocketTransform,
	double& OutEdgeLengthUu)
{
	OutEdgeLengthUu = 0.0;

	if (StaticMesh == nullptr)
	{
		return false;
	}

	const FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(0);
	if (MeshDescription == nullptr || MeshDescription->Vertices().Num() == 0)
	{
		return false;
	}

	const FStaticMeshConstAttributes Attributes(*MeshDescription);
	const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();

	const FPolySnapSocketBasis Basis = FPolySnapGeometry::BasisFromTransform(InSocketTransform);
	const double ProbeDepth = UPolySnapSettings::Get().EdgeProbeDepthUu;

	// Two passes: find how far the piece reaches along Outward, then measure only what is out
	// there. One pass would need every projection stored.
	double MaxOutward = -TNumericLimits<double>::Max();
	for (const FVertexID VertexID : MeshDescription->Vertices().GetElementIDs())
	{
		const FVector Position(Positions[VertexID]);
		MaxOutward = FMath::Max(MaxOutward, (Position - Basis.Location) | Basis.Outward);
	}

	double MinTangent = TNumericLimits<double>::Max();
	double MaxTangent = -TNumericLimits<double>::Max();
	int32 EdgeVertexCount = 0;

	for (const FVertexID VertexID : MeshDescription->Vertices().GetElementIDs())
	{
		const FVector Offset = FVector(Positions[VertexID]) - Basis.Location;
		if ((Offset | Basis.Outward) < MaxOutward - ProbeDepth)
		{
			continue;
		}

		const double AlongTangent = Offset | Basis.Tangent;
		MinTangent = FMath::Min(MinTangent, AlongTangent);
		MaxTangent = FMath::Max(MaxTangent, AlongTangent);
		++EdgeVertexCount;
	}

	if (EdgeVertexCount < 2)
	{
		return false;
	}

	OutEdgeLengthUu = MaxTangent - MinTangent;
	return OutEdgeLengthUu > UE_DOUBLE_SMALL_NUMBER;
}

void FPolySnapMeshValidation::Validate(const UStaticMesh* StaticMesh, FPolySnapValidationReport& OutReport)
{
	using namespace PolySnapMeshValidationPrivate;

	if (StaticMesh == nullptr)
	{
		return;
	}

	const UPolySnapSettings& Settings = UPolySnapSettings::Get();
	const FBox Bounds = StaticMesh->GetBoundingBox();

	// A mesh asset carries no convention of its own, and this runs from an import hook with no
	// piece component in sight, so the project default is all there is to check against. A mesh
	// whose piece overrides it is therefore checked against the wrong convention.
	//
	// How wrong depends on which part differs. A Tangent sign flip -- the common case, and what a
	// socket authored directly in Unreal looks like -- costs nothing: the edge measurement is a
	// max minus a min along that axis, so its sign cancels. An Outward sign flip or an
	// Outward/Tangent swap measures a different edge entirely and Errors on a correct asset.
	// Set DefaultSocketAxes to whichever pipeline the project's assets mostly come from.
	const FQuat AxisCorrection = FPolySnapGeometry::AxisCorrection(Settings.DefaultSocketAxes);

	int32 FlatAxis = 2;
	const bool bPlanar = IsPlanar(Bounds, Settings.PlanarExtentRatio, FlatAxis);

	TMap<int32, FName> SeenIds;

	for (const TObjectPtr<UStaticMeshSocket>& SocketPtr : StaticMesh->Sockets)
	{
		const UStaticMeshSocket* Socket = SocketPtr.Get();
		if (Socket == nullptr)
		{
			continue;
		}

		FPolySnapSocketDescriptor Descriptor;
		FString Error;
		const EPolySnapParseResult ParseResult = FPolySnapSocketName::Parse(Socket->SocketName, Descriptor, &Error);

		if (ParseResult == EPolySnapParseResult::NotPolySnap)
		{
			// Somebody else's socket. Passed over without a diagnostic, which is what lets an
			// equipment plugin and this one read the same mesh.
			continue;
		}

		if (ParseResult == EPolySnapParseResult::Malformed)
		{
			OutReport.Add(EPolySnapValidationSeverity::Error, MoveTemp(Error));
			continue;
		}

		++OutReport.SocketCount;

		const FString SocketName = Socket->SocketName.ToString();

		// The tail was stripped by the parser, so this fires on a genuinely duplicated ID and not
		// on a hex and a pent that merely shared a .blend.
		if (const FName* Existing = SeenIds.Find(Descriptor.Id))
		{
			OutReport.Add(EPolySnapValidationSeverity::Error,
				FString::Printf(TEXT("Duplicate socket ID %03d: '%s' and '%s'. IDs are permanent identity and "
									 "must be unique within a piece."),
					Descriptor.Id, *Existing->ToString(), *SocketName));
		}
		else
		{
			SeenIds.Add(Descriptor.Id, Socket->SocketName);
		}

		const FTransform RawTransform = SocketTransform(*Socket);
		const FTransform Transform = FPolySnapGeometry::Canonicalise(RawTransform, AxisCorrection);
		const FPolySnapSocketBasis Basis = FPolySnapGeometry::BasisFromTransform(Transform);

		// -- Hard check 1: the socket's own frame ------------------------------------------
		// On the raw transform, deliberately: the correction is a rotation, so it would carry a
		// scale or a shear straight through, but reading what the asset actually stores is the
		// point of this check and CONVENTIONS.md section 6 says so.
		double Determinant = 0.0;
		if (!IsRotationOrthonormal(RawTransform, Determinant))
		{
			OutReport.Add(EPolySnapValidationSeverity::Error,
				FString::Printf(TEXT("Socket '%s' has a scaled, sheared or mirrored frame (scale %s, determinant %.6f). "
						 "PolySnap reads rotation and translation only; socket scale must be 1."),
					*SocketName, *Socket->RelativeScale.ToString(), Determinant));
		}

		// -- Hard check 2: the edge length against the Length token -------------------------
		//
		// Length is the only size token this can check. Thickness has no counterpart here and is
		// deliberately not measured: a socket transform records a point and a basis, so there is
		// nothing in it to compare a cross-section against, and a bounds-derived guess would fire
		// on every piece that is not a flat slab.
		double EdgeLengthUu = 0.0;
		if (MeasureEdgeLengthUu(StaticMesh, Transform, EdgeLengthUu))
		{
			const double MeasuredMm = EdgeLengthUu * UuToMm;
			const double ToleranceMm = Descriptor.LengthMillimetres * (Settings.EdgeLengthTolerancePercent / 100.0);

			if (FMath::Abs(MeasuredMm - Descriptor.LengthMillimetres) > ToleranceMm)
			{
				OutReport.Add(EPolySnapValidationSeverity::Error,
					FString::Printf(TEXT("Socket '%s' sits on an edge measuring %.1f mm but is labelled %d mm (%.1f%% out). "
							 "A gross mismatch is an export scale error; check Apply Unit and Convert Scene "
							 "Unit before touching the name."),
						*SocketName, MeasuredMm, Descriptor.LengthMillimetres,
						100.0 * FMath::Abs(MeasuredMm - Descriptor.LengthMillimetres) / Descriptor.LengthMillimetres));
			}
		}
		else
		{
			OutReport.Add(EPolySnapValidationSeverity::Warning,
				FString::Printf(TEXT("Socket '%s': could not find an edge under it to measure, so the length "
									 "token was not checked."),
					*SocketName));
		}

		// -- Warnings, each behind its own switch ------------------------------------------
		if (Settings.bWarnOnInwardOutward)
		{
			const FVector ToSocket = Basis.Location - Bounds.GetCenter();
			if ((Basis.Outward | ToSocket) <= 0.0)
			{
				OutReport.Add(EPolySnapValidationSeverity::Warning,
					FString::Printf(TEXT("Socket '%s': Outward points into the piece, or Outward and Tangent "
										 "are swapped. A concave outline can fail this legitimately."),
						*SocketName));
			}
		}

		if (Settings.bWarnOnNonCanonicalPieceFrame && bPlanar)
		{
			FVector FlatAxisDirection = FVector::ZeroVector;
			FlatAxisDirection[FlatAxis] = 1.0;

			if (!IsParallel(Basis.Normal, FlatAxisDirection, Settings.AxisAlignmentToleranceDegrees))
			{
				OutReport.Add(EPolySnapValidationSeverity::Warning,
					FString::Printf(TEXT("Socket '%s': Normal is not parallel to the piece's flat axis. The "
										 "panel was probably authored standing up, or the socket has stray roll."),
						*SocketName));
			}
		}

		if (Settings.bWarnOnSocketOffMidPlane && bPlanar)
		{
			const double OffsetFromMidPlane = FMath::Abs(Basis.Location[FlatAxis] - Bounds.GetCenter()[FlatAxis]);
			const double HalfThickness = Bounds.GetExtent()[FlatAxis];

			if (OffsetFromMidPlane > FMath::Max(HalfThickness * 0.1, UE_DOUBLE_KINDA_SMALL_NUMBER))
			{
				OutReport.Add(EPolySnapValidationSeverity::Warning,
					FString::Printf(TEXT("Socket '%s' sits %.2f uu off the panel's mid-plane. With the origin on "
										 "a face, every flip displaces the panel by its thickness, which surfaces "
										 "much later as drift and reads as a bug in the snap math."),
						*SocketName, OffsetFromMidPlane));
			}
		}

		if (Settings.bWarnOnUnalignedPrimarySocket && Descriptor.Id == 1)
		{
			if (!IsParallel(Basis.Outward, FVector::ForwardVector, Settings.AxisAlignmentToleranceDegrees))
			{
				OutReport.Add(EPolySnapValidationSeverity::Warning,
					FString::Printf(TEXT("Socket '%s' is socket 001 but its Outward is not along the piece's "
										 "local +X. House style, not a requirement of the snapping math."),
						*SocketName));
			}
		}

		if (Settings.bWarnOnNumericAuthoringTail)
		{
			FStringView Head;
			FStringView Tail;
			FPolySnapSocketName::SplitAuthoringTail(SocketName, Head, Tail);

			if (!Tail.IsEmpty() && Algo::AllOf(Tail, [](TCHAR Character) { return FChar::IsDigit(Character); }))
			{
				OutReport.Add(EPolySnapValidationSeverity::Warning,
					FString::Printf(TEXT("Socket '%s' has a purely numeric authoring tail, so Blender assigned "
										 "it and a name collision went unnoticed. Benign, but '.001' reads like "
										 "the ID field."),
						*SocketName));
			}
		}
	}
}

bool FPolySnapMeshValidation::ValidateAndLog(const UStaticMesh* StaticMesh)
{
	FPolySnapValidationReport Report;
	Validate(StaticMesh, Report);

	if (Report.SocketCount == 0 && Report.Messages.IsEmpty())
	{
		return true;
	}

	for (const FPolySnapValidationMessage& Message : Report.Messages)
	{
		if (Message.Severity == EPolySnapValidationSeverity::Error)
		{
			UE_LOG(LogPolySnap, Error, TEXT("[%s] %s"), *GetNameSafe(StaticMesh), *Message.Text);
		}
		else
		{
			UE_LOG(LogPolySnap, Warning, TEXT("[%s] %s"), *GetNameSafe(StaticMesh), *Message.Text);
		}
	}

	if (!Report.HasErrors())
	{
		UE_LOG(LogPolySnap, Log,
			TEXT("[%s] %d PolySnap socket(s) validated, %d warning(s)."), *GetNameSafe(StaticMesh), Report.SocketCount,
				Report.CountBySeverity(EPolySnapValidationSeverity::Warning));
	}

	return !Report.HasErrors();
}
