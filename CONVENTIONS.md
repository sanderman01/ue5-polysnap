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

The naming grammar's `Size` field adds a fourth unit. The chain for a nominal 2000 edge:

```
Size token   2000        (millimetres — a label, never used in math)
Blender      2.0 m
FBX          200.0       (centimetres)
Unreal       200.0 uu
```

**The FBX is written in centimetres, not metres.** Centimetres are FBX's native unit, and
Unreal reads the numbers in the file as centimetres directly — it does not scale them by any
unit metadata. Blender's **Apply Unit** export setting (§4) is what performs the metre-to-
centimetre conversion on the way out, which is why turning it off is the classic "everything
imported 100× too small" bug: 2.0 goes out as `2.0` and arrives as 2 cm.

The practical consequence is that **no second unit conversion may be enabled anywhere** — see
Convert Scene Unit in §5. The chain above has exactly one scaling step in it, and §7's first
calibration check is there to confirm that.

`Size` is compared for equality and nothing else (README §2.2). The one place these units are
related is the import-time validator (§6).

---

## 2. The socket basis

The load-bearing convention. README §2.3 names the three directions; here is which axis each
one *is*.

| Role | Unreal socket local | Blender empty local | Meaning |
| --- | --- | --- | --- |
| **Outward** | **+X** (Forward) | **−Y** | In the panel plane, perpendicular to the edge, away from the piece's centre. |
| **Tangent** | **+Y** (Right) | **−X** | Along the edge. |
| **Normal** | **+Z** (Up) | **+Z** | The panel's surface normal. Arbitrary which face; see README §2.6. |

**Outward takes +X** because every Unreal system that consumes a socket transform assumes a
thing points along its local +X. Tangent and Normal then fall into Right and Up.

```cpp
const FTransform Socket = Mesh->GetSocketTransform(SocketName, RTS_Component);
const FVector Outward = Socket.GetUnitAxis(EAxis::X);
const FVector Tangent = Socket.GetUnitAxis(EAxis::Y);
const FVector Normal  = Socket.GetUnitAxis(EAxis::Z);   // == Outward ^ Tangent
```

Unreal's *coordinate system* is left-handed, but `FVector::CrossProduct` is the standard
right-hand formula, so `X ^ Y == Z` is what the code sees — README §2.3's relation needs no sign
corrections anywhere in Unreal.

**It does need one in Blender.** The physical triad here — face along Outward, right hand along
Tangent, head along Normal — is left-handed, exactly like Unreal's (Forward, Right, Up). Unreal
hides that by pairing a left-handed coordinate system with a right-handed cross product, so the
two inversions cancel. Blender's coordinate system is genuinely right-handed and nothing
cancels, so the same socket satisfies

```
Blender:   Outward × Tangent == −Normal      (−Y) × (−X) == −Z
Unreal:    Outward ^ Tangent == +Normal      (+X) ^ (+Y) == +Z
```

Both describe the identical physical arrangement. Anyone porting README §2.3's relation into a
Blender-side sanity script without flipping the sign will reject correct assets — this is the
only place in the pipeline where the handedness difference is visible to a human rather than
absorbed by the exporter.

**Socket scale is always 1.** PolySnap reads rotation and translation only.

**Outward is the vector that sweeps as the dihedral changes**: at 180° (coplanar)
`Outward_B == -Outward_A`, and folding rotates the two toward each other about the shared
tangent. README §2.3 defines the angle itself — signed, over [0°, 360°), in the anchor socket's
basis. Both branches away from 180° are real folds; the sign only says which side of the anchor
panel's plane the other landed on, and means nothing about inside or outside.

### The authoring mental model

The Blender column's double negatives are hard to remember. Use one picture instead:

> The empty is a **person standing on the panel at the middle of the edge**, feet on the surface
> (head along **Normal**), facing off the edge away from the panel (**Outward**). Their right
> hand points along the edge (**Tangent**).

