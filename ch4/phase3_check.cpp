// ---------------------------------------------------------------------------
// phase3_check.cpp - verification program for Chapter 4, Phase 3.
//
// The trace is ENABLED here but nothing is relocated: this program only checks
// that what the trace reports is a faithful description of the traversal that
// actually happened. Everything Etapa 1 will later decide rests on these
// numbers, so they are worth proving before any page is written.
//
// Invariants checked, per query, for both range and k-NN:
//
//   T1  NodesEntered == page reads            (the tree reads one page per node)
//   T2  expanded edges + 1 == NodesEntered    (every node but the root was
//                                              reached through one expanded edge)
//   T3  IndexEntriesEntered <= IndexEntriesScanned
//   T4  IndexDistances <= evaluator delta, and the derived
//       LeafDistances = delta - IndexDistances is in [0, LeafEntriesScanned]
//   T5  every logged (Parent, Child) really is a parent/child pair, and the
//       logged Radius matches the parent entry
//   T6  range queries: every edge is Expanded (the traversal is depth-first)
//   T7  k-NN: TauTrace is non-increasing, QueuePopsDiscarded <= QueuePops, and
//       the final tau equals the result radius once k neighbours are known
//   T8  answers match brute force
//
// Build with -DST_SLIM_TRACE=1; see benchmark/scripts/build_ch4_check.sh.
// ---------------------------------------------------------------------------

#include <arboretum/stPlainDiskPageManager.h>
#include <arboretum/stSlimTree.h>
#include <arboretum/stSlimMetrics.h>

#include "ch4_common.h"

#include <cstdio>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#if !ST_SLIM_TRACE
#error "phase3_check.cpp requires -DST_SLIM_TRACE=1"
#endif

typedef stResult<TCity> myResult;
typedef stSlimTree<TCity, TCityDistanceEvaluator> mySlimTree;

using ch4::Checker;

// ---------------------------------------------------------------------------
// What the trace said about one query, plus what we measured independently.
// ---------------------------------------------------------------------------
struct QueryObs {
    long nodesEntered = 0;
    long leavesEntered = 0;
    long expandedEdges = 0;
    long totalEdges = 0;
    long indexScanned = 0;
    long indexEntered = 0;
    long leafScanned = 0;
    long indexDistances = 0;
    long evaluatorDelta = 0;
    long pageReads = 0;
    long tauUpdates = 0;
    long queuePops = 0;
    long queuePopsDiscarded = 0;
    double finalTau = 0.0;
    bool tauMonotone = true;
    bool allEdgesExpanded = true;
    std::vector<stSlimVisit> visits;
};

