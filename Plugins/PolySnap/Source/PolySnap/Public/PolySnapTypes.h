// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#pragma once

#include "CoreMinimal.h"

#include "PolySnapTypes.generated.h"

class UPolySnapPieceComponent;

/**
 * Edge geometry of a socket, the first half of the compatibility key (README section 2.2).
 *
 * Only Straight is in scope. Curved subtypes will need more than one parameter -- a radius and
 * an arc length -- which is why the naming grammar puts the subtype after the fixed-arity head
 * rather than at the end.
 */
UENUM(BlueprintType)
enum class EPolySnapEdgeSubType : uint8
{
	Straight
};

/**
 * A socket name parsed into its fields, cached once per piece and never re-parsed.
 *
 * Runtime compatibility tests operate on these fields; nothing string-compares a socket name
 * during a snap query.
 */
USTRUCT(BlueprintType)
struct POLYSNAP_API FPolySnapSocketDescriptor
{
	GENERATED_BODY()

	/** The engine socket name in full, authoring tail included. Identity for the mesh, not for PolySnap. */
	UPROPERTY(BlueprintReadOnly, Category = "PolySnap")
	FName SocketName;

	/**
	 * Identity within the piece, 1-999. Permanent: save games store it, so a removed socket's ID
	 * is retired rather than reassigned. Never participates in compatibility.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "PolySnap")
	int32 Id = 0;

	/** Edge geometry. Both halves of the compatibility key must match for two sockets to mate. */
	UPROPERTY(BlueprintReadOnly, Category = "PolySnap")
	EPolySnapEdgeSubType SubType = EPolySnapEdgeSubType::Straight;

	/**
	 * Nominal edge length in MILLIMETRES, and a compatibility token rather than a measurement.
	 * Unreal units are centimetres; never use this as a length. Snap geometry comes entirely
	 * from the socket transforms baked into the mesh.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "PolySnap")
	int32 SizeMillimetres = 0;

	/** True once Parse has filled this in. A default-constructed descriptor matches nothing. */
	[[nodiscard]] bool IsValid() const { return Id > 0 && SizeMillimetres > 0; }

	/**
	 * README section 2.2: two sockets may connect only when SubType and Size both match. The ID
	 * never participates -- it is identity, not classification.
	 */
	[[nodiscard]] bool IsCompatibleWith(const FPolySnapSocketDescriptor& Other) const
	{
		return IsValid() && Other.IsValid() && SubType == Other.SubType && SizeMillimetres == Other.SizeMillimetres;
	}
};

/**
 * The orthonormal basis a socket defines (README section 2.3), resolved from a socket transform.
 *
 * Which local axis carries which role is fixed in CONVENTIONS.md section 2. Note Tangent: it is
 * the socket's negative Y, because converting a right-handed Blender basis into Unreal's
 * left-handed one negates Y. Deriving it as +Y runs every polarity test backwards.
 */
struct POLYSNAP_API FPolySnapSocketBasis
{
	/** Socket position, in whatever space the source transform was in. */
	FVector Location = FVector::ZeroVector;

	/** In the plane of the panel, perpendicular to the edge, away from the piece's centre. Local +X. */
	FVector Outward = FVector::ForwardVector;

	/** Along the edge. Local -Y. */
	FVector Tangent = -FVector::RightVector;

	/** The panel's surface normal. Local +Z. Arbitrary which face; it does not mean "outside". */
	FVector Normal = FVector::UpVector;
};

/**
 * Which way round the two panels' normals end up (README section 2.3). The mating condition
 * leaves the tangent sign free, and both values are always admissible -- a flip is a proper
 * 180 degree rotation about the socket's Outward axis, a motion the player can physically
 * perform, not a mirror image.
 */
UENUM(BlueprintType)
enum class EPolySnapPolarity : uint8
{
	/** Tangent_B == -Tangent_A. Surface orientation is consistent across the joint. */
	Aligned,

