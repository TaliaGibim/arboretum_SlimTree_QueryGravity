#!/usr/bin/env bash
#
# build_ch4.sh - builds (and optionally runs) a Chapter 4 driver.
#
# Usage:
#   bash arboretum_SlimTree_QueryGravity/ch4/build_ch4.sh <driver> [-- <driver args>]
#
#   <driver>  a file name under this ch4/ directory, with or without .cpp:
#               phase1_check   fat factor + structural metrics   (trace OFF)
#               phase3_check   traversal trace invariants        (trace ON)
#               phase4_check   Etapa 1 relocation invariants     (trace ON)
#               chapter4       the section 4.5 experiment driver (trace ON)
#
# Whether the trace is compiled in is decided PER DRIVER below, not by the
# caller, so a driver can never be measured in the wrong configuration by
# accident.
#
# Examples:
#   bash arboretum_SlimTree_QueryGravity/ch4/build_ch4.sh phase1_check
#   bash arboretum_SlimTree_QueryGravity/ch4/build_ch4.sh phase3_check -- --queries 100 --k 20
#   NO_RUN=1 bash arboretum_SlimTree_QueryGravity/ch4/build_ch4.sh chapter4
#
# Environment:
#   NO_RUN=1    build only, do not run.
#   FULL_LIB=1  rebuild libarboretum.a from scratch. Off by default: no library
#               .cpp includes stSlimTree.h, so header-only changes need just the
#               driver relinked.
#   OPT_FLAGS   override -O2.
#
set -euo pipefail

CH4_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "${CH4_DIR}/.." && pwd)"
ROOT="$(cd "${REPO}/.." && pwd)"
ARB="${REPO}/arboretum"
DATASTORE="${ARB}/test-brcities-datastore"
# build.sh and compat.h still live in the Chapter 3 harness; Chapter 4 reuses
# them so that both chapters are compiled with provably identical flags.
BENCH_DIR="${ROOT}/benchmark"
OUT_DIR="${CH4_DIR}/build"

if [ "$#" -lt 1 ]; then
    sed -n '2,30p' "$0"
    exit 2
fi

DRIVER="${1%.cpp}"
shift
# Everything after a bare -- is forwarded to the binary.
DRIVER_ARGS=()
if [ "$#" -gt 0 ]; then
    if [ "$1" = "--" ]; then shift; fi
    DRIVER_ARGS=("$@")
fi

SRC="${CH4_DIR}/${DRIVER}.cpp"
[ -f "$SRC" ] || { echo "ERROR: no such driver: $SRC" >&2; exit 1; }

# ---------------------------------------------------------------------------
# Toolchain. MSYS2 UCRT64 supplies g++; its usr/bin supplies make. ucrt64/bin
# must come FIRST (compiler and the runtime DLLs the binary needs at run time)
# and usr/bin LAST, so that MSYS2 coreutils do not shadow the ones this shell
# already has - notably mktemp, which fails under MSYS2 for the same reason
# build.sh has to pass TMP to make explicitly.
# ---------------------------------------------------------------------------
if ! command -v g++ >/dev/null 2>&1 || ! command -v make >/dev/null 2>&1; then
    export PATH="/c/msys64/ucrt64/bin:${PATH}:/c/msys64/usr/bin"
fi
command -v g++ >/dev/null 2>&1 || {
    echo "ERROR: g++ not found. Install MSYS2 UCRT64 or put it on PATH." >&2
    exit 1
}

# ---------------------------------------------------------------------------
# Trace configuration, per driver.
# ---------------------------------------------------------------------------
case "$DRIVER" in
    phase1_check)            TRACE=0 ;;
    phase3_check|phase4_check|chapter4) TRACE=1 ;;
    *)                       TRACE=1 ;;
esac

export EXTRA_DEFINES="-DST_SLIM_TRACE=${TRACE}"
export BENCH_SRC="$SRC"
if [ "${FULL_LIB:-0}" != "1" ] && [ -f "${ARB}/build/libarboretum.a" ]; then
    export SKIP_LIB=1
fi

mkdir -p "$OUT_DIR"
BIN="${OUT_DIR}/${DRIVER}.exe"

echo "=== Chapter 4 driver: ${DRIVER} (ST_SLIM_TRACE=${TRACE}) ==="
bash "${BENCH_DIR}/scripts/build.sh" "$REPO" "$BIN"

if [ "${NO_RUN:-0}" = "1" ]; then
    echo "built ${BIN} (NO_RUN=1, not running)"
    exit 0
fi

# ---------------------------------------------------------------------------
# Run with the defaults each checker expects. Explicit args override these,
# because they come later on the command line.
# ---------------------------------------------------------------------------
case "$DRIVER" in
    phase1_check)
        set -- --dataset "${DATASTORE}/BrazilianCities.txt" \
               --index-dir "$OUT_DIR"
        ;;
    phase3_check|phase4_check)
        set -- --dataset "${DATASTORE}/BrazilianCities.txt" \
               --queryfile "${DATASTORE}/BrazilianCities500.txt" \
               --index-dir "$OUT_DIR"
        ;;
    *)
        set -- --dataset "${DATASTORE}/BrazilianCities.txt" \
               --queryfile "${DATASTORE}/BrazilianCities500.txt" \
               --index-dir "$OUT_DIR"
        ;;
esac

echo
"$BIN" "$@" "${DRIVER_ARGS[@]+"${DRIVER_ARGS[@]}"}"
