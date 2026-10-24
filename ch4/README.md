# Chapter 4 — adaptive Slim-tree

Implementation and experiment for Chapter 4 of the thesis: incorporating the
query workload into the organisation of a Slim-tree.

Two alternatives, which the thesis treats as mutually exclusive:

- **Etapa 1** (§4.3) — query-guided relocation, incremental, after every query.
- **Etapa 2** (§4.4) — a query tree `T_Q`, a Voronoi partition induced by the
  queries, and one batch reorganisation.

## Quick start

```bash
# everything: build, run the matrix, analyse (20 repetitions, ~10 minutes)
bash arboretum_SlimTree_QueryGravity/ch4/run_ch4.sh

# a fast smoke run
bash arboretum_SlimTree_QueryGravity/ch4/run_ch4.sh --quick

# one verification program (build + run, trace configured automatically)
bash arboretum_SlimTree_QueryGravity/ch4/build_ch4.sh phase4_check
```

Results land in `ch4/results/`; `SUMMARY.md` there is the readable report.

## Protocol

Eight arms, three regimes (`concentrado`, `disperso`, `uniforme`), k-NN and range:

| Arm | What is measured |
|---|---|
| `original` | Slim-tree built in a random insertion order |
| `slimdown` | `original` after the classic `Optimize()` |
| `etapa1` | `original` after query-guided relocation (§4.3) |
| `etapa2` | a new tree, inserted cell by cell from the Voronoi partition of the **query** pivots (§4.4) |
| `etapa2_data` | control: the `etapa2` pipeline with the same \|P\| pivots taken from the **data** |
| `hilbert` | reference: the cities inserted along a Hilbert curve — insertion order alone |
| `etapa2_bulk` | a new tree built **top-down**: the queries decide the partition of every level (`stSlimQueryBulkLoad`) |
| `etapa2_bulk_data` | control: the same code with the queries switched off |

- **Independent repetitions.** Repetition `r` runs with seed `42 + r`. The
  regions, the queries and the insertion order all change between
  repetitions; within one repetition every arm shares them, so arms are
  compared **paired**.
- **Regions.** A region is a metric ball: a centre drawn at random from the
  cities plus its nearest cities. Balls are disjoint
  (`d(c_i,c_j) > r_i + r_j`). Region size is `--region-factor` (default 3)
  times the queries drawn from it, so a third of each region is never queried.
- **Adapt / measure split.** Inside each region the cities are shuffled and
  split disjointly; every region contributes to both sets, and both sequences
  are shuffled so the regions arrive mixed. `uniforme` draws without
  replacement. No city is ever in both sets.
- **Reported, not hidden.** `measure_to_adapt` (mean distance from a measured
  query to the nearest adapt query) and `reorg_distances` (what each
  reorganisation cost) are in `aggregated.csv`; `workload.csv` lists every
  centre and query of every repetition.

This protocol replaced a first version (commit `ffa0d2e`) whose five
repetitions all used the same seed and so were identical, whose centres came
from farthest-point sampling and always landed on the borders of the country,
and whose adapt/measure split made every measured query the ranking neighbour
of an adapt query while covering only 1 of 2 (`concentrado`) or 4 of 8
(`disperso`) regions.

## Layout

Headers are in `arboretum/src/include/arboretum/` so they are includable as
`<arboretum/...>` like the rest of the library; drivers are here.

| File | What it is |
|---|---|
| `arboretum/.../stSlimQueryTrace.h` | The traversal trace. The **only** thing that had to go inside `stSlimTree`, as 17 one-line macro calls. Compiled out unless `ST_SLIM_TRACE=1`. |
| `arboretum/.../stSlimEtapa1.h` | Etapa 1. Equations 4.3–4.7. Works purely through the page manager; never includes `stSlimTree.h`. |
| `arboretum/.../stSlimVoronoiRebuild.h` | Etapa 2. `T_Q`, pivots, Equation 4.9, and the §4.4.2 navigation graph. |
| `arboretum/.../stSlimQueryBulkLoad.h` | Etapa 2, top-down variant: k-centre pivots from the queries and the data at every level, Voronoi cells, bottom-up representatives. Produces trees that are not height-balanced. |
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

20 independent repetitions × 3 regimes × 2 query types × 8 arms = 960 measured
batches, every one exact against brute force. Full tables in
`results/SUMMARY.md`; percentages below are paired against `original` and
`±` is the 95% confidence interval.

- **H2 supported.** The destination radius never grew, in any run.
- **H3 not supported.** Etapa 1 spends 25–42k distances adapting and never
  repays them within the 200 adapt queries; per query it saves 1–3%.
- **H4 supported.** Relocations per query decay under `concentrado`
  (7.5 → 5.6) and `disperso` (7.5 → 5.8) and stay nearly flat under
  `uniforme` (7.8 → 7.2).
- **Etapa 2 (insertion order) cuts 64–66% of the distances, but so does any
  spatially coherent order.** Against its data-pivot control the difference
  is within noise in 5 of 6 configurations (the exception: −6.3% ± 3.7,
  `concentrado` range), and the Hilbert order beats it by 4–13%.
- **The top-down variant is the best organisation measured**: −73% to −76%
  distances and −81% to −83% page reads against `original`; 15–24% fewer
  distances than Hilbert and 19–33% fewer than Etapa 2, in 19–20 of 20
  repetitions. It is also the cheapest reorganisation (≈190k distances against
  ≈345k for Etapa 2), so it repays itself in ≈630–780 queries instead of
  ≈1300–1600.
- **But the queries are not what makes it good.** `etapa2_bulk` and
  `etapa2_bulk_data` differ by 0–3%, within noise everywhere but `disperso`
  k-NN (−3.0% ± 2.6). The gain is the top-down construction itself.
- **The queries do change the shape, as designed.** Under `concentrado` the
  queried regions sit 0.37 ± 0.07 levels closer to the root than the rest of
  the tree; in the data-only control they sit 0.12 levels deeper. The
  shallower hot regions just do not translate into fewer distances at
  `QueryShare = 0.5`.

### Why the controls matter

The first version of this experiment (commit `ffa0d2e`) reported that query
pivots added +18% to +29% over data pivots. That came from the protocol, not
from the method: identical repetitions, centres on the borders of the
country, and every measured query paired with an adapt query next to it. With
independent repetitions and disjoint, region-balanced sets, the advantage
disappears. Every claim that a gain comes from the QUERIES needs an arm that
does the same thing without them.

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
- **The top-down trees are not height-balanced**, by design: queried regions
  can sit closer to the root. The Slim-tree queries do not depend on balance
  (they recurse by node type) and every answer is verified, but the fat
  factor assumes every leaf at depth H and is meaningless for those two arms
  — it even goes slightly negative. Inserting into a top-down tree afterwards
  with `Add()` has not been tested.
- **The top-down build writes pages directly** and hands the result to the tree
  with `stSlimTree::AdoptBulkTree()`, the one method added to the tree for it.
  Like `GetRootPageID()`, it is a member of a class template that generates
  no code unless called, so the byte-identity argument above should still
  hold; that has not been re-checked.
- Only `RangeQuery` and `NearestQuery` are instrumented. The other query forms
  (`ListNearestQuery`, `KAndRangeQuery`, the incremental variants) have no
  trace hooks and so are invisible to Etapa 1.
