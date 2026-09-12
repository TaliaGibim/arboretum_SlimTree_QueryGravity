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
// Regimes (section 4.5): concentrado, disperso, uniforme. The last is the
// adverse case for Etapa 1 and is meant to be.
//
// PROTOCOL
// --------
// The query load is split DISJOINTLY into an adapt half and a measure half,
// drawn from the same distribution, because section 4.5 requires one sequence
// to adapt the tree and a DIFFERENT one to measure it. Etapa 2 uses the same
// split: the adapt half builds T_Q, the measure half evaluates T'.
//
// Every arm builds its own tree from the SAME deterministic insertion order,
// rather than copying index files, so no arm can inherit another page layout.
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

#include "ch4_common.h"

#include <algorithm>
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
    unsigned int seed = 42;
    int hotspots = 0;        // 0 = regime default
    bool verify = true;
    bool seedFirst = false;  // Etapa 2 variant
    bool knnGate = false;    // Etapa 1, Equation 4.8 gate
    std::string commit = "";
};

// ---------------------------------------------------------------------------
// Workload generation (section 4.5)
//
// The generator that ships with the Chapter 3 harness is unsuitable here: it
// repeats EXACT points, because Query Gravity merged hotspots below 1e-6.
// Chapter 4 needs REGIONS, so a regime is a set of centres plus the cities
// nearest to them.
// ---------------------------------------------------------------------------
struct Workload {
    std::vector<TCity *> adapt;
    std::vector<TCity *> measure;
    int centres = 0;
};

// Farthest-point sampling: picks centres that are as mutually distant as the
// data allows, which is what makes `disperso` genuinely several separated
// regions rather than one blurred blob.
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

