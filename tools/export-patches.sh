#!/usr/bin/env bash
# Refresh patches/ from OpenRAC's executive-edition branch: every commit
# since it left master, as git format-patch output, plus the base commit.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
OPENRAC=${OPENRAC:-$(cd "$HERE/../OpenRAC" && pwd)}
BRANCH=${BRANCH:-executive-edition}
base=$(git -C "$OPENRAC" merge-base master "$BRANCH")
rm -f "$HERE"/patches/*.patch
git -C "$OPENRAC" format-patch -q -o "$HERE/patches" "$base..$BRANCH"
{
  echo "# Patches against OpenRAC"
  echo
  echo "Base: OpenRAC master commit $base"
  echo "Branch: $BRANCH"
  echo
  echo "Apply onto a fresh OpenRAC checkout at the base commit with:"
  echo
  echo '    git -C OpenRAC switch -c executive-edition && git -C OpenRAC am ../rac1-executive-edition/patches/*.patch'
  echo
  ls "$HERE"/patches/*.patch 2>/dev/null | xargs -n1 basename | sed 's/^/- /'
} > "$HERE/patches/README.md"
cat "$HERE/patches/README.md"
