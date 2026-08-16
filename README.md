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
| **Socket** | A named, oriented connection point on a piece, with a type and a size. |
| **Joint** | A shared edge line in space. Hosts two or more sockets — a seam, or a T-junction. |
| **Connection** | A socket's participation in a joint. |
| **Assembly** | The connected graph of pieces and joints. |
| **Weld group** | A set of pieces within an assembly that have been welded into one rigid body. |

### 2.2 Sockets

Sockets are authored in Blender as empties parented to the mesh and exported via FBX. Unreal
imports FBX nodes prefixed `SOCKET_` as static mesh sockets, stripping the prefix — so the
remainder of the name is ours to use as an encoded descriptor of type, size, and role.

The empty's name carries the metadata, rather than a parallel data asset that could drift out
of sync with the mesh.

There are two broad classes of socket:

- **Edge sockets** — at the midpoint of each edge of a panel. These join pieces structurally.
- **Attachment sockets** — mounting points for equipment, wiring, pipes, and similar. Same
  underlying primitive, different type namespace; they participate in the graph too, so that
  systems can ask what a piece has attached to it.

#### The naming grammar

```
SOCKET_<Type>_<ID>_<SubType>_<Size>

SOCKET_Edge_001_Straight_200
```

After import the engine strips the prefix, so the socket is named `Edge_001_Straight_200`.

| Field | Example | Meaning |
| --- | --- | --- |
| `SOCKET_` | `SOCKET_` | Required prefix. How Unreal recognises the empty as a socket. |
| `Type` | `Edge` | What kind of socket. `Edge` is structural; `Attach` is for equipment. Determines how the remaining fields are read. |
| `ID` | `001` | Identity within the piece. Three digits, zero-padded, `001`–`999`. |
| `SubType` | `Straight` | Edge geometry. **Only `Straight` is in scope** — curves and circles are a later extension. |
| `Size` | `200` | Edge length in centimetres (= Unreal units). Integers only. |

**`SOCKET_<Type>_<ID>` is a fixed-arity head; everything after it is type-specific and of
variable arity.** This is why the ID sits third rather than last: when curved edges arrive they
will need more than one parameter (a radius *and* an arc length), and they can extend the tail
without disturbing how every socket's identity is parsed.

**Compatibility:** two edge sockets may connect only when `Type`, `SubType`, and `Size` all
match. `Straight_200` mates only with `Straight_200`. The `ID` never participates in
compatibility — it is identity, not classification.

**Why `SubType` and `Size` stay separate fields** rather than a fused `Straight200`: splitting
a fused token requires scanning for the alpha/digit boundary, which breaks as soon as a subtype
name contains a digit; and a subtype with two parameters — as curves will have — cannot be
expressed as "name plus one number" at all. The compatibility key is the tuple either way.

#### Field rules

- **`ID` is permanent.** It is what save games store (§2.9), so it must never be renumbered or
  reused. If a socket is removed, retire its ID rather than reassigning it. Changing a panel's
  size changes its `Size` field but must leave IDs untouched.
- **`Size` is an integer** number of centimetres. There is no decimal form; design panels to
  whole-centimetre edges.
- **No underscores inside a field.** The underscore is the delimiter.
- **Case:** write the prefix as `SOCKET_`. The importer's comparison is case-insensitive, so
  `Socket_` also works, but there is no reason to depend on that.

#### Authoring traps

- **Blender's `.001` duplicate suffix.** Duplicating an empty produces
  `SOCKET_Edge_003_Straight_200.001` — note the trailing `.001` is Blender's suffix and has
  nothing to do with the three-digit `ID` field, which is easy to misread. Validation must
  **reject** this rather than silently stripping the suffix: a duplicated socket also carries a
  duplicated `ID`, which is a real error the artist has to fix. Auto-stripping would hide it.
- **The empty must export as an FBX null.** The importer only accepts `eNull` (and skeleton)
  attributes, which is what a Blender empty parented to the mesh produces.

#### Validation and parsing

Malformed names must fail **loudly at import**, not mysteriously at runtime. This is the job
of PolySnap's editor module: parse every socket on a piece and report malformed names,
duplicate IDs, unknown subtypes, and unparseable sizes.

Names are parsed **once** into a struct and cached — never re-parsed per frame, and never
string-compared during a snap query. Runtime compatibility tests operate on the parsed fields.

