// ---------------------------------------------------------------------------
// chapter4.cpp - the section 4.5 experiment.
//
// Compares the FOUR configurations section 4.5 asks for, under three query
// regimes, for both range and k-NN queries:
//
//   original  the Slim-tree as built
//   slimdown  the same tree after the classic Slim-Down (Optimize())
//   etapa1    the tree adapted by query-guided relocation   (section 4.3)
//   etapa2    a new tree built from the Voronoi partition   (section 4.4)
//
// plus two controls that say where the etapa2 gain comes from:
//
//   etapa2_data  the etapa2 pipeline with pivots drawn from the DATA
//   hilbert      cities inserted along a Hilbert curve: insertion order alone
//
// Regimes (section 4.5): concentrado, disperso, uniforme. The last is the
// adverse case for Etapa 1 and is meant to be.
//
// PROTOCOL
// --------
// The query load is split DISJOINTLY into an adapt set and a measure set,
// drawn from the same distribution, because section 4.5 requires one sequence
// to adapt the tree and a DIFFERENT one to measure it. Etapa 2 uses the same
// split: the adapt set builds T_Q, the measure set evaluates T'.
//
// Every repetition uses its OWN seed (--seed + repetition), so the regions, the
// queries and the insertion order all change between repetitions and the
// repetitions are independent samples. Within one repetition every arm builds
// its own tree from the SAME insertion order, rather than copying index files,
// so no arm can inherit another page layout and the arms can be compared
// pairwise.
//
// All four arms are run in one process so that the H3 break-even can be
// computed against the `original` arm directly, with no cross-process join.
//
// ACCOUNTING
// ----------
// The metric evaluator keeps ONE global counter, so query cost and adaptation
// cost are separated by monotone delta snapshots taken around each phase.
// ResetStatistics() is never called mid-batch, which would destroy the
// construction accounting. Etapa 1 and Etapa 2 additionally own their own
// evaluators, so their distances are already disjoint - the deltas are there to
// catch anything that is not.
//
// Structural metrics and the fat factor are collected AFTER the CSV row for the
// repetition has been written: GetFatFactor() alone costs millions of distances
// and page reads, and there is no way to give the counters back.
//
// Build and run:
//   bash arboretum_SlimTree_QueryGravity/ch4/build_ch4.sh chapter4
// ---------------------------------------------------------------------------

#include <arboretum/stPlainDiskPageManager.h>
#include <arboretum/stSlimTree.h>
#include <arboretum/stSlimEtapa1.h>
#include <arboretum/stSlimMetrics.h>
#include <arboretum/stSlimVoronoiRebuild.h>
#include <arboretum/stSlimQueryBulkLoad.h>

#include "ch4_common.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#if !ST_SLIM_TRACE
#error "chapter4.cpp requires -DST_SLIM_TRACE=1"
#endif

typedef stResult<TCity> myResult;
typedef stSlimTree<TCity, TCityDistanceEvaluator> mySlimTree;
typedef stSlimEtapa1<TCity, TCityDistanceEvaluator> myEtapa1;
typedef stSlimMetrics<TCity, TCityDistanceEvaluator> myMetrics;
typedef stSlimVoronoiRebuild<TCity, TCityDistanceEvaluator> myRebuild;
typedef stSlimQueryBulkLoad<TCity, TCityDistanceEvaluator> myBulk;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
struct Config {
    std::string dataset, datasetLabel = "brcities", queryFile;
    std::string indexDir = ".", outDir = ".";
    std::string regime = "concentrado";
    std::string queryType = "both";
    unsigned int pageSize = 1024;
    unsigned int k = 10;
    double radius = 0.5;
    int adaptQueries = 200;
    int measureQueries = 100;
    int warmupQueries = 10;
    int chunk = 25;          // H3: measure after every `chunk` adapt queries
    int reps = 1;
    int repOffset = 0;
    unsigned int seed = 42;  // base seed; repetition r uses seed + r
    int hotspots = 0;        // 0 = regime default
    double regionFactor = 3.0;  // region size = factor * queries drawn from it
    bool fileOrder = false;  // insert in file order instead of a per-rep shuffle
    bool verify = true;
    bool seedFirst = false;  // Etapa 2 variant
    bool knnGate = false;    // Etapa 1, Equation 4.8 gate
    double bulkQueryShare = 0.5;  // etapa2_bulk: fan-out share of query pivots
    int pivots = 0;          // etapa2 |P|; 0 = the root of T_Q, whatever its size
    std::string commit = "";
};

// ---------------------------------------------------------------------------
// Workload generation (section 4.5)
//
// A regime is a set of REGIONS plus a rule for drawing queries from them:
//
//   concentrado  2 regions      disperso  8 regions      uniforme  none
//
// A region is a metric ball: a centre city and its `regionSize` nearest cities.
// Centres are drawn at random from the CITIES - so dense parts of the country
// are picked more often, as they would be by real users - and a candidate is
// rejected when its ball intersects one already chosen, d(c_i,c_j) <= r_i + r_j,
// so the regions are genuinely separate.
//
// Inside each region the cities are shuffled and split DISJOINTLY: the first
// ones become adapt queries, the next ones measure queries, and the rest - a
// third of the region with the default --region-factor 3 - is never queried.
// Every region contributes its share to BOTH sets, and both sequences are then
// shuffled so the regions arrive mixed, as one stream.
//
// What this replaces, and why:
//  - Centres came from farthest-point sampling, which always lands on the
//    borders of the country (the first two were Codo-MA and Chui-RS) and, with
//    the seed fixed, on the same ones in every repetition.
//  - A region was a list ranked by distance and split by alternating
//    positions, so every measure query had an adapt query next to it in the
//    ranking - a twin the reorganisation had been built from.
//  - The split kept the first 100 measure positions, which covered only the
//    first region(s): 1 of 2 under concentrado, 4 of 8 under disperso.
//  - uniforme drew with replacement, so one city could be in both sets.
// ---------------------------------------------------------------------------
struct Workload {
    std::vector<TCity *> adapt;
    std::vector<TCity *> measure;
    std::vector<int> adaptRegion, measureRegion;  // -1 under uniforme
    std::vector<TCity *> centres;                 // not owned
    std::vector<double> centreRadius;
    int regionSize = 0;
    int attempts = 0;  // centre candidates drawn, rejected ones included
    /** Mean over measure queries of the distance to the nearest adapt query. */
    double measureToAdapt = 0.0;
};

