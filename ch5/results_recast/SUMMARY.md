# Chapter 5, phase R0 - RECAST-style paid pivot columns in the Slim-tree

```
timestamp   : 2026-10-30T19:07:53-03:00
commit      : c350489
compiler    : g++.exe (Rev5, Built by MSYS2 project) 16.1.0
reps        : 20
seeds       : 42..61
node slots  : 2,4,8
adapt       : 200
measure     : 100
k           : 10
radius      : 0.5
page size   : 1024
region fact.: 3
extra       : 
dataset     : /c/Users/tataw/OneDrive/Desktop/Talia/Slim_Tree/arboretum_SlimTree_QueryGravity/arboretum/test-brcities-datastore/BrazilianCities.txt
query pool  : /c/Users/tataw/OneDrive/Desktop/Talia/Slim_Tree/arboretum_SlimTree_QueryGravity/arboretum/test-brcities-datastore/BrazilianCities500.txt
```

Repetitions: 20. Node slots K: 2, 4, 8.
All answers exact (brute force): **yes**. Replica = real tree: **yes**. Engine with empty tables = replica: **yes**.

Savings are in % of the Slim-tree (`none`) on the same queries, mean ± 95% CI over repetitions. Distances include every d(q,p) paid to a pivot. Differences between arms are paired by repetition, in percentage points (pp).

| arm | what |
|---|---|
| `none` | the Slim-tree as it is (replica, checked against the real tree) |
| `recast` | paid columns left by the adapt queries in the nodes they read; frozen for the measured queries |
| `recast_live` | the same tables, still learning during the measured queries |
| `recast_data` | CONTROL: the same mechanism, columns left by as many 'queries' on random cities (their cost is overhead) |
| `oracle_q` | F0 ceiling: full column of the nearest adapt query, found for free |
| `oracle_d` | control of oracle_q: nearest member of a random data sample |
| `pm_dfar4` | reference: 4 global data pivots with full columns (PM-tree style; installation and page space not charged) |

## Measured queries - mixed stream (Chapter 4 protocol)

### knn

| regime | Slim-tree dist / reads | recast K=2: dist % / reads % (d(q,p)) | recast K=4: dist % / reads % (d(q,p)) | recast K=8: dist % / reads % (d(q,p)) | recast_data K=8 | oracle_q | pm_dfar4 |
|---|---|---|---|---|---|---|---|
| concentrado | 401.2 / 70.9 | 35.1 ± 0.9 / 1.3 ± 0.3 (29.9) | 42.2 ± 1.3 / 2.6 ± 0.4 (51.9) | 46.3 ± 1.1 / 4.4 ± 0.5 (64.1) | 40.4 / 2.1 | 70.9 / 45.3 | 68.3 / 19.8 |
| disperso | 399.2 / 70.4 | 34.4 ± 0.9 / 1.2 ± 0.2 (31.4) | 41.8 ± 0.9 / 2.2 ± 0.2 (51.8) | 45.9 ± 0.7 / 4.0 ± 0.4 (64.8) | 40.0 / 2.0 | 70.7 / 44.4 | 68.6 / 20.4 |
| uniforme | 401.6 / 69.9 | 28.5 ± 0.8 / 0.6 ± 0.1 (34.1) | 35.7 ± 0.8 / 1.0 ± 0.2 (60.5) | 39.9 ± 0.9 / 1.8 ± 0.3 (79.3) | 40.3 / 1.9 | 65.5 / 33.4 | 68.7 / 20.2 |

### range

| regime | Slim-tree dist / reads | recast K=2: dist % / reads % (d(q,p)) | recast K=4: dist % / reads % (d(q,p)) | recast K=8: dist % / reads % (d(q,p)) | recast_data K=8 | oracle_q | pm_dfar4 |
|---|---|---|---|---|---|---|---|
| concentrado | 345.6 / 69.4 | 37.3 ± 1.5 / 1.5 ± 0.3 (27.2) | 47.5 ± 1.5 / 2.9 ± 0.5 (44.4) | 51.8 ± 1.5 / 4.9 ± 0.7 (57.4) | 48.4 / 2.1 | 80.2 / 47.0 | 75.7 / 20.6 |
| disperso | 342.6 / 68.7 | 38.0 ± 0.8 / 1.3 ± 0.3 (26.5) | 48.5 ± 0.8 / 2.6 ± 0.4 (40.7) | 53.3 ± 0.6 / 4.6 ± 0.5 (52.3) | 48.1 / 2.1 | 79.7 / 46.3 | 75.8 / 21.4 |
| uniforme | 343.8 / 68.1 | 31.0 ± 0.5 / 0.5 ± 0.1 (28.3) | 42.2 ± 0.6 / 1.1 ± 0.2 (45.2) | 48.0 ± 0.5 / 1.8 ± 0.3 (59.0) | 48.6 / 2.0 | 74.2 / 34.4 | 76.0 / 21.2 |