### 2.3 Socket orientation — the load-bearing convention

Each edge socket defines an orthonormal basis:

- **Outward** — in the plane of the panel, perpendicular to the edge, pointing away from the
  piece's centre.
- **Tangent** — along the edge.
- **Normal** — the panel's surface normal. Which of the panel's two faces this points to is an
  arbitrary authoring convention; it does **not** mean "outside of the structure" (§2.6).

Mating two edge sockets means:

1. Their positions coincide.
2. Their tangents are collinear — `Tangent_B == ±Tangent_A`.

That leaves **exactly one continuous degree of freedom**: rotation about the shared edge axis.
That scalar is the **dihedral angle** between the two panels — measured through the
structure's interior, so 180° means the panels are coplanar and smaller values fold them
inward.

#### Flipping — the two discrete solutions

The `±` in the tangent condition is a genuine choice, not slack in the constraint. With a
right-handed basis (`Outward × Tangent = Normal`), the two cases are:

- **`Tangent_B == -Tangent_A`** — the panels' normals land on the same side. Surface
  orientation is consistent across the joint.
- **`Tangent_B == +Tangent_A`** — piece B's normal is reversed; its outward face now points
  into the structure.

The second case is **not** a mirror image. It is a proper 180° rotation of the piece about its
socket's Outward axis, which reverses tangent and normal together and leaves chirality
untouched. It is a motion the player can physically perform by turning the panel over, so it
is permitted by default.

Consequences:

- **Mating is two-valued.** Each socket pair yields two candidate transform families, each
  with its own dihedral DOF. The snapper resolves this by choosing the admissible solution
  requiring the **least rotation from the piece's current orientation** — the player turns the
  piece roughly the way they want it and the snap commits to that reading. No explicit flip
  control is needed.
- **Flipping is always permitted.** No socket declares a preferred face and no piece has an
  authored "inside". See §2.6 — this is a deliberate design decision, not an unimplemented
  check.
- **Flipping relocates attachment sockets** (§2.2) from the hull interior to the exterior.
  That is a real and intended gameplay consequence, owned by the player.
- **Surface orientation is not globally guaranteed.** Panel normals do not agree on which
  side is inside, so nothing may treat them as if they do. Inside/outside is derived from the
  enclosed volume (§2.8).

That free DOF is not a problem to be constrained away. It is the mechanism — see §2.4.

### 2.4 Angles are emergent, and joints host many edges

#### Nothing authors an angle

**Sockets do not declare permitted dihedral angles.** There is no angle vocabulary, no
per-socket range, no authored list of shapes the system knows how to build.

A single connection leaves the dihedral free: the piece is a **hinge**, swinging about the
shared edge. What removes that freedom is the **next** connection. When a second edge of the
same piece also mates, the angle is no longer free — it is whatever the geometry requires.
Three panels closing a vertex determine their dihedrals exactly.

This is why the hex-hex angle of a truncated icosahedron is ~138.19°: not because anyone wrote
that number down, but because that is what regular hexagons and pentagons closing a vertex
*imply*. Author the panel shapes and the angles fall out.

The consequences are the point:

- **Any shape the panel set admits is buildable** — spheres, domes, tubes, irregular hulls,
  things we have not thought of. The system has no opinion about the target shape.
- **New panel geometry needs no new metadata.** Cut a new panel in Blender, and the angles it
  can form are already implied by its outline.
- **Rigidity is emergent, not declared.** One connection: a hinge. Two or more: progressively
  constrained. Welding freezes whatever the structure settled into.

The only angle limit worth keeping is **mechanical** — an edge profile that would
self-intersect if folded too far. That is a fact about the mesh, not a design constraint on
shape, and collision can express it. Treat any such clamp as optional and geometric; never as
a way of steering the player toward intended shapes.

#### Joints: an edge line can host more than two panels

A connection is not restricted to a pair. Several panels may meet along **one shared edge
line** — an internal bulkhead meeting a hull seam, a partition dividing a cylinder, a spine
with panels fanning off it.

So the graph gains a first-class concept:

> A **Joint** is a shared edge line in space. Sockets attach *to a joint*, not directly to each
> other. A two-socket joint is an ordinary seam; an N-socket joint is a T-junction or fan.