	/** Tangent_B == +Tangent_A. Piece B is turned over; its outward face now points the other way. */
	Flipped
};

/**
 * One socket of one piece, resolved into world space. The unit the candidate search works on.
 *
 * The search takes an array of these rather than reaching into actors, which is what keeps it
 * pure and testable without a world.
 */
struct POLYSNAP_API FPolySnapWorldSocket
{
	/** The piece this socket belongs to. Null is legal, and is what the automation specs use. */
	TWeakObjectPtr<UPolySnapPieceComponent> Piece;

	/** Parsed fields. Compatibility is decided from these alone. */
	FPolySnapSocketDescriptor Descriptor;

	/** The socket's world transform, as baked into the mesh and moved by its piece. */
	FTransform WorldTransform = FTransform::Identity;

	/** True when the socket already participates in a connection. */
	bool bConnected = false;
};

/** Why a socket pair failed the candidate tests, so the debug readout can say more than "no". */
enum class EPolySnapRejection : uint8
{
	None,
	SamePiece,
	Incompatible,
	TooFar,
	TangentNotCollinear,
	JointFull
};

/**
 * A socket pair that passed every test in README section 2.5, together with the placement it
 * implies. Exactly one candidate becomes the anchor, and the piece's transform is solved from
 * the anchor and from nothing else.
 */
struct POLYSNAP_API FPolySnapCandidate
{
	/** The socket on the piece being placed. */
	FPolySnapWorldSocket HeldSocket;

	/** The socket already in the world that it would mate with. */
	FPolySnapWorldSocket TargetSocket;

	/** Which way round the held piece ends up. Chosen by least rotation from its current pose. */
	EPolySnapPolarity Polarity = EPolySnapPolarity::Aligned;

	/** Signed dihedral in the target socket's basis, over [0, 360). */
	double DihedralDegrees = 180.0;

	/** Distance between the two socket positions before the snap, in Unreal units. */
	double GapUu = 0.0;

	/** Angle between the two tangent lines before the snap, in degrees, ignoring polarity. */
	double TangentAngleDegrees = 0.0;

	/** How far the held piece has to rotate to reach the solved pose, in degrees. */
	double RequiredRotationDegrees = 0.0;

	/** Dimensionless anchor score; lower wins. See FPolySnapSnapQuery::FindBest. */
	double Cost = 0.0;

	/** The world transform the held piece takes if this candidate is committed. */
	FTransform SolvedPieceTransform = FTransform::Identity;

	[[nodiscard]] bool IsSet() const { return HeldSocket.Descriptor.IsValid() && TargetSocket.Descriptor.IsValid(); }
};

/**
 * One socket's participation in a connection, recorded on the piece that owns the socket.
 *
 * Milestone 1 records connections directly between pieces. README section 2.4's first-class
 * Joint -- a shared edge line hosting two or more sockets -- arrives with the assembly graph in
 * Milestone 3, and this struct is what it will grow out of.
 */
USTRUCT()
struct POLYSNAP_API FPolySnapConnection
{
	GENERATED_BODY()

	/** ID of the socket on this piece that is participating. */
	UPROPERTY()
	int32 LocalSocketId = 0;

	/** The piece on the other side of the seam. */
	UPROPERTY()
	TWeakObjectPtr<UPolySnapPieceComponent> OtherPiece;

	/** ID of the participating socket on that piece. */
	UPROPERTY()
	int32 OtherSocketId = 0;

	/** True when this pair was the anchor, whose mating conditions held to float precision. */
	UPROPERTY()
	bool bWasAnchor = false;

	/**
	 * The gap this connection was adopted at, in millimetres, measured at the instant of
	 * placement. Zero for an anchor by construction. Nothing needs it to rebuild the assembly;
	 * it is a few bytes that make a drifting build diagnosable long afterwards.
	 */
	UPROPERTY()
	float ResidualMm = 0.0f;
};
