# Chapter 5 - pivots given by the queries

Plan: query centres become **pivots**. Their distances to the data are stored in
the tree and used (1) to prune subtrees and objects without computing a distance
and (2) to guide insertion. A pivot stays while the queries concentrate near it;
when it stops pruning, it is replaced. Background: RECAST (Liu & Mao, 2026),
Ciaccia, Patella & Zezula (PODS'98) and Ciaccia, Nanni & Patella (ADC'99,
"witnesses").

The plan starts with a cheap pilot (phase F0) that measures the potential
**before** pivots are wired into the pages, insertion and splits of the
Slim-tree, because in Chapter 4 the gain credited to the queries vanished once a
data-only control arm was added.

## Phase F0 - `qp_oracle.cpp`

Same seeds, regions, queries, insertion order and tree as the Chapter 4
`original` arm: `chapter4.cpp` is included as is, with its `main` renamed away,
so the workload cannot drift.

For every measured query:

1. the real Slim-tree query gives the true distance and page counts;
2. a node-by-node replica of the same search, on an in-memory snapshot of the
   tree, must reproduce those counts exactly (`replica_exact`);
3. the replica is rerun with a pivot filter for each pivot set. The filter is
   applied where a pivot-enabled tree would apply it: after the
   parent-representative filter, before the full distance.
   - Leaf entry x: prune if `|d(q,p) - d(x,p)| > r`.
   - Index entry e: prune if `d(q,p) + r < lo_e(p)` or `d(q,p) - r > hi_e(p)`,
     where `[lo_e, hi_e]` is the ring of `d(y,p)` over the subtree of e.
   - In k-NN, r is the current radius r_k.

Every answer is checked against brute force, and the S distances d(q,p) are
charged to the query. The space the pivot values would take in a page is not
modelled: the pilot isolates pruning on the same tree.

Pivot sets, S = 1..4 each:

| set | what |
|---|---|
| `q_medoids` | k-medoids of the adapt queries (the concentration points) |
| `q_kcenter` | farthest-first among the adapt queries |
| `d_far` | farthest-first over the data, from the border (outlier pivots) |
| `d_medoids` | k-medoids of a random data sample |
| `d_random` | random data objects |
| `q_nearest` | ORACLE, S=1: per query, its nearest adapt query |
| `d_nearest` | control: nearest member of a random data sample of the same size |
| `q_nearest_dfar` | ORACLE: nearest adapt query + (S-1) `d_far` |
| `d_nearest_dfar` | control: nearest data-sample member + (S-1) `d_far` |

It also computes HV, the "homogeneity of viewpoints" of PODS'98 (Def. 2), for
the data set.

## Running

From the folder that holds `arboretum_SlimTree_QueryGravity/`, in Git Bash:

```bash
# full pilot: 20 repetitions x 3 regimes, about 30 s, then the analysis
bash arboretum_SlimTree_QueryGravity/ch5/run_pilot.sh

# smoke run: 2 repetitions, 100 adapt / 50 measured queries
bash arboretum_SlimTree_QueryGravity/ch5/run_pilot.sh --quick --results /tmp/pilot

# build only
NO_RUN=1 bash arboretum_SlimTree_QueryGravity/ch5/build_ch5.sh qp_oracle
```

Outputs go to `ch5/results_pilot/`:

| file | contents |
|---|---|
| `pilot.csv` | one row per (repetition, regime, query type, set, S) |
| `summary.csv` | the same, averaged over repetitions |
| `paired.csv` | the pre-registered paired comparisons |
| `pivots.csv` | where every pivot is |
| `hv.csv` | homogeneity of viewpoints |
| `SUMMARY.md` | tables and the go/no-go verdict |

## Phase R0 - `qp_recast.cpp`: RECAST-style paid columns per node

F0 showed that a pivot helps when it sits next to the query, but its local
pivot was an oracle. It assumed 200 full columns and found the nearest one for
free. R0 measures the realistic version, after RECAST (Liu & Mao, arXiv
2608.06962). No pivot is installed and no global pivot is used.

