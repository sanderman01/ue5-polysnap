---
name: unreal-build
description: Compile the PolySnapSandbox editor or runtime target, and regenerate project files after a module or plugin changes. Use before reporting any C++ change as working, when a build fails and the UBT output needs reading, or after adding, removing, or renaming a module, plugin, or .Build.cs — and read the editor guard first, because a command-line build cannot overwrite a module .so the running editor has loaded.
---

# Building PolySnapSandbox

## First: is the editor running?

The editor is often open. **Before any command-line build:**

```sh
pgrep -af "[U]nrealEditor .*PolySnapSandbox.uproject"
```

The bracket around `U` stops the check from matching its own shell command line — without it
the check always reports a false positive. Exit code 1 and no output means nothing is running.

If the editor is running, **stop and ask the user to close it**. The build cannot overwrite the
loaded module `.so`. **Never kill the editor process.**

This guard is for builds only. Generating a clang database (`clangd-database` skill) invokes
neither compiler nor linker and may run with the editor open.

## Building

With the editor closed, from the project root:

```sh
make PolySnapSandboxEditor          # PolySnapSandboxEditor-Linux-Development
```

or directly:

```sh
/home/sander/UnrealEngine_5_8_1/Engine/Build/BatchFiles/Linux/Build.sh \
  PolySnapSandboxEditor Linux Development \
  -Project="/home/sander/dev/unreal/PolySnap/PolySnapSandbox.uproject"
```

`PolySnapSandboxEditor` is the target to build by default — it is a superset of the `PolySnapSandbox`
runtime target and is what the editor loads.

## What counts as verified

A build is not "done" until it compiles clean. **`Target is up to date` means nothing was
compiled — that is not a verification.** If UBT skipped everything, either the change did not
land or the wrong target was built; find out which before reporting success.

Report the actual UBT output on failure, do not paraphrase it.

Launching the editor and playing the level is the user's job — offer the build result, not a
claim about in-game behaviour.

## After adding, removing, or renaming a module or plugin

Regenerate project files:

```sh
/home/sander/UnrealEngine_5_8_1/Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh \
  -project="/home/sander/dev/unreal/PolySnap/PolySnapSandbox.uproject" -game -engine
```

Then regenerate the clang database too (`clangd-database` skill) — new translation units never
appear in it on their own.

## Include-order errors

Both targets set `IncludeOrderVersion = EngineIncludeOrderVersion.Latest`. Epic's template
leaves this field unset, which silently resolves to `Oldest` (= `Unreal5_6` in 5.8) and makes
engine headers backfill transitive includes that newer UE removed.

Strict order is deliberate here: it enforces the IWYU rule ([CODE_STYLE.md](../../../CODE_STYLE.md))
and stops a plugin from compiling only because of includes a target project may not provide.
**Do not relax it to silence an include error — add the missing `#include` instead.**

`Latest` auto-advances on engine upgrade, so an engine integration may surface a wave of new
include errors. That is the intended tradeoff; fix the includes.