// Farthest-point sampling over the DATA. Used only by the etapa2_data control,
// which needs |P| well spread pivots that owe nothing to the queries.
static std::vector<size_t> farthestPointSample(TCityDistanceEvaluator &eval,
                                               const std::vector<TCity *> &objects,
                                               int howMany, unsigned int seed) {
    std::vector<size_t> chosen;
    if (objects.empty() || howMany <= 0) return chosen;

    std::mt19937 rng(seed);
    chosen.push_back(rng() % objects.size());

    std::vector<double> best(objects.size(), 1e300);
    while ((int)chosen.size() < howMany) {
        TCity *last = objects[chosen.back()];  // GetDistance takes non-const refs
        size_t far = 0;
        double farDist = -1.0;
        for (size_t i = 0; i < objects.size(); i++) {
            const double d = eval.GetDistance(*objects[i], *last);
            if (d < best[i]) best[i] = d;
            if (best[i] > farDist) {
                farDist = best[i];
                far = i;
            }
        }
        chosen.push_back(far);
    }
    return chosen;
}

/** Share of `total` that part `i` of `parts` gets; the first ones get the rest. */
static int shareOf(int total, int parts, int i) {
    return total / parts + (i < total % parts ? 1 : 0);
}

/** Applies the same random permutation to a sequence and its region tags. */
static void shuffleTagged(std::vector<TCity *> &v, std::vector<int> &tag,
                          std::mt19937 &rng) {
    std::vector<size_t> perm(v.size());
    for (size_t i = 0; i < perm.size(); i++) perm[i] = i;
    std::shuffle(perm.begin(), perm.end(), rng);
    std::vector<TCity *> v2(v.size());
    std::vector<int> t2(tag.size());
    for (size_t i = 0; i < perm.size(); i++) {
        v2[i] = v[perm[i]];
        t2[i] = tag[perm[i]];
    }
    v.swap(v2);
    tag.swap(t2);
}

// The `pool` is used only for the uniform regime; the clustered regimes draw
// their queries from the indexed set so that a region really is populated.
static Workload makeWorkload(const Config &cfg, TCityDistanceEvaluator &eval,
                             const std::vector<TCity *> &objects,
                             const std::vector<TCity *> &pool, unsigned int seed) {
    Workload w;
    std::mt19937 rng(seed);

    if (cfg.regime == "uniforme") {
        // Adverse case: no privileged region for the tree to converge towards.
        // Drawn WITHOUT replacement, so no city is in both sets.
        if ((int)pool.size() < cfg.adaptQueries + cfg.measureQueries) {
            std::cerr << "ERROR: the query pool has " << pool.size()
                      << " cities; uniforme needs adapt + measure = "
                      << cfg.adaptQueries + cfg.measureQueries << "\n";
            std::exit(2);
        }
        std::vector<TCity *> shuffled(pool);
        std::shuffle(shuffled.begin(), shuffled.end(), rng);
        for (int i = 0; i < cfg.adaptQueries; i++) {
            w.adapt.push_back(shuffled[i]);
            w.adaptRegion.push_back(-1);
        }
        for (int i = 0; i < cfg.measureQueries; i++) {
            w.measure.push_back(shuffled[cfg.adaptQueries + i]);
            w.measureRegion.push_back(-1);
        }
    } else {
        const int regions = cfg.hotspots > 0
                                ? cfg.hotspots
                                : (cfg.regime == "concentrado" ? 2 : 8);
        // The largest region share decides the region size, so every region is
        // the same size and at least regionFactor times its query load.
        const int perRegion = shareOf(cfg.adaptQueries, regions, 0) +
                              shareOf(cfg.measureQueries, regions, 0);
        w.regionSize = (int)std::ceil(cfg.regionFactor * perRegion);
        if (w.regionSize < perRegion || w.regionSize > (int)objects.size()) {
            std::cerr << "ERROR: region size " << w.regionSize << " must lie in ["
                      << perRegion << ", " << objects.size() << "]\n";
            std::exit(2);
        }

        std::vector<std::vector<size_t> > members;
        std::uniform_int_distribution<size_t> pick(0, objects.size() - 1);
        const int maxAttempts = 100000;
        while ((int)members.size() < regions) {
            if (++w.attempts > maxAttempts) {
                std::cerr << "ERROR: could not place " << regions
                          << " disjoint regions of " << w.regionSize
                          << " cities; lower --region-factor or --centres\n";
                std::exit(2);
            }
            TCity *c = objects[pick(rng)];
            std::vector<std::pair<double, size_t> > byDist;
            byDist.reserve(objects.size());
            for (size_t i = 0; i < objects.size(); i++)
                byDist.push_back(std::make_pair(eval.GetDistance(*objects[i], *c), i));
            std::partial_sort(byDist.begin(), byDist.begin() + w.regionSize,
                              byDist.end());
            const double radius = byDist[w.regionSize - 1].first;

            bool clash = false;
            for (size_t j = 0; j < w.centres.size() && !clash; j++)
                clash = eval.GetDistance(*c, *w.centres[j]) <= radius + w.centreRadius[j];
            if (clash) continue;

            std::vector<size_t> m;
            for (int i = 0; i < w.regionSize; i++) m.push_back(byDist[i].second);
            members.push_back(m);
            w.centres.push_back(c);
            w.centreRadius.push_back(radius);
        }

        for (int g = 0; g < regions; g++) {
            std::vector<size_t> &m = members[g];
            std::shuffle(m.begin(), m.end(), rng);
            const int na = shareOf(cfg.adaptQueries, regions, g);
            const int nm = shareOf(cfg.measureQueries, regions, g);
            for (int i = 0; i < na; i++) {
                w.adapt.push_back(objects[m[i]]);
                w.adaptRegion.push_back(g);
            }
            for (int i = 0; i < nm; i++) {
                w.measure.push_back(objects[m[na + i]]);
                w.measureRegion.push_back(g);
            }
        }
        shuffleTagged(w.adapt, w.adaptRegion, rng);
        shuffleTagged(w.measure, w.measureRegion, rng);
    }

    // How close the measured queries are to the ones the structure was adapted
    // to. Reported, because Etapa 2 can only be as good as this proximity.
    double sum = 0.0;
    for (TCity *m : w.measure) {
        double best = 1e300;
        for (TCity *a : w.adapt) best = std::min(best, eval.GetDistance(*m, *a));
        sum += best;
    }
    w.measureToAdapt = w.measure.empty() ? 0.0 : sum / (double)w.measure.size();
    return w;
}

