# Conventions

Companion to [README.md](README.md). The README describes *what* we are building and *why*; this
file pins down the mechanical conventions — coordinate systems, which local axis carries which
meaning, and the Blender and Unreal settings that keep the two agreeing.

Most of this is **fixed**: once panels are authored at volume, changing it means re-exporting
every asset. The few rules that are house style rather than requirement are marked as optional
where they appear, and are enforced only by a validator warning (§6).

---

## 1. Coordinate systems and units

| | Blender | FBX (on disk) | Unreal |
| --- | --- | --- | --- |
| Up axis | +Z | +Y | +Z |
| Handedness | right | right | left |
| Unit | 1 unit = 1 m | **centimetres** | 1 unit = 1 cm |

The naming grammar's two size fields, `Thickness` and `Length`, add a fourth unit. The chain for
a nominal 2000 edge:

```
Length token 2000        (millimetres — a label, never used in math)
Blender      2.0 m
FBX          200.0       (centimetres)
Unreal       200.0 uu
```

`Thickness` runs down the same chain: the 40 mm calibration panel is 0.04 m in Blender and 4.0 uu
in Unreal. Unlike `Length` it is never measured back off the asset (README §2.2), so the token is
the only place the number is recorded.

Default Blender scene settings with default export and import settings (§4, §5) put exactly
**one** scaling step in that chain: Blender's export, converting metres to FBX's centimetres.
Nothing else may scale. A panel that imports 100× too small or too large means a second
conversion is switched on somewhere; §7's first check confirms this once.

Both size tokens are compared for equality and nothing else (README §2.2). The one place these units are
related is the import-time validator (§6).

---

## 2. The socket basis

The load-bearing convention. README §2.3 names the three directions; here is which axis each
one *is*.

| Role | Blender empty local | Unreal socket local | Meaning |
| --- | --- | --- | --- |
| **Outward** | **+X** | **+X** (Forward) | In the panel plane, perpendicular to the edge, away from the piece's centre. |
| **Tangent** | **+Y** | **−Y** | Along the edge. |
| **Normal** | **+Z** | **+Z** (Up) | The panel's surface normal. Arbitrary which face; see README §2.6. |

**The Blender column is chosen for ease of authoring.** An empty with zero rotation is already
a valid socket on the piece's +X edge, so authoring a socket is "place it, then yaw about Z"
and nothing else.

The Unreal column is what that becomes after import: the conversion negates Y — that is how a
right-handed Blender basis is expressed in Unreal's left-handed one — so **Tangent lands on
−Y**. Outward keeping +X is the part that matters at runtime, because every Unreal system that
consumes a socket transform assumes a thing points along its local +X.

**Tangent is −Y, not +Y.** Derive it through the sign, or every polarity test in the snapper
runs backwards:

```cpp
const FTransform Socket = Mesh->GetSocketTransform(SocketName, RTS_Component);
const FVector Outward = Socket.GetUnitAxis(EAxis::X);
const FVector Tangent = -Socket.GetUnitAxis(EAxis::Y);
const FVector Normal  = Socket.GetUnitAxis(EAxis::Z);   // == Tangent ^ Outward
```

That last comment is the other consequence. The triad is right-handed as a *physical*
arrangement, so `Outward × Tangent == +Normal` in Blender, where the coordinate system is
right-handed too. Unreal's coordinate system is left-handed while `FVector::CrossProduct` is
the plain right-hand formula, so the identical socket computes as
`Outward ^ Tangent == −Normal` there. Unreal code that needs Normal from the other two writes
`Tangent ^ Outward`; code that reads `EAxis::Z` directly is unaffected.

**Socket scale is always 1.** PolySnap reads rotation and translation only.

**Outward is the vector that sweeps as the dihedral changes**: at 180° (coplanar)
`Outward_B == -Outward_A`, and folding rotates the two toward each other about the shared
tangent. README §2.3 defines the angle itself — signed, over [0°, 360°), in the anchor socket's
basis. Both branches away from 180° are real folds; the sign only says which side of the anchor
panel's plane the other landed on, and means nothing about inside or outside.

### The authoring mental model

