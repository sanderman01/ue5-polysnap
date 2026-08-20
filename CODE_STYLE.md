# Code style

Companion to [README.md](README.md), [DESIGN.md](DESIGN.md) and
[CONVENTIONS.md](CONVENTIONS.md). The README describes *what* we are building and DESIGN.md *why*;
CONVENTIONS.md pins down the asset-pipeline conventions shared between Blender and Unreal; this
file governs the C++ we write.

Applies to every module in the project — plugins and the `Sandbox` game module alike.

---

## Enforced by tooling, not by memory

`.clang-format` and `.clang-tidy` at the project root encode as much of this document as a
machine can check: indentation, brace placement, include order (`.generated.h` last), const
correctness on parameters, `override`, and the `check()`-with-side-effects trap. Run them with
the `clang-tools` skill — `Tools/format.sh` and `Tools/tidy.sh`.

The rest of this file is the part tooling cannot check. Identifier prefixes in particular are
review's job: whether a type is `F`, `U`, `A`, or `I` depends on its base class, which no regex
can see.

---

## Epic's coding standard, strictly

This is non-negotiable and outranks personal taste:

- Prefixes: `F` structs, `U` UObjects, `A` Actors, `E` enums, `T` templates, `I` interfaces,
  `S` Slate widgets. PascalCase everywhere; `b` prefix for bools (`bIsPlacing`).
- Tabs for indentation, Allman braces — match the existing template files.
- IWYU includes; the `.generated.h` include is always last. No `using namespace`.
- One class per header where reasonable; keep implementation in `.cpp`.
- Copyright header on every new file: `// Copyright (c) 2026, Alexander Verbeek. All rights reserved.`
- Per-plugin `UE_LOG` category, declared in the plugin's module header.

## Modern C++ where the engine allows it

Never at the cost of Epic conventions, and never STL containers in place of UE ones
(`TArray`/`TMap`/`TSet`/`FString`, not `std::vector`/`std::string`):

- `const` correctness by default, including `const` member functions and `const TArray<T>&` params
- `TObjectPtr<T>` for UPROPERTY object members; raw pointers only for locals/params
- Ranged-for, structured bindings, `if`-with-initializer, `constexpr`, `[[nodiscard]]`
- `MoveTemp` (not `std::move`), `TOptional`, `TVariant`, `TFunction`
- `enum class` over plain enums; `static_assert` over comments about invariants

## Memory and lifetime, explicitly

- Every UObject reference held by a UObject is `UPROPERTY()` — no exceptions, or the GC eats it
- `TWeakObjectPtr<T>` for non-owning references that may outlive the target
- `TSharedPtr`/`TSharedRef`/`TUniquePtr` for non-UObject types; no raw `new`/`delete`
- `check()` for programmer errors that must never happen, `ensure()` for recoverable
  invariants worth a callstack, `verify()` when the expression must still run in shipping.
  Validate designer-supplied data with `IsValid()` and a log, not a crash.

## Blueprint-facing API hygiene

The exposed surface is the plugin's public contract:

- Deliberate specifiers: `EditDefaultsOnly` for class-level tuning, `EditAnywhere` only when
  per-instance override is genuinely wanted, `VisibleAnywhere` for read-only inspection
- Every `UPROPERTY`/`UFUNCTION` exposed to Blueprint gets a `Category` and a doc comment
- `BlueprintPure` only for genuinely side-effect-free, cheap functions
- `MODULE_API` export only on types Blueprints or other modules actually need; keep the rest
  internal
- `BlueprintCallable` is an API commitment — renaming it later breaks user Blueprints
  silently. Name things carefully the first time.

## Includes and engine-header order

Both targets set `IncludeOrderVersion = EngineIncludeOrderVersion.Latest`, which enforces the
IWYU rule above at compile time: engine headers no longer backfill transitive includes that
newer UE removed. When that surfaces an include error, **add the missing `#include`** — do not
relax the setting. The reasoning and the engine-upgrade consequence are in the `unreal-build`
skill.