### Queries vs random cities, same mechanism (mixed)

`recast` minus `recast_data`, measured queries. Positive = the columns left by the queries prune more. n+ = repetitions where `recast` wins.

| regime | query | K | distances (pp) | n+ | reads (pp) | n+ |
|---|---|---|---|---|---|---|
| concentrado | knn | 2 | +5.9 ± 0.9 | 20/20 | +0.8 ± 0.2 | 19/20 |
| concentrado | knn | 4 | +6.3 ± 0.8 | 20/20 | +1.4 ± 0.3 | 20/20 |
| concentrado | knn | 8 | +5.9 ± 0.8 | 20/20 | +2.3 ± 0.4 | 20/20 |
| concentrado | range | 2 | +6.2 ± 1.1 | 20/20 | +0.7 ± 0.2 | 18/20 |
| concentrado | range | 4 | +4.9 ± 0.7 | 20/20 | +1.7 ± 0.4 | 19/20 |
| concentrado | range | 8 | +3.3 ± 0.8 | 19/20 | +2.8 ± 0.4 | 20/20 |
| disperso | knn | 2 | +5.3 ± 0.9 | 20/20 | +0.6 ± 0.3 | 18/20 |
| disperso | knn | 4 | +6.4 ± 0.7 | 20/20 | +1.1 ± 0.1 | 20/20 |
| disperso | knn | 8 | +5.8 ± 0.6 | 20/20 | +2.0 ± 0.3 | 20/20 |
| disperso | range | 2 | +6.8 ± 1.0 | 20/20 | +0.6 ± 0.3 | 18/20 |
| disperso | range | 4 | +6.2 ± 0.9 | 20/20 | +1.5 ± 0.3 | 20/20 |
| disperso | range | 8 | +5.3 ± 0.7 | 20/20 | +2.6 ± 0.5 | 20/20 |
| uniforme | knn | 2 | -0.3 ± 0.7 | 8/20 | +0.0 ± 0.1 | 11/20 |
| uniforme | knn | 4 | +0.3 ± 0.5 | 11/20 | -0.1 ± 0.2 | 8/20 |
| uniforme | knn | 8 | -0.4 ± 0.5 | 6/20 | -0.1 ± 0.2 | 7/20 |
| uniforme | range | 2 | -0.2 ± 0.9 | 8/20 | -0.1 ± 0.1 | 8/20 |
| uniforme | range | 4 | -0.6 ± 0.5 | 6/20 | -0.0 ± 0.1 | 10/20 |
| uniforme | range | 8 | -0.6 ± 0.5 | 6/20 | -0.2 ± 0.2 | 6/20 |

### Online, while the adapt queries build the tables (mixed)

| regime | query | K | distances % | reads % | d(q,p) per query |
|---|---|---|---|---|---|
| concentrado | knn | 2 | 35.6 ± 0.8 | 1.3 ± 0.2 | 23.0 |
| concentrado | knn | 4 | 44.5 ± 0.9 | 2.4 ± 0.3 | 34.8 |
| concentrado | knn | 8 | 48.8 ± 0.8 | 4.2 ± 0.4 | 43.4 |
| concentrado | range | 2 | 37.8 ± 1.4 | 1.4 ± 0.3 | 21.6 |
| concentrado | range | 4 | 49.0 ± 1.4 | 2.7 ± 0.4 | 31.2 |
| concentrado | range | 8 | 53.8 ± 1.3 | 4.6 ± 0.5 | 40.2 |
| disperso | knn | 2 | 33.7 ± 0.6 | 1.1 ± 0.1 | 23.7 |
| disperso | knn | 4 | 42.3 ± 0.7 | 2.0 ± 0.2 | 34.9 |
| disperso | knn | 8 | 46.6 ± 0.7 | 3.6 ± 0.3 | 43.2 |
| disperso | range | 2 | 36.6 ± 0.7 | 1.2 ± 0.1 | 20.9 |
| disperso | range | 4 | 47.6 ± 0.7 | 2.3 ± 0.2 | 29.1 |
| disperso | range | 8 | 52.6 ± 0.6 | 4.1 ± 0.4 | 37.0 |
| uniforme | knn | 2 | 28.1 ± 0.6 | 0.6 ± 0.1 | 25.0 |
| uniforme | knn | 4 | 36.2 ± 0.7 | 1.0 ± 0.2 | 40.0 |
| uniforme | knn | 8 | 41.2 ± 0.8 | 1.7 ± 0.2 | 50.2 |
| uniforme | range | 2 | 29.5 ± 0.6 | 0.6 ± 0.1 | 21.7 |
| uniforme | range | 4 | 40.6 ± 0.5 | 1.0 ± 0.2 | 31.8 |
| uniforme | range | 8 | 46.5 ± 0.5 | 1.8 ± 0.3 | 40.9 |