// ---------------------------------------------------------------------------
// Hilbert order - the `hilbert` reference arm
//
// The cheapest spatially coherent insertion order there is: sort by position
// on a Hilbert curve over (latitude, longitude). It uses COORDINATES, which a
// general metric space does not have, so it is a reference point rather than a
// competitor: it shows how much of any gain is available from insertion order
// alone, with no pivots and no queries.
// ---------------------------------------------------------------------------
static std::uint64_t hilbertIndex(std::uint32_t n, std::uint32_t x, std::uint32_t y) {
    std::uint64_t d = 0;
    for (std::uint32_t s = n / 2; s > 0; s /= 2) {
        const std::uint32_t rx = (x & s) ? 1 : 0;
        const std::uint32_t ry = (y & s) ? 1 : 0;
        d += (std::uint64_t)s * s * ((3 * rx) ^ ry);
        if (ry == 0) {
            if (rx == 1) {
                x = n - 1 - x;
                y = n - 1 - y;
            }
            std::swap(x, y);
        }
    }
    return d;
}

static std::vector<TCity *> hilbertOrder(const std::vector<TCity *> &objects) {
    if (objects.empty()) return objects;
    double minLat = 1e300, maxLat = -1e300, minLon = 1e300, maxLon = -1e300;
    for (TCity *o : objects) {
        minLat = std::min(minLat, o->GetLatitude());
        maxLat = std::max(maxLat, o->GetLatitude());
        minLon = std::min(minLon, o->GetLongitude());
        maxLon = std::max(maxLon, o->GetLongitude());
    }
    const std::uint32_t n = 1u << 16;
    auto cell = [n](double v, double lo, double hi) -> std::uint32_t {
        if (hi <= lo) return 0;
        const double t = (v - lo) / (hi - lo);
        return (std::uint32_t)std::min<double>(n - 1, std::floor(t * n));
    };
    // Stable on ties: equal keys keep the incoming (per-repetition) order.
    std::vector<std::pair<std::uint64_t, size_t> > key;
    for (size_t i = 0; i < objects.size(); i++)
        key.push_back(std::make_pair(
            hilbertIndex(n, cell(objects[i]->GetLatitude(), minLat, maxLat),
                         cell(objects[i]->GetLongitude(), minLon, maxLon)),
            i));
    std::sort(key.begin(), key.end());
    std::vector<TCity *> out;
    for (size_t i = 0; i < key.size(); i++) out.push_back(objects[key[i].second]);
    return out;
}

// ---------------------------------------------------------------------------
// One measured batch
// ---------------------------------------------------------------------------
struct Measurement {
    long queries = 0;
    double totalMs = 0.0, meanMs = 0.0, medianMs = 0.0, p95Ms = 0.0, stdMs = 0.0;
    double distances = 0.0, reads = 0.0, writes = 0.0;
    double nodesEntered = 0.0, leavesEntered = 0.0, pruned = 0.0;
    double indexDistances = 0.0, leafDistances = 0.0;
    double tauUpdates = 0.0, queuePops = 0.0, queuePopsDiscarded = 0.0;
    double resultSize = 0.0;
    long correct = -1;  // 1 all matched, 0 some did not, -1 not verified
};

static Measurement measureBatch(mySlimTree *tree, stPlainDiskPageManager *pm,
                                const std::vector<TCity *> &queries, bool knn,
                                const Config &cfg,
                                const std::vector<TCity *> &objects,
                                TCityDistanceEvaluator &verifyEval, bool verify,
                                int warmup) {
    Measurement m;
    if (queries.empty()) return m;

    // Warm-up, untimed and untraced, so the first query does not pay for cold
    // file-system caches on behalf of the batch.
    for (int i = 0; i < warmup && i < (int)queries.size(); i++) {
        myResult *r = knn ? tree->NearestQuery(queries[i], cfg.k)
                          : tree->RangeQuery(queries[i], cfg.radius);
        delete r;
    }

    std::vector<double> times;
    times.reserve(queries.size());
    long correctCount = 0;
    stSlimTrace trace;

    const long d0 = (long)tree->GetMetricEvaluator()->GetDistanceCount();
    const long r0 = (long)pm->GetReadCount();
    const long w0 = (long)pm->GetWriteCount();

    for (size_t i = 0; i < queries.size(); i++) {
        trace.Reset();
        stSlimActiveTrace = &trace;

        ch4::Timer t;
        myResult *r = knn ? tree->NearestQuery(queries[i], cfg.k)
                          : tree->RangeQuery(queries[i], cfg.radius);
        times.push_back(t.ms());

        stSlimActiveTrace = 0;

        m.nodesEntered += trace.NodesEntered;
        m.leavesEntered += trace.LeavesEntered;
        m.pruned += trace.GetSubtreesPruned();
        m.indexDistances += trace.IndexDistances;
        m.tauUpdates += trace.TauUpdates;
        m.queuePops += trace.QueuePops;
        m.queuePopsDiscarded += trace.QueuePopsDiscarded;
        m.resultSize += r->GetNumOfEntries();

        if (verify) {
            const std::vector<double> want =
                knn ? ch4::bruteKnn(verifyEval, objects, queries[i], cfg.k)
                    : ch4::bruteRange(verifyEval, objects, queries[i], cfg.radius);
            if (ch4::sameDistances(ch4::resultDistances(r), want)) correctCount++;
        }
        delete r;
    }

    const long dd = (long)tree->GetMetricEvaluator()->GetDistanceCount() - d0;
    m.queries = (long)queries.size();
    m.distances = (double)dd;
    m.reads = (double)((long)pm->GetReadCount() - r0);
    m.writes = (double)((long)pm->GetWriteCount() - w0);
    m.leafDistances = m.distances - m.indexDistances;

    m.totalMs = 0.0;
    for (double t : times) m.totalMs += t;
    m.meanMs = ch4::mean(times);
    m.medianMs = ch4::median(times);
    m.p95Ms = ch4::percentile(times, 0.95);
    m.stdMs = ch4::stddev(times);
    m.correct = verify ? (correctCount == (long)queries.size() ? 1 : 0) : -1;
    return m;
}

