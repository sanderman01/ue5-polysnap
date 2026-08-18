#!/usr/bin/env bash
# Run clang-format over the project's own C++ — never over Engine/ or Intermediate/.
#
#   Tools/format.sh                 rewrite every tracked source file in place
#   Tools/format.sh --check         report unformatted files, exit 1 if any (CI / pre-commit)
#   Tools/format.sh --staged        rewrite only the lines staged in git (git-clang-format)
#   Tools/format.sh <file>...       rewrite just those files
#
# See CODE_STYLE.md for the style itself; .clang-format is the machine-readable copy of it.

set -euo pipefail

ROOT=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$ROOT"

CLANG_FORMAT=${CLANG_FORMAT:-clang-format}
command -v "$CLANG_FORMAT" >/dev/null || { echo "error: $CLANG_FORMAT not on PATH" >&2; exit 127; }

# clang-format 21 turned ReflowComments into an enum; .clang-format uses the enum form.
MAJOR=$("$CLANG_FORMAT" --version | grep -oE '[0-9]+' | head -1)
if [[ $MAJOR -lt 21 ]]; then
	echo "error: clang-format $MAJOR is too old; .clang-format needs 21 or newer" >&2
	exit 1
fi

# Source/ and Plugins/ only. Intermediate/ holds UHT output and Engine/ is not ours.
collect_sources() {
	find Source Plugins -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' -o -name '*.inl' \) \
		-not -path '*/Intermediate/*' -not -path '*/Binaries/*' 2>/dev/null | sort
}

case ${1-} in
	--check)
		FAILED=0
		while IFS= read -r f; do
			[[ -n $f ]] || continue
			if ! "$CLANG_FORMAT" --style=file --dry-run --Werror "$f" 2>/dev/null; then
				echo "needs formatting: $f"
				FAILED=1
			fi
		done < <(collect_sources)
		[[ $FAILED -eq 0 ]] && echo "all sources are formatted"
		exit $FAILED
		;;
	--staged)
		exec git clang-format --style=file
		;;
	'')
		mapfile -t FILES < <(collect_sources)
		[[ ${#FILES[@]} -gt 0 ]] || { echo "no sources found under Source/ or Plugins/"; exit 0; }
		"$CLANG_FORMAT" --style=file -i "${FILES[@]}"
		echo "formatted ${#FILES[@]} file(s)"
		;;
	*)
		"$CLANG_FORMAT" --style=file -i "$@"
		echo "formatted $# file(s)"
		;;
esac
