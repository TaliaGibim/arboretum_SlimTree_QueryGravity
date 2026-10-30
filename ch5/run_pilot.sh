#!/usr/bin/env bash
#
# run_pilot.sh - Chapter 5, phase F0, end to end: build qp_oracle, compute the
# homogeneity of viewpoints, run every repetition of every regime, analyse.
#
# Usage:
#   bash arboretum_SlimTree_QueryGravity/ch5/run_pilot.sh [options]
#
#   --reps N        repetitions, seeds 42..42+N-1      (default 20)
#   --seed N        base seed                          (default 42)
#   --slots N       largest pivot set size S           (default 4)
#   --results DIR   output directory                   (default ch5/results_pilot)
#   --adapt N       adapt queries per repetition       (default 200)
#   --measure N     measured queries per repetition    (default 100)
#   --k N           k of the k-NN batches              (default 10)
#   --radius R      radius of the range batches        (default 0.5)
#   --page-size N   page size in bytes                 (default 1024)
#   --region-factor F                                  (default 3)
#   --regimes LIST  space-separated regimes (default "concentrado disperso uniforme")
#   --quick         2 repetitions, 100 adapt, 50 measured queries
#   --no-analyse    skip analyze_pilot.py at the end
#
# Same protocol and parameters as the Chapter 4 runs: same seeds, same regions,
# same queries, same insertion order, same tree.
#
set -euo pipefail

CH5_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "${CH5_DIR}/.." && pwd)"
ARB="${REPO}/arboretum"
DATASTORE="${ARB}/test-brcities-datastore"
OUT_DIR="${CH5_DIR}/build"
RESULTS="${CH5_DIR}/results_pilot"

REPS=20
SEED=42
SLOTS=4
ADAPT=200
MEASURE=100
K=10
RADIUS=0.5
PAGE_SIZE=1024
REGION_FACTOR=3
REGIMES="concentrado disperso uniforme"
ANALYSE=1

while [ "$#" -gt 0 ]; do
    case "$1" in
        --reps)          REPS="$2"; shift 2 ;;
        --seed)          SEED="$2"; shift 2 ;;
        --slots)         SLOTS="$2"; shift 2 ;;
        --results)       RESULTS="$2"; shift 2 ;;
        --adapt)         ADAPT="$2"; shift 2 ;;
        --measure)       MEASURE="$2"; shift 2 ;;
        --k)             K="$2"; shift 2 ;;
        --radius)        RADIUS="$2"; shift 2 ;;
        --page-size)     PAGE_SIZE="$2"; shift 2 ;;
        --region-factor) REGION_FACTOR="$2"; shift 2 ;;
        --regimes)       REGIMES="$2"; shift 2 ;;
        --quick)         REPS=2; ADAPT=100; MEASURE=50; shift ;;
        --no-analyse)    ANALYSE=0; shift ;;
        -h|--help)       sed -n '2,24p' "$0"; exit 0 ;;
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
NO_RUN=1 bash "${CH5_DIR}/build_ch5.sh" qp_oracle
BIN="${OUT_DIR}/qp_oracle.exe"
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
if [ -n "$(git -C "$REPO" status --porcelain -- . ':!ch5/results_pilot' 2>/dev/null)" ]; then
    COMMIT="${COMMIT}-dirty"
fi
{
    echo "timestamp   : $(date -Is)"
    echo "commit      : ${COMMIT}"
    echo "compiler    : $(g++ --version | head -1)"
    echo "reps        : ${REPS}"
    echo "seeds       : ${SEED}..$((SEED + REPS - 1))"
    echo "slots       : 1..${SLOTS}"
    echo "adapt       : ${ADAPT}"
    echo "measure     : ${MEASURE}"
    echo "k           : ${K}"
    echo "radius      : ${RADIUS}"
    echo "page size   : ${PAGE_SIZE}"
    echo "region fact.: ${REGION_FACTOR}"
    echo "regimes     : ${REGIMES}"
    echo "dataset     : ${DATASTORE}/BrazilianCities.txt"
    echo "query pool  : ${DATASTORE}/BrazilianCities500.txt"
} > "${RESULTS}/environment.txt"

COMMON=(--dataset "${DATASTORE}/BrazilianCities.txt"
        --queryfile "${DATASTORE}/BrazilianCities500.txt"
        --dataset-label brcities5507 --index-dir "$OUT_DIR"
        --seed "$SEED" --slots "$SLOTS" --adapt-queries "$ADAPT"
        --measure-queries "$MEASURE" --k "$K" --radius "$RADIUS"
        --page-size "$PAGE_SIZE" --region-factor "$REGION_FACTOR")

# ---------------------------------------------------------------------------
# 3. Homogeneity of viewpoints: a property of the data set, computed once.
# ---------------------------------------------------------------------------
"$BIN" "${COMMON[@]}" --hv-only --hv-out "${RESULTS}/hv.csv" \
    | tee -a "${RESULTS}/run.log"

# ---------------------------------------------------------------------------
# 4. Every repetition of every regime, one process each.
# ---------------------------------------------------------------------------
for rep in $(seq 0 $((REPS - 1))); do
    echo "--- repetition $((rep + 1))/${REPS} ---"
    for regime in $REGIMES; do
        if ! "$BIN" "${COMMON[@]}" --regime "$regime" --reps 1 --rep-offset "$rep" \
                --pilot-out "${RESULTS}/pilot.csv" \
                --pivots-out "${RESULTS}/pivots.csv" >> "${RESULTS}/run.log" 2>&1; then
            echo "ERROR: ${regime}, repetition ${rep} failed; see ${RESULTS}/run.log" >&2
            tail -5 "${RESULTS}/run.log" >&2
            exit 1
        fi
        echo "  ${regime} done"
    done
done

# Index files are reproducible; do not leave them lying around.
find "$OUT_DIR" -maxdepth 1 -name '*.dat' -exec rm -f {} + 2>/dev/null || true
echo
echo "results in ${RESULTS}"

# ---------------------------------------------------------------------------
# 5. Analysis
# ---------------------------------------------------------------------------
if [ "$ANALYSE" = "1" ]; then
    if command -v python >/dev/null 2>&1; then
        python "${CH5_DIR}/analyze_pilot.py" --results "$RESULTS"
    elif command -v python3 >/dev/null 2>&1; then
        python3 "${CH5_DIR}/analyze_pilot.py" --results "$RESULTS"
    else
        echo "python not found; skipping analysis" >&2
    fi
fi
