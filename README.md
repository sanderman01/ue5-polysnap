# Construction

A game about building pressurised structures in microgravity, in orbit — and, more
immediately, a testbed for the reusable plugin modules that game will be built from.

Unreal Engine 5.8, Linux. See [CLAUDE.md](CLAUDE.md) for working conventions (build commands,
code style, plugin isolation rules). This document describes *what* we are building and *why*;
CLAUDE.md describes *how* to work on it.

---

## 1. The idea

The player is an astronaut in microgravity, moving under Newtonian physics — closer to
*Hardspace: Shipbreaker* than to a walking sim. They pull construction panels from a rack or
container, manoeuvre them into place, and snap them together into airtight structures.

The play area is centred on a beacon or service vessel and is deliberately small. World scale
and floating-point precision are **not** concerns for this project; no origin rebasing, no
large-world-coordinates work.

### Why not a grid

Games in this space — *Space Engineers*, *Stationeers* — snap everything to cells of a regular
grid. That is a good fit for boxy, axis-aligned bases and a bad fit for what this game is about.

We want **spherical and tubular pressure vessels** assembled from pentagonal and hexagonal
panels, like a geodesic dome or a buckyball. Those panels meet at irrational, non-orthogonal
dihedral angles that no regular grid can express. A truncated icosahedron, for instance, needs
roughly 138.2° between two hexagons and roughly 142.6° between a pentagon and a hexagon.

So instead of snapping to *positions in a global grid*, pieces snap to *each other*, through
explicitly authored connection points. Validity becomes a local property of a pair of sockets
rather than a global property of world position. This is the core bet of the project.

---

## 2. PolySnap — the core plugin

`PolySnap` is the node-based snapping system. It is the first and most important plugin, and
it must remain fully independent of this game project (see §6).

### 2.1 Concepts

| Concept | What it is |
| --- | --- |
| **Piece** | A placeable object — a hull panel, a strut, a hatch. Owns a set of sockets. |
| **Socket** | A named, oriented connection point on a piece, with a type, a size, and constraints. |
| **Connection** | A realised link between two compatible sockets on two pieces. |
| **Assembly** | The connected graph of pieces and connections. |
| **Weld group** | A set of pieces within an assembly that have been welded into one rigid body. |

### 2.2 Sockets

Sockets are authored in Blender as empties parented to the mesh and exported via FBX. Unreal
imports FBX nodes prefixed `SOCKET_` as static mesh sockets, stripping the prefix — so the
remainder of the name is ours to use as an encoded descriptor of type, size, and role.

**The exact naming scheme and metadata encoding are not yet decided** — see §7. What *is*
decided is that the empty's name carries the metadata, rather than a parallel data asset that
could drift out of sync with the mesh.

There are two broad classes of socket:

- **Edge sockets** — at the midpoint of each edge of a panel. These join pieces structurally.
- **Attachment sockets** — mounting points for equipment, wiring, pipes, and similar. Same
  underlying primitive, different type namespace; they participate in the graph too, so that
  systems can ask what a piece has attached to it.

### 2.3 Socket orientation — the load-bearing convention

Each edge socket defines an orthonormal basis:

- **Outward** — in the plane of the panel, perpendicular to the edge, pointing away from the
  piece's centre.
- **Tangent** — along the edge.
- **Normal** — the panel's surface normal, pointing to the *outside* of the finished structure.

Mating two edge sockets means:

1. Their positions coincide.
2. Their tangents are collinear and opposed (`Tangent_A == -Tangent_B`), which keeps panel
   winding consistent and prevents mirrored assemblies.

That leaves **exactly one degree of freedom**: rotation about the shared edge axis. That
scalar is the **dihedral angle** between the two panels — measured through the structure's
interior, so 180° means the panels are coplanar and smaller values fold them inward.

This is why the "permissible angles" metadata is tractable: it constrains one number, not an
orientation. A socket pair that permits a single dihedral value yields a **rigid** joint; a
pair that permits a range yields a **hinge**. A buckyball is then just two dihedral values
applied consistently across the panel set.

### 2.4 Compatibility and snapping

A candidate connection is evaluated as:

1. **Type and size match** — a large hex edge does not mate with a small pent edge. Cheap
   rejection first.
2. **Proximity** — socket positions within a tolerance.
3. **Orientation** — tangents opposable within a tolerance.
4. **Dihedral admissibility** — the resulting angle lies within both sockets' permitted range.
5. **Occupancy** — neither socket is already connected.

When a candidate passes, the piece is snapped: its transform is solved from the target
socket's basis so that the mating conditions hold exactly, rather than approximately. Snapping
should be *exact* — accumulated error is what stops a sphere from closing, and a closed
buckyball is the acceptance test for this whole subsystem.

### 2.5 Physics and welding

The assembly has two regimes:

- **Loose** — connected pieces remain individual rigid bodies joined by physics constraints.
  The structure can flex, wobble, and be pushed around while under construction. This is what
  makes microgravity assembly feel physical rather than administrative.
- **Welded** — once the player is satisfied, welding merges a set of pieces into a **single
  rigid body** with one collision representation, and the constraints between them are
  removed. This is both the fiction (a sealed, structural join) and the performance strategy:
  long constraint chains are the thing that will otherwise degrade as structures grow.