> The empty is a **person standing on the panel at the middle of the edge**, feet on the surface
> (head along **Normal**), facing off the edge away from the panel (**Outward**). Their **left**
> hand points along the edge (**Tangent**).

That person faces +X in both applications. Their left hand is Blender's +Y and Unreal's −Y —
same hand, same physical direction, opposite sign.

### Assets from another pipeline

The table above is a **default**, not a hard-coded assumption. PolySnap is built to be dropped
into a project whose panels may not have come from this Blender recipe at all — a socket authored
directly in the Unreal static mesh editor, for instance, most naturally has `Tangent` on **+Y**,
because nothing negated it on the way in.

So the mapping is configured in two places:

| Where | Scope |
| --- | --- |
| `UPolySnapSettings::DefaultSocketAxes` (Project Settings → Plugins → PolySnap → Conventions) | The project's pipeline. Defaults to the table above. |
| `Socket Axes` on a piece's `PolySnap Piece` component, behind its override tick-box | One piece whose mesh came from somewhere else. |

**All three roles are declared, and all three are honoured exactly.** That is why the settings are
strict about the third one: a socket frame is orthonormal with determinant +1, so naming `Outward`
and `Tangent` already fixes `Normal` down to its sign, and a triad that disagrees about that sign
is not a basis any socket can have. PolySnap rejects such a mapping and names the axis to flip
rather than silently honouring two declarations out of three. The Unreal-native example above is
therefore declared `Outward +X, Tangent +Y, Normal −Z` — which face `Normal` names is arbitrary
(§2 above), but it has to be the face the other two put it on.

That leaves **24 legal mappings** — six choices of `Outward`, four perpendicular `Tangent`s,
`Normal` forced — which are exactly the 24 rotations of a cube.

At runtime the mapping is applied once, where a socket transform is read off the mesh
(`UPolySnapPieceComponent::GetSocketWorldTransform`), and everything downstream works in the
canonical convention above. Nothing in the snapping math knows the setting exists.

---

## 3. Blender authoring

### The piece

**Author panels flat, lying in the XY plane, surface normal along +Z.** Not standing up like a
wall.

Every edge socket has `Normal = +Z` (§2), so a flat panel makes every socket's Normal parallel to
the piece's own +Z: sockets then differ **by rotation about Z only**. A hex panel becomes
verifiable by inspection — six sockets, `Roll 0 / Pitch 0`, yaw stepping by 60°, `Z = 0`. Author
it standing and every socket carries a compound rotation instead, checkable only by geometry.

There is no "up" for a panel to stand relative to: the game has no gravity, and a hex in a
buckyball ends up at every orientation there is.

This is separate from the person-mnemonic in §2, which orients an *individual socket empty*. The
piece itself has no facing direction.

Non-planar pieces generalise the rule: the dominant surface lies in XY, and a linear piece such
as a strut runs its long axis along +X.

### The origin

- **Centroid, in-plane.** README §2.3 defines Outward relative to "the piece's centre", so the
  origin should *be* that centre. It also makes a held piece tumble about itself.
- **Mid-thickness, never on a face.** Put `Z = 0` at the mid-surface and keep the panel symmetric
  about that mid-plane wherever the design allows.

Mid-thickness is the rule with teeth. Flipping (README §2.3) is a 180° rotation about a socket's
Outward axis; with sockets at mid-thickness on a symmetric panel it maps the panel onto itself,
so the hull surface stays put and only which face things are mounted on changes — exactly what
§2.3 says flipping should do. With the origin on a face, **every flip displaces the panel by its
thickness**, surfacing much later as drift when closing a buckyball and reading as a bug in the
snap math.

### Optional: the canonical piece frame

> **Socket `001`'s Outward points along the piece's local +X** — that is, socket `001`'s empty
> has zero rotation (§2).

This makes a piece's authored frame deterministic: panels of the same type become directly
comparable and rotational variants trivial to diff. Without it, a piece's yaw about its own
normal is arbitrary — harmless, but arbitrary.

House style, not a requirement of the snapping math. A project with its own idea of a canonical
frame turns the warning off (§6); nothing at runtime consults it.

### The empty

- Type **Empty → Plain Axes** or **Arrows**. Arrows shows the local axes in the viewport, which
  is the only way to eyeball a socket's orientation before export.
