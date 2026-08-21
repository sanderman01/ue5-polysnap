# PolySnap — Design Record

The decisions behind the system and the reasons for them. [README.md](README.md) is the overview;
[CONVENTIONS.md](CONVENTIONS.md) is the mechanical half — axes, units, and the exact
Blender↔Unreal settings. Section numbers here are stable and are cited from the code.

---

## 1. The idea

An astronaut in microgravity under Newtonian physics — closer to *Hardspace: Shipbreaker* than to
a walking sim — pulls panels from a rack and snaps them into airtight structures. The play area is
small, so world scale and float precision are **not** concerns: no origin rebasing, no large-world
coordinates.

**Why not a grid.** Spherical and tubular pressure vessels built from pentagons and hexagons meet
at irrational, non-orthogonal dihedral angles no grid can express — a truncated icosahedron needs
~138.2° hex-to-hex and ~142.6° pent-to-hex. So parts snap to *each other* through authored
connection points, and validity becomes a local property of a pair of sockets rather than a global
property of world position. That is the core bet.

---

## 2. PolySnap — the core plugin

### 2.1 Concepts

Part, Socket, Joint, Connection, Assembly, Weld group — tabulated in
[README.md](README.md#concepts).

**Why "part", and why the component is a "connector".** That list is mechanical-assembly
vocabulary, so the word for the thing being assembled has to belong to it too: parts are joined by
joints into assemblies, in CAD and here. The word was **piece** first, which belongs to the jigsaw
register and sat oddly beside "weld group". The component keeps a name of its own because *part*
and *component* are near-synonyms, so `UPolySnapPartComponent` would say the same thing twice.
`UPolySnapConnectorComponent` says what the component does instead: it connects its actor into an
assembly. The code holds the distinction. **Part** is the domain noun, in identifiers like
`HeldPart` and `RegisteredParts`. **Connector** names the component object where the code means
the component itself, as `CommitConnection`'s `HeldConnector` and `TargetConnector` do.

### 2.2 Sockets

Sockets are Blender empties parented to the mesh, exported via FBX. Unreal imports nodes prefixed
`SOCKET_` as static mesh sockets and strips the prefix, so the rest of the name is ours to encode
metadata in — rather than a parallel data asset that could drift out of sync with the mesh.

**PolySnap works on edge sockets and nothing else**: an edge socket sits at an edge midpoint and
joins parts structurally. Mounts for equipment, wiring and pipes belong to another plugin (§7).

#### The naming grammar

```
SOCKET_Edge_<ID>_<SubType>_<Thickness>_<Length>[.<AuthoringTail>]

SOCKET_Edge_001_Straight_40_2000.Pent   ->   Edge_001_Straight_40_2000_Pent   after import
```

Import strips `SOCKET_` **and rewrites the tail's `.` to `_`** — the second spelling is the only
one the parser ever sees.

| Field | Example | Meaning |
| --- | --- | --- |
| `SOCKET_` | `SOCKET_` | Required prefix. How Unreal recognises the empty as a socket. |
| `Edge` | `Edge` | Required namespace tag. Marks the socket as PolySnap's. Always this word. |
| `ID` | `001` | Identity within the part. Three digits, zero-padded, `000`–`999`. |
| `SubType` | `Straight` | Edge geometry. **Only `Straight` is in scope** (§7). |
| `Thickness` | `40` | Panel thickness at that edge. Letters and digits, **no unit implied**. |
| `Length` | `2000` | Nominal edge length, on the same terms as `Thickness`. |
| `.<Tail>` | `.Pent` | Optional authoring scratch. Never identity, never compatibility. |

Both ordering decisions exist so the grammar survives new subtypes. **`SOCKET_Edge_<ID>` is a
fixed-arity head, everything after it variable-arity**, so a subtype needing more parameters — a
curve wants a radius *and* an arc length — extends the tail without disturbing how identity is
parsed; `ID` alone is that identity, and it is what persistence stores (§2.9) and what doubles as
network identity (§2.10). **`Thickness` precedes `Length` because only `Length` belongs to the
subtype**, which keeps `Thickness` at a fixed position in every subtype. The cost is legibility:
nothing but position tells the two tokens apart.

#### Sockets PolySnap does not own

A mesh may carry sockets belonging to other systems, and the `Edge` tag separates them — which is
why it stays in the name though it never varies:

> A socket is PolySnap's **only if its name begins `Edge_`**. If it does and the rest fails to
> parse, that is a loud error — `SOCKET_Edge_01_Straight_40_2000` is a typo, not a foreign socket.
> If it does not, the socket is passed over in silence.

**Equipment mounting is therefore out of scope**, owned by whichever plugin handles equipment
under its own tag (§7), so both read the same mesh while ignoring each other's sockets.

#### Compatibility

**Two sockets may connect only when `SubType`, `Thickness` and `Length` all match.** `ID` never
participates — it is identity, not classification.

- **`Thickness` is in the key** because equal edge *length* alone does not make a flush seam: a
  thin partition and a thick hull panel of the same edge length meet at a step nothing geometric
  notices.
- **The key does not cover the edge `profile`** — a square edge and a chamfered one are declared
  compatible and mate to a seam with a groove down it. Closing that needs a vocabulary of profiles
  rather than a number, and is deliberately not attempted; §2.4's clamp is the other place the
  same gap shows.
- **A placed part is never scaled** — actor and component scale are 1, and PolySnap should
  `ensure` on anything else. A panel at 1.5× is half again as long and thick while its tokens
  still read `2000` and `40`, and nothing can catch that: the scale lives on the instance, the
  tokens describe the asset.

#### The size fields are tokens, not measurements

**Nothing in the snap math reads `Thickness` or `Length`.** Geometry comes entirely from the
socket transforms baked into the mesh; the size fields only answer "may these two edges mate?" by
equality. So **neither carries a unit and neither is ever read as a number**, and all of these are
legal — a project picks one and holds it:

```
Edge_001_Straight_40_2000      Edge_001_Straight_40mm_2000mm      Edge_001_Straight_Hull_Long
```

A token must be **one or more letters or digits**: a `.` or `_` inside a size field means a name
that did not come through CONVENTIONS §3's pipeline. Two consequences:

- **Spelling is significant.** `40mm`, `40` and `040` mate with none of each other, and nothing
  normalises a mixed vocabulary — hence one vocabulary per project. Case is the exception, the
  tokens being `FName`s.
- **Nothing checks a token against the mesh.** A socket transform has no cross-section to compare
  `Thickness` against, and a measured edge is in Unreal units while `Length` is in nothing.
  Export-scale errors are caught once by CONVENTIONS §7's calibration check, not per asset.

Derived lengths are frequently irrational, and two parts both labelled `3464` (for 2000√3 ≈
3464.1) are compatible whether their edges match to a nanometre or differ by half a millimetre.
Hence:

> **Derived lengths are constructed from the geometry they derive from, never typed.** Snap the
> brace to the hexagon's actual vertices. Round only for the name.

#### The authoring tail

**Everything past the five-field head is ignored** — not identity, not compatibility, never read
at runtime. It exists because **Blender object names are unique per `.blend` file, not per
object**, so a hex and a pent in one file cannot both own `SOCKET_Edge_001_Straight_40_2000`;
without the tail, either every panel needs its own file or socket IDs must be allocated globally
across the part set.

**The importer does not preserve the `.`**, so on an imported asset the tail is simply a sixth
underscore-separated field, which the fixed-arity head makes unambiguous. The parser splits on
that and never looks for the reserved character; a name still carrying a `.` fails to parse rather
than being accommodated by a code path no asset exercises. The forced cost: a typo like
`Edge_001_Straight_40_2000_Extra` parses with the tail `Extra` rather than failing on field count.

#### Field rules

- **`ID` is permanent and unique within its part.** Save games store it (§2.9), so it is never
  renumbered or reused; retire the ID of a removed socket.
- **`ID` is identity, not an index.** Nothing counts sockets, iterates a range of IDs or reads one
  as an ordinal, so the numbering need not be contiguous and there is no reason to forbid `000`.
  Number from `000` on a new part; a part already numbered from `001` is equally valid and is not
  worth renumbering, since renumbering is exactly what the permanence rule forbids.
- **A part's edge lengths are fixed for the life of the asset.** Resizing a panel is a **new
  asset**, not an edit — so socket names never change, and nothing can be broken by a rename that
  cannot happen.
- **`ID` is zero-padded to three digits** so Blender's outliner and Unreal's socket manager, both
  alphabetical, sort `002` before `010`. The parser has no opinion on how the size fields are
  spelled, but *you* must.
- **No underscores inside a field**, and **`.` is reserved** for the tail — a rule about the
  `.blend`, since import rewrites it.
- **Prefer a meaningful tail** (`.Pent`, `.HatchRim`) over Blender's `.001`, which sits next to
  the three-digit `ID` and invites misreading; the validator warns about the numeric kind.
- **Write the prefix as `SOCKET_`**, and export the empty as an FBX null — the importer accepts
  only `eNull` and skeleton attributes, which is what a parented Blender empty produces.

#### Validation and parsing

Malformed names must fail **loudly at import**, not mysteriously at runtime: PolySnap's editor
module reports malformed names, duplicate IDs, unknown subtypes and unparseable sizes per asset,
and passes over sockets not named `Edge_*` without comment.

**Strip the authoring tail before checking ID uniqueness.** Blender's `.001` duplicate suffix is a
tail like any other; the duplicated `ID` that usually accompanies it is the real error and is
caught independently, so the duplicate still fails while a hex and a pent sharing a file pass.

Deliberately *not* checked: near-miss size tokens across the part set (`Straight_3464` beside
`Straight_3465`), which needs a project-wide scan to catch what a failed snap reports in two
seconds; and either size token against the mesh, for the reasons above.

Names are parsed **once** into a struct and cached — never re-parsed per frame, never
string-compared during a snap query.

### 2.3 Socket orientation — the load-bearing convention

Each edge socket defines an orthonormal basis: **Outward** (in the panel plane, perpendicular to
the edge, away from the part's centre), **Tangent** (along the edge), **Normal** (the surface
normal — which face is arbitrary and does **not** mean "outside of the structure", §2.6).

Which local axis carries which role — `Outward` **+X**, `Tangent` **−Y**, `Normal` **+Z** — is the
default set out in [CONVENTIONS.md](CONVENTIONS.md) §2; another pipeline declares its own mapping.
Note the sign: `Tangent` is **−Y**, so every expression below means that derived vector, never
`GetUnitAxis(EAxis::Y)`.

Mating means positions coincide and tangents are collinear, `Tangent_B == ±Tangent_A`. That leaves
**exactly one continuous degree of freedom**: rotation about the shared edge. That scalar is the
**dihedral angle**, measured in the **anchor socket's own basis** (§2.5) — not "through the
structure's interior", which does not exist yet and may never (§2.6, §2.8):

> **θ is the signed rotation from `Outward_A` to `Outward_B` about `Tangent_A`, over [0°, 360°).**
>
> ```
> θ = atan2( (Outward_A ^ Outward_B) · Tangent_A,  Outward_A · Outward_B )   wrapped to [0, 360)
> ```

180° is coplanar; θ → 0 and θ → 360 are both fully closed, reached from opposite sides, so the two
halves of the circle are the two folding senses. The range is signed because an unsigned angle
spans only 0–180° and reports θ and 360−θ identically, collapsing concave corners onto convex
ones. The sign is deterministic but arbitrary — it is not "toward the inside", and it is not
symmetric between the panels, a flipped panel measured from B's basis returning 360−θ. Naming the
anchor as the reference is what makes θ single-valued at all.

#### Flipping — the two discrete solutions

The `±` is a genuine choice, not slack: `Tangent_B == −Tangent_A` puts the normals on the same
side, while `+Tangent_A` reverses part B's normal. The second case is **not** a mirror but a
proper 180° rotation about the socket's Outward axis, leaving chirality untouched — a motion the
player can perform by turning the panel over, so it is permitted by default.

- **Mating is two-valued.** Each pair yields two candidate transform families, each with its own
  dihedral DOF. The snapper takes the admissible solution requiring the **least rotation from the
  part's current orientation**, so no explicit flip control is needed.
- **Flipping is always permitted** — no socket declares a preferred face and no part has an
  authored inside (§2.6). A decision, not an unimplemented check.
- **Flipping moves whatever is mounted on the panel** from interior to exterior: an intended
  gameplay consequence, owned by the player.
- **Surface orientation is not globally guaranteed**, so nothing may treat panel normals as
  agreeing on which side is inside. Inside/outside comes from §2.8.

That free DOF is not a problem to be constrained away — it is the mechanism (§2.4).

### 2.4 Angles are emergent, and joints host many edges

#### Nothing authors an angle

**Sockets do not declare permitted dihedral angles** — no angle vocabulary, no per-socket range,
no authored list of shapes the system knows how to build.

One connection leaves the dihedral free: the part is a **hinge**. The **next** connection removes
that freedom, *selecting* an angle out of the hinge family — the one bringing the second socket
pair closest together. It selects; it does not **imply**: a second mate is five constraints on a
one-parameter family, so it is over-determined and generically has no exact solution, which is the
normal case and which §2.5 handles. This is why the hex-hex angle of a truncated icosahedron is
~138.19° — not because anyone wrote it down, but because that is what hexagons and pentagons
closing a vertex imply.

- **Any shape the panel set admits is buildable** — spheres, domes, tubes, irregular hulls.
- **New panel geometry needs no new metadata**: cut a panel and its angles follow from its
  outline.
- **Rigidity is emergent.** One connection is a hinge, two or more are progressively constrained,
  and welding freezes whatever the structure settled into.

The only angle limit worth keeping is **mechanical** — an edge profile that would self-intersect
if folded too far, which is a fact about the mesh and never a way to steer the player toward
intended shapes. **Collision cannot express it**: simple collision is convex, and the convex hull
of a chamfered edge is the unchamfered edge. Measure the profile once at import and store a
derived per-socket clamp on `|180 − θ|`; panels are symmetric about their mid-plane (CONVENTIONS
§3), so the sign of θ never enters it.

#### Joints: an edge line can host more than two panels

Several panels may meet along **one shared edge line** — a bulkhead meeting a hull seam, a
partition dividing a cylinder, a spine with panels fanning off it. So:

> A **Joint** is a shared edge line in space. Sockets attach *to a joint*, not directly to each
> other. A two-socket joint is an ordinary seam; an N-socket joint is a T-junction or fan.

With N sockets there are N−1 independent dihedral angles, free until further connections constrain
them. **No angle is stored** — every one is recomputable from the socket transforms persistence
saves anyway (§2.9), so storing them would be redundant state with its own way of going stale.
Where one is needed — ordering a degree-3 joint's participants for enclosure (§2.8) — derive it
then, in the anchor connection's basis.

So **socket occupancy is a capacity question, not a boolean**: not "is this socket taken?" but
"does this joint accept another participant?", and by default it does. This covers panels sharing
a *whole* edge line; a long edge spanned by shorter ones end-to-end is deliberately unsolved (§7).

#### Subassemblies join to each other, not just parts to assemblies

**A loose part is a subassembly of one**, so joining a part to an assembly and joining assembly
X to Y are the same graph operation, and §2.5's anchor-and-adopt rule was built for several socket
pairs mating at once. What differs: placement applies the anchor's one solved transform to every
part in the held subassembly; residuals are larger, so the adoption tolerance may have to scale
with how much is closing at once (§7); and weld groups do not merge. Rim sockets on both halves
are free, so the common case creates fresh joints and needs no joint merging; the exception is
left open (§7).

#### Closure is the ordinary case, not the last step

**Every connection after a part's first one is a closure** — the last panel of a buckyball is the
same operation as the third panel at a vertex, differing only in accumulated geometry. So: when a
socket pair lands within tolerance, **connect, and let the physics constraints absorb the residual
strain.** That is nearly free, the constraint solver being an iterative relaxation solver already,
and it avoids yanking placed parts out from under the player. Welding then freezes it (§2.7,
§2.10).

It holds **only if the anchored connection is exact when it is made** (§2.5). Visible drift when
closing a buckyball is a bug in the snap transform or the panel outlines, not a tolerance to widen
— Milestone 2 measures it rather than eyeballing it (§5).

### 2.5 Compatibility and snapping

A candidate connection is evaluated as:

1. **Descriptor match** — `SubType`, `Thickness` and `Length` all equal (§2.2). `FName` equality
   is an interned-index compare, so reject on this first.
2. **Proximity** — socket positions within a tolerance.
3. **Orientation** — tangents collinear within a tolerance, in either polarity.
4. **Joint capacity** — the joint accepts another participant (§2.4). By default it passes.

Note what is *absent*: no dihedral check. The angle is whatever the placement produces and stays
free until further connections constrain it. Both polarities are always admissible (§2.3), so
every passing pair yields two candidate families rather than one solution.

#### One anchor, many connections

A part has **one transform** but may gain **several connections** from a single placement;
conflating the two is what makes §2.4's over-determination look like a contradiction. So:

> **Exactly one socket pair is the anchor. The part's transform is solved from the anchor and
> from nothing else. Every other socket pair that lands within tolerance after that solve is
> adopted into its joint, and records the gap it was adopted at.**

The anchor is the pair the player is driving — the passing candidate nearest by §2.3's
least-rotation reading. Its mating conditions hold to float precision; adopted connections hold to
within whatever the panel geometry implies, float noise if the outlines are cut correctly. **That
exactness describes the moment of placement, not a standing invariant**: in the loose regime the
solver perturbs everything on the next tick, the anchor included (§2.10). It exists so error is
diagnosable at the instant it is introduced, which is when the residual is recorded.

The rule buys **no least-squares placement**, since a solver spreading error across every pair
leaves *no* connection exact and exactness on one is what separates a geometry bug from an
accumulation artefact; **determinism**, the transform being a pure function of anchor pair,
polarity and dihedral, so a server can re-derive it (§2.10); and **a number to measure**, the
adoption gap being Milestone 2's residual.

#### Solving the transform

The anchor pins five of six degrees of freedom, leaving θ. θ and polarity are chosen together:

1. **Adopt-driven.** As the part hinges, each other socket sweeps a **circle** about the anchor
   axis. For a candidate target the θ bringing the two closest is closed form: decompose the
   target's offset from the anchor into components along and across the axis, then rotate the held
   socket's across-axis component onto the target's. One `atan2`, no iteration. The along-axis
   component does not rotate — **that leftover is the residual.** Search both polarities and every
   secondary pair; every θ whose residual is inside the adoption tolerance is admissible, and
   **the one requiring least rotation from the part's current pose wins**, the residual breaking
   ties rather than deciding.
2. **Player-driven.** With no secondary candidate in tolerance, both come from the nearest point
   on the constraint manifold to the part's current orientation (§2.3).

**Least rotation rather than least residual, and the difference is not cosmetic.** Folding a panel
flat onto a panel already placed lands *every* one of its sockets on that panel's sockets, at a
residual of exactly zero — so ranking closures by residual makes that degenerate answer beat the
intended one whenever both are available, and an assembly built that way stacks panels rather than
closing them. It is not a rare case. It cost the buckyball its twelfth face, and nothing
placed after that had anything to snap to. Since any closure inside the tolerance is geometrically acceptable, the
question left is which one the builder was aiming at, and rotation is what answers it. What this
does *not* cover is a coincident placement that is the only closure on offer (§7).

**Adoption takes unconnected target sockets only.** A joint may host more than two panels (§2.4),
but joining a new part into an *occupied* joint needs the first-class joint of Milestone 3 — until
then a seam that stays visibly open is a better failure than one that silently became a threesome.
The anchor is not restricted this way; it is adoption, which nobody is aiming, that must not
surprise.

A secondary socket lying *on* the anchor axis sweeps a degenerate circle and has no θ to offer;
detect the near-zero radius and skip it.

#### The adoption tolerance

**2 cm, and it is not a second snap distance.** `SnapDistanceUu` is how near the player must aim;
the adoption tolerance is how much geometric error a seam may carry and still count as closed, so
it is far tighter. Panels cut correctly close to float noise: the buckyball's ninety edges came in
at a worst of 0.06 mm and a mean of 0.006 mm, three hundred times inside the tolerance. Anything
approaching a millimetre is a panel problem to look at, not a tolerance to widen (§2.4).

**Adopt or drop, per secondary — a failing secondary never rejects the anchor.** The player gets
the placement they asked for plus an unclosed seam they can see and nudge; refusing the whole
placement over a fraction of a millimetre makes the builder feel broken for no gain.

### 2.6 No authored inside or outside

**Parts do not have an authored interior face, and sockets never constrain which way round a
part is fitted.** Inside/outside is not a property of a *joint*, so encoding it in the socket
constraint system puts the check in the wrong layer: whether a placement is *sensible* depends on
the environment the part ends up in, a runtime question the enclosure system (§2.8) answers. So
hatches work both ways round, racks do not care unless what they store needs an atmosphere, and
equipment may or may not function in vacuum — gameplay discovered by the player rather than a rule
forbidding the build.

> **PolySnap decides whether parts can physically connect. It never decides whether the result is
> a good idea.**

### 2.7 Physics and welding

- **Loose** — connected parts stay individual rigid bodies joined by physics constraints. The
  structure flexes and can be pushed around, which is what makes microgravity assembly feel
  physical rather than administrative.
- **Welded** — welding merges parts into a **single rigid body** with one collision
  representation and removes the constraints between them. Both the fiction of a sealed structural
  join and the performance strategy, long constraint chains being what would otherwise degrade as
  structures grow.

**Critical invariant: merging bodies must not merge *identities*.** After welding the graph still
knows every part, its sockets and its connections — save/load, airtightness and disassembly all
depend on it.

Welding also settles geometry: while loose, where a part sits is the solver's working state, live
and strained, and the weld snapshots it into durable data (§2.10). Unwelding should be assumed to
be a requirement, so implement the merge reversibly.

### 2.8 The assembly graph

The graph is **bipartite**: parts and joints are both nodes, and a connection links a part's
socket to a joint — which is what lets a joint host three or more panels (§2.4) without a special
case. Game systems then get answers by walking the graph rather than by geometric queries:

- **Open edges** — sockets in no joint. Drives build UI, ghost previews, "what can I attach here".
  A socket already in a joint may still accept more panels, so open and available differ.
- **Enclosure** — which sets of parts bound a closed volume, and so are candidates to be treated
  as airtight compartments.
- **Structural queries** — connectivity, reachability, what breaks off if this part is cut.
- **Environment lookup** — which enclosed volume a given **world position** sits in. Taking a
  position rather than a socket keeps PolySnap ignorant of what is asking: equipment, a pawn, a
  dropped item all use the same query (§2.6).

The boundary: PolySnap reports **topology** — what is connected, what is enclosed — and does not
model pressure, gas or temperature. The graph is derived from connections and maintained
incrementally as parts are added and removed, never rebuilt by scanning the world.

#### Enclosure is a graph walk

**Peel.** Repeatedly drop any part with an edge socket in no joint, until nothing changes; what
survives is boundary-free and is the enclosure candidate. It is a peel rather than a single test
because a free edge somewhere in a component must not disqualify all of it — weld a fin to a
finished buckyball and the ball is still sealed.

**Propagate sides.** Every part has a +Z and a −Z face (CONVENTIONS §3), and §2.3's tangent
polarity already says how a joint pairs them: `−Tangent_A` puts B's +Z on the same side as A's,
`+Tangent_A` pairs it with A's −Z. Pick a surviving part, call its +Z "side one", and walk,
flipping at every flipped connection. Consistent all the way round means the surface is orientable
and the two sides are a global inside and outside; a contradiction means it bounds nothing. This
is what §2.6's "no authored inside" costs, and it costs nothing: only **relative** facing is ever
needed, and the snap already decided that.

**A hatch is an ordinary part** — peeled or kept by the same rule, and whether it is open changes
nothing, a closed surface being a claim about topology rather than pressure. The atmosphere system
unions the compartments an open hatch connects.

Topology runs out at a joint of degree three or more: a partition across a cylinder shares every
edge, so the peel keeps everything and the walk reports one region, yet a bulkhead exists
precisely to make two compartments. Deciding which pair of the three panels bounds which needs
their cyclic order about the axis — an angle, derived on demand (§2.4), and the only part of
enclosure that is not connectivity (§7).

### 2.9 Persistence

Save/load must reconstruct an assembly exactly: every part's class, transform and identity, every
joint and its participants, every weld group. That requires **stable numeric IDs** — one per part
instance, one per joint instance, and the socket's own `ID` field (§2.2). A connection is
therefore `(JointID, PartID, SocketID)`: one record per participating socket, so a joint of
degree three saves as three records rather than needing a special case.

Each connection also records its **adoption residual** (§2.5) and whether it was the anchor.
Neither is needed to rebuild the assembly, since transforms are saved directly; both are a few
bytes and make a drifting save diagnosable long afterwards.

Socket IDs must survive mesh re-exports or saves break when art is updated — cheap to guarantee,
because a part's edge lengths never change (§2.2), so every socket name a save refers to is still
there.

### 2.10 Replication-readiness

Multiplayer is **not** being implemented now, but PolySnap should be shaped so co-op does not
require a rewrite:

- Snapping is a **validated operation on the graph**, not a client writing a transform. The
  payload is §2.5's placement inputs — anchor pair, polarity, and the secondary pair that selected
  the dihedral — so the server re-derives the transform. An adopt-driven placement is therefore
  **integers only**; only the player-driven free-hinge case sends a quantised θ.
- The numeric IDs from §2.9 double as network identity.
- **The graph is the authority on topology, always.** Physics never creates or destroys a
  connection.
- **Authority over geometry follows the regime** (§2.7). While loose the solver owns it, and
  clients may visibly disagree by up to the constraint tolerance — cosmetic precisely because it
  is transient and nothing durable reads it.
- **Welding is therefore server-authoritative**, promoting solver state to permanent data: one
  authority snapshots and replicates the transforms. Letting each client freeze its own settled
  geometry would leave them disagreeing for good, the solver being iterative and not bit-identical
  across machines.

### 2.11 A part is a component, not a base class

**PolySnap ships no actor class.** A part is any actor carrying a `UPolySnapConnectorComponent`, and
that component is the whole of what makes it one: it parses the mesh's sockets, holds the
connection records, registers with the subsystem, applies the damping and sleep of §2.7, and owns
the anchored rule. Authoring a part is adding a component, never changing a parent class.

**Why.** The plugin's destination is other projects, whose actors already have a base class of
their own — a pooled actor, a `Pawn`, something with a project-wide interface on it. A required
base class makes PolySnap compete with that hierarchy, and single inheritance means one of them
loses. A component composes with anything, which is the point of the pattern.

**Why not keep one as a convenience.** There was a `APolySnapPiece` doing exactly that, and its
own header conceded nothing about it was required. A convenience base class that also carries
behaviour becomes the documented path and the tested path, and the component path rots quietly
until someone with their own actor discovers it never worked. Its one real behaviour was
anchoring, which now lives on the component where a Blueprint actor can reach it — strictly more
capability than before, since a Blueprint part was never a subclass in the first place.

**What PolySnap therefore does not own.** It does not create the mesh, choose the actor's root, or
decide whether a body simulates. Collision and physics setup belong to whoever authored the actor,
and that is not a gap to close later: a project that pools its parts, drives them from a
`GeometryCollection`, or keeps them kinematic under its own controller must be able to, and a
plugin that quietly forced `SimulatePhysics` at BeginPlay would fight it. The one exception is an
**anchored** part, which PolySnap holds kinematic whatever the actor asked for, because it is the
fixed reference the rest of the structure is built against and the builder refuses to pick it up.

**Consequence for §2.9.** "Every part's class" in a save record is the *project's* actor class,
not a PolySnap one. Loading spawns that class and expects the component to come with it.

---

## 3. Player movement (test harness only)

A Newtonian free-flying pawn — RCS-style translation and rotation, no gravity — is needed to
exercise the system: approach a rack, grab a part, manoeuvre it, snap it. This is **test
scaffolding, not a product**, and lives in the `Sandbox` game module, never in PolySnap.

## 4. Content

Construction parts — hex and pent panels, struts, hatches — live in this project's `Content/`,
**not** inside the plugin. PolySnap supplies the types, rules and runtime behaviour; the panels
are game content that happens to use it.

## 5. Milestones

The list is in [README.md](README.md#milestones). One note belongs here, on Milestone 2's residual
measurement: **run it with the parts kinematic and physics off.** A constraint solver leaves
bodies slightly off their ideal positions, so a residual measured against a simulating assembly
mixes panel-geometry error — what the test is for — with solver strain, which is expected and
unrelated. A flawless buckyball would report a nonzero number, and a real 0.2 mm error could hide
beneath the noise.

**What Milestone 2 measured.** Thirty-two panels, ninety connections, no open socket: the shell
seals. Thirty-one of the ninety edges are anchors and fifty-nine are adopted, so two thirds of the
structure is held together by connections nobody aimed at. Worst adoption residual **0.06 mm**,
mean 0.006 mm, against 2000 mm edges. Simulated, the seams settle at 0.06 mm as well, so the
constraint solver is holding no strain worth the name.

Two things the run taught that the plan did not anticipate. The first is §2.5's ranking rule; the
assembly failed on it before it was found. The second is about where a shell is built. The first
simulated attempt tore itself apart at metres per second, and neither the constraints nor the
panels were at fault: the shell was built at the world origin, inside the level's own floor.
Thirty-two rigid bodies that begin simulating already interpenetrating something get flung apart
by depenetration, and the symptom looks exactly like a constraint network that cannot hold.

The test rig lives in `PolySnapTests` and PolySnap itself knows nothing of buckyballs — a shell is
assembled out of the parts to hand, never a shape the plugin can make. The rig supplies only what
a player's hands would: which part, in what order, roughly where. Every transform in the finished
shell comes from the same query and the same commit the builder component calls.

## 6. Plugin boundaries

Stated in [README.md](README.md#plugin-boundaries) and in [CLAUDE.md](CLAUDE.md), whose version
governs.

---

## 7. Open questions

Marked explicitly so nobody builds on them as though they were settled.

- **Where equipment mounting lives.** Out of PolySnap (§2.2), but which plugin owns it, and
  whether it reuses this grammar under its own tag, is undecided.
- **Spawning parts.** A rack that hands the player a fresh panel needs a class to spawn, and since
  §2.11 removed PolySnap's actor class there is nothing left to constrain that against. Whether it
  becomes a `TSubclassOf<AActor>` validated at spawn for the component, a data asset listing part
  types, or stays entirely the game's problem is undecided.
- **Curved edge subtypes.** Only `Straight` is in scope. Curves need a multi-parameter tail
  (radius and arc length), which the grammar accommodates but nothing has designed for.
- **Collinear subdivision.** A long edge spanned by shorter ones — a 2 m panel meeting two 1 m
  panels — is *not* addressed by joints (§2.4), which cover panels sharing a whole edge line. It
  would likely add a *span* field, and unlike the size tokens a span is arithmetic, so it would be
  the first field needing a declared unit.
- **Joint capacity limits.** Whether a joint should ever refuse a participant, and on what
  grounds.
- **Coincident joints.** A joint on each of two subassemblies landing on the same edge line needs
  two existing joints to become one, with an identity to pick between them and a save format that
  survives it. Rare enough to defer, common enough that refusing it silently would be a bug.
- **Magnets.** Current recommendation: no separate magnet plugin. Assisted alignment is better
  understood as an attraction behaviour of the existing socket search, which needs no new concept.
- **Weld merge mechanics.** Which merge strategy preserves reversibility at acceptable cost.
- **Enclosure at T-junctions.** The algorithm is otherwise settled (§2.8); splitting a degree-3
  joint into separate compartments by angular order is not.
- **Environment queries.** "Am I in an enclosed volume" is a graph query; "is that volume
  pressurised" belongs to a future atmosphere system, and the interface between them is
  undesigned.
- **Tolerances.** Snap distance and angular thresholds; whether they are global, per socket type,
  or scaled by part size. The **adoption tolerance** (§2.5) is a third and behaves differently:
  too tight and a correctly built vertex refuses its second connection, too loose and visibly
  misaligned panels are welded into the graph as though they had closed. Its value is now 2 cm and Milestone
  2 says nothing against it. A shell that closes three hundred times inside the tolerance
  exercises neither edge of the range, so what counts as *too* loose is still unknown.
- **Coincident placements.** A closure that folds a part flat onto a part already placed is
  degenerate, and §2.5's least-rotation ranking only stops it winning when a sensible closure is
  also available. When it is the only closure on offer it is still taken. Whether a placement
  should be refused for landing a part where a part already is — and how "already is" is decided
  without a collision query the snapper has no business making — is undesigned.
- **Anchor selection.** The anchor is exact and every adopted connection carries the residual, so
  which pair anchors decides where the error lands. Whether the player-driven pair is always the
  right anchor is unexplored.
- **Framework choices.** Enhanced Input, GAS, StateTree and similar are undecided and chosen per
  feature rather than adopted wholesale.
