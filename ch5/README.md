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

The toolchain notes of `ch4/README.md` apply unchanged: PATH order, TMP for
make, and Smart App Control, which can block a freshly built `.exe` for a few
minutes.
