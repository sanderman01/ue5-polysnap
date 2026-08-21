// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapBuckyballFixture.h"

namespace PolySnapBuckyballFixturePrivate
{
/** The icosahedron this is truncated from has edge length 2, which sets every tolerance below. */
constexpr double IcosahedronEdge = 2.0;

/** Squared-distance slack when deciding whether two icosahedron vertices are adjacent. */
constexpr double AdjacencyTolerance = 1.0e-6;

/** How near two truncation points must be to count as the same vertex. */
constexpr double MergeTolerance = 1.0e-6;

/** The twelve icosahedron vertices: the cyclic permutations of (0, +-1, +-phi). */
[[nodiscard]] TArray<FVector> Icosahedron()
{
	const double Phi = 0.5 * (1.0 + FMath::Sqrt(5.0));

	TArray<FVector> Result;
	Result.Reserve(12);

	for (const double SignA : {-1.0, 1.0})
	{
		for (const double SignB : {-1.0, 1.0})
		{
			Result.Add(FVector(0.0, SignA, SignB * Phi));
			Result.Add(FVector(SignA, SignB * Phi, 0.0));
			Result.Add(FVector(SignB * Phi, 0.0, SignA));
		}
	}

	return Result;
}

[[nodiscard]] bool AreAdjacent(const FVector& A, const FVector& B)
{
	return FMath::IsNearlyEqual(FVector::DistSquared(A, B), IcosahedronEdge * IcosahedronEdge, AdjacencyTolerance);
}

/** Adds a point to the vertex list, or returns the index of the one already there. */
[[nodiscard]] int32 AddUniqueVertex(TArray<FVector>& Vertices, const FVector& Point)
{
	for (int32 Index = 0; Index < Vertices.Num(); ++Index)
	{
		if (FVector::DistSquared(Vertices[Index], Point) < MergeTolerance)
		{
			return Index;
		}
	}

	return Vertices.Add(Point);
}

/**
 * Puts a face's vertices in ring order about the face's outward normal.
 *
 * The polyhedron is centred on the origin, so the face's own centre gives the outward normal and
 * the ring is simply the vertices sorted by angle about it.
 */
void SortIntoRing(const TArray<FVector>& Vertices, FPolySnapFixtureFace& Face)
{
	FVector Centre = FVector::ZeroVector;
	for (const int32 Index : Face.VertexIndices)
	{
		Centre += Vertices[Index];
	}
	Centre /= Face.VertexIndices.Num();

	const FVector Normal = Centre.GetSafeNormal();
	const FVector AxisX = (Vertices[Face.VertexIndices[0]] - Centre).GetSafeNormal();
	const FVector AxisY = FVector::CrossProduct(Normal, AxisX);

	Face.VertexIndices.Sort(
		[&Vertices, Centre, AxisX, AxisY](const int32 A, const int32 B)
		{
			const FVector OffsetA = Vertices[A] - Centre;
			const FVector OffsetB = Vertices[B] - Centre;

			return FMath::Atan2(OffsetA | AxisY, OffsetA | AxisX) < FMath::Atan2(OffsetB | AxisY, OffsetB | AxisX);
		});

	Face.Centre = Centre;
	Face.Normal = Normal;
}
} // namespace PolySnapBuckyballFixturePrivate

