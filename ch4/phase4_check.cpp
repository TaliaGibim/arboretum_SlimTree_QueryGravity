// ---------------------------------------------------------------------------
// phase4_check.cpp - verification program for Chapter 4, Phase 4.
//
// Etapa 1 actually MOVES objects here, so this program is about correctness,
// not performance. It adapts the tree with one query sequence and then proves
// that the structure is still a valid Slim-tree and still answers exactly.
//
// Invariants checked:
//
//   E1  at least one object was relocated (otherwise nothing is being tested)
//   E2  DestinationRadiusGrowthEvents == 0        <-- hypothesis H2
//   E3  node count and object count are unchanged (no page was created,
//       disposed, or lost - Etapa 1 must be page-count preserving)
//   E4  for every leaf, the radius stored in its parent equals the leaf's own
//       GetMinimumRadius(), and NEntries equals its entry count
//   E5  for every index entry, Radius >= max over children of
//       (child.Distance + child.Radius)          <-- the covering invariant
//       as visible one level up
//   E6  every leaf has exactly ONE entry at distance 0 (its representative):
//       a second one would be picked up by GetRepresentativeEntry()
//   E7  Definition 1 in full: recomputing d(x, O) for every object against
//       every ancestor representative never exceeds that ancestor's radius
//   E8  every measured query still matches brute force, for range and k-NN
//   E9  adaptation is genuinely OFF during measurement (no moves happen)
//
// Build and run:
//   bash arboretum_SlimTree_QueryGravity/ch4/build_ch4.sh phase4_check
// ---------------------------------------------------------------------------

#include <arboretum/stPlainDiskPageManager.h>
#include <arboretum/stSlimTree.h>
#include <arboretum/stSlimEtapa1.h>
#include <arboretum/stSlimMetrics.h>

#include "ch4_common.h"

#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#if !ST_SLIM_TRACE
#error "phase4_check.cpp requires -DST_SLIM_TRACE=1"
#endif

typedef stResult<TCity> myResult;
typedef stSlimTree<TCity, TCityDistanceEvaluator> mySlimTree;
typedef stSlimEtapa1<TCity, TCityDistanceEvaluator> myEtapa1;
typedef stSlimMetrics<TCity, TCityDistanceEvaluator> myMetrics;

// ---------------------------------------------------------------------------
// Structural validation, walking the tree read-only through the page manager.
// ---------------------------------------------------------------------------
struct Violations {
    long radiusMismatch = 0;   // E4a; E4b is a separate pass, see checkNEntries
    long coverageBreach = 0;   // E5
    long badRepresentative = 0;  // E6
    long definition1Breach = 0;  // E7
    std::string firstDetail;

    void note(const std::string &d) {
        if (firstDetail.empty()) firstDetail = d;
    }
    long total() const {
        return radiusMismatch + coverageBreach + badRepresentative +
               definition1Breach;
    }
};

