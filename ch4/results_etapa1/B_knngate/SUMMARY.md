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
| concentrado | 2 | 450 | 0.290 ± 0.060 |
| disperso | 8 | 114 | 0.320 ± 0.031 |
| uniforme | 0 | - | 0.741 ± 0.040 |

Region centres drawn per repetition (`workload.csv` has every query):

- **concentrado** - 40 distinct centres over 20 repetitions. rep 0: Carnaubal-CE, Floreal-SP; rep 1: Novo Triunfo-BA, Graça Aranha-MA; rep 2: Vassouras-RJ, Tianguá-CE; rep 3: São Luiz-RR, Mauá da Serra-PR; rep 4: Conde-BA, Iraceminha-SC; ...
- **disperso** - 158 distinct centres over 20 repetitions. rep 0: Carnaubal-CE, Vitória do Jari-AP, São Francisco do Oeste-RN, Floreal-SP, Sarzedo-MG, Taió-SC, Várzea Branca-PI, Ponto Chique-MG; rep 1: Novo Triunfo-BA, Graça Aranha-MA, Bom Sucesso do Sul-PR, Kaloré-PR, Formosa do Rio Preto-BA, Cumari-GO, Campo Grande-MS, Montenegro-RS; rep 2: Vassouras-RJ, Tianguá-CE, Sítio do Quinto-BA, Sítio Novo-RN, Rio Paranaíba-MG, Itapuranga-GO, Junqueirópolis-SP, Brasnorte-MT; rep 3: São Luiz-RR, Almas-TO, Mauá da Serra-PR, Santa Maria-RN, Tanguá-RJ, Meleiro-SC, Pingo-d'Água-MG, Santo Antônio do Jardim-SP; rep 4: Conde-BA, Iraceminha-SC, Pindorama do Tocantins-TO, Boa Vista-RR, Rio Pomba-MG, Cachoeirinha-RS, Solânea-PB, Varjão de Minas-MG; ...


## Distance computations per query

Lower is better. Mean over repetitions, then the paired change against `original` ± 95% CI.

| regime | query | original | slimdown | etapa1 | etapa2 | etapa2_data | hilbert | etapa2_bulk | etapa2_bulk_data | hilbert_etapa1 | etapa2_etapa1 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| concentrado | knn | 401.2 | 387.7 (-3.3% ± 0.6) | 396.9 (-1.0% ± 0.3) | 142.5 (-64.5% ± 2.0) | 137.3 (-65.5% ± 2.3) | 131.6 (-66.9% ± 1.6) | 101.0 (-74.7% ± 1.6) | 101.2 (-74.6% ± 1.1) | 131.9 (-66.9% ± 1.6) | 143.9 (-64.1% ± 2.0) |
| concentrado | range | 345.6 | 333.7 (-3.4% ± 0.4) | 341.6 (-1.2% ± 0.3) | 117.8 (-66.1% ± 1.2) | 126.0 (-63.6% ± 2.0) | 114.0 (-67.0% ± 1.5) | 94.3 (-72.7% ± 1.1) | 94.7 (-72.6% ± 1.1) | 113.8 (-67.1% ± 1.6) | 118.0 (-66.1% ± 1.2) |
| disperso | knn | 399.2 | 386.6 (-3.1% ± 0.4) | 394.6 (-1.1% ± 0.2) | 143.0 (-64.1% ± 1.7) | 144.8 (-63.6% ± 1.9) | 126.4 (-68.2% ± 1.0) | 95.6 (-76.0% ± 0.8) | 98.7 (-75.2% ± 0.8) | 126.8 (-68.1% ± 1.0) | 143.8 (-63.9% ± 1.7) |
| disperso | range | 342.6 | 331.1 (-3.3% ± 0.3) | 337.7 (-1.4% ± 0.2) | 124.0 (-63.8% ± 1.6) | 127.0 (-62.9% ± 1.3) | 112.4 (-67.1% ± 1.0) | 95.8 (-72.0% ± 0.7) | 94.7 (-72.3% ± 0.9) | 112.5 (-67.1% ± 1.0) | 124.0 (-63.8% ± 1.6) |
| uniforme | knn | 401.6 | 388.7 (-3.2% ± 0.4) | 393.3 (-2.1% ± 0.3) | 134.8 (-66.4% ± 1.5) | 137.5 (-65.7% ± 1.6) | 125.1 (-68.8% ± 0.7) | 95.8 (-76.1% ± 0.7) | 98.0 (-75.5% ± 0.6) | 124.7 (-68.9% ± 0.7) | 135.0 (-66.3% ± 1.5) |
| uniforme | range | 343.8 | 331.7 (-3.5% ± 0.3) | 335.8 (-2.3% ± 0.2) | 122.0 (-64.5% ± 1.1) | 121.3 (-64.7% ± 1.1) | 112.3 (-67.3% ± 0.8) | 93.6 (-72.8% ± 0.6) | 94.0 (-72.6% ± 0.6) | 111.9 (-67.4% ± 0.8) | 121.9 (-64.5% ± 1.2) |