FPolySnapBuckyballFixture::FPolySnapBuckyballFixture(double EdgeLengthUu)
{
	using namespace PolySnapBuckyballFixturePrivate;

	const TArray<FVector> Seeds = Icosahedron();

	// Truncation: each icosahedron edge contributes the two points a third of the way along it from
	// either end. Those 60 points are the buckyball's vertices, and the thirds are what make every
	// resulting edge the same length -- a third of the icosahedron's on the edges it cut, and a
	// third of one on the pentagon edges it opened up.
	TArray<TArray<int32>> PentagonVertices;
	PentagonVertices.SetNum(Seeds.Num());

	for (int32 A = 0; A < Seeds.Num(); ++A)
	{
		for (int32 B = 0; B < Seeds.Num(); ++B)
		{
			if (A == B || !AreAdjacent(Seeds[A], Seeds[B]))
			{
				continue;
			}

			PentagonVertices[A].Add(AddUniqueVertex(Vertices, Seeds[A] + (Seeds[B] - Seeds[A]) / 3.0));
		}
	}

	// A pentagon per icosahedron vertex: the corner that truncating it cut off.
	for (const TArray<int32>& Ring : PentagonVertices)
	{
		FPolySnapFixtureFace Face;
		Face.Kind = EPolySnapFixtureFaceKind::Pentagon;
		Face.VertexIndices = Ring;
		Faces.Add(MoveTemp(Face));
	}

	// A hexagon per icosahedron face: what is left of the triangle once its three corners are gone.
	for (int32 A = 0; A < Seeds.Num(); ++A)
	{
		for (int32 B = A + 1; B < Seeds.Num(); ++B)
		{
			for (int32 C = B + 1; C < Seeds.Num(); ++C)
			{
				if (!AreAdjacent(Seeds[A], Seeds[B]) || !AreAdjacent(Seeds[B], Seeds[C])
					|| !AreAdjacent(Seeds[A], Seeds[C]))
				{
					continue;
				}

				FPolySnapFixtureFace Face;
				Face.Kind = EPolySnapFixtureFaceKind::Hexagon;

				for (const TPair<int32, int32> Edge :
					{TPair<int32, int32>(A, B), TPair<int32, int32>(B, C), TPair<int32, int32>(C, A)})
				{
					Face.VertexIndices.Add(
						AddUniqueVertex(Vertices, Seeds[Edge.Key] + (Seeds[Edge.Value] - Seeds[Edge.Key]) / 3.0));
					Face.VertexIndices.Add(
						AddUniqueVertex(Vertices, Seeds[Edge.Value] + (Seeds[Edge.Key] - Seeds[Edge.Value]) / 3.0));
				}

				Faces.Add(MoveTemp(Face));
			}
		}
	}

	// Truncating at thirds leaves edges a third of the icosahedron's, so this is the whole of the
	// scaling. Applied before the rings are sorted, so the centres and normals come out at scale.
	const double Scale = EdgeLengthUu / (IcosahedronEdge / 3.0);
	for (FVector& Vertex : Vertices)
	{
		Vertex *= Scale;
	}

	for (FPolySnapFixtureFace& Face : Faces)
	{
		SortIntoRing(Vertices, Face);
	}

	// Adjacency from the rings rather than from a table: an edge belongs to exactly the two faces
	// whose rings both contain its endpoints consecutively.
	for (int32 FaceIndex = 0; FaceIndex < Faces.Num(); ++FaceIndex)
	{
		const TArray<int32>& Ring = Faces[FaceIndex].VertexIndices;

		for (int32 Step = 0; Step < Ring.Num(); ++Step)
		{
			const int32 VertexA = Ring[Step];
			const int32 VertexB = Ring[(Step + 1) % Ring.Num()];

			FPolySnapFixtureEdge* Existing = Edges.FindByPredicate(
				[VertexA, VertexB](const FPolySnapFixtureEdge& Edge)
				{
					return (Edge.VertexA == VertexA && Edge.VertexB == VertexB)
						|| (Edge.VertexA == VertexB && Edge.VertexB == VertexA);
				});

			if (Existing != nullptr)
			{
				Existing->FaceB = FaceIndex;
				continue;
			}

			Edges.Add(FPolySnapFixtureEdge{VertexA, VertexB, FaceIndex, INDEX_NONE});
		}
	}
}

TArray<FVector> FPolySnapBuckyballFixture::EdgeMidpoints(int32 FaceIndex) const
{
	const TArray<int32>& Ring = Faces[FaceIndex].VertexIndices;

	TArray<FVector> Midpoints;
	Midpoints.Reserve(Ring.Num());

	for (int32 Step = 0; Step < Ring.Num(); ++Step)
	{
		Midpoints.Add(0.5 * (Vertices[Ring[Step]] + Vertices[Ring[(Step + 1) % Ring.Num()]]));
	}

	return Midpoints;
}

TArray<int32> FPolySnapBuckyballFixture::NeighbouringFaces(int32 FaceIndex) const
{
	const TArray<int32>& Ring = Faces[FaceIndex].VertexIndices;

	TArray<int32> Neighbours;
	Neighbours.Reserve(Ring.Num());

	for (int32 Step = 0; Step < Ring.Num(); ++Step)
	{
		const int32 VertexA = Ring[Step];
		const int32 VertexB = Ring[(Step + 1) % Ring.Num()];

		for (const FPolySnapFixtureEdge& Edge : Edges)
		{
			const bool bIsThisEdge = (Edge.VertexA == VertexA && Edge.VertexB == VertexB)
								  || (Edge.VertexA == VertexB && Edge.VertexB == VertexA);

			if (bIsThisEdge)
			{
				Neighbours.Add(Edge.FaceA == FaceIndex ? Edge.FaceB : Edge.FaceA);
				break;
			}
		}
	}

	return Neighbours;
}

TArray<int32> FPolySnapBuckyballFixture::BuildOrder() const
{
	TArray<int32> Order;
	Order.Reserve(Faces.Num());

	TArray<bool> bVisited;
	bVisited.Init(false, Faces.Num());

	// From face 0, which is a pentagon: the pentagons are added first, so index 0 is one of them.
	Order.Add(0);
	bVisited[0] = true;

	for (int32 Cursor = 0; Cursor < Order.Num(); ++Cursor)
	{
		for (const int32 Neighbour : NeighbouringFaces(Order[Cursor]))
		{
			if (Neighbour != INDEX_NONE && !bVisited[Neighbour])
			{
				bVisited[Neighbour] = true;
				Order.Add(Neighbour);
			}
		}
	}

	return Order;
}

double FPolySnapBuckyballFixture::InteriorDihedralDegrees(int32 FaceIndexA, int32 FaceIndexB) const
{
	// On a convex solid the interior angle is the supplement of the angle between outward normals.
	const double NormalAngle = FMath::RadiansToDegrees(
		FMath::Acos(FMath::Clamp(Faces[FaceIndexA].Normal | Faces[FaceIndexB].Normal, -1.0, 1.0)));

	return 180.0 - NormalAngle;
}
