# Construction

A game about building pressurised structures in microgravity, in orbit — and, more
immediately, a testbed for the reusable plugin modules that game will be built from.

Unreal Engine 5.8, Linux. This document describes *what* we are building and *why*.
[CLAUDE.md](CLAUDE.md) describes *how* to work on it (build commands, code style, plugin
isolation rules), and [CONVENTIONS.md](CONVENTIONS.md) pins down the mechanical conventions
shared between Blender and Unreal — axes, units, and exact import/export settings.

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
remainder of the name is ours to encode metadata in, rather than a parallel data asset that
could drift out of sync with the mesh.

**PolySnap works on edge sockets and nothing else.** An edge socket sits at the midpoint of an
edge of a piece and joins pieces structurally; that is the whole of the plugin's subject
matter. Mounting points for equipment, wiring and pipes are a real requirement of the game but
not of this plugin — see "Sockets PolySnap does not own" below.

#### The naming grammar

```
SOCKET_Edge_<ID>_<SubType>_<Size>[.<AuthoringTail>]

SOCKET_Edge_001_Straight_2000
SOCKET_Edge_001_Straight_2000.Pent
```

After import the engine strips the `SOCKET_` prefix, so the socket is named
`Edge_001_Straight_2000`.

| Field | Example | Meaning |
| --- | --- | --- |
| `SOCKET_` | `SOCKET_` | Required prefix. How Unreal recognises the empty as a socket. |
| `Edge` | `Edge` | Required namespace tag. Marks the socket as PolySnap's — see below. Not a variable field; it is always this word. |
| `ID` | `001` | Identity within the piece. Three digits, zero-padded, `001`–`999`. |
| `SubType` | `Straight` | Edge geometry. **Only `Straight` is in scope** — curves and circles are a later extension. |
| `Size` | `2000` | Nominal edge length in **millimetres**. Bare integer, no zero-padding, no fixed width. A compatibility token, not a measurement — see below. |
| `.<Tail>` | `.Pent` | Optional. Everything from the first `.` onward is authoring scratch: ignored by the parser, never identity and never compatibility — see below. |

**`SOCKET_Edge_<ID>` is a fixed-arity head; everything after it is of variable arity.** This is
why the ID sits third rather than last: when curved edges arrive they will need more than one
parameter (a radius *and* an arc length), and they can extend the tail without disturbing how
every socket's identity is parsed. The `ID` alone is that identity — it is what persistence
stores (§2.9) and what doubles as network identity (§2.10).

#### Sockets PolySnap does not own

A static mesh may carry sockets belonging to other systems entirely — an effect anchor, a
mounting point for equipment, something a future plugin invents. PolySnap has no business
erroring on those, and the `Edge` tag is how it tells them apart:

> A socket is PolySnap's **only if its name begins `Edge_`** (`SOCKET_Edge_` before import). If
> it does and the rest fails to parse, that is a loud error — `SOCKET_Edge_01_Straight_2000` is
> a typo, not a foreign socket. If it does not, the socket is none of PolySnap's business and
> is passed over in silence.

This is why the tag stays in the name even though it never varies. `SOCKET_001_Straight_2000`
would work equally well as a format and would give the plugin no way to know whether that
socket was meant for it.

**Equipment mounting is out of scope**, then — not deferred within PolySnap, but owned by
whichever plugin ends up responsible for equipment. That plugin brings its own tag and its own
tail, and the rule above lets both plugins read the same mesh while ignoring each other's
sockets completely. The grammar here is a reasonable thing for it to copy; the point is that it
copies rather than shares.

**Compatibility:** two sockets may connect only when `SubType` and `Size` both match.
`Straight_2000` mates only with `Straight_2000`. The `ID` never participates — it is identity,
not classification.

That tuple is sufficient **on the assumption that all pieces share a common thickness and edge
profile**, so that any two edges of equal length meet flush. Nothing in the geometry records
this and nothing checks it. A thin partition panel authored against a thick hull panel would be
declared compatible and would mate to a stepped seam that cannot be airtight; if that piece is
ever wanted, the compatibility key needs a profile term and this paragraph is the warning that
it was left out deliberately.