That person faces −Y in Blender — the direction a model faces in front view — and +X in Unreal.
Same person, both applications.

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

> **Socket `001`'s Outward points along the piece's local +X.**

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

Follow README §2.2 exactly: `SOCKET_Edge_001_Straight_2000`. Because Blender object names are
unique per *file*, a socket may also carry an **authoring tail** — anything from a `.` onward,
which the importer keeps and the parser ignores. Prefer a meaningful one (`.Pent`) over letting
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

## 4. Blender FBX export settings

Blender 4.x, `File → Export → FBX (.fbx)`. Unlisted fields stay at their defaults.

| Section | Setting | Value | Why |
| --- | --- | --- | --- |
| Include | Limit to **Selected Objects** | on | |
| Include | **Object Types** | Mesh + Empty | **Mandatory** — without Empty there are no sockets and the import silently succeeds. |
| Include | Custom Properties | off | Metadata lives in the socket name. |
| Transform | Scale | 1.00 | |
| Transform | Apply Scalings | All Local | Default. |
| Transform | **Forward** | **−Z Forward** | Default. Do not change — §2's axis table is derived from it. |
| Transform | **Up** | **Y Up** | Default. Same. |
| Transform | **Apply Unit** | **on** | Converts Blender's metres into FBX's centimetres (§1). Off is the classic "everything is 100× too small" bug. |
| Transform | **Apply Transform** | **off** | Rewrites object transforms on the way out — exactly what a socket empty consists of. |
| Geometry | Smoothing | Face | Unreal wants explicit smoothing groups. |
| Geometry | Apply Modifiers | on | |
| Geometry | Tangent Space | on | Needed for normal-mapped panels. |
| Geometry | Loose Edges | off | |
| Armature | Bake Animation | off | Static meshes only. |

Every "my sockets are rotated 90°" report traces back to the two Transform axis fields or to
Force Front X Axis (§5).

---

## 5. Unreal import settings

Static mesh import. Unlisted fields stay at their defaults.

| Setting | Value | Why |
| --- | --- | --- |
| Skeletal Mesh | off | |
| Combine Meshes | off | One panel, one asset. |
| Import Uniform Scale | 1.0 | The file is already in centimetres (§1). Nothing here needs scaling. |
| **Convert Scene Unit** | **off** (default) | Would rescale by the file's unit metadata — a second conversion on top of Blender's Apply Unit. The chain in §1 has exactly one scaling step and this is how it stays that way. |
| Convert Scene | on | Performs the FBX→Unreal axis conversion §2's table assumes. |
| **Force Front X Axis** | **off** | Adds a 90° yaw to everything, invalidating §2's axis table. |
| Auto Generate Collision | off | Panels need authored collision; a generated hull will not respect an edge profile (README §2.4). |
| **Transform Vertex to Absolute** | **on** (default) | Bakes the FBX node transform into the vertices. Harmless when every piece is authored at identity, and the reason §3 insists on it. |

**Transform Vertex to Absolute** is what makes §3's "stack every piece at the origin" rule
mandatory rather than merely tidy. Turning it off would import each mesh relative to its own
pivot and so permit pieces laid out side by side in Blender, but it changes how every asset
imports and interacts with Combine Meshes. That is not a blast radius worth accepting to buy
viewport convenience: leave it on and author at identity.

Unreal creates static mesh sockets from FBX nodes prefixed `SOCKET_`, stripping the prefix
(README §2.2). The node must be an FBX null, which is what a parented Blender empty produces.

**UE 5.8 uses Interchange for FBX import by default.** The legacy importer's socket handling is
well documented; Interchange's is the thing to confirm on the calibration asset (§7). If it
diverges, the legacy path is still available via Project Settings → Interchange.

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
| `EdgeLength_uu * 10` is within **1%** of `Size_mm` | A wrong export scale. |