## Page reads per query

Page reads equal nodes entered: the page manager has no buffer.

| regime | query | original | slimdown | etapa1 | etapa2 | etapa2_data | hilbert | etapa2_bulk | etapa2_bulk_data | hilbert_etapa1 | etapa2_etapa1 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| concentrado | knn | 70.9 | 65.5 (-7.4% ± 0.7) | 68.3 (-3.5% ± 0.4) | 21.7 (-69.4% ± 1.7) | 20.8 (-70.5% ± 1.7) | 18.2 (-74.2% ± 1.0) | 12.9 (-81.7% ± 1.0) | 12.7 (-82.0% ± 0.9) | 17.7 (-74.8% ± 0.9) | 21.2 (-70.1% ± 1.6) |
| concentrado | range | 69.4 | 64.0 (-7.6% ± 0.7) | 66.8 (-3.7% ± 0.4) | 20.2 (-70.9% ± 1.5) | 20.8 (-69.9% ± 1.7) | 17.7 (-74.4% ± 1.2) | 13.2 (-80.9% ± 1.0) | 13.0 (-81.1% ± 1.1) | 17.3 (-75.0% ± 1.1) | 19.7 (-71.6% ± 1.4) |
| disperso | knn | 70.4 | 65.0 (-7.7% ± 0.6) | 67.8 (-3.7% ± 0.4) | 21.4 (-69.6% ± 1.5) | 21.9 (-68.9% ± 1.7) | 17.7 (-74.8% ± 0.7) | 11.9 (-83.1% ± 0.6) | 12.3 (-82.5% ± 0.7) | 17.4 (-75.2% ± 0.7) | 21.1 (-70.0% ± 1.5) |
| disperso | range | 68.7 | 63.3 (-7.7% ± 0.6) | 66.2 (-3.6% ± 0.4) | 20.6 (-70.0% ± 1.7) | 21.3 (-68.8% ± 1.5) | 17.5 (-74.4% ± 0.8) | 12.8 (-81.3% ± 0.5) | 12.8 (-81.2% ± 0.7) | 17.2 (-74.9% ± 0.8) | 20.3 (-70.5% ± 1.7) |
| uniforme | knn | 69.9 | 64.7 (-7.5% ± 0.4) | 66.2 (-5.4% ± 0.4) | 20.9 (-70.0% ± 1.4) | 20.8 (-70.2% ± 1.4) | 17.3 (-75.2% ± 0.5) | 12.0 (-82.9% ± 0.4) | 12.3 (-82.4% ± 0.4) | 17.0 (-75.6% ± 0.5) | 20.7 (-70.4% ± 1.3) |
| uniforme | range | 68.1 | 62.9 (-7.6% ± 0.5) | 64.4 (-5.3% ± 0.4) | 20.8 (-69.4% ± 1.3) | 20.3 (-70.1% ± 1.4) | 17.3 (-74.5% ± 0.7) | 12.6 (-81.4% ± 0.5) | 12.8 (-81.1% ± 0.5) | 17.0 (-74.9% ± 0.7) | 20.6 (-69.7% ± 1.2) |

## Wall-clock time per query (ms)

Supporting evidence only: tens of microseconds, on a laptop.

