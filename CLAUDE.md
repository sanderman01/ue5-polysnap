# Construction — Project Guide

Supplements `~/AGENTS.md`. This file covers only what is specific to this project.
Where the two overlap, this file wins.

## What this project is

See [README.md](README.md) for the design: what the game is, how the PolySnap node-based
snapping system works, and which questions are still open. Read it before doing design work.


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

## Building

The editor is often open. **Before any command-line build, check for a running editor:**

```sh
pgrep -af "[U]nrealEditor .*Construction.uproject"
```

The bracket around `U` stops the check from matching its own shell command line — without it
the check always reports a false positive. Exit code 1 and no output means nothing is running.

If the editor is running, stop and ask the user to close it — the build cannot overwrite the
loaded module `.so`. **Never kill the editor process.**

With the editor closed:

```sh
# from the project root
make ConstructionEditor          # ConstructionEditor-Linux-Development
```

or directly:

```sh
/home/sander/UnrealEngine_5_8_1/Engine/Build/BatchFiles/Linux/Build.sh \
  ConstructionEditor Linux Development \
  -Project="/home/sander/dev/unreal/Construction/Construction.uproject"
```

After adding, removing, or renaming a module or plugin, regenerate project files:

```sh
/home/sander/UnrealEngine_5_8_1/Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh \
  -project="/home/sander/dev/unreal/Construction/Construction.uproject" -game -engine
```

A build is not "done" until it compiles clean. `Target is up to date` means nothing was
compiled — that is not a verification. Report the actual UBT output on failure, do not
paraphrase it. Launching the editor and playing the level is the user's job.

Both targets set `IncludeOrderVersion = EngineIncludeOrderVersion.Latest`. Epic's template
leaves this field unset, which silently resolves to `Oldest` (= `Unreal5_6` in 5.8) and makes
engine headers backfill transitive includes that newer UE removed. Strict order is deliberate
here: it enforces the IWYU rule and stops a plugin from compiling only because of includes a
target project may not provide. Do not relax it to silence an include error — add the missing
`#include` instead.

Note that `Latest` auto-advances on engine upgrade, so an engine integration may surface new
include errors. That is the intended tradeoff; fix the includes.

## Testing

Automation Spec tests **only for pure/algorithmic code** — logic that runs without a world
(math, grid/placement rules, serialization, data transforms). Put them in the plugin's own
`Tests/` folder in a module that is not shipped with the runtime target. Everything that
needs a world, actors, or rendering is verified by the user playing the level.

Do not write tests for world-dependent systems unless asked. Do not write ad-hoc throwaway
test executables.

## Code style

**Epic's coding standard, strictly.** This is non-negotiable and outranks personal taste:

- Prefixes: `F` structs, `U` UObjects, `A` Actors, `E` enums, `T` templates, `I` interfaces,
  `S` Slate widgets. PascalCase everywhere; `b` prefix for bools (`bIsPlacing`).
- Tabs for indentation, Allman braces — match the existing template files.
- IWYU includes; the `.generated.h` include is always last. No `using namespace`.
- One class per header where reasonable; keep implementation in `.cpp`.
- Copyright header on every new file: `// Copyright (c) 2026, Alexander Verbeek. All rights reserved.`
- Per-plugin `UE_LOG` category, declared in the plugin's module header.

**Modern C++ where the engine allows it** — but never at the cost of Epic conventions, and
never STL containers in place of UE ones (`TArray`/`TMap`/`TSet`/`FString`, not
`std::vector`/`std::string`):

- `const` correctness by default, including `const` member functions and `const TArray<T>&` params
- `TObjectPtr<T>` for UPROPERTY object members; raw pointers only for locals/params
- Ranged-for, structured bindings, `if`-with-initializer, `constexpr`, `[[nodiscard]]`
- `MoveTemp` (not `std::move`), `TOptional`, `TVariant`, `TFunction`
- `enum class` over plain enums; `static_assert` over comments about invariants

**Memory and lifetime, explicitly:**

- Every UObject reference held by a UObject is `UPROPERTY()` — no exceptions, or the GC eats it
- `TWeakObjectPtr<T>` for non-owning references that may outlive the target
- `TSharedPtr`/`TSharedRef`/`TUniquePtr` for non-UObject types; no raw `new`/`delete`
- `check()` for programmer errors that must never happen, `ensure()` for recoverable
  invariants worth a callstack, `verify()` when the expression must still run in shipping.
  Validate designer-supplied data with `IsValid()` and a log, not a crash.

**Blueprint-facing API hygiene** — the exposed surface is the plugin's public contract:

- Deliberate specifiers: `EditDefaultsOnly` for class-level tuning, `EditAnywhere` only when
  per-instance override is genuinely wanted, `VisibleAnywhere` for read-only inspection
- Every `UPROPERTY`/`UFUNCTION` exposed to Blueprint gets a `Category` and a doc comment
- `BlueprintPure` only for genuinely side-effect-free, cheap functions
- `MODULE_API` export only on types Blueprints or other modules actually need; keep the rest
  internal
- `BlueprintCallable` is an API commitment — renaming it later breaks user Blueprints
  silently. Name things carefully the first time.

## Assets and config

- **Never edit binary assets** (`.uasset`, `.umap`). When a change requires one, give the
  user precise in-editor steps and the exact values to set.
- `Config/Default*.ini` edits are fine (plugin enablement, input, module settings). Keep
  plugin-specific config inside the plugin.
- Do not commit `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`.
