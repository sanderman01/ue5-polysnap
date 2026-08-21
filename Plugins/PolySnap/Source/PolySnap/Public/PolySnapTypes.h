// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#pragma once

#include "CoreMinimal.h"

#include "PolySnapTypes.generated.h"

class UPolySnapConnectorComponent;

/**
 * Edge geometry of a socket, the first of the three compatibility terms (DESIGN section 2.2).
 *
 * It is also what fixes the arity of the fields after it: Length is Straight's one shape
 * parameter, and a curved subtype would replace it with a radius and an arc length. That is why
 * the grammar puts the subtype's parameters last and Thickness, which every subtype has, ahead
 * of them.
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
 * A socket name parsed into its fields, cached once per part and never re-parsed.
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
	 * Identity within the part, 1-999. Permanent: save games store it, so a removed socket's ID
	 * is retired rather than reassigned. Never participates in compatibility.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "PolySnap")
	int32 Id = 0;

	/** Edge geometry. All three compatibility terms must match for two sockets to mate. */
	UPROPERTY(BlueprintReadOnly, Category = "PolySnap")
	EPolySnapEdgeSubType SubType = EPolySnapEdgeSubType::Straight;

	/**
	 * Panel thickness at the edge, as an opaque token. It is what stops a thin partition panel
	 * being declared compatible with a thick hull panel of the same edge length and mating to a
	 * stepped seam.
	 *
	 * PolySnap never interprets it: no unit is implied, no arithmetic is ever done on it, and the
	 * only operation is equality against another socket's token. "40", "40mm", "4cm" and "Thin"
	 * are all legal and all distinct; a project picks one vocabulary and holds it. See DESIGN
	 * section 2.2.
	 *
	 * Declared before Length to match the naming grammar, where thickness precedes the subtype's
	 * own shape parameters because every subtype has it and they differ per subtype.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "PolySnap")
	FName Thickness;

	/**
	 * Nominal edge length, an opaque token on exactly the same terms as Thickness, and Straight's
	 * one shape parameter. Snap geometry comes entirely from the socket transforms baked into the
	 * mesh; this field is a label, never a distance.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "PolySnap")
	FName Length;

	/** True once Parse has filled this in. A default-constructed descriptor matches nothing. */
	[[nodiscard]] bool IsValid() const { return Id > 0 && !Thickness.IsNone() && !Length.IsNone(); }

	/**
	 * DESIGN section 2.2: two sockets may connect only when SubType, Thickness and Length all
	 * match. The ID never participates -- it is identity, not classification.
	 *
	 * FName equality is an interned-index compare, so this stays the cheapest test in the query,
	 * and it is case-insensitive: "40MM" mates with "40mm". Spelling is otherwise significant --
	 * "0500" and "500" are different tokens, because nothing here reads them as numbers.
	 */
	[[nodiscard]] bool IsCompatibleWith(const FPolySnapSocketDescriptor& Other) const
	{
		return IsValid() && Other.IsValid() && SubType == Other.SubType && Thickness == Other.Thickness
			&& Length == Other.Length;
	}
};

/**
 * One signed socket-local axis, as spelled in CONVENTIONS.md section 2's table.
 *
 * Six values rather than three plus a separate sign: a role is a direction, not a line, and it is
 * Tangent's sign that decides which polarity counts as Aligned.
 */
UENUM(BlueprintType)
enum class EPolySnapSocketAxis : uint8
{
	PlusX,
	MinusX,
	PlusY,
	MinusY,
	PlusZ,
	MinusZ
};

/**
 * Which socket-local axis carries which role, for one authoring pipeline.
 *
 * The defaults are CONVENTIONS.md section 2's table: panels authored in Blender and exported
 * through the default FBX settings, where the right-to-left-handed conversion negates Y. A mesh
 * that came from anywhere else declares its own mapping rather than being re-exported, which is
 * what stops the plugin assuming this project's pipeline.
 *
 * All three roles are declared, and all three are honoured exactly -- which is why Validate is
 * strict about handedness. A socket frame is orthonormal with determinant +1, so naming Outward
 * and Tangent already determines Normal down to its sign; a triad that disagrees about that sign
 * cannot be realised, and only one of the three declarations could survive. Rather than silently
 * pick one and leave a dropdown that does nothing, Validate rejects the pair and says which axis
 * to flip. Every accepted mapping therefore means precisely what it says.
 *
 * That leaves 24 legal mappings -- 6 choices of Outward, 4 of Tangent, Normal forced -- which are
 * exactly the 24 rotations of a cube.
 */
USTRUCT(BlueprintType)
struct POLYSNAP_API FPolySnapSocketAxes
{
	GENERATED_BODY()

	/** In the plane of the panel, perpendicular to the edge, away from the part's centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PolySnap")
	EPolySnapSocketAxis OutwardAxis = EPolySnapSocketAxis::PlusX;

	/** Along the edge. Its sign is what decides which polarity is Aligned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PolySnap")
	EPolySnapSocketAxis TangentAxis = EPolySnapSocketAxis::MinusY;

	/** The panel's surface normal. Which face is arbitrary, but it must agree with the other two. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PolySnap")
	EPolySnapSocketAxis NormalAxis = EPolySnapSocketAxis::PlusZ;

	/**
	 * True when these three roles describe a basis a socket can actually have: three different
	 * base axes, and a Normal on the side the other two put it. OutError explains which rule
	 * failed and what to change.
	 */
	[[nodiscard]] bool Validate(FString* OutError = nullptr) const;