**Paid columns.** When a query finishes, every node it read may keep one column
for it. The column is made of what the query already computed: for each entry,
an interval that contains the distance from the query to every object of the
entry.

| entry | interval |
|---|---|
| leaf, distance computed | exact |
| index, distance computed | ± the covering radius, tightened by the child's intervals when the query went down |
| skipped by the parent filter | the triangle inequality through the node representative |
| pruned by another pivot | the bound that pivot gave |

Building the columns costs no distance.

**Pruning.** A later query prunes an entry with pivot p when `d(q,p) + r < lo` or
`d(q,p) - r > hi`.
- d(q,p) is paid once per query and shared by all nodes that hold p.
- It is not paid at all when the node representative already shows that p
  cannot prune the entry.
- In k-NN, r is the current r_k.

**Life cycle, per node** (RECAST Sec. III-B/C):
- **Admission:** the query computed distances in the node and some of them were
  false positives. The column enters as a trial pivot.
- **Gain:** the work the pivot pruned, minus its share of d(q,p). Forgotten at
  `--decay` per visit of the node.
- **Confirmation:** a trial pivot is confirmed when its gain exceeds `--tau`.
- **Eviction, with K slots per node:**
  - while the node's cost is falling, only a useless trial may leave;
  - on a cost spike (κ = 4 times the moving average), the oldest pivot leaves;
  - otherwise, the least efficient pivot leaves.

The tree is static: R0 does not model insertion, and the tables live in memory,
as in RECAST. Their size is reported.

**Arms.** All arms run on the same tree and queries as F0 and Chapter 4.

| arm | what |
|---|---|
| `none` | the Slim-tree (replica, checked against the real tree) |
| `recast` | tables built online by the 200 adapt queries, frozen for the 100 measured ones |
| `recast_live` | the same tables, still learning during the measured queries |
| `recast_data` | **control**: the same mechanism, columns left by 200 "queries" on random cities, whose cost is overhead |
| `oracle_q`, `oracle_d` | the F0 local oracle and its control (ceiling) |
| `pm_dfar4` | 4 global data pivots, full columns (reference; installation not charged) |

**Runs.** Each run uses K ∈ {2, 4, 8} and the three regimes with the Chapter 4
stream. There is also a `salto` order for concentrado and disperso: the same
adapt queries, region by region, so the focus jumps.

**Hypotheses.** These were written after a one-repetition smoke run and before
the 20-repetition run.
- **H-R0.1:** `recast` costs fewer distances than the Slim-tree (CI above 0), in
  every regime and query type, for some K.
- **H-R0.2:** `recast` beats `recast_data` by ≥ 5 pp of distances (paired CI
  above 0), in concentrado and disperso, both query types, for some K. This is
  the claim that the gain comes from the queries; the threshold is the F0 gate.
- **H-R0.3** (descriptive): in uniforme, `recast` and `recast_data` are within
  noise.
- **H-R0.4** (descriptive): page reads saved. F0's oracle saved about 45%, but
  paid columns only bound unexplored subtrees by ± the covering radius, so a
  much smaller number is expected.

```bash
# full run: 20 repetitions x 5 (regime, order) runs, about 2 minutes, then the analysis
bash arboretum_SlimTree_QueryGravity/ch5/run_recast.sh

# smoke run
bash arboretum_SlimTree_QueryGravity/ch5/run_recast.sh --quick --results /tmp/r0
```

Outputs go to `ch5/results_recast/`:

| file | contents |
|---|---|
| `recast.csv` | one row per (repetition, regime, order, query type, arm, phase, K) |
| `online.csv` | the adapt phase in blocks of 25 queries, for the curves |
| `summary.csv` | the same, averaged over repetitions |
| `paired.csv` | the paired comparisons |
| `SUMMARY.md` | tables and the verdict on each hypothesis |

## Toolchain

The toolchain notes of `ch4/README.md` apply unchanged: PATH order, TMP for
make, and Smart App Control, which can block a freshly built `.exe` for a few
minutes.
