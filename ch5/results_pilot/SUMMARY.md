# Chapter 5, phase F0 - pivot pilot (qp_oracle)

20 repetition(s), concentrado, disperso, uniforme; S = 1..4 pivots per set. Same seeds, regions, queries, insertion order and tree as the Chapter 4 `original` arm.

- Replica of the Slim-tree search reproduces the real tree's distance and page counts on every query: **yes**
- Every answer, with and without pivots, matches brute force: **yes**

Savings are PAIRED per repetition against the same tree without pivots, and include the cost of the pivots (S distances d(q,p) per query). `±` is the 95% CI half-width over repetitions. The space pivot values would take in a page is NOT modelled: this measures pruning on the same tree.

## What each set is

- **q_medoids** - k-medoids of the adapt queries (the concentration points)
- **q_kcenter** - farthest-first among the adapt queries, from their 1-medoid
- **d_far** - farthest-first over the data, from the border (outlier pivots)
- **d_medoids** - k-medoids of a random data sample (central, query-blind)
- **d_random** - random data objects
- **q_nearest** - ORACLE, S=1: per query, its nearest adapt query
- **d_nearest** - control of q_nearest: nearest member of a random data sample of the same size
- **q_nearest_dfar** - ORACLE: nearest adapt query + (S-1) d_far pivots
- **d_nearest_dfar** - control of q_nearest_dfar: nearest member of the random data sample + (S-1) d_far pivots

## Homogeneity of viewpoints (Ciaccia, Patella & Zezula, PODS'98)

HV = **0.915** over 200 objects (mean discrepancy 0.085; p10/p50/p90 = 0.026 / 0.074 / 0.152; d+ = 38.1). Their real and synthetic data sets all had HV > 0.98: the lower the HV, the more objects 'see' the data differently, and the more the position of a pivot can matter.

## Baseline (no pivots)

| regime | k-NN distances | k-NN reads | range distances | range reads |
|---|---|---|---|---|
| concentrado | 401.2 | 70.9 | 345.6 | 69.4 |
| disperso | 399.2 | 70.4 | 342.6 | 68.7 |
| uniforme | 401.6 | 69.9 | 343.8 | 68.1 |

## Distance savings (%), pivot cost included