| regime | query | original | slimdown | etapa1 | etapa2 | etapa2_data | hilbert | etapa2_bulk | etapa2_bulk_data | hilbert_etapa1 | etapa2_etapa1 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| concentrado | knn | 0.139 | 0.125 (-9.3% ± 4.6) | 0.130 (-6.2% ± 4.0) | 0.049 (-64.8% ± 2.7) | 0.045 (-67.6% ± 3.3) | 0.040 (-71.1% ± 1.5) | 0.032 (-77.0% ± 2.3) | 0.030 (-78.3% ± 1.5) | 0.040 (-71.0% ± 2.0) | 0.045 (-67.6% ± 2.3) |
| concentrado | range | 0.130 | 0.119 (-7.6% ± 3.7) | 0.121 (-6.5% ± 2.7) | 0.040 (-69.2% ± 1.9) | 0.042 (-67.6% ± 2.6) | 0.038 (-70.0% ± 3.1) | 0.029 (-77.8% ± 1.6) | 0.029 (-77.8% ± 2.0) | 0.036 (-72.2% ± 2.4) | 0.040 (-68.9% ± 2.3) |
| disperso | knn | 0.134 | 0.122 (-9.0% ± 1.6) | 0.129 (-3.7% ± 1.5) | 0.046 (-65.6% ± 1.8) | 0.046 (-65.5% ± 1.6) | 0.041 (-69.2% ± 1.6) | 0.028 (-79.4% ± 0.8) | 0.029 (-78.6% ± 0.9) | 0.037 (-72.3% ± 0.9) | 0.048 (-64.3% ± 3.3) |
| disperso | range | 0.130 | 0.117 (-9.5% ± 2.9) | 0.123 (-5.0% ± 2.8) | 0.041 (-68.3% ± 2.2) | 0.043 (-66.4% ± 1.8) | 0.036 (-71.9% ± 1.1) | 0.029 (-77.7% ± 1.4) | 0.028 (-78.1% ± 1.8) | 0.037 (-71.5% ± 1.5) | 0.041 (-67.9% ± 2.6) |
| uniforme | knn | 0.136 | 0.124 (-9.0% ± 3.1) | 0.128 (-5.5% ± 4.0) | 0.046 (-66.1% ± 1.6) | 0.044 (-67.8% ± 1.3) | 0.039 (-71.2% ± 1.3) | 0.029 (-78.2% ± 1.7) | 0.029 (-78.5% ± 0.9) | 0.038 (-71.7% ± 1.7) | 0.045 (-66.8% ± 1.9) |
| uniforme | range | 0.127 | 0.115 (-8.9% ± 2.3) | 0.119 (-6.2% ± 2.6) | 0.041 (-67.4% ± 1.5) | 0.041 (-67.3% ± 1.5) | 0.037 (-70.6% ± 1.3) | 0.029 (-77.4% ± 1.3) | 0.028 (-77.5% ± 1.6) | 0.035 (-72.4% ± 0.9) | 0.043 (-66.3% ± 3.0) |

## Where the gain comes from

Each row compares an arm with a control on the SAME workload and insertion order, repetition by repetition. `etapa2` and `etapa2_data` share pipeline and |P|; `etapa2_bulk` and `etapa2_bulk_data` share every line of code but the query share. Negative means the first arm computes FEWER distances; `lower in` counts the repetitions in which it did.

