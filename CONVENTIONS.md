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
| Unit | 1 unit = 1 m | metres + unit metadata | 1 unit = 1 cm |

The naming grammar's `Size` field adds a fourth unit. The chain for a nominal 2000 edge:

```
Size token   2000        (millimetres — a label, never used in math)
Blender      2.0 m
Unreal       200.0 uu
```

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
corrections anywhere.

**Socket scale is always 1.** PolySnap reads rotation and translation only.

**Outward is the vector that sweeps as the dihedral changes**: at 180° (coplanar)
`Outward_B == -Outward_A`, and folding inward rotates the two toward each other about the shared
tangent.

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
so the hull surface stays put and only the attachment sockets change sides — exactly what §2.3
says flipping should do. With the origin on a face, **every flip displaces the panel by its
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

Follow README §2.2 exactly: `SOCKET_Edge_001_Straight_2000`. Watch for Blender's `.001` duplicate
suffix, which the validator rejects deliberately because a duplicated empty carries a duplicated
`ID`.

### Transforms

The **mesh** object sits at the world origin with identity rotation and scale 1; apply transforms
(`Ctrl+A` → All Transforms) before export.

The **empty's rotation is the data** — an empty has no mesh data to bake it into, so whatever is
in the N-panel is what ships. Hence **Apply Transform must stay off** (§4).

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
| Transform | Apply Unit | on | Writes the metre metadata Unreal reads for scale. |
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
| Import Uniform Scale | 1.0 | Scale comes from the FBX unit metadata. |
| Convert Scene | on | Performs the FBX→Unreal axis conversion §2's table assumes. |
| **Force Front X Axis** | **off** | Adds a 90° yaw to everything, invalidating §2's axis table. |
| Auto Generate Collision | off | Panels need authored collision; a generated hull will not respect an edge profile (README §2.4). |

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

| Check | Catches |
| --- | --- |
| Basis orthonormal, socket scale ≈ 1 | A scaled or sheared empty. |
| `Outward ^ Tangent ≈ Normal` | An empty rotated into a mirrored basis. |
| `Outward · (SocketPos − MeshCentroid) > 0` | Outward pointing into the piece — a 180° slip. |
| Tangent parallel to a real mesh edge at that position | The socket is not on an edge, or the axis roles are swapped. |
| Socket position ≈ that edge's midpoint | A socket slid along its edge. |
| `abs(EdgeLength_uu * 10 − Size_mm) ≤ 1` | Wrong export scale **and** a mislabelled `Size`, in one test. |
| Socket `Z` ≈ the piece's mid-thickness at that edge | An origin on a face — the flip-displacement trap (§3). |

The scale check earns its place: it ties the `Size` label to geometry exactly once, so the
runtime can keep treating `Size` as an opaque token nothing measures with. A panel exported at
the wrong scale fails on asset one rather than 40 assets later.

Two further checks enforce §3's authoring conventions rather than correctness, so both are
**warnings** a project that authors differently can disable:

| Check | Setting | Catches |
| --- | --- | --- |
| On a planar piece, every socket Normal ∥ the piece's local +Z | `bWarnOnNonCanonicalPieceFrame` | A panel authored standing, or a socket with stray roll. |
| Socket `001`'s Outward ∥ the piece's local +X | `bWarnOnUnalignedPrimarySocket` | A piece not following the canonical-frame rule (§3). |

Both live in PolySnap's own `UDeveloperSettings`, not in project config — the plugin ships its
conventions with it and a downstream project overrides them locally.

---

## 7. Calibration

Two things here are derived from how the Blender exporter and the Unreal importer compose, and
should be **confirmed once on a throwaway asset before panels are authored in bulk**: §2's axis
mapping, and Interchange's socket handling (§5). Every later asset inherits whatever is actually
true, so the cost of not checking is a re-export of the entire panel set.

The calibration asset is a single flat panel with one edge socket, authored per §3, exported per
§4, imported per §5.

1. **Scale.** A `Straight_2000` edge measures **200.0 uu** in the static mesh editor.
2. **Axes.** In the socket manager, the gizmo's **red (+X) arrow points away from the panel, in
   its plane**; **blue (+Z)** is along the surface normal; green (+Y) runs along the edge.
3. **Round trip.** Two instances snap edge-to-edge into a coplanar 180° seam with no visible
   offset — milestone 1 in miniature.

If (2) fails, §2's **Blender** column needs correcting, not the Unreal column: the Unreal roles
come from engine convention and stay as they are.