- **Parent it to the mesh object** (`Ctrl+P` → Object, Keep Transform). Unreal only recognises
  sockets that are children of the mesh node in the FBX hierarchy.
- Empty *display size* is cosmetic; empty **object scale must stay 1**.
- Place it at the **midpoint of the edge**.

### Naming

The **object name** in the outliner becomes the FBX node name, and therefore the socket name —
not the data name, not a custom property.

Follow README §2.2 exactly: `SOCKET_Edge_001_Straight_40_2000` — the last two fields are the
edge's length and the panel's thickness, both in millimetres. Because Blender object names are
unique per *file*, a socket may also carry an **authoring tail** — anything from a `.` onward,
which the parser ignores. The importer **rewrites that `.` to `_`** (§5), so the tail reaches
Unreal as a sixth field rather than verbatim, and that sixth field is the only spelling the
parser knows. Prefer a meaningful one (`.Pent`) over letting
Blender assign `.001`; the validator warns about the numeric kind (§6).

### Transforms

The **mesh** object sits at the world origin with identity rotation and scale 1; apply transforms
(`Ctrl+A` → All Transforms) before export.

**Apply them before parenting the empties, not after.** Applying a transform to an object that
already has children makes Blender push a compensating transform into each child to hold it in
place, and the child of a socket empty is its rotation — which is the data (below). Get the mesh
clean first, then parent. If you must apply after parenting, re-check every empty's rotation in
the N-panel before exporting.

The **empty's rotation is the data** — an empty has no mesh data to bake it into, so whatever is
in the N-panel is what ships. Hence **Apply Transform must stay off** (§4).

### Several pieces in one file

Authoring a whole panel family in one `.blend` is worth doing: it is the only cheap way to
guarantee that a hex and a pent genuinely share an edge length rather than nearly sharing one.
Two rules make it safe.

**Every piece keeps an identity transform, stacked at the origin.** Do not lay pieces out side
by side. Unreal bakes the FBX node transform into the vertices (§5), so a pent authored at
`(5, 0, 0)` imports with its geometry 500 uu off the asset origin while its parented sockets
stay correct in local space. The origin is then no longer the centroid, §6's inward-Outward
warning starts firing on assets that are actually fine, and every placement is offset — which
reads as a bug in the snap math and is not. Work on one piece at a time with collection
visibility or local view (`/`).

**One collection per piece.** Authoring tails are suffixes, so a file's socket names interleave
alphabetically in the outliner and the collection is what gives a piece visual grouping. It also
makes "select this mesh and its empties" a single action, which matters because an export that
drops the empties **succeeds silently** (§4).

---

## 4. Blender scene and FBX export settings

**Default Blender scene settings, and default FBX exporter settings** (`File → Export →
FBX (.fbx)`, Blender 4.x). The defaults produce §1's unit chain and §2's axis table; the
Transform section is where changing a field silently breaks one or the other, so leave
**Forward −Z / Up Y**, **Apply Unit on** and **Apply Transform off** exactly as they come. Apply
Transform in particular rewrites object transforms on the way out, and a socket empty *is* an
object transform (§3).

Two things the defaults do not decide:

- **The export must contain the empties.** Object Types defaults to all types, which is
  correct — but an export that drops them **succeeds silently** and lands a panel with no
  sockets at all.
- **One file per piece.** With a panel family in one `.blend` (§3), either tick **Limit to
  Selected Objects** or use **Batch Mode → Collection**, which fits the one-collection-per-piece
  rule. Neither touches axes or units; batch mode's interaction with the rest is unconfirmed, so
  check it on the calibration asset (§7) before relying on it.

---

## 5. Unreal import settings

**Use the default import pipeline** — UE 5.8's Interchange FBX pipeline, settings untouched.
Convert Scene on, Convert Scene Unit off, Force Front X Axis off, Import Uniform Scale 1.0 and
Transform Vertex to Absolute on are all defaults, and together they are what §1 and §2
describe.

Two consequences of the defaults are worth naming:

- **Transform Vertex to Absolute bakes the FBX node transform into the vertices.** Harmless
  when every piece is authored at identity, and the reason §3 insists on it rather than merely
  recommending it.
