#!/usr/bin/env bash
# Build core and machine-pages (again only when the stock OS or elekloader changed) and each selected mod (MOD=<name>,
# default all) into out/. A mod is staged with common/ beside it first (tools/stage_mod.py), so out/<mod>/stage is what
# the SDK compiles.
set -euo pipefail
. "$(dirname "$0")/env.sh"
# sha256 of stdin (sha256sum on Linux, shasum on macOS).
sha() { if command -v sha256sum >/dev/null 2>&1; then sha256sum; else shasum -a 256; fi | cut -d' ' -f1; }
# What a platform build depends on: the stock OS and the elekloader checkout (HEAD plus uncommitted tracked changes).
# Empty when elekloader is not a git checkout, which forces a rebuild every time.
platform_stamp() {
  local rev
  rev="$(git -C "$ELEKLOADER_DIR" rev-parse HEAD 2>/dev/null)" || return 0
  printf '%s %s %s\n' "$(sha <"$ELEKLOADER_STOCK")" "$rev" "$(git -C "$ELEKLOADER_DIR" diff HEAD | sha)"
}
STAMP="$(platform_stamp)"
# Build platform mod $2 from $1 unless out/$2 holds a build with the same stamp. Old elemods go first, so lint.sh and
# patch.sh (which take the first match) never pick a stale version.
build_platform() {
  local out="$OUT/$2"
  if [ -n "$STAMP" ] && [ "$(cat "$out/.stamp" 2>/dev/null)" = "$STAMP" ] && ls "$out/$2"-*.elemod >/dev/null 2>&1; then
    return 0
  fi
  rm -f "$out"/*.elemod "$out/.stamp"
  "$PY" -m elekloader.sdk.build "$1" --out "$out"
  [ -z "$STAMP" ] || printf '%s\n' "$STAMP" >"$out/.stamp"
}
build_platform "$CORE_DIR" core
build_platform "$PAGES_DIR" machine-pages
for m in $MODS; do
  stage="$("$PY" "$ROOT/tools/stage_mod.py" "$m" "$OUT/$m/stage")"
  rm -f "$OUT/$m"/*.elemod
  "$PY" -m elekloader.sdk.build "$stage" --out "$OUT/$m"
done
