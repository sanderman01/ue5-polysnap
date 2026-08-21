// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "Misc/AutomationTest.h"
#include "PolySnapBuckyballFixture.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FPolySnapBuckyballSpec, "PolySnap.Buckyball",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** The edge length the fixture is asked for, and therefore the one every edge must come out at. */
static constexpr double EdgeUu = 200.0;

END_DEFINE_SPEC(FPolySnapBuckyballSpec)

void FPolySnapBuckyballSpec::Define()
{
	// The fixture is what tells the Milestone 2 test which part goes where. If its geometry is
	// wrong the measurement it produces is meaningless, and it would look like a snapping bug --
	// so the shape is checked here, where it costs nothing, rather than inferred from a shell that
	// refused to close.
	Describe("the truncated icosahedron",
		[this]()
		{
			It("has 60 vertices, 32 faces and 90 edges",
				[this]()
				{
					const FPolySnapBuckyballFixture Fixture(EdgeUu);

					TestEqual("vertices", Fixture.GetVertices().Num(), 60);
					TestEqual("faces", Fixture.GetFaces().Num(), 32);
					TestEqual("edges", Fixture.GetEdges().Num(), 90);
				});

			It("is twelve pentagons and twenty hexagons",
				[this]()
				{
					const FPolySnapBuckyballFixture Fixture(EdgeUu);

					int32 Pentagons = 0;
					for (const FPolySnapFixtureFace& Face : Fixture.GetFaces())
					{
						const bool bIsPentagon = Face.Kind == EPolySnapFixtureFaceKind::Pentagon;
						Pentagons += bIsPentagon ? 1 : 0;

						TestEqual("ring length", Face.VertexIndices.Num(), bIsPentagon ? 5 : 6);
					}

					TestEqual("pentagons", Pentagons, 12);
					TestEqual("hexagons", Fixture.GetFaces().Num() - Pentagons, 20);
				});

			It("cuts every one of the ninety edges to the same length",
				[this]()
				{
					// The whole premise of the test asset: pentagon and hexagon panels share an edge
					// length, so any socket may mate with any other. An edge set that is not uniform
					// would show up later as a residual and be blamed on the snapper.
					const FPolySnapBuckyballFixture Fixture(EdgeUu);

					for (const FPolySnapFixtureEdge& Edge : Fixture.GetEdges())
					{
						TestEqual("edge length",
							FVector::Dist(Fixture.GetVertices()[Edge.VertexA], Fixture.GetVertices()[Edge.VertexB]),
							EdgeUu, 1.0e-9);
					}
				});

			It("gives every edge exactly two faces",
				[this]()
				{
					// What makes the shell closed: no edge is left with one side, so a completed
					// assembly has no open sockets at all.
					const FPolySnapBuckyballFixture Fixture(EdgeUu);

					for (const FPolySnapFixtureEdge& Edge : Fixture.GetEdges())
					{
						TestTrue("first face", Edge.FaceA != INDEX_NONE);
						TestTrue("second face", Edge.FaceB != INDEX_NONE);
					}
				});

			It("surrounds every pentagon with hexagons, and alternates around every hexagon",
				[this]()
				{
					// The rule that decides which part the test spawns at each open socket. A
					// pentagon never touches a pentagon, and a hexagon's neighbours alternate.
					const FPolySnapBuckyballFixture Fixture(EdgeUu);
					const TArray<FPolySnapFixtureFace>& Faces = Fixture.GetFaces();

					for (int32 Index = 0; Index < Faces.Num(); ++Index)
					{
						const TArray<int32> Neighbours = Fixture.NeighbouringFaces(Index);
						TestEqual("neighbour count", Neighbours.Num(), Faces[Index].VertexIndices.Num());

						if (Faces[Index].Kind == EPolySnapFixtureFaceKind::Pentagon)
						{
							for (const int32 Neighbour : Neighbours)
							{
								TestTrue("pentagons touch only hexagons",
									Faces[Neighbour].Kind == EPolySnapFixtureFaceKind::Hexagon);
							}

							continue;
						}

						int32 PentagonNeighbours = 0;
						for (int32 Step = 0; Step < Neighbours.Num(); ++Step)
						{
							const bool bIsPentagon = Faces[Neighbours[Step]].Kind == EPolySnapFixtureFaceKind::Pentagon;
							PentagonNeighbours += bIsPentagon ? 1 : 0;

							TestNotEqual("neighbours alternate", bIsPentagon,
								Faces[Neighbours[(Step + 1) % Neighbours.Num()]].Kind
									== EPolySnapFixtureFaceKind::Pentagon);
						}

						TestEqual("three of a hexagon's six neighbours are pentagons", PentagonNeighbours, 3);
					}
				});

			It("folds to 138.19 degrees between hexagons and 142.62 against a pentagon",
				[this]()
				{
					// The two angles no grid can express, and the reason PolySnap exists at all
					// (DESIGN section 1).
					const FPolySnapBuckyballFixture Fixture(EdgeUu);
					const TArray<FPolySnapFixtureFace>& Faces = Fixture.GetFaces();

					for (const FPolySnapFixtureEdge& Edge : Fixture.GetEdges())
					{
						const bool bTouchesPentagon = Faces[Edge.FaceA].Kind == EPolySnapFixtureFaceKind::Pentagon
												   || Faces[Edge.FaceB].Kind == EPolySnapFixtureFaceKind::Pentagon;

						TestEqual(bTouchesPentagon ? TEXT("pentagon to hexagon")
												   : TEXT("hexagon to hexagon"),
														 Fixture.InteriorDihedralDegrees(Edge.FaceA, Edge.FaceB),
														 bTouchesPentagon ? 142.62263 : 138.18969, 1.0e-4);
					}
				});

			It("orders the build so that every face after the second touches two already placed",
				[this]()
				{
					// What makes the assembly a chain of closures rather than a chain of guesses: a
					// face reached breadth-first always has two placed neighbours, so its fold is
					// decided by the geometry already built.
					const FPolySnapBuckyballFixture Fixture(EdgeUu);
					const TArray<int32> Order = Fixture.BuildOrder();

					TestEqual("every face is built", Order.Num(), Fixture.GetFaces().Num());
					TestEqual("starts on a pentagon", static_cast<int32>(Fixture.GetFaces()[Order[0]].Kind),
						static_cast<int32>(EPolySnapFixtureFaceKind::Pentagon));

					TArray<int32> Placed;
					for (const int32 FaceIndex : Order)
					{
						int32 PlacedNeighbours = 0;
						for (const int32 Neighbour : Fixture.NeighbouringFaces(FaceIndex))
						{
							PlacedNeighbours += Placed.Contains(Neighbour) ? 1 : 0;
						}

						if (Placed.Num() >= 2)
						{
							TestTrue(FString::Printf(TEXT("face %d has two placed neighbours"), FaceIndex),
								PlacedNeighbours >= 2);
						}

						Placed.Add(FaceIndex);
					}
				});
		});
}

#endif // WITH_DEV_AUTOMATION_TESTS
