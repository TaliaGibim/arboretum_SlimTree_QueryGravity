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

| regime | query | original | slimdown | etapa1 | etapa2 | etapa2_data | hilbert | etapa2_bulk | etapa2_bulk_data | hilbert_etapa1 | etapa2_etapa1 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| concentrado | knn | 403.6 | 390.1 (-3.3% ± 0.7) | 396.3 (-1.7% ± 0.7) | 136.8 (-65.9% ± 2.0) | 138.4 (-65.5% ± 1.7) | 132.1 (-67.0% ± 1.5) | 104.5 (-74.0% ± 1.4) | 100.1 (-75.0% ± 1.0) | 134.6 (-66.4% ± 1.5) | 140.0 (-65.1% ± 1.9) |
| concentrado | range | 348.3 | 336.2 (-3.5% ± 0.4) | 342.7 (-1.6% ± 0.7) | 122.5 (-64.8% ± 1.4) | 127.1 (-63.4% ± 1.6) | 114.9 (-67.0% ± 1.5) | 95.1 (-72.7% ± 1.2) | 94.9 (-72.8% ± 1.1) | 115.9 (-66.7% ± 1.5) | 123.8 (-64.5% ± 1.4) |
| disperso | knn | 396.8 | 384.1 (-3.2% ± 0.4) | 386.7 (-2.5% ± 0.5) | 139.6 (-64.7% ± 2.1) | 140.7 (-64.4% ± 2.1) | 126.2 (-68.1% ± 0.9) | 98.2 (-75.2% ± 0.8) | 99.6 (-74.8% ± 0.8) | 129.1 (-67.4% ± 1.0) | 142.2 (-64.1% ± 2.2) |
| disperso | range | 345.9 | 334.4 (-3.3% ± 0.3) | 337.6 (-2.4% ± 0.5) | 127.7 (-63.1% ± 1.6) | 128.1 (-62.9% ± 1.6) | 113.9 (-67.0% ± 0.9) | 98.7 (-71.4% ± 0.7) | 96.6 (-72.0% ± 0.8) | 115.8 (-66.5% ± 1.0) | 128.9 (-62.7% ± 1.6) |

## Page reads per query

Page reads equal nodes entered: the page manager has no buffer.

| regime | query | original | slimdown | etapa1 | etapa2 | etapa2_data | hilbert | etapa2_bulk | etapa2_bulk_data | hilbert_etapa1 | etapa2_etapa1 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| concentrado | knn | 70.9 | 65.6 (-7.4% ± 0.6) | 66.9 (-5.6% ± 0.6) | 20.9 (-70.3% ± 1.6) | 21.6 (-69.4% ± 1.9) | 18.3 (-74.1% ± 0.9) | 13.3 (-81.2% ± 0.9) | 12.7 (-82.1% ± 0.8) | 17.7 (-74.9% ± 0.9) | 20.4 (-71.1% ± 1.5) |
| concentrado | range | 69.4 | 64.1 (-7.5% ± 0.7) | 65.3 (-5.8% ± 0.6) | 20.5 (-70.4% ± 1.4) | 21.7 (-68.6% ± 1.7) | 17.7 (-74.3% ± 1.1) | 13.2 (-80.9% ± 1.1) | 13.1 (-81.0% ± 1.0) | 17.2 (-75.1% ± 1.1) | 19.9 (-71.2% ± 1.4) |
| disperso | knn | 70.3 | 65.0 (-7.5% ± 0.6) | 66.5 (-5.3% ± 0.6) | 21.5 (-69.3% ± 1.8) | 21.8 (-68.9% ± 2.0) | 17.7 (-74.7% ± 0.7) | 12.2 (-82.6% ± 0.6) | 12.4 (-82.2% ± 0.6) | 17.3 (-75.3% ± 0.7) | 21.0 (-70.0% ± 1.8) |
| disperso | range | 68.8 | 63.5 (-7.6% ± 0.6) | 65.1 (-5.3% ± 0.6) | 21.2 (-69.1% ± 1.5) | 21.6 (-68.5% ± 1.8) | 17.7 (-74.2% ± 0.8) | 13.3 (-80.6% ± 0.6) | 13.1 (-80.9% ± 0.7) | 17.3 (-74.8% ± 0.8) | 20.7 (-69.9% ± 1.5) |

## Wall-clock time per query (ms)

Supporting evidence only: tens of microseconds, on a laptop.