### Still learning vs frozen, and share of the F0 ceiling (mixed)

| regime | query | K | live - frozen, distances (pp) | recast / oracle_q savings |
|---|---|---|---|---|
| concentrado | knn | 2 | -0.2 ± 0.5 | 50% |
| concentrado | knn | 4 | -0.9 ± 0.3 | 60% |
| concentrado | knn | 8 | -1.2 ± 0.2 | 65% |
| concentrado | range | 2 | -0.1 ± 0.4 | 47% |
| concentrado | range | 4 | -0.8 ± 0.4 | 59% |
| concentrado | range | 8 | -1.2 ± 0.3 | 65% |
| disperso | knn | 2 | -0.5 ± 0.4 | 49% |
| disperso | knn | 4 | -1.0 ± 0.2 | 59% |
| disperso | knn | 8 | -1.4 ± 0.2 | 65% |
| disperso | range | 2 | -0.7 ± 0.3 | 48% |
| disperso | range | 4 | -1.0 ± 0.3 | 61% |
| disperso | range | 8 | -1.2 ± 0.2 | 67% |
| uniforme | knn | 2 | +0.1 ± 0.4 | 44% |
| uniforme | knn | 4 | -0.9 ± 0.3 | 55% |
| uniforme | knn | 8 | -1.2 ± 0.3 | 61% |
| uniforme | range | 2 | +0.0 ± 0.3 | 42% |
| uniforme | range | 4 | -0.6 ± 0.3 | 57% |
| uniforme | range | 8 | -1.0 ± 0.2 | 65% |

## Measured queries - salto: adapt queries region by region (the focus jumps)

### knn

| regime | Slim-tree dist / reads | recast K=2: dist % / reads % (d(q,p)) | recast K=4: dist % / reads % (d(q,p)) | recast K=8: dist % / reads % (d(q,p)) | recast_data K=8 | oracle_q | pm_dfar4 |
|---|---|---|---|---|---|---|---|
| concentrado | 401.2 / 70.9 | 32.5 ± 0.9 / 1.3 ± 0.3 (33.4) | 39.9 ± 0.9 / 2.6 ± 0.4 (52.8) | 44.7 ± 1.1 / 4.2 ± 0.6 (64.4) | 40.4 / 2.1 | 70.9 / 45.3 | 68.3 / 19.8 |
| disperso | 399.2 / 70.4 | 30.6 ± 1.1 / 1.1 ± 0.2 (38.4) | 39.4 ± 0.9 / 2.3 ± 0.3 (50.8) | 44.4 ± 0.8 / 4.1 ± 0.4 (63.3) | 40.0 / 2.0 | 70.7 / 44.4 | 68.6 / 20.4 |

### range

| regime | Slim-tree dist / reads | recast K=2: dist % / reads % (d(q,p)) | recast K=4: dist % / reads % (d(q,p)) | recast K=8: dist % / reads % (d(q,p)) | recast_data K=8 | oracle_q | pm_dfar4 |
|---|---|---|---|---|---|---|---|
| concentrado | 345.6 / 69.4 | 34.6 ± 1.5 / 1.7 ± 0.4 (32.1) | 43.7 ± 1.6 / 2.9 ± 0.5 (49.2) | 49.0 ± 1.8 / 4.6 ± 0.6 (60.2) | 48.4 / 2.1 | 80.2 / 47.0 | 75.7 / 20.6 |
| disperso | 342.6 / 68.7 | 32.1 ± 1.1 / 1.3 ± 0.3 (35.8) | 43.3 ± 0.9 / 2.5 ± 0.3 (46.9) | 49.4 ± 0.8 / 4.2 ± 0.5 (58.1) | 48.1 / 2.1 | 79.7 / 46.3 | 75.8 / 21.4 |

### Queries vs random cities, same mechanism (salto)

