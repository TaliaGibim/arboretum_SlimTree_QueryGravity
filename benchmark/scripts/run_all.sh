#!/usr/bin/env bash
#
# run_all.sh - End-to-end experiment driver.
#
#   1. resolves the two commits (Query Gravity and its parent) from the log;
#   2. creates one git worktree per version (never overwriting any source file);
#   3. builds both with identical flags;
#   4. records the execution environment;
#   5. runs the full experiment matrix;
#   6. leaves one result directory per version plus a combined CSV.
#
# Usage:
#   ./run_all.sh [--reps N] [--queries N] [--quick]
#
# Environment overrides:
#   WORK_DIR   scratch area for worktrees, binaries and index files.
#              Default: $HOME/slimtree-bench
#              It is deliberately OUTSIDE the OneDrive-synced project folder:
#              the benchmark writes and re-reads the index file constantly, and
#              having OneDrive scan/lock it would corrupt the timings.
#
set -euo pipefail

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
BENCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_DIR="$(cd "${BENCH_DIR}/.." && pwd)"
# benchmark/ lives inside the repository; next to it is the old layout.
if [ -d "${PROJECT_DIR}/arboretum" ]; then
    REPO="${PROJECT_DIR}"
else
    REPO="${PROJECT_DIR}/arboretum_SlimTree_QueryGravity"
fi
DATASTORE="${REPO}/arboretum/test-brcities-datastore"
RESULTS="${BENCH_DIR}/results"

# Default scratch root. On Windows/MSYS2 $HOME is /home/<user> inside the MSYS
# tree, which is not where a user would look; the Windows profile directory is
# the friendlier and more discoverable choice.
if [ -z "${WORK_DIR:-}" ]; then
    if [ -n "${USERPROFILE:-}" ] && command -v cygpath >/dev/null 2>&1; then
        WORK_DIR="$(cygpath -u "$USERPROFILE")/slimtree-bench"
    else
        WORK_DIR="${HOME}/slimtree-bench"
    fi
fi
WT_DIR="${WORK_DIR}/worktrees"
BIN_DIR="${WORK_DIR}/bin"
IDX_DIR="${WORK_DIR}/index"

REPS=10
MEASURE_QUERIES=200
TRAIN_QUERIES=250
WARMUP_QUERIES=50
SEED=42
PAGE_SIZE=1024
K_LIST="1,5,10,20,50"
RADIUS_LIST="0.2"
HOTSPOTS=10   # number of hot regions in the skewed workload

while [ "$#" -gt 0 ]; do
    case "$1" in
        --reps)    REPS="$2"; shift 2 ;;
        --queries) MEASURE_QUERIES="$2"; shift 2 ;;
        --quick)   REPS=2; MEASURE_QUERIES=50; WARMUP_QUERIES=10; shift ;;
        -h|--help) sed -n '2,25p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