**A placed piece is never scaled.** Its actor and component scale are 1, and PolySnap should
`ensure` on anything else. A panel scaled 1.5× in the level has 3000 mm edges while its
descriptors still read `2000`, so it snaps to things it cannot meet — a lie the validator
cannot catch, because the validator inspects the asset and the scale lives on the instance.

#### `Size` is a token, not a measurement

**Nothing in the snap math reads `Size`.** Geometry comes entirely from the socket transforms
baked into the mesh, which are exact floats; `Size` only answers "may these two edges mate?"
by equality. It is a category label that happens to be legible.

Two properties pick the format:

- **Integers**, so equality is exact and no float-formatting ambiguity (`346.4` against
  `346.40`) can reach the token.
- **Millimetres**, because that is the coarsest unit at which integers still separate any two
  lengths anyone would plausibly design as *distinct*. At centimetres, `346` swallows everything
  from 3455 mm to 3465 mm, and two edges never meant to mate are declared compatible.

**`Size` is not in Unreal units.** UE units are centimetres; this field is millimetres. Never
use it as a length in code.

Rounding is therefore free — but only because the true lengths already agree. Edge lengths are
frequently irrational (a regular hexagon of side 2000 has a short diagonal of
2000√3 ≈ 3464.1 mm), and two pieces both labelled `3464` are compatible whether their real edges
match to a nanometre or differ by half a millimetre. The token cannot tell. Hence:

> **Derived lengths are constructed from the geometry they derive from, never typed.** Snap the
> brace to the hexagon's actual vertices; do not read "3464.1" off a document and type it.
> Round only for the name.

Typing is what turns a rounded label into a real mismatch, and it is the only way this field
can hurt you. Authoring a panel family in one `.blend` (CONVENTIONS §3) is what makes
constructing the cheaper option.

An abstract size vocabulary (`Straight_S`, `Straight_M`) would avoid the rounding question but
is opaque at a glance, and reintroduces a centrally maintained vocabulary of the kind removed
when angles became emergent (§2.4).

**Why `SubType` and `Size` stay separate fields** rather than a fused `Straight2000`: splitting
a fused token requires scanning for the alpha/digit boundary, which breaks as soon as a subtype
name contains a digit; and a subtype with two parameters — as curves will have — cannot be
expressed as "name plus one number" at all. The compatibility key is the tuple either way.

#### The authoring tail

**Everything from the first `.` to the end of the name is ignored.** It is not identity, not
compatibility, and nothing at runtime reads it. `Edge_001_Straight_2000.Pent` and
`Edge_001_Straight_2000` are the same socket as far as PolySnap is concerned.

It exists because **Blender object names are unique per `.blend` file, not per object**, and
collections do not namespace them. A hex and a pent authored in the same file therefore cannot
both own `SOCKET_Edge_001_Straight_2000`; Blender appends `.001` to whichever came second.
Without a rule for the tail, either every panel needs its own file, or socket IDs have to be
allocated globally across the whole piece set — a centrally maintained vocabulary of exactly
the kind this grammar exists to avoid.

Reserving `.` costs nothing. Every field is alphanumeric, so the character is unused, and
Blender has already claimed it by convention. Prefer a meaningful tail (`.Pent`, `.HatchRim`)
over letting Blender assign `.001`: a numeric tail sits right next to the three-digit `ID` and
invites the misreading described below, which is why the validator warns about it.

Because the tail is not identity, renaming `.Pent` to `.Pentagon` is free and does not touch
save games (§2.9).

#### Field rules

- **`ID` is permanent, and unique within its piece.** It is what save games store (§2.9), so it
  must never be renumbered or reused. If a socket is removed, retire its ID rather than
  reassigning it.
- **A piece's edge lengths are fixed for the life of the asset.** Resizing an existing panel is
  not an edit, it is a **new asset**. So socket names never change once authored, and nothing —
  a save game, a Blueprint reference, an effect attachment — can be broken by a rename that
  cannot happen.
- **`ID` is zero-padded to three digits; `Size` is not padded at all.** `Straight_500`, never
  `Straight_0500` — one size, one spelling. The two fields differ on purpose: `ID` is padded so
  that Blender's outliner and Unreal's socket manager, both of which sort alphabetically, put
  `002` before `010`. IDs run past nine on any panel edited a few times, since retired ones are
  never reused. `Size` has no such ordering to protect and a fixed width would only invite a
  four-digit assumption that `Straight_500` and `Straight_12000` both break.