With N sockets on a joint there are N−1 independent dihedral angles about the shared axis, all
free until further connections constrain them. Everything above still holds; the joint is
simply where the shared axis lives.

Practically this means **socket occupancy is a capacity question, not a boolean**. "Is this
socket taken?" becomes "does this joint accept another participant?" — and by default it does.

Note this covers panels sharing a *whole* edge line. A long edge spanned by several shorter
edges end-to-end is a different problem, and is deliberately **not** solved here (§7).

#### Closing a loop

With free angles, a ring of panels will rarely close to floating-point exactness. When the
final socket pair lands within tolerance, **connect, and let the physics constraints absorb
the residual strain.**

This is deliberately the simple option, and it is nearly free: the constraint solver is
already an iterative relaxation solver, so letting the assembly settle distributes the error
around the loop by itself — which is what a bespoke closure solver would do, without the extra
machinery and without yanking already-placed pieces out from under the player. Welding then
freezes the settled geometry.

This holds **only if per-connection snapping stays exact**. Visible drift when closing a
buckyball is a bug in the snap transform, not a tolerance to widen. Milestone 2 should measure
the residual, not eyeball it.

### 2.5 Compatibility and snapping

A candidate connection is evaluated as:

1. **Descriptor match** — `Type`, `SubType`, and `Size` all equal (§2.2). `Straight_200` mates
   only with `Straight_200`. Cheap integer comparison, so reject on this first.
2. **Proximity** — socket positions within a tolerance.
3. **Orientation** — tangents collinear within a tolerance, in either polarity.
4. **Joint capacity** — the joint accepts another participant (§2.4). This replaces a simple
   occupancy test, and by default it passes.

Note what is *absent*: no dihedral check. The resulting angle is whatever the placement
produces, and remains free until further connections constrain it (§2.4).

Both polarities are always admissible (§2.3), so a passing socket pair yields two candidate
solutions and the snapper picks the one requiring least rotation from the piece's current
orientation.

When a candidate passes, the piece is snapped: its transform is solved from the target
socket's basis so that the mating conditions hold exactly, rather than approximately. Snapping
should be *exact* — accumulated error is what stops a sphere from closing, and a closed
buckyball is the acceptance test for this whole subsystem.

### 2.6 No authored inside or outside

**Pieces do not have an authored interior face, and sockets never constrain which way round a
piece is fitted.** The player decides, always.

The reasoning: inside/outside is not a property of a *joint*, so encoding it in the socket
constraint system puts the check in the wrong layer. Whether a placement is *sensible* depends
on the environment the piece ends up in — which is a runtime question the enclosure system
(§2.8) already answers — not on a flag set at authoring time.

Consequences, which are features rather than costs:

- **Hatches work both ways round.** A hatch is a hole that can be sealed; it does not care
  which side the pressure is on.
- **Racks do not care at all**, unless something stored in them needs an atmosphere — and that
  is a property of the stored item, checked at runtime.
- **Equipment may or may not function as intended** in vacuum versus atmosphere. That is
  gameplay, discovered by the player, not a placement rule that forbids the build.

So the rule is: **PolySnap decides whether pieces can physically connect. It never decides
whether the result is a good idea.** Environmental suitability is a separate, runtime concern
belonging to the equipment and atmosphere systems, which query the enclosure graph.

This keeps PolySnap's responsibility narrow, removes a class of authoring decisions from every
new piece, and lets the player build things we did not anticipate.

### 2.7 Physics and welding

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

### 2.8 The assembly graph

The graph is **bipartite**: pieces and joints are both nodes, and a connection links a piece's
socket to a joint. This is what lets a joint host three or more panels (§2.4) without the
graph needing a special case — a T-junction is just a joint of degree three.

Once that exists, game systems get their answers by walking the graph rather than by geometric
queries:

- **Open edges** — sockets participating in no joint. Drives build UI, ghost previews, and the
  "what can I attach here" query. Note a socket already in a joint may still accept more
  panels, so "open" and "available" are different questions.
- **Enclosure** — which sets of pieces bound a fully closed volume, and are therefore
  candidates to be treated as **airtight compartments** for atmosphere simulation.
- **Structural queries** — connectivity, reachability, what breaks off if this piece is cut.
- **Environment lookup** — which enclosed volume, if any, a given piece or attachment socket
  sits in. This is what lets equipment decide at runtime whether it is in vacuum or atmosphere
  (§2.6), instead of that being constrained at build time.