`recast` minus `recast_data`, measured queries. Positive = the columns left by the queries prune more. n+ = repetitions where `recast` wins.

| regime | query | K | distances (pp) | n+ | reads (pp) | n+ |
|---|---|---|---|---|---|---|
| concentrado | knn | 2 | +3.2 ± 1.0 | 19/20 | +0.8 ± 0.2 | 19/20 |
| concentrado | knn | 4 | +4.0 ± 0.9 | 19/20 | +1.4 ± 0.3 | 20/20 |
| concentrado | knn | 8 | +4.3 ± 0.9 | 20/20 | +2.1 ± 0.5 | 19/20 |
| concentrado | range | 2 | +3.5 ± 1.2 | 17/20 | +0.9 ± 0.3 | 19/20 |
| concentrado | range | 4 | +1.1 ± 0.8 | 14/20 | +1.8 ± 0.4 | 19/20 |
| concentrado | range | 8 | +0.6 ± 1.0 | 11/20 | +2.5 ± 0.4 | 20/20 |
| disperso | knn | 2 | +1.4 ± 1.2 | 15/20 | +0.6 ± 0.2 | 16/20 |
| disperso | knn | 4 | +4.0 ± 0.7 | 20/20 | +1.2 ± 0.2 | 20/20 |
| disperso | knn | 8 | +4.4 ± 0.7 | 20/20 | +2.1 ± 0.3 | 20/20 |
| disperso | range | 2 | +0.9 ± 1.2 | 12/20 | +0.6 ± 0.3 | 17/20 |
| disperso | range | 4 | +1.1 ± 0.8 | 14/20 | +1.3 ± 0.3 | 19/20 |
| disperso | range | 8 | +1.4 ± 0.7 | 15/20 | +2.2 ± 0.3 | 20/20 |

### Online, while the adapt queries build the tables (salto)

| regime | query | K | distances % | reads % | d(q,p) per query |
|---|---|---|---|---|---|
| concentrado | knn | 2 | 37.7 ± 0.8 | 1.8 ± 0.3 | 22.8 |
| concentrado | knn | 4 | 46.4 ± 0.9 | 3.3 ± 0.4 | 32.3 |
| concentrado | knn | 8 | 50.9 ± 0.8 | 5.4 ± 0.4 | 38.1 |
| concentrado | range | 2 | 40.4 ± 1.4 | 2.1 ± 0.4 | 22.0 |
| concentrado | range | 4 | 50.7 ± 1.5 | 3.8 ± 0.5 | 30.4 |
| concentrado | range | 8 | 55.7 ± 1.3 | 6.1 ± 0.6 | 36.6 |
| disperso | knn | 2 | 41.3 ± 0.6 | 3.2 ± 0.3 | 22.8 |
| disperso | knn | 4 | 47.8 ± 0.6 | 4.8 ± 0.4 | 28.9 |
| disperso | knn | 8 | 50.8 ± 0.6 | 7.4 ± 0.4 | 33.8 |
| disperso | range | 2 | 45.3 ± 0.7 | 3.8 ± 0.3 | 21.8 |
| disperso | range | 4 | 52.5 ± 0.6 | 5.4 ± 0.4 | 27.9 |
| disperso | range | 8 | 56.0 ± 0.6 | 8.0 ± 0.5 | 32.7 |

### Still learning vs frozen, and share of the F0 ceiling (salto)

| regime | query | K | live - frozen, distances (pp) | recast / oracle_q savings |
|---|---|---|---|---|
| concentrado | knn | 2 | +1.1 ± 0.8 | 46% |
| concentrado | knn | 4 | +0.7 ± 0.5 | 56% |
| concentrado | knn | 8 | +0.4 ± 0.3 | 63% |
| concentrado | range | 2 | +1.2 ± 0.6 | 43% |
| concentrado | range | 4 | +1.1 ± 0.5 | 54% |
| concentrado | range | 8 | +0.9 ± 0.4 | 61% |
| disperso | knn | 2 | +1.2 ± 0.6 | 43% |
| disperso | knn | 4 | +0.5 ± 0.4 | 56% |
| disperso | knn | 8 | +0.4 ± 0.5 | 63% |
| disperso | range | 2 | +2.1 ± 0.7 | 40% |
| disperso | range | 4 | +1.2 ± 0.4 | 54% |
| disperso | range | 8 | +0.9 ± 0.4 | 62% |

## The focus jumps: savings per block of adapt queries (salto)