- **No underscores inside a field.** The underscore is the delimiter.
- **`.` is reserved.** It opens the authoring tail and may not appear inside any field.
- **Case:** write the prefix as `SOCKET_`. The importer's comparison is case-insensitive, so
  `Socket_` also works, but there is no reason to depend on that.

#### Authoring traps

- **Blender's `.001` duplicate suffix.** Duplicating an empty produces
  `SOCKET_Edge_003_Straight_2000.001` — note the trailing `.001` is Blender's suffix and has
  nothing to do with the three-digit `ID` field, which is easy to misread. It is an authoring
  tail like any other and is **stripped**, not rejected.

  An earlier version of this document had the validator reject the suffix outright, arguing
  that a duplicated empty carries a duplicated `ID` and that stripping would hide the error.
  That was wrong. The suffix is a symptom; the duplicated `ID` is the error, and the error is
  independently detectable. Strip the tail **first**, then check that no ID repeats on the
  piece: the duplicated socket still fails, while the legitimate cases — a hex and a pent
  sharing a file, or a whole piece duplicated to make a variant — pass, as they must.
- **The empty must export as an FBX null.** The importer only accepts `eNull` (and skeleton)
  attributes, which is what a Blender empty parented to the mesh produces.

#### Validation and parsing

Malformed names must fail **loudly at import**, not mysteriously at runtime. This is the job
of PolySnap's editor module: parse every socket on a piece and report malformed names,
duplicate IDs, unknown subtypes, and unparseable sizes — all of it per asset, at import.
Sockets not named `Edge_*` are not PolySnap's and are passed over without comment.

One ordering rule has teeth: **strip the authoring tail before checking ID uniqueness.** The
other way round, a hex and a pent sharing a `.blend` fail validation for an error neither of
them has.

Deliberately *not* checked: near-miss size tokens across the piece set (`Straight_3464` beside
`Straight_3465`). It would need a project-wide asset scan to catch something a failed snap
reports in two seconds, and the construct-don't-type rule above is the actual fix.

Names are parsed **once** into a struct and cached — never re-parsed per frame, and never
string-compared during a snap query. Runtime compatibility tests operate on the parsed fields.

### 2.3 Socket orientation — the load-bearing convention

Each edge socket defines an orthonormal basis:

- **Outward** — in the plane of the panel, perpendicular to the edge, pointing away from the
  piece's centre.
- **Tangent** — along the edge.
- **Normal** — the panel's surface normal. Which of the panel's two faces this points to is an
  arbitrary authoring convention; it does **not** mean "outside of the structure" (§2.6).

Which local axis carries which role — in Unreal, `Outward` is socket-local **+X**, `Tangent`
**−Y**, `Normal` **+Z** — is fixed in [CONVENTIONS.md](CONVENTIONS.md), together with the
Blender authoring recipe and the export settings that make the two agree. Note the sign:
`Tangent` is the socket's **−Y**, so every expression below means that derived vector, never
`GetUnitAxis(EAxis::Y)`. The mapping is a convention, not a design decision; this section only
depends on the three directions existing and being orthonormal.

Mating two edge sockets means:

1. Their positions coincide.
2. Their tangents are collinear — `Tangent_B == ±Tangent_A`.

That leaves **exactly one continuous degree of freedom**: rotation about the shared edge axis.
That scalar is the **dihedral angle**, and it is measured in the **anchor socket's own basis**
(§2.5) — not "through the structure's interior," which does not exist yet and may never (§2.6,
§2.8):

> **θ is the signed rotation from `Outward_A` to `Outward_B` about `Tangent_A`, over [0°, 360°).**
>
> ```
> θ = atan2( (Outward_A ^ Outward_B) · Tangent_A,  Outward_A · Outward_B )   wrapped to [0, 360)
> ```

180° is coplanar. θ → 0 and θ → 360 are both the fully-closed configuration, reached from
opposite sides, so the two halves of the circle are the two folding senses. A signed range is
necessary rather than tidy: the unsigned angle between the two Outward vectors spans only
0–180° and so reports θ and 360−θ identically, collapsing concave corners onto convex ones.
Internal bulkheads and recesses are ordinary shapes, and §2.4 promises to build them.

Two things this sign is not:

- **It is not "toward the inside."** It is deterministic but arbitrary — it flips if a panel is
  authored with its normal toward the other face. Reproducible, which is what debug readouts,
  fold limits, and internal maths need; meaningless as a claim about enclosure, which stays
  §2.8's job.
- **It is not symmetric between the two panels.** With `Tangent_B == −Tangent_A`, measuring
  from B's basis returns the same θ; with a flipped panel it returns 360−θ. Naming the anchor
  as the reference is therefore what makes θ single-valued at all, not a convenience.

#### Flipping — the two discrete solutions

The `±` in the tangent condition is a genuine choice, not slack in the constraint. The triad is
right-handed as a physical arrangement, so `Outward × Tangent == Normal` — the sign is inverted
in Unreal's cross product (CONVENTIONS §2) and the argument below holds either way. The two
cases are:

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
  authored "inside" (§2.6) — a deliberate decision, not an unimplemented check.
- **Flipping moves whatever is mounted on the panel** from the hull interior to the exterior,
  and vice versa. PolySnap does not track mounted equipment (§2.2), but the panel it does track
  is what carries it, so this is a real and intended gameplay consequence, owned by the player.
- **Surface orientation is not globally guaranteed.** Panel normals do not agree on which
  side is inside, so nothing may treat them as if they do. Inside/outside is derived from the
  enclosed volume (§2.8).

That free DOF is not a problem to be constrained away — it is the mechanism (§2.4).

### 2.4 Angles are emergent, and joints host many edges

#### Nothing authors an angle

**Sockets do not declare permitted dihedral angles.** There is no angle vocabulary, no
per-socket range, no authored list of shapes the system knows how to build.

A single connection leaves the dihedral free: the piece is a **hinge**, swinging about the
shared edge. What removes that freedom is the **next** connection. When a second edge of the
same piece also mates, that mate *selects* an angle out of the hinge family — the one that
brings the second socket pair closest together. Three panels closing a vertex select their
dihedrals to within the accuracy of the panel outlines themselves.

Note the wording: the second mate **selects** the angle, it does not **imply** it. Mating a
second socket is five constraints — three of position, two of direction — imposed on a
one-parameter family, so it is over-determined and generically has no exact solution. That is
not a flaw to engineer around; it is the normal case, and §2.5 is where placement deals with it.

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
shape. Treat any such clamp as optional and geometric; never as a way of steering the player
toward intended shapes.

**Collision cannot express it**, which is worth stating because it is the obvious guess. Simple
collision on a simulated body is convex, and a convex hull is exactly what erases the concavity
of an edge profile — the hull of a chamfered edge is the unchamfered edge. The clamp is better
found once at import, by measuring the profile, and stored as a derived per-socket value: a
number the mesh already implies, not metadata anyone authors.

Such a clamp needs only `|180 − θ|`, the fold magnitude. Panels are symmetric about their
mid-plane (CONVENTIONS §3), so a profile's limit is symmetric about coplanar and the sign of
§2.3's θ never enters it.

#### Joints: an edge line can host more than two panels

A connection is not restricted to a pair. Several panels may meet along **one shared edge
line** — an internal bulkhead meeting a hull seam, a partition dividing a cylinder, a spine
with panels fanning off it.

So the graph gains a first-class concept:

> A **Joint** is a shared edge line in space. Sockets attach *to a joint*, not directly to each
> other. A two-socket joint is an ordinary seam; an N-socket joint is a T-junction or fan.

With N sockets on a joint there are N−1 independent dihedral angles about the shared axis, all
free until further connections constrain them.

**No angle is stored.** Every one is recomputable from the socket transforms, which persistence
saves anyway (§2.9), so holding them on the joint would be redundant state with its own way of
going stale. Where an angle is genuinely needed — ordering the participants of a degree-3 joint
about its axis, for enclosure (§2.8) — derive it then, in the anchor connection's basis (§2.3).

Practically this means **socket occupancy is a capacity question, not a boolean**. "Is this
socket taken?" becomes "does this joint accept another participant?" — and by default it does.

Note this covers panels sharing a *whole* edge line. A long edge spanned by several shorter
edges end-to-end is a different problem, and is deliberately **not** solved here (§7).

#### Subassemblies join to each other, not just pieces to assemblies

