#!/usr/bin/env bash
# Run clang-tidy over the project's own C++ using a compile database built for it.
#
#   Tools/tidy.sh                   analyse every .cpp under Source/ and Plugins/
#   Tools/tidy.sh <file>...         analyse just those translation units
#   Tools/tidy.sh --fix             apply the fixes clang-tidy is confident about
#   Tools/tidy.sh --strict          exit non-zero on any diagnostic (CI)
#   Tools/tidy.sh --analyze         also run clang-analyzer-* (minutes per file)
#   Tools/tidy.sh --reuse-db        skip regenerating the compile database
#
# The checks live in .clang-tidy. Two things this script exists to handle:
#
#  1. The clangd database (compile_commands.json) compiles against the engine's shared
#     precompiled header. A PCH is only readable by the exact clang that wrote it, so
#     clang-tidy dies with "PCH file uses an older format". We regenerate the database with
#     -NoPCH -NoSharedPCH into Intermediate/ClangTidy/ instead.
#
#  2. UBT appends `-isystem <toolchain>/lib/clang/<N>/include` — the *engine* clang's
#     builtin headers. Feeding clang-20 intrinsic headers to a clang-22 frontend produces
#     hundreds of bogus "use of undeclared identifier '__builtin_ia32_*'" errors. Strip it
#     and clang-tidy uses its own matching resource directory.
#
# Regenerating the database rewrites the .rsp files under Intermediate/Build, which UBT
# also uses for real builds; the next `make ConstructionEditor` rebuilds the module once
# because the command line changed. It never touches a loaded module .so, so this is safe
# with the editor open (unlike a build — see the `unreal-build` skill).

set -euo pipefail

ROOT=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$ROOT"

PROJECT=$ROOT/Construction.uproject
TARGET=ConstructionEditor
DB_DIR=$ROOT/Intermediate/ClangTidy

CLANG_TIDY=${CLANG_TIDY:-clang-tidy}
command -v "$CLANG_TIDY" >/dev/null || { echo "error: $CLANG_TIDY not on PATH" >&2; exit 127; }

ENGINE=${UE_ROOT:-$(sed -n 's/^UE_5\.8=//p' "$HOME/.config/Epic/UnrealEngine/Install.ini" 2>/dev/null)}
[[ -x "$ENGINE/Engine/Build/BatchFiles/Linux/Build.sh" ]] ||
	{ echo "error: cannot find the engine; set UE_ROOT" >&2; exit 1; }

FIX=() ; STRICT=0 ; REUSE=0 ; EXTRA_CHECKS='' ; FILES=()
for arg in "$@"; do
	case $arg in
		--fix)       FIX=(--fix --fix-errors) ;;
		--strict)    STRICT=1 ;;
		--reuse-db)  REUSE=1 ;;
		--analyze)   EXTRA_CHECKS=',clang-analyzer-*' ;;
		-*)          echo "error: unknown option $arg" >&2; exit 2 ;;
		*)           FILES+=("$(realpath "$arg")") ;;
	esac
done

# --- 1. compile database --------------------------------------------------------------
if [[ $REUSE -eq 0 || ! -f $DB_DIR/compile_commands.json ]]; then
	mkdir -p "$DB_DIR"
	echo "==> generating PCH-free compile database"
	"$ENGINE/Engine/Build/BatchFiles/Linux/Build.sh" -mode=GenerateClangDatabase \
		-project="$PROJECT" -OutputDir="$DB_DIR" -NoPCH -NoSharedPCH \
		"$TARGET" Linux Development >/dev/null

	python3 - "$DB_DIR/compile_commands.json" <<'PY'
import json, re, sys
path = sys.argv[1]
db = json.load(open(path))
strip = re.compile(r'\s-isystem\s+\S*/lib/clang/\d+/include\b')
for entry in db:
    entry['command'] = strip.sub('', entry['command'])
json.dump(db, open(path, 'w'), indent=1)
print(f'    {len(db)} translation units')
PY
fi

# --- 2. what to analyse ---------------------------------------------------------------
if [[ ${#FILES[@]} -eq 0 ]]; then
	mapfile -t FILES < <(find "$ROOT/Source" "$ROOT/Plugins" -type f -name '*.cpp' \
		-not -path '*/Intermediate/*' -not -path '*/Binaries/*' 2>/dev/null | sort)
fi
[[ ${#FILES[@]} -gt 0 ]] || { echo "no .cpp found under Source/ or Plugins/"; exit 0; }

# --- 3. run ---------------------------------------------------------------------------
# --header-filter is absolute so it means "our headers", not "any path containing /Source/"
# — the engine lives in Engine/Source and would otherwise match.
ARGS=(
	-p "$DB_DIR"
	--quiet
	--header-filter="^$ROOT/(Source|Plugins)/.*"
	--extra-arg=-Wno-error          # UBT compiles with -Werror; do not fail on engine warnings
	--extra-arg=-Wno-unknown-warning-option
)
[[ -n $EXTRA_CHECKS ]] && ARGS+=(--checks="$EXTRA_CHECKS")

echo "==> clang-tidy over ${#FILES[@]} translation unit(s)"
STATUS=0
for f in "${FILES[@]}"; do
	"$CLANG_TIDY" "${ARGS[@]}" "${FIX[@]}" "$f" || STATUS=1
done

if [[ $STATUS -ne 0 && $STRICT -eq 0 ]]; then
	echo "==> diagnostics above (advisory; --strict makes them fatal)"
	STATUS=0
fi
exit $STATUS
