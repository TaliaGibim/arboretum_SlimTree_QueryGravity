# Chapter 4 — adaptive Slim-tree

Implementation and experiment for Chapter 4 of the thesis: incorporating the
query workload into the organisation of a Slim-tree.

Two alternatives, which the thesis treats as mutually exclusive:

- **Etapa 1** (§4.3) — query-guided relocation, incremental, after every query.
- **Etapa 2** (§4.4) — a query tree `T_Q`, a Voronoi partition induced by the
  queries, and one batch reorganisation.

## Quick start

```bash
# everything: build, run the matrix, analyse
bash arboretum_SlimTree_QueryGravity/ch4/run_ch4.sh --reps 5

# a fast smoke run
bash arboretum_SlimTree_QueryGravity/ch4/run_ch4.sh --quick

# one verification program (build + run, trace configured automatically)
bash arboretum_SlimTree_QueryGravity/ch4/build_ch4.sh phase4_check
```

Results land in `ch4/results/`; `SUMMARY.md` there is the readable report.

## Layout

Headers are in `arboretum/src/include/arboretum/` so they are includable as
`<arboretum/...>` like the rest of the library; drivers are here.

| File | What it is |
|---|---|
| `arboretum/.../stSlimQueryTrace.h` | The traversal trace. The **only** thing that had to go inside `stSlimTree`, as 17 one-line macro calls. Compiled out unless `ST_SLIM_TRACE=1`. |
| `arboretum/.../stSlimEtapa1.h` | Etapa 1. Equations 4.3–4.7. Works purely through the page manager; never includes `stSlimTree.h`. |
| `arboretum/.../stSlimVoronoiRebuild.h` | Etapa 2. `T_Q`, pivots, Equation 4.9, and the §4.4.2 navigation graph. |
| `arboretum/.../stSlimMetrics.h` | Read-only structural collector: occupancy, radii, overlap. |
| `chapter4.cpp` | The §4.5 experiment: 5 arms × 3 regimes × 2 query types. |
| `phase1/3/4/6_check.cpp` | Verification programs, one per implementation stage. |
| `run_ch4.sh`, `analyze_ch4.py` | Orchestration and analysis. |

## Why the trace lives inside the tree and nothing else does

`stSlimTree` has only `public:` and `private:` — there is no `protected:` — and
`GetRoot()` is private except under `__stDEBUG__`, which also switches on
expensive checks. The recursive query overloads are private and non-virtual. So
a subclass cannot instrument the traversal.

The compromise: the *trace* is inside the tree, because the traversal is; but
everything that *acts* on it is outside, driven by the page ids the trace
records. Etapa 1 therefore needs nothing private, and the whole modification to
upstream arboretum is 17 macro calls plus one `#include`.

With `ST_SLIM_TRACE=0` (the default) that is provably free: `bench.cpp`
compiled against pristine `c88c87d` and against this tree produces **byte-identical
object files**.

## Verification

Each stage has a checker; all pass.

| Program | Checks |
|---|---|
| `phase1_check` | `GetFatFactor()` links (it was declared and never defined upstream); structural metrics agree with the tree; determinism. |
| `phase3_check` | 8 trace invariants, e.g. *expanded edges + 1 == nodes entered*, and that index/leaf distances reconcile exactly with the evaluator. |
| `phase4_check` | 11 Etapa 1 invariants, including **Definition 1 recomputed for every object against every ancestor**, and that node and object counts never change. |
| `phase6_check` | 9 Etapa 2 invariants, including that no query centre leaks into the indexed data and that the original tree still answers after `T'` is built. |

Every measured query in the main experiment is also verified against a
brute-force scan; the run aborts if any answer differs.

## Results

5 repetitions, 150 configurations, all exact. Full tables in
`results/SUMMARY.md`.

- **H2 supported.** The destination radius never grew, in any run. Equation 4.3
  holds by construction and is checked at run time.
- **H3 not supported on this workload.** Etapa 1 costs 18–25k distances to adapt
  and saves between −135 and +1248 per measure pass; break-even is never
  reached.
- **H4 supported.** Relocations per query decay under `concentrado`
  (6.5 → 4.2) and stay flat under `uniforme` (7.5 → 7.2) — with no privileged
  region there is nothing to converge to, exactly as predicted.
- **Etapa 1 is performance-neutral** (−3.1% to +0.5% distances) despite
  measurably reducing overlap and radii.
- **Etapa 2 wins large**: −61% to −71% in both distances and page reads, in
  every regime, with the fat factor falling from 0.148 to 0.016–0.022.

### The control arm

`etapa2_data` runs the identical pipeline with identical |P| but pivots drawn
from the **data**, not the queries — essentially the VD-tree. Without it one
cannot tell "the queries showed us where to look" from "any spatially coherent
insertion order helps", and the first is what §4.4 claims.

It shows both: most of the gain is the insertion order, **but** query-derived
pivots add a further +18% (concentrado k-NN) and +29% (disperso k-NN) over
data-derived ones — and lose 17% under `uniforme`, the regime the thesis
already treats as adverse. The query derivation earns its keep precisely where
there is query structure to exploit.

## Known limitations

- **Index-level relocation is not implemented.** It is correct under the same
  common-parent argument, but Equation 4.3 would have to strengthen to
  `d(O_c,O_j) + r_c <= r_j`, which requires one sibling ball to dominate an
  entire subtree and is essentially never satisfiable in a balanced tree. The
  code counts `PairsSkippedNotLeaf` so the frequency is at least visible.
- **`stMemoryPageManager` is broken in this version of arboretum**: a
  `stSlimTree` built on one segfaults on the *second* insertion, reproducibly,
  independently of Chapter 4. Nothing else in the library uses it, which is
  presumably why it went unnoticed. `T_Q` therefore uses its own disk page
  manager — the accounting argument is about having a *separate* manager, not
  about avoiding disk.
- Only `RangeQuery` and `NearestQuery` are instrumented. The other query forms
  (`ListNearestQuery`, `KAndRangeQuery`, the incremental variants) have no
  trace hooks and so are invisible to Etapa 1.
