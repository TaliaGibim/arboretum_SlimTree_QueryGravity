#!/usr/bin/env bash
#
# build.sh - Compiles libarboretum.a plus the benchmark binary for ONE version
#            of the Slim-Tree, from a git worktree that is never modified.
#
# Usage:  build.sh <worktree-dir> <output-binary>
#
# Example:
#   ./build.sh /c/Users/me/slimtree-bench/worktrees/baseline \
#              /c/Users/me/slimtree-bench/bin/bench_baseline.exe
#
# Both versions are built with byte-identical flags (see FLAGS below); the only
# difference between the two builds is the include path, which decides which
# copy of stSlimTree-inl.h gets instantiated. That is what makes the comparison
# fair.
#
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <worktree-dir> <output-binary>" >&2
    exit 2
fi

WORKTREE="$1"
OUTBIN="$2"

BENCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPAT="${BENCH_DIR}/src/compat.h"
# BENCH_SRC may be overridden so that the same flags build ANY driver, e.g.
# the Chapter 4 programs under src/ch4. Defaults to the Chapter 3 benchmark.
BENCH_SRC="${BENCH_SRC:-${BENCH_DIR}/src/bench.cpp}"

ARB="${WORKTREE}/arboretum"
APP_DIR="${ARB}/test-brcities-sample01-arboretum-slimtree"

for p in "$COMPAT" "$BENCH_SRC" "$ARB/Makefile" "$APP_DIR/city.cpp"; do
    [ -e "$p" ] || { echo "ERROR: missing $p" >&2; exit 1; }
done

# ---------------------------------------------------------------------------
# Compiler flags. Kept in ONE place so that both versions provably share them.
#   -m64 -fPIC   : taken verbatim from the repository's own Makefile
#   -std=c++20   : taken verbatim from the repository's own Makefile
#   -O2          : the repository default is -O0, which makes wall-clock timings
#                  dominated by unoptimised template code. Overridable via
#                  OPT_FLAGS, and applied identically to both versions.
#   -include     : force-includes the portability shim without touching any
#                  file tracked by git (see src/compat.h).
# ---------------------------------------------------------------------------
OPT_FLAGS="${OPT_FLAGS:--O2}"
FLAGS="-m64 -fPIC ${OPT_FLAGS} -std=c++20 -include ${COMPAT}"

# MSYS2's make.exe spawns its own sh, which discards TMP/TEMP when they were
# inherited from a DIFFERENT MSYS runtime (Git Bash is one). It then falls back
# to C:\WINDOWS\, which is not writable, and every compile dies with
# "Cannot create temporary file". Passing them as make COMMAND-LINE variables is
# the only form that survives that hand-off. Harmless when TMP is already sane,
# e.g. inside the MSYS2 UCRT64 shell.
MAKE_TMP='TMP=/tmp TEMP=/tmp TMPDIR=/tmp'

# --- hyper-parameter sweep support (optional) ------------------------------
# QG_PARAM_DIR : directory holding the parameterised copies of stSlimTree.h and
#                stSlimTree-inl.h (see gen_qg_headers.sh). Placed FIRST on the
#                include path so it shadows the worktree's originals.
# QG_DEFINES   : extra -D flags selecting the hyper-parameter values.
# SKIP_LIB     : set to 1 to reuse the existing libarboretum.a. Safe because no
#                library .cpp includes stSlimTree.h, so the sweep only ever
#                needs the benchmark object rebuilt -- 6s instead of a minute.
QG_PARAM_DIR="${QG_PARAM_DIR:-}"
QG_DEFINES="${QG_DEFINES:-}"
# EXTRA_DEFINES: further -D flags for the driver, e.g. -DST_SLIM_TRACE=1. Kept
# separate from QG_DEFINES so Chapter 4 never borrows a Chapter 3 name.
EXTRA_DEFINES="${EXTRA_DEFINES:-}"
SKIP_LIB="${SKIP_LIB:-0}"

PARAM_INC=""
if [ -n "$QG_PARAM_DIR" ]; then
    [ -f "${QG_PARAM_DIR}/arboretum/stSlimTree-inl.h" ] || {
        echo "ERROR: QG_PARAM_DIR has no arboretum/stSlimTree-inl.h" >&2; exit 1; }
    PARAM_INC="-I${QG_PARAM_DIR}"
fi

echo "=== building $(basename "$WORKTREE") ==="
echo "    worktree : ${WORKTREE}"
echo "    flags    : ${FLAGS}"
[ -n "$QG_DEFINES" ] && echo "    params   : ${QG_DEFINES}"
echo "    g++      : $(g++ --version | head -1)"

# --- 1. library -------------------------------------------------------------
# Uses the repository's own Makefile; CFLAGS/STD are overridden on the command
# line so the Makefile itself stays untouched. 'clean' first so that a stale
# build/ directory cannot make the 'mkdir build' rule fail.
if [ "$SKIP_LIB" = "1" ] && [ -f "${ARB}/build/libarboretum.a" ]; then
    echo "    library  : reusing ${ARB}/build/libarboretum.a"
else
    make -C "$ARB" clean >/dev/null 2>&1 || true
    make -C "$ARB" CFLAGS="-m64 -fPIC ${OPT_FLAGS} -include ${COMPAT}" STD="-std=c++20" ${MAKE_TMP}
fi

# --- 2. benchmark binary ----------------------------------------------------
mkdir -p "$(dirname "$OUTBIN")"
OBJDIR="$(mktemp -d)"
trap 'rm -rf "$OBJDIR"' EXIT

g++ $FLAGS $QG_DEFINES $EXTRA_DEFINES $PARAM_INC -I"${ARB}/src/include" \
    -I"${APP_DIR}" -I"${WORKTREE}/ch4" \
    -c "$BENCH_SRC" -o "${OBJDIR}/bench.o"
g++ $FLAGS -I"${ARB}/src/include" -I"${APP_DIR}" \
    -c "${APP_DIR}/city.cpp" -o "${OBJDIR}/city.o"

g++ $FLAGS "${OBJDIR}/bench.o" "${OBJDIR}/city.o" \
    -o "$OUTBIN" -L"${ARB}/build" -larboretum -lstdc++ -lm

# On Windows a just-linked .exe can briefly refuse to start ("Permission denied")
# while a real-time virus scanner still holds it open. Poll until it actually
# runs, so the caller never trips over that race.
for attempt in $(seq 1 40); do
    if "$OUTBIN" --help >/dev/null 2>&1; then
        break
    fi
    if [ "$attempt" -eq 40 ]; then
        echo "ERROR: ${OUTBIN} was linked but will not execute." >&2
        "$OUTBIN" --help || true
        exit 1
    fi
    sleep 1
done

echo "    -> ${OUTBIN}"