The player can build two halves and bring them together. This costs almost nothing, because
**a loose piece is a subassembly of one**: joining a piece to an assembly and joining assembly
X to assembly Y are the same graph operation, a joint formed between sockets that were in
different components. And §2.5's anchor-and-adopt rule was built for several socket pairs
mating at once, which is exactly what two half-domes meeting along a rim produces.

What actually differs:

- **Placement moves a set of bodies.** The anchor still solves one transform; it is applied to
  every piece in the held subassembly rather than to one.
- **Residuals are larger.** A rim closure adopts dozens of connections, each carrying error
  accumulated across half a structure, where a single-piece placement adopts one or two at
  float noise. The adoption tolerance may have to scale with how much is closing at once (§7).
- **Weld groups do not merge.** Joining two welded subassemblies leaves two rigid bodies
  connected by constraints (§2.7). The player welds again if they want one.

The common case needs no joint merging at all: the rim sockets on both halves are free, so the
operation creates fresh joints and never touches existing ones. Only the case where a joint on
X and a joint on Y land on the same edge line — four panels arriving as two pre-formed pairs —
would need joints themselves to merge, and that is left open (§7).

#### Closure is the ordinary case, not the last step

It is tempting to read the previous section as describing a problem that appears only when a
ring of panels comes back around to meet itself. It is not. **Every connection after a piece's
first one is a closure** — the last panel of a buckyball is the same operation as the third
panel at a vertex, differing only in how much geometry accumulated ahead of it.

So the answer is the same in both places. When a socket pair lands within tolerance,
**connect, and let the physics constraints absorb the residual strain.**

This is nearly free: the constraint solver is already an iterative relaxation solver, so
letting the assembly settle distributes the error by itself — what a bespoke closure solver
would do, without the machinery and without yanking already-placed pieces out from under the
player. Welding then freezes the settled geometry, which is the moment that geometry stops
being the solver's business and becomes durable data (§2.7, §2.10).

This holds **only if the anchored connection is exact when it is made** (§2.5). Visible drift
when closing a buckyball is a bug in the snap transform or in the panel outlines, not a
tolerance to widen. Milestone 2 measures it rather than eyeballing it (§5).

### 2.5 Compatibility and snapping

A candidate connection is evaluated as:

1. **Descriptor match** — `SubType` and `Size` both equal (§2.2). `Straight_2000` mates
   only with `Straight_2000`. Cheap integer comparison, so reject on this first.
2. **Proximity** — socket positions within a tolerance.
3. **Orientation** — tangents collinear within a tolerance, in either polarity.
4. **Joint capacity** — the joint accepts another participant (§2.4). This replaces a simple
   occupancy test, and by default it passes.

Note what is *absent*: no dihedral check. The resulting angle is whatever the placement
produces, and remains free until further connections constrain it (§2.4).

Both polarities are always admissible (§2.3), so every passing socket pair yields two candidate
families rather than one solution.

#### One anchor, many connections

Placement and connection are separate questions, and conflating them is what makes §2.4's
over-determination look like a contradiction. A piece has **one transform** but may gain
**several connections** from a single placement. So:

> **Exactly one socket pair is the anchor. The piece's transform is solved from the anchor and
> from nothing else. Every other socket pair that lands within tolerance after that solve is
> adopted into its joint, and records the gap it was adopted at.**

The anchor is the pair the player is driving: the passing candidate nearest the held piece by
the §2.3 least-rotation reading. Its mating conditions then hold to float precision. Every
adopted connection holds to within whatever the panel geometry actually implies — float noise
if the outlines are cut correctly, and a measurable error if they are not.

**That exactness describes the moment of placement, not a standing invariant.** In the loose
regime the solver perturbs everything on the next tick, the anchor included (§2.10). It exists
so that error is *diagnosable at the instant it is introduced*, which is when the residual is
recorded; nothing may assert it a frame later.

What the rule buys:

- **No least-squares placement.** A solver that spreads the error across every mating pair
  leaves *no* connection exact, and exactness on one connection is the invariant that
  distinguishes a geometry bug from an accumulation artefact.
- **Determinism.** The transform becomes a pure function of the anchor pair, the polarity, and
  the dihedral, so a server re-derives it instead of trusting a client transform (§2.10).
- **A number to measure.** The adoption gap is the residual Milestone 2 is after, and it falls
  out of ordinary placement rather than needing an instrument built for it.

