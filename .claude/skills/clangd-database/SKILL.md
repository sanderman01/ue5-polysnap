---
name: clangd-database
description: Regenerate compile_commands.json so clangd can resolve includes and symbols. Use when clangd reports missing headers or unknown symbols in code that compiles fine, or after adding/removing/renaming a module, plugin, source file, or .Build.cs — new translation units never appear in the database on their own.
---

# Regenerating the clangd compile database

Run from the project root. The output is gitignored and machine-local.

```sh
ENGINE=$(sed -n 's/^UE_5\.8=//p' ~/.config/Epic/UnrealEngine/Install.ini)
"$ENGINE/Engine/Build/BatchFiles/Linux/Build.sh" -mode=GenerateClangDatabase \
  -project="$PWD/Construction.uproject" -OutputDir="$PWD" \
  ConstructionEditor Linux Development
```

Takes a few seconds and ends with `ClangDatabase written to <project>/compile_commands.json`.

- **`-OutputDir` is mandatory.** The mode defaults to the *engine* root and ignores
  `-project` when choosing where to write, so omitting it silently drops the file where
  clangd never looks — and still reports `Succeeded`. Delete any stray engine-root copy.
- **`-project` must be absolute**, hence `$PWD`; Build.sh `cd`s into the engine directory.
- **The editor may stay open.** This never invokes the compiler or linker, so it cannot
  touch the loaded module `.so`. CLAUDE.md's `pgrep` guard covers builds, not this.
- Use `ConstructionEditor` — it is a superset of `Construction`. For the runtime target,
  add `-OutputFilename=` so it does not clobber the editor database.

## Sanity checks

Compare the entry count against `find Source Plugins -name '*.cpp' | wc -l`, not against a
remembered number. Headers get no entries of their own; a header in a new module stays
unresolved until that module has a compiled `.cpp`.

The emitted flags force `-NoPCH` and disable unity builds, so they do **not** match what
`make ConstructionEditor` runs. Read the database as code intelligence only — never copy
flags out of it into a `.Build.cs`.

## Still wrong after regenerating

- **Errors only on `.generated.h` symbols** — UHT has not run. Build the editor target once, then regenerate.
- **After an engine upgrade** — `IncludeOrderVersion = Latest` moves engine headers between versions. Regenerate before trusting any clangd error.
- **A whole plugin missing** — check it is enabled in `Construction.uproject` and has a `.Build.cs`. UBT only emits what the target really compiles, so this is usually a build-config bug, not a database bug.