// ---------------------------------------------------------------------------
// Confirms that each logged edge is a real parent/child link with the radius
// the parent actually stores. Runs AFTER measurement, so its own page reads
// cannot pollute the observation.
// ---------------------------------------------------------------------------
static bool edgesAreReal(stPageManager *pm, const std::vector<stSlimVisit> &visits,
                         std::string &why) {
    // Group by parent so each parent page is read once.
    std::map<u_int32_t, std::vector<const stSlimVisit *> > byParent;
    for (const stSlimVisit &v : visits) byParent[v.Parent].push_back(&v);

    for (const auto &kv : byParent) {
        stPage *page = pm->GetPage(kv.first);
        if (page == NULL) {
            why = "parent page " + std::to_string(kv.first) + " does not exist";
            return false;
        }
        stSlimNode *node = stSlimNode::CreateNode(page);
        if (node->GetNodeType() != stSlimNode::INDEX) {
            why = "logged parent " + std::to_string(kv.first) + " is not an index node";
            delete node;
            pm->ReleasePage(page);
            return false;
        }
        stSlimIndexNode *parent = (stSlimIndexNode *)node;

        for (const stSlimVisit *v : kv.second) {
            bool found = false;
            for (u_int32_t e = 0; e < parent->GetNumberOfEntries(); e++) {
                if (parent->GetIndexEntry(e).PageID == v->Child) {
                    found = true;
                    if (parent->GetIndexEntry(e).Radius != v->Radius) {
                        why = "radius mismatch for child " + std::to_string(v->Child);
                        delete node;
                        pm->ReleasePage(page);
                        return false;
                    }
                    break;
                }
            }
            if (!found) {
                why = "child " + std::to_string(v->Child) + " is not an entry of parent " +
                      std::to_string(v->Parent);
                delete node;
                pm->ReleasePage(page);
                return false;
            }
        }
        delete node;
        pm->ReleasePage(page);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Runs one query with the trace armed and returns the observation.
// ---------------------------------------------------------------------------
static QueryObs runOne(mySlimTree *tree, stPlainDiskPageManager *pm, TCity *q,
                       bool knn, unsigned int k, double radius,
                       std::vector<double> &answerOut) {
    stSlimTrace trace;
    trace.Reset();

    const long d0 = static_cast<long>(tree->GetMetricEvaluator()->GetDistanceCount());
    const long r0 = static_cast<long>(pm->GetReadCount());

    stSlimActiveTrace = &trace;
    myResult *result = knn ? tree->NearestQuery(q, k) : tree->RangeQuery(q, radius);
    stSlimActiveTrace = 0;

    QueryObs o;
    o.evaluatorDelta =
        static_cast<long>(tree->GetMetricEvaluator()->GetDistanceCount()) - d0;
    o.pageReads = static_cast<long>(pm->GetReadCount()) - r0;

    o.nodesEntered = trace.NodesEntered;
    o.leavesEntered = trace.LeavesEntered;
    o.indexScanned = trace.IndexEntriesScanned;
    o.indexEntered = trace.IndexEntriesEntered;
    o.leafScanned = trace.LeafEntriesScanned;
    o.indexDistances = trace.IndexDistances;
    o.tauUpdates = trace.TauUpdates;
    o.queuePops = trace.QueuePops;
    o.queuePopsDiscarded = trace.QueuePopsDiscarded;
    o.finalTau = trace.FinalTau;
    o.totalEdges = static_cast<long>(trace.Visits.size());
    o.visits = trace.Visits;

    for (const stSlimVisit &v : trace.Visits) {
        if (v.Expanded) o.expandedEdges++;
        else o.allEdgesExpanded = false;
    }
    for (size_t i = 1; i < trace.TauTrace.size(); i++)
        if (trace.TauTrace[i] > trace.TauTrace[i - 1]) o.tauMonotone = false;

    answerOut = ch4::resultDistances(result);

    // The result radius must equal the final tau once k neighbours exist.
    if (knn && trace.TauUpdates > 0 && result->GetNumOfEntries() >= k) {
        const double maxd = result->GetMaximumDistance();
        if (std::fabs(maxd - trace.FinalTau) > 1e-12) o.finalTau = -1.0;  // flagged
    }

    delete result;
    return o;
}

int main(int argc, char **argv) {
    std::string dataset, queryFile;
    std::string indexDir = ".";
    unsigned int pageSize = 1024;
    unsigned int k = 10;
    double radius = 0.5;
    int queries = 40;

    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--dataset" && i + 1 < argc) dataset = argv[++i];
        else if (a == "--queryfile" && i + 1 < argc) queryFile = argv[++i];
        else if (a == "--index-dir" && i + 1 < argc) indexDir = argv[++i];
        else if (a == "--page-size" && i + 1 < argc)
            pageSize = static_cast<unsigned int>(std::stoul(argv[++i]));
        else if (a == "--k" && i + 1 < argc)
            k = static_cast<unsigned int>(std::stoul(argv[++i]));
        else if (a == "--radius" && i + 1 < argc) radius = std::stod(argv[++i]);
        else if (a == "--queries" && i + 1 < argc) queries = std::stoi(argv[++i]);
        else if (a == "--help" || a == "-h") {
            std::cout << "usage: " << argv[0]
                      << " --dataset <f> --queryfile <f> [--index-dir <d>]"
                         " [--page-size N] [--k N] [--radius R] [--queries N]\n";
            return 0;
        } else {
            std::cerr << "unknown option: " << a << "\n";
            return 2;
        }
    }
    if (dataset.empty() || queryFile.empty()) {
        std::cerr << "ERROR: --dataset and --queryfile are required\n";
        return 2;
    }

    std::vector<TCity *> objects = ch4::loadCities(dataset);
    std::vector<TCity *> queryPool = ch4::loadCities(queryFile);
    if (static_cast<int>(queryPool.size()) < queries)
        queries = static_cast<int>(queryPool.size());

    std::cout << "Loaded " << objects.size() << " objects, " << queryPool.size()
              << " query candidates\n";
    std::cout << "page size " << pageSize << ", k " << k << ", radius " << radius
              << ", queries " << queries << "\n\n";

    const std::string indexPath = indexDir + "/phase3.dat";
    std::error_code ec;
    std::filesystem::remove(indexPath, ec);

    stPlainDiskPageManager *pm =
        new stPlainDiskPageManager(indexPath.c_str(), pageSize);
    mySlimTree *tree = new mySlimTree(pm);
    for (TCity *o : objects) tree->Add(o);

    std::cout << "tree: height " << tree->GetHeight() << ", nodes "
              << tree->GetNodeCount() << ", objects " << tree->GetNumberOfObjects()
              << "\n\n";

    // A separate evaluator for brute force, so it never touches the tree counter.
    TCityDistanceEvaluator verifyEval;

    struct Totals {
        long t1 = 0, t2 = 0, t3 = 0, t4 = 0, t6 = 0, t7 = 0, t8 = 0;
        long nodes = 0, pruned = 0, idist = 0, ldist = 0, edges = 0, expanded = 0;
        long pops = 0, discarded = 0, tauUpdates = 0;
    };

    int failures = 0;

    for (int pass = 0; pass < 2; pass++) {
        const bool knn = (pass == 0);
        Totals tot;
        std::string edgeWhy;
        bool edgesOk = true;

        for (int i = 0; i < queries; i++) {
            std::vector<double> got;
            const QueryObs o =
                runOne(tree, pm, queryPool[i], knn, k, radius, got);

            const long leafDistances = o.evaluatorDelta - o.indexDistances;

            if (o.nodesEntered == o.pageReads) tot.t1++;
            if (o.expandedEdges + 1 == o.nodesEntered) tot.t2++;
            if (o.indexEntered <= o.indexScanned) tot.t3++;
            if (o.indexDistances <= o.evaluatorDelta && leafDistances >= 0 &&
                leafDistances <= o.leafScanned)
                tot.t4++;
            if (knn) {
                if (o.tauMonotone && o.queuePopsDiscarded <= o.queuePops &&
                    o.finalTau >= 0.0)
                    tot.t7++;
            } else {
                if (o.allEdgesExpanded) tot.t6++;
            }

            const std::vector<double> want =
                knn ? ch4::bruteKnn(verifyEval, objects, queryPool[i], k)
                    : ch4::bruteRange(verifyEval, objects, queryPool[i], radius);
            if (ch4::sameDistances(got, want)) tot.t8++;

            if (edgesOk && i < 5) {  // full edge validation on a sample
                edgesOk = edgesAreReal(pm, o.visits, edgeWhy);
            }

            tot.nodes += o.nodesEntered;
            tot.pruned += (o.indexScanned - o.indexEntered);
            tot.idist += o.indexDistances;
            tot.ldist += leafDistances;
            tot.edges += o.totalEdges;
            tot.expanded += o.expandedEdges;
            tot.pops += o.queuePops;
            tot.discarded += o.queuePopsDiscarded;
            tot.tauUpdates += o.tauUpdates;
        }

        const double n = static_cast<double>(queries);
        std::cout << "=== " << (knn ? "k-NN" : "range") << " over " << queries
                  << " queries ===\n";
        std::cout << "  nodes entered / query    : " << tot.nodes / n << "\n";
        std::cout << "  subtrees pruned / query  : " << tot.pruned / n << "\n";
        std::cout << "  index distances / query  : " << tot.idist / n << "\n";
        std::cout << "  leaf distances / query   : " << tot.ldist / n << "\n";
        std::cout << "  edges logged / query     : " << tot.edges / n
                  << "  (expanded " << tot.expanded / n << ")\n";
        if (knn) {
            std::cout << "  queue pops / query       : " << tot.pops / n
                      << "  (discarded after tau " << tot.discarded / n << ")\n";
            std::cout << "  tau updates / query      : " << tot.tauUpdates / n << "\n";
        }

        Checker check;
        check(tot.t1 == queries, "T1 NodesEntered == page reads");
        check(tot.t2 == queries, "T2 expanded edges + 1 == NodesEntered");
        check(tot.t3 == queries, "T3 IndexEntriesEntered <= IndexEntriesScanned");
        check(tot.t4 == queries, "T4 index + leaf distances reconcile with evaluator");
        check(edgesOk, std::string("T5 logged edges are real parent/child pairs") +
                           (edgesOk ? "" : ": " + edgeWhy));
        if (knn) {
            check(tot.t7 == queries, "T7 tau non-increasing, pops consistent, "
                                     "final tau == result radius");
        } else {
            check(tot.t6 == queries, "T6 every edge of a range query is expanded");
        }
        check(tot.t8 == queries, "T8 answers match brute force");
        failures += check.failures;
        std::cout << "\n";
    }

    delete tree;
    delete pm;
    ch4::deleteAll(objects);
    ch4::deleteAll(queryPool);

    std::cout << (failures == 0 ? "PHASE 3 OK" : "PHASE 3 FAILED") << " ("
              << failures << " failing check(s))\n";
    return failures == 0 ? 0 : 1;
}
