#!/usr/bin/env bash
# Build core and machine-pages (once) and each selected mod (MOD=<name>, default all) into out/. A mod is staged
# with common/ beside it first (tools/stage_mod.py), so out/<mod>/stage is what the SDK compiles.
set -euo pipefail
. "$(dirname "$0")/env.sh"
ls "$OUT"/core/core-*.elemod >/dev/null 2>&1 || "$PY" -m elekloader.sdk.build "$CORE_DIR" --out "$OUT/core"
ls "$OUT"/machine-pages/machine-pages-*.elemod >/dev/null 2>&1 || "$PY" -m elekloader.sdk.build "$PAGES_DIR" --out "$OUT/machine-pages"
for m in $MODS; do
  stage="$("$PY" "$ROOT/tools/stage_mod.py" "$m" "$OUT/$m/stage")"
  rm -f "$OUT/$m"/*.elemod
  "$PY" -m elekloader.sdk.build "$stage" --out "$OUT/$m"
done
