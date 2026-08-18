// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapDebug.h"

#include "Components/MeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "PolySnapGeometry.h"
#include "PolySnapPieceComponent.h"

namespace PolySnapDebugPrivate
{
static TAutoConsoleVariable<int32> CVarDrawMode(TEXT("PolySnap.Debug.Draw"), 1,
	TEXT("PolySnap socket visualisation. 0 off, 1 held piece and candidates, 2 every registered piece."), ECVF_Cheat);

static TAutoConsoleVariable<int32> CVarText(TEXT("PolySnap.Debug.Text"), 1,
	TEXT("PolySnap on-screen readout: held piece, candidate, gap, angle, polarity, dihedral."), ECVF_Cheat);

static TAutoConsoleVariable<float> CVarMaxDistance(TEXT("PolySnap.Debug.MaxDistance"), 3000.0f,
	TEXT("Beyond this distance from the viewer, PolySnap draws nothing. Unreal units."), ECVF_Cheat);

static TAutoConsoleVariable<float> CVarGizmoScale(TEXT("PolySnap.Debug.GizmoScale"), 1.0f,
	TEXT("Size multiplier for PolySnap socket gizmos."), ECVF_Cheat);

/** Base gizmo arrow length in Unreal units, before the scale multiplier. */
constexpr double BaseArrowLength = 18.0;

/** Unreal units are centimetres and the debug readout is in millimetres, like the Size token. */
constexpr double UuToMm = 10.0;

const FColor OutwardColour = FColor::Red;
const FColor TangentColour = FColor::Green;
const FColor NormalColour = FColor::Blue;
const FColor FreeSocketColour = FColor(160, 160, 160);
const FColor ConnectedSocketColour = FColor(60, 220, 90);
const FColor HighlightColour = FColor::Yellow;
const FColor JointColour = FColor(220, 60, 220);

[[nodiscard]] FVector GetViewerLocation(const UWorld* World)
{
	if (World == nullptr)
	{
		return FVector::ZeroVector;
	}

	if (const APlayerController* Controller = World->GetFirstPlayerController())
	{
		FVector ViewLocation = FVector::ZeroVector;
		FRotator ViewRotation = FRotator::ZeroRotator;
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);

		return ViewLocation;
	}

	return FVector::ZeroVector;
}
}

int32 FPolySnapDebug::GetDrawMode()
{
	return PolySnapDebugPrivate::CVarDrawMode.GetValueOnAnyThread();
}

bool FPolySnapDebug::IsTextEnabled()
{
	return PolySnapDebugPrivate::CVarText.GetValueOnAnyThread() != 0;
}

double FPolySnapDebug::GetMaxDrawDistance()
{
	return PolySnapDebugPrivate::CVarMaxDistance.GetValueOnAnyThread();
}

double FPolySnapDebug::GetGizmoScale()
{
	return PolySnapDebugPrivate::CVarGizmoScale.GetValueOnAnyThread();
}

bool FPolySnapDebug::IsInDrawRange(const UWorld* World, const FVector& Location)
{
	const double MaxDistance = GetMaxDrawDistance();
	return FVector::DistSquared(PolySnapDebugPrivate::GetViewerLocation(World), Location) <= MaxDistance * MaxDistance;
}

void FPolySnapDebug::DrawSocket(const UWorld* World, const FTransform& SocketTransform,
	const FPolySnapSocketDescriptor& Descriptor, bool bConnected, bool bHighlighted)
{
#if ENABLE_DRAW_DEBUG
	using namespace PolySnapDebugPrivate;

	if (World == nullptr || !IsInDrawRange(World, SocketTransform.GetLocation()))
	{
		return;
	}

	const FPolySnapSocketBasis Basis = FPolySnapGeometry::BasisFromTransform(SocketTransform);
	const double ArrowLength = BaseArrowLength * GetGizmoScale();
	const double ArrowHead = ArrowLength * 0.25;

	// The three named directions, drawn in the level so an axis-role mistake in Blender is
	// visible rather than inferred. These are the resolved roles rather than raw mesh axes --
	// GetSocketWorldTransform has already applied the piece's convention -- so red is Outward
	// whichever local axis that piece's assets put it on, and a wrong convention shows up here
	// as arrows pointing somewhere the panel's geometry says they should not. The arrow colours
	// are never overridden -- the held piece is signalled by the socket sphere below, so the
	// axis legend still reads on the one piece whose convention you are most likely checking.
	DrawDebugDirectionalArrow(World, Basis.Location, Basis.Location + Basis.Outward * ArrowLength, ArrowHead,
		OutwardColour, false, -1.0f, 0, 0.6f);
	DrawDebugDirectionalArrow(World, Basis.Location, Basis.Location + Basis.Tangent * ArrowLength, ArrowHead,
		TangentColour, false, -1.0f, 0, 0.6f);
	DrawDebugDirectionalArrow(World, Basis.Location, Basis.Location + Basis.Normal * ArrowLength, ArrowHead,
		NormalColour, false, -1.0f, 0, 0.6f);

	const FColor SocketColour =
		bHighlighted ? HighlightColour : (bConnected ? ConnectedSocketColour : FreeSocketColour);
	DrawDebugSphere(World, Basis.Location, ArrowLength * 0.12, 8, SocketColour, false, -1.0f, 0, 0.5f);

	if (IsTextEnabled())
	{
		DrawDebugString(World, Basis.Location + Basis.Normal * (ArrowLength * 1.2),
			FString::Printf(TEXT("%03d"), Descriptor.Id), nullptr, SocketColour, 0.0f, true, 1.0f);
	}
#endif
}

