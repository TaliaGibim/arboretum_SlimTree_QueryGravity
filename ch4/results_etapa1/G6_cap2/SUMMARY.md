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
| concentrado | 2 | 550 | 0.154 ± 0.024 |
| disperso | 8 | 138 | 0.159 ± 0.011 |

Region centres drawn per repetition (`workload.csv` has every query):

- **concentrado** - 40 distinct centres over 20 repetitions. rep 0: Carnaubal-CE, Floreal-SP; rep 1: Novo Triunfo-BA, São Sebastião do Tocantins-TO; rep 2: Vassouras-RJ, Tianguá-CE; rep 3: São Luiz-RR, Mauá da Serra-PR; rep 4: Conde-BA, Iraceminha-SC; ...
- **disperso** - 159 distinct centres over 20 repetitions. rep 0: Carnaubal-CE, Vitória do Jari-AP, São Francisco do Oeste-RN, Floreal-SP, Sarzedo-MG, Taió-SC, Várzea Branca-PI, Pejuçara-RS; rep 1: Novo Triunfo-BA, Graça Aranha-MA, Bom Sucesso do Sul-PR, Kaloré-PR, Formosa do Rio Preto-BA, Cumari-GO, Montenegro-RS, Itu-SP; rep 2: Vassouras-RJ, Tianguá-CE, Sítio do Quinto-BA, Sítio Novo-RN, Rio Paranaíba-MG, Itapuranga-GO, Junqueirópolis-SP, Brasnorte-MT; rep 3: São Luiz-RR, Almas-TO, Mauá da Serra-PR, Santa Maria-RN, Tanguá-RJ, Meleiro-SC, Pingo-d'Água-MG, Santo Antônio do Jardim-SP; rep 4: Conde-BA, Iraceminha-SC, Pindorama do Tocantins-TO, Boa Vista-RR, Rio Pomba-MG, Cachoeirinha-RS, Solânea-PB, Varjão de Minas-MG; ...


## Distance computations per query

Lower is better. Mean over repetitions, then the paired change against `original` ± 95% CI.

| regime | query | original | slimdown | etapa1 | etapa2 | etapa2_data | hilbert | etapa2_bulk | etapa2_bulk_data | hilbert_etapa1 | etapa2_etapa1 | slimdown_etapa1 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| concentrado | knn | 403.6 | 390.1 (-3.3% ± 0.7) | 395.2 (-2.1% ± 0.6) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 387.1 (-4.0% ± 0.7) |
| concentrado | range | 348.3 | 336.2 (-3.5% ± 0.4) | 337.5 (-3.1% ± 0.4) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 332.7 (-4.5% ± 0.4) |
| disperso | knn | 396.8 | 384.1 (-3.2% ± 0.4) | 393.8 (-0.8% ± 0.4) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 383.4 (-3.4% ± 0.5) |
| disperso | range | 345.9 | 334.4 (-3.3% ± 0.3) | 340.2 (-1.7% ± 0.4) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 332.6 (-3.8% ± 0.5) |

## Page reads per query

Page reads equal nodes entered: the page manager has no buffer.

| regime | query | original | slimdown | etapa1 | etapa2 | etapa2_data | hilbert | etapa2_bulk | etapa2_bulk_data | hilbert_etapa1 | etapa2_etapa1 | slimdown_etapa1 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| concentrado | knn | 70.9 | 65.6 (-7.4% ± 0.6) | 67.6 (-4.6% ± 0.5) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 64.9 (-8.3% ± 0.6) |
| concentrado | range | 69.4 | 64.1 (-7.5% ± 0.7) | 65.8 (-5.0% ± 0.5) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 63.4 (-8.5% ± 0.7) |
| disperso | knn | 70.3 | 65.0 (-7.5% ± 0.6) | 67.3 (-4.1% ± 0.3) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 64.4 (-8.3% ± 0.7) |
| disperso | range | 68.8 | 63.5 (-7.6% ± 0.6) | 65.9 (-4.2% ± 0.4) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 63.0 (-8.4% ± 0.7) |

## Wall-clock time per query (ms)

Supporting evidence only: tens of microseconds, on a laptop.

| regime | query | original | slimdown | etapa1 | etapa2 | etapa2_data | hilbert | etapa2_bulk | etapa2_bulk_data | hilbert_etapa1 | etapa2_etapa1 | slimdown_etapa1 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| concentrado | knn | 0.141 | 0.126 (-9.6% ± 5.6) | 0.127 (-8.9% ± 3.8) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 0.123 (-11.6% ± 4.1) |
| concentrado | range | 0.127 | 0.117 (-7.8% ± 1.9) | 0.120 (-5.6% ± 2.0) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 0.117 (-7.7% ± 2.5) |
| disperso | knn | 0.134 | 0.127 (-5.0% ± 4.8) | 0.127 (-5.3% ± 2.2) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 0.124 (-7.1% ± 2.2) |
| disperso | range | 0.127 | 0.118 (-7.1% ± 3.0) | 0.120 (-5.4% ± 2.0) | n/a | n/a | n/a | n/a | n/a | n/a | n/a | 0.117 (-7.2% ± 1.9) |

## Where the gain comes from

