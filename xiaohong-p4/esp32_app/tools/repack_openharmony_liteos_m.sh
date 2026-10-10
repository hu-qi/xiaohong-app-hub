#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

TOOLCHAIN_PREFIX="${TOOLCHAIN_PREFIX:-riscv32-esp-elf-}"
AR="${TOOLCHAIN_PREFIX}ar"
RANLIB="${TOOLCHAIN_PREFIX}ranlib"
NM="${TOOLCHAIN_PREFIX}nm"

# Path recorded inside the .sha256 file, relative to the esp32_app dir (PROJECT_ROOT).
REL_LIB="components/openharmony_liteos/lib/libopenharmony_liteos_m.a"
DST_LIB="${PROJECT_ROOT}/${REL_LIB}"

check_lib()
{
    local lib="${1:-$DST_LIB}"

    if [ ! -f "$lib" ]; then
        echo "[ERR] lib not found: $lib"
        exit 1
    fi

    echo "[INFO] checking lib: $lib"

    local tmp
    tmp="$(mktemp)"
    "$NM" -g --defined-only "$lib" > "$tmp" || {
        echo "[ERR] nm failed"
        rm -f "$tmp"
        exit 1
    }

    local required_symbols=(
        LOS_KernelInit
        LOS_Start
        LOS_TaskCreate
        LOS_TaskDelay
        LOS_QueueCreate
        LOS_QueueWrite
        LOS_QueueRead
        LOS_SemCreate
        LOS_SemPost
        LOS_SemPend
        LOS_EventInit
        LOS_EventWrite
        LOS_EventRead
        LOS_MuxCreate
        LOS_MuxPend
        LOS_MuxPost
        memset_s
        memcpy_s
    )

    local missing=0

    for sym in "${required_symbols[@]}"; do
        if grep -Eq "[[:space:]]${sym}$" "$tmp"; then
            echo "[OK] $sym"
        else
            echo "[MISS] $sym"
            missing=1
        fi
    done

    rm -f "$tmp"

    if [ "$missing" -ne 0 ]; then
        echo "[ERR] required symbols missing"
        exit 1
    fi

    echo "[OK] lib symbol check passed"
}

if [ "${1:-}" = "--check" ]; then
    check_lib "${2:-$DST_LIB}"
    exit 0
fi

SRC_LIB="${1:-}"

if [ -z "$SRC_LIB" ]; then
    echo "Usage:"
    echo "  $0 --check [lib_path]"
    echo "  $0 <source_lib.a>"
    echo ""
    echo "Example:"
    echo "  $0 ~/OpenHarmony_master/out/xxx/libopenharmony_liteos_m.a"
    echo "  $0 --check"
    exit 1
fi

if [ ! -f "$SRC_LIB" ]; then
    echo "[ERR] source lib not found: $SRC_LIB"
    exit 1
fi

echo "[INFO] source lib: $SRC_LIB"
echo "[INFO] target lib: $DST_LIB"

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

cp -a "$SRC_LIB" "$TMP_DIR/source.a"

pushd "$TMP_DIR" >/dev/null

"$AR" x source.a

OBJ_COUNT="$(find . -maxdepth 1 -name "*.o" | wc -l)"

if [ "$OBJ_COUNT" -le 0 ]; then
    echo "[ERR] no object files extracted from source lib"
    exit 1
fi

echo "[INFO] extracted object count: $OBJ_COUNT"

"$AR" rcs libopenharmony_liteos_m.a ./*.o
"$RANLIB" libopenharmony_liteos_m.a

popd >/dev/null

mkdir -p "$(dirname "$DST_LIB")"

#if [ -f "$DST_LIB" ]; then
##   BACKUP="${DST_LIB}.bak.$(date +%Y%m%d_%H%M%S)"
#    BACKUP="${DST_LIB}.bak"
#    cp -a "$DST_LIB" "$BACKUP"
#    echo "[INFO] old lib backup: $BACKUP"
#fi

#if [ -f "${DST_LIB}.sha256" ]; then
#    SHA_BACKUP="${DST_LIB}.sha256.bak"
#    cp -a "${DST_LIB}.sha256" "$SHA_BACKUP"
#    echo "[INFO] old sha256 backup: $SHA_BACKUP"
#fi

cp -a "$TMP_DIR/libopenharmony_liteos_m.a" "$DST_LIB"

# Generate the checksum with a path relative to the esp32_app dir so the .sha256
# file stays valid across machines and checkout locations.
( cd "${PROJECT_ROOT}" && sha256sum "${REL_LIB}" ) > "${DST_LIB}.sha256"
echo "[INFO] sha256 written: ${DST_LIB}.sha256 (recorded path: ${REL_LIB})"

check_lib "$DST_LIB"

# The temp archive created by build_esp32p4.sh is no longer needed once
# libopenharmony_liteos_m.a has been generated; remove it by name so the lib/ dir
# stays clean. Standalone repacks of other archives are left untouched.
if [ "$(basename "${SRC_LIB}")" = "libkernel_liteos_m_rebuilt.a" ]; then
    rm -f "${SRC_LIB}"
    echo "[INFO] removed temp source lib: ${SRC_LIB}"
fi

echo "[OK] repack finished"
