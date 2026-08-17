---
name: automation-tests
description: Decide whether a change should get an automated test at all, and if so write and run it as an Unreal Automation Spec. Use before writing any test in this project, when asked to add test coverage, or when deciding how to verify a change — most of this project is verified by the user playing the level, and only pure algorithmic code is tested in code.
---

# Testing Construction

## What gets a test

Automation Spec tests **only for pure/algorithmic code** — logic that runs without a world:
math, grid/placement rules, socket-name parsing, serialization, data transforms.

Everything that needs a world, actors, or rendering is verified by **the user playing the
level**. That is the intended workflow, not a gap to fill.

- Do not write tests for world-dependent systems unless asked.
- Do not write ad-hoc throwaway test executables.
- If a change is world-dependent, say so and hand the user something concrete to check in the
  level instead of inventing coverage.

Signal that something is testable here: it takes values in and returns values out, and you can
call it from a free function without spawning anything.

## Where tests live

In the plugin's own `Tests/` folder, in a **module that is not shipped with the runtime
target** — so the test module is listed in `ConstructionEditor.Target.cs`'s build graph via the
plugin, never pulled into `Construction.Target.cs`.

A plugin's test module goes in its `.uplugin` with a type that keeps it out of shipping
(`"Type": "Editor"` or `"DeveloperTool"`) and `"LoadingPhase": "Default"`. The module's
`.Build.cs` depends on the plugin's runtime module plus the engine's automation support.

The rule from CLAUDE.md still binds: a plugin's tests may not reference anything in
`Source/Construction`. If a test only passes because the game module exists, that is a bug in
the plugin.

## Writing one

Use the Spec macros (`BEGIN_DEFINE_SPEC` / `END_DEFINE_SPEC`, `Describe` / `It`), not the older
`IMPLEMENT_SIMPLE_AUTOMATION_TEST` form — Specs give nested naming that reads well in the
Session Frontend and keeps one behaviour per `It`.

Flags for pure logic: `EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter`
— no `ApplicationContextMask` entry that implies a client or a world.

Name the spec after the behaviour under test, and keep each `It` to a single assertion's worth
of meaning. Test the documented contract ([CONVENTIONS.md](../../../CONVENTIONS.md) for socket
and unit rules), not the current implementation's incidental output.

## Running them

Build first (`unreal-build` skill) — the test module has to compile into the editor target.

Headless, from the project root:

```sh
/home/sander/UnrealEngine_5_8_1/Engine/Binaries/Linux/UnrealEditor-Cmd \
  "$PWD/Construction.uproject" \
  -ExecCmds="Automation RunTests <SpecPrefix>; Quit" \
  -unattended -nopause -nosplash -nullrhi -log
```

`<SpecPrefix>` is the spec's declared name, or any prefix of it — `Automation RunTests PolySnap`
runs the whole plugin's specs. The trailing `Quit` is an *Automation* subcommand (the handler
splits its own argument on `;`), so it is queued behind the run rather than quitting into it.

A filter matching nothing is a **failure**, not a pass — 5.8 logs `No automation tests matched
'<filter>'` as a command-line error unless `Automation AllowZeroTestResults` is passed. So a
clean exit does mean tests actually ran; still read the log for which ones, and report failing
assertions verbatim. `Automation List` dumps every registered name when a filter is wrong.

In-editor, the user can run the same specs from **Tools → Session Frontend → Automation**.