Mean over repetitions of the distance saving of each block of consecutive adapt queries; `region` is the region the block queries. Every jump starts in nodes no earlier query has read, so the first block of a region starts from (almost) empty tables.

**concentrado, knn, K=8**

| block | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| region | 0 | 0 | 0 | 0 | 1 | 1 | 1 | 1 |
| saving % | 48.3 | 55.0 | 53.7 | 52.2 | 44.6 | 51.7 | 50.7 | 49.4 |

**concentrado, range, K=8**

| block | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| region | 0 | 0 | 0 | 0 | 1 | 1 | 1 | 1 |
| saving % | 52.1 | 59.5 | 58.2 | 56.5 | 49.1 | 57.1 | 55.9 | 54.8 |

**disperso, knn, K=8**

| block | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| region | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
| saving % | 54.4 | 51.6 | 51.6 | 50.8 | 50.6 | 49.0 | 46.1 | 48.3 |

**disperso, range, K=8**

| block | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| region | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
| saving % | 59.2 | 57.0 | 57.0 | 55.4 | 54.1 | 54.3 | 49.8 | 52.6 |

## What the tables hold at the end of the adapt phase (mixed)

| regime | query | K | pivots (queries kept) | slots | intervals | confirmed | admitted | evicted | refused |
|---|---|---|---|---|---|---|---|---|---|
| concentrado | knn | 2 | 200 | 390 | 6408 | 242 | 7806 | 7415 | 2893 |
| concentrado | knn | 4 | 200 | 738 | 12141 | 495 | 8312 | 7574 | 962 |
| concentrado | knn | 8 | 200 | 1330 | 22068 | 750 | 7978 | 6648 | 53 |
| concentrado | range | 2 | 200 | 375 | 6183 | 238 | 7463 | 7088 | 2659 |
| concentrado | range | 4 | 200 | 706 | 11710 | 488 | 7373 | 6667 | 820 |
| concentrado | range | 8 | 200 | 1251 | 20987 | 749 | 6486 | 5235 | 40 |
| disperso | knn | 2 | 200 | 584 | 9618 | 365 | 7632 | 7048 | 3061 |
| disperso | knn | 4 | 200 | 1108 | 18416 | 741 | 8214 | 7106 | 1124 |
| disperso | knn | 8 | 200 | 1977 | 33435 | 1117 | 8182 | 6204 | 55 |
| disperso | range | 2 | 200 | 562 | 9336 | 352 | 7304 | 6742 | 2752 |
| disperso | range | 4 | 200 | 1059 | 17801 | 734 | 7371 | 6311 | 927 |
| disperso | range | 8 | 200 | 1847 | 31674 | 1120 | 6770 | 4923 | 47 |
| uniforme | knn | 2 | 200 | 730 | 11540 | 441 | 8093 | 7363 | 3068 |
| uniforme | knn | 4 | 200 | 1426 | 22821 | 948 | 8677 | 7250 | 1322 |
| uniforme | knn | 8 | 200 | 2613 | 43081 | 1477 | 8787 | 6174 | 119 |
| uniforme | range | 2 | 200 | 711 | 11354 | 432 | 8035 | 7324 | 2747 |
| uniforme | range | 4 | 200 | 1378 | 22366 | 925 | 8246 | 6867 | 1084 |
| uniforme | range | 8 | 200 | 2461 | 41449 | 1433 | 7818 | 5357 | 78 |

## Hypotheses (fixed before the 20-repetition run)

- **H-R0.1** `recast` costs fewer distances than the Slim-tree (CI above 0), in every regime and query type, for some K: **holds**.
- **H-R0.2** the columns of the queries beat the columns of random cities by at least 5 pp of distances (paired CI above 0) in concentrado AND disperso, both query types, for some K: **holds**.
  - concentrado, knn: K = 2, 4, 8
  - concentrado, range: K = 2
  - disperso, knn: K = 2, 4, 8
  - disperso, range: K = 2, 4, 8
- **H-R0.3** in uniforme the two are within noise (descriptive): knn K=2: -0.3 ± 0.7 pp; knn K=4: +0.3 ± 0.5 pp; knn K=8: -0.4 ± 0.5 pp; range K=2: -0.2 ± 0.9 pp; range K=4: -0.6 ± 0.5 pp; range K=8: -0.6 ± 0.5 pp.
- **H-R0.4** (descriptive) page reads saved by `recast`, K=8: concentrado/knn 4.4%; concentrado/range 4.9%; disperso/knn 4.0%; disperso/range 4.6%; uniforme/knn 1.8%; uniforme/range 1.8%.