| regime | query | original | slimdown | etapa1 | etapa2 | etapa2_data | hilbert | etapa2_bulk | etapa2_bulk_data | hilbert_etapa1 | etapa2_etapa1 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| concentrado | knn | 0.139 | 0.123 (-11.3% ± 3.3) | 0.127 (-8.3% ± 4.2) | 0.046 (-67.0% ± 1.8) | 0.045 (-67.2% ± 2.0) | 0.042 (-69.6% ± 1.8) | 0.031 (-77.6% ± 1.2) | 0.029 (-78.8% ± 1.0) | 0.039 (-71.7% ± 1.6) | 0.043 (-68.9% ± 1.9) |
| concentrado | range | 0.129 | 0.123 (-4.4% ± 6.2) | 0.118 (-8.1% ± 2.1) | 0.041 (-67.8% ± 1.6) | 0.044 (-65.4% ± 2.2) | 0.041 (-68.1% ± 3.5) | 0.029 (-77.1% ± 2.1) | 0.028 (-77.9% ± 1.7) | 0.035 (-72.4% ± 1.7) | 0.041 (-68.0% ± 2.2) |
| disperso | knn | 0.136 | 0.124 (-8.6% ± 2.9) | 0.125 (-8.1% ± 2.1) | 0.049 (-63.5% ± 3.3) | 0.048 (-65.0% ± 2.5) | 0.040 (-70.8% ± 1.4) | 0.031 (-77.2% ± 2.3) | 0.030 (-77.9% ± 1.1) | 0.041 (-69.9% ± 2.3) | 0.045 (-67.1% ± 2.7) |
| disperso | range | 0.133 | 0.121 (-8.4% ± 4.0) | 0.119 (-9.9% ± 3.2) | 0.043 (-66.9% ± 2.5) | 0.045 (-65.9% ± 1.9) | 0.037 (-72.0% ± 1.5) | 0.029 (-77.8% ± 1.5) | 0.029 (-78.0% ± 1.9) | 0.036 (-72.4% ± 1.9) | 0.043 (-66.9% ± 3.6) |

## Where the gain comes from

Each row compares an arm with a control on the SAME workload and insertion order, repetition by repetition. `etapa2` and `etapa2_data` share pipeline and |P|; `etapa2_bulk` and `etapa2_bulk_data` share every line of code but the query share. Negative means the first arm computes FEWER distances; `lower in` counts the repetitions in which it did.

| regime | query | comparison | distances | lower in | page reads |
|---|---|---|---|---|---|
| concentrado | knn | etapa2 vs etapa2_data | -0.0% ± 7.2 | 11/20 | -1.8% ± 7.9 |
| concentrado | range | etapa2 vs etapa2_data | -3.3% ± 5.3 | 13/20 | -4.5% ± 7.2 |
| disperso | knn | etapa2 vs etapa2_data | +0.7% ± 8.3 | 12/20 | +0.3% ± 8.2 |
| disperso | range | etapa2 vs etapa2_data | +0.3% ± 5.6 | 7/20 | -0.9% ± 6.5 |
| concentrado | knn | etapa2 vs hilbert | +3.6% ± 5.0 | 7/20 | +14.7% ± 5.7 |
| concentrado | range | etapa2 vs hilbert | +7.2% ± 4.9 | 4/20 | +16.2% ± 6.1 |
| disperso | knn | etapa2 vs hilbert | +10.6% ± 6.1 | 4/20 | +21.1% ± 6.1 |
| disperso | range | etapa2 vs hilbert | +12.0% ± 3.6 | 1/20 | +19.6% ± 4.6 |
| concentrado | knn | etapa2_bulk vs etapa2_bulk_data | +4.7% ± 5.4 | 6/20 | +5.1% ± 5.6 |
| concentrado | range | etapa2_bulk vs etapa2_bulk_data | +0.2% ± 1.6 | 11/20 | +0.7% ± 3.4 |
| disperso | knn | etapa2_bulk vs etapa2_bulk_data | -1.2% ± 2.5 | 12/20 | -1.8% ± 3.2 |
| disperso | range | etapa2_bulk vs etapa2_bulk_data | +2.1% ± 1.2 | 5/20 | +1.5% ± 2.6 |
| concentrado | knn | etapa2_bulk vs hilbert | -21.0% ± 3.6 | 20/20 | -27.4% ± 3.2 |
| concentrado | range | etapa2_bulk vs hilbert | -17.1% ± 2.2 | 20/20 | -25.7% ± 2.6 |
| disperso | knn | etapa2_bulk vs hilbert | -22.1% ± 2.0 | 20/20 | -31.2% ± 2.3 |
| disperso | range | etapa2_bulk vs hilbert | -13.3% ± 1.3 | 20/20 | -24.9% ± 1.5 |
| concentrado | knn | etapa2_bulk vs etapa2 | -23.1% ± 4.8 | 19/20 | -36.0% ± 4.5 |
| concentrado | range | etapa2_bulk vs etapa2 | -22.2% ± 2.7 | 20/20 | -35.3% ± 3.9 |
| disperso | knn | etapa2_bulk vs etapa2 | -28.6% ± 4.6 | 20/20 | -42.5% ± 3.5 |
| disperso | range | etapa2_bulk vs etapa2 | -22.2% ± 2.7 | 20/20 | -36.9% ± 2.2 |
| concentrado | knn | hilbert_etapa1 vs hilbert | +1.8% ± 0.5 | 0/20 | -3.0% ± 0.2 |
| concentrado | range | hilbert_etapa1 vs hilbert | +0.9% ± 0.4 | 3/20 | -2.9% ± 0.3 |
| disperso | knn | hilbert_etapa1 vs hilbert | +2.3% ± 0.4 | 0/20 | -2.5% ± 0.3 |
| disperso | range | hilbert_etapa1 vs hilbert | +1.7% ± 0.4 | 0/20 | -2.4% ± 0.3 |
| concentrado | knn | etapa2_etapa1 vs etapa2 | +2.4% ± 0.6 | 0/20 | -2.7% ± 0.5 |
| concentrado | range | etapa2_etapa1 vs etapa2 | +1.1% ± 0.6 | 5/20 | -2.9% ± 0.4 |
| disperso | knn | etapa2_etapa1 vs etapa2 | +1.9% ± 0.4 | 0/20 | -2.3% ± 0.2 |
| disperso | range | etapa2_etapa1 vs etapa2 | +1.0% ± 0.4 | 4/20 | -2.4% ± 0.3 |

