# PolySnapSandbox

An Unreal Engine 5.8 project (Linux) containing functionality related to object snapping and
detection of enclosed volumes formed by connected pieces. A testbed for reusable plugin modules
for a game about building pressurised structures in microgravity, in orbit.

> **Work in progress.** Nothing here is stable — the design is still moving, most of it is
> unbuilt, and what is written down may change without notice. This project is being developed
> with generative LLM agentic coding tools.

| Document | Covers |
| --- | --- |
| [DESIGN.md](DESIGN.md) | Why the system is shaped this way: every decision, its reasoning, and the open questions. |
| [CONVENTIONS.md](CONVENTIONS.md) | The Blender↔Unreal contract — axes, units, socket basis, exact import/export settings. |
| [CODE_STYLE.md](CODE_STYLE.md) | How the C++ is written. |
| [CLAUDE.md](CLAUDE.md) | Architecture rules and how to work on the project. |

---

## The idea

The player is an astronaut in microgravity, moving under Newtonian physics — closer to *Hardspace:
Shipbreaker* than to a walking sim. They pull construction panels from a rack, manoeuvre them into
place, and snap them together into airtight structures.

The structures are **spherical and tubular pressure vessels** assembled from pentagons and
hexagons, like a geodesic dome or a buckyball. Panels like those meet at irrational,
non-orthogonal dihedral angles that no regular grid can express — which is why PolySnap does not
use one. Instead of snapping to positions in a global grid, pieces snap to *each other*, through
explicitly authored connection points (DESIGN [§1](DESIGN.md#1-the-idea)).

## Concepts

| Concept | What it is |
| --- | --- |
| **Piece** | A placeable object — a hull panel, a strut, a hatch. Owns a set of sockets. |
| **Socket** | A named, oriented connection point on a piece, with a type and a size. |
| **Joint** | A shared edge line in space. Hosts two or more sockets — a seam, or a T-junction. |
| **Connection** | A socket's participation in a joint. |
| **Assembly** | The connected graph of pieces and joints. |
| **Weld group** | A set of pieces within an assembly that have been welded into one rigid body. |

## How PolySnap works

**Sockets are authored in Blender and named.** An edge socket is an empty at the midpoint of a
panel edge, exported via FBX and imported as a static mesh socket. Its name carries its metadata:
`SOCKET_Edge_001_Straight_40_2000` — an ID, an edge subtype, a thickness and a length. Malformed
names fail loudly at import. (DESIGN [§2.2](DESIGN.md#22-sockets))

**Compatibility is a token match.** Two sockets may connect when their subtype, thickness and
length are equal. The size fields are opaque labels compared for equality, never measurements —
nothing in the snap math reads them. (DESIGN [§2.2](DESIGN.md#22-sockets))

**A socket defines a basis, and mating leaves one degree of freedom.** Outward, Tangent and Normal
are orthonormal; mating two sockets makes their positions coincide and their tangents collinear,
which leaves rotation about the shared edge — the dihedral angle — free. (DESIGN
[§2.3](DESIGN.md#23-socket-orientation--the-load-bearing-convention))

**Angles are emergent.** Nothing authors a dihedral angle. One connection is a hinge; the next
connection selects an angle out of that hinge family. The ~138.19° hex-to-hex angle of a truncated
icosahedron is what regular panels closing a vertex imply, not a number anyone wrote down. Author
the panel shapes and the angles fall out. (DESIGN
[§2.4](DESIGN.md#24-angles-are-emergent-and-joints-host-many-edges))

**Joints host more than two panels.** Sockets attach to a joint — a shared edge line — rather than
directly to each other, so a T-junction where a bulkhead meets a hull seam is an ordinary joint of
degree three. (DESIGN [§2.4](DESIGN.md#24-angles-are-emergent-and-joints-host-many-edges))

**One anchor is exact; the rest are adopted.** A placement solves the piece's transform from
exactly one socket pair — the one the player is driving — and every other pair that lands within
tolerance is adopted into its joint, recording the gap it closed at. That residual is the
project's measure of whether the panels are cut correctly. (DESIGN
[§2.5](DESIGN.md#25-compatibility-and-snapping))

**The graph answers the structural questions.** Pieces and joints form a bipartite graph, and open
edges, connectivity and enclosure are walks over it rather than geometric queries. PolySnap
reports topology — what is connected, what is enclosed — and never models pressure or gas. (DESIGN
[§2.8](DESIGN.md#28-the-assembly-graph))

**Loose, then welded.** Connected pieces stay individual rigid bodies joined by physics
constraints while under construction, so the structure flexes and can be pushed around. Welding
merges a set into one rigid body and freezes its geometry, without merging the pieces' identities.
(DESIGN [§2.7](DESIGN.md#27-physics-and-welding))

## Milestones

1. [x] **Two pieces snap edge-to-edge in PIE.** One piece type, one socket type — proves the socket
   math.
2. [ ] **A closed buckyball.** Hexes and pents assembling into a sealed truncated icosahedron, with the
   worst adoption residual across the ninety edges reported rather than eyeballed.
3. [ ] **Assembly graph and open-edge queries**, including a joint of degree three.
4. [ ] **Welding** — merge to a single rigid body, identities preserved, reversible.
5. [ ] **Save/load** of a complete assembly.
6. [ ] **Enclosure detection** — identify airtight volumes.

Atmosphere simulation and equipment come after the structural core works, and neither is
PolySnap's.

## Plugin boundaries

**PolySnap must have zero dependencies on the `Sandbox` game module** — no includes, no references
to project content, no assumptions about the player pawn or game mode. The game module is a thin
harness that wires the plugin into a test level, and if PolySnap only works because of something
in `Sandbox`, that is a bug rather than a shortcut. Where a new feature belongs is decided per
feature, in conversation, not assumed.
