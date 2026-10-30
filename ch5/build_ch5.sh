#!/usr/bin/env bash
#
# build_ch5.sh - builds (and optionally runs) a Chapter 5 driver.
#
# Usage:
#   bash arboretum_SlimTree_QueryGravity/ch5/build_ch5.sh <driver> [-- <driver args>]
#
#   <driver>  a file name under this ch5/ directory, with or without .cpp:
#               qp_oracle   phase F0 pilot: how much would query pivots prune?
#
# Same flags, same compiler and same libarboretum.a as Chapter 4: this reuses
# benchmark/scripts/build.sh exactly like ch4/build_ch4.sh does. The pilot does
# not change the page layout, so the existing library is reused as is.
#
# Environment:
#   NO_RUN=1    build only, do not run.
#   FULL_LIB=1  rebuild libarboretum.a from scratch.
#   OPT_FLAGS   override -O2.
#
set -euo pipefail

CH5_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "${CH5_DIR}/.." && pwd)"
ROOT="$(cd "${REPO}/.." && pwd)"
ARB="${REPO}/arboretum"
DATASTORE="${ARB}/test-brcities-datastore"
BENCH_DIR="${ROOT}/benchmark"
OUT_DIR="${CH5_DIR}/build"

if [ "$#" -lt 1 ]; then
    sed -n '2,20p' "$0"
    exit 2
fi

DRIVER="${1%.cpp}"
shift
DRIVER_ARGS=()
if [ "$#" -gt 0 ]; then
    if [ "$1" = "--" ]; then shift; fi
    DRIVER_ARGS=("$@")
fi

SRC="${CH5_DIR}/${DRIVER}.cpp"
[ -f "$SRC" ] || { echo "ERROR: no such driver: $SRC" >&2; exit 1; }

# Toolchain: MSYS2 UCRT64 first (g++ and the runtime DLLs), its usr/bin last
# (make), exactly as in ch4/build_ch4.sh.
if ! command -v g++ >/dev/null 2>&1 || ! command -v make >/dev/null 2>&1; then
    export PATH="/c/msys64/ucrt64/bin:${PATH}:/c/msys64/usr/bin"
fi
command -v g++ >/dev/null 2>&1 || {
    echo "ERROR: g++ not found. Install MSYS2 UCRT64 or put it on PATH." >&2
    exit 1
}

# qp_oracle reuses chapter4.cpp, which refuses to compile without the trace.
export EXTRA_DEFINES="-DST_SLIM_TRACE=1"
export BENCH_SRC="$SRC"
if [ "${FULL_LIB:-0}" != "1" ] && [ -f "${ARB}/build/libarboretum.a" ]; then
    export SKIP_LIB=1
fi

mkdir -p "$OUT_DIR"
BIN="${OUT_DIR}/${DRIVER}.exe"

echo "=== Chapter 5 driver: ${DRIVER} ==="
bash "${BENCH_DIR}/scripts/build.sh" "$REPO" "$BIN"

if [ "${NO_RUN:-0}" = "1" ]; then
    echo "built ${BIN} (NO_RUN=1, not running)"
    exit 0
fi

echo
"$BIN" --dataset "${DATASTORE}/BrazilianCities.txt" \
       --queryfile "${DATASTORE}/BrazilianCities500.txt" \
       --index-dir "$OUT_DIR" \
       "${DRIVER_ARGS[@]+"${DRIVER_ARGS[@]}"}"