| set | S | concentrado knn | concentrado range | disperso knn | disperso range | uniforme knn | uniforme range |
|---|---|---|---|---|---|---|---|
| q_medoids | 1 | 52.2 ± 1.0 | 59.8 ± 0.9 | 50.7 ± 0.6 | 57.6 ± 0.7 | 49.0 ± 0.5 | 55.9 ± 0.6 |
| q_medoids | 2 | 64.5 ± 0.5 | 72.8 ± 0.5 | 63.8 ± 0.6 | 71.1 ± 0.6 | 62.5 ± 0.4 | 69.8 ± 0.5 |
| q_medoids | 3 | 66.9 ± 0.6 | 74.9 ± 0.7 | 67.3 ± 0.8 | 74.7 ± 0.8 | 68.0 ± 0.5 | 75.2 ± 0.4 |
| q_medoids | 4 | 68.1 ± 0.6 | 76.0 ± 0.7 | 68.7 ± 0.7 | 76.1 ± 0.7 | 68.9 ± 0.4 | 76.2 ± 0.3 |
| q_kcenter | 1 | 52.2 ± 1.0 | 59.8 ± 0.9 | 50.7 ± 0.6 | 57.6 ± 0.7 | 49.0 ± 0.5 | 55.9 ± 0.6 |
| q_kcenter | 2 | 63.2 ± 0.6 | 70.8 ± 0.7 | 63.5 ± 0.6 | 70.7 ± 0.6 | 63.8 ± 0.4 | 70.9 ± 0.4 |
| q_kcenter | 3 | 66.2 ± 0.7 | 73.8 ± 0.7 | 67.3 ± 0.7 | 74.7 ± 0.7 | 68.0 ± 0.4 | 75.2 ± 0.4 |
| q_kcenter | 4 | 67.7 ± 0.8 | 75.4 ± 0.8 | 68.8 ± 0.7 | 76.1 ± 0.6 | 69.1 ± 0.4 | 76.3 ± 0.4 |
| d_far | 1 | 52.7 ± 1.1 | 59.7 ± 1.1 | 52.9 ± 0.9 | 59.7 ± 0.9 | 52.8 ± 0.6 | 59.7 ± 0.8 |
| d_far | 2 | 65.2 ± 1.0 | 72.9 ± 0.7 | 65.7 ± 0.5 | 73.2 ± 0.4 | 66.0 ± 0.3 | 73.5 ± 0.2 |
| d_far | 3 | 68.2 ± 0.6 | 75.7 ± 0.6 | 68.4 ± 0.5 | 75.6 ± 0.4 | 68.5 ± 0.3 | 75.8 ± 0.3 |
| d_far | 4 | 68.3 ± 0.6 | 75.7 ± 0.6 | 68.6 ± 0.5 | 75.8 ± 0.4 | 68.7 ± 0.3 | 76.0 ± 0.3 |
| d_medoids | 1 | 48.6 ± 1.2 | 55.3 ± 1.3 | 49.0 ± 0.7 | 55.7 ± 0.8 | 48.8 ± 0.6 | 55.7 ± 0.6 |
| d_medoids | 2 | 62.4 ± 0.8 | 70.0 ± 1.0 | 62.5 ± 0.5 | 69.6 ± 0.5 | 62.4 ± 0.4 | 69.7 ± 0.4 |
| d_medoids | 3 | 67.8 ± 0.8 | 75.3 ± 0.8 | 68.0 ± 0.5 | 75.1 ± 0.5 | 68.0 ± 0.5 | 75.2 ± 0.4 |
| d_medoids | 4 | 69.1 ± 0.6 | 76.6 ± 0.7 | 69.0 ± 0.5 | 76.2 ± 0.5 | 69.1 ± 0.3 | 76.3 ± 0.3 |
| d_random | 1 | 50.6 ± 1.2 | 57.5 ± 1.2 | 51.3 ± 0.9 | 58.2 ± 0.9 | 51.1 ± 0.8 | 58.1 ± 1.0 |
| d_random | 2 | 62.2 ± 1.4 | 69.7 ± 1.5 | 62.7 ± 1.3 | 70.0 ± 1.2 | 62.7 ± 1.0 | 70.1 ± 1.0 |
| d_random | 3 | 65.2 ± 1.7 | 72.7 ± 1.9 | 66.0 ± 1.1 | 73.2 ± 1.2 | 66.1 ± 1.0 | 73.4 ± 1.0 |
| d_random | 4 | 66.7 ± 1.2 | 74.2 ± 1.3 | 67.2 ± 0.9 | 74.4 ± 0.9 | 67.3 ± 0.7 | 74.6 ± 0.8 |
| q_nearest | 1 | 70.9 ± 0.6 | 80.2 ± 0.8 | 70.7 ± 0.6 | 79.7 ± 0.6 | 65.5 ± 0.5 | 74.2 ± 0.4 |
| d_nearest | 1 | 66.0 ± 0.8 | 75.0 ± 0.9 | 65.8 ± 0.6 | 74.3 ± 0.5 | 65.8 ± 0.5 | 74.5 ± 0.5 |
| q_nearest_dfar | 2 | 72.2 ± 0.6 | 81.4 ± 0.8 | 72.1 ± 0.6 | 80.9 ± 0.6 | 69.3 ± 0.4 | 77.8 ± 0.3 |
| q_nearest_dfar | 3 | 72.8 ± 0.6 | 81.9 ± 0.7 | 72.8 ± 0.5 | 81.6 ± 0.5 | 71.1 ± 0.3 | 79.5 ± 0.3 |
| q_nearest_dfar | 4 | 73.1 ± 0.6 | 82.1 ± 0.8 | 73.0 ± 0.5 | 81.7 ± 0.5 | 71.6 ± 0.3 | 79.8 ± 0.3 |
| d_nearest_dfar | 2 | 69.6 ± 0.6 | 78.4 ± 0.8 | 69.5 ± 0.5 | 77.9 ± 0.4 | 69.5 ± 0.4 | 78.0 ± 0.4 |
| d_nearest_dfar | 3 | 71.1 ± 0.7 | 79.8 ± 0.6 | 71.1 ± 0.5 | 79.4 ± 0.4 | 71.3 ± 0.3 | 79.7 ± 0.3 |
| d_nearest_dfar | 4 | 71.7 ± 0.6 | 80.2 ± 0.7 | 71.6 ± 0.5 | 79.7 ± 0.4 | 71.8 ± 0.3 | 79.9 ± 0.3 |

## Page-read savings (%)