// ---------------------------------------------------------------------------
// CSV
// ---------------------------------------------------------------------------
static const char *AGG_HEADER =
    "commit,dataset,regime,centres,arm,query_type,k,radius,repetition,"
    "query_count,construction_time_ms,adapt_time_ms,"
    "total_query_time_ms,mean_query_time_ms,median_query_time_ms,"
    "p95_query_time_ms,std_query_time_ms,"
    "mean_distances,mean_index_distances,mean_leaf_distances,"
    "mean_reads,mean_writes,mean_nodes_entered,mean_leaves_entered,"
    "mean_subtrees_pruned,mean_tau_updates,mean_queue_pops,"
    "mean_queue_pops_discarded,mean_result_size,"
    "adapt_distances,adapt_reads,adapt_writes,objects_relocated,"
    "reverse_relocations,radius_shrink_total,"
    "tree_height,node_count,num_objects,index_size_bytes,"
    "correct_results,page_size,seed,"
    "construction_distances,reorg_distances,pivots,region_size,"
    "measure_to_adapt,insertion_order,leaf_depth_hot,leaf_depth_cold";

static const char *STRUCT_HEADER =
    "commit,dataset,regime,query_type,arm,repetition,tree_height,index_nodes,"
    "leaf_nodes,"
    "total_nodes,objects,mean_leaf_entries,min_leaf_entries,max_leaf_entries,"
    "mean_leaf_fill,mean_leaf_radius,min_leaf_radius,max_leaf_radius,"
    "sibling_pairs,overlapping_pairs,overlap_ratio,mean_abs_overlap,"
    "fat_factor,relative_fat_factor";

static const char *MOVES_HEADER =
    "commit,dataset,regime,query_type,repetition,query_index,moves,"
    "cumulative_moves";

static const char *BREAKEVEN_HEADER =
    "commit,dataset,regime,query_type,repetition,adapt_queries_so_far,"
    "cum_adapt_distances,cum_adapt_reads,cum_adapt_writes,cum_adapt_ms,"
    "measure_total_distances,measure_total_reads,measure_total_ms,"
    "baseline_total_distances,baseline_total_reads,baseline_total_ms";

static const char *WORKLOAD_HEADER =
    "commit,dataset,regime,repetition,seed,set,region,city,latitude,longitude,"
    "region_radius";

// ---------------------------------------------------------------------------
/** Per-row values that are not part of a Measurement. */
struct RowExtra {
    unsigned int seed = 0;
    /** Distances spent building the tree the arm starts from. */
    long constructionDistances = 0;
    /**
    * Distances spent turning that tree into the one that is measured:
    * Optimize() for slimdown, the adaptation for etapa1, T_Q (or the data
    * pivot sample) + Equation 4.9 + building T' for the etapa2 arms, and the
    * whole rebuild for hilbert. Zero for original. This is the cost to amortise.
    */
    long reorgDistances = 0;
    long pivots = 0;
    int regionSize = 0;
    double measureToAdapt = 0.0;
    std::string insertionOrder;
    /**
    * Mean depth of the leaf holding an object, for objects inside a queried
    * region (hot) and outside every one (cold). Equal to the height for a
    * balanced tree; the top-down arms are not balanced. Negative = no
    * regions (uniforme), written as an empty field.
    */
    double leafDepthHot = -1.0, leafDepthCold = -1.0;
};

static void writeAgg(std::ofstream &os, const Config &cfg, const std::string &arm,
                     const std::string &qtype, int rep, const Measurement &m,
                     double buildMs, double adaptMs, const myEtapa1::Stats *ad,
                     mySlimTree *tree, double indexBytes, int centres,
                     const RowExtra &x) {
    if (!os.is_open()) return;
    const double n = m.queries > 0 ? (double)m.queries : 1.0;
    os << cfg.commit << ',' << cfg.datasetLabel << ',' << cfg.regime << ','
       << centres << ',' << arm << ',' << qtype << ',' << cfg.k << ','
       << cfg.radius << ',' << rep << ',' << m.queries << ',' << buildMs << ','
       << adaptMs << ',' << m.totalMs << ',' << m.meanMs << ',' << m.medianMs
       << ',' << m.p95Ms << ',' << m.stdMs << ',' << m.distances / n << ','
       << m.indexDistances / n << ',' << m.leafDistances / n << ','
       << m.reads / n << ',' << m.writes / n << ',' << m.nodesEntered / n << ','
       << m.leavesEntered / n << ',' << m.pruned / n << ','
       << m.tauUpdates / n << ',' << m.queuePops / n << ','
       << m.queuePopsDiscarded / n << ',' << m.resultSize / n << ','
       << (ad ? ad->Distances : 0) << ',' << (ad ? ad->PageReads : 0) << ','
       << (ad ? ad->PageWrites : 0) << ',' << (ad ? ad->ObjectsRelocated : 0)
       << ',' << (ad ? ad->ReverseRelocations : 0) << ','
       << (ad ? ad->RadiusShrinkTotal : 0.0) << ',' << tree->GetHeight() << ','
       << tree->GetNodeCount() << ',' << tree->GetNumberOfObjects() << ','
       << indexBytes << ',' << m.correct << ',' << cfg.pageSize << ','
       << x.seed << ',' << x.constructionDistances << ',' << x.reorgDistances
       << ',' << x.pivots << ',' << x.regionSize << ',' << x.measureToAdapt
       << ',' << x.insertionOrder << ',';
    if (x.leafDepthHot >= 0.0) os << x.leafDepthHot;
    os << ',';
    if (x.leafDepthCold >= 0.0) os << x.leafDepthCold;
    os << "\n";
}

