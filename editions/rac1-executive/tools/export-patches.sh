#!/usr/bin/env bash
# Refresh patches/ from OpenRAC's executive-edition branch: everything the
# branch adds to master, as one diff against their merge base. One diff rather
# than per-commit format-patch output, because the branch has merged master
# since (OpenRAC moved games/rac1/pal to games/rac1), and the early commits'
# paths no longer exist.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
OPENRAC=${OPENRAC:-$(cd "$HERE/../../../OpenRAC" && pwd)}
BRANCH=${BRANCH:-executive-edition}
base=$(git -C "$OPENRAC" merge-base master "$BRANCH")
rm -f "$HERE"/patches/*.patch
git -C "$OPENRAC" diff --binary "$base" "$BRANCH" > "$HERE/patches/openrac-executive-edition.patch"
{
  echo "# Patches against OpenRAC"
  echo
  echo "Base: OpenRAC master commit $base"
  echo "Branch: $BRANCH"
  echo
  echo "What the edition adds to OpenRAC, as one diff against the base:"
  echo
  git -C "$OPENRAC" diff --stat "$base" "$BRANCH" | sed 's/^/    /'
  echo
  echo "Apply onto an OpenRAC checkout of master (the base or later) with:"
  echo
  echo '    git -C OpenRAC apply ../rac1-executive-edition/editions/rac1-executive/patches/openrac-executive-edition.patch'
} > "$HERE/patches/README.md"
cat "$HERE/patches/README.md"
