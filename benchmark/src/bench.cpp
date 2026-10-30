/*
 * bench.cpp - Slim-Tree benchmark harness (baseline vs. Query Gravity).
 *
 * This file lives OUTSIDE the arboretum source tree and is compiled against the
 * headers of whichever git worktree is being measured. Neither version of the
 * library is modified in any way; the only thing that changes between runs is
 * the -I include path (and therefore which stSlimTree-inl.h gets instantiated).
 *
 * What it measures, per (version, protocol, dataset, query_type, k, repetition):
 *   - index construction time, kept strictly separate from query time;
 *   - per-query wall time (mean / median / p95 / sample standard deviation);
 *   - distance computations, via the library's own DistanceFunction counter;
 *   - disk page reads/writes, via the library's own stPageManager counters;
 *   - index file size, tree height, node count, number of indexed objects;
 *   - correctness against an exact brute-force k-NN scan.
 *
 * Two protocols exist because Query Gravity only ever changes tree structure
 * inside SlimDownCanSwap(), which is reachable exclusively through
 * Optimize() -> SlimDownRecursive() -> SlimDown() -> LocalSlimDown():
 *
 *   no-optimize    build -> query.  Optimize() is never called, so the tree
 *                  layout is identical in both versions and this isolates the
 *                  pure *overhead* of the AddQueryHotspot() bookkeeping.
 *
 *   train-optimize build -> training queries (populate the hotspot list) ->
 *                  Optimize() -> reset counters -> measured queries. This is
 *                  the only setting in which Query Gravity can actually pay off.
 *
 * Author: benchmark harness for the Slim-Tree / Query Gravity comparison.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include <arboretum/stPlainDiskPageManager.h>
#include <arboretum/stSlimTree.h>

#include "city.h"

typedef stSlimTree<TCity, TCityDistanceEvaluator> mySlimTree;
typedef stResult<TCity> myResult;

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
struct Config {
    std::string dataset;
    std::string datasetLabel;
    std::string queryFile;
    std::string version = "unknown";
    std::string commit = "unknown";
    std::string protocol = "no-optimize"; // no-optimize | train-optimize
    std::string trainMode = "disjoint";   // disjoint | same
    std::string queryDist = "uniform";    // uniform | skewed
    int hotspots = 10;                    // number of hot regions when skewed
    std::string queryType = "knn";        // knn | range | both
    std::vector<unsigned int> kList{1, 5, 10, 20, 50};
    std::vector<double> radiusList{0.2};
    int reps = 5;
    int repOffset = 0; // global repetition number of the first local repetition
    int measureQueries = 200;
    int trainQueries = 250;
    int warmupQueries = 50;
    unsigned int seed = 42;
    unsigned int pageSize = 1024;
    unsigned int trainK = 10; // k used for the hotspot-training queries
    std::string indexPath = "SlimTree.dat";
    std::string aggOut;
    std::string rawOut;
    std::string neighborsOut;
    bool verify = true;
};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static std::vector<std::string> splitCsv(const std::string &s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static double mean(const std::vector<double> &v) {
    if (v.empty()) return 0.0;
    return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

// Sample standard deviation (n-1 denominator).
static double stddev(const std::vector<double> &v) {
    if (v.size() < 2) return 0.0;
    const double m = mean(v);
    double acc = 0.0;
    for (double x : v) acc += (x - m) * (x - m);
    return std::sqrt(acc / static_cast<double>(v.size() - 1));
}

static double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    if (v.size() == 1) return v[0];
    const double pos = p * static_cast<double>(v.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(pos));
    const size_t hi = static_cast<size_t>(std::ceil(pos));
    const double frac = pos - static_cast<double>(lo);
    return v[lo] + (v[hi] - v[lo]) * frac;
}

static double median(const std::vector<double> &v) { return percentile(v, 0.5); }

// Loads a "<name>\t<lat>\t<long>" file using exactly the same parsing logic as
// the original test application (app.cpp), so the object set is identical.
static std::vector<TCity *> loadCities(const std::string &fileName) {
    std::vector<TCity *> out;
    std::ifstream in(fileName.c_str());
    if (!in.is_open()) {
        std::cerr << "ERROR: cannot open '" << fileName << "'\n";
        return out;
    }
    char cityName[200];
    double dLat, dLong;
    while (in.getline(cityName, 200, '\t')) {
        in >> dLat;
        in >> dLong;
        in.ignore();
        out.push_back(new TCity(cityName, dLat, dLong));
    }
    in.close();
    return out;
}

// ---------------------------------------------------------------------------
// Exact brute-force k-NN, used only to validate correctness. It deliberately
// uses its OWN evaluator instance so that it never pollutes the tree's official
// distance counter.
// ---------------------------------------------------------------------------
static std::vector<double> bruteForceKnnDistances(TCityDistanceEvaluator &eval,
                                                  std::vector<TCity *> &objects,
                                                  TCity *query, unsigned int k) {
    std::vector<double> d;
    d.reserve(objects.size());
    for (TCity *o : objects) d.push_back(eval.GetDistance(*o, *query));
    const size_t kk = std::min<size_t>(k, d.size());
    std::partial_sort(d.begin(), d.begin() + kk, d.end());
    d.resize(kk);
    return d;
}

static std::vector<double> bruteForceRangeDistances(TCityDistanceEvaluator &eval,
                                                    std::vector<TCity *> &objects,
                                                    TCity *query, double radius) {
    std::vector<double> d;
    for (TCity *o : objects) {
        const double dist = eval.GetDistance(*o, *query);
        if (dist <= radius) d.push_back(dist);
    }
    std::sort(d.begin(), d.end());
    return d;
}

// ---------------------------------------------------------------------------
// One aggregated CSV row
// ---------------------------------------------------------------------------
struct Row {
    std::string queryType;
    long k = 0;
    double radius = -1.0;
    int repetition = 0;
    size_t queryCount = 0;
    double constructionTimeMs = 0.0;
    double optimizeTimeMs = 0.0;
    double totalQueryTimeMs = 0.0;
    double meanQueryTimeMs = 0.0;
    double medianQueryTimeMs = 0.0;
    double p95QueryTimeMs = 0.0;
    double stdQueryTimeMs = 0.0;
    double totalDistanceCalculations = 0.0;
    double meanDistanceCalculations = 0.0;
    double totalDiskAccesses = 0.0;
    double meanDiskAccesses = 0.0;
    double totalDiskWrites = 0.0;
    double constructionDistanceCalculations = 0.0;
    double constructionDiskAccesses = 0.0;
    double indexSizeBytes = 0.0;
    long treeHeight = 0;
    long nodeCount = 0;
    long numObjects = 0;
    int correctResults = 0;   // 1 == every query matched brute force
    long correctQueryCount = 0;
    double meanResultSize = 0.0;
};

static const char *AGG_HEADER =
    "version,commit_hash,dataset,protocol,train_mode,query_dist,hotspots,"
    "query_type,k,radius,repetition,"
    "query_count,construction_time_ms,optimize_time_ms,total_query_time_ms,"
    "mean_query_time_ms,median_query_time_ms,p95_query_time_ms,std_query_time_ms,"
    "total_distance_calculations,mean_distance_calculations,"
    "total_disk_accesses,mean_disk_accesses,total_disk_writes,"
    "construction_distance_calculations,construction_disk_accesses,"
    "index_size_bytes,tree_height,node_count,num_objects,"
    "correct_results,correct_query_count,mean_result_size,"
    "page_size,seed\n";

static void openCsv(std::ofstream &os, const std::string &path, const char *header) {
    if (path.empty()) return;
    const bool exists = std::filesystem::exists(path) &&
                        std::filesystem::file_size(path) > 0;
    os.open(path, std::ios::app);
    if (!os) {
        std::cerr << "ERROR: cannot open output file '" << path << "'\n";
        return;
    }
    if (!exists) os << header;
}

static void writeRow(std::ofstream &os, const Config &cfg, const Row &r) {
    if (!os.is_open()) return;
    os.setf(std::ios::fixed);
    os << cfg.version << ',' << cfg.commit << ',' << cfg.datasetLabel << ','
       << cfg.protocol << ',' << cfg.trainMode << ',' << cfg.queryDist << ','
       << (cfg.queryDist == "skewed" ? cfg.hotspots : 0) << ','
       << r.queryType << ',' << r.k << ',';
    os.precision(6);
    os << r.radius << ',' << r.repetition << ',' << r.queryCount << ','
       << r.constructionTimeMs << ',' << r.optimizeTimeMs << ','
       << r.totalQueryTimeMs << ',' << r.meanQueryTimeMs << ','
       << r.medianQueryTimeMs << ',' << r.p95QueryTimeMs << ','
       << r.stdQueryTimeMs << ',';
    os.precision(2);
    os << r.totalDistanceCalculations << ',' << r.meanDistanceCalculations << ','
       << r.totalDiskAccesses << ',' << r.meanDiskAccesses << ','
       << r.totalDiskWrites << ',' << r.constructionDistanceCalculations << ','
       << r.constructionDiskAccesses << ',' << r.indexSizeBytes << ','
       << r.treeHeight << ',' << r.nodeCount << ',' << r.numObjects << ','
       << r.correctResults << ',' << r.correctQueryCount << ','
       << r.meanResultSize << ',' << cfg.pageSize << ',' << cfg.seed << '\n';
}

// ---------------------------------------------------------------------------
// Argument parsing
// ---------------------------------------------------------------------------
static bool parseArgs(int argc, char **argv, Config &cfg) {
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&](const char *what) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "ERROR: missing value for " << what << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--dataset") cfg.dataset = next("--dataset");
        else if (a == "--dataset-label") cfg.datasetLabel = next("--dataset-label");
        else if (a == "--queryfile") cfg.queryFile = next("--queryfile");
        else if (a == "--version-label") cfg.version = next("--version-label");
        else if (a == "--commit") cfg.commit = next("--commit");
        else if (a == "--protocol") cfg.protocol = next("--protocol");
        else if (a == "--train-mode") cfg.trainMode = next("--train-mode");
        else if (a == "--query-dist") cfg.queryDist = next("--query-dist");
        else if (a == "--hotspots") cfg.hotspots = std::stoi(next("--hotspots"));
        else if (a == "--query-type") cfg.queryType = next("--query-type");
        else if (a == "--k-list") {
            cfg.kList.clear();
            for (const auto &s : splitCsv(next("--k-list")))
                cfg.kList.push_back(static_cast<unsigned int>(std::stoul(s)));
        } else if (a == "--radius-list") {
            cfg.radiusList.clear();
            for (const auto &s : splitCsv(next("--radius-list")))
                cfg.radiusList.push_back(std::stod(s));
        }
        else if (a == "--reps") cfg.reps = std::stoi(next("--reps"));
        else if (a == "--rep-offset") cfg.repOffset = std::stoi(next("--rep-offset"));
        else if (a == "--measure-queries") cfg.measureQueries = std::stoi(next("--measure-queries"));
        else if (a == "--train-queries") cfg.trainQueries = std::stoi(next("--train-queries"));
        else if (a == "--warmup-queries") cfg.warmupQueries = std::stoi(next("--warmup-queries"));
        else if (a == "--seed") cfg.seed = static_cast<unsigned int>(std::stoul(next("--seed")));
        else if (a == "--page-size") cfg.pageSize = static_cast<unsigned int>(std::stoul(next("--page-size")));
        else if (a == "--train-k") cfg.trainK = static_cast<unsigned int>(std::stoul(next("--train-k")));
        else if (a == "--index") cfg.indexPath = next("--index");
        else if (a == "--agg-out") cfg.aggOut = next("--agg-out");
        else if (a == "--raw-out") cfg.rawOut = next("--raw-out");
        else if (a == "--neighbors-out") cfg.neighborsOut = next("--neighbors-out");
        else if (a == "--no-verify") cfg.verify = false;
        else if (a == "--help" || a == "-h") {
            std::cout <<
                "Usage: bench --dataset F --queryfile F [options]\n"
                "  --version-label S   --commit S      --dataset-label S\n"
                "  --protocol no-optimize|train-optimize\n"
                "  --train-mode disjoint|same          --query-type knn|range|both\n"
                "  --query-dist uniform|skewed         --hotspots N\n"
                "  --k-list 1,5,10,20,50               --radius-list 0.2\n"
                "  --reps N            --rep-offset N        --train-queries N\n"
                "  --measure-queries N\n"
                "  --warmup-queries N  --seed N        --page-size N   --train-k N\n"
                "  --index PATH        --agg-out CSV   --raw-out CSV\n"
                "  --neighbors-out CSV --no-verify\n";
            std::exit(0);
        } else {
            std::cerr << "ERROR: unknown argument '" << a << "'\n";
            return false;
        }
    }
    if (cfg.dataset.empty() || cfg.queryFile.empty()) {
        std::cerr << "ERROR: --dataset and --queryfile are required\n";
        return false;
    }
    if (cfg.datasetLabel.empty())
        cfg.datasetLabel = std::filesystem::path(cfg.dataset).filename().string();
    return true;
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    Config cfg;
    if (!parseArgs(argc, argv, cfg)) return 2;

    std::vector<TCity *> objects = loadCities(cfg.dataset);
    std::vector<TCity *> allQueries = loadCities(cfg.queryFile);
    if (objects.empty() || allQueries.empty()) {
        std::cerr << "ERROR: empty dataset or query file\n";
        return 3;
    }
    std::cout << "Loaded " << objects.size() << " objects, "
              << allQueries.size() << " candidate query objects\n";

    // --- Reproducible query selection -------------------------------------
    // A single fixed-seed permutation drives everything, so the measured set
    // and the training set are byte-for-byte identical across versions, runs
    // and machines.
    std::vector<size_t> perm(allQueries.size());
    std::iota(perm.begin(), perm.end(), 0);
    std::mt19937 rng(cfg.seed);
    std::shuffle(perm.begin(), perm.end(), rng);

    std::vector<TCity *> measureSet, trainSet;
    // A dedicated evaluator: choosing the query sets must never show up in the
    // tree's official distance counter.
    TCityDistanceEvaluator selEval;

    if (cfg.queryDist == "skewed") {
        // ------------------------------------------------------------------
        // Skewed / repetitive workload -- the scenario Query Gravity targets.
        //
        // The key constraint comes from AddQueryHotspot() itself: it merges a
        // query into an existing hotspot only when the distance is < 1e-6, so a
        // hotspot's `frequency` grows ONLY under near-exact repetition. Under a
        // uniform workload every query therefore creates a fresh frequency-1
        // entry and the 50-slot list just churns, leaving CalculateHeat() with
        // almost nothing to work with.
        //
        // So: the training phase hammers H exact points (frequency climbs to
        // ~trainQueries/H each), and the measured queries are the cities
        // *nearest* those points -- same skewed distribution, disjoint objects.
        // ------------------------------------------------------------------
        const size_t H = std::max<size_t>(
            1, std::min<size_t>(cfg.hotspots, allQueries.size()));
        std::vector<size_t> centres(perm.begin(), perm.begin() + H);

        std::vector<char> used(allQueries.size(), 0);
        for (size_t c : centres) used[c] = 1; // centres are training points only

        const size_t nTrain = std::max<size_t>(H, static_cast<size_t>(cfg.trainQueries));
        for (size_t i = 0; i < nTrain; i++)
            trainSet.push_back(allQueries[centres[i % H]]);

        const size_t want = static_cast<size_t>(cfg.measureQueries);
        const size_t per = std::max<size_t>(1, want / H);
        for (size_t ci = 0; ci < H && measureSet.size() < want; ci++) {
            std::vector<std::pair<double, size_t>> cand;
            for (size_t j = 0; j < allQueries.size(); j++) {
                if (used[j]) continue;
                cand.emplace_back(
                    selEval.GetDistance(*allQueries[centres[ci]], *allQueries[j]), j);
            }
            // Sorting on (distance, index) keeps ties deterministic.
            std::sort(cand.begin(), cand.end());
            for (size_t t = 0; t < per && t < cand.size(); t++) {
                if (measureSet.size() >= want) break;
                used[cand[t].second] = 1;
                measureSet.push_back(allQueries[cand[t].second]);
            }
        }
        std::cout << "Skewed workload: " << H << " hot centres, "
                  << (nTrain / H) << " exact repetitions each\n";
    } else {
        // Uniform workload: one fixed-seed permutation, measured set first,
        // disjoint training set immediately after it.
        const size_t nMeasure = std::min<size_t>(cfg.measureQueries, perm.size());
        for (size_t i = 0; i < nMeasure; i++) measureSet.push_back(allQueries[perm[i]]);

        const size_t avail = perm.size() - nMeasure;
        const size_t nTrain = std::min<size_t>(cfg.trainQueries, avail);
        if (nTrain < static_cast<size_t>(cfg.trainQueries)) {
            std::cerr << "WARNING: only " << nTrain << " disjoint training "
                      << "queries available (asked for " << cfg.trainQueries << ")\n";
        }
        for (size_t i = 0; i < nTrain; i++)
            trainSet.push_back(allQueries[perm[nMeasure + i]]);
    }

    if (cfg.trainMode == "same") trainSet = measureSet;

    std::cout << "Measured queries: " << measureSet.size()
              << ", training queries: " << trainSet.size()
              << ", protocol: " << cfg.protocol
              << ", query-dist: " << cfg.queryDist
              << ", train-mode: " << cfg.trainMode << "\n";

    std::ofstream aggOs, rawOs, nbrOs;
    openCsv(aggOs, cfg.aggOut, AGG_HEADER);
    openCsv(rawOs, cfg.rawOut,
            "version,commit_hash,dataset,protocol,train_mode,query_dist,query_type,"
            "k,radius,repetition,query_index,query_time_ms,result_size\n");
    openCsv(nbrOs, cfg.neighborsOut,
            "version,dataset,protocol,query_dist,query_type,k,radius,query_index,"
            "rank,neighbor_name,distance\n");

    TCityDistanceEvaluator bfEval; // brute-force only; never touches tree stats

    // =====================================================================
    // Repetitions
    // =====================================================================
    for (int localRep = 0; localRep < cfg.reps; localRep++) {
        // Repetitions are numbered globally so that the driver script can
        // interleave the two versions (one process per repetition) and still
        // produce a coherent, correctly numbered CSV.
        const int rep = cfg.repOffset + localRep;
        std::cout << "\n=== repetition " << (rep + 1) << " ===\n";

        std::error_code ec;
        std::filesystem::remove(cfg.indexPath, ec);

        stPlainDiskPageManager *pm =
            new stPlainDiskPageManager(cfg.indexPath.c_str(), cfg.pageSize);
        mySlimTree *tree = new mySlimTree(pm);

        // ---------------- index construction (timed on its own) ----------
        pm->ResetStatistics();
        tree->GetMetricEvaluator()->ResetStatistics();

        const auto cStart = Clock::now();
        for (TCity *o : objects) tree->Add(o);
        const auto cEnd = Clock::now();

        const double constructionTimeMs =
            std::chrono::duration<double, std::milli>(cEnd - cStart).count();
        const double constructionDist =
            static_cast<double>(tree->GetMetricEvaluator()->GetDistanceCount());
        const double constructionReads = static_cast<double>(pm->GetReadCount());

        std::cout << "  construction: " << constructionTimeMs << " ms, "
                  << tree->GetNumberOfObjects() << " objects, height "
                  << tree->GetHeight() << ", nodes " << tree->GetNodeCount() << "\n";

        // ---------------- optional training + Optimize() -----------------
        double optimizeTimeMs = 0.0;
        if (cfg.protocol == "train-optimize") {
            if (tree->GetHeight() < 3) {
                std::cerr << "WARNING: tree height is " << tree->GetHeight()
                          << " (< 3): Optimize() performs no Slim-Down, so "
                          << "Query Gravity cannot change the tree.\n";
            }
            // These queries exist purely to populate the Query Gravity hotspot
            // list. In the baseline they are simply harmless extra queries, so
            // both versions execute exactly the same call sequence.
            for (TCity *q : trainSet) {
                myResult *r = tree->NearestQuery(q, cfg.trainK);
                delete r;
            }
            const auto oStart = Clock::now();
            tree->Optimize();
            const auto oEnd = Clock::now();
            optimizeTimeMs =
                std::chrono::duration<double, std::milli>(oEnd - oStart).count();
            std::cout << "  optimize: " << optimizeTimeMs << " ms, height "
                      << tree->GetHeight() << ", nodes " << tree->GetNodeCount()
                      << "\n";
        }

        const long treeHeight = static_cast<long>(tree->GetHeight());
        const long nodeCount = static_cast<long>(tree->GetNodeCount());
        const long numObjects = static_cast<long>(tree->GetNumberOfObjects());

        std::vector<Row> rows;

        // ---------------- measured query batches -------------------------
        const bool doKnn = (cfg.queryType == "knn" || cfg.queryType == "both");
        const bool doRange = (cfg.queryType == "range" || cfg.queryType == "both");

        // A batch is one (query_type, k or radius) sweep over the measured set.
        auto runBatch = [&](const char *qtype, unsigned int k, double radius) {
            // ---- warm-up: untimed, counters reset afterwards -------------
            const size_t nWarm =
                std::min<size_t>(cfg.warmupQueries, measureSet.size());
            for (size_t i = 0; i < nWarm; i++) {
                myResult *r = (std::strcmp(qtype, "knn") == 0)
                                  ? tree->NearestQuery(measureSet[i], k)
                                  : tree->RangeQuery(measureSet[i], radius);
                delete r;
            }

            pm->ResetStatistics();
            tree->GetMetricEvaluator()->ResetStatistics();

            std::vector<double> timesMs;
            timesMs.reserve(measureSet.size());
            double resultSizeAcc = 0.0;

            for (size_t i = 0; i < measureSet.size(); i++) {
                const auto t0 = Clock::now();
                myResult *r = (std::strcmp(qtype, "knn") == 0)
                                  ? tree->NearestQuery(measureSet[i], k)
                                  : tree->RangeQuery(measureSet[i], radius);
                const auto t1 = Clock::now();
                const double ms =
                    std::chrono::duration<double, std::milli>(t1 - t0).count();
                const unsigned int resultSize = r->GetNumOfEntries();
                resultSizeAcc += static_cast<double>(resultSize);
                delete r;
                timesMs.push_back(ms);

                if (rawOs.is_open()) {
                    rawOs.setf(std::ios::fixed);
                    rawOs.precision(6);
                    rawOs << cfg.version << ',' << cfg.commit << ','
                          << cfg.datasetLabel << ',' << cfg.protocol << ','
                          << cfg.trainMode << ',' << cfg.queryDist << ','
                          << qtype << ',' << k << ','
                          << radius << ',' << rep << ',' << i << ',' << ms << ','
                          << resultSize << '\n';
                }
            }

            Row row;
            row.queryType = qtype;
            row.k = static_cast<long>(k);
            row.radius = radius;
            row.repetition = rep;
            row.queryCount = measureSet.size();
            row.constructionTimeMs = constructionTimeMs;
            row.optimizeTimeMs = optimizeTimeMs;
            row.totalQueryTimeMs = std::accumulate(timesMs.begin(), timesMs.end(), 0.0);
            row.meanQueryTimeMs = mean(timesMs);
            row.medianQueryTimeMs = median(timesMs);
            row.p95QueryTimeMs = percentile(timesMs, 0.95);
            row.stdQueryTimeMs = stddev(timesMs);
            row.totalDistanceCalculations =
                static_cast<double>(tree->GetMetricEvaluator()->GetDistanceCount());
            row.totalDiskAccesses = static_cast<double>(pm->GetReadCount());
            row.totalDiskWrites = static_cast<double>(pm->GetWriteCount());
            const double n = static_cast<double>(measureSet.size());
            row.meanDistanceCalculations = row.totalDistanceCalculations / n;
            row.meanDiskAccesses = row.totalDiskAccesses / n;
            row.constructionDistanceCalculations = constructionDist;
            row.constructionDiskAccesses = constructionReads;
            row.treeHeight = treeHeight;
            row.nodeCount = nodeCount;
            row.numObjects = numObjects;
            row.meanResultSize = resultSizeAcc / n;
            rows.push_back(row);

            std::cout << "  " << qtype << " k=" << k << " r=" << radius
                      << ": mean " << row.meanQueryTimeMs << " ms, "
                      << row.meanDistanceCalculations << " dist/query, "
                      << row.meanDiskAccesses << " reads/query\n";
        };

        if (doKnn)
            for (unsigned int k : cfg.kList) runBatch("knn", k, -1.0);
        if (doRange)
            for (double rad : cfg.radiusList) runBatch("range", 0, rad);

        // ---------------- correctness verification (untimed) -------------
        // Runs strictly AFTER every timed batch so it cannot perturb any
        // measurement, and only on the first repetition to keep runtime sane.
        if (cfg.verify && rep == 0) {
            std::cout << "  verifying against exact brute-force search...\n";
            for (Row &row : rows) {
                long ok = 0;
                for (size_t i = 0; i < measureSet.size(); i++) {
                    const bool isKnn = (row.queryType == "knn");
                    myResult *r =
                        isKnn ? tree->NearestQuery(measureSet[i],
                                                   static_cast<unsigned int>(row.k))
                              : tree->RangeQuery(measureSet[i], row.radius);

                    std::vector<double> got;
                    got.reserve(r->GetNumOfEntries());
                    for (unsigned int j = 0; j < r->GetNumOfEntries(); j++)
                        got.push_back(r->GetPair(j)->GetDistance());
                    std::sort(got.begin(), got.end());

                    const std::vector<double> want =
                        isKnn ? bruteForceKnnDistances(bfEval, objects, measureSet[i],
                                                       static_cast<unsigned int>(row.k))
                              : bruteForceRangeDistances(bfEval, objects,
                                                         measureSet[i], row.radius);

                    // Compare the sorted distance vectors rather than object
                    // identities: with ties, several distinct neighbour sets are
                    // equally correct, but the distances must match exactly.
                    bool match = (got.size() == want.size());
                    if (match) {
                        for (size_t j = 0; j < got.size(); j++) {
                            if (std::fabs(got[j] - want[j]) > 1e-9) { match = false; break; }
                        }
                    }
                    if (match) ok++;

                    if (nbrOs.is_open()) {
                        nbrOs.setf(std::ios::fixed);
                        for (unsigned int j = 0; j < r->GetNumOfEntries(); j++) {
                            nbrOs.precision(6);
                            nbrOs << cfg.version << ',' << cfg.datasetLabel << ','
                                  << cfg.protocol << ',' << cfg.queryDist << ','
                                  << row.queryType << ','
                                  << row.k << ',' << row.radius << ',' << i << ','
                                  << j << ",\""
                                  << r->GetPair(j)->GetObject()->GetName() << "\",";
                            nbrOs.precision(10);
                            nbrOs << r->GetPair(j)->GetDistance() << '\n';
                        }
                    }
                    delete r;
                }
                row.correctQueryCount = ok;
                row.correctResults =
                    (ok == static_cast<long>(measureSet.size())) ? 1 : 0;
                if (!row.correctResults) {
                    std::cerr << "  WARNING: " << row.queryType << " k=" << row.k
                              << " r=" << row.radius << ": only " << ok << "/"
                              << measureSet.size() << " queries matched brute force\n";
                }
            }
        } else {
            // Not verified in this repetition; -1 marks "not measured".
            for (Row &row : rows) {
                row.correctResults = -1;
                row.correctQueryCount = -1;
            }
        }

        // ---------------- tear down, then measure the index file ---------
        // The tree/page-manager destructors flush the header, so the file size
        // is only final once both are gone.
        delete tree;
        delete pm;

        double indexSize = 0.0;
        if (std::filesystem::exists(cfg.indexPath, ec))
            indexSize = static_cast<double>(std::filesystem::file_size(cfg.indexPath, ec));

        for (Row &row : rows) {
            row.indexSizeBytes = indexSize;
            writeRow(aggOs, cfg, row);
        }
        aggOs.flush();
        if (rawOs.is_open()) rawOs.flush();
        if (nbrOs.is_open()) nbrOs.flush();
    }

    for (TCity *o : objects) delete o;
    for (TCity *q : allQueries) delete q;

    std::cout << "\nDone.\n";
    return 0;
}