#### Solving the transform

The anchor pins five of the six degrees of freedom, leaving the dihedral θ about the shared
edge axis. θ and the polarity are chosen together, by one of two readings:

1. **Adopt-driven.** As the piece hinges, each of its other sockets sweeps a **circle** about
   the anchor axis. For a candidate target socket the θ bringing the two closest is closed
   form: decompose the target's offset from the anchor into components along and across the
   axis, then rotate the held socket's across-axis component onto the target's. One `atan2`, no
   iteration. The along-axis component does not rotate — **that leftover is the residual.** Run
   the search over both polarities, take the candidate with the smallest residual, and if it is
   within tolerance its θ and polarity are the placement.
2. **Player-driven.** If no secondary candidate is in tolerance, both come from the nearest
   point on the constraint manifold to the piece's current orientation. The player turns the
   piece roughly the way they want it and the snap commits to that reading (§2.3).

A secondary socket lying *on* the anchor axis sweeps a degenerate circle and has no θ to offer.
Detect the near-zero radius and skip it.

#### When a secondary does not fit

**Adopt or drop, per secondary — a failing secondary never rejects the anchor.** The player
gets the placement they asked for plus an unclosed seam they can see and nudge. Refusing a
whole placement because a third connection missed by a fraction of a millimetre makes the
builder feel broken for no gain; the piece is already where the player put it.

Accumulated error is what stops a sphere from closing, and a closed buckyball is the acceptance
test for this subsystem.

### 2.6 No authored inside or outside

**Pieces do not have an authored interior face, and sockets never constrain which way round a
piece is fitted.** The player decides, always.

The reasoning: inside/outside is not a property of a *joint*, so encoding it in the socket
constraint system puts the check in the wrong layer. Whether a placement is *sensible* depends
on the environment the piece ends up in — which is a runtime question the enclosure system
(§2.8) already answers — not on a flag set at authoring time.

Consequences, which are features rather than costs:

- **Hatches work both ways round.** A hatch is an ordinary piece with ordinary edge sockets
  (§2.8), and it does not care which side the pressure is on.
- **Racks do not care at all**, unless something stored in them needs an atmosphere — and that
  is a property of the stored item, checked at runtime.
- **Equipment may or may not function as intended** in vacuum versus atmosphere. That is
  gameplay, discovered by the player, not a placement rule that forbids the build.

So the rule is: **PolySnap decides whether pieces can physically connect. It never decides
whether the result is a good idea.** Environmental suitability is a separate, runtime concern
belonging to the equipment and atmosphere systems, which query the enclosure graph.

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
still knows about every individual piece, its sockets, and its connections. Save/load,
airtightness, and disassembly all depend on per-piece identity surviving the weld.

Welding is also **more than a performance optimisation**, because of what it does to geometry.
While loose, where a piece sits is the solver's working state — live, strained, and not to be
relied on. The weld snapshots it, and from that instant those transforms are durable data that
persistence stores and that nothing recomputes. It is the moment the assembly's shape stops
being provisional (§2.10).

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
  candidates to be treated as **airtight compartments** for atmosphere simulation. See below;
  this is mostly a graph walk, not geometry.
- **Structural queries** — connectivity, reachability, what breaks off if this piece is cut.
- **Environment lookup** — which enclosed volume, if any, a given **world position** sits in.
  Taking a position rather than a socket keeps PolySnap ignorant of what is asking: equipment,
  a pawn, a dropped item, all use the same query. This is what lets equipment decide at runtime
  whether it is in vacuum or atmosphere (§2.6), instead of that being constrained at build time.

The boundary: PolySnap reports **topology** — what is connected, what is enclosed. It does not
model pressure, gas, or temperature. An atmosphere system layered on top consumes these
queries.

The graph is derived from connections and should be maintained incrementally as pieces are
added and removed, not rebuilt by scanning the world.

#### Enclosure is a graph walk

Enclosure sounds like it needs geometry — closed volumes, inside versus outside — and almost
none of it does. Two passes over the graph answer it.

**Peel.** Repeatedly drop any piece with an edge socket in no joint, until nothing changes. A
free edge is a hole, and dropping a piece exposes its neighbours' edges in turn. What survives
is boundary-free and is the enclosure candidate. It is a peel rather than a single test because
a free edge somewhere in a component must not disqualify the whole of it — weld a fin to the
outside of a finished buckyball and the ball is still sealed.