	/**
	 * The Normal that Outward and Tangent imply, whatever NormalAxis says.
	 *
	 * Tangent ^ Outward, the same formula MatedBasis uses -- Unreal's cross product is the plain
	 * right-hand formula while its coordinate system is left-handed, so the physically
	 * right-handed triad computes in this order. Zero when Outward and Tangent share an axis.
	 */
	[[nodiscard]] FVector DerivedNormal() const;

	[[nodiscard]] bool operator==(const FPolySnapSocketAxes& Other) const
	{
		return OutwardAxis == Other.OutwardAxis && TangentAxis == Other.TangentAxis && NormalAxis == Other.NormalAxis;
	}

	[[nodiscard]] bool operator!=(const FPolySnapSocketAxes& Other) const { return !(*this == Other); }
};

/** The unit vector an axis names. */
[[nodiscard]] POLYSNAP_API FVector PolySnapAxisToVector(EPolySnapSocketAxis Axis);

/** "+X", "-Y" and so on -- CONVENTIONS.md section 2's notation, and what the specs assert on. */
[[nodiscard]] POLYSNAP_API const TCHAR* PolySnapAxisToString(EPolySnapSocketAxis Axis);

/**
 * The orthonormal basis a socket defines (DESIGN section 2.3), resolved from a socket transform.
 *
 * Which local axis carries which role is fixed in CONVENTIONS.md section 2. Note Tangent: it is
 * the socket's negative Y, because converting a right-handed Blender basis into Unreal's
 * left-handed one negates Y. Deriving it as +Y runs every polarity test backwards.
 */
struct POLYSNAP_API FPolySnapSocketBasis
{
	/** Socket position, in whatever space the source transform was in. */
	FVector Location = FVector::ZeroVector;

	/** In the plane of the panel, perpendicular to the edge, away from the part's centre. Local +X. */
	FVector Outward = FVector::ForwardVector;

	/** Along the edge. Local -Y. */
	FVector Tangent = -FVector::RightVector;

	/** The panel's surface normal. Local +Z. Arbitrary which face; it does not mean "outside". */
	FVector Normal = FVector::UpVector;
};

/**
 * Which way round the two panels' normals end up (DESIGN section 2.3). The mating condition
 * leaves the tangent sign free, and both values are always admissible -- a flip is a proper
 * 180 degree rotation about the socket's Outward axis, a motion the player can physically
 * perform, not a mirror image.
 */
UENUM(BlueprintType)
enum class EPolySnapPolarity : uint8
{
	/** Tangent_B == -Tangent_A. Surface orientation is consistent across the joint. */
	Aligned,

	/** Tangent_B == +Tangent_A. Part B is turned over; its outward face now points the other way. */
	Flipped
};

/**
 * One socket of one part, resolved into world space. The unit the candidate search works on.
 *
 * The search takes an array of these rather than reaching into actors, which is what keeps it
 * pure and testable without a world.
 */
struct POLYSNAP_API FPolySnapWorldSocket
{
	/** The part this socket belongs to. Null is legal, and is what the automation specs use. */
	TWeakObjectPtr<UPolySnapConnectorComponent> Part;

	/** Parsed fields. Compatibility is decided from these alone. */
	FPolySnapSocketDescriptor Descriptor;

	/** The socket's world transform, as baked into the mesh and moved by its part. */
	FTransform WorldTransform = FTransform::Identity;

	/** True when the socket already participates in a connection. */
	bool bConnected = false;
};

/** Why a socket pair failed the candidate tests, so the debug readout can say more than "no". */
enum class EPolySnapRejection : uint8
{
	None,
	SamePart,
	Incompatible,
	TooFar,
	TangentNotCollinear,
	JointFull
};

/**
 * A socket pair that passed every test in DESIGN section 2.5, together with the placement it
 * implies. Exactly one candidate becomes the anchor, and the part's transform is solved from
 * the anchor and from nothing else.
 */
struct POLYSNAP_API FPolySnapCandidate
{
	/** The socket on the part being placed. */
	FPolySnapWorldSocket HeldSocket;

	/** The socket already in the world that it would mate with. */
	FPolySnapWorldSocket TargetSocket;

	/** Which way round the held part ends up. Chosen by least rotation from its current pose. */
	EPolySnapPolarity Polarity = EPolySnapPolarity::Aligned;

	/** Signed dihedral in the target socket's basis, over [0, 360). */
	double DihedralDegrees = 180.0;

	/** Distance between the two socket positions before the snap, in Unreal units. */
	double GapUu = 0.0;

	/** Angle between the two tangent lines before the snap, in degrees, ignoring polarity. */
	double TangentAngleDegrees = 0.0;

	/** How far the held part has to rotate to reach the solved pose, in degrees. */
	double RequiredRotationDegrees = 0.0;

	/** Dimensionless anchor score; lower wins. See FPolySnapSnapQuery::FindBest. */
	double Cost = 0.0;

	/** The world transform the held part takes if this candidate is committed. */
	FTransform SolvedPartTransform = FTransform::Identity;

	[[nodiscard]] bool IsSet() const { return HeldSocket.Descriptor.IsValid() && TargetSocket.Descriptor.IsValid(); }
};

/**
 * One socket's participation in a connection, recorded on the part that owns the socket.
 *
 * Milestone 1 records connections directly between parts. DESIGN section 2.4's first-class
 * Joint -- a shared edge line hosting two or more sockets -- arrives with the assembly graph in
 * Milestone 3, and this struct is what it will grow out of.
 */
USTRUCT()
struct POLYSNAP_API FPolySnapConnection
{
	GENERATED_BODY()

	/** ID of the socket on this part that is participating. */
	UPROPERTY()
	int32 LocalSocketId = 0;

	/** The part on the other side of the seam. */
	UPROPERTY()
	TWeakObjectPtr<UPolySnapConnectorComponent> OtherPart;

	/** ID of the participating socket on that part. */
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