// Walks one subtree. `ancestors` carries (representative, radius) of every
// ancestor, so Definition 1 can be checked in full when `deep` is set.
static void validate(stPageManager *pm, TCityDistanceEvaluator &eval,
                     u_int32_t pageID, bool deep,
                     std::vector<std::pair<TCity, double> > &ancestors,
                     Violations &v) {
    stPage *page = pm->GetPage(pageID);
    if (page == NULL) return;
    stSlimNode *node = stSlimNode::CreateNode(page);

    if (node->GetNodeType() == stSlimNode::INDEX) {
        stSlimIndexNode *idx = (stSlimIndexNode *)node;
        const u_int32_t n = idx->GetNumberOfEntries();

        // Snapshot what we need, then release before recursing.
        std::vector<u_int32_t> childPage(n);
        std::vector<double> childRadius(n);
        std::vector<double> childDistance(n);
        std::vector<TCity> childRep(n);
        for (u_int32_t i = 0; i < n; i++) {
            childPage[i] = idx->GetIndexEntry(i).PageID;
            childRadius[i] = idx->GetIndexEntry(i).Radius;
            childDistance[i] = idx->GetIndexEntry(i).Distance;
            childRep[i].Unserialize(idx->GetObject(i), idx->GetObjectSize(i));
        }
        delete node;
        pm->ReleasePage(page);

        for (u_int32_t i = 0; i < n; i++) {
            // E4/E5 need the child, so read it once here.
            stPage *cp = pm->GetPage(childPage[i]);
            if (cp == NULL) continue;
            stSlimNode *cn = stSlimNode::CreateNode(cp);

            if (cn->GetNodeType() == stSlimNode::LEAF) {
                stSlimLeafNode *leaf = (stSlimLeafNode *)cn;
                const double own = leaf->GetMinimumRadius();
                if (own != childRadius[i]) {
                    v.radiusMismatch++;
                    v.note("leaf " + std::to_string(childPage[i]) + " radius " +
                           std::to_string(own) + " but parent says " +
                           std::to_string(childRadius[i]));
                }
                // E6: exactly one entry at distance zero.
                long zeros = 0;
                for (u_int32_t e = 0; e < leaf->GetNumberOfEntries(); e++)
                    if (leaf->GetLeafEntry(e).Distance == 0.0) zeros++;
                if (zeros != 1) {
                    v.badRepresentative++;
                    v.note("leaf " + std::to_string(childPage[i]) + " has " +
                           std::to_string(zeros) + " entries at distance 0");
                }
            } else {
                stSlimIndexNode *ci = (stSlimIndexNode *)cn;
                // E5: the parent radius must cover child ball extents.
                double need = 0.0;
                for (u_int32_t e = 0; e < ci->GetNumberOfEntries(); e++) {
                    const double reach = ci->GetIndexEntry(e).Distance +
                                         ci->GetIndexEntry(e).Radius;
                    if (reach > need) need = reach;
                }
                if (need > childRadius[i] + 1e-9) {
                    v.coverageBreach++;
                    v.note("index " + std::to_string(childPage[i]) +
                           " needs radius " + std::to_string(need) +
                           " but parent says " + std::to_string(childRadius[i]));
                }
            }
            delete cn;
            pm->ReleasePage(cp);

            ancestors.push_back(std::make_pair(childRep[i], childRadius[i]));
            validate(pm, eval, childPage[i], deep, ancestors, v);
            ancestors.pop_back();
        }
        return;
    }

    // Leaf: Definition 1 against every ancestor.
    stSlimLeafNode *leaf = (stSlimLeafNode *)node;
    if (deep) {
        for (u_int32_t e = 0; e < leaf->GetNumberOfEntries(); e++) {
            TCity x;
            x.Unserialize(leaf->GetObject(e), leaf->GetObjectSize(e));
            for (size_t a = 0; a < ancestors.size(); a++) {
                const double d = eval.GetDistance(x, ancestors[a].first);
                if (d > ancestors[a].second + 1e-9) {
                    v.definition1Breach++;
                    v.note("object at leaf " + std::to_string(pageID) +
                           " is " + std::to_string(d) + " from an ancestor of radius " +
                           std::to_string(ancestors[a].second));
                }
            }
        }
    }
    delete node;
    pm->ReleasePage(page);
}

// The NEntries check needs the leaf and the parent entry together; doing it
// inline above would have required carrying the parent node, so it is a small
// separate pass.
static long checkNEntries(stPageManager *pm, u_int32_t pageID,
                          std::string &detail) {
    long bad = 0;
    stPage *page = pm->GetPage(pageID);
    if (page == NULL) return 0;
    stSlimNode *node = stSlimNode::CreateNode(page);
    if (node->GetNodeType() != stSlimNode::INDEX) {
        delete node;
        pm->ReleasePage(page);
        return 0;
    }
    stSlimIndexNode *idx = (stSlimIndexNode *)node;
    const u_int32_t n = idx->GetNumberOfEntries();
    std::vector<u_int32_t> childPage(n);
    std::vector<u_int32_t> childNEntries(n);
    for (u_int32_t i = 0; i < n; i++) {
        childPage[i] = idx->GetIndexEntry(i).PageID;
        childNEntries[i] = idx->GetIndexEntry(i).NEntries;
    }
    delete node;
    pm->ReleasePage(page);

    for (u_int32_t i = 0; i < n; i++) {
        stPage *cp = pm->GetPage(childPage[i]);
        if (cp == NULL) continue;
        stSlimNode *cn = stSlimNode::CreateNode(cp);
        if (cn->GetNodeType() == stSlimNode::LEAF) {
            const u_int32_t have = cn->GetNumberOfEntries();
            if (have != childNEntries[i]) {
                bad++;
                if (detail.empty())
                    detail = "leaf " + std::to_string(childPage[i]) + " has " +
                             std::to_string(have) + " entries but parent says " +
                             std::to_string(childNEntries[i]);
            }
        }
        delete cn;
        pm->ReleasePage(cp);
        bad += checkNEntries(pm, childPage[i], detail);
    }
    return bad;
}

