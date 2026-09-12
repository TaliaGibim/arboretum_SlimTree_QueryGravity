# Chapter 4 - section 4.5 results

Collapsed over 5 repetition(s). Every measured query in every arm was verified against a brute-force scan.


## Arms

- **original** - Slim-tree as built, in file insertion order.
- **slimdown** - After the classic Slim-Down (Optimize()).
- **etapa1** - Query-guided relocation, section 4.3.
- **etapa2** - Rebuilt from the Voronoi partition of the QUERY centres, section 4.4.
- **etapa2_data** - Control: same pipeline, same |P|, pivots from the DATA. Isolates what the query distribution actually contributes.


## Distance computations per query

Lower is better. Percentages are against `original`.

| regime | query | original | slimdown | etapa1 | etapa2 | etapa2_data |
|---|---|---|---|---|---|---|
| concentrado | knn | 370.1 | 358.9 (-3.0%) | 370.0 (-0.0%) | 125.1 (-66.2%) | 153.1 (-58.6%) |
| concentrado | range | 279.8 | 269.3 (-3.8%) | 281.1 (+0.5%) | 88.1 (-68.5%) | 97.3 (-65.2%) |
| disperso | knn | 274.8 | 274.5 (-0.1%) | 273.7 (-0.4%) | 89.5 (-67.4%) | 126.4 (-54.0%) |
| disperso | range | 164.9 | 161.8 (-1.9%) | 165.3 (+0.2%) | 47.8 (-71.0%) | 64.5 (-60.9%) |
| uniforme | knn | 406.5 | 391.2 (-3.8%) | 394.1 (-3.1%) | 158.0 (-61.1%) | 135.5 (-66.7%) |
| uniforme | range | 356.4 | 342.0 (-4.0%) | 346.0 (-2.9%) | 135.0 (-62.1%) | 124.3 (-65.1%) |

## Page reads per query

| regime | query | original | slimdown | etapa1 | etapa2 | etapa2_data |
|---|---|---|---|---|---|---|
| concentrado | knn | 68.2 | 63.5 (-7.0%) | 66.3 (-2.9%) | 19.8 (-71.0%) | 20.7 (-69.7%) |
| concentrado | range | 66.0 | 61.0 (-7.7%) | 63.9 (-3.2%) | 18.0 (-72.7%) | 16.2 (-75.4%) |
| disperso | knn | 46.3 | 44.4 (-4.1%) | 45.4 (-1.9%) | 13.4 (-71.0%) | 17.6 (-61.9%) |
| disperso | range | 40.9 | 39.1 (-4.3%) | 40.4 (-1.3%) | 11.9 (-71.0%) | 14.7 (-64.0%) |
| uniforme | knn | 73.5 | 67.8 (-7.8%) | 71.1 (-3.3%) | 25.9 (-64.9%) | 21.4 (-70.9%) |
| uniforme | range | 72.1 | 66.4 (-8.0%) | 69.5 (-3.6%) | 24.7 (-65.8%) | 21.3 (-70.4%) |

## What the queries actually contribute

`etapa2` and `etapa2_data` run the identical pipeline with the identical |P|; only the origin of the pivots differs. The gap between them is the part of the gain that is attributable to the QUERY distribution rather than to spatially coherent insertion order.

| regime | query | etapa2 | etapa2_data | query advantage |
|---|---|---|---|---|
| concentrado | knn | 125.1 | 153.1 | +18.3% |
| concentrado | range | 88.1 | 97.3 | +9.4% |
| disperso | knn | 89.5 | 126.4 | +29.2% |
| disperso | range | 47.8 | 64.5 | +25.9% |
| uniforme | knn | 158.0 | 135.5 | -16.6% |
| uniforme | range | 135.0 | 124.3 | -8.6% |

## H2 - the destination radius never grows

Etapa 1 relocated objects in 30 measured configuration(s). The driver aborts if the destination radius ever grows, and it did not: every run completed. Equation 4.3 holds by construction and was checked at run time.


## H3 - amortisation

| regime | query | break-even | saving per measure pass | cumulative adaptation cost |
|---|---|---|---|---|
| concentrado | knn | not reached | 14.0 | 24846.0 |
| concentrado | range | not reached | -135.0 | 23120.0 |
| disperso | knn | not reached | 116.0 | 23720.0 |
| disperso | range | not reached | -34.0 | 18517.0 |
| uniforme | knn | not reached | 1248.0 | 24674.0 |
| uniforme | range | not reached | 1040.0 | 24765.0 |

Saving and cost are both in distance computations. A break-even of *not reached* means the measured saving never grew large enough to repay what the adaptation spent.


## H4 - convergence of the relocation rate

Mean relocations per adaptation query, in blocks:

| regime | block 1 | block 2 | block 3 | block 4 |
|---|---|---|---|---|
| concentrado | 6.54 | 6.74 | 4.12 | 4.16 |
| disperso | 4.96 | 0.48 | 2.55 | 2.04 |
| uniforme | 7.52 | 7.86 | 7.44 | 7.18 |

A decaying rate supports H4: the structure is converging. A flat rate under `uniforme` is the predicted adverse case - with no privileged region there is nothing to converge to, and objects keep being moved in conflicting directions.


## Structure

| regime | arm | nodes | mean leaf radius | overlap ratio | fat factor |
|---|---|---|---|---|---|
| concentrado | original | 393 | 3.714 | 0.344 | 0.1480 |
| concentrado | slimdown | 378 | 3.476 | 0.309 | 0.1394 |
| concentrado | etapa1 | 393 | 3.670 | 0.338 | 0.1462 |
| concentrado | etapa2 | 482 | 0.975 | 0.322 | 0.0220 |
| concentrado | etapa2_data | 465 | 0.966 | 0.330 | 0.0198 |
| disperso | original | 393 | 3.714 | 0.344 | 0.1480 |
| disperso | slimdown | 378 | 3.476 | 0.309 | 0.1394 |
| disperso | etapa1 | 393 | 3.636 | 0.333 | 0.1453 |
| disperso | etapa2 | 488 | 0.826 | 0.360 | 0.0160 |
| disperso | etapa2_data | 466 | 1.135 | 0.322 | 0.0278 |
| uniforme | original | 393 | 3.714 | 0.344 | 0.1480 |
| uniforme | slimdown | 378 | 3.476 | 0.309 | 0.1394 |
| uniforme | etapa1 | 393 | 3.566 | 0.325 | 0.1417 |
| uniforme | etapa2 | 475 | 0.979 | 0.320 | 0.0270 |
| uniforme | etapa2_data | 465 | 0.979 | 0.330 | 0.0212 |
