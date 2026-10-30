# benchmark/ — generic Slim-tree measurement harness

This directory holds the *general-purpose* measurement harness. It is not tied
to any one experiment.

The Query Gravity experiment that used to live here has been removed: its
scripts (`qg_demo.cpp`, `demo.sh`, `gen_qg_headers.sh`, `run_sweep.sh`,
`analyze_sweep.py`) and its results are gone, and the Slim-tree headers were
reverted to pristine arboretum. The commit is kept in the history
(`cf6fd9d`, "Implementation of Quey Gravity") together with the revert that
follows it, so the episode remains reproducible from git if it is ever needed.

**The Chapter 4 experiment is not here.** It lives in `ch4/`, next to this
directory. See [ch4/README.md](../ch4/README.md).

This directory used to sit next to the repository, outside git. It is versioned
now, so a fresh clone builds. `ch4/build_ch4.sh` looks for it inside the
repository first and falls back to the old sibling location.

## What is left

| File | Purpose |
|---|---|
| `src/bench.cpp` | A/B benchmark driver: builds a tree, optionally runs training queries plus `Optimize()`, then times range and k-NN batches, verifying every answer against a brute-force scan. Emits a 35-column aggregate CSV plus per-query and per-neighbour CSVs. |
| `src/compat.h` | Portability shim, force-included into every translation unit. It defines `u_int8_t`/`16`/`32`/`64`, which MinGW does not provide and which arboretum uses everywhere. **Compiling without it produces hundreds of errors in `CNSCache.h` that look like arboretum bugs and are not.** |
| `scripts/build.sh` | The single place compiler flags are defined, so that two versions under comparison are provably built identically. Also used by the Chapter 4 build script for exactly that reason. |
| `scripts/run_all.sh` | Runs a two-version comparison out of git worktrees, alternating which version is measured first. |
| `scripts/analyze.py` | Collapses repetitions, compares two version labels, writes `SUMMARY.md` and hand-rolled SVG charts. Stdlib only. |

`build.sh` takes `<worktree-dir> <output-binary>` and honours:

- `BENCH_SRC` — build a different driver (this is how Chapter 4 reuses it)
- `EXTRA_DEFINES` — extra `-D` flags, e.g. `-DST_SLIM_TRACE=1`
- `SKIP_LIB=1` — reuse `libarboretum.a`. Safe: no library `.cpp` includes
  `stSlimTree.h`, so header-only changes need only the driver relinked.
- `OPT_FLAGS` — override `-O2`

## Toolchain

MSYS2 UCRT64. Three things bite on Windows and all three are handled, but they
are worth knowing:

1. **PATH order.** `/c/msys64/ucrt64/bin` must come first (compiler, and the
   runtime DLLs the binary needs to *run* — without them it exits 127 with no
   message). `/c/msys64/usr/bin` must come last, or MSYS2 coreutils shadow the
   ones this shell already has.
2. **TMP.** MSYS2's `make` spawns its own `sh`, which discards `TMP`/`TEMP`
   inherited from a different MSYS runtime (Git Bash is one) and falls back to
   `C:\WINDOWS\`, which is not writable. `build.sh` passes them as make
   command-line variables, the only form that survives.
3. **Antivirus.** A freshly linked `.exe` can briefly refuse to start.
   `build.sh` polls it before returning; if a build still fails with
   "Permission denied", simply run it again.

Note that `build.sh` runs `make clean` before building the library, which
*deletes* `arboretum/build/libarboretum.a`. If a build fails partway, the
library is gone and the next `SKIP_LIB=1` has nothing to reuse.