The boundary: PolySnap reports **topology** — what is connected, what is enclosed. It does not
model pressure, gas, or temperature. An atmosphere system layered on top consumes these
queries.

The graph is derived from connections and should be maintained incrementally as pieces are
added and removed, not rebuilt by scanning the world.

### 2.9 Persistence

Save/load must reconstruct an assembly exactly: every piece's class, transform, and identity,
every joint and its participants, and every weld group.

This requires **stable numeric IDs**:

- Each piece instance gets an ID, unique within the save.
- Each socket is identified by the `ID` field of its name (§2.2), unique within its piece.
  This is why that field is permanent and never reused.
- Each joint instance gets an ID, unique within the save.
- A connection is therefore `(JointID, PieceID, SocketID)` — one record per participating
  socket, so a joint of degree three saves as three records rather than needing a special case.

Socket IDs must be stable across mesh re-exports, or existing saves break when art is updated.
The grammar (§2.2) is built for this: `ID` is a field of its own, so re-cutting a panel changes
its `Size` while leaving every socket's identity intact.

### 2.10 Replication-readiness

Multiplayer is **not** being implemented now, but PolySnap should be shaped so co-op does not
require a rewrite:

- Snapping is expressed as a **validated operation on the graph** ("connect socket X to socket
  Y"), not as a client directly writing a transform. Validation is a pure function of the two
  sockets and the graph state, so a server can re-run it.
- The numeric IDs from §2.9 double as network identity.
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

1. **Two pieces snap edge-to-edge in PIE.** One piece type, one socket type. Proximity and
   orientation checks; exact snap transform; the resulting joint free to hinge. This proves
   the socket math before anything is built on top of it.
2. **A closed buckyball.** Hexes and pents assembling into a sealed truncated icosahedron,
   with the dihedral angles emerging from vertex closure rather than being authored (§2.4).
   The real geometric test — **measure** the residual gap at closure rather than eyeballing it.
3. **Assembly graph and open-edge queries**, including a joint of degree three.
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

- **Attachment socket fields.** The grammar's head (§2.2) is settled for all socket types, but
  what an `Attach` socket needs in its tail — mount class, load rating, orientation freedom —
  is undesigned. Deferred until equipment is a real feature.
- **Curved edge subtypes.** Only `Straight` is in scope. Curves will need a multi-parameter
  tail (radius and arc length), which the grammar is shaped to accommodate but which nothing
  has been designed for.
- **Collinear subdivision.** A long edge spanned by several shorter edges end-to-end — a 2 m
  panel meeting two 1 m panels — is *not* addressed by joints (§2.4), which cover panels
  sharing a whole edge line. It would need either sockets with extent along the edge, or
  several sockets authored along the long edge. Deferred deliberately; if it becomes a
  requirement it likely adds a length field to the naming grammar.
- **Joint capacity limits.** Whether a joint should ever refuse a participant, and on what
  grounds — physical interpenetration, or nothing at all.
- **Magnets.** The idea of magnet sockets to assist alignment is open. Current recommendation:
  do *not* build a separate magnet plugin yet. Assisted alignment is better understood as an
  attraction behaviour of the existing socket search — pulling a held piece toward the best
  candidate socket pair — which needs no new concept. Revisit only if magnets turn out to need
  behaviour that socket attraction genuinely cannot express.
- **Weld merge mechanics.** Which merge strategy preserves reversibility at acceptable cost.
- **Enclosure algorithm.** How enclosure is actually determined — graph cycle analysis, or a
  geometric test — and how it handles hatches, which are openable holes in an otherwise
  sealed surface. Because pieces have no authored facing (§2.6), panel normals cannot be
  assumed to agree on which side is "inside"; the algorithm must derive that from the enclosed
  volume itself.
- **Environment queries.** What exactly equipment asks for, and how cheaply. "Am I in an
  enclosed volume" is a graph query; "is that volume actually pressurised" belongs to a future
  atmosphere system (§2.8). The interface between them is undesigned.
- **Tolerances.** Snap distance and angular thresholds; whether they are global, per socket
  type, or scaled by piece size.
- **Framework choices.** Enhanced Input, GAS, StateTree, and similar are undecided and will be
  chosen per feature rather than adopted wholesale up front.
