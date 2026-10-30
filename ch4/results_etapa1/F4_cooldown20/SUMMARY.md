# Chapter 4 - section 4.5 results

20 independent repetition(s), seeds 42-61. Every measured query in every arm was verified against a brute-force scan.


## Protocol

- Each repetition draws its own regions, queries and insertion order from its own seed (base seed + repetition).
- A region is a metric ball: a centre city drawn at random from the data plus its nearest cities, and region balls are disjoint. Region size = factor x the queries drawn from it, so a share of every region is never queried.
- Inside a region the cities are shuffled and split DISJOINTLY into adapt and measure queries; every region contributes to both sets.
- `uniforme` draws adapt and measure without replacement from the query pool.
- Percentages are PAIRED: computed per repetition against `original` on the same workload and the same insertion order, then averaged; `±` is the 95% confidence interval half-width of that mean.


## Arms

- **original** - Slim-tree as built, in a random insertion order drawn per repetition.
- **slimdown** - After the classic Slim-Down (Optimize()).
- **etapa1** - Query-guided relocation, section 4.3.
- **etapa2** - Rebuilt from the Voronoi partition of the QUERY centres, section 4.4.
- **etapa2_data** - Control: same pipeline, same |P|, pivots from the DATA. Isolates what the query distribution actually contributes.
- **hilbert** - Reference: cities inserted along a Hilbert curve over (lat, long). Insertion order alone - no pivots, no queries. Uses coordinates, so it is not available in a general metric space.
- **etapa2_bulk** - Top-down variant: T' bulk-loaded level by level, the queries deciding the partition of every level (stSlimQueryBulkLoad). Not height-balanced.
- **etapa2_bulk_data** - Control for etapa2_bulk: the same code with the queries switched off (data pivots only).
- **hilbert_etapa1** - Etapa 1 run on the Hilbert-built tree: relocation as fine tuning of a tree whose leaves are already tight.
- **etapa2_etapa1** - Etapa 1 run on T' after the Etapa 2 rebuild.
- **slimdown_etapa1** - Slim-down (Optimize()) first, then Etapa 1 on top.


## Workload

`measure->adapt` is the mean distance from a measured query to the nearest adapt query - how close the evaluation is to what the structure was adapted to. For scale: the range radius is 0.50.

| regime | regions | region size | measure->adapt (mean ± CI) |
|---|---|---|---|
| concentrado | 2 | 450 | 0.290 ± 0.060 |
| disperso | 8 | 114 | 0.320 ± 0.031 |
| uniforme | 0 | - | 0.741 ± 0.040 |

Region centres drawn per repetition (`workload.csv` has every query):

- **concentrado** - 40 distinct centres over 20 repetitions. rep 0: Carnaubal-CE, Floreal-SP; rep 1: Novo Triunfo-BA, Graça Aranha-MA; rep 2: Vassouras-RJ, Tianguá-CE; rep 3: São Luiz-RR, Mauá da Serra-PR; rep 4: Conde-BA, Iraceminha-SC; ...
- **disperso** - 158 distinct centres over 20 repetitions. rep 0: Carnaubal-CE, Vitória do Jari-AP, São Francisco do Oeste-RN, Floreal-SP, Sarzedo-MG, Taió-SC, Várzea Branca-PI, Ponto Chique-MG; rep 1: Novo Triunfo-BA, Graça Aranha-MA, Bom Sucesso do Sul-PR, Kaloré-PR, Formosa do Rio Preto-BA, Cumari-GO, Campo Grande-MS, Montenegro-RS; rep 2: Vassouras-RJ, Tianguá-CE, Sítio do Quinto-BA, Sítio Novo-RN, Rio Paranaíba-MG, Itapuranga-GO, Junqueirópolis-SP, Brasnorte-MT; rep 3: São Luiz-RR, Almas-TO, Mauá da Serra-PR, Santa Maria-RN, Tanguá-RJ, Meleiro-SC, Pingo-d'Água-MG, Santo Antônio do Jardim-SP; rep 4: Conde-BA, Iraceminha-SC, Pindorama do Tocantins-TO, Boa Vista-RR, Rio Pomba-MG, Cachoeirinha-RS, Solânea-PB, Varjão de Minas-MG; ...


