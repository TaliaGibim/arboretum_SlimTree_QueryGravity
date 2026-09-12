#!/usr/bin/env bash
#
# run_ch4.sh - runs the whole section 4.5 experiment and analyses it.
#
# Usage:
#   bash arboretum_SlimTree_QueryGravity/ch4/run_ch4.sh [options]
#
# Options:
#   --reps N        repetitions (default 5)
#   --adapt N       adaptation queries per repetition (default 200)
#   --measure N     measured queries per repetition   (default 100)
#   --k N           k for the k-NN batches            (default 10)
#   --radius R      radius for the range batches      (default 0.5)
#   --page-size N   page size in bytes                (default 1024)
#   --chunk N       H3 break-even sampling interval   (default 50)
#   --quick         2 reps, 100 adapt, 50 measure
#   --no-analyse    skip analyze_ch4.py at the end
#
# Unlike the Chapter 3 harness this needs NO git worktrees and no commit
# archaeology: every arm is a runtime flag on one binary built from one tree
# state, so there is nothing to check out and nothing to keep in sync.
#
set -euo pipefail

CH4_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "${CH4_DIR}/.." && pwd)"
ARB="${REPO}/arboretum"
DATASTORE="${ARB}/test-brcities-datastore"
OUT_DIR="${CH4_DIR}/build"
RESULTS="${CH4_DIR}/results"

REPS=5
ADAPT=200
MEASURE=100
K=10
RADIUS=0.5
PAGE_SIZE=1024
CHUNK=50
ANALYSE=1

while [ "$#" -gt 0 ]; do
    case "$1" in
        --reps)       REPS="$2"; shift 2 ;;
        --adapt)      ADAPT="$2"; shift 2 ;;
        --measure)    MEASURE="$2"; shift 2 ;;
        --k)          K="$2"; shift 2 ;;
        --radius)     RADIUS="$2"; shift 2 ;;
        --page-size)  PAGE_SIZE="$2"; shift 2 ;;
        --chunk)      CHUNK="$2"; shift 2 ;;
        --quick)      REPS=2; ADAPT=100; MEASURE=50; shift ;;
        --no-analyse) ANALYSE=0; shift ;;
        -h|--help)    sed -n '2,22p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

if ! command -v g++ >/dev/null 2>&1 || ! command -v make >/dev/null 2>&1; then
    export PATH="/c/msys64/ucrt64/bin:${PATH}:/c/msys64/usr/bin"
fi

[ -f "${DATASTORE}/BrazilianCities.txt" ] || {
    echo "ERROR: dataset not found in ${DATASTORE}" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 1. Build
# ---------------------------------------------------------------------------
NO_RUN=1 bash "${CH4_DIR}/build_ch4.sh" chapter4

BIN="${OUT_DIR}/chapter4.exe"
[ -x "$BIN" ] || { echo "ERROR: ${BIN} was not built" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 2. Fresh results
# ---------------------------------------------------------------------------
mkdir -p "$RESULTS"
find "$RESULTS" -mindepth 1 -maxdepth 1 -name '*.csv' -exec rm -f {} + 2>/dev/null || true
find "$RESULTS" -mindepth 1 -maxdepth 1 -name '*.md' -exec rm -f {} + 2>/dev/null || true

COMMIT="$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)"

{
    echo "timestamp   : $(date -Is)"
    echo "commit      : ${COMMIT}"
    echo "compiler    : $(g++ --version | head -1)"
    echo "reps        : ${REPS}"
    echo "adapt       : ${ADAPT}"
    echo "measure     : ${MEASURE}"
    echo "k           : ${K}"
    echo "radius      : ${RADIUS}"
    echo "page size   : ${PAGE_SIZE}"
    echo "chunk       : ${CHUNK}"
    echo "dataset     : ${DATASTORE}/BrazilianCities.txt"
    echo "query pool  : ${DATASTORE}/BrazilianCities500.txt"
} > "${RESULTS}/environment.txt"

# ---------------------------------------------------------------------------
# 3. The matrix
#
# The regime order is rotated per repetition so that thermal drift or a
# background process cannot systematically favour whichever regime always ran
# first.
# ---------------------------------------------------------------------------
for rep in $(seq 0 $((REPS - 1))); do
    echo "--- repetition $((rep + 1))/${REPS} ---"
    case $((rep % 3)) in
        0) order="concentrado disperso uniforme" ;;
        1) order="disperso uniforme concentrado" ;;
        *) order="uniforme concentrado disperso" ;;
    esac
    for regime in $order; do
        "$BIN" \
            --dataset      "${DATASTORE}/BrazilianCities.txt" \
            --dataset-label brcities5507 \
            --queryfile    "${DATASTORE}/BrazilianCities500.txt" \
            --regime       "$regime" \
            --query-type   both \
            --k            "$K" \
            --radius       "$RADIUS" \
            --page-size    "$PAGE_SIZE" \
            --adapt-queries   "$ADAPT" \
            --measure-queries "$MEASURE" \
            --chunk        "$CHUNK" \
            --rep-offset   "$rep" \
            --commit       "$COMMIT" \
            --index-dir    "$OUT_DIR" \
            --agg-out       "${RESULTS}/aggregated.csv" \
            --struct-out    "${RESULTS}/structure.csv" \
            --moves-out     "${RESULTS}/moves.csv" \
            --breakeven-out "${RESULTS}/breakeven.csv" \
            --nav-out       "${RESULTS}/navgraph.csv" \
            >> "${RESULTS}/run.log" 2>&1
        echo "  ${regime} done"
    done
done

# Index files are large and reproducible; do not leave them lying around.
find "$OUT_DIR" -maxdepth 1 -name '*.dat' -exec rm -f {} + 2>/dev/null || true

echo
echo "results in ${RESULTS}"

# ---------------------------------------------------------------------------
# 4. Analysis
# ---------------------------------------------------------------------------
if [ "$ANALYSE" = "1" ]; then
    if command -v python >/dev/null 2>&1; then
        python "${CH4_DIR}/analyze_ch4.py" --results "$RESULTS"
    elif command -v python3 >/dev/null 2>&1; then
        python3 "${CH4_DIR}/analyze_ch4.py" --results "$RESULTS"
    else
        echo "python not found; skipping analysis" >&2
    fi
fi
