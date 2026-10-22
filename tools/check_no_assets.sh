#!/bin/sh
# Fails if any Fallout game data is tracked in git (or, with a path argument,
# present in a release folder). The game's files are copyrighted: players
# bring their own copy.
#
#   tools/check_no_assets.sh            # check the repository
#   tools/check_no_assets.sh dist/v1.0  # check a release folder
set -eu

PATTERN='\.(dat|DAT|frm|FRM|fr[0-5]|FR[0-5]|acm|ACM|mve|MVE|pro|PRO|map|MAP|int|INT|msg|MSG|pal|PAL|aaf|AAF|lst|LST|sav|SAV|gam|GAM)$'

if [ $# -gt 0 ]; then
    files=$(cd "$1" && find . -type f | sed 's|^\./||')
    where="$1"
else
    files=$(git ls-files)
    where="the repository"
fi

# Source files that merely share an extension (e.g. CMake .map outputs) can
# be allowed here.
bad=$(printf '%s\n' "$files" | grep -E "$PATTERN" | grep -v -E '^(third_party|cmake|os)/' || true)

# Whole game folders.
bad="$bad
$(printf '%s\n' "$files" | grep -i -E '(^|/)(data|movies|sound)/' | grep -v -E '^(src|third_party|cmake|tools|docs)/' || true)"

bad=$(printf '%s\n' "$bad" | sed '/^$/d' | sort -u)
if [ -n "$bad" ]; then
    echo "Game data found in $where - remove it before publishing:" >&2
    printf '  %s\n' $bad >&2
    exit 1
fi
echo "OK: no game data in $where"