| regime | query | comparison | distances | lower in | page reads |
|---|---|---|---|---|---|
| concentrado | knn | etapa2 vs etapa2_data | +4.4% ± 7.7 | 8/20 | +5.0% ± 7.8 |
| concentrado | range | etapa2 vs etapa2_data | -6.3% ± 3.7 | 17/20 | -2.6% ± 5.9 |
| disperso | knn | etapa2 vs etapa2_data | -0.3% ± 6.6 | 9/20 | -1.0% ± 7.4 |
| disperso | range | etapa2 vs etapa2_data | -1.7% ± 6.6 | 10/20 | -2.7% ± 7.6 |
| uniforme | knn | etapa2 vs etapa2_data | -1.7% ± 4.0 | 12/20 | +1.5% ± 6.0 |
| uniforme | range | etapa2 vs etapa2_data | +0.8% ± 3.6 | 10/20 | +3.1% ± 6.0 |
| concentrado | knn | etapa2 vs hilbert | +8.5% ± 8.1 | 7/20 | +19.7% ± 8.8 |
| concentrado | range | etapa2 vs hilbert | +3.5% ± 4.7 | 9/20 | +14.4% ± 6.8 |
| disperso | knn | etapa2 vs hilbert | +13.2% ± 5.5 | 4/20 | +21.0% ± 6.6 |
| disperso | range | etapa2 vs hilbert | +10.3% ± 5.2 | 3/20 | +17.4% ± 6.7 |
| uniforme | knn | etapa2 vs hilbert | +7.9% ± 4.8 | 6/20 | +21.0% ± 5.8 |
| uniforme | range | etapa2 vs hilbert | +8.7% ± 3.6 | 3/20 | +20.4% ± 5.3 |
| concentrado | knn | etapa2_bulk vs etapa2_bulk_data | -0.0% ± 5.3 | 9/20 | +2.2% ± 5.4 |
| concentrado | range | etapa2_bulk vs etapa2_bulk_data | -0.3% ± 2.0 | 12/20 | +1.4% ± 2.6 |
| disperso | knn | etapa2_bulk vs etapa2_bulk_data | -3.0% ± 2.6 | 14/20 | -3.2% ± 3.1 |
| disperso | range | etapa2_bulk vs etapa2_bulk_data | +1.1% ± 1.5 | 7/20 | +0.2% ± 2.3 |
| uniforme | knn | etapa2_bulk vs etapa2_bulk_data | -2.2% ± 2.7 | 12/20 | -2.5% ± 1.9 |
| uniforme | range | etapa2_bulk vs etapa2_bulk_data | -0.4% ± 1.4 | 10/20 | -1.6% ± 1.6 |
| concentrado | knn | etapa2_bulk vs hilbert | -23.3% ± 4.0 | 19/20 | -28.7% ± 3.8 |
| concentrado | range | etapa2_bulk vs hilbert | -17.0% ± 2.6 | 20/20 | -25.4% ± 2.4 |
| disperso | knn | etapa2_bulk vs hilbert | -24.4% ± 1.9 | 20/20 | -32.9% ± 1.8 |
| disperso | range | etapa2_bulk vs hilbert | -14.8% ± 1.6 | 20/20 | -26.6% ± 1.5 |
| uniforme | knn | etapa2_bulk vs hilbert | -23.4% ± 1.9 | 20/20 | -31.0% ± 0.9 |
| uniforme | range | etapa2_bulk vs hilbert | -16.7% ± 1.0 | 20/20 | -27.2% ± 0.7 |
| concentrado | knn | etapa2_bulk vs etapa2 | -27.8% ± 6.0 | 19/20 | -39.3% ± 4.8 |
| concentrado | range | etapa2_bulk vs etapa2 | -19.3% ± 2.9 | 20/20 | -34.0% ± 3.5 |
| disperso | knn | etapa2_bulk vs etapa2 | -32.5% ± 3.6 | 20/20 | -43.8% ± 3.5 |
| disperso | range | etapa2_bulk vs etapa2 | -22.1% ± 3.2 | 20/20 | -36.7% ± 3.4 |
| uniforme | knn | etapa2_bulk vs etapa2 | -28.4% ± 3.3 | 20/20 | -42.3% ± 2.9 |
| uniforme | range | etapa2_bulk vs etapa2 | -23.0% ± 2.7 | 20/20 | -39.0% ± 2.8 |
| concentrado | knn | hilbert_etapa1 vs hilbert | +0.2% ± 0.3 | 9/20 | -2.3% ± 0.3 |
| concentrado | range | hilbert_etapa1 vs hilbert | -0.2% ± 0.2 | 14/20 | -2.2% ± 0.3 |
| disperso | knn | hilbert_etapa1 vs hilbert | +0.3% ± 0.2 | 3/20 | -1.8% ± 0.2 |
| disperso | range | hilbert_etapa1 vs hilbert | +0.1% ± 0.2 | 8/20 | -1.7% ± 0.2 |
| uniforme | knn | hilbert_etapa1 vs hilbert | -0.3% ± 0.1 | 17/20 | -1.6% ± 0.2 |
| uniforme | range | hilbert_etapa1 vs hilbert | -0.4% ± 0.1 | 19/20 | -1.6% ± 0.2 |
| concentrado | knn | etapa2_etapa1 vs etapa2 | +1.0% ± 0.4 | 1/20 | -2.2% ± 0.3 |
| concentrado | range | etapa2_etapa1 vs etapa2 | +0.2% ± 0.2 | 6/20 | -2.2% ± 0.3 |
| disperso | knn | etapa2_etapa1 vs etapa2 | +0.6% ± 0.3 | 5/20 | -1.5% ± 0.2 |
| disperso | range | etapa2_etapa1 vs etapa2 | +0.0% ± 0.3 | 10/20 | -1.4% ± 0.3 |
| uniforme | knn | etapa2_etapa1 vs etapa2 | +0.1% ± 0.2 | 10/20 | -1.3% ± 0.2 |
| uniforme | range | etapa2_etapa1 vs etapa2 | -0.1% ± 0.2 | 11/20 | -1.1% ± 0.2 |