An interval that contains zero means the difference is not distinguishable from noise at this number of repetitions.


## Leaf depth inside and outside the queried regions

Mean depth (root = 1) of the leaf holding an object, for objects inside a queried region (hot) and outside every one (cold). Insertion-built trees are balanced, so both equal the height; only the top-down arms can differ. `hot - cold` is paired per repetition: negative means queried regions sit closer to the root.

| regime | arm | hot | cold | hot - cold |
|---|---|---|---|---|
| concentrado | etapa2_bulk | 4.15 | 4.38 | -0.22 ± 0.09 |
| concentrado | etapa2_bulk_data | 4.45 | 4.33 | 0.12 ± 0.17 |
| disperso | etapa2_bulk | 4.39 | 4.33 | 0.07 ± 0.08 |
| disperso | etapa2_bulk_data | 4.43 | 4.33 | 0.10 ± 0.07 |


## Cost of reorganising, and when it pays

`cost` is the distances spent turning the original tree into the measured one (for etapa2: T_Q + Equation 4.9 + building T'; for hilbert: the rebuild). `saving` is distances saved per query against `original`. `break-even` is the median, over repetitions, of cost / saving: the number of queries after which the reorganisation has paid for itself.

| regime | query | arm | cost | saving/query | break-even (queries) |
|---|---|---|---|---|---|
| concentrado | knn | slimdown | 44534 | 13.5 | 3329 |
| concentrado | knn | etapa1 | 239220 | 7.3 | 35511 (never in 2/20) |
| concentrado | knn | etapa2 | 322798 | 266.9 | 1236 |
| concentrado | knn | etapa2_data | 309725 | 265.2 | 1198 |
| concentrado | knn | hilbert | 261791 | 271.5 | 1001 |
| concentrado | knn | etapa2_bulk | 219605 | 299.1 | 741 |
| concentrado | knn | etapa2_bulk_data | 211335 | 303.5 | 719 |
| concentrado | knn | hilbert_etapa1 | 299363 | 269.1 | 1148 |
| concentrado | knn | etapa2_etapa1 | 366355 | 263.6 | 1420 |
| concentrado | range | slimdown | 44534 | 12.1 | 3678 |
| concentrado | range | etapa1 | 232270 | 5.6 | 34659 (never in 4/20) |
| concentrado | range | etapa2 | 322798 | 225.8 | 1477 |
| concentrado | range | etapa2_data | 309725 | 221.2 | 1462 |
| concentrado | range | hilbert | 261791 | 233.4 | 1174 |
| concentrado | range | etapa2_bulk | 219605 | 253.2 | 904 |
| concentrado | range | etapa2_bulk_data | 211335 | 253.4 | 851 |
| concentrado | range | hilbert_etapa1 | 297694 | 232.4 | 1326 |
| concentrado | range | etapa2_etapa1 | 365297 | 224.5 | 1695 |
| disperso | knn | slimdown | 44534 | 12.8 | 3437 |
| disperso | knn | etapa1 | 227279 | 10.1 | 26882 |
| disperso | knn | etapa2 | 325243 | 257.3 | 1271 |
| disperso | knn | etapa2_data | 312957 | 256.1 | 1259 |
| disperso | knn | hilbert | 261791 | 270.6 | 974 |
| disperso | knn | etapa2_bulk | 222510 | 298.6 | 740 |
| disperso | knn | etapa2_bulk_data | 211150 | 297.3 | 715 |
| disperso | knn | hilbert_etapa1 | 297920 | 267.8 | 1123 |
| disperso | knn | etapa2_etapa1 | 370316 | 254.7 | 1492 |
| disperso | range | slimdown | 44534 | 11.5 | 3882 |
| disperso | range | etapa1 | 220056 | 8.3 | 29288 |
| disperso | range | etapa2 | 325243 | 218.2 | 1465 |
| disperso | range | etapa2_data | 312957 | 217.8 | 1478 |
| disperso | range | hilbert | 261791 | 232.1 | 1146 |
| disperso | range | etapa2_bulk | 222510 | 247.2 | 920 |
| disperso | range | etapa2_bulk_data | 211150 | 249.3 | 851 |
| disperso | range | hilbert_etapa1 | 297885 | 230.1 | 1307 |
| disperso | range | etapa2_etapa1 | 370188 | 217.0 | 1663 |

## H2 - the destination radius never grows

Etapa 1 relocated objects in 240 measured configuration(s). The driver aborts if the destination radius ever grows, and it did not: every run completed. Equation 4.3 holds by construction and was checked at run time.


## H3 - amortisation of Etapa 1

| regime | query | break-even | saving per measure pass | cumulative adaptation cost |
|---|---|---|---|---|
| concentrado | knn | not reached | 726.5 | 239219.8 |
| concentrado | range | not reached | 561.9 | 232270.2 |
| disperso | knn | not reached | 1011.1 | 227278.8 |
| disperso | range | not reached | 828.3 | 220056.2 |

Saving and cost are both in distance computations and are averaged over repetitions chunk by chunk. A break-even of *not reached* means the measured saving never grew large enough to repay what the adaptation spent.


## H4 - convergence of the relocation rate

Mean relocations per adaptation query, in blocks:

| regime | block 1 | block 2 | block 3 | block 4 |
|---|---|---|---|---|
| concentrado | 6.43 | 5.14 | 4.86 | 4.50 |
| disperso | 6.53 | 5.33 | 4.89 | 4.75 |

A decaying rate supports H4: the structure is converging. A flat rate under `uniforme` is the predicted adverse case - with no privileged region there is nothing to converge to, and objects keep being moved in conflicting directions.


## Structure

Mean over repetitions, k-NN runs (only `etapa1` differs between query types). The fat factor assumes every leaf at depth H, so it is NOT comparable for the top-down arms, which are not height-balanced.

| regime | arm | height | index nodes | leaves | mean leaf radius | overlap ratio | fat factor |
|---|---|---|---|---|---|---|---|
| concentrado | original | 4.0 | 27 | 356 | 3.607 | 0.375 | 0.1425 |
| concentrado | slimdown | 4.0 | 27 | 340 | 3.404 | 0.335 | 0.1361 |
| concentrado | etapa1 | 4.0 | 27 | 356 | 3.486 | 0.356 | 0.1378 |
| concentrado | etapa2 | 4.0 | 43 | 431 | 0.866 | 0.339 | 0.0168 |
| concentrado | etapa2_data | 4.0 | 41 | 426 | 0.940 | 0.327 | 0.0218 |
| concentrado | hilbert | 4.0 | 42 | 437 | 0.816 | 0.302 | 0.0123 |
| concentrado | etapa2_bulk | 6.0 | 64 | 417 | 0.694 | 0.403 | 0.0001 |
| concentrado | etapa2_bulk_data | 6.0 | 64 | 421 | 0.692 | 0.400 | -0.0008 |
| concentrado | hilbert_etapa1 | 4.0 | 42 | 437 | 0.801 | 0.292 | 0.0121 |
| concentrado | etapa2_etapa1 | 4.0 | 43 | 431 | 0.849 | 0.328 | 0.0166 |
| disperso | original | 4.0 | 27 | 356 | 3.607 | 0.375 | 0.1425 |
| disperso | slimdown | 4.0 | 27 | 340 | 3.404 | 0.335 | 0.1361 |
| disperso | etapa1 | 4.0 | 27 | 356 | 3.429 | 0.348 | 0.1353 |
| disperso | etapa2 | 4.0 | 42 | 430 | 0.905 | 0.346 | 0.0185 |
| disperso | etapa2_data | 4.0 | 40 | 423 | 0.932 | 0.330 | 0.0209 |
| disperso | hilbert | 4.0 | 42 | 437 | 0.816 | 0.302 | 0.0123 |
| disperso | etapa2_bulk | 5.9 | 63 | 416 | 0.700 | 0.405 | -0.0000 |
| disperso | etapa2_bulk_data | 6.0 | 64 | 421 | 0.692 | 0.400 | -0.0008 |
| disperso | hilbert_etapa1 | 4.0 | 42 | 437 | 0.798 | 0.291 | 0.0121 |
| disperso | etapa2_etapa1 | 4.0 | 42 | 430 | 0.883 | 0.331 | 0.0182 |