[ -d "$REPO/.git" ] || { echo "ERROR: git repo not found at $REPO" >&2; exit 1; }
[ -f "$DATASTORE/BrazilianCities.txt" ] || { echo "ERROR: datasets not found in $DATASTORE" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 1. Resolve the two commits under comparison
# ---------------------------------------------------------------------------
# The Query Gravity commit is found by its message (note: the message in the
# repository contains the typo "Quey Gravity", so the pattern is deliberately
# loose). Its immediate parent is the baseline.
QG_COMMIT="$(git -C "$REPO" log --all --format='%H' -i --grep='gravity' -1)"
[ -n "$QG_COMMIT" ] || { echo "ERROR: no commit matching 'gravity' found" >&2; exit 1; }
BASE_COMMIT="$(git -C "$REPO" rev-parse "${QG_COMMIT}^")"

QG_SHORT="$(git -C "$REPO" rev-parse --short "$QG_COMMIT")"
BASE_SHORT="$(git -C "$REPO" rev-parse --short "$BASE_COMMIT")"

echo "=========================================================="
echo " baseline      : $BASE_SHORT  $(git -C "$REPO" log -1 --format=%s "$BASE_COMMIT")"
echo " query gravity : $QG_SHORT  $(git -C "$REPO" log -1 --format=%s "$QG_COMMIT")"
echo " work dir      : $WORK_DIR"
echo "=========================================================="

# ---------------------------------------------------------------------------
# 2. Worktrees (idempotent)
# ---------------------------------------------------------------------------
mkdir -p "$WT_DIR" "$BIN_DIR" "$IDX_DIR"

ensure_worktree() {
    local path="$1" commit="$2"
    if [ -d "$path" ]; then
        local head; head="$(git -C "$path" rev-parse HEAD)"
        if [ "$head" = "$commit" ]; then
            echo "worktree ok: $path @ $(git -C "$path" rev-parse --short HEAD)"
            return
        fi
        echo "worktree at $path points at $head, re-checking out $commit"
        git -C "$path" checkout --detach "$commit"
        return
    fi
    git -C "$REPO" worktree add --detach "$path" "$commit"
}

ensure_worktree "${WT_DIR}/baseline"      "$BASE_COMMIT"
ensure_worktree "${WT_DIR}/querygravity"  "$QG_COMMIT"

# ---------------------------------------------------------------------------
# 3. Build both versions with identical flags
# ---------------------------------------------------------------------------
bash "${BENCH_DIR}/scripts/build.sh" "${WT_DIR}/baseline"     "${BIN_DIR}/bench_baseline.exe"
bash "${BENCH_DIR}/scripts/build.sh" "${WT_DIR}/querygravity" "${BIN_DIR}/bench_querygravity.exe"

# ---------------------------------------------------------------------------
# 4. Fresh results tree + environment record
# ---------------------------------------------------------------------------
# Empty the directory rather than removing it: on Windows a directory any shell
# is sitting in cannot be deleted ("Device or resource busy"), and under `set -e`
# that aborts the run *after* the old contents are already gone.
mkdir -p "$RESULTS"
find "$RESULTS" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null || true
mkdir -p "$RESULTS/baseline" "$RESULTS/querygravity" "$RESULTS/charts"

ENVFILE="${RESULTS}/environment.txt"
{
    echo "timestamp            : $(date -Is)"
    echo "baseline_commit      : $BASE_COMMIT"
    echo "querygravity_commit  : $QG_COMMIT"
    echo "compiler             : $(g++ --version | head -1)"
    echo "compiler_path        : $(command -v g++)"
    echo "compile_flags        : -m64 -fPIC ${OPT_FLAGS:--O2} -std=c++20 -include benchmark/src/compat.h"
    echo "make                 : $(make --version | head -1)"
    echo "uname                : $(uname -a)"
    echo "reps                 : $REPS"
    echo "execution_order      : one repetition per process, versions interleaved, order alternated each repetition"
    echo "measure_queries      : $MEASURE_QUERIES"
    echo "train_queries        : $TRAIN_QUERIES"
    echo "warmup_queries       : $WARMUP_QUERIES"
    echo "seed                 : $SEED"
    echo "page_size            : $PAGE_SIZE"
    echo "k_list               : $K_LIST"
    echo "radius_list          : $RADIUS_LIST"
    echo "query_dists          : uniform, skewed"
    echo "hotspots             : $HOTSPOTS"
    if command -v powershell.exe >/dev/null 2>&1; then
        echo "os                   : $(powershell.exe -NoProfile -Command '(Get-CimInstance Win32_OperatingSystem).Caption + " " + (Get-CimInstance Win32_OperatingSystem).Version' 2>/dev/null | tr -d '\r')"
        echo "cpu                  : $(powershell.exe -NoProfile -Command '(Get-CimInstance Win32_Processor).Name' 2>/dev/null | tr -d '\r')"
        echo "cpu_cores            : $(powershell.exe -NoProfile -Command '(Get-CimInstance Win32_Processor).NumberOfLogicalProcessors' 2>/dev/null | tr -d '\r')"
        echo "memory_bytes         : $(powershell.exe -NoProfile -Command '(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory' 2>/dev/null | tr -d '\r')"
    else
        echo "os                   : $(uname -sr)"
        echo "cpu                  : $(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ //')"
        echo "cpu_cores            : $(nproc 2>/dev/null)"
        echo "memory_bytes         : $(( $(grep MemTotal /proc/meminfo 2>/dev/null | awk '{print $2}') * 1024 ))"
    fi
} | tee "$ENVFILE"
echo

# ---------------------------------------------------------------------------
# 5. Experiment matrix
# ---------------------------------------------------------------------------
# Two datasets are exercised:
#   brcities5507 - the full file is indexed, the 500-city file supplies queries
#   brcities500  - the small file is indexed and also supplies the queries
#
# Two protocols are exercised (see the header of src/bench.cpp for why):
#   no-optimize    isolates the pure overhead of Query Gravity's bookkeeping
#   train-optimize the only setting where Query Gravity can change the tree
#
# Execution order matters. Running every repetition of one version and only then
# the other lets machine drift (CPU frequency ramp-up, OS file cache warming)
# masquerade as a difference between versions -- measured at roughly 5% on this
# workload, which is the same order of magnitude as the effect being studied.
# The driver therefore runs ONE repetition per process, interleaves the two
# versions, and flips which one goes first on every repetition, so that any
# ordering bias cancels out instead of accumulating in one version's favour.
run_one() {
    local version="$1" dataset_file="$2" dataset_label="$3" protocol="$4" \
          query_dist="$5" rep="$6"
    local bin commit
    if [ "$version" = "baseline" ]; then
        bin="${BIN_DIR}/bench_baseline.exe";     commit="$BASE_SHORT"
    else
        bin="${BIN_DIR}/bench_querygravity.exe"; commit="$QG_SHORT"
    fi

    "$bin" \
        --dataset       "${DATASTORE}/${dataset_file}" \
        --dataset-label "${dataset_label}" \
        --queryfile     "${DATASTORE}/BrazilianCities500.txt" \
        --version-label "$version" \
        --commit        "$commit" \
        --protocol      "$protocol" \
        --train-mode    disjoint \
        --query-dist    "$query_dist" \
        --hotspots      "$HOTSPOTS" \
        --query-type    both \
        --k-list        "$K_LIST" \
        --radius-list   "$RADIUS_LIST" \
        --reps          1 \
        --rep-offset    "$rep" \
        --measure-queries "$MEASURE_QUERIES" \
        --train-queries   "$TRAIN_QUERIES" \
        --warmup-queries  "$WARMUP_QUERIES" \
        --seed          "$SEED" \
        --page-size     "$PAGE_SIZE" \
        --index         "${IDX_DIR}/${version}_${dataset_label}_${protocol}_${query_dist}.dat" \
        --agg-out       "${RESULTS}/${version}/aggregated.csv" \
        --raw-out       "${RESULTS}/${version}/raw_queries.csv" \
        --neighbors-out "${RESULTS}/${version}/neighbors.csv" \
        >> "${RESULTS}/${version}/run.log" 2>&1
}

for rep in $(seq 0 $((REPS - 1))); do
    echo "--- repetition $((rep + 1))/${REPS} ---"
    for query_dist in uniform skewed; do
        for protocol in no-optimize train-optimize; do
            for ds in "BrazilianCities.txt:brcities5507" "BrazilianCities500.txt:brcities500"; do
                ds_file="${ds%%:*}"
                ds_label="${ds##*:}"
                # Alternate which version is measured first.
                if [ $((rep % 2)) -eq 0 ]; then
                    order="baseline querygravity"
                else
                    order="querygravity baseline"
                fi
                for version in $order; do
                    run_one "$version" "$ds_file" "$ds_label" "$protocol" \
                            "$query_dist" "$rep"
                done
            done
        done
    done
done

# ---------------------------------------------------------------------------
# 6. Combined file
# ---------------------------------------------------------------------------
head -1 "${RESULTS}/baseline/aggregated.csv" > "${RESULTS}/combined_aggregated.csv"
tail -n +2 "${RESULTS}/baseline/aggregated.csv"     >> "${RESULTS}/combined_aggregated.csv"
tail -n +2 "${RESULTS}/querygravity/aggregated.csv" >> "${RESULTS}/combined_aggregated.csv"

echo
echo "=========================================================="
echo "Results written to: $RESULTS"
echo "  baseline/aggregated.csv      per-repetition rows, baseline"
echo "  querygravity/aggregated.csv  per-repetition rows, query gravity"
echo "  combined_aggregated.csv      both versions, one file"
echo "  */raw_queries.csv            one row per individual query"
echo "  */neighbors.csv              k-NN result dump (repetition 0)"
echo "  environment.txt              compiler / OS / CPU / memory record"
echo
echo "Next:  py benchmark/scripts/analyze.py"
echo "=========================================================="