An interval that contains zero means the difference is not distinguishable from noise at this number of repetitions.


## Leaf depth inside and outside the queried regions

Mean depth (root = 1) of the leaf holding an object, for objects inside a queried region (hot) and outside every one (cold). Insertion-built trees are balanced, so both equal the height; only the top-down arms can differ. `hot - cold` is paired per repetition: negative means queried regions sit closer to the root.

| regime | arm | hot | cold | hot - cold |
|---|---|---|---|---|
| concentrado | etapa2_bulk | 4.01 | 4.38 | -0.37 ± 0.07 |
| concentrado | etapa2_bulk_data | 4.45 | 4.33 | 0.12 ± 0.17 |
| disperso | etapa2_bulk | 4.27 | 4.31 | -0.04 ± 0.07 |
| disperso | etapa2_bulk_data | 4.42 | 4.34 | 0.08 ± 0.08 |


## Cost of reorganising, and when it pays

`cost` is the distances spent turning the original tree into the measured one (for etapa2: T_Q + Equation 4.9 + building T'; for hilbert: the rebuild). `saving` is distances saved per query against `original`. `break-even` is the median, over repetitions, of cost / saving: the number of queries after which the reorganisation has paid for itself.

| regime | query | arm | cost | saving/query | break-even (queries) |
|---|---|---|---|---|---|
| concentrado | knn | slimdown | 44534 | 13.5 | 3447 |
| concentrado | knn | etapa1 | 71583 | 4.2 | 18933 |
| concentrado | knn | etapa2 | 344944 | 258.7 | 1331 |
| concentrado | knn | etapa2_data | 408531 | 263.9 | 1609 |
| concentrado | knn | hilbert | 261791 | 269.6 | 1026 |
| concentrado | knn | etapa2_bulk | 189982 | 300.2 | 667 |
| concentrado | knn | etapa2_bulk_data | 190116 | 299.9 | 652 |
| concentrado | knn | hilbert_etapa1 | 270170 | 269.3 | 1065 |
| concentrado | knn | etapa2_etapa1 | 354637 | 257.3 | 1366 |
| concentrado | range | slimdown | 44534 | 12.0 | 3933 |
| concentrado | range | etapa1 | 69417 | 4.0 | 16662 |
| concentrado | range | etapa2 | 344944 | 227.9 | 1531 |
| concentrado | range | etapa2_data | 408531 | 219.7 | 1917 |
| concentrado | range | hilbert | 261791 | 231.6 | 1183 |
| concentrado | range | etapa2_bulk | 189982 | 251.3 | 772 |
| concentrado | range | etapa2_bulk_data | 190116 | 250.9 | 763 |
| concentrado | range | hilbert_etapa1 | 269805 | 231.9 | 1218 |
| concentrado | range | etapa2_etapa1 | 353609 | 227.7 | 1578 |
| disperso | knn | slimdown | 44534 | 12.6 | 3350 |
| disperso | knn | etapa1 | 69812 | 4.6 | 15868 |
| disperso | knn | etapa2 | 350001 | 256.2 | 1321 |
| disperso | knn | etapa2_data | 415344 | 254.4 | 1673 |
| disperso | knn | hilbert | 261791 | 272.8 | 979 |
| disperso | knn | etapa2_bulk | 190209 | 303.6 | 630 |
| disperso | knn | etapa2_bulk_data | 190076 | 300.5 | 627 |
| disperso | knn | hilbert_etapa1 | 269749 | 272.4 | 1010 |
| disperso | knn | etapa2_etapa1 | 360441 | 255.4 | 1369 |
| disperso | range | slimdown | 44534 | 11.5 | 3908 |
| disperso | range | etapa1 | 67259 | 4.8 | 14385 |
| disperso | range | etapa2 | 350001 | 218.5 | 1592 |
| disperso | range | etapa2_data | 415344 | 215.6 | 1965 |
| disperso | range | hilbert | 261791 | 230.2 | 1152 |
| disperso | range | etapa2_bulk | 190209 | 246.8 | 778 |
| disperso | range | etapa2_bulk_data | 190076 | 247.8 | 775 |
| disperso | range | hilbert_etapa1 | 269608 | 230.0 | 1188 |
| disperso | range | etapa2_etapa1 | 359786 | 218.5 | 1638 |
| uniforme | knn | slimdown | 44534 | 12.8 | 3555 |
| uniforme | knn | etapa1 | 65527 | 8.3 | 8384 |
| uniforme | knn | etapa2 | 337365 | 266.8 | 1289 |
| uniforme | knn | etapa2_data | 395179 | 264.1 | 1510 |
| uniforme | knn | hilbert | 261791 | 276.5 | 956 |
| uniforme | knn | etapa2_bulk | 189986 | 305.8 | 630 |
| uniforme | knn | etapa2_bulk_data | 189984 | 303.5 | 642 |
| uniforme | knn | hilbert_etapa1 | 269864 | 276.9 | 984 |
| uniforme | knn | etapa2_etapa1 | 347221 | 266.6 | 1324 |
| uniforme | range | slimdown | 44534 | 12.1 | 3715 |
| uniforme | range | etapa1 | 63044 | 8.1 | 7657 |
| uniforme | range | etapa2 | 337365 | 221.8 | 1532 |
| uniforme | range | etapa2_data | 395179 | 222.6 | 1816 |
| uniforme | range | hilbert | 261791 | 231.5 | 1133 |
| uniforme | range | etapa2_bulk | 189986 | 250.3 | 769 |
| uniforme | range | etapa2_bulk_data | 189984 | 249.9 | 754 |
| uniforme | range | hilbert_etapa1 | 269520 | 232.0 | 1164 |
| uniforme | range | etapa2_etapa1 | 346991 | 221.9 | 1568 |

## H2 - the destination radius never grows

Etapa 1 relocated objects in 360 measured configuration(s). The driver aborts if the destination radius ever grows, and it did not: every run completed. Equation 4.3 holds by construction and was checked at run time.


## H3 - amortisation of Etapa 1

| regime | query | break-even | saving per measure pass | cumulative adaptation cost |
|---|---|---|---|---|
| concentrado | knn | not reached | 423.5 | 71583.2 |
| concentrado | range | not reached | 400.1 | 69417.3 |
| disperso | knn | not reached | 463.8 | 69811.8 |
| disperso | range | not reached | 484.9 | 67258.9 |
| uniforme | knn | not reached | 832.6 | 65526.8 |
| uniforme | range | not reached | 807.2 | 63043.7 |

Saving and cost are both in distance computations and are averaged over repetitions chunk by chunk. A break-even of *not reached* means the measured saving never grew large enough to repay what the adaptation spent.


## H4 - convergence of the relocation rate

Mean relocations per adaptation query, in blocks:

| regime | block 1 | block 2 | block 3 | block 4 |
|---|---|---|---|---|
| concentrado | 2.20 | 0.45 | 0.24 | 0.11 |
| disperso | 2.82 | 0.86 | 0.36 | 0.20 |
| uniforme | 4.11 | 2.47 | 1.37 | 0.90 |

A decaying rate supports H4: the structure is converging. A flat rate under `uniforme` is the predicted adverse case - with no privileged region there is nothing to converge to, and objects keep being moved in conflicting directions.


## Structure

Mean over repetitions, k-NN runs (only `etapa1` differs between query types). The fat factor assumes every leaf at depth H, so it is NOT comparable for the top-down arms, which are not height-balanced.

| regime | arm | height | index nodes | leaves | mean leaf radius | overlap ratio | fat factor |
|---|---|---|---|---|---|---|---|
| concentrado | original | 4.0 | 27 | 356 | 3.607 | 0.375 | 0.1425 |
| concentrado | slimdown | 4.0 | 27 | 340 | 3.404 | 0.335 | 0.1361 |
| concentrado | etapa1 | 4.0 | 27 | 356 | 3.537 | 0.364 | 0.1397 |
| concentrado | etapa2 | 4.0 | 43 | 440 | 0.854 | 0.331 | 0.0161 |
| concentrado | etapa2_data | 4.0 | 41 | 423 | 0.922 | 0.328 | 0.0193 |
| concentrado | hilbert | 4.0 | 42 | 437 | 0.816 | 0.302 | 0.0123 |
| concentrado | etapa2_bulk | 6.0 | 64 | 417 | 0.692 | 0.401 | 0.0001 |
| concentrado | etapa2_bulk_data | 6.0 | 64 | 421 | 0.692 | 0.400 | -0.0008 |
| concentrado | hilbert_etapa1 | 4.0 | 42 | 437 | 0.807 | 0.296 | 0.0122 |
| concentrado | etapa2_etapa1 | 4.0 | 43 | 440 | 0.843 | 0.325 | 0.0159 |
| disperso | original | 4.0 | 27 | 356 | 3.607 | 0.375 | 0.1425 |
| disperso | slimdown | 4.0 | 27 | 340 | 3.404 | 0.335 | 0.1361 |
| disperso | etapa1 | 4.0 | 27 | 356 | 3.492 | 0.357 | 0.1378 |
| disperso | etapa2 | 4.0 | 43 | 437 | 0.862 | 0.338 | 0.0167 |
| disperso | etapa2_data | 4.0 | 41 | 424 | 0.944 | 0.334 | 0.0205 |
| disperso | hilbert | 4.0 | 42 | 437 | 0.816 | 0.302 | 0.0123 |
| disperso | etapa2_bulk | 5.9 | 63 | 412 | 0.698 | 0.406 | -0.0003 |
| disperso | etapa2_bulk_data | 6.0 | 64 | 421 | 0.692 | 0.400 | -0.0008 |
| disperso | hilbert_etapa1 | 4.0 | 42 | 437 | 0.805 | 0.295 | 0.0121 |
| disperso | etapa2_etapa1 | 4.0 | 43 | 437 | 0.849 | 0.329 | 0.0165 |
| uniforme | original | 4.0 | 27 | 356 | 3.607 | 0.375 | 0.1425 |
| uniforme | slimdown | 4.0 | 27 | 340 | 3.404 | 0.335 | 0.1361 |
| uniforme | etapa1 | 4.0 | 27 | 356 | 3.383 | 0.341 | 0.1333 |
| uniforme | etapa2 | 4.0 | 43 | 429 | 0.929 | 0.351 | 0.0195 |
| uniforme | etapa2_data | 4.0 | 41 | 427 | 0.919 | 0.331 | 0.0192 |
| uniforme | hilbert | 4.0 | 42 | 437 | 0.816 | 0.302 | 0.0123 |
| uniforme | etapa2_bulk | 5.9 | 64 | 420 | 0.682 | 0.398 | -0.0007 |
| uniforme | etapa2_bulk_data | 6.0 | 64 | 421 | 0.692 | 0.400 | -0.0008 |
| uniforme | hilbert_etapa1 | 4.0 | 42 | 437 | 0.790 | 0.288 | 0.0119 |
| uniforme | etapa2_etapa1 | 4.0 | 43 | 429 | 0.901 | 0.336 | 0.0191 |