Each row compares an arm with a control on the SAME workload and insertion order, repetition by repetition. `etapa2` and `etapa2_data` share pipeline and |P|; `etapa2_bulk` and `etapa2_bulk_data` share every line of code but the query share. Negative means the first arm computes FEWER distances; `lower in` counts the repetitions in which it did.

| regime | query | comparison | distances | lower in | page reads |
|---|---|---|---|---|---|
| concentrado | knn | slimdown_etapa1 vs slimdown | -0.8% ± 0.2 | 19/20 | -1.0% ± 0.2 |
| concentrado | range | slimdown_etapa1 vs slimdown | -1.0% ± 0.3 | 20/20 | -1.1% ± 0.2 |
| disperso | knn | slimdown_etapa1 vs slimdown | -0.2% ± 0.2 | 14/20 | -0.9% ± 0.2 |
| disperso | range | slimdown_etapa1 vs slimdown | -0.5% ± 0.3 | 17/20 | -0.8% ± 0.1 |

An interval that contains zero means the difference is not distinguishable from noise at this number of repetitions.


## Leaf depth inside and outside the queried regions

Mean depth (root = 1) of the leaf holding an object, for objects inside a queried region (hot) and outside every one (cold). Insertion-built trees are balanced, so both equal the height; only the top-down arms can differ. `hot - cold` is paired per repetition: negative means queried regions sit closer to the root.

| regime | arm | hot | cold | hot - cold |
|---|---|---|---|---|


## Cost of reorganising, and when it pays

`cost` is the distances spent turning the original tree into the measured one (for etapa2: T_Q + Equation 4.9 + building T'; for hilbert: the rebuild). `saving` is distances saved per query against `original`. `break-even` is the median, over repetitions, of cost / saving: the number of queries after which the reorganisation has paid for itself.

| regime | query | arm | cost | saving/query | break-even (queries) |
|---|---|---|---|---|---|
| concentrado | knn | slimdown | 44534 | 13.5 | 3329 |
| concentrado | knn | etapa1 | 328846 | 8.4 | 35058 (never in 1/20) |
| concentrado | knn | slimdown_etapa1 | 395229 | 16.5 | 24177 |
| concentrado | range | slimdown | 44534 | 12.1 | 3678 |
| concentrado | range | etapa1 | 322228 | 10.8 | 29379 |
| concentrado | range | slimdown_etapa1 | 385278 | 15.6 | 24087 |
| disperso | knn | slimdown | 44534 | 12.8 | 3437 |
| disperso | knn | etapa1 | 341307 | 3.0 | 92235 (never in 3/20) |
| disperso | knn | slimdown_etapa1 | 395944 | 13.5 | 31083 |
| disperso | range | slimdown | 44534 | 11.5 | 3882 |
| disperso | range | etapa1 | 333967 | 5.8 | 50941 |
| disperso | range | slimdown_etapa1 | 386257 | 13.3 | 32144 |

## H2 - the destination radius never grows

Etapa 1 relocated objects in 160 measured configuration(s). The driver aborts if the destination radius ever grows, and it did not: every run completed. Equation 4.3 holds by construction and was checked at run time.


## H3 - amortisation of Etapa 1

| regime | query | break-even | saving per measure pass | cumulative adaptation cost |
|---|---|---|---|---|
| concentrado | knn | not reached | 838.1 | 328846.0 |
| concentrado | range | not reached | 1077.8 | 322227.8 |
| disperso | knn | not reached | 302.3 | 341307.0 |
| disperso | range | not reached | 575.2 | 333967.4 |

Saving and cost are both in distance computations and are averaged over repetitions chunk by chunk. A break-even of *not reached* means the measured saving never grew large enough to repay what the adaptation spent.


## H4 - convergence of the relocation rate

Mean relocations per adaptation query, in blocks:

| regime | block 1 | block 2 | block 3 | block 4 |
|---|---|---|---|---|
| concentrado | 3.40 | 0.58 | 0.21 | 0.13 |
| disperso | 4.28 | 0.91 | 0.43 | 0.24 |

A decaying rate supports H4: the structure is converging. A flat rate under `uniforme` is the predicted adverse case - with no privileged region there is nothing to converge to, and objects keep being moved in conflicting directions.


## Structure

Mean over repetitions, k-NN runs (only `etapa1` differs between query types). The fat factor assumes every leaf at depth H, so it is NOT comparable for the top-down arms, which are not height-balanced.

| regime | arm | height | index nodes | leaves | mean leaf radius | overlap ratio | fat factor |
|---|---|---|---|---|---|---|---|
| concentrado | original | 4.0 | 27 | 356 | 3.607 | 0.375 | 0.1425 |
| concentrado | slimdown | 4.0 | 27 | 340 | 3.404 | 0.335 | 0.1361 |
| concentrado | etapa1 | 4.0 | 27 | 356 | 3.499 | 0.360 | 0.1382 |
| concentrado | slimdown_etapa1 | 4.0 | 27 | 340 | 3.383 | 0.332 | 0.1352 |
| disperso | original | 4.0 | 27 | 356 | 3.607 | 0.375 | 0.1425 |
| disperso | slimdown | 4.0 | 27 | 340 | 3.404 | 0.335 | 0.1361 |
| disperso | etapa1 | 4.0 | 27 | 356 | 3.456 | 0.352 | 0.1363 |
| disperso | slimdown_etapa1 | 4.0 | 27 | 340 | 3.376 | 0.331 | 0.1349 |
