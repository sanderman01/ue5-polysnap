---
name: clang-tools
description: Format C++ with clang-format and lint it with clang-tidy. Use before committing C++, when asked to format or tidy code, or when a clang-tidy run fails with PCH or __builtin_ia32 errors — the engine's compile database is not directly usable by clang-tidy and this skill explains the workaround.
---

# clang-format and clang-tidy

Two config files at the project root, two scripts in `Tools/`. The configs encode
[CODE_STYLE.md](../../../CODE_STYLE.md); read that first for the *why*.

| File | Purpose |
| --- | --- |
| `.clang-format` | Epic's style: tabs, Allman, `.generated.h` pinned last, UE macros on their own lines |
| `.clang-tidy` | Check list, trimmed to what catches bugs or enforces a documented project rule |
| `Tools/format.sh` | Runs clang-format over `Source/` and `Plugins/` only |
| `Tools/tidy.sh` | Builds the compile database clang-tidy needs, then runs it |

Needs clang-format **21+** and clang-tidy **19+**. Override with `CLANG_FORMAT=` / `CLANG_TIDY=`.

## Formatting

```sh
Tools/format.sh            # rewrite everything under Source/ and Plugins/
Tools/format.sh --check    # report unformatted files, exit 1 — use this in review
Tools/format.sh --staged   # only the lines staged in git
Tools/format.sh Source/Sandbox/Foo.cpp
```

**Never run clang-format over `Engine/`, `Intermediate/`, or `Content/`.** The scripts already
scope themselves; a bare `clang-format -i` in the project root does not.

## Linting

```sh
Tools/tidy.sh                    # every .cpp under Source/ and Plugins/
Tools/tidy.sh --fix              # apply confident fixes, reformatted via .clang-format
Tools/tidy.sh --strict           # non-zero exit on any diagnostic
Tools/tidy.sh --analyze          # add clang-analyzer-*; minutes per translation unit
Tools/tidy.sh --reuse-db         # skip the ~3s database regeneration
```

Runs one translation unit at a time. Once there are enough files for that to hurt, swap the
loop in `Tools/tidy.sh` for `run-clang-tidy -p "$DB_DIR" -j "$(nproc)"`.

## Why tidy.sh cannot use compile_commands.json

The database from the `clangd-database` skill is built for clangd, and clang-tidy chokes on
it twice:

- **`PCH file uses an older format that is no longer supported`** — the database compiles
  against the engine's shared PCH, and a `.gch` is only readable by the exact clang that
  wrote it. `Tools/tidy.sh` regenerates the database with `-NoPCH -NoSharedPCH` into
  `Intermediate/ClangTidy/`.
- **Hundreds of `use of undeclared identifier '__builtin_ia32_*'`** — UBT appends
  `-isystem <toolchain>/lib/clang/<N>/include`, the *engine* clang's builtin headers
  (clang 20 for UE 5.8). A newer clang-tidy frontend cannot compile them. The script strips
  that one flag so clang-tidy uses its own resource directory. clangd is immune because it
  injects its own `-resource-dir`; clang-tidy does not.

Both databases share the `.rsp` files under `Intermediate/Build`, so whichever tool ran
last wins. That is why `tidy.sh` regenerates by default — `--reuse-db` is the opt-out, not the norm.

Regenerating never invokes the compiler or linker, so it is safe with the editor open. It does
rewrite the `.rsp` files UBT uses for real builds, so the next `make PolySnapSandboxEditor`
recompiles the module once because the command line changed.

## Editing the check list

`.clang-tidy` carries a block of comments naming the checks that are deliberately *off* and
why. Before enabling anything, apply the three rules at the top of that file — in particular,
a check that fights Epic's coding standard is wrong, not the standard.

Identifier naming is **not** enforced by tooling. `llvm::Regex` cannot express Epic's prefixes,
because whether a type is `F`, `U`, `A`, or `I` depends on its base class, not its spelling.

## Editor integration

clangd picks up both files automatically. To see clang-tidy diagnostics inline, launch it with
`--clang-tidy`; per-machine `.clangd` files are gitignored here, so that is a local setting.