static void writeStruct(std::ofstream &os, const Config &cfg,
                        const std::string &qtype, const std::string &arm, int rep,
                        mySlimTree *tree, const myMetrics::Result &s, double fat,
                        double rfat) {
    if (!os.is_open()) return;
    os << cfg.commit << ',' << cfg.datasetLabel << ',' << cfg.regime << ',' << qtype
       << ',' << arm
       << ',' << rep << ',' << tree->GetHeight() << ',' << s.IndexNodes << ','
       << s.LeafNodes << ',' << s.TotalNodes << ',' << s.Objects << ','
       << s.MeanLeafEntries << ',' << s.MinLeafEntries << ',' << s.MaxLeafEntries
       << ',' << s.MeanLeafFill << ',' << s.MeanLeafRadius << ','
       << s.MinLeafRadius << ',' << s.MaxLeafRadius << ',' << s.SiblingPairs
       << ',' << s.OverlappingPairs << ',' << s.OverlapRatio << ','
       << s.MeanAbsOverlap << ',' << fat << ',' << rfat << "\n";
}

/** The centres and both query sets, so a repetition can be inspected or plotted. */
static void writeWorkload(std::ofstream &os, const Config &cfg, int rep,
                          unsigned int seed, const Workload &w) {
    if (!os.is_open()) return;
    auto row = [&](const char *set, int region, TCity *c, double radius) {
        os << cfg.commit << ',' << cfg.datasetLabel << ',' << cfg.regime << ','
           << rep << ',' << seed << ',' << set << ',' << region << ",\""
           << c->GetName() << "\"," << c->GetLatitude() << ','
           << c->GetLongitude() << ',' << radius << "\n";
    };
    for (size_t g = 0; g < w.centres.size(); g++)
        row("centre", (int)g, w.centres[g], w.centreRadius[g]);
    for (size_t i = 0; i < w.adapt.size(); i++)
        row("adapt", w.adaptRegion[i], w.adapt[i], 0.0);
    for (size_t i = 0; i < w.measure.size(); i++)
        row("measure", w.measureRegion[i], w.measure[i], 0.0);
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    std::cout << std::unitbuf;
    Config cfg;
    std::string aggOut, structOut, movesOut, breakevenOut, navOut, workloadOut;

    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&](const char *what) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "ERROR: " << what << " needs a value\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--dataset") cfg.dataset = next("--dataset");
        else if (a == "--dataset-label") cfg.datasetLabel = next("--dataset-label");
        else if (a == "--queryfile") cfg.queryFile = next("--queryfile");
        else if (a == "--index-dir") cfg.indexDir = next("--index-dir");
        else if (a == "--out-dir") cfg.outDir = next("--out-dir");
        else if (a == "--regime") cfg.regime = next("--regime");
        else if (a == "--query-type") cfg.queryType = next("--query-type");
        else if (a == "--page-size") cfg.pageSize = (unsigned)std::stoul(next("--page-size"));
        else if (a == "--k") cfg.k = (unsigned)std::stoul(next("--k"));
        else if (a == "--radius") cfg.radius = std::stod(next("--radius"));
        else if (a == "--adapt-queries") cfg.adaptQueries = std::stoi(next("--adapt-queries"));
        else if (a == "--measure-queries") cfg.measureQueries = std::stoi(next("--measure-queries"));
        else if (a == "--warmup-queries") cfg.warmupQueries = std::stoi(next("--warmup-queries"));
        else if (a == "--chunk") cfg.chunk = std::stoi(next("--chunk"));
        else if (a == "--reps") cfg.reps = std::stoi(next("--reps"));
        else if (a == "--rep-offset") cfg.repOffset = std::stoi(next("--rep-offset"));
        else if (a == "--seed") cfg.seed = (unsigned)std::stoul(next("--seed"));
        else if (a == "--centres") cfg.hotspots = std::stoi(next("--centres"));
        else if (a == "--region-factor") cfg.regionFactor = std::stod(next("--region-factor"));
        else if (a == "--file-order") cfg.fileOrder = true;
        else if (a == "--commit") cfg.commit = next("--commit");
        else if (a == "--no-verify") cfg.verify = false;
        else if (a == "--seed-first") cfg.seedFirst = true;
        else if (a == "--knn-gate") cfg.knnGate = true;
        else if (a == "--bulk-query-share") cfg.bulkQueryShare = std::stod(next("--bulk-query-share"));
        else if (a == "--pivots") cfg.pivots = std::stoi(next("--pivots"));
        else if (a == "--agg-out") aggOut = next("--agg-out");
        else if (a == "--struct-out") structOut = next("--struct-out");
        else if (a == "--moves-out") movesOut = next("--moves-out");
        else if (a == "--breakeven-out") breakevenOut = next("--breakeven-out");
        else if (a == "--nav-out") navOut = next("--nav-out");
        else if (a == "--workload-out") workloadOut = next("--workload-out");
        else if (a == "--help" || a == "-h") {
            std::cout
                << "usage: " << argv[0] << " --dataset <f> --queryfile <f>\n"
                << "  --regime concentrado|disperso|uniforme   (default concentrado)\n"
                << "  --query-type knn|range|both              (default both)\n"
                << "  --k N --radius R --page-size N\n"
                << "  --adapt-queries N --measure-queries N --warmup-queries N\n"
                << "  --chunk N        H3 break-even sampling interval\n"
                << "  --reps N --rep-offset N --centres N\n"
                << "  --seed N         base seed; repetition r uses N + r\n"
                << "  --region-factor F  region size = F * queries drawn from it (default 3)\n"
                << "  --file-order     insert in file order, not a per-repetition shuffle\n"
                << "  --seed-first     Etapa 2: round-robin seeding variant\n"
                << "  --knn-gate       Etapa 1: only accept radius-shrinking moves\n"
                << "  --pivots N       etapa2/etapa2_data: exactly N Voronoi pivots (default 0 =\n"
                << "                   the root entries of T_Q, as many as it happens to have)\n"
                << "  --bulk-query-share S  etapa2_bulk: share of each fan-out given to\n"
                << "                   query pivots (default 0.5)\n"
                << "  --no-verify      skip brute-force verification\n"
                << "  --agg-out/--struct-out/--moves-out/--breakeven-out/--nav-out/"
                   "--workload-out <csv>\n";
            return 0;
        } else {
            std::cerr << "unknown option: " << a << "\n";
            return 2;
        }
    }
    if (cfg.dataset.empty() || cfg.queryFile.empty()) {
        std::cerr << "ERROR: --dataset and --queryfile are required\n";
        return 2;
    }
    if (cfg.regime != "concentrado" && cfg.regime != "disperso" &&
        cfg.regime != "uniforme") {
        std::cerr << "ERROR: unknown regime: " << cfg.regime << "\n";
        return 2;
    }
    if (cfg.regionFactor < 1.0) {
        std::cerr << "ERROR: --region-factor must be at least 1\n";
        return 2;
    }

    std::vector<TCity *> objects = ch4::loadCities(cfg.dataset);
    std::vector<TCity *> pool = ch4::loadCities(cfg.queryFile);
    TCityDistanceEvaluator genEval, verifyEval;

    std::ofstream aggFile, structFile, movesFile, breakevenFile, workloadFile;
    ch4::openCsv(aggFile, aggOut, AGG_HEADER);
    ch4::openCsv(structFile, structOut, STRUCT_HEADER);
    ch4::openCsv(movesFile, movesOut, MOVES_HEADER);
    ch4::openCsv(breakevenFile, breakevenOut, BREAKEVEN_HEADER);
    ch4::openCsv(workloadFile, workloadOut, WORKLOAD_HEADER);

    std::vector<std::string> queryTypes;
    if (cfg.queryType == "both") {
        queryTypes.push_back("knn");
        queryTypes.push_back("range");
    } else {
        queryTypes.push_back(cfg.queryType);
    }

    // etapa2_data is the CONTROL that isolates what the queries contribute.
    // It runs the identical Voronoi pipeline with the identical |P|, but the
    // pivots come from a farthest-point sample of the DATA - essentially the
    // VD-tree of Moriyama et al. If it matches etapa2, then the gain is from
    // spatially coherent insertion order and NOT from the query distribution,
    // which is the claim section 4.4 rests on.
    //
    // hilbert is the reference for insertion order ALONE: no pivots, no
    // queries, just the cities sorted along a Hilbert curve.
    //
    // etapa2_bulk builds T' top-down, with the queries deciding the partition
    // of every level (stSlimQueryBulkLoad); etapa2_bulk_data is the same code
    // with the queries switched off - its control.
    const char *arms[] = {"original", "slimdown", "etapa1", "etapa2",
                          "etapa2_data", "hilbert", "etapa2_bulk",
                          "etapa2_bulk_data"};
    int failures = 0;

    for (int r = 0; r < cfg.reps; r++) {
        const int rep = cfg.repOffset + r;
        // One seed per repetition: regions, queries and insertion order all
        // change, so repetitions are independent samples rather than copies.
        const unsigned int repSeed = cfg.seed + (unsigned int)rep;

        const Workload w = makeWorkload(cfg, genEval, objects, pool, repSeed);
        writeWorkload(workloadFile, cfg, rep, repSeed, w);
        std::cout << "dataset " << objects.size() << " objects | regime "
                  << cfg.regime << " | rep " << rep << " seed " << repSeed
                  << " | adapt " << w.adapt.size() << " measure "
                  << w.measure.size() << " | measure->adapt "
                  << w.measureToAdapt << "\n";
        for (size_t g = 0; g < w.centres.size(); g++)
            std::cout << "  region " << g << ": " << w.centres[g]->GetName()
                      << " (" << w.regionSize << " cities, radius "
                      << w.centreRadius[g] << ")\n";

        // The insertion order every arm starts from in this repetition.
        std::vector<TCity *> order(objects);
        if (!cfg.fileOrder) {
            std::mt19937 orderRng(repSeed + 1000003u);
            std::shuffle(order.begin(), order.end(), orderRng);
        }
        const std::vector<TCity *> hilbert = hilbertOrder(order);

        // Which objects lie inside a queried region, for the leaf-depth
        // diagnostic. Uses the generator's evaluator, never a measured one.
        std::vector<char> hot(order.size(), 0);
        for (size_t i = 0; i < order.size(); i++)
            for (size_t g = 0; g < w.centres.size() && !hot[i]; g++)
                hot[i] = genEval.GetDistance(*order[i], *w.centres[g]) <=
                         w.centreRadius[g];
        auto meanDepth = [&](const std::vector<int> &depth, char which) {
            double sum = 0.0;
            long n = 0;
            for (size_t i = 0; i < depth.size(); i++)
                if (hot[i] == which) {
                    sum += depth[i];
                    n++;
                }
            return n ? sum / (double)n : -1.0;
        };

        for (const std::string &qtype : queryTypes) {
            const bool knn = (qtype == "knn");
            // Baseline totals for the H3 comparison, filled by the first arm.
            double baseDist = 0.0, baseReads = 0.0, baseMs = 0.0;
            std::size_t pivotsUsed = 0;  // set by etapa2, reused by the control

            for (const std::string &arm : arms) {
                const std::string idxPath = cfg.indexDir + "/ch4_" + cfg.regime +
                                            "_" + qtype + "_" + arm + ".dat";
                std::error_code ec;
                std::filesystem::remove(idxPath, ec);

                stPlainDiskPageManager *pm =
                    new stPlainDiskPageManager(idxPath.c_str(), cfg.pageSize);
                mySlimTree *tree = new mySlimTree(pm);

                const std::vector<TCity *> &insertion =
                    (arm == "hilbert") ? hilbert : order;
                ch4::Timer buildTimer;
                for (TCity *o : insertion) tree->Add(o);
                const double buildMs = buildTimer.ms();

                RowExtra extra;
                extra.seed = repSeed;
                extra.constructionDistances =
                    (long)tree->GetMetricEvaluator()->GetDistanceCount();
                extra.regionSize = w.regionSize;
                extra.measureToAdapt = w.measureToAdapt;
                extra.insertionOrder = (arm == "hilbert")
                                           ? "hilbert"
                                           : (cfg.fileOrder ? "file" : "shuffled");
                if (!w.centres.empty()) {
                    // Insertion-built trees are balanced: every leaf at Height.
                    extra.leafDepthHot = extra.leafDepthCold = tree->GetHeight();
                }

                double adaptMs = 0.0;
                myEtapa1::Stats adaptStats;
                const myEtapa1::Stats *adaptPtr = NULL;
                mySlimTree *queried = tree;      // the tree the queries run on
                stPlainDiskPageManager *qpm = pm;
                std::string queriedPath = idxPath;
                mySlimTree *extraTree = NULL;    // Etapa 2 builds a second one
                stPlainDiskPageManager *extraPm = NULL;
                myRebuild rebuild;

                if (arm == "hilbert") {
                    // Reaching this organisation from an existing tree means
                    // rebuilding it, so the whole build is the cost to amortise.
                    extra.reorgDistances = extra.constructionDistances;
                    adaptMs = buildMs;

                } else if (arm == "slimdown") {
                    if (tree->GetHeight() < 3) {
                        std::cerr << "ERROR: Optimize() is a silent no-op below "
                                     "height 3 (height is "
                                  << tree->GetHeight()
                                  << "). Pick a smaller --page-size so the "
                                     "slimdown arm is not a copy of original.\n";
                        return 3;
                    }
                    const long d0 = (long)tree->GetMetricEvaluator()->GetDistanceCount();
                    ch4::Timer t;
                    tree->Optimize();
                    adaptMs = t.ms();
                    extra.reorgDistances =
                        (long)tree->GetMetricEvaluator()->GetDistanceCount() - d0;

                } else if (arm == "etapa1") {
                    myEtapa1::Config ec1;
                    ec1.RequireShrink = cfg.knnGate;
                    myEtapa1 etapa1(pm, ec1);
                    stSlimTrace trace;
                    long cumMoves = 0;
                    double cumMs = 0.0;

                    for (size_t i = 0; i < w.adapt.size(); i++) {
                        trace.Reset();
                        stSlimActiveTrace = &trace;
                        myResult *res = knn ? tree->NearestQuery(w.adapt[i], cfg.k)
                                            : tree->RangeQuery(w.adapt[i], cfg.radius);
                        stSlimActiveTrace = 0;
                        delete res;

                        ch4::Timer t;
                        const long moved = etapa1.ApplyAfterQuery(trace);
                        cumMs += t.ms();
                        cumMoves += moved;

                        if (movesFile.is_open()) {
                            movesFile << cfg.commit << ',' << cfg.datasetLabel << ','
                                      << cfg.regime << ',' << qtype << ',' << rep
                                      << ',' << i << ',' << moved << ','
                                      << cumMoves << "\n";
                        }

                        // H3: after every `chunk` adapt queries, price the whole
                        // measure set read-only and record the cumulative cost.
                        if (cfg.chunk > 0 && breakevenFile.is_open() &&
                            ((i + 1) % (size_t)cfg.chunk == 0)) {
                            const Measurement probe =
                                measureBatch(tree, pm, w.measure, knn, cfg, objects,
                                             verifyEval, false, 0);
                            const myEtapa1::Stats &st = etapa1.GetStats();
                            breakevenFile
                                << cfg.commit << ',' << cfg.datasetLabel << ','
                                << cfg.regime << ',' << qtype << ',' << rep << ','
                                << (i + 1) << ',' << st.Distances << ','
                                << st.PageReads << ',' << st.PageWrites << ','
                                << cumMs << ',' << probe.distances << ','
                                << probe.reads << ',' << probe.totalMs << ','
                                << baseDist << ',' << baseReads << ',' << baseMs
                                << "\n";
                        }
                    }
                    adaptMs = cumMs;
                    adaptStats = etapa1.GetStats();
                    adaptPtr = &adaptStats;
                    extra.reorgDistances = adaptStats.Distances;

                    if (adaptStats.DestinationRadiusGrowthEvents != 0) {
                        std::cerr << "FATAL: H2 violated - the destination radius "
                                     "grew "
                                  << adaptStats.DestinationRadiusGrowthEvents
                                  << " time(s)\n";
                        failures++;
                    }

                } else if (arm == "etapa2" || arm == "etapa2_data") {
                    ch4::Timer t;
                    long pivotCost = 0;
                    if (arm == "etapa2") {
                        rebuild.BuildQueryTree(w.adapt, cfg.pageSize,
                                               cfg.indexDir + "/ch4_TQ.dat");
                        pivotsUsed = (cfg.pivots > 0)
                                         ? rebuild.ExtractNPivots((size_t)cfg.pivots)
                                         : rebuild.ExtractPivots(myRebuild::pmNODE_REPS);
                        pivotCost = rebuild.GetQueryTreeDistances() +
                                    rebuild.GetPivotDistances();
                    } else {
                        // Same |P|, but chosen from the DATA and never from a
                        // query. Everything downstream is identical.
                        TCityDistanceEvaluator fpsEval;
                        std::vector<TCity *> dataPivots;
                        const std::vector<size_t> picks = farthestPointSample(
                            fpsEval, objects, (int)pivotsUsed, repSeed + 7u);
                        for (size_t pi = 0; pi < picks.size(); pi++)
                            dataPivots.push_back(objects[picks[pi]]);
                        rebuild.SetPivots(dataPivots);
                        pivotCost = (long)fpsEval.GetDistanceCount();
                    }
                    extra.pivots = (long)rebuild.GetPivotCount();
                    rebuild.AssignCells(objects);

                    queriedPath = cfg.indexDir + "/ch4_" + cfg.regime + "_" + qtype +
                                  "_" + arm + "_new.dat";
                    std::filesystem::remove(queriedPath, ec);
                    extraPm = new stPlainDiskPageManager(queriedPath.c_str(),
                                                         cfg.pageSize);
                    extraTree = new mySlimTree(extraPm);
                    rebuild.BuildTree(extraTree, objects, cfg.seedFirst);
                    adaptMs = t.ms();
                    extra.reorgDistances =
                        pivotCost + rebuild.GetAssignDistances() +
                        (long)extraTree->GetMetricEvaluator()->GetDistanceCount();
                    extra.insertionOrder = "voronoi_cells";

                    // Section 4.4.1: the ORIGINAL tree stays alive alongside T'.
                    queried = extraTree;
                    qpm = extraPm;

                    if (!navOut.empty() && r == 0 && knn && arm == "etapa2") {
                        std::ofstream nav(navOut);
                        if (nav) rebuild.ExportNavigationGraph(nav, true);
                    }

                } else if (arm == "etapa2_bulk" || arm == "etapa2_bulk_data") {
                    // Top-down: no T_Q and no reinsertion. The adapt queries
                    // are handed over directly and split level by level.
                    myBulk::Config bc;
                    bc.QueryShare = (arm == "etapa2_bulk") ? cfg.bulkQueryShare : 0.0;
                    myBulk bulk(bc);

                    queriedPath = cfg.indexDir + "/ch4_" + cfg.regime + "_" + qtype +
                                  "_" + arm + "_new.dat";
                    std::filesystem::remove(queriedPath, ec);
                    extraPm = new stPlainDiskPageManager(queriedPath.c_str(),
                                                         cfg.pageSize);
                    extraTree = new mySlimTree(extraPm);
                    ch4::Timer t;
                    bulk.Build(extraTree, extraPm, order, w.adapt);
                    adaptMs = t.ms();

                    const myBulk::Stats &bs = bulk.GetStats();
                    extra.reorgDistances = bs.Distances;
                    extra.pivots = bs.QueryPivots + bs.DataPivots;
                    extra.insertionOrder = "top_down";
                    if (!w.centres.empty()) {
                        extra.leafDepthHot = meanDepth(bulk.GetLeafDepth(), 1);
                        extra.leafDepthCold = meanDepth(bulk.GetLeafDepth(), 0);
                    }
                    if (extraTree->GetNumberOfObjects() != (long)objects.size()) {
                        std::cerr << "FATAL: " << arm << " stored "
                                  << extraTree->GetNumberOfObjects() << " of "
                                  << objects.size() << " objects\n";
                        failures++;
                    }

                    queried = extraTree;
                    qpm = extraPm;
                }

                const Measurement m =
                    measureBatch(queried, qpm, w.measure, knn, cfg, objects,
                                 verifyEval, cfg.verify, cfg.warmupQueries);

                if (m.correct == 0) {
                    std::cerr << "FATAL: arm " << arm << " (" << qtype
                              << ") returned wrong answers\n";
                    failures++;
                }

                const double bytes =
                    (double)std::filesystem::file_size(queriedPath, ec);

                writeAgg(aggFile, cfg, arm, qtype, rep, m, buildMs, adaptMs,
                         adaptPtr, queried, ec ? 0.0 : bytes,
                         (int)w.centres.size(), extra);

                if (arm == "original") {
                    baseDist = m.distances;
                    baseReads = m.reads;
                    baseMs = m.totalMs;
                }

                std::cout << "  " << cfg.regime << " " << qtype << " " << arm
                          << ": " << m.meanMs << " ms, " << m.distances / (double)m.queries
                          << " dist, " << m.reads / (double)m.queries << " reads"
                          << (adaptPtr ? ", " + std::to_string(adaptPtr->ObjectsRelocated) +
                                             " moved"
                                       : "")
                          << (extra.pivots ? ", |P| " + std::to_string(extra.pivots) : "")
                          << (m.correct == 1 ? "  [exact]" : "") << "\n";

                // Structure and fat factor LAST: GetFatFactor() costs millions of
                // distances and reads, and the counters cannot be given back.
                if (structFile.is_open()) {
                    const myMetrics::Result s =
                        myMetrics::Collect(qpm, queried->GetRootPageID());
                    writeStruct(structFile, cfg, qtype, arm, rep, queried, s,
                                queried->GetFatFactor(),
                                queried->GetRelativeFatFactor());
                }

                if (extraTree != NULL) {
                    delete extraTree;
                    delete extraPm;
                }
                delete tree;
                delete pm;
            }
        }
    }

    ch4::deleteAll(objects);
    ch4::deleteAll(pool);

    std::cout << (failures == 0 ? "\nDONE" : "\nDONE WITH FAILURES") << " ("
              << failures << ")\n";
    return failures == 0 ? 0 : 1;
}