- **Auto-generated collision is the one thing to switch off** per asset. Panels need authored
  collision; a generated hull will not respect an edge profile (README §2.4). That is a content
  decision, not a change to the pipeline.

Unreal creates static mesh sockets from FBX nodes prefixed `SOCKET_`, stripping the prefix
(README §2.2). The node must be an FBX null, which is what a parented Blender empty produces.
**Interchange rewrites the authoring tail's `.` to `_`.** Confirmed on the calibration asset
(§7 check 5): `SOCKET_Edge_004_Straight_40_2000.Pent` imports as
`Edge_004_Straight_40_2000_Pent`. The parser splits on the sixth field alone and never looks for
the `.`, which the head's fixed arity of five fields makes unambiguous (README §2.2). Nothing needs to change in the export; the `.` is
still the right thing to author — it just does not survive the trip.

---

## 6. What the validator enforces

PolySnap's editor module parses socket names at import (README §2.2). With the basis convention
fixed it can also check geometry against the name, which is where mistakes in this document's
settings actually surface — at import, never as a runtime surprise.

Name parsing has one ordering rule worth repeating here: **strip the authoring tail before
checking ID uniqueness.** The other way round, two pieces sharing a `.blend` fail for an error
neither of them has. Sockets not named `Edge_*` belong to some other system and are skipped
without a diagnostic (README §2.2).

Two hard checks, and they are deliberately few:

| Check | Catches |
| --- | --- |
| The socket's rotation matrix is orthonormal with determinant +1, and socket scale ≈ 1 | A scaled, sheared, or mirrored empty. Read the **raw matrix**, not `GetUnitAxis` — that accessor goes through `TransformVectorNoScale`, so a check written on its output is orthonormal by construction and can never fail. |
| `EdgeLength_uu * 10` is within **1%** of `Length_mm` | A wrong export scale. |

`Thickness` gets no row: a socket transform carries a point and a basis, not a cross-section, so
there is nothing to measure it against (README §2.2). It is checked as a token by the parser and
no further.

The scale check is the one that earns its keep, though not for the reason it looks like. Its
value is catching a 100× unit error on asset one rather than 40 assets later; `Length` is simply
the free oracle that makes such a check possible at all, since the name states an intended
length. It is **not** a label check — a percentage tolerance is deliberate, because a panel
mislabelled by a millimetre or two announces itself the first time it refuses to snap, which is
faster and clearer than any import warning.

Everything else is a **warning** a project that authors differently can disable:

| Check | Setting | Catches |
| --- | --- | --- |
| `Outward · (SocketPos − MeshCentroid) > 0` | `bWarnOnInwardOutward` | Outward pointing into the piece — a 180° slip. Also catches swapped Outward/Tangent roles, since an Outward running along the edge drives the dot product to ≈ 0. A warning rather than an error because a concave outline can fail it legitimately. |
| On a planar piece, every socket Normal ∥ the piece's local +Z | `bWarnOnNonCanonicalPieceFrame` | A panel authored standing, or a socket with stray roll. |
| On a planar piece, socket `Z` ≈ the piece's mid-thickness | `bWarnOnSocketOffMidPlane` | An origin on a face — the flip-displacement trap (§3). Scoped to planar pieces, so it sits in the same tier as the convention it depends on. |
| Socket `001`'s Outward ∥ the piece's local +X | `bWarnOnUnalignedPrimarySocket` | A piece not following the canonical-frame rule (§3). |
| A socket's authoring tail is purely numeric | `bWarnOnNumericAuthoringTail` | Blender assigned the tail itself, so a name collision went unnoticed. Benign, but the `_001` it imports as reads like the `ID` field. |

All of them live in PolySnap's own `UDeveloperSettings`, not in project config — the plugin
ships its conventions with it and a downstream project overrides them locally.

**The validator checks against `DefaultSocketAxes` only.** It runs from an import hook, on a
`UStaticMesh`, with no piece component to ask — so a mesh whose piece overrides the socket axis
mapping (§2) is checked against the project's convention rather than its own. A `Tangent` sign
difference costs nothing, because the edge-length measurement is a max minus a min along that
axis and the sign cancels; an `Outward` sign difference or an `Outward`/`Tangent` swap measures a
different edge and **errors on a correct asset**. Set `DefaultSocketAxes` to whichever pipeline
the project's assets mostly come from.