**Propagate sides.** Every piece has two faces, its +Z and its −Z (CONVENTIONS §3), and §2.3's
tangent polarity already says how a joint pairs them:

- `Tangent_B == −Tangent_A` — B's +Z is on the same side of the surface as A's +Z.
- `Tangent_B == +Tangent_A` — B is flipped; its +Z pairs with A's −Z.

So pick any surviving piece, call its +Z "side one," and walk, flipping at every flipped
connection. Consistent all the way round means the surface is orientable and the two sides are
a global inside and outside. A contradiction means it is not orientable and bounds nothing.

This is what §2.6's "no authored inside" costs, and it costs nothing: **absolute** facing is
never needed, only the **relative** facing at each joint, and the snap already decided that.

**A hatch is an ordinary piece.** It has edge sockets, it joins like anything else, and it is
peeled or kept by the same rule. Whether it happens to be open changes nothing here: a closed
surface is a claim about topology, not about pressure. The atmosphere system takes PolySnap's
compartments and unions the ones an open hatch connects — the same division of labour as §2.6,
one level up. PolySnap says what *bounds* a volume; it never says the volume is sealed.

The one place topology genuinely runs out is a joint of degree three or more. Put a partition
across a cylinder and every edge is still shared, so the peel keeps everything and the walk
reports one enclosed region — but a bulkhead exists precisely to make two compartments, and the
question equipment actually asks is which one it is in. Three panels on an edge line create
three angular sectors, and deciding which pair bounds which compartment needs their cyclic
order about the axis. That is an angle, derived on demand (§2.4), and it is the only part of
enclosure that is not connectivity.

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

Each connection also records its **adoption residual** (§2.5) and whether it was the anchor.
Neither is needed to rebuild the assembly, since transforms are saved directly. Both are a few
bytes and they make a drifting save diagnosable long after the build that produced it.

Socket IDs must be stable across mesh re-exports, or existing saves break when art is updated.
That is cheap to guarantee because a piece's edge lengths never change (§2.2): a re-export may
fix modelling errors, adjust thickness, or add a socket, but a panel of a different size is a
different asset. Every socket name an existing save refers to is still there.

### 2.10 Replication-readiness

Multiplayer is **not** being implemented now, but PolySnap should be shaped so co-op does not
require a rewrite:

- Snapping is expressed as a **validated operation on the graph**, not as a client directly
  writing a transform. The payload is §2.5's placement inputs — anchor pair, polarity, and the
  secondary pair that selected the dihedral — so the server re-derives the transform rather
  than trusting one. An adopt-driven placement is therefore **integers only**; only the
  player-driven free-hinge case has to send a quantised θ.
- The numeric IDs from §2.9 double as network identity.
- **The graph is the authority on topology, always.** Physics never creates or destroys a
  connection, in either regime.
- **Authority over geometry follows the regime** (§2.7). While loose, the solver owns it:
  clients may visibly disagree by up to the constraint tolerance, and that divergence is
  cosmetic precisely because it is transient and nothing durable reads it. Welding ends that.
- **Welding is therefore server-authoritative.** It promotes solver state to permanent data, so
  one authority snapshots the transforms and replicates them. Letting each client freeze its own
  settled geometry would leave them disagreeing about where the panels are for good — the
  solver is iterative and not bit-identical across machines or frame timings.

---

## 3. Player movement (test harness only)

A simple Newtonian free-flying pawn — RCS-style translation and rotation, no gravity — is
needed to exercise the system: approach a rack, grab a piece, manoeuvre it, snap it.

This is **test scaffolding, not a product**. It lives in the `Construction` game module (or as
a Blueprint in `Content/`), never in PolySnap.

---

## 4. Content

Construction pieces — the hex and pent panels, struts, hatches — live in this project's
`Content/` directory, **not** inside the plugin. PolySnap supplies the types, rules, and
runtime behaviour; the panels are game content that happens to use it. A different game brings
its own panels and gets the same snapping system.

---

## 5. Milestones

1. **Two pieces snap edge-to-edge in PIE.** One piece type, one socket type. Proximity and
   orientation checks; exact snap transform; the resulting joint free to hinge. This proves
   the socket math before anything is built on top of it.