| set | S | concentrado knn | concentrado range | disperso knn | disperso range | uniforme knn | uniforme range |
|---|---|---|---|---|---|---|---|
| q_medoids | 1 | 10.1 ± 0.8 | 10.4 ± 0.7 | 8.8 ± 0.5 | 9.2 ± 0.6 | 6.9 ± 0.4 | 7.5 ± 0.4 |
| q_medoids | 2 | 20.0 ± 1.1 | 20.7 ± 1.1 | 16.3 ± 0.9 | 16.9 ± 0.9 | 13.5 ± 0.4 | 14.2 ± 0.4 |
| q_medoids | 3 | 23.3 ± 1.0 | 24.0 ± 1.2 | 20.5 ± 0.9 | 21.3 ± 1.0 | 18.3 ± 0.6 | 19.2 ± 0.6 |
| q_medoids | 4 | 25.5 ± 1.1 | 26.2 ± 1.4 | 23.6 ± 1.2 | 24.4 ± 1.2 | 20.9 ± 0.7 | 21.9 ± 0.7 |
| q_kcenter | 1 | 10.1 ± 0.8 | 10.4 ± 0.7 | 8.8 ± 0.5 | 9.2 ± 0.6 | 6.9 ± 0.4 | 7.5 ± 0.4 |
| q_kcenter | 2 | 16.9 ± 0.9 | 17.6 ± 0.9 | 15.5 ± 0.6 | 16.1 ± 0.6 | 13.4 ± 0.6 | 14.1 ± 0.6 |
| q_kcenter | 3 | 20.6 ± 1.0 | 21.5 ± 1.1 | 20.0 ± 0.8 | 20.8 ± 0.8 | 18.1 ± 0.6 | 19.0 ± 0.5 |
| q_kcenter | 4 | 23.7 ± 1.0 | 24.7 ± 1.1 | 23.0 ± 0.8 | 23.9 ± 0.9 | 20.8 ± 0.7 | 21.7 ± 0.6 |
| d_far | 1 | 8.9 ± 1.2 | 9.2 ± 1.2 | 9.0 ± 0.9 | 9.4 ± 0.9 | 8.9 ± 0.5 | 9.2 ± 0.6 |
| d_far | 2 | 14.7 ± 0.9 | 15.2 ± 1.0 | 15.0 ± 0.7 | 15.7 ± 0.6 | 15.0 ± 0.6 | 15.6 ± 0.6 |
| d_far | 3 | 18.7 ± 1.2 | 19.5 ± 1.4 | 19.1 ± 0.8 | 19.9 ± 0.8 | 19.0 ± 0.7 | 19.7 ± 0.7 |
| d_far | 4 | 19.8 ± 1.3 | 20.6 ± 1.5 | 20.4 ± 0.9 | 21.4 ± 1.0 | 20.2 ± 0.8 | 21.2 ± 0.8 |
| d_medoids | 1 | 6.4 ± 0.9 | 6.8 ± 0.9 | 6.9 ± 0.7 | 7.3 ± 0.8 | 7.0 ± 0.4 | 7.6 ± 0.4 |
| d_medoids | 2 | 14.1 ± 0.9 | 14.8 ± 1.1 | 13.6 ± 0.9 | 14.2 ± 0.9 | 13.6 ± 0.5 | 14.3 ± 0.5 |
| d_medoids | 3 | 18.6 ± 1.1 | 19.4 ± 1.3 | 18.4 ± 1.0 | 19.2 ± 1.0 | 18.3 ± 0.8 | 19.2 ± 0.8 |
| d_medoids | 4 | 21.9 ± 1.3 | 22.9 ± 1.6 | 21.2 ± 1.1 | 22.1 ± 1.2 | 21.3 ± 0.6 | 22.2 ± 0.7 |
| d_random | 1 | 7.5 ± 0.9 | 7.8 ± 0.9 | 8.3 ± 0.6 | 8.7 ± 0.6 | 8.5 ± 0.6 | 9.0 ± 0.6 |
| d_random | 2 | 13.4 ± 1.4 | 14.0 ± 1.4 | 13.8 ± 1.0 | 14.4 ± 1.1 | 13.9 ± 0.7 | 14.5 ± 0.7 |
| d_random | 3 | 16.6 ± 1.9 | 17.4 ± 2.0 | 17.1 ± 1.3 | 17.9 ± 1.3 | 17.2 ± 0.9 | 18.1 ± 0.9 |
| d_random | 4 | 18.7 ± 1.8 | 19.6 ± 1.9 | 19.2 ± 1.3 | 20.0 ± 1.3 | 19.2 ± 1.0 | 20.0 ± 1.0 |
| q_nearest | 1 | 45.3 ± 1.3 | 47.0 ± 2.0 | 44.4 ± 1.0 | 46.3 ± 1.4 | 33.4 ± 0.9 | 34.4 ± 0.9 |
| d_nearest | 1 | 35.1 ± 1.4 | 36.1 ± 1.6 | 33.9 ± 1.1 | 34.8 ± 1.1 | 34.2 ± 1.0 | 35.2 ± 1.0 |
| q_nearest_dfar | 2 | 45.9 ± 1.3 | 47.6 ± 2.1 | 45.1 ± 1.1 | 46.9 ± 1.4 | 35.0 ± 0.9 | 36.0 ± 0.8 |
| q_nearest_dfar | 3 | 46.4 ± 1.3 | 48.1 ± 2.1 | 45.6 ± 1.0 | 47.4 ± 1.4 | 36.3 ± 0.9 | 37.3 ± 0.9 |
| q_nearest_dfar | 4 | 46.7 ± 1.3 | 48.5 ± 2.1 | 45.9 ± 1.0 | 47.8 ± 1.4 | 37.1 ± 0.9 | 38.1 ± 0.8 |
| d_nearest_dfar | 2 | 36.6 ± 1.3 | 37.6 ± 1.6 | 35.6 ± 1.0 | 36.4 ± 1.1 | 35.8 ± 0.9 | 36.8 ± 0.9 |
| d_nearest_dfar | 3 | 37.7 ± 1.3 | 38.8 ± 1.6 | 36.8 ± 1.0 | 37.7 ± 1.0 | 37.0 ± 0.9 | 38.1 ± 0.9 |
| d_nearest_dfar | 4 | 38.5 ± 1.3 | 39.6 ± 1.7 | 37.6 ± 1.0 | 38.5 ± 1.1 | 37.8 ± 0.9 | 38.9 ± 0.9 |