// The `pool` is used only for the uniform regime; the clustered regimes draw
// their queries from the indexed set so that a region really is populated.
static Workload makeWorkload(const Config &cfg, TCityDistanceEvaluator &eval,
                             const std::vector<TCity *> &objects,
                             const std::vector<TCity *> &pool) {
    Workload w;
    // The load is split in half, so each half must be able to supply the
    // requested count on its own: generate twice the LARGER of the two.
    const int wanted = 2 * std::max(cfg.adaptQueries, cfg.measureQueries);
    std::vector<TCity *> all;

    if (cfg.regime == "uniforme") {
        // Adverse case: no privileged region for the tree to converge towards.
        w.centres = 0;
        std::mt19937 rng(cfg.seed);
        for (int i = 0; i < wanted; i++)
            all.push_back(pool[rng() % pool.size()]);
    } else {
        const int centres = cfg.hotspots > 0
                                ? cfg.hotspots
                                : (cfg.regime == "concentrado" ? 2 : 8);
        w.centres = centres;
        const std::vector<size_t> seeds =
            farthestPointSample(eval, objects, centres, cfg.seed);

        // Each centre contributes its `perCentre` nearest cities, so the queries
        // land inside a dense region rather than on an arbitrary point.
        const int perCentre = (wanted + centres - 1) / centres;
        for (size_t s = 0; s < seeds.size(); s++) {
            std::vector<std::pair<double, size_t> > byDist;
            byDist.reserve(objects.size());
            for (size_t i = 0; i < objects.size(); i++)
                byDist.push_back(std::make_pair(
                    eval.GetDistance(*objects[i], *objects[seeds[s]]), i));
            const size_t take = std::min<size_t>(perCentre, byDist.size());
            std::partial_sort(byDist.begin(), byDist.begin() + take, byDist.end());
            for (size_t i = 0; i < take; i++) all.push_back(objects[byDist[i].second]);
        }
        if ((int)all.size() > wanted) all.resize(wanted);
    }

    // Disjoint split, interleaved so both halves see the same distribution.
    for (size_t i = 0; i < all.size(); i++)
        (i % 2 ? w.adapt : w.measure).push_back(all[i]);
    if ((int)w.adapt.size() > cfg.adaptQueries) w.adapt.resize(cfg.adaptQueries);
    if ((int)w.measure.size() > cfg.measureQueries) w.measure.resize(cfg.measureQueries);
    return w;
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
    "correct_results,page_size,seed";

static const char *STRUCT_HEADER =
    "commit,dataset,regime,arm,repetition,tree_height,index_nodes,leaf_nodes,"
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

// ---------------------------------------------------------------------------
static void writeAgg(std::ofstream &os, const Config &cfg, const std::string &arm,
                     const std::string &qtype, int rep, const Measurement &m,
                     double buildMs, double adaptMs, const myEtapa1::Stats *ad,
                     mySlimTree *tree, double indexBytes, int centres) {
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
       << cfg.seed << "\n";
}

static void writeStruct(std::ofstream &os, const Config &cfg,
                        const std::string &arm, int rep, mySlimTree *tree,
                        const myMetrics::Result &s, double fat, double rfat) {
    if (!os.is_open()) return;
    os << cfg.commit << ',' << cfg.datasetLabel << ',' << cfg.regime << ',' << arm
       << ',' << rep << ',' << tree->GetHeight() << ',' << s.IndexNodes << ','
       << s.LeafNodes << ',' << s.TotalNodes << ',' << s.Objects << ','
       << s.MeanLeafEntries << ',' << s.MinLeafEntries << ',' << s.MaxLeafEntries
       << ',' << s.MeanLeafFill << ',' << s.MeanLeafRadius << ','
       << s.MinLeafRadius << ',' << s.MaxLeafRadius << ',' << s.SiblingPairs
       << ',' << s.OverlappingPairs << ',' << s.OverlapRatio << ','
       << s.MeanAbsOverlap << ',' << fat << ',' << rfat << "\n";
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    std::cout << std::unitbuf;
    Config cfg;
    std::string aggOut, structOut, movesOut, breakevenOut, navOut;

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
        else if (a == "--commit") cfg.commit = next("--commit");
        else if (a == "--no-verify") cfg.verify = false;
        else if (a == "--seed-first") cfg.seedFirst = true;
        else if (a == "--knn-gate") cfg.knnGate = true;
        else if (a == "--agg-out") aggOut = next("--agg-out");
        else if (a == "--struct-out") structOut = next("--struct-out");
        else if (a == "--moves-out") movesOut = next("--moves-out");
        else if (a == "--breakeven-out") breakevenOut = next("--breakeven-out");
        else if (a == "--nav-out") navOut = next("--nav-out");
        else if (a == "--help" || a == "-h") {
            std::cout
                << "usage: " << argv[0] << " --dataset <f> --queryfile <f>\n"
                << "  --regime concentrado|disperso|uniforme   (default concentrado)\n"
                << "  --query-type knn|range|both              (default both)\n"
                << "  --k N --radius R --page-size N\n"
                << "  --adapt-queries N --measure-queries N --warmup-queries N\n"
                << "  --chunk N        H3 break-even sampling interval\n"
                << "  --reps N --rep-offset N --seed N --centres N\n"
                << "  --seed-first     Etapa 2: round-robin seeding variant\n"
                << "  --knn-gate       Etapa 1: only accept radius-shrinking moves\n"
                << "  --no-verify      skip brute-force verification\n"
                << "  --agg-out/--struct-out/--moves-out/--breakeven-out/--nav-out <csv>\n";
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

    std::vector<TCity *> objects = ch4::loadCities(cfg.dataset);
    std::vector<TCity *> pool = ch4::loadCities(cfg.queryFile);
    TCityDistanceEvaluator genEval, verifyEval;

    const Workload w = makeWorkload(cfg, genEval, objects, pool);
    std::cout << "dataset " << objects.size() << " objects | regime " << cfg.regime
              << " (" << w.centres << " centres) | adapt " << w.adapt.size()
              << " measure " << w.measure.size() << "\n";

    std::ofstream aggFile, structFile, movesFile, breakevenFile;
    ch4::openCsv(aggFile, aggOut, AGG_HEADER);
    ch4::openCsv(structFile, structOut, STRUCT_HEADER);
    ch4::openCsv(movesFile, movesOut, MOVES_HEADER);
    ch4::openCsv(breakevenFile, breakevenOut, BREAKEVEN_HEADER);

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
    const char *arms[] = {"original", "slimdown", "etapa1", "etapa2",
                          "etapa2_data"};
    int failures = 0;

    for (int r = 0; r < cfg.reps; r++) {
        const int rep = cfg.repOffset + r;
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

                ch4::Timer buildTimer;
                for (TCity *o : objects) tree->Add(o);
                const double buildMs = buildTimer.ms();

                double adaptMs = 0.0;
                myEtapa1::Stats adaptStats;
                const myEtapa1::Stats *adaptPtr = NULL;
                mySlimTree *queried = tree;      // the tree the queries run on
                stPlainDiskPageManager *qpm = pm;
                mySlimTree *extraTree = NULL;    // Etapa 2 builds a second one
                stPlainDiskPageManager *extraPm = NULL;
                myRebuild rebuild;

                if (arm == "slimdown") {
                    if (tree->GetHeight() < 3) {
                        std::cerr << "ERROR: Optimize() is a silent no-op below "
                                     "height 3 (height is "
                                  << tree->GetHeight()
                                  << "). Pick a smaller --page-size so the "
                                     "slimdown arm is not a copy of original.\n";
                        return 3;
                    }
                    ch4::Timer t;
                    tree->Optimize();
                    adaptMs = t.ms();

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

                    if (adaptStats.DestinationRadiusGrowthEvents != 0) {
                        std::cerr << "FATAL: H2 violated - the destination radius "
                                     "grew "
                                  << adaptStats.DestinationRadiusGrowthEvents
                                  << " time(s)\n";
                        failures++;
                    }

                } else if (arm == "etapa2" || arm == "etapa2_data") {
                    ch4::Timer t;
                    if (arm == "etapa2") {
                        rebuild.BuildQueryTree(w.adapt, cfg.pageSize,
                                               cfg.indexDir + "/ch4_TQ.dat");
                        pivotsUsed = rebuild.ExtractPivots(myRebuild::pmNODE_REPS);
                    } else {
                        // Same |P|, but chosen from the DATA and never from a
                        // query. Everything downstream is identical.
                        std::vector<TCity *> dataPivots;
                        const std::vector<size_t> picks = farthestPointSample(
                            genEval, objects, (int)pivotsUsed, cfg.seed + 7);
                        for (size_t pi = 0; pi < picks.size(); pi++)
                            dataPivots.push_back(objects[picks[pi]]);
                        rebuild.SetPivots(dataPivots);
                    }
                    rebuild.AssignCells(objects);

                    const std::string newPath = cfg.indexDir + "/ch4_" + cfg.regime +
                                                "_" + qtype + "_etapa2_new.dat";
                    std::filesystem::remove(newPath, ec);
                    extraPm = new stPlainDiskPageManager(newPath.c_str(), cfg.pageSize);
                    extraTree = new mySlimTree(extraPm);
                    rebuild.BuildTree(extraTree, objects, cfg.seedFirst);
                    adaptMs = t.ms();

                    // Section 4.4.1: the ORIGINAL tree stays alive alongside T'.
                    queried = extraTree;
                    qpm = extraPm;

                    if (!navOut.empty() && rep == cfg.repOffset && knn &&
                        arm == "etapa2") {
                        std::ofstream nav(navOut);
                        if (nav) rebuild.ExportNavigationGraph(nav, true);
                    }
                }

                const Measurement m =
                    measureBatch(queried, qpm, w.measure, knn, cfg, objects,
                                 verifyEval, cfg.verify, cfg.warmupQueries);

                if (m.correct == 0) {
                    std::cerr << "FATAL: arm " << arm << " (" << qtype
                              << ") returned wrong answers\n";
                    failures++;
                }

                const double bytes = (double)std::filesystem::file_size(
                    (extraTree != NULL) ? (cfg.indexDir + "/ch4_" + cfg.regime + "_" +
                                           qtype + "_" + arm + "_new.dat")
                                        : idxPath,
                    ec);

                writeAgg(aggFile, cfg, arm, qtype, rep, m, buildMs, adaptMs,
                         adaptPtr, queried, bytes, w.centres);

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
                          << (m.correct == 1 ? "  [exact]" : "") << "\n";

                // Structure and fat factor LAST: GetFatFactor() costs millions of
                // distances and reads, and the counters cannot be given back.
                if (structFile.is_open()) {
                    const myMetrics::Result s =
                        myMetrics::Collect(qpm, queried->GetRootPageID());
                    writeStruct(structFile, cfg, arm, rep, queried, s,
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