### What is deliberately not checked

Two mesh-inspecting checks were considered and dropped, because they cost the most and catch the
least: **"Tangent is parallel to a real mesh edge at that position"** and **"the socket sits at
that edge's midpoint."** Neither survives a chamfered or profiled edge, where there is no single
mesh edge under the socket to compare against — and profiled edges are exactly what README §2.4
relies on for its mechanical fold limit. The midpoint check also pre-answers the collinear
subdivision question (README §7), which may yet want sockets somewhere other than the midpoint.

What they were buying — swapped axis roles — is covered twice over: by the inward-Outward
warning above, and by §7's calibration, which catches an axis-role error once, visually, before
any panel is authored in bulk. That is the right place for it.

---

## 7. Calibration

Run this **once on a throwaway asset before panels are authored in bulk**. It confirms that the
pipeline in §4 and §5 is actually set up as described, and pins down the two things nothing
here specifies: Interchange's treatment of an authoring tail, and batch export. Every later
asset inherits whatever is actually true, so the cost of not checking is a re-export of the
entire panel set.

### The asset

Specified exactly, so every check below has a number to compare against rather than an
impression:

> A **square panel, 2000 × 2000 × 40 mm**, centred on the Blender origin and lying in the XY
> plane. One empty named `SOCKET_Edge_001_Straight_40_2000`, placed at Blender **`(1.0, 0, 0)`**
> — the midpoint of the +X edge — with **zero rotation**. The trailing `40` is the panel's
> thickness in millimetres, taken from the 40 mm in the line above.

Zero rotation is the point of the design: §2's Blender column makes an unrotated empty a correct
socket for the +X edge, so anything the socket shows in Unreal other than what check 3 states
came from the pipeline and nothing else. Author per §3, export per §4, import per §5.

### The checks

1. **Scale.** The mesh bounds read **200 × 200 × 4 uu**.
2. **Socket position.** The socket sits at **`(100, 0, 0)`** in the static mesh editor — Blender
   `(1.0, 0, 0)` is one metre along +X, which is 100 uu along Unreal +X.
3. **Socket axes.** The gizmo reads: **red (+X)** points away
   from the panel in its plane (Outward), **green (+Y)** runs along the edge — its **negative**
   is Tangent (§2) — and **blue (+Z)** is the surface normal.
4. **Round trip, by hand.** This one needs no code. Place instance A at the world origin, and
   instance B at **`(200, 0, 0)` with yaw 180°**. Their sockets now coincide at `(100, 0, 0)`
   with opposed Outwards and reversed Tangents — a coplanar 180° seam, and the two panels should
   read as one flush 2 × 4 m rectangle with no step, gap, or overlap at the join. Any visible
   seam artefact here is an axis or scale error, not a snapping bug; there is no snapping yet.
   Re-run it as milestone 1's first test once the snapper exists.
5. **Authoring tail.** Re-export with the socket renamed `SOCKET_Edge_001_Straight_40_2000.Pent`.
   It imports as `Edge_001_Straight_40_2000_Pent` — the importer **rewrites the `.` to `_`**
   rather than preserving or swallowing it — and still parses to ID `001` (README §2.2).
   Confirmed.
6. **Foreign socket.** Add a second empty named `SOCKET_Mount_A` and re-export. It imports as a
   socket, and PolySnap ignores it without a diagnostic (README §2.2). Confirms the plugin can
   share a mesh with another system, which is the whole basis for equipment living elsewhere.
7. **Batch export.** Blender's FBX exporter has a **Batch Mode → Collection** that writes one
   file per collection, the natural fit for §3's one-collection-per-piece rule. Its interaction
   with *Limit to Selected Objects* is unconfirmed (§4); check it here, and if it behaves, use
   it.

If (1) fails, a second unit conversion is switched on somewhere — check Apply Unit (§4) and
Convert Scene Unit (§5) before touching anything else.

If (2) or (3) fails, a Transform field in the exporter or the import pipeline has been changed
from its default. §2's table is not the thing to adjust.