## Paired comparisons (percentage points of savings)

Positive = the first set saves more. `wins` = repetitions where it did. The go/no-go below uses distances only, as fixed in the plan; page reads are reported for information.

### q_medoids vs d_medoids - same construction, queries vs data

distances:

| S | concentrado knn | concentrado range | disperso knn | disperso range | uniforme knn | uniforme range |
|---|---|---|---|---|---|---|
| 1 | +3.6 ± 1.1 (18/20) | +4.5 ± 1.1 (20/20) | +1.7 ± 0.7 (16/20) | +1.9 ± 0.7 (16/20) | +0.2 ± 0.2 (10/20) | +0.2 ± 0.3 (10/20) |
| 2 | +2.1 ± 0.7 (18/20) | +2.8 ± 0.7 (19/20) | +1.3 ± 0.3 (19/20) | +1.5 ± 0.5 (19/20) | +0.1 ± 0.3 (9/20) | +0.1 ± 0.3 (8/20) |
| 3 | -0.9 ± 0.8 (6/20) | -0.4 ± 0.7 (9/20) | -0.7 ± 0.7 (5/20) | -0.4 ± 0.7 (8/20) | -0.0 ± 0.4 (9/20) | -0.0 ± 0.4 (11/20) |
| 4 | -1.0 ± 0.5 (4/20) | -0.5 ± 0.4 (5/20) | -0.3 ± 0.4 (7/20) | -0.1 ± 0.4 (11/20) | -0.2 ± 0.2 (7/20) | -0.2 ± 0.2 (7/20) |

page reads:

| S | concentrado knn | concentrado range | disperso knn | disperso range | uniforme knn | uniforme range |
|---|---|---|---|---|---|---|
| 1 | +3.6 ± 1.0 (20/20) | +3.6 ± 0.9 (20/20) | +1.8 ± 0.7 (17/20) | +1.8 ± 0.7 (17/20) | -0.0 ± 0.2 (10/20) | -0.1 ± 0.2 (9/20) |
| 2 | +5.9 ± 1.3 (20/20) | +5.8 ± 1.3 (20/20) | +2.7 ± 0.8 (19/20) | +2.7 ± 0.9 (19/20) | -0.1 ± 0.2 (10/20) | -0.1 ± 0.2 (8/20) |
| 3 | +4.7 ± 1.3 (20/20) | +4.5 ± 1.3 (19/20) | +2.1 ± 0.7 (18/20) | +2.1 ± 0.7 (19/20) | -0.0 ± 0.4 (9/20) | -0.1 ± 0.4 (8/20) |
| 4 | +3.6 ± 1.2 (20/20) | +3.4 ± 1.1 (19/20) | +2.3 ± 0.7 (18/20) | +2.3 ± 0.7 (18/20) | -0.4 ± 0.4 (4/20) | -0.3 ± 0.4 (7/20) |