## Distance computations per query

Lower is better. Mean over repetitions, then the paired change against `original` ± 95% CI.

| regime | query | original | slimdown | etapa1 | etapa2 | etapa2_data | hilbert | etapa2_bulk | etapa2_bulk_data | hilbert_etapa1 | etapa2_etapa1 | slimdown_etapa1 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| concentrado | knn | 401.2 | 387.7 (-3.3% ± 0.6) | 393.5 (-1.9% ± 0.6) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 386.2 (-3.7% ± 0.8) |
| concentrado | range | 345.6 | 333.7 (-3.4% ± 0.4) | 336.2 (-2.7% ± 0.5) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 330.9 (-4.2% ± 0.5) |
| disperso | knn | 399.2 | 386.6 (-3.1% ± 0.4) | 398.0 (-0.3% ± 0.4) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 386.3 (-3.2% ± 0.5) |
| disperso | range | 342.6 | 331.1 (-3.3% ± 0.3) | 337.8 (-1.4% ± 0.3) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 329.4 (-3.8% ± 0.4) |
| uniforme | knn | 401.6 | 388.7 (-3.2% ± 0.4) | 403.7 (+0.5% ± 0.3) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 390.4 (-2.8% ± 0.4) |
| uniforme | range | 343.8 | 331.7 (-3.5% ± 0.3) | 341.8 (-0.6% ± 0.2) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 331.8 (-3.5% ± 0.4) |

## Page reads per query

Page reads equal nodes entered: the page manager has no buffer.

| regime | query | original | slimdown | etapa1 | etapa2 | etapa2_data | hilbert | etapa2_bulk | etapa2_bulk_data | hilbert_etapa1 | etapa2_etapa1 | slimdown_etapa1 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| concentrado | knn | 70.9 | 65.5 (-7.4% ± 0.7) | 68.0 (-3.9% ± 0.5) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 65.0 (-8.1% ± 0.7) |
| concentrado | range | 69.4 | 64.0 (-7.6% ± 0.7) | 66.3 (-4.3% ± 0.5) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 63.5 (-8.3% ± 0.7) |
| disperso | knn | 70.4 | 65.0 (-7.7% ± 0.6) | 68.2 (-3.1% ± 0.4) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 64.7 (-8.2% ± 0.6) |
| disperso | range | 68.7 | 63.3 (-7.7% ± 0.6) | 66.3 (-3.4% ± 0.4) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 62.9 (-8.3% ± 0.6) |
| uniforme | knn | 69.9 | 64.7 (-7.5% ± 0.4) | 67.4 (-3.6% ± 0.3) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 64.2 (-8.2% ± 0.4) |
| uniforme | range | 68.1 | 62.9 (-7.6% ± 0.5) | 65.5 (-3.7% ± 0.3) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 62.4 (-8.3% ± 0.5) |

## Wall-clock time per query (ms)

Supporting evidence only: tens of microseconds, on a laptop.