The scale check is the one that earns its keep, though not for the reason it looks like. Its
value is catching a 100× unit error on asset one rather than 40 assets later; `Size` is simply
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
| A socket's authoring tail is purely numeric | `bWarnOnNumericAuthoringTail` | Blender assigned the tail itself, so a name collision went unnoticed. Benign, but `.001` reads like the `ID` field. |

All of them live in PolySnap's own `UDeveloperSettings`, not in project config — the plugin
ships its conventions with it and a downstream project overrides them locally.

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

Several things here are derived from how the Blender exporter and the Unreal importer compose
rather than from anything either one documents, and should be **confirmed once on a throwaway
asset before panels are authored in bulk**: §2's axis mapping, Interchange's socket handling
(§5), and how both treat an authoring tail. Every later asset inherits whatever is actually
true, so the cost of not checking is a re-export of the entire panel set.

### The asset

Specified exactly, so every check below has a number to compare against rather than an
impression:

> A **square panel, 2000 × 2000 × 40 mm**, centred on the Blender origin and lying in the XY
> plane. One empty named `SOCKET_Edge_001_Straight_2000`, placed at Blender **`(0, −1.0, 0)`** —
> the midpoint of the −Y edge — with **zero rotation**.

Zero rotation is the point of the design. An empty with no rotation of its own has local −Y
along global −Y, which §2's table says is Outward, and −Y is the edge the socket sits on. So any
rotation the socket shows in Unreal came from the axis conversion and nothing else, which is
precisely what is being calibrated. Author per §3, export per §4, import per §5.

### The checks

1. **Scale.** The mesh bounds read **200 × 200 × 4 uu**.
2. **Socket position.** The socket sits at **`(100, 0, 0)`** in the static mesh editor. This is
   the whole axis table in one number: Blender `(0, −1.0, 0)` is one metre along Blender −Y,
   which §2 maps to Unreal +X at 100 uu. A socket at `(0, −100, 0)` or `(0, 100, 0)` means the
   conversion is not what §2 assumes.
3. **Socket axes.** Its rotation is **identity** — the gizmo's **red (+X)** arrow points away
   from the panel in its plane, **green (+Y)** runs along the edge, **blue (+Z)** is the surface
   normal.
4. **Round trip, by hand.** This one needs no code. Place instance A at the world origin, and
   instance B at **`(200, 0, 0)` with yaw 180°**. Their sockets now coincide at `(100, 0, 0)`
   with opposed Outwards and reversed Tangents — a coplanar 180° seam, and the two panels should
   read as one flush 2 × 4 m rectangle with no step, gap, or overlap at the join. Any visible
   seam artefact here is an axis or scale error, not a snapping bug; there is no snapping yet.
   Re-run it as milestone 1's first test once the snapper exists.
5. **Authoring tail.** Re-export with the socket renamed `SOCKET_Edge_001_Straight_2000.Pent`.
   It imports with the tail intact and parses to ID `001`, confirming the importer neither
   mangles nor swallows the `.` (README §2.2).
6. **Foreign socket.** Add a second empty named `SOCKET_Mount_A` and re-export. It imports as a
   socket, and PolySnap ignores it without a diagnostic (README §2.2). Confirms the plugin can
   share a mesh with another system, which is the whole basis for equipment living elsewhere.
7. **Batch export.** Blender's FBX exporter has a **Batch Mode → Collection** that writes one
   file per collection, which is the natural fit for §3's one-collection-per-piece rule. It is
   absent from §4's table because its interaction with *Limit to Selected Objects* and the
   Transform axis fields is unconfirmed. Check it here; if it behaves, add it to §4.

If (1) fails, a second unit conversion is switched on somewhere — check Apply Unit (§4) and
Convert Scene Unit (§5) before touching anything else.

If (2) or (3) fails, §2's **Blender** column needs correcting, not the Unreal column: the Unreal
roles come from engine convention and stay as they are.