2. **A closed buckyball.** Hexes and pents assembling into a sealed truncated icosahedron,
   with the dihedral angles emerging from vertex closure rather than being authored (§2.4).
   The real geometric test: log every connection's **adoption residual** (§2.5) and report the
   worst case across the ninety edges. Float-noise residuals mean the snap math and the panel
   outlines are both right. Residuals at a tenth of a millimetre or worse mean the panels are
   cut wrong — which is a failure the `Size` token cannot see, being a rounded label (§2.2).

   **Run it with the pieces kinematic and physics off.** A constraint solver is iterative and
   leaves its bodies slightly off their ideal positions, so a residual measured against a
   simulating assembly mixes panel-geometry error, which is what this test is for, with solver
   strain, which is expected and unrelated. A flawless buckyball would report a nonzero number,
   and a real 0.2 mm error could hide beneath the noise. Rigid pieces sit exactly where the snap
   put them, so the residual measures only what it claims to. Whether the loose regime settles a
   strained loop gracefully is a real question too, and a separate test.
3. **Assembly graph and open-edge queries**, including a joint of degree three.
4. **Welding** — merge to a single rigid body, identities preserved, reversible.
5. **Save/load** of a complete assembly.
6. **Enclosure detection** — identify airtight volumes.

Atmosphere simulation and equipment come after the structural core works, and neither is
PolySnap's (§2.2, §2.8).

---

## 6. Plugin boundaries

Restating CLAUDE.md's rule, because it is the point of this project:

**PolySnap must have zero dependencies on the `Construction` game module.** No includes, no
references to project content, no assumptions about the player pawn or game mode. The game
module is a thin harness that wires the plugin into a test level. If PolySnap only works
because of something in `Construction`, that is a bug, not a shortcut.

Where a new feature belongs — new plugin, existing plugin, or game module — is decided
per feature, in conversation, not assumed.

---

## 7. Open questions

Marked explicitly so nobody builds on them as though they were settled.

- **Where equipment mounting lives.** Out of PolySnap (§2.2), but which plugin owns it, and
  whether it reuses this naming grammar under its own tag or invents its own, is undecided. The
  only thing settled is that the two coexist on a mesh without either knowing about the other.
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
- **Coincident joints.** Joining two subassemblies normally creates fresh joints between free
  sockets and needs no merging (§2.4). The exception is a joint on each side landing on the
  same edge line — four panels arriving as two pre-formed pairs — which would need two existing
  joints to become one, with an identity to pick between them and a save format that survives
  it. Rare enough to defer, common enough that refusing it silently would be a bug.
- **Magnets.** The idea of magnet sockets to assist alignment is open. Current recommendation:
  do *not* build a separate magnet plugin yet. Assisted alignment is better understood as an
  attraction behaviour of the existing socket search — pulling a held piece toward the best
  candidate socket pair — which needs no new concept. Revisit only if magnets turn out to need
  behaviour that socket attraction genuinely cannot express.
- **Weld merge mechanics.** Which merge strategy preserves reversibility at acceptable cost.
- **Enclosure at T-junctions.** The algorithm is otherwise settled (§2.8): peel free edges,
  propagate sides from tangent polarity. What is not is splitting a degree-3 joint into
  separate compartments by angular order.
- **Environment queries.** What exactly equipment asks for, and how cheaply. "Am I in an
  enclosed volume" is a graph query; "is that volume actually pressurised" belongs to a future
  atmosphere system (§2.8). The interface between them is undesigned.
- **Tolerances.** Snap distance and angular thresholds; whether they are global, per socket
  type, or scaled by piece size. The **adoption tolerance** for secondary connections (§2.5) is
  a third and behaves differently from the other two: too tight and a correctly built vertex
  refuses its second connection, too loose and visibly misaligned panels get welded into the
  graph as though they had closed.
- **Anchor selection.** The anchor is exact and every adopted connection carries the residual,
  so which pair anchors decides where the error lands. Whether the player-driven pair is always
  the right anchor — and whether it is worth re-anchoring a piece once the assembly has relaxed
  — is unexplored.
- **Framework choices.** Enhanced Input, GAS, StateTree, and similar are undecided and will be
  chosen per feature rather than adopted wholesale up front.