| regime | query | original | slimdown | etapa1 | etapa2 | etapa2_data | hilbert | etapa2_bulk | etapa2_bulk_data | hilbert_etapa1 | etapa2_etapa1 | slimdown_etapa1 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| concentrado | knn | 0.133 | 0.122 (-8.0% ± 1.8) | 0.131 (-1.0% ± 3.7) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 0.122 (-8.0% ± 2.1) |
| concentrado | range | 0.125 | 0.116 (-7.7% ± 1.5) | 0.120 (-4.7% ± 0.8) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 0.116 (-7.6% ± 2.2) |
| disperso | knn | 0.132 | 0.121 (-8.1% ± 1.4) | 0.129 (-2.3% ± 1.6) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 0.120 (-8.9% ± 1.0) |
| disperso | range | 0.125 | 0.118 (-5.0% ± 4.7) | 0.123 (-1.3% ± 3.9) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 0.114 (-8.5% ± 1.3) |
| uniforme | knn | 0.134 | 0.123 (-7.7% ± 4.3) | 0.128 (-4.4% ± 3.1) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 0.121 (-9.0% ± 3.2) |
| uniforme | range | 0.123 | 0.120 (-2.8% ± 5.7) | 0.118 (-3.8% ± 1.0) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 0.114 (-7.0% ± 1.5) |

## Where the gain comes from

Each row compares an arm with a control on the SAME workload and insertion order, repetition by repetition. `etapa2` and `etapa2_data` share pipeline and |P|; `etapa2_bulk` and `etapa2_bulk_data` share every line of code but the query share. Negative means the first arm computes FEWER distances; `lower in` counts the repetitions in which it did.

| regime | query | comparison | distances | lower in | page reads |
|---|---|---|---|---|---|
| concentrado | knn | slimdown_etapa1 vs slimdown | -0.4% ± 0.3 | 16/20 | -0.7% ± 0.2 |
| concentrado | range | slimdown_etapa1 vs slimdown | -0.8% ± 0.3 | 18/20 | -0.8% ± 0.2 |
| disperso | knn | slimdown_etapa1 vs slimdown | -0.1% ± 0.2 | 11/20 | -0.5% ± 0.1 |
| disperso | range | slimdown_etapa1 vs slimdown | -0.5% ± 0.2 | 17/20 | -0.7% ± 0.1 |
| uniforme | knn | slimdown_etapa1 vs slimdown | +0.4% ± 0.2 | 1/20 | -0.8% ± 0.1 |
| uniforme | range | slimdown_etapa1 vs slimdown | +0.0% ± 0.1 | 10/20 | -0.7% ± 0.1 |

An interval that contains zero means the difference is not distinguishable from noise at this number of repetitions.


## Leaf depth inside and outside the queried regions

Mean depth (root = 1) of the leaf holding an object, for objects inside a queried region (hot) and outside every one (cold). Insertion-built trees are balanced, so both equal the height; only the top-down arms can differ. `hot - cold` is paired per repetition: negative means queried regions sit closer to the root.

| regime | arm | hot | cold | hot - cold |
|---|---|---|---|---|


## Cost of reorganising, and when it pays

