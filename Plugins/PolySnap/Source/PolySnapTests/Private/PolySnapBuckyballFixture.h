// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * A truncated icosahedron, generated for the Milestone 2 test and for nothing else.
 *
 * This lives in the test module deliberately. PolySnap has no notion of a buckyball and must not
 * grow one: a shell of hexes and pents is something a developer or a player assembles out of the
 * parts to hand, not a shape the plugin knows how to make. What the fixture provides is what a
 * player's hands would have provided -- which part goes where, in what order, and roughly at what
 * pose -- and every transform that ends up in the world is still solved by PolySnap itself.
 *
 * Built by truncating an icosahedron rather than from a table of coordinates: each of the 30 edges
 * contributes two vertices at a third and two thirds along, giving 60; each of the 12 icosahedron
 * vertices becomes a pentagon and each of the 20 faces a hexagon, giving 32 faces and 90 edges.
 * Adjacency falls out of which faces share two vertices, so nothing has to be tabulated and got
 * right by hand.
 */
enum class EPolySnapFixtureFaceKind : uint8
{
	Pentagon,
	Hexagon
};

/** One face: a ring of vertex indices in order around it, with the plane it lies in. */
struct FPolySnapFixtureFace
{
	EPolySnapFixtureFaceKind Kind = EPolySnapFixtureFaceKind::Hexagon;

	/** Vertex indices in ring order about the face's outward normal, wound the same way on every face. */
	TArray<int32> VertexIndices;

	FVector Centre = FVector::ZeroVector;

	/** Unit normal, pointing away from the solid's centre. */
	FVector Normal = FVector::UpVector;
};

/** One edge, and the two faces that meet along it. */
struct FPolySnapFixtureEdge
{
	int32 VertexA = INDEX_NONE;
	int32 VertexB = INDEX_NONE;
	int32 FaceA = INDEX_NONE;
	int32 FaceB = INDEX_NONE;
};

class FPolySnapBuckyballFixture
{
public:
	/** @param EdgeLengthUu The length every one of the 90 edges is to have. */
	explicit FPolySnapBuckyballFixture(double EdgeLengthUu);

	[[nodiscard]] const TArray<FVector>& GetVertices() const { return Vertices; }
	[[nodiscard]] const TArray<FPolySnapFixtureFace>& GetFaces() const { return Faces; }
	[[nodiscard]] const TArray<FPolySnapFixtureEdge>& GetEdges() const { return Edges; }

	/** How many vertices a face of this kind has. */
	[[nodiscard]] static int32 VertexCount(EPolySnapFixtureFaceKind Kind)
	{
		return Kind == EPolySnapFixtureFaceKind::Pentagon ? 5 : 6;
	}

	/** The midpoints of a face's edges, in the same ring order as its vertices. */
	[[nodiscard]] TArray<FVector> EdgeMidpoints(int32 FaceIndex) const;

	/** The faces sharing an edge with this one, in ring order. */
	[[nodiscard]] TArray<int32> NeighbouringFaces(int32 FaceIndex) const;

	/**
	 * Faces in breadth-first order from the first pentagon, which is the order the test builds in.
	 *
	 * Breadth-first is what makes every placement after the second one a closure: a face reached
	 * this way always touches at least two faces already placed, so its fold is decided by the
	 * geometry already built rather than by the pose it was spawned at.
	 */
	[[nodiscard]] TArray<int32> BuildOrder() const;

	/**
	 * The interior dihedral between two adjacent faces, in degrees: 180 for coplanar, less for a
	 * convex fold. 138.19 between two hexagons, 142.62 between a pentagon and a hexagon.
	 */
	[[nodiscard]] double InteriorDihedralDegrees(int32 FaceIndexA, int32 FaceIndexB) const;

private:
	TArray<FVector> Vertices;
	TArray<FPolySnapFixtureFace> Faces;
	TArray<FPolySnapFixtureEdge> Edges;
};