### q_medoids vs d_far - query concentration points vs classic outlier pivots

distances:

| S | concentrado knn | concentrado range | disperso knn | disperso range | uniforme knn | uniforme range |
|---|---|---|---|---|---|---|
| 1 | -0.5 ± 1.4 (9/20) | +0.1 ± 1.3 (9/20) | -2.2 ± 1.0 (3/20) | -2.1 ± 1.1 (4/20) | -3.8 ± 0.7 (0/20) | -3.8 ± 0.9 (0/20) |
| 2 | -0.7 ± 0.7 (5/20) | -0.1 ± 0.7 (6/20) | -1.9 ± 0.6 (1/20) | -2.1 ± 0.6 (1/20) | -3.5 ± 0.3 (0/20) | -3.7 ± 0.3 (0/20) |
| 3 | -1.3 ± 0.6 (3/20) | -0.8 ± 0.6 (7/20) | -1.1 ± 0.7 (4/20) | -1.0 ± 0.7 (4/20) | -0.6 ± 0.3 (2/20) | -0.6 ± 0.3 (3/20) |
| 4 | -0.2 ± 0.5 (8/20) | +0.3 ± 0.6 (11/20) | +0.1 ± 0.5 (13/20) | +0.2 ± 0.5 (14/20) | +0.2 ± 0.2 (14/20) | +0.2 ± 0.2 (14/20) |

page reads:

| S | concentrado knn | concentrado range | disperso knn | disperso range | uniforme knn | uniforme range |
|---|---|---|---|---|---|---|
| 1 | +1.1 ± 1.3 (14/20) | +1.2 ± 1.3 (14/20) | -0.3 ± 1.0 (10/20) | -0.2 ± 1.0 (10/20) | -1.9 ± 0.6 (1/20) | -1.7 ± 0.7 (3/20) |
| 2 | +5.3 ± 0.8 (20/20) | +5.5 ± 0.8 (20/20) | +1.3 ± 0.7 (15/20) | +1.2 ± 0.8 (16/20) | -1.5 ± 0.6 (3/20) | -1.4 ± 0.6 (3/20) |
| 3 | +4.5 ± 1.1 (20/20) | +4.5 ± 1.2 (20/20) | +1.4 ± 0.7 (16/20) | +1.4 ± 0.8 (16/20) | -0.7 ± 0.4 (3/20) | -0.6 ± 0.4 (4/20) |
| 4 | +5.7 ± 1.2 (20/20) | +5.6 ± 1.2 (20/20) | +3.1 ± 0.9 (18/20) | +3.1 ± 1.0 (17/20) | +0.7 ± 0.4 (15/20) | +0.7 ± 0.4 (13/20) |

### q_kcenter vs d_far - same construction (farthest-first), queries vs data

distances:

| S | concentrado knn | concentrado range | disperso knn | disperso range | uniforme knn | uniforme range |
|---|---|---|---|---|---|---|
| 1 | -0.5 ± 1.4 (9/20) | +0.1 ± 1.3 (9/20) | -2.2 ± 1.0 (3/20) | -2.1 ± 1.1 (4/20) | -3.8 ± 0.7 (0/20) | -3.8 ± 0.9 (0/20) |
| 2 | -2.1 ± 0.9 (3/20) | -2.1 ± 0.8 (2/20) | -2.2 ± 0.6 (1/20) | -2.5 ± 0.5 (1/20) | -2.2 ± 0.3 (0/20) | -2.5 ± 0.4 (0/20) |
| 3 | -2.0 ± 0.7 (1/20) | -1.9 ± 0.6 (0/20) | -1.1 ± 0.6 (3/20) | -1.0 ± 0.6 (4/20) | -0.5 ± 0.3 (3/20) | -0.5 ± 0.3 (3/20) |
| 4 | -0.6 ± 0.6 (8/20) | -0.3 ± 0.6 (7/20) | +0.2 ± 0.4 (12/20) | +0.2 ± 0.4 (11/20) | +0.3 ± 0.2 (15/20) | +0.3 ± 0.3 (15/20) |

page reads:

| S | concentrado knn | concentrado range | disperso knn | disperso range | uniforme knn | uniforme range |
|---|---|---|---|---|---|---|
| 1 | +1.1 ± 1.3 (14/20) | +1.2 ± 1.3 (14/20) | -0.3 ± 1.0 (10/20) | -0.2 ± 1.0 (10/20) | -1.9 ± 0.6 (1/20) | -1.7 ± 0.7 (3/20) |
| 2 | +2.2 ± 0.6 (19/20) | +2.4 ± 0.5 (20/20) | +0.5 ± 0.6 (13/20) | +0.5 ± 0.7 (12/20) | -1.6 ± 0.6 (3/20) | -1.5 ± 0.6 (3/20) |
| 3 | +1.9 ± 1.1 (17/20) | +2.1 ± 1.1 (17/20) | +0.9 ± 0.7 (13/20) | +0.9 ± 0.7 (13/20) | -0.9 ± 0.6 (2/20) | -0.8 ± 0.6 (3/20) |
| 4 | +3.9 ± 1.1 (20/20) | +4.1 ± 1.2 (20/20) | +2.5 ± 0.9 (19/20) | +2.5 ± 0.9 (18/20) | +0.5 ± 0.6 (14/20) | +0.5 ± 0.5 (13/20) |

### q_nearest vs d_nearest - local pivot: nearest past query vs nearest data sample

distances:

| S | concentrado knn | concentrado range | disperso knn | disperso range | uniforme knn | uniforme range |
|---|---|---|---|---|---|---|
| 1 | +4.9 ± 0.4 (20/20) | +5.2 ± 0.4 (20/20) | +4.9 ± 0.5 (20/20) | +5.4 ± 0.5 (20/20) | -0.3 ± 0.3 (6/20) | -0.3 ± 0.2 (4/20) |

page reads:

| S | concentrado knn | concentrado range | disperso knn | disperso range | uniforme knn | uniforme range |
|---|---|---|---|---|---|---|
| 1 | +10.2 ± 0.8 (20/20) | +11.0 ± 1.0 (20/20) | +10.5 ± 1.0 (20/20) | +11.5 ± 1.3 (20/20) | -0.8 ± 0.5 (5/20) | -0.8 ± 0.7 (9/20) |

### q_nearest_dfar vs d_nearest_dfar - local + global: queries vs data

distances:

| S | concentrado knn | concentrado range | disperso knn | disperso range | uniforme knn | uniforme range |
|---|---|---|---|---|---|---|
| 2 | +2.6 ± 0.2 (20/20) | +3.0 ± 0.2 (20/20) | +2.6 ± 0.2 (20/20) | +3.1 ± 0.3 (20/20) | -0.2 ± 0.2 (3/20) | -0.2 ± 0.2 (7/20) |
| 3 | +1.7 ± 0.2 (20/20) | +2.2 ± 0.2 (20/20) | +1.7 ± 0.2 (20/20) | +2.2 ± 0.2 (20/20) | -0.2 ± 0.1 (6/20) | -0.2 ± 0.1 (4/20) |
| 4 | +1.4 ± 0.1 (20/20) | +1.9 ± 0.2 (20/20) | +1.3 ± 0.1 (20/20) | +2.0 ± 0.2 (20/20) | -0.2 ± 0.1 (3/20) | -0.2 ± 0.1 (4/20) |

page reads:

| S | concentrado knn | concentrado range | disperso knn | disperso range | uniforme knn | uniforme range |
|---|---|---|---|---|---|---|
| 2 | +9.3 ± 0.8 (20/20) | +10.0 ± 0.9 (20/20) | +9.5 ± 0.8 (20/20) | +10.5 ± 1.2 (20/20) | -0.7 ± 0.5 (7/20) | -0.8 ± 0.6 (8/20) |
| 3 | +8.7 ± 0.7 (20/20) | +9.3 ± 0.9 (20/20) | +8.8 ± 0.8 (20/20) | +9.7 ± 1.1 (20/20) | -0.7 ± 0.5 (7/20) | -0.7 ± 0.5 (6/20) |
| 4 | +8.2 ± 0.7 (20/20) | +8.9 ± 0.8 (20/20) | +8.4 ± 0.7 (20/20) | +9.2 ± 1.1 (20/20) | -0.7 ± 0.4 (5/20) | -0.8 ± 0.5 (7/20) |

### q_nearest_dfar vs d_far - adding a local query pivot vs all-global pivots

distances:

| S | concentrado knn | concentrado range | disperso knn | disperso range | uniforme knn | uniforme range |
|---|---|---|---|---|---|---|
| 2 | +7.0 ± 0.7 (20/20) | +8.5 ± 1.0 (20/20) | +6.4 ± 0.4 (20/20) | +7.7 ± 0.5 (20/20) | +3.3 ± 0.3 (20/20) | +4.3 ± 0.3 (20/20) |
| 3 | +4.6 ± 0.4 (20/20) | +6.3 ± 0.5 (20/20) | +4.4 ± 0.2 (20/20) | +6.0 ± 0.3 (20/20) | +2.6 ± 0.2 (20/20) | +3.7 ± 0.2 (20/20) |
| 4 | +4.8 ± 0.4 (20/20) | +6.3 ± 0.5 (20/20) | +4.4 ± 0.2 (20/20) | +5.8 ± 0.3 (20/20) | +2.9 ± 0.1 (20/20) | +3.8 ± 0.2 (20/20) |

page reads:

| S | concentrado knn | concentrado range | disperso knn | disperso range | uniforme knn | uniforme range |
|---|---|---|---|---|---|---|
| 2 | +31.2 ± 1.3 (20/20) | +32.4 ± 2.0 (20/20) | +30.0 ± 1.0 (20/20) | +31.2 ± 1.4 (20/20) | +20.1 ± 0.8 (20/20) | +20.4 ± 0.8 (20/20) |
| 3 | +27.6 ± 1.4 (20/20) | +28.6 ± 1.8 (20/20) | +26.5 ± 1.0 (20/20) | +27.5 ± 1.2 (20/20) | +17.4 ± 0.7 (20/20) | +17.6 ± 0.7 (20/20) |
| 4 | +26.9 ± 1.5 (20/20) | +27.8 ± 1.7 (20/20) | +25.5 ± 0.9 (20/20) | +26.4 ± 1.2 (20/20) | +16.9 ± 0.7 (20/20) | +16.9 ± 0.7 (20/20) |

## Go/no-go (plan, phase F0)

Criterion fixed before the run: the best query-derived set beats the best data-derived set by at least **5 percentage points** of distance savings, with the paired 95% CI above zero, in `concentrado` AND `disperso`, for some S, in every query type. Both best sets are picked by their mean, which favours the data side.

### global pivots: **DOES NOT PASS**

| regime | query | S | best query set | best data set | difference (pp) | wins | clears 5 pp |
|---|---|---|---|---|---|---|---|
| concentrado | knn | 1 | q_medoids | d_far | -0.5 ± 1.4 | 9/20 | no |
| concentrado | knn | 2 | q_medoids | d_far | -0.7 ± 0.7 | 5/20 | no |
| concentrado | knn | 3 | q_medoids | d_far | -1.3 ± 0.6 | 3/20 | no |
| concentrado | knn | 4 | q_medoids | d_medoids | -1.0 ± 0.5 | 4/20 | no |
| concentrado | range | 1 | q_medoids | d_far | +0.1 ± 1.3 | 9/20 | no |
| concentrado | range | 2 | q_medoids | d_far | -0.1 ± 0.7 | 6/20 | no |
| concentrado | range | 3 | q_medoids | d_far | -0.8 ± 0.6 | 7/20 | no |
| concentrado | range | 4 | q_medoids | d_medoids | -0.5 ± 0.4 | 5/20 | no |
| disperso | knn | 1 | q_medoids | d_far | -2.2 ± 1.0 | 3/20 | no |
| disperso | knn | 2 | q_medoids | d_far | -1.9 ± 0.6 | 1/20 | no |
| disperso | knn | 3 | q_kcenter | d_far | -1.1 ± 0.6 | 3/20 | no |
| disperso | knn | 4 | q_kcenter | d_medoids | -0.3 ± 0.4 | 8/20 | no |
| disperso | range | 1 | q_medoids | d_far | -2.1 ± 1.1 | 4/20 | no |
| disperso | range | 2 | q_medoids | d_far | -2.1 ± 0.6 | 1/20 | no |
| disperso | range | 3 | q_medoids | d_far | -1.0 ± 0.7 | 4/20 | no |
| disperso | range | 4 | q_kcenter | d_medoids | -0.1 ± 0.5 | 9/20 | no |
| uniforme | knn | 1 | q_medoids | d_far | -3.8 ± 0.7 | 0/20 | no |
| uniforme | knn | 2 | q_kcenter | d_far | -2.2 ± 0.3 | 0/20 | no |
| uniforme | knn | 3 | q_kcenter | d_far | -0.5 ± 0.3 | 3/20 | no |
| uniforme | knn | 4 | q_kcenter | d_medoids | -0.1 ± 0.3 | 8/20 | no |
| uniforme | range | 1 | q_medoids | d_far | -3.8 ± 0.9 | 0/20 | no |
| uniforme | range | 2 | q_kcenter | d_far | -2.5 ± 0.4 | 0/20 | no |
| uniforme | range | 3 | q_kcenter | d_far | -0.5 ± 0.3 | 3/20 | no |
| uniforme | range | 4 | q_kcenter | d_medoids | -0.0 ± 0.3 | 11/20 | no |

