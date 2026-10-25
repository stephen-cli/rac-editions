#!/usr/bin/env bash
# Host tests of the randomizer's logic (tests/randomizer_test.c against
# src/randomizer.c), with gcc: the machine's, or WSL's where only that has one.
#
#   bash tools/test_randomizer.sh [SEEDS]          # SEEDS layouts per pool, 2000 by default
#   bash tools/test_randomizer.sh SEED BOOTS [START]   # print one layout (BOOTS 0 or 1, START an RzStart)
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=$HERE/build/tests
mkdir -p "$OUT"
SRC=("$HERE/src/randomizer.c" "$HERE/tests/randomizer_test.c")
if command -v gcc >/dev/null 2>&1; then
  gcc -std=c99 -O2 -Wall -Wextra -I"$HERE/src" "${SRC[@]}" -o "$OUT/randomizer_test"
  "$OUT/randomizer_test" "$@"
elif command -v wsl >/dev/null 2>&1; then
  to_wsl() { MSYS_NO_PATHCONV=1 wsl -d Ubuntu -e wslpath -a "$(cygpath -w "$1")" | tr -d '\r'; }
  W_OUT=$(to_wsl "$OUT")
  W_SRC=()
  for s in "${SRC[@]}"; do W_SRC+=("$(to_wsl "$s")"); done
  MSYS_NO_PATHCONV=1 wsl -d Ubuntu -e gcc -std=c99 -O2 -Wall -Wextra -I"$(to_wsl "$HERE/src")" "${W_SRC[@]}" -o "$W_OUT/randomizer_test"
  MSYS_NO_PATHCONV=1 wsl -d Ubuntu -e "$W_OUT/randomizer_test" "$@"
else
  echo "no gcc (install one, or WSL with gcc)" >&2
  exit 2
fi