**Critical invariant:** merging bodies must not merge *identities*. After welding, the graph
still knows about every individual piece, its sockets, and its connections. The merge is a
physics-representation optimisation, nothing more. Save/load, airtightness, and disassembly
all depend on per-piece identity surviving the weld.

Unwelding — cutting a structure back apart — should be assumed to be a requirement, and the
merge implemented so it can be reversed.

### 2.6 The assembly graph

Pieces are nodes; connections are edges. Once that exists, game systems get their answers by
walking the graph rather than by geometric queries:

- **Open edges** — sockets with no connection. Drives build UI, ghost previews, and the
  "what can I attach here" query.
- **Enclosure** — which sets of pieces bound a fully closed volume, and are therefore
  candidates to be treated as **airtight compartments** for atmosphere simulation.
- **Structural queries** — connectivity, reachability, what breaks off if this piece is cut.

The graph is derived from connections and should be maintained incrementally as pieces are
added and removed, not rebuilt by scanning the world.

### 2.7 Persistence

Save/load must reconstruct an assembly exactly: every piece's class, transform, and identity,
every connection, and every weld group.

This requires **stable numeric IDs**:

- Each piece instance gets an ID, unique within the save.
- Each socket is identified by a stable local index or ID within its piece.
- A connection is therefore `(PieceID_A, SocketID_A, PieceID_B, SocketID_B)`.

Socket IDs must be stable across mesh re-exports, or existing saves break when art is updated.
This is a real constraint on whatever naming/metadata scheme §2.2 settles on.

### 2.8 Replication-readiness

Multiplayer is **not** being implemented now, but PolySnap should be shaped so co-op does not
require a rewrite:

- Snapping is expressed as a **validated operation on the graph** ("connect socket X to socket
  Y"), not as a client directly writing a transform. Validation is a pure function of the two
  sockets and the graph state, so a server can re-run it.
- The numeric IDs from §2.7 double as network identity.
- Physics-driven wobble is treated as cosmetic; the graph is the authority on what is
  connected to what.

This costs a little discipline now and avoids an architectural rewrite later.

---

## 3. Player movement (test harness only)

A simple Newtonian free-flying pawn — RCS-style translation and rotation, no gravity — is
needed to exercise the system: approach a rack, grab a piece, manoeuvre it, snap it.

This is **test scaffolding, not a product**. It lives in the `Construction` game module (or as
a Blueprint in `Content/`), never in PolySnap. A snapping plugin has no business knowing how
the player moves.

---

## 4. Content

Construction pieces — the hex and pent panels, struts, hatches — live in this project's
`Content/` directory, **not** inside the plugin. PolySnap supplies the types, rules, and
runtime behaviour; the panels are game content that happens to use it.

This is the right split for a plugin meant to be reused: a different game would bring its own
panels and get the same snapping system.

---

## 5. Milestones

1. **Two pieces snap edge-to-edge in PIE.** One piece type, one socket type. Proximity,
   orientation, and dihedral checks; exact snap transform. This proves the socket math before
   anything is built on top of it.
2. **A closed buckyball.** Hexes and pents assembling into a sealed truncated icosahedron
   without accumulated drift. The real geometric test.
3. **Assembly graph and open-edge queries.**
4. **Welding** — merge to a single rigid body, identities preserved, reversible.
5. **Save/load** of a complete assembly.
6. **Enclosure detection** — identify airtight volumes.

Attachment sockets, atmosphere simulation, and equipment come after the structural core works.

---

## 6. Plugin boundaries

Restating the rule from CLAUDE.md because it is the whole point of this project:

**PolySnap must have zero dependencies on the `Construction` game module.** No includes, no
references to project content, no assumptions about the player pawn or game mode. The game
module is a thin harness that wires the plugin into a test level. If PolySnap only works
because of something in `Construction`, that is a bug, not a shortcut.

Where a new feature belongs — new plugin, existing plugin, or game module — is decided
per feature, in conversation, not assumed.

---

## 7. Open questions

Marked explicitly so nobody builds on them as though they were settled.

- **Socket naming scheme and metadata encoding.** Decided: the name carries the metadata.
  Undecided: the actual grammar, how size and type are namespaced, how permissible-angle
  ranges are expressed, and how socket identity stays stable across mesh re-export (§2.7).
- **Magnets.** The idea of magnet sockets to assist alignment is open. Current recommendation:
  do *not* build a separate magnet plugin yet. Assisted alignment is better understood as an
  attraction behaviour of the existing socket search — pulling a held piece toward the best
  candidate socket pair — which needs no new concept. Revisit only if magnets turn out to need
  behaviour that socket attraction genuinely cannot express.
- **Weld merge mechanics.** Which merge strategy preserves reversibility at acceptable cost.
- **Enclosure algorithm.** How enclosure is actually determined — graph cycle analysis, or a
  geometric test — and how it handles hatches, which are openable holes in an otherwise
  sealed surface.
- **Tolerances.** Snap distance and angular thresholds; whether they are global, per socket
  type, or scaled by piece size.
- **Framework choices.** Enhanced Input, GAS, StateTree, and similar are undecided and will be
  chosen per feature rather than adopted wholesale up front.