void FPolySnapDebug::DrawPiece(const UWorld* World, const UPolySnapPieceComponent& Piece, bool bHighlighted)
{
#if ENABLE_DRAW_DEBUG
	for (const FPolySnapSocketDescriptor& Descriptor : Piece.GetSocketDescriptors())
	{
		DrawSocket(World, Piece.GetSocketWorldTransform(Descriptor.SocketName), Descriptor,
			Piece.IsSocketConnected(Descriptor.Id), bHighlighted);
	}
#endif
}

void FPolySnapDebug::DrawCandidate(const UWorld* World, const FPolySnapCandidate& Candidate)
{
#if ENABLE_DRAW_DEBUG
	using namespace PolySnapDebugPrivate;

	if (World == nullptr || !Candidate.IsSet())
	{
		return;
	}

	const FVector HeldLocation = Candidate.HeldSocket.WorldTransform.GetLocation();
	const FVector TargetLocation = Candidate.TargetSocket.WorldTransform.GetLocation();

	if (!IsInDrawRange(World, TargetLocation))
	{
		return;
	}

	DrawSocket(World, Candidate.HeldSocket.WorldTransform, Candidate.HeldSocket.Descriptor, false, true);
	DrawSocket(World, Candidate.TargetSocket.WorldTransform, Candidate.TargetSocket.Descriptor, false, true);
	DrawDebugLine(World, HeldLocation, TargetLocation, HighlightColour, false, -1.0f, 0, 1.0f);

	if (IsTextEnabled())
	{
		DrawDebugString(World, (HeldLocation + TargetLocation) * 0.5,
			FString::Printf(TEXT("%.1f mm  %.1f deg"), Candidate.GapUu * UuToMm, Candidate.TangentAngleDegrees),
				nullptr, HighlightColour, 0.0f, true, 1.0f);
	}
#endif
}

void FPolySnapDebug::DrawPreview(const UWorld* World, const UPolySnapPieceComponent& HeldPiece,
	const FTransform& SolvedTransform)
{
#if ENABLE_DRAW_DEBUG
	using namespace PolySnapDebugPrivate;

	const UMeshComponent* Mesh = HeldPiece.GetResolvedSocketMesh();
	if (World == nullptr || Mesh == nullptr)
	{
		return;
	}

	// Local bounds, re-expressed at the solved transform: where the piece lands if placed now.
	const FBoxSphereBounds LocalBounds = Mesh->CalcBounds(FTransform::Identity);
	DrawDebugBox(World, SolvedTransform.TransformPosition(LocalBounds.Origin), LocalBounds.BoxExtent,
		SolvedTransform.GetRotation(), HighlightColour, false, -1.0f, 0, 0.8f);
#endif
}

void FPolySnapDebug::DrawCommittedJoint(const UWorld* World, const FTransform& JointTransform, double EdgeLengthUu,
	double DihedralDegrees)
{
#if ENABLE_DRAW_DEBUG
	using namespace PolySnapDebugPrivate;

	if (World == nullptr)
	{
		return;
	}

	const FPolySnapSocketBasis Basis = FPolySnapGeometry::BasisFromTransform(JointTransform);
	const FVector HalfEdge = Basis.Tangent * (EdgeLengthUu * 0.5);

	// The shared edge line: the axis the joint is free to hinge about.
	DrawDebugLine(World, Basis.Location - HalfEdge, Basis.Location + HalfEdge, JointColour, false, 5.0f, 0, 1.5f);

	if (IsTextEnabled())
	{
		DrawDebugString(World, Basis.Location,
			FString::Printf(TEXT("%.2f deg"), DihedralDegrees), nullptr, JointColour, 5.0f, true, 1.0f);
	}
#endif
}
