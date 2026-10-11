#!/usr/bin/env bash
# First-gen WS63 / OpenHarmony 6.1 build handoff; no flash or cloud changes.
set -euo pipefail
MODE="${1:---check}"
ARG="${2:-}"
case "$MODE" in
  --help) echo "Usage: bash scripts/ws63-build-handoff.sh [--check|--stage|--build] /ABS/OHROOT"; exit 0 ;;
  --check|--stage|--build) ;;
  *) echo "Invalid mode" >&2; exit 2 ;;
esac
if [[ "$ARG" != /* || ! -d "$ARG" ]]; then
  echo "ERROR: supply an existing absolute OpenHarmony root" >&2; exit 2
fi
OH="$(cd "$ARG" && pwd -P)"
BASE="$(cd "$(dirname "$0")/.." && pwd -P)"
LIST="$BASE/scripts/ws63-overlay-manifest.txt"
VENDOR="$OH/vendor/atomgit/xiaohong"
[[ "$OH" != "$BASE" && -d "$OH/.repo" && -f "$VENDOR/xiaohong/BUILD.gn" ]] || {
  echo "ERROR: invalid first-gen OpenHarmony xiaohong workspace" >&2; exit 2;
}
command -v git >/dev/null || exit 2
SHA="$(git -C "$BASE" rev-parse HEAD)"
if [[ -n "${EXPECTED_SHA:-}" && "$SHA" != "$EXPECTED_SHA" ]]; then
  echo "ERROR: checkout differs from EXPECTED_SHA: $SHA" >&2; exit 2
fi
if [[ "$MODE" != "--check" ]] && {
  ! git -C "$BASE" diff --quiet || ! git -C "$BASE" diff --cached --quiet; }; then
  echo "ERROR: source checkout contains uncommitted modifications" >&2; exit 2
fi
CFG="$OH/device/soc/hisilicon/ws63v100/sdkv106/build/config/target_config/ws63/menuconfig/acore/ws63_liteos_app.config"
if [[ -f "$CFG" ]] && grep -q '^CONFIG_MIDDLEWARE_SUPPORT_LFS=y' "$CFG"; then
  echo "ERROR: first-gen xiaohong must disable SDK internal LFS adapter" >&2
  exit 2
fi
FILES=()
MATCH=0
while IFS= read -r item || [[ -n "$item" ]]; do
  [[ -z "$item" || "$item" == \#* ]] && continue
  case "$item" in /*|*..*|*//*|*[!a-zA-Z0-9_./-]*) echo "Unsafe manifest item: $item" >&2; exit 2;; esac
  [[ -f "$BASE/xiaohong/xiaohong/$item" ]] || { echo "Missing source: $item" >&2; exit 2; }
  FILES+=("$item")
  if [[ -f "$VENDOR/$item" ]] && cmp -s "$BASE/xiaohong/xiaohong/$item" "$VENDOR/$item"; then MATCH=$((MATCH+1)); fi
done < "$LIST"
[[ "${#FILES[@]}" -gt 15 ]] || { echo "Incomplete overlay manifest" >&2; exit 2; }
echo "PR SHA=$SHA; OH root=$OH; manifest files=${#FILES[@]}; matched=$MATCH"
if [[ "$MODE" == "--check" ]]; then
  echo "READ-ONLY: workspace and source validated. Nothing was modified."
  exit 0
fi
EVIDENCE="$OH/out/xiaohong/pr2-handoff/$SHA"
BACKUP="$EVIDENCE/backup"
MARKER="$EVIDENCE/staged.ok"
if [[ "$MODE" == "--stage" ]]; then
  [[ ! -e "$MARKER" ]] || { echo "Already staged: refusing to overwrite original backups" >&2; exit 2; }
  mkdir -p "$BACKUP"
  for item in "${FILES[@]}"; do
    mkdir -p "$BACKUP/$(dirname "$item")"
    if [[ -e "$VENDOR/$item" ]]; then
      cp -p "$VENDOR/$item" "$BACKUP/$item"
    else
      echo "$item" >> "$EVIDENCE/missing-before-stage.txt"
    fi
  done
  for item in "${FILES[@]}"; do
    mkdir -p "$(dirname "$VENDOR/$item")"
    cp -p "$BASE/xiaohong/xiaohong/$item" "$VENDOR/$item"
  done
  printf '%s\n' "$SHA" > "$MARKER"
  echo "STAGED: backups at $BACKUP. No build or flashing."
  exit 0
fi
[[ -f "$MARKER" && "$(cat "$MARKER")" == "$SHA" ]] || {
  echo "ERROR: first stage exactly this source commit using --stage" >&2; exit 2;
}
for item in "${FILES[@]}"; do
  cmp -s "$BASE/xiaohong/xiaohong/$item" "$VENDOR/$item" || {
    echo "ERROR: source-to-SDK drift: $item" >&2; exit 2;
  }
done
command -v hb >/dev/null || { echo "ERROR: hb not on PATH" >&2; exit 2; }
FIRMWARE="$OH/out/xiaohong/xiaohong/ws63-liteos-app/ws63-liteos-app_all.fwpkg"
touch "$EVIDENCE/build-start"
(cd "$OH" && hb set xiaohong && hb build -f) 2>&1 | tee "$EVIDENCE/hb-build.log"
[[ -s "$FIRMWARE" && "$FIRMWARE" -nt "$EVIDENCE/build-start" ]] || {
  echo "ERROR: fresh ws63-liteos-app_all.fwpkg not generated" >&2; exit 1;
}
if command -v sha256sum >/dev/null; then
  (cd "$(dirname "$FIRMWARE")" && sha256sum "$(basename "$FIRMWARE")") > "$EVIDENCE/firmware.sha256"
else
  (cd "$(dirname "$FIRMWARE")" && shasum -a 256 "$(basename "$FIRMWARE")") > "$EVIDENCE/firmware.sha256"
fi
printf 'source_sha=%s\nfirmware=%s\n' "$SHA" "$FIRMWARE" > "$EVIDENCE/provenance.txt"
echo "BUILD COMPLETE: $FIRMWARE"
echo "Logs and hashes: $EVIDENCE; no flash performed."