### local (oracle) pivots: **DOES NOT PASS**

| regime | query | S | best query set | best data set | difference (pp) | wins | clears 5 pp |
|---|---|---|---|---|---|---|---|
| concentrado | knn | 1 | q_nearest | d_nearest | +4.9 ± 0.4 | 20/20 | no |
| concentrado | knn | 2 | q_nearest_dfar | d_nearest_dfar | +2.6 ± 0.2 | 20/20 | no |
| concentrado | knn | 3 | q_nearest_dfar | d_nearest_dfar | +1.7 ± 0.2 | 20/20 | no |
| concentrado | knn | 4 | q_nearest_dfar | d_nearest_dfar | +1.4 ± 0.1 | 20/20 | no |
| concentrado | range | 1 | q_nearest | d_nearest | +5.2 ± 0.4 | 20/20 | yes |
| concentrado | range | 2 | q_nearest_dfar | d_nearest_dfar | +3.0 ± 0.2 | 20/20 | no |
| concentrado | range | 3 | q_nearest_dfar | d_nearest_dfar | +2.2 ± 0.2 | 20/20 | no |
| concentrado | range | 4 | q_nearest_dfar | d_nearest_dfar | +1.9 ± 0.2 | 20/20 | no |
| disperso | knn | 1 | q_nearest | d_nearest | +4.9 ± 0.5 | 20/20 | no |
| disperso | knn | 2 | q_nearest_dfar | d_nearest_dfar | +2.6 ± 0.2 | 20/20 | no |
| disperso | knn | 3 | q_nearest_dfar | d_nearest_dfar | +1.7 ± 0.2 | 20/20 | no |
| disperso | knn | 4 | q_nearest_dfar | d_nearest_dfar | +1.3 ± 0.1 | 20/20 | no |
| disperso | range | 1 | q_nearest | d_nearest | +5.4 ± 0.5 | 20/20 | yes |
| disperso | range | 2 | q_nearest_dfar | d_nearest_dfar | +3.1 ± 0.3 | 20/20 | no |
| disperso | range | 3 | q_nearest_dfar | d_nearest_dfar | +2.2 ± 0.2 | 20/20 | no |
| disperso | range | 4 | q_nearest_dfar | d_nearest_dfar | +2.0 ± 0.2 | 20/20 | no |
| uniforme | knn | 1 | q_nearest | d_nearest | -0.3 ± 0.3 | 6/20 | no |
| uniforme | knn | 2 | q_nearest_dfar | d_nearest_dfar | -0.2 ± 0.2 | 3/20 | no |
| uniforme | knn | 3 | q_nearest_dfar | d_nearest_dfar | -0.2 ± 0.1 | 6/20 | no |
| uniforme | knn | 4 | q_nearest_dfar | d_nearest_dfar | -0.2 ± 0.1 | 3/20 | no |
| uniforme | range | 1 | q_nearest | d_nearest | -0.3 ± 0.2 | 4/20 | no |
| uniforme | range | 2 | q_nearest_dfar | d_nearest_dfar | -0.2 ± 0.2 | 7/20 | no |
| uniforme | range | 3 | q_nearest_dfar | d_nearest_dfar | -0.2 ± 0.1 | 4/20 | no |
| uniforme | range | 4 | q_nearest_dfar | d_nearest_dfar | -0.2 ± 0.1 | 4/20 | no |

## Environment

```
timestamp   : 2026-10-30T14:22:48-03:00
commit      : a2828ae
compiler    : g++.exe (Rev5, Built by MSYS2 project) 16.1.0
reps        : 20
seeds       : 42..61
slots       : 1..4
adapt       : 200
measure     : 100
k           : 10
radius      : 0.5
page size   : 1024
region fact.: 3
regimes     : concentrado disperso uniforme
dataset     : /c/Users/tataw/OneDrive/Desktop/Talia/Slim_Tree/arboretum_SlimTree_QueryGravity/arboretum/test-brcities-datastore/BrazilianCities.txt
query pool  : /c/Users/tataw/OneDrive/Desktop/Talia/Slim_Tree/arboretum_SlimTree_QueryGravity/arboretum/test-brcities-datastore/BrazilianCities500.txt
```