int main(int argc, char **argv) {
    std::string dataset, queryFile;
    std::string indexDir = ".";
    unsigned int pageSize = 1024;
    unsigned int k = 10;
    double radius = 0.5;
    int adaptQueries = 150;
    int measureQueries = 60;
    bool deep = true;

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
        else if (a == "--adapt-queries" && i + 1 < argc)
            adaptQueries = std::stoi(argv[++i]);
        else if (a == "--measure-queries" && i + 1 < argc)
            measureQueries = std::stoi(argv[++i]);
        else if (a == "--no-deep") deep = false;
        else if (a == "--help" || a == "-h") {
            std::cout << "usage: " << argv[0]
                      << " --dataset <f> --queryfile <f> [--index-dir <d>]"
                         " [--page-size N] [--k N] [--radius R]"
                         " [--adapt-queries N] [--measure-queries N] [--no-deep]\n";
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
    std::vector<TCity *> pool = ch4::loadCities(queryFile);

    // Disjoint adapt / measure split, as section 4.5 requires: one sequence
    // adapts the tree, a DIFFERENT one measures it. Odd indices adapt, even
    // indices measure, so both are drawn from the same distribution.
    std::vector<TCity *> adaptSet, measureSet;
    for (size_t i = 0; i < pool.size(); i++)
        (i % 2 ? adaptSet : measureSet).push_back(pool[i]);
    if ((int)adaptSet.size() > adaptQueries) adaptSet.resize(adaptQueries);
    if ((int)measureSet.size() > measureQueries) measureSet.resize(measureQueries);

    std::cout << "Loaded " << objects.size() << " objects; adapt "
              << adaptSet.size() << ", measure " << measureSet.size() << "\n";
    std::cout << "page size " << pageSize << ", k " << k << ", radius " << radius
              << "\n\n";

    const std::string indexPath = indexDir + "/phase4.dat";
    std::error_code ec;
    std::filesystem::remove(indexPath, ec);

    stPlainDiskPageManager *pm =
        new stPlainDiskPageManager(indexPath.c_str(), pageSize);
    mySlimTree *tree = new mySlimTree(pm);
    for (TCity *o : objects) tree->Add(o);

    const long nodesBefore = tree->GetNodeCount();
    const long objectsBefore = tree->GetNumberOfObjects();
    const myMetrics::Result before = myMetrics::Collect(pm, tree->GetRootPageID());

    std::cout << "before: height " << tree->GetHeight() << ", nodes " << nodesBefore
              << ", objects " << objectsBefore << ", overlap ratio "
              << before.OverlapRatio << ", mean leaf radius "
              << before.MeanLeafRadius << "\n\n";

    // --- adaptation pass ---------------------------------------------------
    myEtapa1::Config cfg;  // all defaults: the thesis as written
    myEtapa1 etapa1(pm, cfg);

    stSlimTrace trace;
    for (size_t i = 0; i < adaptSet.size(); i++) {
        trace.Reset();
        stSlimActiveTrace = &trace;
        myResult *r = tree->NearestQuery(adaptSet[i], k);
        stSlimActiveTrace = 0;
        delete r;
        etapa1.ApplyAfterQuery(trace);
    }

    const myEtapa1::Stats &st = etapa1.GetStats();
    std::cout << "=== adaptation over " << adaptSet.size() << " k-NN queries ===\n";
    std::cout << "  objects relocated        : " << st.ObjectsRelocated << "\n";
    std::cout << "  queries that moved       : " << st.QueriesWithWork << "\n";
    std::cout << "  sibling pairs considered : " << st.PairsConsidered << "\n";
    std::cout << "  skipped by bound (free)  : " << st.SkippedByBound << "\n";
    std::cout << "  rejected by coverage 4.3 : " << st.RejectedByCoverage << "\n";
    std::cout << "  rejected by capacity 4.5 : " << st.RejectedByCapacity << "\n";
    std::cout << "  rejected representative  : " << st.RejectedByRepresentative << "\n";
    std::cout << "  rejected duplicate       : " << st.RejectedByDuplicate << "\n";
    std::cout << "  rejected underflow       : " << st.RejectedByUnderflow << "\n";
    std::cout << "  rejected by gain 4.4     : " << st.RejectedByGain << "\n";
    std::cout << "  reverse relocations      : " << st.ReverseRelocations << "\n";
    std::cout << "  total radius shrink      : " << st.RadiusShrinkTotal << "\n";
    std::cout << "  cost: " << st.Distances << " distances, " << st.PageReads
              << " reads, " << st.PageWrites << " writes\n";
    std::cout << "        of which " << st.PairDistances << " per sibling pair, "
              << st.CandidateDistances << " per candidate\n";
    const long decided = st.SkippedByBound + st.CandidateDistances;
    const double freeFrac =
        decided > 0 ? (double)st.SkippedByBound / (double)decided : 0.0;
    std::cout << "  candidates decided for free  : " << (freeFrac * 100.0)
              << "%  (triangle bound, zero distances)\n\n";

    const myMetrics::Result after = myMetrics::Collect(pm, tree->GetRootPageID());
    std::cout << "after : nodes " << tree->GetNodeCount() << ", objects "
              << tree->GetNumberOfObjects() << ", overlap ratio "
              << after.OverlapRatio << ", mean leaf radius " << after.MeanLeafRadius
              << "\n\n";

    // --- structural validation --------------------------------------------
    TCityDistanceEvaluator verifyEval;
    Violations v;
    std::vector<std::pair<TCity, double> > ancestors;
    validate(pm, verifyEval, tree->GetRootPageID(), deep, ancestors, v);
    std::string nDetail;
    const long nEntriesBad = checkNEntries(pm, tree->GetRootPageID(), nDetail);

    // --- measurement pass, adaptation OFF ---------------------------------
    const long movesBeforeMeasure = st.ObjectsRelocated;
    long knnOk = 0, rangeOk = 0;
    for (size_t i = 0; i < measureSet.size(); i++) {
        myResult *r = tree->NearestQuery(measureSet[i], k);
        if (ch4::sameDistances(ch4::resultDistances(r),
                               ch4::bruteKnn(verifyEval, objects, measureSet[i], k)))
            knnOk++;
        delete r;

        myResult *rr = tree->RangeQuery(measureSet[i], radius);
        if (ch4::sameDistances(
                ch4::resultDistances(rr),
                ch4::bruteRange(verifyEval, objects, measureSet[i], radius)))
            rangeOk++;
        delete rr;
    }

    std::cout << "=== checks ===\n";
    ch4::Checker check;
    check(st.ObjectsRelocated > 0, "E1 at least one object was relocated");
    check(st.DestinationRadiusGrowthEvents == 0,
          "E2 destination radius never grew (H2)");
    check(tree->GetNodeCount() == nodesBefore &&
              tree->GetNumberOfObjects() == objectsBefore &&
              after.TotalNodes == before.TotalNodes &&
              after.Objects == before.Objects,
          "E3 node and object counts unchanged");
    check(v.radiusMismatch == 0,
          "E4a every leaf radius matches its parent entry" +
              (v.radiusMismatch ? ": " + v.firstDetail : std::string()));
    check(nEntriesBad == 0, "E4b every leaf NEntries matches its parent entry" +
                                (nEntriesBad ? ": " + nDetail : std::string()));
    check(v.coverageBreach == 0, "E5 index radii cover their children");
    check(v.badRepresentative == 0,
          "E6 every leaf has exactly one entry at distance 0");
    if (deep)
        check(v.definition1Breach == 0,
              "E7 Definition 1 holds for every object and ancestor");
    check(knnOk == (long)measureSet.size(), "E8a k-NN answers match brute force");
    check(rangeOk == (long)measureSet.size(), "E8b range answers match brute force");
    check(st.ObjectsRelocated == movesBeforeMeasure,
          "E9 no relocation happened during measurement");

    delete tree;
    delete pm;
    ch4::deleteAll(objects);
    ch4::deleteAll(pool);
    return check.report();
}
