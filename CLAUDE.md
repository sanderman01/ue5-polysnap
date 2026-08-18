# Construction — Project Guide

Supplements `~/AGENTS.md`. This file covers only what is specific to this project.
Where the two overlap, this file wins.

## Where things are written down

| Document | Covers |
| --- | --- |
| [README.md](README.md) | The design: what the game is, how the PolySnap node-based snapping system works, which questions are still open. Read it before doing design work. |
| [CONVENTIONS.md](CONVENTIONS.md) | The Blender↔Unreal contract: axes, units, socket basis, exact import/export settings. |
| [CODE_STYLE.md](CODE_STYLE.md) | How the C++ is written: Epic's standard, modern C++, lifetime rules, Blueprint API hygiene. Read it before writing code. |
| `unreal-build` skill | Building a target, the editor guard, regenerating project files. |
| `automation-tests` skill | What gets a test, where it lives, how to run it. |
| `clang-tools` skill | Running clang-format and clang-tidy, and why clang-tidy needs its own compile database. |

## What this project is

A first-person space building/sandbox project used as an **incubator for reusable plugin
modules**. The eventual product is not this project — it is the plugins developed here,
which will be dropped into a real game project later. Also a deliberate learning vehicle for
UE5 C++, so favour the idiomatic engine solution over the clever one, and explain the engine
machinery when it is non-obvious.

- Unreal Engine **5.8**, source build at `/home/sander/UnrealEngine_5_8_1`
- Linux, single runtime module `Construction`, First Person template as the test bed
- Plugins enabled: `ModelingToolsEditorMode` (editor-only), `GameplayStateTree`

## Architecture rules

**Ask before placing a new feature.** At the start of each new feature, ask whether it
belongs in a new plugin, an existing plugin, or the `Construction` game module. Do not
assume.

**Plugins have zero dependencies on the game module — hard rule.**
- A plugin may never `#include` or otherwise reference anything from `Source/Construction`,
  nor depend on Construction-specific content or config.
- Everything a plugin needs ships inside the plugin: its own `Content/`, its own settings
  (`UDeveloperSettings`), its own config.
- Cross-plugin dependencies must be declared explicitly in `Build.cs` and justified in the
  commit message. Prefer no dependency at all.
- The `Construction` game module is a thin harness: it glues plugins together and holds
  test-level scaffolding. Real logic does not live there.
- Test each plugin as if the game module did not exist. If it only works because of
  something in `Construction`, that is a bug.

**C++ for all logic; Blueprint for data and visuals only.** Blueprints configure assets,
materials, and cosmetic wiring. No gameplay logic in Blueprint graphs. Every system is a
C++ class with a deliberate Blueprint-facing surface.

Framework choices (Enhanced Input, GAS, StateTree, Mass, subsystems) are undecided — raise
them per feature rather than assuming a stack.

## Building and testing

Use the **`unreal-build`** skill for anything that compiles, and the **`automation-tests`**
skill before writing a test. Two rules that must hold even if you never open them:

- **Never kill the editor process.** A command-line build cannot overwrite a module `.so` the
  running editor has loaded, so check `pgrep -af "[U]nrealEditor .*Construction.uproject"`
  first and ask the user to close it.
- **Automated tests are for pure/algorithmic code only.** Anything needing a world, actors, or
  rendering is verified by the user playing the level — do not write tests for it unless asked.

## Code style

See [CODE_STYLE.md](CODE_STYLE.md) — read it before writing C++. Epic's coding standard is
non-negotiable and outranks personal taste; modern C++ is welcome where it does not conflict
with it, and never via STL containers in place of UE ones.

`.clang-format` and `.clang-tidy` at the project root are the machine-readable half of that
document. Run them with the **`clang-tools`** skill; `Tools/format.sh --check` is the quickest
way to confirm a change is committable.

## Assets and config

- **Never edit binary assets, unless specifically requested** (`.uasset`, `.umap`). When a change requires one, give the
  user precise in-editor steps and the exact values to set.
- `Config/Default*.ini` edits are fine (plugin enablement, input, module settings). Keep
  plugin-specific config inside the plugin.
- Do not commit `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`.