`cost` is the distances spent turning the original tree into the measured one (for etapa2: T_Q + Equation 4.9 + building T'; for hilbert: the rebuild). `saving` is distances saved per query against `original`. `break-even` is the median, over repetitions, of cost / saving: the number of queries after which the reorganisation has paid for itself.

| regime | query | arm | cost | saving/query | break-even (queries) |
|---|---|---|---|---|---|
| concentrado | knn | slimdown | 44534 | 13.5 | 3447 |
| concentrado | knn | etapa1 | 64453 | 7.7 | 8244 (never in 1/20) |
| concentrado | knn | slimdown_etapa1 | 115899 | 15.0 | 7945 (never in 1/20) |
| concentrado | range | slimdown | 44534 | 12.0 | 3933 |
| concentrado | range | etapa1 | 62919 | 9.5 | 7224 |
| concentrado | range | slimdown_etapa1 | 113002 | 14.8 | 7761 |
| disperso | knn | slimdown | 44534 | 12.6 | 3350 |
| disperso | knn | etapa1 | 61072 | 1.2 | 19231 (never in 7/20) |
| disperso | knn | slimdown_etapa1 | 113158 | 12.9 | 8010 |
| disperso | range | slimdown | 44534 | 11.5 | 3908 |
| disperso | range | etapa1 | 59093 | 4.7 | 11007 |
| disperso | range | slimdown_etapa1 | 110553 | 13.1 | 8958 |
| uniforme | knn | slimdown | 44534 | 12.8 | 3555 |
| uniforme | knn | etapa1 | 52224 | -2.1 | 794771 (never in 17/20) |
| uniforme | knn | slimdown_etapa1 | 108098 | 11.2 | 10112 |
| uniforme | range | slimdown | 44534 | 12.1 | 3715 |
| uniforme | range | etapa1 | 50111 | 2.1 | 19646 (never in 3/20) |
| uniforme | range | slimdown_etapa1 | 105001 | 12.1 | 8544 |

## H2 - the destination radius never grows

Etapa 1 relocated objects in 240 measured configuration(s). The driver aborts if the destination radius ever grows, and it did not: every run completed. Equation 4.3 holds by construction and was checked at run time.


## H3 - amortisation of Etapa 1

| regime | query | break-even | saving per measure pass | cumulative adaptation cost |
|---|---|---|---|---|
| concentrado | knn | not reached | 768.5 | 64452.8 |
| concentrado | range | not reached | 948.9 | 62919.2 |
| disperso | knn | not reached | 120.5 | 61071.7 |
| disperso | range | not reached | 472.9 | 59093.3 |
| uniforme | knn | not reached | -214.2 | 52224.1 |
| uniforme | range | not reached | 206.8 | 50111.0 |

Saving and cost are both in distance computations and are averaged over repetitions chunk by chunk. A break-even of *not reached* means the measured saving never grew large enough to repay what the adaptation spent.


## H4 - convergence of the relocation rate

Mean relocations per adaptation query, in blocks:

| regime | block 1 | block 2 | block 3 | block 4 |
|---|---|---|---|---|
| concentrado | 6.81 | 4.47 | 3.54 | 3.22 |
| disperso | 7.18 | 5.46 | 4.02 | 3.34 |
| uniforme | 7.64 | 7.08 | 6.36 | 5.53 |

A decaying rate supports H4: the structure is converging. A flat rate under `uniforme` is the predicted adverse case - with no privileged region there is nothing to converge to, and objects keep being moved in conflicting directions.


## Structure

Mean over repetitions, k-NN runs (only `etapa1` differs between query types). The fat factor assumes every leaf at depth H, so it is NOT comparable for the top-down arms, which are not height-balanced.

| regime | arm | height | index nodes | leaves | mean leaf radius | overlap ratio | fat factor |
|---|---|---|---|---|---|---|---|
| concentrado | original | 4.0 | 27 | 356 | 3.607 | 0.375 | 0.1425 |
| concentrado | slimdown | 4.0 | 27 | 340 | 3.404 | 0.335 | 0.1361 |
| concentrado | etapa1 | 4.0 | 27 | 356 | 3.521 | 0.363 | 0.1390 |
| concentrado | slimdown_etapa1 | 4.0 | 27 | 340 | 3.390 | 0.333 | 0.1355 |
| disperso | original | 4.0 | 27 | 356 | 3.607 | 0.375 | 0.1425 |
| disperso | slimdown | 4.0 | 27 | 340 | 3.404 | 0.335 | 0.1361 |
| disperso | etapa1 | 4.0 | 27 | 356 | 3.492 | 0.358 | 0.1378 |
| disperso | slimdown_etapa1 | 4.0 | 27 | 340 | 3.385 | 0.333 | 0.1353 |
| uniforme | original | 4.0 | 27 | 356 | 3.607 | 0.375 | 0.1425 |
| uniforme | slimdown | 4.0 | 27 | 340 | 3.404 | 0.335 | 0.1361 |
| uniforme | etapa1 | 4.0 | 27 | 356 | 3.450 | 0.352 | 0.1360 |
| uniforme | slimdown_etapa1 | 4.0 | 27 | 340 | 3.374 | 0.331 | 0.1347 |
