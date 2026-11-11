#!/usr/bin/env bash
# Definition of done: build, lint, patch, unit tests. MOD=<name> limits it to one mod; default is every mod.
# The main pass uses ELEKLOADER_STOCK (default OS 1.53). If ELEKLOADER_STOCK_154 names an existing 1.54 stock, build, lint
# and patch run a second time against it into out/154 (a separate output root, so the 1.53 cache is untouched).
set -euo pipefail
d="$(dirname "$0")"
"$d/build.sh"; "$d/lint.sh"; "$d/patch.sh"; "$d/test.sh"
if [ -z "${ELEKLOADER_STOCK_154:-}" ]; then
  echo "NOT CHECKED: the OS 1.54 variant (set ELEKLOADER_STOCK_154 to a 1.54 stock file)" >&2
elif [ ! -f "$ELEKLOADER_STOCK_154" ]; then
  echo "ELEKLOADER_STOCK_154 not found: $ELEKLOADER_STOCK_154" >&2; exit 2
else
  root154="$(cd "$d/.." && pwd)/out/154"
  echo "== OS 1.54 pass: $ELEKLOADER_STOCK_154 -> $root154"
  export BUILD_OUT="$root154" ELEKLOADER_STOCK="$ELEKLOADER_STOCK_154"
  "$d/build.sh"; "$d/lint.sh"; "$d/patch.sh"
fi
