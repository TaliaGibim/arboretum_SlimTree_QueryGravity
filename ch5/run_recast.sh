#!/usr/bin/env bash
#
# run_recast.sh - Chapter 5, phase R0, end to end: build qp_recast, run every
# repetition of every regime and order, analyse.
#
# Usage:
#   bash arboretum_SlimTree_QueryGravity/ch5/run_recast.sh [options]
#
#   --reps N         repetitions, seeds 42..42+N-1        (default 20)
#   --seed N         base seed                            (default 42)
#   --node-slots L   K values, comma-separated            (default 2,4,8)
#   --results DIR    output directory                     (default ch5/results_recast)
#   --adapt N        adapt queries per repetition         (default 200)
#   --measure N      measured queries per repetition      (default 100)
#   --k N            k of the k-NN batches                (default 10)
#   --radius R       radius of the range batches          (default 0.5)
#   --page-size N    page size in bytes                   (default 1024)
#   --region-factor F                                     (default 3)
#   --extra "ARGS"   extra qp_recast options, e.g. "--decay 0"
#   --quick          2 repetitions, 100 adapt, 50 measured queries
#   --no-analyse     skip analyze_recast.py at the end
#
# Runs: concentrado, disperso and uniforme with the adapt queries mixed (the
# Chapter 4 stream), then concentrado and disperso with the adapt queries region
# by region ("salto": the focus jumps). Same seeds, regions, queries, insertion
# order and tree as Chapter 4 and the F0 pilot.
#
set -euo pipefail

CH5_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "${CH5_DIR}/.." && pwd)"
ARB="${REPO}/arboretum"
DATASTORE="${ARB}/test-brcities-datastore"
OUT_DIR="${CH5_DIR}/build"
RESULTS="${CH5_DIR}/results_recast"

REPS=20
SEED=42
NODE_SLOTS="2,4,8"
ADAPT=200
MEASURE=100
K=10
RADIUS=0.5
PAGE_SIZE=1024
REGION_FACTOR=3
EXTRA=""
ANALYSE=1

while [ "$#" -gt 0 ]; do
    case "$1" in
        --reps)          REPS="$2"; shift 2 ;;
        --seed)          SEED="$2"; shift 2 ;;
        --node-slots)    NODE_SLOTS="$2"; shift 2 ;;
        --results)       RESULTS="$2"; shift 2 ;;
        --adapt)         ADAPT="$2"; shift 2 ;;
        --measure)       MEASURE="$2"; shift 2 ;;
        --k)             K="$2"; shift 2 ;;
        --radius)        RADIUS="$2"; shift 2 ;;
        --page-size)     PAGE_SIZE="$2"; shift 2 ;;
        --region-factor) REGION_FACTOR="$2"; shift 2 ;;
        --extra)         EXTRA="$2"; shift 2 ;;
        --quick)         REPS=2; ADAPT=100; MEASURE=50; shift ;;
        --no-analyse)    ANALYSE=0; shift ;;
        -h|--help)       sed -n '2,27p' "$0"; exit 0 ;;
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
NO_RUN=1 bash "${CH5_DIR}/build_ch5.sh" qp_recast
BIN="${OUT_DIR}/qp_recast.exe"
[ -x "$BIN" ] || { echo "ERROR: ${BIN} was not built" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 2. Fresh results
# ---------------------------------------------------------------------------
mkdir -p "$RESULTS"
find "$RESULTS" -mindepth 1 -maxdepth 1 \( -name '*.csv' -o -name '*.md' \) \
    -exec rm -f {} + 2>/dev/null || true
rm -f "${RESULTS}/run.log"

COMMIT="$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)"
# Uncommitted changes - untracked files included - mean the binary is NOT what
# that commit would build.
if [ -n "$(git -C "$REPO" status --porcelain -- . ':!ch5/results_recast' ':!ch5/results_pilot' 2>/dev/null)" ]; then
    COMMIT="${COMMIT}-dirty"
fi
{
    echo "timestamp   : $(date -Is)"
    echo "commit      : ${COMMIT}"
    echo "compiler    : $(g++ --version | head -1)"
    echo "reps        : ${REPS}"
    echo "seeds       : ${SEED}..$((SEED + REPS - 1))"
    echo "node slots  : ${NODE_SLOTS}"
    echo "adapt       : ${ADAPT}"
    echo "measure     : ${MEASURE}"
    echo "k           : ${K}"
    echo "radius      : ${RADIUS}"
    echo "page size   : ${PAGE_SIZE}"
    echo "region fact.: ${REGION_FACTOR}"
    echo "extra       : ${EXTRA}"
    echo "dataset     : ${DATASTORE}/BrazilianCities.txt"
    echo "query pool  : ${DATASTORE}/BrazilianCities500.txt"
} > "${RESULTS}/environment.txt"

COMMON=(--dataset "${DATASTORE}/BrazilianCities.txt"
        --queryfile "${DATASTORE}/BrazilianCities500.txt"
        --dataset-label brcities5507 --index-dir "$OUT_DIR"
        --seed "$SEED" --node-slots "$NODE_SLOTS" --adapt-queries "$ADAPT"
        --measure-queries "$MEASURE" --k "$K" --radius "$RADIUS"
        --page-size "$PAGE_SIZE" --region-factor "$REGION_FACTOR")
# shellcheck disable=SC2206
EXTRA_ARGS=($EXTRA)

# ---------------------------------------------------------------------------
# 3. Every repetition of every (regime, order), one process each.
# ---------------------------------------------------------------------------
RUNS="concentrado:mixed disperso:mixed uniforme:mixed concentrado:salto disperso:salto"
for rep in $(seq 0 $((REPS - 1))); do
    echo "--- repetition $((rep + 1))/${REPS} ---"
    for run in $RUNS; do
        regime="${run%%:*}"
        order="${run##*:}"
        if ! "$BIN" "${COMMON[@]}" "${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}" \
                --regime "$regime" --order "$order" --reps 1 --rep-offset "$rep" \
                --out "${RESULTS}/recast.csv" \
                --online-out "${RESULTS}/online.csv" >> "${RESULTS}/run.log" 2>&1; then
            echo "ERROR: ${regime}/${order}, repetition ${rep} failed; see ${RESULTS}/run.log" >&2
            tail -5 "${RESULTS}/run.log" >&2
            exit 1
        fi
        echo "  ${regime} (${order}) done"
    done
done

# Index files are reproducible; do not leave them lying around.
find "$OUT_DIR" -maxdepth 1 -name '*.dat' -exec rm -f {} + 2>/dev/null || true
echo
echo "results in ${RESULTS}"

# ---------------------------------------------------------------------------
# 4. Analysis
# ---------------------------------------------------------------------------
if [ "$ANALYSE" = "1" ]; then
    if command -v python >/dev/null 2>&1; then
        python "${CH5_DIR}/analyze_recast.py" --results "$RESULTS"
    elif command -v python3 >/dev/null 2>&1; then
        python3 "${CH5_DIR}/analyze_recast.py" --results "$RESULTS"
    else
        echo "python not found; skipping analysis" >&2
    fi
fi
